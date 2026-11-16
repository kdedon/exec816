/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * timer.device, with the AmigaOS 3.x interface (docs/internals.md,
 * "timer.device").  Two clocks advance on the 100 Hz tick, in a server
 * on INTV_TICK after the scheduler's: the E-clock, counting CIO U70
 * C/T 3's input (time constant TC per tick), and the system time.
 * Between ticks both are refined by C/T 3's current count when it reads
 * back.
 *
 * Requests with an E-clock deadline (UNIT_MICROHZ, UNIT_VBLANK,
 * UNIT_ECLOCK, UNIT_WAITECLOCK) form one list and UNIT_WAITUNTIL
 * requests another, each ordered by deadline, FIFO among equal ones.
 * The server replies to those due at each tick.
 *
 * Open and Close run under OpenDevice's Forbid.  BeginIO, AbortIO and
 * the base functions run unlocked in the caller; the clocks and lists
 * are shared with the server, so every access runs with VI off.
 */
#include "kernel.h"
#include <devices/timer.h>
#include <resources/battclock.h>

#define NUNITS		5
#define TICKUS		10000L		/* microseconds per tick */
#define MILLION		1000000L

/* CIO U70 C/T 3: command/status and current count. */
#define P_CT3CS		0x0019
#define P_CT3CCH	0x0029
#define P_CT3CCL	0x002b
#define CS_IP		0x20
#define CS_RCC		0x08
#define CS_GCB		0x04

struct TimerBase {
	struct Device tb_Device;
	struct Unit tb_Unit[NUNITS];
	struct List tb_Pending;		/* E-clock deadlines */
	struct List tb_Until;		/* system-time deadlines */
	struct Interrupt tb_Int;
	struct EClockVal tb_EClock;	/* at the last tick served */
	struct timeval tb_Time;		/* system time then */
	ULONG tb_Ticks;			/* TickCount then */
	ULONG tb_Freq;			/* E-clock counts per second */
	UWORD tb_TC;			/* E-clock counts per tick */
	UWORD tb_Div;			/* E-clock counts per microsecond */
	UWORD tb_Live;			/* the current count reads back */
	UWORD tb_Probe;			/* find tb_Live at the next tick */
};

static struct {
	struct JumpVec lv_Vec[11];
	struct TimerBase lv_Base;
} timerbaselib;
#define timerbase timerbaselib.lv_Base
static char timername[] = TIMERNAME;

/* The tick server runs first at priority 127. */
#define TIMERPRI	100

#define TR(r)		((struct timerequest *)(r))
#define EV(r)		((struct EClockVal *)&TR(r)->tr_time)
#define UNITNO(r)	((struct Unit *)(r)->io_Unit - timerbase.tb_Unit)

/* C/T 3's count, frozen for the read; VI off. */
static UWORD
ct3count()
{
	register UWORD c;

	outb(P_CT3CS, CS_RCC | CS_GCB);
	c = (inb(P_CT3CCH) & 0xff) << 8;
	return (c | (inb(P_CT3CCL) & 0xff));
}

/* E-clock counts since the last tick served; VI off.  A tick the
 * scheduler took but this server has not yet, or one still pending
 * (IP set with the count reloaded), adds TC. */
static ULONG
counts(tb)
register struct TimerBase *tb;
{
	register ULONG e;
	register UWORD c;

	e = TickCount != tb->tb_Ticks ? (ULONG)tb->tb_TC : 0L;
	if (tb->tb_Live) {
		c = tb->tb_TC - ct3count();
		if ((inb(P_CT3CS) & CS_IP) && c < tb->tb_TC / 2)
			e += tb->tb_TC;
		e += c;
	}
	return (e);
}

static void
evadd(ev, hi, lo)
register struct EClockVal *ev;
ULONG hi, lo;
{
	ev->ev_lo += lo;
	ev->ev_hi += hi + (ev->ev_lo < lo);
}

/* a <= b */
static int
evle(a, b)
register struct EClockVal *a, *b;
{
	return (a->ev_hi < b->ev_hi ||
	    (a->ev_hi == b->ev_hi && a->ev_lo <= b->ev_lo));
}

/* The E-clock now, VI off. */
static void
enow(tb, ev)
register struct TimerBase *tb;
struct EClockVal *ev;
{
	*ev = tb->tb_EClock;
	evadd(ev, 0L, counts(tb));
}

/* hi:lo = a * b */
static void
mulu(a, b, hi, lo)
ULONG a, b, *hi, *lo;
{
	ULONG ll, lh, hl, mid;

	ll = (a & 0xFFFFL) * (b & 0xFFFFL);
	lh = (a & 0xFFFFL) * (b >> 16);
	hl = (a >> 16) * (b & 0xFFFFL);
	mid = (ll >> 16) + (lh & 0xFFFFL) + (hl & 0xFFFFL);
	*lo = (mid << 16) | (ll & 0xFFFFL);
	*hi = (a >> 16) * (b >> 16) + (lh >> 16) + (hl >> 16) + (mid >> 16);
}

static void
tAddTime(tb, d, s)
struct TimerBase *tb;
register struct timeval *d, *s;
{
	d->tv_secs += s->tv_secs;
	if ((d->tv_micro += s->tv_micro) >= MILLION) {
		d->tv_micro -= MILLION;
		d->tv_secs++;
	}
}

static void
tSubTime(tb, d, s)
struct TimerBase *tb;
register struct timeval *d, *s;
{
	d->tv_secs -= s->tv_secs;
	if (d->tv_micro < s->tv_micro) {
		d->tv_micro += MILLION - s->tv_micro;
		d->tv_secs--;
	} else
		d->tv_micro -= s->tv_micro;
}

/* 0 if equal, -1 if d is later, 1 if d is earlier. */
static LONG
tCmpTime(tb, d, s)
struct TimerBase *tb;
register struct timeval *d, *s;
{
	if (d->tv_secs != s->tv_secs)
		return (d->tv_secs > s->tv_secs ? -1L : 1L);
	if (d->tv_micro != s->tv_micro)
		return (d->tv_micro > s->tv_micro ? -1L : 1L);
	return (0L);
}

static ULONG
tReadEClock(tb, ev)
register struct TimerBase *tb;
struct EClockVal *ev;
{
	int s;

	s = intoff();
	enow(tb, ev);
	intrest(s);
	return (tb->tb_Freq);
}

static void
tGetSysTime(tb, tv)
register struct TimerBase *tb;
register struct timeval *tv;
{
	ULONG e;
	int s;

	s = intoff();
	e = counts(tb);
	*tv = tb->tb_Time;
	intrest(s);
	if (e != 0) {
		tv->tv_micro += e / tb->tb_Div;
		while (tv->tv_micro >= MILLION) {
			tv->tv_micro -= MILLION;
			tv->tv_secs++;
		}
	}
}

/* The system time at the last tick becomes *tv less the time since. */
static void
setsys(tb, tv)
register struct TimerBase *tb;
struct timeval *tv;
{
	struct timeval since;
	int s;

	since.tv_secs = 0;
	s = intoff();
	since.tv_micro = counts(tb) / tb->tb_Div;
	tb->tb_Time = *tv;
	tSubTime(tb, &tb->tb_Time, &since);
	intrest(s);
}

static int
tserv(tb, n)
register struct TimerBase *tb;
UWORD n;
{
	register struct IORequest *ior;
	UWORD a;

	tb->tb_Ticks = TickCount;
	if (tb->tb_Probe) {
		tb->tb_Probe = 0;
		a = ct3count();
		tb->tb_Live = a <= tb->tb_TC && ct3count() != a;
	}
	evadd(&tb->tb_EClock, 0L, (ULONG)tb->tb_TC);
	if ((tb->tb_Time.tv_micro += TICKUS) >= MILLION) {
		tb->tb_Time.tv_micro -= MILLION;
		tb->tb_Time.tv_secs++;
	}
	while ((ior = (struct IORequest *)tb->tb_Pending.lh_Head)
	    ->io_Message.mn_Node.ln_Succ != NULL &&
	    evle(EV(ior), &tb->tb_EClock)) {
		xRemove(&ior->io_Message.mn_Node);
		if (UNITNO(ior) != UNIT_WAITECLOCK)
			TR(ior)->tr_time.tv_secs = TR(ior)->tr_time.tv_micro = 0;
		xReplyMsg(&ior->io_Message);
	}
	while ((ior = (struct IORequest *)tb->tb_Until.lh_Head)
	    ->io_Message.mn_Node.ln_Succ != NULL &&
	    tCmpTime(tb, &TR(ior)->tr_time, &tb->tb_Time) >= 0) {
		xRemove(&ior->io_Message.mn_Node);
		xReplyMsg(&ior->io_Message);
	}
	return (0);
}

static void
topen(dev, ior, unit, flags)
struct Device *dev;
register struct IORequest *ior;
ULONG unit, flags;
{
	if (unit >= NUNITS) {
		ior->io_Error = IOERR_OPENFAIL;
		return;
	}
	ior->io_Unit = &timerbase.tb_Unit[(int)unit];
	ior->io_Unit->unit_OpenCnt++;
	dev->dd_Library.lib_OpenCnt++;
}

static int
haveport(l, port)
struct List *l;
struct MsgPort *port;
{
	register struct Node *n;

	for (n = l->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (((struct Message *)n)->mn_ReplyPort == port)
			return (1);
	return (0);
}

/* Closing with a request of this binding still pending is misuse: the
 * borrowed requests share the opener's reply port. */
static APTR
tclose(dev, ior)
struct Device *dev;
struct IORequest *ior;
{
	struct MsgPort *port;
	int s;

	if ((port = ior->io_Message.mn_ReplyPort) != NULL) {
		s = intoff();
		if (haveport(&timerbase.tb_Pending, port) ||
		    haveport(&timerbase.tb_Until, port))
			kpanic("timer.device: CloseDevice with requests pending");
		intrest(s);
	}
	ior->io_Unit->unit_OpenCnt--;
	dev->dd_Library.lib_OpenCnt--;
	return (NULL);
}

/* Queue ior on l after every request due no later. */
static void
enqueue(l, ior)
struct List *l;
register struct IORequest *ior;
{
	register struct Node *n;
	int s;

	ior->io_Flags &= ~IOF_QUICK;
	s = intoff();
	for (n = l->lh_TailPred; n->ln_Pred != NULL; n = n->ln_Pred)
		if (l == &timerbase.tb_Until ?
		    tCmpTime(&timerbase, &TR(n)->tr_time, &TR(ior)->tr_time) <= 0 :
		    evle(EV(n), EV(ior)))
			break;
	xInsert(l, &ior->io_Message.mn_Node, n);
	intrest(s);
}

/* TR_ADDREQUEST: queue ior, or 1 if its time has come. */
static int
addreq(tb, ior)
register struct TimerBase *tb;
register struct IORequest *ior;
{
	register struct timerequest *tr;
	struct EClockVal now, d;
	struct timeval tv;
	int s, unit;

	tr = TR(ior);
	unit = UNITNO(ior);
	if (unit == UNIT_WAITUNTIL) {
		tGetSysTime(tb, &tv);
		if (tCmpTime(tb, &tr->tr_time, &tv) >= 0)
			return (1);
		enqueue(&tb->tb_Until, ior);
		return (0);
	}
	s = intoff();
	enow(tb, &now);
	intrest(s);
	if (unit == UNIT_WAITECLOCK) {
		if (evle(EV(ior), &now))
			return (1);
	} else {
		if (unit == UNIT_ECLOCK)
			d = *EV(ior);
		else {
			mulu(tr->tr_time.tv_secs + tr->tr_time.tv_micro / MILLION,
			    tb->tb_Freq, &d.ev_hi, &d.ev_lo);
			evadd(&d, 0L, tr->tr_time.tv_micro % MILLION * tb->tb_Div);
		}
		/* Without the current count, now may be up to a tick late. */
		if (!tb->tb_Live)
			evadd(&now, 0L, (ULONG)tb->tb_TC);
		evadd(&now, d.ev_hi, d.ev_lo);
		*EV(ior) = now;
	}
	enqueue(&tb->tb_Pending, ior);
	return (0);
}

static void
tbegin(tb, ior)
register struct TimerBase *tb;
register struct IORequest *ior;
{
	IOBEGIN(ior);
	switch (ior->io_Command) {
	case TR_ADDREQUEST:
		if (!addreq(tb, ior))
			return;
		break;
	case TR_GETSYSTIME:
		tGetSysTime(tb, &TR(ior)->tr_time);
		break;
	case TR_SETSYSTIME:
		setsys(tb, &TR(ior)->tr_time);
		break;
	default:
		ior->io_Error = IOERR_NOCMD;
	}
	xTermIO(ior);
}

/* The first terminal commit wins: a request no longer pending is left
 * alone. */
static LONG
tabort(dev, ior)
struct Device *dev;
register struct IORequest *ior;
{
	register struct Node *n;
	struct List *l;
	LONG r;
	int s;

	r = -1;
	l = UNITNO(ior) == UNIT_WAITUNTIL ? &timerbase.tb_Until :
	    &timerbase.tb_Pending;
	s = intoff();
	for (n = l->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (n == &ior->io_Message.mn_Node) {
			xRemove(n);
			ior->io_Error = IOERR_ABORTED;
			xReplyMsg(&ior->io_Message);
			r = 0;
			break;
		}
	intrest(s);
	return (r);
}

static APTR timerbasefuncs[] = {
	(APTR)topen, (APTR)tclose, (APTR)knull, (APTR)knull,
	(APTR)tbegin, (APTR)tabort,
	(APTR)tAddTime, (APTR)tSubTime, (APTR)tCmpTime, (APTR)tReadEClock,
	(APTR)tGetSysTime, (APTR)-1L
};

void ktimerinit();

struct Resident timertag = {
	RTC_MATCHWORD, &timertag, (APTR)(&timertag + 1), RTF_COLDSTART, 1,
	NT_DEVICE, 0, timername, NULL, (APTR)ktimerinit
};

/* Boot, after battclock.resource: the clocks run from here on, open or
 * not; the system time starts at the battery clock's. */
void
ktimerinit()
{
	register struct TimerBase *tb;
	APTR bc;

	tb = &timerbase;
	kinitlib(&tb->tb_Device.dd_Library, timername, timerbasefuncs);
	xNewList(&tb->tb_Pending);
	xNewList(&tb->tb_Until);
	tb->tb_TC = mach_tickconst();
	tb->tb_Div = tb->tb_TC / (UWORD)TICKUS;
	tb->tb_Freq = (ULONG)tb->tb_TC * (MILLION / TICKUS);
	tb->tb_Ticks = TickCount;
	tb->tb_Probe = 1;
	if ((bc = xOpenResource(BATTCLOCKNAME)) != NULL)
		tb->tb_Time.tv_secs =
		    (*(ULONG (*)())((char *)bc + LVOReadBattClock))(bc);
	tb->tb_Int.is_Node.ln_Pri = TIMERPRI;
	tb->tb_Int.is_Node.ln_Name = timername;
	tb->tb_Int.is_Data = (APTR)tb;
	tb->tb_Int.is_Code = tserv;
	xAddIntServer((UWORD)INTV_TICK, &tb->tb_Int);
	xAddDevice(&tb->tb_Device);
}
