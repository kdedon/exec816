/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * serial.device: the Z8030 SCC lines.  Upstream has no RS-232 device, so
 * the model is classic Amiga serial.device; docs/internals.md gives the
 * contract and the departures.  Units are kboot's bi_serial line numbers.
 *
 * Each SCC supplies status in its vector (WR2 base, WR9 VIS): per channel
 * Tx, Ext/Status, Rx and Special at base+0..6 for channel B (the lower
 * address) and base+8..14 for channel A.  One handler per chip decodes the
 * vector.  Receive is interrupt driven into a per-unit ring.  Transmit is
 * one pump, run from BeginIO, the Tx interrupt and the tick: it loads
 * bytes while the transmitter buffer is empty, at most TXBURST per call,
 * so VI-off time stays bounded where Tx Buffer Empty never clears.
 *
 * Unit 0 is the kernel's polled console line.  Its line parameters stay
 * as the ROM set them; its interrupts are enabled only while the unit is
 * open, so con_getc sees input whenever it is closed.
 *
 * Open and Close run under OpenDevice's Forbid.  BeginIO and AbortIO run
 * unlocked: queue and ring state shared with the handlers is changed with
 * VI off, and SETPARAMS and RESET, which reprogram the unit and swap its
 * ring, hold Forbid so no other Task queues a request meanwhile.
 */
#include "kernel.h"
#include <devices/serial.h>

/* Register n of a channel is at base + 2n + 1. */
#define RR0		0x01
#define RR1		0x03
#define RR8		0x11
#define RR12		0x19
#define RR13		0x1b
#define WR0		0x01
#define WR1		0x03
#define WR2		0x05
#define WR3		0x07
#define WR4		0x09
#define WR5		0x0b
#define WR8		0x11
#define WR9		0x13
#define WR10		0x15
#define WR11		0x17
#define WR12		0x19
#define WR13		0x1b
#define WR14		0x1d
#define WR15		0x1f

/* WR0 commands. */
#define RESEXT		0x10	/* reset Ext/Status interrupts */
#define RESTXI		0x28	/* reset Tx interrupt pending */
#define ERRESET		0x30	/* error reset */
#define RESIUS		0x38	/* reset highest IUS */

/* WR1. */
#define EXTIE		0x01
#define TXIE		0x02
#define RXALL		0x10	/* Rx on every character or special condition */

/* WR3, WR4, WR5. */
#define RXEN		0x01
#define X16CLK		0x40
#define STOP1		0x04
#define STOP2		0x0c
#define PARON		0x01
#define PAREVEN		0x02
#define TXEN		0x08
#define RTS		0x02
#define DTR		0x80
#define SENDBRK		0x10
#define CONWR5		0xea	/* the ROM's console: DTR, 8 bits, Tx, RTS */

#define NRZ		0x00	/* WR10 */
#define BRGCLK		0x56	/* WR11: Rx and Tx clocks from the BRG */
#define BRGOFF		0x02	/* WR14: BRG from PCLK, stopped */
#define BRGON		0x03	/* WR14: BRG from PCLK, running */
#define BREAKIE		0x80	/* WR15 */
#define MIEVIS		0x09	/* WR9: master enable, status in vector */

/* RR0, RR1. */
#define RXAVAIL		0x01
#define TXEMPTY		0x04
#define DCD		0x08
#define CTS		0x20
#define BREAKST		0x80
#define ALLSENT		0x01
#define PARERR		0x10
#define OVERRUN		0x20
#define FRAMERR		0x40

#define SCCWAIT		40000	/* All Sent polls before giving up */
#define TXBURST		32	/* bytes per pump */
#define RXBURST		8	/* bytes per receive interrupt */
#define NCHIP		3
#define SCCVEC(c)	(INTV_SCC + ((c) << 4))
#define TICKUS		10000L

/* su_Flags. */
#define SUF_PRESENT	0x01
#define SUF_EXCL	0x02	/* opened without SERF_SHARED */
#define SUF_BRK		0x04	/* a break is on the line */
#define SUF_CONSOLE	0x08	/* the kernel console line */

/* The ROM's console is on line 0. */
#define CONLINE		0

/* Parameters a unit keeps; the SETPARAMS fields. */
struct SerUnit {
	struct Unit su_Unit;
	struct List su_Reads;
	struct List su_Writes;	/* CMD_WRITE and SDCMD_BREAK, in order */
	UBYTE *su_Buf;		/* receive ring, while open */
	UWORD su_Size;
	UWORD su_Head;
	UWORD su_Tail;
	UWORD su_Count;
	UWORD su_Base;
	UWORD su_Flags;
	UWORD su_Status;	/* latched SERST_ bits */
	UWORD su_TC;
	BYTE su_Err;		/* latched receive error for the next read */
	UBYTE su_WR5;
	UBYTE su_RMask;
	UBYTE su_ReadLen;
	UBYTE su_WriteLen;
	UBYTE su_StopBits;
	UBYTE su_SerFlags;
	UBYTE su_Pad;
	ULONG su_BrkEnd;	/* tick that ends the break on the line */
	ULONG su_Baud;
	ULONG su_CtlChar;
	ULONG su_RBufLen;
	ULONG su_BrkTime;
	struct IOTArray su_Term;
};

struct SerBase {
	struct Device sb_Device;
	struct Interrupt sb_Int[NCHIP];
	struct Interrupt sb_Tick;
};

static struct {
	struct JumpVec lv_Vec[6];
	struct SerBase lv_Base;
} serbaselib;
#define serbase serbaselib.lv_Base
/* Units in channel pairs, A then B, allocated at boot for each chip with a
 * line present; NULL for an absent chip. */
static struct SerUnit *serchip[NCHIP];
static UWORD serport[SER_UNITS] = {
	0x0100, 0x0120, 0x0300, 0x0320, 0x0380, 0x03a0
};
static char sername[] = SERIALNAME;
static ULONG pclk;

/* After every queued request has run; timer.device's server is at 100. */
#define SERTICKPRI	90

#define UNIT(ior)	((struct SerUnit *)(ior)->io_Unit)
#define EXT(ior)	((struct IOExtSer *)(ior))
#define STD(ior)	((struct IOStdReq *)(ior))
#define FIRST(l)	((l)->lh_Head->ln_Succ != NULL ? \
			    (struct IORequest *)(l)->lh_Head : \
			    (struct IORequest *)NULL)

/* Baud-rate time constant, PCLK/(32*baud) - 2, or -1 when the rate is out
 * of the generator's range. */
static long
tcof(baud)
ULONG baud;
{
	ULONG q;

	if (baud == 0)
		return (-1L);
	q = pclk / (baud * 32L);
	if (q < 2 || q > 0xFFFFL + 2)
		return (-1L);
	return ((long)(q - 2));
}

/* WR3/WR5 bits-per-character code for 5-8 bits. */
static int
bitcode(n)
int n;
{
	switch (n) {
	case 5:
		return (0);
	case 6:
		return (2);
	case 7:
		return (1);
	}
	return (3);
}

static void
allsent(b)
UWORD b;
{
	register unsigned n;

	for (n = SCCWAIT; n != 0 && (inb(b + RR1) & ALLSENT) == 0; n--)
		;
}

/* Program a non-console line from its parameters.  The last character
 * leaves first: a reprogram while shifting garbles it. */
static void
program(u)
register struct SerUnit *u;
{
	register UWORD b;
	int wr4, s;

	b = u->su_Base;
	wr4 = X16CLK | (u->su_StopBits == 2 ? STOP2 : STOP1);
	if (u->su_SerFlags & SERF_PARTY_ON)
		wr4 |= PARON | ((u->su_SerFlags & SERF_PARTY_ODD) ? 0 : PAREVEN);
	s = intoff();
	u->su_WR5 = (u->su_WR5 & (DTR | RTS | SENDBRK)) | TXEN |
	    (bitcode(u->su_WriteLen) << 5);
	allsent(b);
	outb(b + WR4, wr4);
	outb(b + WR3, (bitcode(u->su_ReadLen) << 6) | RXEN);
	outb(b + WR5, u->su_WR5);
	outb(b + WR14, BRGOFF);
	outb(b + WR12, u->su_TC & 0xff);
	outb(b + WR13, (u->su_TC >> 8) & 0xff);
	outb(b + WR14, BRGON);
	intrest(s);
	u->su_RMask = (1 << u->su_ReadLen) - 1;
}

static void
complete(ior, err)
register struct IORequest *ior;
int err;
{
	ior->io_Error = err;
	xTermIO(ior);
}

static int
isterm(u, c)
register struct SerUnit *u;
register int c;
{
	register UBYTE *t;
	register int i;

	t = (UBYTE *)&u->su_Term;
	for (i = 0; i < 8; i++)
		if (t[i] == c)
			return (1);
	return (0);
}

/* Move ring bytes into ior; TRUE when it is done.  VI off. */
static int
rxfill(u, ior)
register struct SerUnit *u;
struct IORequest *ior;
{
	register struct IOStdReq *r;
	register int c;
	ULONG act;
	int done;

	r = STD(ior);
	act = r->io_Actual;
	done = 0;
	while (act < r->io_Length && u->su_Count != 0) {
		c = u->su_Buf[u->su_Head];
		if (++u->su_Head == u->su_Size)
			u->su_Head = 0;
		u->su_Count--;
		r->io_Data[(UWORD)act] = c;
		act++;
		if ((u->su_SerFlags & SERF_EOFMODE) && isterm(u, c)) {
			done = 1;
			break;
		}
	}
	r->io_Actual = act;
	return (done || act >= r->io_Length);
}

/* Complete the reads the ring can satisfy.  VI off. */
static void
rxserve(u)
register struct SerUnit *u;
{
	register struct IORequest *ior;
	int err;

	while ((ior = FIRST(&u->su_Reads)) != NULL && rxfill(u, ior)) {
		xRemove(&ior->io_Message.mn_Node);
		err = u->su_Err;
		u->su_Err = 0;
		complete(ior, err);
	}
}

static void
rxlatch(u, err)
register struct SerUnit *u;
int err;
{
	if (err == SerErr_BufOverflow)
		u->su_Status |= SERST_OVERRUN;
	if (u->su_Err == 0)
		u->su_Err = err;
}

static void
rxirq(u)
register struct SerUnit *u;
{
	register UWORD b;
	register int c, n;

	b = u->su_Base;
	for (n = 0; n < RXBURST && (inb(b + RR0) & RXAVAIL) != 0; n++) {
		c = inb(b + RR8) & u->su_RMask;
		if (u->su_Buf == NULL)
			continue;
		if (u->su_Count == u->su_Size) {
			rxlatch(u, SerErr_BufOverflow);
			continue;
		}
		u->su_Buf[u->su_Tail] = c;
		if (++u->su_Tail == u->su_Size)
			u->su_Tail = 0;
		u->su_Count++;
	}
	rxserve(u);
}

/* A write is done when its bytes are loaded; io_Length -1 means up to the
 * NUL. */
static int
wdone(r)
register struct IOStdReq *r;
{
	if (r->io_Length == 0xFFFFFFFFL)
		return (r->io_Data[(UWORD)r->io_Actual] == 0);
	return (r->io_Actual >= r->io_Length);
}

static ULONG
brkticks(u)
struct SerUnit *u;
{
	return ((u->su_BrkTime + TICKUS - 1) / TICKUS + 1);
}

/* Drop a break from the line.  VI off. */
static void
brkoff(u)
register struct SerUnit *u;
{
	u->su_WR5 &= ~SENDBRK;
	outb(u->su_Base + WR5, u->su_WR5);
	u->su_Flags &= ~SUF_BRK;
}

/* Load bytes while the transmitter takes them; complete finished writes
 * and start a queued break once the line is idle.  Returns the bytes
 * loaded.  VI off. */
static int
txpump(u)
register struct SerUnit *u;
{
	register struct IORequest *ior;
	register struct IOStdReq *r;
	register UWORD b;
	int n;

	b = u->su_Base;
	n = (u->su_Flags & SUF_CONSOLE) ? kcontx() : 0;
	while ((ior = FIRST(&u->su_Writes)) != NULL) {
		if (ior->io_Command == SDCMD_BREAK) {
			if ((u->su_Flags & SUF_BRK) == 0 &&
			    (inb(b + RR1) & ALLSENT) != 0) {
				u->su_WR5 |= SENDBRK;
				outb(b + WR5, u->su_WR5);
				u->su_BrkEnd = TickCount + brkticks(u);
				u->su_Flags |= SUF_BRK;
			}
			break;
		}
		r = STD(ior);
		if (wdone(r)) {
			xRemove(&ior->io_Message.mn_Node);
			complete(ior, 0);
			continue;
		}
		if (n >= TXBURST || (inb(b + RR0) & TXEMPTY) == 0)
			break;
		outb(b + WR8, r->io_Data[(UWORD)r->io_Actual]);
		r->io_Actual++;
		n++;
	}
	return (n);
}

/* One handler per chip; is_Data is the chip's unit pair.  SCCVEC is a
 * multiple of 16, so the low nibble is the status the chip put in. */
static int
sccint(ub, id)
struct SerUnit *ub;
UWORD id;
{
	register struct SerUnit *u;
	register UWORD b;
	int off, rr;

	off = id & 0x0f;
	u = ub + (off >> 3);
	b = u->su_Base;
	switch ((off >> 1) & 3) {
	case 0:
		if (txpump(u) == 0)
			outb(b + WR0, RESTXI);
		break;
	case 1:
		rr = inb(b + RR0);
		outb(b + WR0, RESEXT);
		if (rr & BREAKST)
			u->su_Status |= SERST_READBRK;
		break;
	case 2:
		rxirq(u);
		break;
	default:
		rr = inb(b + RR1);
		if (rr & (PARERR | OVERRUN | FRAMERR)) {
			inb(b + RR8);
			outb(b + WR0, ERRESET);
			rxlatch(u, (rr & OVERRUN) ? SerErr_BufOverflow :
			    (rr & FRAMERR) ? SerErr_LineErr : SerErr_ParityErr);
		}
		break;
	}
	outb(b + WR0, RESIUS);
	return (0);
}

/* Ends breaks that are due and pumps every open line with writes queued:
 * the transmit fallback where a Tx interrupt does not come. */
static int
sertick(data, n)
APTR data;
UWORD n;
{
	register struct SerUnit *u;
	register struct IORequest *ior;
	int i;

	for (i = 0; i < SER_UNITS; i++) {
		if ((u = serchip[i >> 1]) == NULL)
			continue;
		u += i & 1;
		if ((ior = FIRST(&u->su_Writes)) == NULL) {
			if (u->su_Flags & SUF_CONSOLE)
				kcontx();
			continue;
		}
		if ((u->su_Flags & SUF_BRK) &&
		    (long)(TickCount - u->su_BrkEnd) >= 0) {
			brkoff(u);
			xRemove(&ior->io_Message.mn_Node);
			complete(ior, 0);
		}
		txpump(u);
	}
	return (0);
}

static void
abortlist(u, l)
register struct SerUnit *u;
struct List *l;
{
	register struct IORequest *ior;

	while ((ior = FIRST(l)) != NULL) {
		if (l == &u->su_Writes && (u->su_Flags & SUF_BRK))
			brkoff(u);
		xRemove(&ior->io_Message.mn_Node);
		complete(ior, IOERR_ABORTED);
	}
}

static void
flush(u)
struct SerUnit *u;
{
	int s;

	s = intoff();
	abortlist(u, &u->su_Reads);
	abortlist(u, &u->su_Writes);
	intrest(s);
}

static void
clear(u)
register struct SerUnit *u;
{
	register UWORD b;
	int s, n;

	b = u->su_Base;
	s = intoff();
	for (n = 0; n < RXBURST && (inb(b + RR0) & RXAVAIL) != 0; n++)
		inb(b + RR8);
	u->su_Head = u->su_Tail = u->su_Count = 0;
	u->su_Err = 0;
	u->su_Status = 0;
	intrest(s);
}

static void
query(u, x)
register struct SerUnit *u;
register struct IOExtSer *x;
{
	register UWORD st;
	int rr;

	rr = inb(u->su_Base + RR0);
	st = u->su_Status;
	if (rr & CTS)
		st |= SERST_CTS;
	if (rr & DCD)
		st |= SERST_CD;
	if (u->su_WR5 & RTS)
		st |= SERST_RTS;
	if (u->su_WR5 & DTR)
		st |= SERST_DTR;
	if (u->su_Flags & SUF_BRK)
		st |= SERST_WROTEBRK;
	x->io_Status = st;
	x->IOSer.io_Actual = u->su_Count;
}

/* The unit's parameters into a request. */
static void
fill(u, x)
register struct SerUnit *u;
register struct IOExtSer *x;
{
	x->io_CtlChar = u->su_CtlChar;
	x->io_RBufLen = u->su_RBufLen;
	x->io_ExtFlags = 0;
	x->io_Baud = u->su_Baud;
	x->io_BrkTime = u->su_BrkTime;
	x->io_TermArray = u->su_Term;
	x->io_ReadLen = u->su_ReadLen;
	x->io_WriteLen = u->su_WriteLen;
	x->io_StopBits = u->su_StopBits;
	x->io_SerFlags = (x->io_SerFlags & SERF_SHARED) | u->su_SerFlags;
	x->io_Status = 0;
}

static int
bufswap(u, len)
register struct SerUnit *u;
ULONG len;
{
	UBYTE *nb, *ob;
	UWORD os;
	int s;

	if (u->su_Buf == NULL || len == u->su_Size)
		return (0);
	if ((nb = (UBYTE *)xAllocMem(len, (ULONG)MEMF_PUBLIC)) == NULL)
		return (SerErr_BufErr);
	s = intoff();
	ob = u->su_Buf;
	os = u->su_Size;
	u->su_Buf = nb;
	u->su_Size = (UWORD)len;
	u->su_Head = u->su_Tail = u->su_Count = 0;
	intrest(s);
	xFreeMem((APTR)ob, (ULONG)os);
	return (0);
}

/* SDCMD_SETPARAMS.  Refused while requests are queued.  The console line
 * keeps the ROM's framing and rate. */
static int
setparams(u, x)
register struct SerUnit *u;
register struct IOExtSer *x;
{
	ULONG len;
	long tc;
	int err;

	if (!IsListEmpty(&u->su_Reads) || !IsListEmpty(&u->su_Writes))
		return (SerErr_DevBusy);
	if ((x->io_SerFlags & SERF_7WIRE) || x->io_ExtFlags != 0)
		return (SerErr_InvParam);
	if (x->io_ReadLen < 5 || x->io_ReadLen > 8 ||
	    x->io_WriteLen < 5 || x->io_WriteLen > 8 ||
	    x->io_StopBits < 1 || x->io_StopBits > 2)
		return (SerErr_InvParam);
	if ((tc = tcof(x->io_Baud)) < 0)
		return (SerErr_BaudMismatch);
	if (u->su_Flags & SUF_CONSOLE) {
		if (x->io_Baud != u->su_Baud)
			return (SerErr_BaudMismatch);
		if (x->io_ReadLen != 8 || x->io_WriteLen != 8 ||
		    x->io_StopBits != 1 || (x->io_SerFlags & SERF_PARTY_ON))
			return (SerErr_InvParam);
	}
	len = x->io_RBufLen < SER_MINBUF ? SER_MINBUF : x->io_RBufLen;
	if (len > SER_MAXBUF)
		return (SerErr_InvParam);
	if ((err = bufswap(u, len)) != 0)
		return (err);
	u->su_RBufLen = len;
	u->su_CtlChar = x->io_CtlChar;
	u->su_BrkTime = x->io_BrkTime;
	u->su_Term = x->io_TermArray;
	u->su_SerFlags = SERF_XDISABLED | (x->io_SerFlags &
	    (SERF_EOFMODE | SERF_RAD_BOOGIE | SERF_QUEUEDBRK |
	    SERF_PARTY_ODD | SERF_PARTY_ON));
	if (u->su_Flags & SUF_CONSOLE)
		return (0);
	u->su_Baud = x->io_Baud;
	u->su_TC = (UWORD)tc;
	u->su_ReadLen = x->io_ReadLen;
	u->su_WriteLen = x->io_WriteLen;
	u->su_StopBits = x->io_StopBits;
	program(u);
	return (0);
}

/* Default parameters into x, the console's rate for the console line. */
static void
defaults(u, x)
struct SerUnit *u;
register struct IOExtSer *x;
{
	x->io_CtlChar = 0x11130000L;
	x->io_RBufLen = SER_DEFBUF;
	x->io_ExtFlags = 0;
	x->io_Baud = (u->su_Flags & SUF_CONSOLE) ? u->su_Baud : SER_DEFBAUD;
	x->io_BrkTime = SER_DEFBRK;
	x->io_TermArray.TermArray0 = 0;
	x->io_TermArray.TermArray1 = 0;
	x->io_ReadLen = 8;
	x->io_WriteLen = 8;
	x->io_StopBits = 1;
	x->io_SerFlags = SERF_XDISABLED;
}

static void
qwrite(u, ior)
register struct SerUnit *u;
register struct IORequest *ior;
{
	int s;

	s = intoff();
	xAddTail(&u->su_Writes, &ior->io_Message.mn_Node);
	txpump(u);
	/* Writes finish in order: still last means still queued. */
	if (u->su_Writes.lh_TailPred == &ior->io_Message.mn_Node)
		ior->io_Flags &= ~IOF_QUICK;
	intrest(s);
}

static void
sbegin(dev, ior)
struct Device *dev;
register struct IORequest *ior;
{
	register struct SerUnit *u;
	struct IOExtSer tmp;
	int err, s;

	IOBEGIN(ior);
	u = UNIT(ior);
	err = 0;
	switch (ior->io_Command) {
	case CMD_READ:
		STD(ior)->io_Actual = 0;
		s = intoff();
		if (IsListEmpty(&u->su_Reads) && rxfill(u, ior)) {
			err = u->su_Err;
			u->su_Err = 0;
			intrest(s);
			break;
		}
		ior->io_Flags &= ~IOF_QUICK;
		xAddTail(&u->su_Reads, &ior->io_Message.mn_Node);
		intrest(s);
		return;
	case CMD_WRITE:
		STD(ior)->io_Actual = 0;
		qwrite(u, ior);
		return;
	case SDCMD_BREAK:
		qwrite(u, ior);
		return;
	case CMD_CLEAR:
		clear(u);
		break;
	case CMD_FLUSH:
		flush(u);
		break;
	case CMD_RESET:
		xForbid();
		flush(u);
		clear(u);
		defaults(u, &tmp);
		err = setparams(u, &tmp);
		xPermit();
		break;
	case SDCMD_QUERY:
		query(u, EXT(ior));
		break;
	case SDCMD_SETPARAMS:
		xForbid();
		err = setparams(u, EXT(ior));
		xPermit();
		break;
	default:
		err = IOERR_NOCMD;
	}
	complete(ior, err);
}

/* The first terminal commit wins: a request no longer queued is left
 * alone. */
static LONG
sabort(dev, ior)
struct Device *dev;
register struct IORequest *ior;
{
	register struct SerUnit *u;
	register struct Node *n;
	struct List *l;
	LONG r;
	int s, i;

	u = UNIT(ior);
	r = -1;
	s = intoff();
	for (i = 0; i < 2; i++) {
		l = i == 0 ? &u->su_Reads : &u->su_Writes;
		for (n = l->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
			if (n == &ior->io_Message.mn_Node)
				break;
		if (n->ln_Succ == NULL)
			continue;
		if (l == &u->su_Writes && n == l->lh_Head &&
		    (u->su_Flags & SUF_BRK))
			brkoff(u);
		xRemove(n);
		complete(ior, IOERR_ABORTED);
		if (l == &u->su_Writes)
			txpump(u);
		r = 0;
		break;
	}
	intrest(s);
	return (r);
}

static void
sopen(dev, ior, unit, flags)
struct Device *dev;
register struct IORequest *ior;
ULONG unit, flags;
{
	register struct SerUnit *u;
	register UWORD b;
	struct IOExtSer *x;
	int shared, s;

	if (unit >= SER_UNITS || flags != 0 ||
	    ior->io_Message.mn_ReplyPort == NULL) {
		ior->io_Error = IOERR_OPENFAIL;
		return;
	}
	if ((u = serchip[(int)unit >> 1]) == NULL) {
		ior->io_Error = IOERR_OPENFAIL;
		return;
	}
	u += (int)unit & 1;
	if ((u->su_Flags & SUF_PRESENT) == 0) {
		ior->io_Error = IOERR_OPENFAIL;
		return;
	}
	if (ior->io_Message.mn_Length < sizeof(struct IOExtSer)) {
		ior->io_Error = IOERR_BADLENGTH;
		return;
	}
	x = EXT(ior);
	if (x->io_SerFlags & SERF_7WIRE) {
		ior->io_Error = SerErr_InvParam;
		return;
	}
	shared = (x->io_SerFlags & SERF_SHARED) != 0;
	if (u->su_Unit.unit_OpenCnt != 0) {
		if (!shared || (u->su_Flags & SUF_EXCL)) {
			ior->io_Error = SerErr_DevBusy;
			return;
		}
	} else {
		u->su_Buf = (UBYTE *)xAllocMem(u->su_RBufLen,
		    (ULONG)MEMF_PUBLIC);
		if (u->su_Buf == NULL) {
			ior->io_Error = SerErr_BufErr;
			return;
		}
		u->su_Size = (UWORD)u->su_RBufLen;
		u->su_Head = u->su_Tail = u->su_Count = 0;
		u->su_Err = 0;
		u->su_Status = 0;
		if (!shared)
			u->su_Flags |= SUF_EXCL;
		b = u->su_Base;
		s = intoff();
		if ((u->su_Flags & SUF_CONSOLE) == 0) {
			u->su_WR5 |= DTR | RTS;
			outb(b + WR5, u->su_WR5);
		}
		outb(b + WR15, BREAKIE);
		outb(b + WR0, RESEXT);
		outb(b + WR0, RESEXT);
		outb(b + WR1, EXTIE | TXIE | RXALL);
		intrest(s);
	}
	u->su_Unit.unit_OpenCnt++;
	ior->io_Unit = &u->su_Unit;
	fill(u, x);
	dev->dd_Library.lib_OpenCnt++;
}

/* Closing with a request of this binding still queued is misuse, as in
 * timer.device. */
static APTR
sclose(dev, ior)
struct Device *dev;
struct IORequest *ior;
{
	register struct SerUnit *u;
	register struct Node *n;
	register UWORD b;
	struct MsgPort *port;
	struct List *l;
	UBYTE *buf;
	int s, i;

	u = UNIT(ior);
	b = u->su_Base;
	port = ior->io_Message.mn_ReplyPort;
	buf = NULL;
	s = intoff();
	for (i = 0; i < 2; i++)
		for (l = i ? &u->su_Writes : &u->su_Reads, n = l->lh_Head;
		    n->ln_Succ != NULL; n = n->ln_Succ)
			if (((struct Message *)n)->mn_ReplyPort == port)
				kpanic("serial.device: CloseDevice with requests pending");
	if (--u->su_Unit.unit_OpenCnt == 0) {
		outb(b + WR1, ktxq != NULL && (u->su_Flags & SUF_CONSOLE) ?
		    TXIE : 0);
		outb(b + WR15, 0);
		if ((u->su_Flags & SUF_CONSOLE) == 0) {
			u->su_WR5 &= ~(DTR | RTS);
			outb(b + WR5, u->su_WR5);
		}
		u->su_Flags &= ~SUF_EXCL;
		buf = u->su_Buf;
		u->su_Buf = NULL;
	}
	intrest(s);
	if (buf != NULL)
		xFreeMem((APTR)buf, (ULONG)u->su_Size);
	dev->dd_Library.lib_OpenCnt--;
	return (NULL);
}

/* Nonzero while unit 0 is open: its receive interrupt owns the console
 * line's input, so con_getc must not read the SCC. */
int
kserbusy()
{
	return (serchip[CONLINE >> 1] != NULL &&
	    serchip[CONLINE >> 1][CONLINE & 1].su_Unit.unit_OpenCnt != 0);
}

static APTR serbasefuncs[] = {
	(APTR)sopen, (APTR)sclose, (APTR)knull, (APTR)knull,
	(APTR)sbegin, (APTR)sabort, (APTR)-1L
};

void kserinit();

struct Resident sertag = {
	RTC_MATCHWORD, &sertag, (APTR)(&sertag + 1), RTF_COLDSTART, 1,
	NT_DEVICE, 0, sername, NULL, (APTR)kserinit
};

/* Boot: every line kboot found, at 9600 8N1 except the console, which
 * keeps the ROM's setup; interrupts stay off until a unit opens. */
void
kserinit()
{
	register struct SerUnit *u;
	register UWORD b;
	struct IOExtSer tmp;
	UWORD lines;
	int i, c, k;

	pclk = mach_cpu6() ? 6000000L : 4000000L;
	lines = mach_serial();
	for (c = 0; c < NCHIP; c++)
		if ((lines & (3 << (2 * c))) != 0 &&
		    (serchip[c] = (struct SerUnit *)xAllocMem(
		    (ULONG)(2 * sizeof(struct SerUnit)),
		    (ULONG)(MEMF_PUBLIC | MEMF_CLEAR))) == NULL)
			kpanic("serial.device: no memory");
	for (i = 0; i < SER_UNITS; i++) {
		if ((u = serchip[i >> 1]) == NULL)
			continue;
		u += i & 1;
		xNewList(&u->su_Reads);
		xNewList(&u->su_Writes);
		u->su_Base = b = serport[i];
		u->su_Flags = (lines & (1 << i)) ? SUF_PRESENT : 0;
		if (i == CONLINE)
			u->su_Flags |= SUF_CONSOLE;
		u->su_Baud = SER_DEFBAUD;
		defaults(u, &tmp);
		u->su_CtlChar = tmp.io_CtlChar;
		u->su_RBufLen = tmp.io_RBufLen;
		u->su_BrkTime = tmp.io_BrkTime;
		u->su_Term = tmp.io_TermArray;
		u->su_ReadLen = u->su_WriteLen = 8;
		u->su_StopBits = 1;
		u->su_SerFlags = SERF_XDISABLED;
		u->su_RMask = 0xff;
		u->su_TC = (UWORD)tcof(SER_DEFBAUD);
		if ((u->su_Flags & SUF_PRESENT) == 0)
			continue;
		outb(b + WR1, 0);
		outb(b + WR15, 0);
		if (i == CONLINE) {
			/* Keep the ROM's rate; report 9600 when it is. */
			u->su_WR5 = CONWR5;
			k = (inb(b + RR12) & 0xff) | (inb(b + RR13) & 0xff) << 8;
			if (k != u->su_TC)
				u->su_Baud = pclk / (32L * (k + 2));
			u->su_TC = k;
		} else {
			u->su_WR5 = 0;
			outb(b + WR10, NRZ);
			outb(b + WR11, BRGCLK);
			program(u);
		}
		outb(b + WR0, RESEXT);
		outb(b + WR0, RESEXT);
	}
	for (c = 0; c < NCHIP; c++) {
		if ((lines & (3 << (2 * c))) == 0)
			continue;
		u = serchip[c];
		serbase.sb_Int[c].is_Node.ln_Name = sername;
		serbase.sb_Int[c].is_Data = (APTR)u;
		serbase.sb_Int[c].is_Code = sccint;
		for (k = 0; k < 16; k += 2)
			xSetIntVector((UWORD)(SCCVEC(c) + k), &serbase.sb_Int[c]);
		if ((u->su_Flags & SUF_PRESENT) == 0)
			u++;
		outb(u->su_Base + WR2, SCCVEC(c));
		outb(u->su_Base + WR9, MIEVIS);
	}
	/* The console's Tx interrupt sends the kernel's queue from here on. */
	if ((lines & (1 << CONLINE)) &&
	    (ktxq = (UBYTE *)xAllocMem((ULONG)KTXQ, (ULONG)MEMF_PUBLIC)) != NULL)
		outb(serport[CONLINE] + WR1, TXIE);
	serbase.sb_Tick.is_Node.ln_Pri = SERTICKPRI;
	serbase.sb_Tick.is_Node.ln_Name = sername;
	serbase.sb_Tick.is_Data = (APTR)&serbase;
	serbase.sb_Tick.is_Code = sertick;
	xAddIntServer((UWORD)INTV_TICK, &serbase.sb_Tick);
	kinitlib(&serbase.sb_Device.dd_Library, sername, serbasefuncs);
	xAddDevice(&serbase.sb_Device);
}
