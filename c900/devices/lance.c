/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * lance.device: the SANA-II driver for the Am7990 LANCE Ethernet card
 * (RDP 0x0700, RAP 0x0702, address PROM 0x0710-0x0714, vector 0x88),
 * modelled on the A2065's a2065.device.  The chip masters the bus with
 * 24-bit physical addresses that bypass the MMU, so the init block,
 * both descriptor rings and every frame buffer are one MEMF_24BITDMA
 * block of under 64 KB inside one physical page, addressed through
 * CachePreDMA.
 *
 * The receive ring has NRX buffers of BUFSZ bytes; the chip owns every
 * one it is not being emptied.  The interrupt server copies each frame
 * into the first matching CMD_READ of every opener (or an S2_READORPHAN)
 * with the opener's CopyToBuff and gives the buffer back at once.  The
 * transmit ring has NTX buffers; a write is copied in with CopyFromBuff
 * when a buffer is free, otherwise queued until the server reaps one,
 * and replied when the chip releases its descriptor.  Both copies run
 * with interrupts off.
 *
 * Unit 0 is the cable.  Units 1 and 2 are the chip's internal and
 * external loopback modes (MODE LOOP|INTL, LOOP), for diagnostics: one
 * unit at a time, frames of at most 32 bytes, no padding.
 *
 * Open, Close and Expunge run under OpenDevice's Forbid.  BeginIO and
 * AbortIO run unlocked; queues, rings, statistics and the chip are
 * shared with the server and changed with VI off.  RAP stays 0 except
 * inside the VI-off start sequence.
 */
#include <exec/exec.h>
#include <devices/sana2.h>

int inw();
void outw();
int intoff();
void intrest();

#define LANCENAME	"lance.device"

#define RDP		0x0700
#define RAP		0x0702
#define PROM		0x0710

#define CSR0_ERR	0x8000
#define CSR0_BABL	0x4000
#define CSR0_MISS	0x1000
#define CSR0_MERR	0x0800
#define CSR0_RINT	0x0400
#define CSR0_TINT	0x0200
#define CSR0_IDON	0x0100
#define CSR0_INTR	0x0080
#define CSR0_INEA	0x0040
#define CSR0_TDMD	0x0008
#define CSR0_STOP	0x0004
#define CSR0_STRT	0x0002
#define CSR0_INIT	0x0001
#define CSR0_ACK	0x7f00		/* the write-1-to-clear bits */
#define CSR3_MODE	0x0007		/* BSWP, ACON, BCON */

#define MODE_LOOP	0x0004
#define MODE_INTL	0x0040
#define MODE_PROM	0x8000

#define D_OWN		0x8000		/* descriptor word 1 */
#define D_ERR		0x4000
#define D_STP		0x0200
#define D_ENP		0x0100
#define R_OFLO		0x1000
#define T_MORE		0x1000
#define T_ONE		0x0800
#define T_BUFF		0x8000		/* TMD3 */
#define T_UFLO		0x4000
#define T_RTRY		0x0400

#define NRXLOG		3
#define NRX		(1 << NRXLOG)
#define NTXLOG		1
#define NTX		(1 << NTXLOG)
#define BUFSZ		1520		/* a frame and its FCS, 8-aligned */

/* The DMA block: init block, rings (8-aligned), buffers. */
#define O_RMD		32
#define O_TMD		(O_RMD + 8 * NRX)
#define O_RBUF		(O_TMD + 8 * NTX)
#define O_TBUF		(O_RBUF + NRX * BUFSZ)
#define MEMSIZE		((ULONG)O_TBUF + NTX * BUFSZ)

#define ADDRLEN		6
#define HDRLEN		14
#define ETHERMTU		1500
#define MINFRAME	60
#define NMCAST		8
#define NTRACK		8
#define NSPECIAL	3
#define NUNIT		3
#define INITWAIT	2000		/* IDON polls */
#define TICKHZ		100

#define LN_CONFIG	0x01		/* ln_State */
#define LN_ONLINE	0x02
#define LN_MINE		0x04

#define QUEUED		(-100)		/* lnqueue: the request is pending */

#define IOBEGIN(ior)	((ior)->io_Error = 0, \
			    (ior)->io_Message.mn_Node.ln_Type = NT_MESSAGE)
#define S2(ior)		((struct IOSana2Req *)(ior))
#define OPENER(ior)	((struct LnOpener *)S2(ior)->ios2_BufferManagement)
#define REQ(n)		((struct IOSana2Req *)(n))
#define EMPTY(l)	((l)->mlh_TailPred == (struct MinNode *)(l))

struct LnOpener {
	struct MinNode lo_Node;
	struct MinList lo_Read;
	int (*lo_ToBuff)();
	int (*lo_FromBuff)();
	struct Hook *lo_Filter;
	UBYTE lo_Prom;
};

struct LnMcast {
	UBYTE lm_Addr[ADDRLEN];
	UWORD lm_Count;
};

struct LnTrack {
	ULONG lt_Type;
	UWORD lt_Used;
	struct Sana2PacketTypeStats lt_Stats;
};

struct LnBase {
	struct Device ln_Dev;
	struct Unit ln_Unit;
	struct MinList ln_Openers;
	struct MinList ln_Orphan;
	struct MinList ln_Event;
	struct MinList ln_Write;
	struct Interrupt ln_Int;
	UBYTE *ln_Mem;
	ULONG ln_Phys;
	struct IOSana2Req *ln_TxReq[NTX];
	UBYTE ln_TxBuf[NTX];		/* the buffer behind each TMD */
	UBYTE ln_TxHead;
	UBYTE ln_TxTail;
	UBYTE ln_TxCount;
	UBYTE ln_Rx;
	UBYTE ln_State;
	UBYTE ln_UnitNum;
	UBYTE ln_Prom;
	UWORD ln_Mode;
	UBYTE ln_Addr[ADDRLEN];
	UBYTE ln_Default[ADDRLEN];
	UWORD ln_Hash[4];		/* from here on cleared by a first open */
	struct LnMcast ln_Mcast[NMCAST];
	struct LnTrack ln_Track[NTRACK];
	struct Sana2DeviceStats ln_Stats;
	ULONG ln_Special[NSPECIAL];
};

static struct LnBase *lnbase;
static char lnname[] = LANCENAME;
static UWORD lnmodes[NUNIT] = { 0, MODE_LOOP | MODE_INTL, MODE_LOOP };
static char ssbadmc[] = "bad multicast";
static char ssretry[] = "retries";
static char ssuflo[] = "FIFO underruns";
static char *lnssname[NSPECIAL] = { ssbadmc, ssretry, ssuflo };

#define SS_BADMC	0
#define SS_RETRY	1
#define SS_UFLO		2

#define RMD(ln, i)	((volatile UWORD *)((ln)->ln_Mem + O_RMD + 8 * (i)))
#define TMD(ln, i)	((volatile UWORD *)((ln)->ln_Mem + O_TMD + 8 * (i)))
#define RBUF(ln, i)	((ln)->ln_Mem + O_RBUF + BUFSZ * (i))
#define TBUF(ln, b)	((ln)->ln_Mem + O_TBUF + BUFSZ * (b))
#define PHYS(ln, o)	((ln)->ln_Phys + (o))


static void
csr(n, v)
int n, v;
{
	outw(RAP, n);
	outw(RDP, v);
}

/* Point a descriptor at physical address a. */
static void
setdesc(d, a, flags, n)
register volatile UWORD *d;
ULONG a;
UWORD flags;
int n;
{
	d[0] = (UWORD)a;
	d[2] = -n;
	d[3] = 0;
	d[1] = flags | ((UWORD)(a >> 16) & 0xff);
}

/* The LADRF bit of a group address: the top 6 bits of the reflected
 * CRC-32 of its 6 bytes. */
static int
lnhashbit(a)
register UBYTE *a;
{
	register ULONG crc;
	register int i, b;
	UWORD c;

	crc = 0xffffffffL;
	for (i = 0; i < ADDRLEN; i++) {
		c = a[i];
		for (b = 0; b < 8; b++) {
			if ((crc ^ c) & 1)
				crc = crc >> 1 ^ 0xedb88320L;
			else
				crc >>= 1;
			c >>= 1;
		}
	}
	return ((int)(crc >> 26));
}

static void
lnrehash(ln)
register struct LnBase *ln;
{
	register int i, h;

	ln->ln_Hash[0] = ln->ln_Hash[1] = ln->ln_Hash[2] = ln->ln_Hash[3] = 0;
	for (i = 0; i < NMCAST; i++)
		if (ln->ln_Mcast[i].lm_Count != 0) {
			h = lnhashbit(ln->ln_Mcast[i].lm_Addr);
			ln->ln_Hash[h >> 4] |= 1 << (h & 15);
		}
}

static int
sameaddr(a, b)
register UBYTE *a, *b;
{
	register int i;

	for (i = 0; i < ADDRLEN; i++)
		if (a[i] != b[i])
			return (0);
	return (1);
}

static struct LnMcast *
lnfindmc(ln, a)
register struct LnBase *ln;
UBYTE *a;
{
	register struct LnMcast *m;
	register int i;

	for (i = 0, m = ln->ln_Mcast; i < NMCAST; i++, m++)
		if (m->lm_Count != 0 && sameaddr(m->lm_Addr, a))
			return (m);
	return (NULL);
}

static struct LnTrack *
lnfindtt(ln, type)
register struct LnBase *ln;
ULONG type;
{
	register struct LnTrack *t;
	register int i;

	for (i = 0, t = ln->ln_Track; i < NTRACK; i++, t++)
		if (t->lt_Used && t->lt_Type == type)
			return (t);
	return (NULL);
}

static void
lnreply(ior, err, werr)
register struct IOSana2Req *ior;
int err;
ULONG werr;
{
	ior->ios2_Req.io_Error = err;
	ior->ios2_WireError = werr;
	ReplyMsg(&ior->ios2_Req.io_Message);
}

/* Complete every request on l.  VI off. */
static void
lnflush(l, err, werr)
register struct MinList *l;
int err;
ULONG werr;
{
	register struct MinNode *n;

	while ((n = (struct MinNode *)RemHead((struct List *)l)) != NULL)
		lnreply(REQ(n), err, werr);
}

/* Report events: every S2_ONEVENT asking for one of them completes.
 * VI off. */
static void
lnevent(ln, ev)
struct LnBase *ln;
ULONG ev;
{
	register struct MinNode *n, *next;
	register struct IOSana2Req *ior;

	for (n = ln->ln_Event.mlh_Head; (next = n->mln_Succ) != NULL;
	    n = next) {
		ior = REQ(n);
		if (ior->ios2_WireError & ev) {
			Remove((struct Node *)n);
			lnreply(ior, 0, ior->ios2_WireError & ev);
		}
	}
}

static ULONG
pkttype(f)
register UBYTE *f;
{
	return ((ULONG)(UWORD)(f[12] << 8 | f[13]));
}

/* A read for type t takes a frame of type ft; 802.3 lengths match any
 * 802.3 read. */
static int
typematch(t, ft)
ULONG t, ft;
{
	return (t == ft || t <= ETHERMTU && ft <= ETHERMTU);
}

/* Hand a received frame to ior.  VI off. */
static void
lngive(ior, f, n, flags)
register struct IOSana2Req *ior;
register UBYTE *f;
int n, flags;
{
	register int i;
	UBYTE *data;
	struct LnOpener *op;

	for (i = 0; i < ADDRLEN; i++) {
		ior->ios2_DstAddr[i] = f[i];
		ior->ios2_SrcAddr[i] = f[ADDRLEN + i];
	}
	ior->ios2_PacketType = pkttype(f);
	ior->ios2_Req.io_Flags = ior->ios2_Req.io_Flags &
	    ~(SANA2IOF_BCAST | SANA2IOF_MCAST) | flags;
	data = f;
	if ((ior->ios2_Req.io_Flags & SANA2IOF_RAW) == 0) {
		data += HDRLEN;
		n -= HDRLEN;
	}
	ior->ios2_DataLength = n;
	op = OPENER(ior);
	if ((*op->lo_ToBuff)(ior->ios2_Data, data, (ULONG)n))
		lnreply(ior, 0, 0L);
	else {
		lnreply(ior, S2ERR_NO_RESOURCES, (ULONG)S2WERR_BUFF_ERROR);
		lnevent(lnbase, S2EVENT_BUFF);
	}
}

static int
lnfilter(op, ior, f)
register struct LnOpener *op;
struct IOSana2Req *ior;
UBYTE *f;
{
	register struct Hook *h;

	if ((h = op->lo_Filter) == NULL)
		return (1);
	if ((ior->ios2_Req.io_Flags & SANA2IOF_RAW) == 0)
		f += HDRLEN;
	return ((*h->h_Entry)(h, ior, f) != 0);
}

/* Deliver one good frame of n bytes, FCS excluded.  VI off. */
static void
lndeliver(ln, f, n)
register struct LnBase *ln;
register UBYTE *f;
int n;
{
	register struct MinNode *o, *r;
	struct LnTrack *t;
	ULONG type;
	int flags, taken;

	if (n < HDRLEN) {
		ln->ln_Stats.BadData++;
		return;
	}
	flags = 0;
	if (f[0] & 1) {
		if ((f[0] & f[1] & f[2] & f[3] & f[4] & f[5]) == 0xff)
			flags = SANA2IOF_BCAST;
		else {
			flags = SANA2IOF_MCAST;
			if (ln->ln_Prom == 0 && lnfindmc(ln, f) == NULL) {
				ln->ln_Special[SS_BADMC]++;
				return;
			}
		}
	}
	ln->ln_Stats.PacketsReceived++;
	type = pkttype(f);
	if ((t = lnfindtt(ln, type)) != NULL) {
		t->lt_Stats.PacketsReceived++;
		t->lt_Stats.BytesReceived += n - HDRLEN;
	}
	taken = 0;
	for (o = ln->ln_Openers.mlh_Head; o->mln_Succ != NULL;
	    o = o->mln_Succ)
		for (r = ((struct LnOpener *)o)->lo_Read.mlh_Head;
		    r->mln_Succ != NULL; r = r->mln_Succ)
			if (typematch(REQ(r)->ios2_PacketType, type)) {
				if (lnfilter((struct LnOpener *)o, REQ(r), f)) {
					Remove((struct Node *)r);
					lngive(REQ(r), f, n, flags);
					taken = 1;
				}
				break;
			}
	if (taken)
		return;
	ln->ln_Stats.UnknownTypesReceived++;
	if ((r = (struct MinNode *)RemHead((struct List *)&ln->ln_Orphan))
	    != NULL)
		lngive(REQ(r), f, n, flags);
	else if (t != NULL)
		t->lt_Stats.PacketsDropped++;
}

/* Empty the receive ring.  VI off. */
static void
lnrx(ln)
register struct LnBase *ln;
{
	register volatile UWORD *d;
	register UWORD st;

	for (;;) {
		d = RMD(ln, ln->ln_Rx);
		if ((st = d[1]) & D_OWN)
			break;
		if ((st & (D_ERR | D_STP | D_ENP)) == (D_STP | D_ENP))
			lndeliver(ln, RBUF(ln, ln->ln_Rx), (d[3] & 0x0fff) - 4);
		else {
			if (st & R_OFLO)
				ln->ln_Stats.Overruns++;
			else
				ln->ln_Stats.BadData++;
			lnevent(ln, S2EVENT_RX | S2EVENT_ERROR);
		}
		d[3] = 0;
		d[1] = D_OWN | (st & 0xff);
		ln->ln_Rx = (ln->ln_Rx + 1) & (NRX - 1);
	}
}

/* Build ior's frame in transmit buffer b and give TMD j to the chip;
 * 0 when the copy failed.  VI off. */
static int
lnframe(ln, ior, j)
register struct LnBase *ln;
register struct IOSana2Req *ior;
int j;
{
	register UBYTE *f;
	register int i, n;
	int ok;

	f = TBUF(ln, ln->ln_TxBuf[j]);
	n = (int)ior->ios2_DataLength;
	if (ior->ios2_Req.io_Flags & SANA2IOF_RAW)
		ok = (*OPENER(ior)->lo_FromBuff)(f, ior->ios2_Data, (ULONG)n);
	else {
		for (i = 0; i < ADDRLEN; i++) {
			f[i] = ior->ios2_Req.io_Command == S2_BROADCAST ?
			    0xff : ior->ios2_DstAddr[i];
			f[ADDRLEN + i] = ln->ln_Addr[i];
		}
		f[12] = (UBYTE)(ior->ios2_PacketType >> 8);
		f[13] = (UBYTE)ior->ios2_PacketType;
		ok = (*OPENER(ior)->lo_FromBuff)(f + HDRLEN, ior->ios2_Data,
		    (ULONG)n);
		n += HDRLEN;
	}
	if (!ok)
		return (0);
	if (ln->ln_Mode == 0)
		for (; n < MINFRAME; n++)
			f[n] = 0;
	ln->ln_TxReq[j] = ior;
	setdesc(TMD(ln, j), PHYS(ln, O_TBUF + BUFSZ * ln->ln_TxBuf[j]),
	    D_OWN | D_STP | D_ENP, n);
	return (1);
}

/* Move queued writes into free transmit buffers.  VI off. */
static void
lntxfill(ln)
register struct LnBase *ln;
{
	register struct IOSana2Req *ior;
	int gave;

	gave = 0;
	while (ln->ln_TxCount < NTX &&
	    (ior = REQ(RemHead((struct List *)&ln->ln_Write))) != NULL) {
		if (lnframe(ln, ior, ln->ln_TxHead)) {
			ln->ln_TxHead = (ln->ln_TxHead + 1) & (NTX - 1);
			ln->ln_TxCount++;
			gave = 1;
		} else {
			lnreply(ior, S2ERR_NO_RESOURCES,
			    (ULONG)S2WERR_BUFF_ERROR);
			lnevent(ln, S2EVENT_BUFF);
		}
	}
	if (gave)
		outw(RDP, CSR0_INEA | CSR0_TDMD);
}

/* Reply every write the chip has released; 1 when the transmitter
 * stopped (underflow) and needs a restart.  VI off. */
static int
lntxdone(ln)
register struct LnBase *ln;
{
	register volatile UWORD *d;
	register struct IOSana2Req *ior;
	register UWORD st;
	struct LnTrack *t;
	int restart, j;

	restart = 0;
	while (ln->ln_TxCount != 0) {
		j = ln->ln_TxTail;
		d = TMD(ln, j);
		if ((st = d[1]) & D_OWN)
			break;
		ior = ln->ln_TxReq[j];
		ln->ln_TxTail = (j + 1) & (NTX - 1);
		ln->ln_TxCount--;
		if (st & D_ERR) {
			if (d[3] & (T_BUFF | T_UFLO)) {
				ln->ln_Special[SS_UFLO]++;
				restart = 1;
			}
			if (d[3] & T_RTRY)
				ln->ln_Special[SS_RETRY] += 16;
			lnreply(ior, S2ERR_TX_FAILURE, d[3] & T_RTRY ?
			    (ULONG)S2WERR_TOO_MANY_RETIRES :
			    (ULONG)S2WERR_GENERIC_ERROR);
			lnevent(ln, S2EVENT_TX | S2EVENT_ERROR);
			continue;
		}
		if (st & T_MORE)
			ln->ln_Special[SS_RETRY] += 2;
		else if (st & T_ONE)
			ln->ln_Special[SS_RETRY]++;
		ln->ln_Stats.PacketsSent++;
		t = lnfindtt(ln, pkttype(TBUF(ln, ln->ln_TxBuf[j])));
		if (t != NULL) {
			t->lt_Stats.PacketsSent++;
			t->lt_Stats.BytesSent += ior->ios2_DataLength;
		}
		lnreply(ior, 0, 0L);
	}
	return (restart);
}

/*
 * Stop the chip and start it again from the init block: receive
 * buffers all given back, writes still owned by the chip moved to the
 * ring's start (the chip restarts there) in order.  0 when the chip
 * does not come up.  VI off.
 */
static int
lnstart(ln)
register struct LnBase *ln;
{
	register volatile UWORD *ib;
	register int i, k;
	struct IOSana2Req *req[NTX];
	UBYTE buf[NTX];
	UWORD tmd[NTX][4];
	ULONG a;

	outw(RDP, CSR0_STOP);
	if (ln->ln_State & LN_ONLINE) {
		lnrx(ln);
		lntxdone(ln);
	}
	for (i = 0; i < NTX; i++) {
		k = (ln->ln_TxTail + i) & (NTX - 1);
		req[i] = ln->ln_TxReq[k];
		buf[i] = ln->ln_TxBuf[k];
		tmd[i][0] = TMD(ln, k)[0];
		tmd[i][1] = TMD(ln, k)[1];
		tmd[i][2] = TMD(ln, k)[2];
	}
	for (i = 0; i < NTX; i++) {
		ln->ln_TxReq[i] = req[i];
		ln->ln_TxBuf[i] = buf[i];
		TMD(ln, i)[0] = tmd[i][0];
		TMD(ln, i)[2] = tmd[i][2];
		TMD(ln, i)[3] = 0;
		TMD(ln, i)[1] = i < ln->ln_TxCount ? tmd[i][1] : 0;
	}
	ln->ln_TxTail = 0;
	ln->ln_TxHead = ln->ln_TxCount & (NTX - 1);
	for (i = 0; i < NRX; i++)
		setdesc(RMD(ln, i), PHYS(ln, O_RBUF + BUFSZ * i), D_OWN, BUFSZ);
	ln->ln_Rx = 0;

	ib = (volatile UWORD *)ln->ln_Mem;
	ib[0] = ln->ln_Mode | (ln->ln_Prom ? MODE_PROM : 0);
	for (i = 0; i < 3; i++)
		ib[1 + i] = ln->ln_Addr[2 * i + 1] << 8 | ln->ln_Addr[2 * i];
	for (i = 0; i < 4; i++)
		ib[4 + i] = ln->ln_Hash[i];
	a = PHYS(ln, O_RMD);
	ib[8] = (UWORD)a;
	ib[9] = NRXLOG << 13 | (UWORD)(a >> 16) & 0xff;
	a = PHYS(ln, O_TMD);
	ib[10] = (UWORD)a;
	ib[11] = NTXLOG << 13 | (UWORD)(a >> 16) & 0xff;
	a = ln->ln_Phys;
	csr(1, (UWORD)a);
	csr(2, (UWORD)(a >> 16) & 0xff);
	csr(3, CSR3_MODE);
	csr(0, CSR0_INIT);
	for (i = 0; (inw(RDP) & CSR0_IDON) == 0; i++)
		if (i == INITWAIT) {
			outw(RDP, CSR0_STOP);
			return (0);
		}
	outw(RDP, CSR0_IDON | CSR0_STRT | CSR0_INEA);
	if (ln->ln_TxCount != 0)
		outw(RDP, CSR0_INEA | CSR0_TDMD);
	return (1);
}

static int
lnserv(ln, n)
register struct LnBase *ln;
UWORD n;
{
	register UWORD c;
	register int i, restart;

	c = inw(RDP);
	if ((c & CSR0_INTR) == 0)
		return (0);
	for (i = 0; i < 8 && (c & CSR0_INTR); i++) {
		outw(RDP, c & CSR0_ACK | CSR0_INEA);
		restart = 0;
		if (c & CSR0_MISS) {
			ln->ln_Stats.Overruns++;
			lnevent(ln, S2EVENT_RX | S2EVENT_ERROR);
		}
		if (c & CSR0_RINT)
			lnrx(ln);
		if (c & CSR0_TINT)
			restart = lntxdone(ln);
		if (c & (CSR0_BABL | CSR0_MERR)) {
			lnevent(ln, S2EVENT_HARDWARE | S2EVENT_ERROR);
			restart = 1;
		}
		if (restart && !lnstart(ln)) {
			lnevent(ln, S2EVENT_HARDWARE | S2EVENT_ERROR);
			return (1);
		}
		lntxfill(ln);
		c = inw(RDP);
	}
	return (1);
}

/* Restart the chip after a filter change, if it is running. */
static int
lnreconf(ln)
register struct LnBase *ln;
{
	int s, ok;

	ok = 1;
	s = intoff();
	if (ln->ln_State & LN_ONLINE)
		ok = lnstart(ln);
	intrest(s);
	return (ok);
}

static int
lnonline(ln, ior)
register struct LnBase *ln;
register struct IOSana2Req *ior;
{
	ULONG t;
	int s;

	if ((ln->ln_State & LN_CONFIG) == 0) {
		ior->ios2_WireError = S2WERR_NOT_CONFIGURED;
		return (S2ERR_BAD_STATE);
	}
	if (ln->ln_State & LN_ONLINE)
		return (0);
	s = intoff();
	if (!lnstart(ln)) {
		intrest(s);
		ior->ios2_WireError = S2WERR_RCVREL_HDW_ERR;
		return (S2ERR_OUTOFSERVICE);
	}
	ln->ln_State |= LN_ONLINE;
	t = SysBase->ex_TickCount;
	ln->ln_Stats.LastStart.tv_secs = t / TICKHZ;
	ln->ln_Stats.LastStart.tv_micro = (t % TICKHZ) * (1000000L / TICKHZ);
	ln->ln_Stats.Reconfigurations++;
	lnevent(ln, S2EVENT_ONLINE);
	intrest(s);
	return (0);
}

/* Take the unit out of service: the chip stops, and every write and
 * read returns S2ERR_OUTOFSERVICE.  VI off. */
static void
lnoffline(ln)
register struct LnBase *ln;
{
	register struct MinNode *o;

	if ((ln->ln_State & LN_ONLINE) == 0)
		return;
	ln->ln_State &= ~LN_ONLINE;
	outw(RDP, CSR0_STOP);
	lntxdone(ln);
	for (; ln->ln_TxCount != 0; ln->ln_TxCount--) {
		lnreply(ln->ln_TxReq[ln->ln_TxTail], S2ERR_OUTOFSERVICE,
		    (ULONG)S2WERR_UNIT_OFFLINE);
		ln->ln_TxTail = (ln->ln_TxTail + 1) & (NTX - 1);
	}
	lnflush(&ln->ln_Write, S2ERR_OUTOFSERVICE, (ULONG)S2WERR_UNIT_OFFLINE);
	lnflush(&ln->ln_Orphan, S2ERR_OUTOFSERVICE,
	    (ULONG)S2WERR_UNIT_OFFLINE);
	for (o = ln->ln_Openers.mlh_Head; o->mln_Succ != NULL;
	    o = o->mln_Succ)
		lnflush(&((struct LnOpener *)o)->lo_Read, S2ERR_OUTOFSERVICE,
		    (ULONG)S2WERR_UNIT_OFFLINE);
	lnevent(ln, S2EVENT_OFFLINE);
}

/* Queue ior on l, unless the unit is offline; tx: l is the write
 * queue. */
static int
lnqueue(ln, ior, l, tx)
struct LnBase *ln;
register struct IOSana2Req *ior;
struct MinList *l;
int tx;
{
	int s;

	s = intoff();
	if ((ln->ln_State & LN_ONLINE) == 0) {
		intrest(s);
		ior->ios2_WireError = S2WERR_UNIT_OFFLINE;
		return (S2ERR_OUTOFSERVICE);
	}
	ior->ios2_Req.io_Flags &= ~IOF_QUICK;
	AddTail((struct List *)l, (struct Node *)ior);
	if (tx)
		lntxfill(ln);
	intrest(s);
	return (QUEUED);
}

static int
lnwrite(ln, ior)
register struct LnBase *ln;
register struct IOSana2Req *ior;
{
	register struct LnOpener *op;

	op = OPENER(ior);
	if (op->lo_FromBuff == NULL) {
		ior->ios2_WireError = S2WERR_BUFF_ERROR;
		return (S2ERR_BAD_ARGUMENT);
	}
	if (ior->ios2_DataLength > (ior->ios2_Req.io_Flags & SANA2IOF_RAW ?
	    ETHERMTU + HDRLEN : ETHERMTU))
		return (S2ERR_MTU_EXCEEDED);
	if (ior->ios2_Req.io_Flags & SANA2IOF_RAW &&
	    ior->ios2_DataLength < HDRLEN)
		return (S2ERR_BAD_ARGUMENT);
	if (ior->ios2_Req.io_Command == S2_MULTICAST &&
	    (ior->ios2_DstAddr[0] & 1) == 0) {
		ior->ios2_WireError = S2WERR_BAD_MULTICAST;
		return (S2ERR_BAD_ADDRESS);
	}
	return (lnqueue(ln, ior, &ln->ln_Write, 1));
}

static int
lnread(ln, ior, l)
struct LnBase *ln;
register struct IOSana2Req *ior;
struct MinList *l;
{
	if (OPENER(ior)->lo_ToBuff == NULL) {
		ior->ios2_WireError = S2WERR_BUFF_ERROR;
		return (S2ERR_BAD_ARGUMENT);
	}
	return (lnqueue(ln, ior, l, 0));
}

static int
lnmcast(ln, ior, add)
register struct LnBase *ln;
register struct IOSana2Req *ior;
int add;
{
	register struct LnMcast *m;
	int i;

	if ((ior->ios2_SrcAddr[0] & 1) == 0) {
		ior->ios2_WireError = S2WERR_BAD_MULTICAST;
		return (S2ERR_BAD_ADDRESS);
	}
	m = lnfindmc(ln, ior->ios2_SrcAddr);
	if (!add) {
		if (m == NULL) {
			ior->ios2_WireError = S2WERR_BAD_MULTICAST;
			return (S2ERR_BAD_STATE);
		}
		if (--m->lm_Count != 0)
			return (0);
	} else if (m != NULL) {
		m->lm_Count++;
		return (0);
	} else {
		for (i = 0, m = ln->ln_Mcast; m->lm_Count != 0; i++, m++)
			if (i == NMCAST - 1) {
				ior->ios2_WireError = S2WERR_MULTICAST_FULL;
				return (S2ERR_NO_RESOURCES);
			}
		for (i = 0; i < ADDRLEN; i++)
			m->lm_Addr[i] = ior->ios2_SrcAddr[i];
		m->lm_Count = 1;
	}
	lnrehash(ln);
	return (lnreconf(ln) ? 0 : S2ERR_OUTOFSERVICE);
}

static int
lntrack(ln, ior)
register struct LnBase *ln;
register struct IOSana2Req *ior;
{
	register struct LnTrack *t;
	ULONG type;
	int s, err, i;

	type = ior->ios2_PacketType;
	err = 0;
	s = intoff();
	t = lnfindtt(ln, type);
	switch (ior->ios2_Req.io_Command) {
	case S2_TRACKTYPE:
		if (t != NULL) {
			ior->ios2_WireError = S2WERR_ALREADY_TRACKED;
			err = S2ERR_BAD_STATE;
			break;
		}
		for (i = 0, t = ln->ln_Track; t->lt_Used; i++, t++)
			if (i == NTRACK - 1) {
				err = S2ERR_NO_RESOURCES;
				goto out;
			}
		t->lt_Type = type;
		t->lt_Used = 1;
		t->lt_Stats.PacketsSent = t->lt_Stats.PacketsReceived = 0;
		t->lt_Stats.BytesSent = t->lt_Stats.BytesReceived = 0;
		t->lt_Stats.PacketsDropped = 0;
		break;
	default:
		if (t == NULL) {
			ior->ios2_WireError = S2WERR_NOT_TRACKED;
			err = S2ERR_BAD_STATE;
		} else if (ior->ios2_Req.io_Command == S2_UNTRACKTYPE)
			t->lt_Used = 0;
		else
			*(struct Sana2PacketTypeStats *)ior->ios2_StatData =
			    t->lt_Stats;
	}
out:
	intrest(s);
	return (err);
}

static void
lnquery(q)
register struct Sana2DeviceQuery *q;
{
	struct Sana2DeviceQuery dq;
	ULONG n;

	n = q->SizeAvailable;
	if (n > sizeof(dq))
		n = sizeof(dq);
	dq.SizeAvailable = q->SizeAvailable;
	dq.SizeSupplied = n;
	dq.DevQueryFormat = 0;
	dq.DeviceLevel = 0;
	dq.AddrFieldSize = 8 * ADDRLEN;
	dq.MTU = ETHERMTU;
	dq.BPS = 10000000L;
	dq.HardwareType = S2WireType_Ethernet;
	dq.RawMTU = ETHERMTU + HDRLEN;
	CopyMem((APTR)&dq, (APTR)q, n);
}

static void
lnspecial(ln, h)
register struct LnBase *ln;
register struct Sana2SpecialStatHeader *h;
{
	register struct Sana2SpecialStatRecord *r;
	register int i;
	int s;

	r = (struct Sana2SpecialStatRecord *)(h + 1);
	s = intoff();
	for (i = 0; i < NSPECIAL && i < h->RecordCountMax; i++) {
		r[i].Type = S2SS_ETHERNET_BADMULTICAST + i;
		r[i].Count = ln->ln_Special[i];
		r[i].String = lnssname[i];
	}
	intrest(s);
	h->RecordCountSupplied = i;
}

static void
lnbegin(dev, ior)
struct Device *dev;
register struct IOSana2Req *ior;
{
	register struct LnBase *ln;
	register int i;
	struct MinNode *o;
	int err, s;

	IOBEGIN(&ior->ios2_Req);
	ln = lnbase;
	if (ior->ios2_Req.io_Command != S2_ONEVENT)
		ior->ios2_WireError = 0;
	err = 0;
	switch (ior->ios2_Req.io_Command) {
	case CMD_READ:
		err = lnread(ln, ior, &OPENER(ior)->lo_Read);
		break;
	case S2_READORPHAN:
		err = lnread(ln, ior, &ln->ln_Orphan);
		break;
	case CMD_WRITE:
	case S2_BROADCAST:
	case S2_MULTICAST:
		err = lnwrite(ln, ior);
		break;
	case S2_ONEVENT:
		if (ior->ios2_WireError & ~0xffL) {
			ior->ios2_WireError = S2WERR_BAD_EVENT;
			err = S2ERR_NOT_SUPPORTED;
			break;
		}
		s = intoff();
		if (ior->ios2_WireError & (ln->ln_State & LN_ONLINE ?
		    S2EVENT_ONLINE : S2EVENT_OFFLINE)) {
			ior->ios2_WireError &= ln->ln_State & LN_ONLINE ?
			    S2EVENT_ONLINE : S2EVENT_OFFLINE;
			intrest(s);
			break;
		}
		ior->ios2_Req.io_Flags &= ~IOF_QUICK;
		AddTail((struct List *)&ln->ln_Event, (struct Node *)ior);
		intrest(s);
		return;
	case S2_DEVICEQUERY:
		lnquery((struct Sana2DeviceQuery *)ior->ios2_StatData);
		break;
	case S2_GETSTATIONADDRESS:
		for (i = 0; i < ADDRLEN; i++) {
			ior->ios2_SrcAddr[i] = ln->ln_Addr[i];
			ior->ios2_DstAddr[i] = ln->ln_Default[i];
		}
		break;
	case S2_CONFIGINTERFACE:
		if (ln->ln_State & LN_CONFIG) {
			ior->ios2_WireError = S2WERR_IS_CONFIGURED;
			err = S2ERR_BAD_STATE;
			break;
		}
		if (ior->ios2_SrcAddr[0] & 1) {
			ior->ios2_WireError = S2WERR_SRC_ADDRESS;
			err = S2ERR_BAD_ADDRESS;
			break;
		}
		for (i = 0; i < ADDRLEN; i++)
			ln->ln_Addr[i] = ior->ios2_SrcAddr[i];
		ln->ln_State |= LN_CONFIG;
		err = lnonline(ln, ior);
		break;
	case S2_ONLINE:
		err = lnonline(ln, ior);
		break;
	case S2_OFFLINE:
		s = intoff();
		lnoffline(ln);
		intrest(s);
		break;
	case S2_ADDMULTICASTADDRESS:
	case S2_DELMULTICASTADDRESS:
		err = lnmcast(ln, ior,
		    ior->ios2_Req.io_Command == S2_ADDMULTICASTADDRESS);
		break;
	case S2_TRACKTYPE:
	case S2_UNTRACKTYPE:
	case S2_GETTYPESTATS:
		err = lntrack(ln, ior);
		break;
	case S2_GETGLOBALSTATS:
		s = intoff();
		*(struct Sana2DeviceStats *)ior->ios2_StatData = ln->ln_Stats;
		intrest(s);
		break;
	case S2_GETSPECIALSTATS:
		lnspecial(ln, (struct Sana2SpecialStatHeader *)
		    ior->ios2_StatData);
		break;
	case CMD_FLUSH:
		s = intoff();
		lnflush(&ln->ln_Write, IOERR_ABORTED, 0L);
		lnflush(&ln->ln_Orphan, IOERR_ABORTED, 0L);
		lnflush(&ln->ln_Event, IOERR_ABORTED, 0L);
		for (o = ln->ln_Openers.mlh_Head; o->mln_Succ != NULL;
		    o = o->mln_Succ)
			lnflush(&((struct LnOpener *)o)->lo_Read,
			    IOERR_ABORTED, 0L);
		intrest(s);
		break;
	default:
		err = IOERR_NOCMD;
	}
	if (err == QUEUED)
		return;
	ior->ios2_Req.io_Error = err;
	TermIO(&ior->ios2_Req);
}

/* Unlink ior if it is still on l.  VI off. */
static int
lnunlink(l, ior)
struct MinList *l;
register struct IORequest *ior;
{
	register struct MinNode *n;

	for (n = l->mlh_Head; n->mln_Succ != NULL; n = n->mln_Succ)
		if ((struct IORequest *)n == ior) {
			Remove((struct Node *)n);
			ior->io_Error = IOERR_ABORTED;
			ReplyMsg(&ior->io_Message);
			return (0);
		}
	return (-1);
}

/* The first terminal commit wins: a write the chip holds, or a request
 * already replied, is left alone. */
static LONG
lnabort(dev, ior)
struct Device *dev;
register struct IORequest *ior;
{
	register struct LnBase *ln;
	register struct MinList *l;
	LONG r;
	int s;

	ln = lnbase;
	switch (ior->io_Command) {
	case CMD_READ:
		l = &OPENER(ior)->lo_Read;
		break;
	case S2_READORPHAN:
		l = &ln->ln_Orphan;
		break;
	case S2_ONEVENT:
		l = &ln->ln_Event;
		break;
	default:
		l = &ln->ln_Write;
	}
	s = intoff();
	r = lnunlink(l, ior);
	intrest(s);
	return (r);
}

/* The first open of a unit allocates the DMA block; the unit stays
 * unconfigured and offline until S2_CONFIGINTERFACE. */
static void
lnopen(dev, ior, unit, flags)
struct Device *dev;
register struct IOSana2Req *ior;
ULONG unit, flags;
{
	register struct LnBase *ln;
	register struct LnOpener *op;
	register struct TagItem *tag;
	UBYTE *p;
	ULONG n;
	int s;

	ln = lnbase;
	if (unit >= NUNIT) {
		ior->ios2_Req.io_Error = IOERR_OPENFAIL;
		return;
	}
	if (ln->ln_Unit.unit_OpenCnt != 0 && (unit != ln->ln_UnitNum ||
	    ln->ln_State & LN_MINE || flags & SANA2OPF_MINE)) {
		ior->ios2_Req.io_Error = IOERR_UNITBUSY;
		return;
	}
	op = (struct LnOpener *)AllocMem((ULONG)sizeof(*op),
	    MEMF_PUBLIC | MEMF_CLEAR);
	if (op == NULL) {
		ior->ios2_Req.io_Error = IOERR_OPENFAIL;
		return;
	}
	if (ln->ln_Unit.unit_OpenCnt == 0) {
		ln->ln_Mem = (UBYTE *)AllocMem(MEMSIZE,
		    MEMF_PUBLIC | MEMF_24BITDMA);
		n = MEMSIZE;
		if (ln->ln_Mem != NULL)
			ln->ln_Phys = (ULONG)CachePreDMA((APTR)ln->ln_Mem, &n,
			    0L);
		if (ln->ln_Mem == NULL || n != MEMSIZE) {
			if (ln->ln_Mem != NULL)
				FreeMem((APTR)ln->ln_Mem, MEMSIZE);
			FreeMem((APTR)op, (ULONG)sizeof(*op));
			ior->ios2_Req.io_Error = IOERR_OPENFAIL;
			return;
		}
		ln->ln_UnitNum = unit;
		ln->ln_Mode = lnmodes[unit];
		ln->ln_State = 0;
		for (s = 0; s < NTX; s++)
			ln->ln_TxBuf[s] = s;
		ln->ln_TxHead = ln->ln_TxTail = ln->ln_TxCount = 0;
		CopyMem((APTR)ln->ln_Default, (APTR)ln->ln_Addr, (ULONG)ADDRLEN);
		p = (UBYTE *)ln->ln_Hash;
		for (s = (UBYTE *)(ln + 1) - p; s > 0; s--)
			*p++ = 0;
	}
	if (flags & SANA2OPF_MINE)
		ln->ln_State |= LN_MINE;
	NewList((struct List *)&op->lo_Read);
	for (tag = (struct TagItem *)ior->ios2_BufferManagement;
	    tag != NULL && tag->ti_Tag != TAG_DONE; tag++)
		if (tag->ti_Tag == TAG_MORE)
			tag = (struct TagItem *)tag->ti_Data - 1;
		else if (tag->ti_Tag == TAG_SKIP)
			tag += tag->ti_Data;
		else if (tag->ti_Tag == S2_CopyToBuff && op->lo_ToBuff == NULL ||
		    tag->ti_Tag == S2_CopyToBuff16)
			op->lo_ToBuff = (int (*)())tag->ti_Data;
		else if (tag->ti_Tag == S2_CopyFromBuff &&
		    op->lo_FromBuff == NULL || tag->ti_Tag == S2_CopyFromBuff16)
			op->lo_FromBuff = (int (*)())tag->ti_Data;
		else if (tag->ti_Tag == S2_PacketFilter)
			op->lo_Filter = (struct Hook *)tag->ti_Data;
	ior->ios2_BufferManagement = (APTR)op;
	s = intoff();
	AddTail((struct List *)&ln->ln_Openers, (struct Node *)op);
	intrest(s);
	if (flags & SANA2OPF_PROM) {
		op->lo_Prom = 1;
		if (ln->ln_Prom++ == 0)
			lnreconf(ln);
	}
	ior->ios2_Req.io_Unit = &ln->ln_Unit;
	ln->ln_Unit.unit_OpenCnt++;
	dev->dd_Library.lib_OpenCnt++;
}

/* Closing with requests pending is misuse.  The last close takes the
 * unit offline and frees the DMA block; the next open starts
 * unconfigured. */
static APTR
lnclose(dev, ior)
struct Device *dev;
register struct IOSana2Req *ior;
{
	register struct LnBase *ln;
	register struct LnOpener *op;
	int s;

	ln = lnbase;
	op = OPENER(ior);
	s = intoff();
	Remove((struct Node *)op);
	intrest(s);
	if (--ln->ln_Unit.unit_OpenCnt == 0) {
		s = intoff();
		lnoffline(ln);
		lnflush(&ln->ln_Event, IOERR_ABORTED, 0L);
		intrest(s);
		FreeMem((APTR)ln->ln_Mem, MEMSIZE);
		ln->ln_Mem = NULL;
		ln->ln_State = 0;
		ln->ln_Prom = 0;
	} else if (op->lo_Prom && --ln->ln_Prom == 0)
		lnreconf(ln);
	FreeMem((APTR)op, (ULONG)sizeof(*op));
	dev->dd_Library.lib_OpenCnt--;
	return (NULL);
}

static APTR
lnnull()
{
	return (NULL);
}

static APTR lnfuncs[] = {
	(APTR)lnopen, (APTR)lnclose, (APTR)lnnull, (APTR)lnnull,
	(APTR)lnbegin, (APTR)lnabort, (APTR)-1L
};

static void lninit();

struct Resident lntag = {
	RTC_MATCHWORD, &lntag, (APTR)(&lntag + 1), RTF_COLDSTART, 1,
	NT_DEVICE, 0, lnname, NULL, (APTR)lninit
};

/* The card answers when RAP reads back what was written and a stopped
 * chip's CSR0 reads STOP alone; a floating bus that echoes the last
 * write fails the second test. */
static int
lnprobe()
{
	outw(RAP, 3);
	if (inw(RAP) != 3)
		return (0);
	outw(RAP, 0);
	outw(RDP, CSR0_STOP);
	return (inw(RDP) == CSR0_STOP);
}

static void
lninit()
{
	register struct LnBase *ln;
	register int i;
	UWORD w;

	if (!lnprobe()) {
		KPutStr("lance.device: no card\n");
		return;
	}
	ln = (struct LnBase *)MakeLibrary((APTR)lnfuncs, NULL, NULL,
	    (ULONG)sizeof(struct LnBase), NULL);
	if (ln == NULL) {
		KPutStr("lance.device: no memory\n");
		return;
	}
	lnbase = ln;
	ln->ln_Dev.dd_Library.lib_Node.ln_Name = lnname;
	ln->ln_Dev.dd_Library.lib_Version = 1;
	NewList((struct List *)&ln->ln_Openers);
	NewList((struct List *)&ln->ln_Orphan);
	NewList((struct List *)&ln->ln_Event);
	NewList((struct List *)&ln->ln_Write);
	for (i = 0; i < 3; i++) {
		w = inw(PROM + 2 * i);
		ln->ln_Default[2 * i] = w >> 8;
		ln->ln_Default[2 * i + 1] = w;
	}
	ln->ln_Int.is_Node.ln_Pri = 0;
	ln->ln_Int.is_Node.ln_Name = lnname;
	ln->ln_Int.is_Data = (APTR)ln;
	ln->ln_Int.is_Code = lnserv;
	AddIntServer((UWORD)INTV_LANCE, &ln->ln_Int);
	AddDevice(&ln->ln_Dev);
}
