/* SPDX-License-Identifier: MIT */
/*
 * lance.device in the chip's internal (unit 1) and external (unit 2)
 * loopback: unit exclusion, the query and address commands, the
 * offline state, configuration and the online event, then frames of at
 * most 32 bytes sent to the unit's own address, broadcast and a joined
 * multicast group and received by type (two openers, the packet
 * filter, raw and orphan reads), abort and flush, statistics and type
 * tracking, offline and back online, and a burst longer than both
 * rings, which also measures the server's interrupt stack depth.
 */
/* LANCE: loop */
#include "lantest.h"

#define T1	0x88b5L
#define T2	0x88b6L
#define TORPH	0x1234L
#define PAYLOAD	18		/* a 32-byte frame */
#define BURST	12

static UBYTE station[ADDRLEN];
static UBYTE txd[PAYLOAD], rxd[BURST][PAYLOAD + HDRLEN];
static UBYTE group[ADDRLEN] = { 0x01, 0x00, 0x5e, 0x00, 0x00, 0x01 };
static UBYTE group2[ADDRLEN] = { 0x01, 0x00, 0x5e, 0x00, 0x00, 0x02 };
static struct IOSana2Req *r[BURST], *w[BURST];

#define INTSTK		512		/* kernel/interrupts.c INTSTKSIZE */
#define INTSTKMAX	448
#define PAINT		0x6b

/* Paint the interrupt stack below its top, or measure the painted bytes
 * since overwritten. */
static int
intstack(paint)
int paint;
{
	register UBYTE *p;
	register int i;

	p = (UBYTE *)MKPTR(SEGBASE(STACKSEG), kintstk - INTSTK);
	if (paint) {
		Disable();
		for (i = 0; i < INTSTK - 64; i++)
			p[i] = PAINT;
		Enable();
		return (0);
	}
	for (i = 0; i < INTSTK - 64 && p[i] == PAINT; i++)
		;
	return (INTSTK - i);
}

static void
fill(seed)
int seed;
{
	register int i;

	for (i = 0; i < PAYLOAD; i++)
		txd[i] = seed * 13 + i;
}

static void
setaddr(a, b)
register UBYTE *a, *b;
{
	register int i;

	for (i = 0; i < ADDRLEN; i++)
		a[i] = b[i];
}

/* Send a frame of the PAYLOAD bytes in txd: cmd to dst. */
static int
snd(a, cmd, dst, type)
struct IOSana2Req *a;
int cmd;
UBYTE *dst;
ULONG type;
{
	lnset(a, cmd, type, (APTR)txd, (ULONG)PAYLOAD);
	if (dst != NULL)
		setaddr(a->ios2_DstAddr, dst);
	return (DoIO((struct IORequest *)a));
}

static void
rd(q, cmd, type, buf, flags)
struct IOSana2Req *q;
int cmd;
ULONG type;
UBYTE *buf;
int flags;
{
	lnset(q, cmd, type, (APTR)buf, 0L);
	q->ios2_Req.io_Flags = flags;
	BeginIO(q);
}

/* q got txd from the station with the given type and flags. */
static int
got(q, type, buf, flags)
register struct IOSana2Req *q;
ULONG type;
UBYTE *buf;
int flags;
{
	return (lnwait(q) == 0 && q->ios2_PacketType == type &&
	    q->ios2_DataLength == PAYLOAD && lnsame(buf, txd, PAYLOAD) &&
	    lnsame(q->ios2_SrcAddr, station, ADDRLEN) &&
	    (q->ios2_Req.io_Flags & (SANA2IOF_BCAST | SANA2IOF_MCAST)) ==
	    flags);
}

static char *
setup(a, unit)
struct IOSana2Req *a;
int unit;
{
	struct IOSana2Req *b;
	struct Sana2DeviceQuery q;

	if ((b = lnreq(0)) == NULL)
		return ("CreateIORequest");
	if (OpenDevice(LANCENAME, (ULONG)(3 - unit), b, 0L) != IOERR_UNITBUSY)
		return ("other unit opened");
	if (OpenDevice(LANCENAME, (ULONG)unit, b, (ULONG)SANA2OPF_MINE) !=
	    IOERR_UNITBUSY)
		return ("exclusive open of an open unit");
	DeleteIORequest(b);
	q.SizeAvailable = sizeof q;
	a->ios2_StatData = (APTR)&q;
	if (lndo(a, S2_DEVICEQUERY) != 0 || q.SizeSupplied != sizeof q ||
	    q.AddrFieldSize != 48 || q.MTU != 1500 || q.BPS != 10000000L ||
	    q.HardwareType != S2WireType_Ethernet || q.RawMTU != 1514)
		return ("S2_DEVICEQUERY");
	if (lndo(a, S2_GETSTATIONADDRESS) != 0 ||
	    !lnsame(a->ios2_SrcAddr, a->ios2_DstAddr, ADDRLEN) ||
	    a->ios2_DstAddr[0] & 1)
		return ("S2_GETSTATIONADDRESS");
	setaddr(station, a->ios2_DstAddr);
	if (snd(a, CMD_WRITE, station, T1) != S2ERR_OUTOFSERVICE ||
	    a->ios2_WireError != S2WERR_UNIT_OFFLINE)
		return ("write while offline");
	if (lndo(a, S2_ONLINE) != S2ERR_BAD_STATE ||
	    a->ios2_WireError != S2WERR_NOT_CONFIGURED)
		return ("S2_ONLINE before configuration");
	b = lncopyreq(a);
	b->ios2_Req.io_Command = S2_ONEVENT;
	b->ios2_WireError = S2EVENT_ONLINE | S2EVENT_TX;
	SendIO((struct IORequest *)b);
	Sleep(1);
	if (CheckIO((struct IORequest *)b) != NULL)
		return ("online event while offline");
	setaddr(a->ios2_SrcAddr, station);
	if (lndo(a, S2_CONFIGINTERFACE) != 0)
		return ("S2_CONFIGINTERFACE");
	if (lnwait(b) != 0 || b->ios2_WireError != S2EVENT_ONLINE)
		return ("online event");
	DeleteIORequest(b);
	if (lndo(a, S2_CONFIGINTERFACE) != S2ERR_BAD_STATE ||
	    a->ios2_WireError != S2WERR_IS_CONFIGURED)
		return ("second S2_CONFIGINTERFACE");
	a->ios2_PacketType = T1;
	if (lndo(a, S2_TRACKTYPE) != 0 || lndo(a, S2_TRACKTYPE) !=
	    S2ERR_BAD_STATE || a->ios2_WireError != S2WERR_ALREADY_TRACKED)
		return ("S2_TRACKTYPE");
	return (NULL);
}

static char *
frames(a, unit)
struct IOSana2Req *a;
int unit;
{
	struct IOSana2Req *b, *ra1, *ra2, *oa, *rb1;
	UBYTE b1[64], b2[64], b3[64], b4[64];

	if ((b = lnreq(1)) == NULL)
		return ("CreateIORequest");
	if (OpenDevice(LANCENAME, (ULONG)unit, b, 0L) != 0)
		return ("second opener");
	ra1 = lncopyreq(a);
	ra2 = lncopyreq(a);
	oa = lncopyreq(a);
	rb1 = lncopyreq(b);
	rd(ra1, CMD_READ, T1, b1, 0);
	rd(ra2, CMD_READ, T2, b2, 0);
	rd(oa, S2_READORPHAN, 0L, b3, 0);
	rd(rb1, CMD_READ, T1, b4, 0);
	lnfiltered = 0;
	fill(1);
	if (snd(a, CMD_WRITE, station, T1) != 0)
		return ("CMD_WRITE");
	if (!got(ra1, T1, b1, 0) || !lnsame(ra1->ios2_DstAddr, station,
	    ADDRLEN))
		return ("frame to the first opener");
	if (!got(rb1, T1, b4, 0) || lnfiltered != 1)
		return ("frame to the second opener");
	fill(2);
	if (snd(a, CMD_WRITE, station, TORPH) != 0 || !got(oa, TORPH, b3, 0))
		return ("orphan");
	if (CheckIO((struct IORequest *)ra2) != NULL || AbortIO(ra2) != 0 ||
	    WaitIO((struct IORequest *)ra2) != IOERR_ABORTED)
		return ("AbortIO of a read");
	rd(ra1, CMD_READ, T1, b1, SANA2IOF_RAW);
	fill(3);
	if (snd(a, S2_BROADCAST, NULL, T1) != 0 || lnwait(ra1) != 0 ||
	    ra1->ios2_DataLength != PAYLOAD + HDRLEN ||
	    !(ra1->ios2_Req.io_Flags & SANA2IOF_BCAST) ||
	    b1[0] != 0xff || b1[5] != 0xff || b1[12] != 0x88 ||
	    b1[13] != 0xb5 || !lnsame(b1 + 6, station, ADDRLEN) ||
	    !lnsame(b1 + HDRLEN, txd, PAYLOAD))
		return ("raw broadcast");
	setaddr(a->ios2_SrcAddr, group);
	if (lndo(a, S2_ADDMULTICASTADDRESS) != 0)
		return ("S2_ADDMULTICASTADDRESS");
	setaddr(a->ios2_SrcAddr, group2);
	if (lndo(a, S2_DELMULTICASTADDRESS) != S2ERR_BAD_STATE ||
	    a->ios2_WireError != S2WERR_BAD_MULTICAST)
		return ("S2_DELMULTICASTADDRESS of an unknown group");
	if (snd(a, S2_MULTICAST, station, T1) != S2ERR_BAD_ADDRESS)
		return ("S2_MULTICAST to a station");
	rd(ra1, CMD_READ, T1, b1, 0);
	fill(4);
	if (snd(a, S2_MULTICAST, group, T1) != 0 ||
	    !got(ra1, T1, b1, SANA2IOF_MCAST))
		return ("multicast");
	setaddr(a->ios2_SrcAddr, group);
	if (lndo(a, S2_DELMULTICASTADDRESS) != 0)
		return ("S2_DELMULTICASTADDRESS");
	CloseDevice((struct IORequest *)b);
	DeleteIORequest(b);
	DeleteIORequest(rb1);
	DeleteIORequest(oa);
	DeleteIORequest(ra2);
	DeleteIORequest(ra1);
	return (NULL);
}

static char *
stats(a)
struct IOSana2Req *a;
{
	struct Sana2PacketTypeStats ts;
	struct Sana2DeviceStats ds;
	struct {
		struct Sana2SpecialStatHeader h;
		struct Sana2SpecialStatRecord r[4];
	} ss;

	a->ios2_PacketType = T1;
	a->ios2_StatData = (APTR)&ts;
	if (lndo(a, S2_GETTYPESTATS) != 0 || ts.PacketsSent != 3 ||
	    ts.PacketsReceived != 3 || ts.BytesSent != 3 * PAYLOAD ||
	    ts.BytesReceived != 3 * PAYLOAD || ts.PacketsDropped != 0)
		return ("S2_GETTYPESTATS");
	a->ios2_StatData = (APTR)&ds;
	if (lndo(a, S2_GETGLOBALSTATS) != 0 || ds.PacketsSent != 4 ||
	    ds.PacketsReceived != 4 || ds.BadData != 0 ||
	    ds.UnknownTypesReceived != 1 || ds.Reconfigurations != 1)
		return ("S2_GETGLOBALSTATS");
	ss.h.RecordCountMax = 4;
	a->ios2_StatData = (APTR)&ss;
	if (lndo(a, S2_GETSPECIALSTATS) != 0 || ss.h.RecordCountSupplied != 3 ||
	    ss.r[0].Type != S2SS_ETHERNET_BADMULTICAST ||
	    ss.r[2].Type != S2SS_ETHERNET_FIFO_UNDERRUNS ||
	    ss.r[2].Count != 0 || ss.r[1].String == NULL)
		return ("S2_GETSPECIALSTATS");
	a->ios2_StatData = (APTR)&ts;
	if (lndo(a, S2_UNTRACKTYPE) != 0 || lndo(a, S2_GETTYPESTATS) !=
	    S2ERR_BAD_STATE || a->ios2_WireError != S2WERR_NOT_TRACKED)
		return ("S2_UNTRACKTYPE");
	lnset(a, CMD_WRITE, T1, (APTR)txd, 1501L);
	if (DoIO((struct IORequest *)a) != S2ERR_MTU_EXCEEDED)
		return ("write over the MTU");
	return (NULL);
}

static char *
service(a)
struct IOSana2Req *a;
{
	struct IOSana2Req *q, *e;
	UBYTE buf[64];

	q = lncopyreq(a);
	e = lncopyreq(a);
	rd(q, CMD_READ, T2, buf, 0);
	e->ios2_Req.io_Command = S2_ONEVENT;
	e->ios2_WireError = S2EVENT_TX;
	SendIO((struct IORequest *)e);
	if (lndo(a, CMD_FLUSH) != 0 || lnwait(q) != IOERR_ABORTED ||
	    lnwait(e) != IOERR_ABORTED)
		return ("CMD_FLUSH");
	rd(q, CMD_READ, T2, buf, 0);
	e->ios2_Req.io_Command = S2_ONEVENT;
	e->ios2_WireError = S2EVENT_OFFLINE;
	SendIO((struct IORequest *)e);
	if (lndo(a, S2_OFFLINE) != 0 || lnwait(q) != S2ERR_OUTOFSERVICE ||
	    lnwait(e) != 0 || e->ios2_WireError != S2EVENT_OFFLINE)
		return ("S2_OFFLINE");
	if (snd(a, CMD_WRITE, station, T1) != S2ERR_OUTOFSERVICE)
		return ("write while offline");
	e->ios2_WireError = S2EVENT_ONLINE | S2EVENT_RX;
	if (lndo(a, S2_ONLINE) != 0 || lndo(e, S2_ONEVENT) != 0 ||
	    e->ios2_WireError != S2EVENT_ONLINE)
		return ("S2_ONLINE");
	e->ios2_WireError = 0x100;
	if (lndo(e, S2_ONEVENT) != S2ERR_NOT_SUPPORTED ||
	    e->ios2_WireError != S2WERR_BAD_EVENT)
		return ("unknown event");
	rd(q, CMD_READ, T2, buf, 0);
	fill(5);
	if (snd(a, CMD_WRITE, station, T2) != 0 || !got(q, T2, buf, 0))
		return ("frame after S2_ONLINE");
	DeleteIORequest(e);
	DeleteIORequest(q);
	return (NULL);
}

/* BURST reads, then BURST writes sent at once: more than both rings. */
static char *
burst(a)
struct IOSana2Req *a;
{
	register int i;

	intstack(1);
	for (i = 0; i < BURST; i++) {
		r[i] = lncopyreq(a);
		w[i] = lncopyreq(a);
		if (r[i] == NULL || w[i] == NULL)
			return ("CreateIORequest");
		rd(r[i], CMD_READ, T1, rxd[i], SANA2IOF_RAW);
	}
	fill(6);
	for (i = 0; i < BURST; i++) {
		lnset(w[i], CMD_WRITE, T1, (APTR)txd, (ULONG)PAYLOAD - 1);
		setaddr(w[i]->ios2_DstAddr, station);
		SendIO((struct IORequest *)w[i]);
	}
	for (i = 0; i < BURST; i++)
		if (lnwait(w[i]) != 0)
			return ("burst write");
	for (i = 0; i < BURST; i++)
		if (lnwait(r[i]) != 0 ||
		    r[i]->ios2_DataLength != PAYLOAD - 1 + HDRLEN ||
		    !lnsame(rxd[i] + HDRLEN, txd, PAYLOAD - 1)) {
			xKPrintf("lanloop: burst frame %d\n", i);
			return ("burst read");
		}
	for (i = 0; i < BURST; i++) {
		DeleteIORequest(r[i]);
		DeleteIORequest(w[i]);
	}
	i = intstack(0);
	xKPrintf("lanloop: interrupt stack %d bytes\n", i);
	return (i > INTSTKMAX ? "interrupt stack" : NULL);
}

static char *
loop(unit)
int unit;
{
	struct IOSana2Req *a;
	char *err;

	if ((a = lnreq(0)) == NULL)
		return ("CreateIORequest");
	if (OpenDevice(LANCENAME, (ULONG)unit, a, 0L) != 0)
		return ("OpenDevice");
	if ((err = setup(a, unit)) == NULL && (err = frames(a, unit)) == NULL &&
	    (err = stats(a)) == NULL && (err = service(a)) == NULL)
		err = burst(a);
	CloseDevice((struct IORequest *)a);
	DeleteIORequest(a);
	return (err);
}

int
main()
{
	struct IOSana2Req *a;
	char *err;
	int unit;

	if ((a = lnreq(0)) == NULL) {
		xKPrintf("FAIL lanloop: CreateIORequest\n");
		return (1);
	}
	if (OpenDevice(LANCENAME, 3L, a, 0L) == 0) {
		xKPrintf("FAIL lanloop: unit 3 opened\n");
		return (1);
	}
	DeleteIORequest(a);
	for (unit = 1; unit <= 2; unit++)
		if ((err = loop(unit)) != NULL) {
			xKPrintf("FAIL lanloop: unit %d: %s\n", unit, err);
			return (1);
		}
	xKPrintf("PASS lanloop\n");
	return (0);
}
