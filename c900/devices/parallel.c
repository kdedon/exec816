/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * parallel.device: the Centronics printer port, in the classic Amiga
 * parallel.device model; docs/internals.md gives the contract and the
 * departures.  The lines (COHERENT's lp driver uses the same):
 *
 *	data	CIO U70 port B, shared with the battery clock
 *	BUSY	CIO U70 PC0, high while the printer cannot take a byte
 *	/STROBE	CIO U66 PC3
 *	/ACK	CIO U66 PB1, inverted, an edge pattern match on vector 0x70
 *
 * Port B may be written only at Task level under Forbid (docs/hardware.md
 * "CIO U70 sharing"), so the handshake runs in a driver Task, created at
 * the first open and gone after the last close.  It holds Forbid except
 * while it Waits.  Per byte it waits for BUSY low, puts the byte on port
 * B, pulses /STROBE, returns port B to 0 (PB7, the clock's STOP, low) and
 * waits for /ACK.  The /ACK interrupt and a tick server Signal it; the
 * tick also ends a byte with no /ACK once BUSY is low two ticks after the
 * strobe, and ends a wait that outlasts io_Timeout.  Short bounded polls
 * catch a fast printer without a Task switch; after PARBURST bytes the
 * Task waits for the next tick, so it never holds Forbid for long.  The
 * request queue is Task-level state shared with the driver Task, so
 * BeginIO and AbortIO take Forbid too; Open and Close run under
 * OpenDevice's.
 *
 * kparinit resets and configures CIO U66, which only this driver and
 * beep.device use.  The emulator models neither U66 nor a printer: BUSY
 * reads back the port C latch and no /ACK arrives, so writes there run
 * at the tick fallback's pace or time out.
 */
#include "kernel.h"
#include <devices/parallel.h>

/* CIO U70: register r at 2r + 1. */
#define P_PBDATA	0x001d
#define P_PCDATA	0x001f
#define P_PBDD		0x0057
#define PC_BUSY		0x01

/* CIO U66. */
#define Q_MICR		0x0081
#define Q_MCCR		0x0083
#define Q_PBIV		0x0087
#define Q_PCDPP		0x008b
#define Q_PCDD		0x008d
#define Q_PCSIO		0x008f
#define Q_PBCS		0x0093
#define Q_PBDATA	0x009d
#define Q_PCDATA	0x009f
#define Q_PBMS		0x00d1
#define Q_PBHS		0x00d3
#define Q_PBDPP		0x00d5
#define Q_PBDD		0x00d7
#define Q_PBSIO		0x00d9
#define Q_PBPP		0x00db
#define Q_PBPT		0x00dd
#define Q_PBPM		0x00df

#define MICR_MIE	0x80
#define MICR_RESET	0x01
#define MCCR_PORTS	0x90	/* port B, port C */
#define PBMS_OR		0x04	/* bit port, OR pattern match */
#define PB_ACK		0x02
#define PC_STROBE	0x08
#define STROBE_LO	0x70	/* PC3 low, PC0-PC2 write-protected */
#define STROBE_HI	0x78
#define CS_CLRIPIUS	0x20
#define CS_SETIE	0xc0

#define TICKUS		10000L
#define ACKSPIN		50	/* polls of the /ACK flag after a strobe */
#define BUSYSPIN	20	/* polls of BUSY before waiting a tick */
#define PARBURST	16	/* bytes per run before waiting a tick */
#define PARSTACK	768L
#define PARPRI		5

/* After beep.device's server at 80. */
#define PARTICKPRI	70

/* pb_Flags. */
#define PBF_EXCL	0x01	/* opened without PARF_SHARED */
#define PBF_WAIT	0x02	/* a byte is strobed; waiting for /ACK */
#define PBF_HOLD	0x04	/* waiting for BUSY low before a byte */
#define PBF_EXIT	0x08	/* the last close: the Task should end */
#define PBF_PACE	0x10	/* a burst is done: run again on the tick */

struct ParBase {
	struct Device pb_Device;
	struct Unit pb_Unit;
	struct List pb_Queue;	/* CMD_WRITE, head first */
	struct Interrupt pb_Ack;
	struct Interrupt pb_Tick;
	struct Task *pb_Task;
	struct IORequest *pb_Cur;	/* the request of the strobed byte */
	ULONG pb_Sig;
	ULONG pb_Strobe;	/* tick of the last strobe */
	ULONG pb_Due;		/* tick the current wait gives up at */
	ULONG pb_Ticks;		/* io_Timeout in ticks */
	ULONG pb_Timeout;
	struct IOPArray pb_Term;
	UWORD pb_Flags;
	UBYTE pb_ParFlags;
	volatile UBYTE pb_Acked;	/* set by the /ACK interrupt */
};

static struct {
	struct JumpVec lv_Vec[6];
	struct ParBase lv_Base;
} parbaselib;
#define parbase parbaselib.lv_Base
static char parname[] = PARALLELNAME;

#define EXT(ior)	((struct IOExtPar *)(ior))
#define STD(ior)	((struct IOStdReq *)(ior))
#define FIRST(l)	((l)->lh_Head->ln_Succ != NULL ? \
			    (struct IORequest *)(l)->lh_Head : \
			    (struct IORequest *)NULL)
#define BUSY()		(inb(P_PCDATA) & PC_BUSY)
#define DUE(p)		((long)(TickCount - (p)->pb_Due) >= 0)

static void
complete(ior, err)
register struct IORequest *ior;
int err;
{
	ior->io_Error = err;
	xTermIO(ior);
}

static void
finish(p, ior, err)
register struct ParBase *p;
register struct IORequest *ior;
int err;
{
	if (p->pb_Cur == ior)
		p->pb_Cur = NULL;
	xRemove(&ior->io_Message.mn_Node);
	complete(ior, err);
}

static int
isterm(p, c)
register struct ParBase *p;
register int c;
{
	register UBYTE *t;
	register int i;

	t = (UBYTE *)&p->pb_Term;
	for (i = 0; i < 8; i++)
		if (t[i] == c)
			return (1);
	return (0);
}

/* io_Length -1 means up to the NUL. */
static int
wdone(r)
register struct IOStdReq *r;
{
	if (r->io_Length == 0xFFFFFFFFL)
		return (r->io_Data[(UWORD)r->io_Actual] == 0);
	return (r->io_Actual >= r->io_Length);
}

/* One byte onto the wire.  Task level, Forbid held. */
static void
strobe(c)
int c;
{
	int s;

	if (inb(P_PBDD) != 0)
		outb(P_PBDD, 0);
	outb(P_PBDATA, c);
	s = intoff();
	parbase.pb_Acked = 0;
	outb(Q_PCDATA, STROBE_LO);
	outb(Q_PCDATA, STROBE_HI);
	intrest(s);
	outb(P_PBDATA, 0);
}

/* TRUE once the strobed byte is taken: /ACK, or BUSY low two ticks on. */
static int
taken(p)
register struct ParBase *p;
{
	register int n;

	for (n = ACKSPIN; n != 0 && !p->pb_Acked; n--)
		;
	return (p->pb_Acked || (!BUSY() && TickCount - p->pb_Strobe >= 2));
}

/* Send what the printer takes now.  Task level, Forbid held. */
static void
parrun(p)
register struct ParBase *p;
{
	register struct IORequest *ior;
	register struct IOStdReq *r;
	register int n;
	int c, sent;

	/* Every event from here on Signals again; an /ACK the polls took
	 * must not end the next Wait early. */
	xSetSignal(0L, p->pb_Sig);
	p->pb_Flags &= ~PBF_PACE;
	sent = 0;
	for (;;) {
		if (p->pb_Flags & PBF_WAIT) {
			if (!taken(p)) {
				if (!DUE(p))
					return;
				p->pb_Flags &= ~PBF_WAIT;
				if ((ior = p->pb_Cur) != NULL)
					finish(p, ior, ParErr_LineErr);
				continue;
			}
			p->pb_Flags &= ~PBF_WAIT;
			if ((ior = p->pb_Cur) != NULL) {
				r = STD(ior);
				c = r->io_Data[(UWORD)r->io_Actual];
				r->io_Actual++;
				if ((p->pb_ParFlags & PARF_EOFMODE) &&
				    isterm(p, c & 0xff))
					finish(p, ior, 0);
			}
			p->pb_Cur = NULL;
		}
		if ((ior = FIRST(&p->pb_Queue)) == NULL)
			return;
		r = STD(ior);
		if (wdone(r)) {
			finish(p, ior, 0);
			continue;
		}
		if (sent >= PARBURST) {
			/* No byte is out: only the tick may end this Wait. */
			xSetSignal(0L, p->pb_Sig);
			p->pb_Flags |= PBF_PACE;
			return;
		}
		for (n = BUSYSPIN; n != 0 && BUSY(); n--)
			;
		if (BUSY()) {
			if ((p->pb_Flags & PBF_HOLD) == 0) {
				p->pb_Flags |= PBF_HOLD;
				p->pb_Due = TickCount + p->pb_Ticks;
			} else if (DUE(p)) {
				p->pb_Flags &= ~PBF_HOLD;
				finish(p, ior, ParErr_LineErr);
				continue;
			}
			return;
		}
		p->pb_Flags &= ~PBF_HOLD;
		strobe(r->io_Data[(UWORD)r->io_Actual] & 0xff);
		p->pb_Cur = ior;
		p->pb_Strobe = TickCount;
		p->pb_Due = TickCount + p->pb_Ticks;
		p->pb_Flags |= PBF_WAIT;
		sent++;
	}
}

static void
partask()
{
	register struct ParBase *p;

	p = &parbase;
	xForbid();
	p->pb_Sig = 1L << xAllocSignal(-1);
	for (;;) {
		parrun(p);
		if ((p->pb_Flags & PBF_EXIT) && IsListEmpty(&p->pb_Queue))
			break;
		xWait(p->pb_Sig);
	}
	/* Still under Forbid: RemTask ends it. */
	p->pb_Flags &= ~(PBF_EXIT | PBF_WAIT | PBF_HOLD | PBF_PACE);
	p->pb_Cur = NULL;
	p->pb_Task = NULL;
}

static void
wake(p)
register struct ParBase *p;
{
	if (p->pb_Task != NULL && p->pb_Sig != 0)
		xSignal(p->pb_Task, p->pb_Sig);
}

static int
parack(p, n)
register struct ParBase *p;
UWORD n;
{
	outb(Q_PBCS, CS_CLRIPIUS);
	p->pb_Acked = 1;
	if (p->pb_Flags & PBF_WAIT)
		wake(p);
	return (0);
}

static int
partick(p, n)
register struct ParBase *p;
UWORD n;
{
	if ((p->pb_Flags & PBF_PACE) || ((p->pb_Flags & (PBF_WAIT |
	    PBF_HOLD)) != 0 && (DUE(p) || !BUSY())))
		wake(p);
	return (0);
}

static void
flushq(p)
register struct ParBase *p;
{
	register struct IORequest *ior;

	while ((ior = FIRST(&p->pb_Queue)) != NULL)
		finish(p, ior, IOERR_ABORTED);
	p->pb_Flags &= ~PBF_HOLD;
}

static void
fill(p, x)
register struct ParBase *p;
register struct IOExtPar *x;
{
	x->io_PExtFlags = 0;
	x->io_Status = 0;
	x->io_ParFlags = (x->io_ParFlags & PARF_SHARED) | p->pb_ParFlags;
	x->io_PTermArray = p->pb_Term;
	x->io_Timeout = p->pb_Timeout;
}

static int
setparams(p, x)
register struct ParBase *p;
register struct IOExtPar *x;
{
	if (!IsListEmpty(&p->pb_Queue))
		return (ParErr_DevBusy);
	if (x->io_PExtFlags != 0 || x->io_Timeout == 0 ||
	    x->io_Timeout > PAR_MAXTIMEOUT)
		return (ParErr_InvParam);
	p->pb_ParFlags = x->io_ParFlags & PARF_EOFMODE;
	p->pb_Term = x->io_PTermArray;
	p->pb_Timeout = x->io_Timeout;
	p->pb_Ticks = (x->io_Timeout + TICKUS - 1) / TICKUS + 1;
	fill(p, x);
	return (0);
}

static void
defaults(x)
register struct IOExtPar *x;
{
	x->io_PExtFlags = 0;
	x->io_ParFlags = 0;
	x->io_PTermArray.PTermArray0 = 0;
	x->io_PTermArray.PTermArray1 = 0;
	x->io_Timeout = PAR_DEFTIMEOUT;
}

static void
pbegin(dev, ior)
struct Device *dev;
register struct IORequest *ior;
{
	register struct ParBase *p;
	struct IOExtPar tmp;
	int err;

	IOBEGIN(ior);
	p = (struct ParBase *)dev;
	err = 0;
	xForbid();
	switch (ior->io_Command) {
	case CMD_WRITE:
		STD(ior)->io_Actual = 0;
		if (IsListEmpty(&p->pb_Queue) && wdone(STD(ior)))
			break;
		ior->io_Flags &= ~IOF_QUICK;
		xAddTail(&p->pb_Queue, &ior->io_Message.mn_Node);
		wake(p);
		xPermit();
		return;
	case CMD_CLEAR:
		break;
	case CMD_FLUSH:
		flushq(p);
		break;
	case CMD_RESET:
		flushq(p);
		defaults(&tmp);
		err = setparams(p, &tmp);
		break;
	case PDCMD_QUERY:
		EXT(ior)->io_Status = BUSY() ? IOPTF_PARBUSY : 0;
		break;
	case PDCMD_SETPARAMS:
		err = setparams(p, EXT(ior));
		break;
	default:
		err = IOERR_NOCMD;
	}
	complete(ior, err);
	xPermit();
}

/* The first terminal commit wins: a request no longer queued is left
 * alone.  A strobed byte's handshake still runs to its end. */
static LONG
pabort(dev, ior)
struct Device *dev;
register struct IORequest *ior;
{
	register struct ParBase *p;
	register struct Node *n;
	LONG r;

	p = (struct ParBase *)dev;
	r = -1;
	xForbid();
	for (n = p->pb_Queue.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (n == &ior->io_Message.mn_Node) {
			finish(p, ior, IOERR_ABORTED);
			wake(p);
			r = 0;
			break;
		}
	xPermit();
	return (r);
}

static void
popen(dev, ior, unit, flags)
struct Device *dev;
register struct IORequest *ior;
ULONG unit, flags;
{
	register struct ParBase *p;
	struct IOExtPar *x;
	int shared;

	p = (struct ParBase *)dev;
	if (unit != 0 || flags != 0 || ior->io_Message.mn_ReplyPort == NULL) {
		ior->io_Error = IOERR_OPENFAIL;
		return;
	}
	if (ior->io_Message.mn_Length < sizeof(struct IOExtPar)) {
		ior->io_Error = IOERR_BADLENGTH;
		return;
	}
	x = EXT(ior);
	shared = (x->io_ParFlags & PARF_SHARED) != 0;
	if (p->pb_Unit.unit_OpenCnt != 0) {
		if (!shared || (p->pb_Flags & PBF_EXCL)) {
			ior->io_Error = ParErr_DevBusy;
			return;
		}
	} else {
		if (p->pb_Task == NULL) {
			p->pb_Sig = 0;
			p->pb_Flags &= ~(PBF_WAIT | PBF_HOLD | PBF_PACE);
			p->pb_Cur = NULL;
			p->pb_Task = xCreateTask(parname, (LONG)PARPRI,
			    (TASKPROC)partask, PARSTACK);
			if (p->pb_Task == NULL) {
				ior->io_Error = ParErr_InitErr;
				return;
			}
		}
		p->pb_Flags &= ~PBF_EXIT;
		if (!shared)
			p->pb_Flags |= PBF_EXCL;
	}
	p->pb_Unit.unit_OpenCnt++;
	ior->io_Unit = &p->pb_Unit;
	fill(p, x);
	dev->dd_Library.lib_OpenCnt++;
}

/* Closing with a request of this binding still queued is misuse, as in
 * timer.device. */
static APTR
pclose(dev, ior)
struct Device *dev;
struct IORequest *ior;
{
	register struct ParBase *p;
	register struct Node *n;
	struct MsgPort *port;

	p = (struct ParBase *)dev;
	port = ior->io_Message.mn_ReplyPort;
	for (n = p->pb_Queue.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (((struct Message *)n)->mn_ReplyPort == port)
			kpanic("parallel.device: CloseDevice with requests pending");
	if (--p->pb_Unit.unit_OpenCnt == 0) {
		p->pb_Flags &= ~PBF_EXCL;
		p->pb_Flags |= PBF_EXIT;
		wake(p);
	}
	dev->dd_Library.lib_OpenCnt--;
	return (NULL);
}

static APTR parbasefuncs[] = {
	(APTR)popen, (APTR)pclose, (APTR)knull, (APTR)knull,
	(APTR)pbegin, (APTR)pabort, (APTR)-1L
};

void kparinit();

struct Resident partag = {
	RTC_MATCHWORD, &partag, (APTR)(&partag + 1), RTF_COLDSTART, 1,
	NT_DEVICE, 0, parname, NULL, (APTR)kparinit
};

/* Boot: reset U66 and set it up for the printer and the speaker. */
void
kparinit()
{
	register struct ParBase *p;
	struct IOExtPar tmp;
	int s;

	p = &parbase;
	xNewList(&p->pb_Queue);
	defaults(&tmp);
	setparams(p, &tmp);
	s = intoff();
	outb(Q_MICR, MICR_RESET);
	outb(Q_MICR, 0);
	outb(Q_PBMS, PBMS_OR);
	outb(Q_PBHS, 0);
	outb(Q_PBDPP, PB_ACK);
	outb(Q_PBSIO, 0);
	outb(Q_PBPP, PB_ACK);
	outb(Q_PBPT, PB_ACK);
	outb(Q_PBPM, PB_ACK);
	outb(Q_PBDATA, 0xff);
	outb(Q_PBDD, 0xff);
	outb(Q_PBIV, INTV_PRINTER);
	outb(Q_PCDPP, 0);
	outb(Q_PCSIO, 0);
	outb(Q_PCDATA, STROBE_HI);
	outb(Q_PCDD, ~PC_STROBE & 0x0f);
	intrest(s);
	p->pb_Ack.is_Node.ln_Name = parname;
	p->pb_Ack.is_Data = (APTR)p;
	p->pb_Ack.is_Code = parack;
	xSetIntVector((UWORD)INTV_PRINTER, &p->pb_Ack);
	s = intoff();
	outb(Q_PBCS, CS_CLRIPIUS);
	outb(Q_PBCS, CS_SETIE);
	outb(Q_MCCR, MCCR_PORTS);
	outb(Q_MICR, MICR_MIE);
	intrest(s);
	p->pb_Tick.is_Node.ln_Pri = PARTICKPRI;
	p->pb_Tick.is_Node.ln_Name = parname;
	p->pb_Tick.is_Data = (APTR)p;
	p->pb_Tick.is_Code = partick;
	xAddIntServer((UWORD)INTV_TICK, &p->pb_Tick);
	kinitlib(&p->pb_Device.dd_Library, parname, parbasefuncs);
	xAddDevice(&p->pb_Device);
}
