/* SPDX-License-Identifier: MIT */
/*
 * timer.device delays: ReadEClock is monotonic and runs at its rate,
 * TR_ADDREQUEST delays on UNIT_MICROHZ, UNIT_VBLANK and UNIT_ECLOCK are
 * never early and end within the tick after, requests from several Tasks
 * and several requests on one port complete in deadline order (FIFO
 * among equal deadlines), UNIT_WAITECLOCK waits for an absolute E-clock,
 * AbortIO ends a pending request, and a bad command is refused.
 */
#include "test.h"
#include <exec/exec.h>
#include <devices/timer.h>

#define STACKSIZE	1024
#define DONE_BIT	16
#define DONE_MASK	(1L << DONE_BIT)
#define NWAITERS	3

struct Device *TimerBase;

static struct Task *parent;
static volatile int nwoken;
static int order[NWAITERS];
static ULONG woke[NWAITERS];
static ULONG start;
static volatile int waiterr;

/* Open a timer request on a fresh port of the calling Task. */
static struct timerequest *
opentimer(unit)
ULONG unit;
{
	struct MsgPort *port;
	struct timerequest *tr;

	if ((port = CreateMsgPort()) == NULL)
		return (NULL);
	tr = (struct timerequest *)CreateIORequest(port,
	    sizeof(struct timerequest));
	if (tr == NULL) {
		DeleteMsgPort(port);
		return (NULL);
	}
	if (OpenDevice(TIMERNAME, unit, tr, 0) != 0) {
		DeleteIORequest(tr);
		DeleteMsgPort(port);
		return (NULL);
	}
	return (tr);
}

static void
closetimer(tr)
struct timerequest *tr;
{
	struct MsgPort *port;

	port = tr->tr_node.io_Message.mn_ReplyPort;
	CloseDevice(tr);
	DeleteIORequest(tr);
	DeleteMsgPort(port);
}

static void
waiter(i, us)
int i;
ULONG us;
{
	struct timerequest *tr;
	int err;

	if ((tr = opentimer((ULONG)UNIT_MICROHZ)) == NULL) {
		waiterr = 1;
		err = 1;
	} else {
		tr->tr_node.io_Command = TR_ADDREQUEST;
		tr->tr_time.tv_secs = 0;
		tr->tr_time.tv_micro = us;
		err = DoIO(tr);
		if (err != 0 || tr->tr_time.tv_micro != 0)
			waiterr = 1;
	}
	Forbid();
	woke[i] = TickCount - start;
	order[nwoken++] = i;
	if (tr != NULL)
		closetimer(tr);
	Signal(parent, DONE_MASK);
}

/* Started in this order; they must finish 1, 2, 0. */
static void
w0()
{
	waiter(0, 300000L);
}

static void
w1()
{
	waiter(1, 100000L);
}

static void
w2()
{
	waiter(2, 200000L);
}

static ULONG freq;

/* The low long of the E-clock; sets freq. */
static ULONG
eread()
{
	struct EClockVal ev;

	freq = ReadEClock(&ev);
	return (ev.ev_lo);
}

/* DoIO(tr), started just after a tick: never shorter than e E-clock
 * counts, and over within two ticks of that. */
static int
delay(tr, e)
struct timerequest *tr;
ULONG e;
{
	ULONG t0, e0, dt, de;

	tr->tr_node.io_Command = TR_ADDREQUEST;
	Sleep(1);
	t0 = TickCount;
	e0 = eread();
	if (DoIO(tr) != 0)
		return (0);
	de = eread() - e0;
	dt = TickCount - t0;
	return (de >= e && dt <= e / (freq / 100) + 2);
}

/* A delay of us microseconds on a MICROHZ or VBLANK request. */
static int
usdelay(tr, s, us)
struct timerequest *tr;
ULONG s, us;
{
	tr->tr_time.tv_secs = s;
	tr->tr_time.tv_micro = us;
	if (!delay(tr, s * freq + us * (freq / 1000000L)))
		return (0);
	return (tr->tr_time.tv_secs == 0 && tr->tr_time.tv_micro == 0);
}

/* A request on unit that would wait 5 s, aborted, is replied at once. */
static int
abortone(tr, unit)
struct timerequest *tr;
int unit;
{
	struct EClockVal *ev;
	ULONG t0;

	ev = (struct EClockVal *)&tr->tr_time;
	tr->tr_node.io_Command = TR_ADDREQUEST;
	tr->tr_time.tv_secs = 5;
	tr->tr_time.tv_micro = 0;
	if (unit == UNIT_ECLOCK || unit == UNIT_WAITECLOCK) {
		if (unit == UNIT_WAITECLOCK)
			ReadEClock(ev);
		else
			ev->ev_hi = ev->ev_lo = 0;
		ev->ev_lo += 5 * freq;
		if (ev->ev_lo < 5 * freq)
			ev->ev_hi++;
	}
	SendIO(tr);
	Sleep(2);
	if (CheckIO(tr) != NULL)
		return (0);
	t0 = TickCount;
	if (AbortIO(tr) != 0 || WaitIO(tr) != IOERR_ABORTED ||
	    TickCount - t0 > 1)
		return (0);
	/* A finished request is left alone. */
	return (AbortIO(tr) != 0 && tr->tr_node.io_Error == IOERR_ABORTED);
}

static char *
run()
{
	struct timerequest *tr, *ta, *tb, *tc, *u[UNIT_WAITECLOCK + 1];
	struct EClockVal a, b, *ev;
	struct MsgPort *port;
	struct Message *m;
	ULONG t0, dt, de;
	int i, bit;

	parent = FindTask(NULL);
	SetTaskPri(parent, 0);

	/* Admission: units 0-4. */
	if ((port = CreateMsgPort()) == NULL)
		return ("CreateMsgPort");
	for (i = 0; i <= UNIT_WAITECLOCK; i++) {
		u[i] = (struct timerequest *)CreateIORequest(port,
		    sizeof(struct timerequest));
		if (u[i] == NULL)
			return ("CreateIORequest");
		if (i == 0 && OpenDevice(TIMERNAME, 5L, u[i], 0) != IOERR_OPENFAIL)
			return ("OpenDevice unit 5");
		if (OpenDevice(TIMERNAME, (ULONG)i, u[i], 0) != 0)
			return ("OpenDevice");
	}
	tr = u[UNIT_MICROHZ];
	TimerBase = tr->tr_node.io_Device;

	/* The E-clock: C/T 3's input, monotonic, in step with the tick. */
	freq = ReadEClock(&a);
	if (freq != 2000000L && freq != 3000000L)
		return ("ReadEClock rate");
	for (i = 0; i < 200; i++) {
		if (i % 20 == 0)
			Sleep(1);
		ReadEClock(&b);
		if (b.ev_hi < a.ev_hi || (b.ev_hi == a.ev_hi && b.ev_lo < a.ev_lo))
			return ("E-clock went backwards");
		a = b;
	}
	Sleep(1);
	t0 = TickCount;
	de = eread();
	Sleep(10);
	de = eread() - de;
	dt = (TickCount - t0) * (freq / 100);
	if (de + freq / 100 < dt || de > dt + freq / 100)
		return ("E-clock against the tick");

	tr->tr_node.io_Command = CMD_READ;
	if (DoIO(tr) != IOERR_NOCMD)
		return ("bad command");

	/* Relative delays, never early. */
	if (!usdelay(tr, 0L, 1L))
		return ("MICROHZ 1 us");
	if (!usdelay(tr, 0L, 5000L))
		return ("MICROHZ 5 ms");
	if (!usdelay(tr, 0L, 50000L))
		return ("MICROHZ 50 ms");
	if (!usdelay(tr, 0L, 0L))
		return ("MICROHZ 0");
	tr->tr_time.tv_secs = 0;
	tr->tr_time.tv_micro = 1050000L;
	if (!delay(tr, freq + freq / 20))
		return ("MICROHZ over 1000000 us");
	if (!usdelay(u[UNIT_VBLANK], 0L, 30000L))
		return ("VBLANK 30 ms");
	if (!usdelay(u[UNIT_VBLANK], 1L, 0L))
		return ("VBLANK 1 s");
	ev = (struct EClockVal *)&u[UNIT_ECLOCK]->tr_time;
	ev->ev_hi = 0;
	ev->ev_lo = freq / 20;
	if (!delay(u[UNIT_ECLOCK], freq / 20) || ev->ev_lo != 0)
		return ("ECLOCK 50 ms");

	/* UNIT_WAITECLOCK: a past target is quick; a future one waits and
	 * is left in place. */
	ta = u[UNIT_WAITECLOCK];
	ev = (struct EClockVal *)&ta->tr_time;
	ta->tr_node.io_Command = TR_ADDREQUEST;
	ReadEClock(ev);
	if (DoIO(ta) != 0 || (ta->tr_node.io_Flags & IOF_QUICK) == 0)
		return ("WAITECLOCK past");
	ReadEClock(ev);
	if ((ev->ev_lo += freq / 10) < freq / 10)
		ev->ev_hi++;
	b = *ev;
	t0 = TickCount;
	if (DoIO(ta) != 0)
		return ("WAITECLOCK");
	ReadEClock(&a);
	if (a.ev_hi < b.ev_hi || (a.ev_hi == b.ev_hi && a.ev_lo < b.ev_lo) ||
	    TickCount - t0 > 12 || ev->ev_lo != b.ev_lo)
		return ("WAITECLOCK time");

	/* Three borrowed requests on one port: 40 ms, then two of 20 ms
	 * whose replies keep submission order. */
	ta = (struct timerequest *)CreateIORequest(port,
	    sizeof(struct timerequest));
	tb = (struct timerequest *)CreateIORequest(port,
	    sizeof(struct timerequest));
	tc = (struct timerequest *)CreateIORequest(port,
	    sizeof(struct timerequest));
	if (ta == NULL || tb == NULL || tc == NULL)
		return ("CreateIORequest");
	ta->tr_node = tr->tr_node;
	ta->tr_node.io_Command = TR_ADDREQUEST;
	tb->tr_node = ta->tr_node;
	tc->tr_node = ta->tr_node;
	ta->tr_time.tv_secs = tb->tr_time.tv_secs = tc->tr_time.tv_secs = 0;
	ta->tr_time.tv_micro = 40000L;
	tb->tr_time.tv_micro = 20000L;
	tc->tr_time.tv_micro = 20000L;
	Disable();
	SendIO(ta);
	SendIO(tb);
	SendIO(tc);
	Enable();
	WaitIO(ta);
	if ((m = GetMsg(port)) != (struct Message *)tb ||
	    GetMsg(port) != (struct Message *)tc || GetMsg(port) != NULL)
		return ("reply order on one port");
	DeleteIORequest(ta);
	DeleteIORequest(tb);
	DeleteIORequest(tc);

	/* AbortIO on each unit but UNIT_WAITUNTIL (tests/systime). */
	if (!abortone(u[UNIT_MICROHZ], UNIT_MICROHZ))
		return ("AbortIO MICROHZ");
	if (!abortone(u[UNIT_VBLANK], UNIT_VBLANK))
		return ("AbortIO VBLANK");
	if (!abortone(u[UNIT_ECLOCK], UNIT_ECLOCK))
		return ("AbortIO ECLOCK");
	if (!abortone(u[UNIT_WAITECLOCK], UNIT_WAITECLOCK))
		return ("AbortIO WAITECLOCK");

	for (i = 0; i <= UNIT_WAITECLOCK; i++) {
		CloseDevice(u[i]);
		DeleteIORequest(u[i]);
	}
	DeleteMsgPort(port);

	/* Several Tasks, each with its own binding. */
	bit = AllocSignal(DONE_BIT);
	if (bit != DONE_BIT)
		return ("AllocSignal");
	Forbid();
	start = TickCount;
	if (CreateTask("w0", 1, w0, STACKSIZE) == NULL ||
	    CreateTask("w1", 1, w1, STACKSIZE) == NULL ||
	    CreateTask("w2", 1, w2, STACKSIZE) == NULL) {
		Permit();
		return ("CreateTask");
	}
	Permit();
	while (nwoken != NWAITERS)
		Wait(DONE_MASK);
	FreeSignal(bit);
	if (waiterr)
		return ("waiter request failed");
	if (order[0] != 1 || order[1] != 2 || order[2] != 0)
		return ("wake order");
	if (woke[1] < 10 || woke[2] < 20 || woke[0] < 30 || woke[0] > 34)
		return ("wake times");

	/* Sleep still works beside the device. */
	t0 = TickCount;
	Sleep(10);
	dt = TickCount - t0;
	if (dt < 10 || dt > 12)
		return ("Sleep");
	return (NULL);
}

int
main()
{
	char *why;
	ULONG before;

	Sleep(1);
	before = AvailMem(MEMF_ANY);
	if ((why = run()) != NULL) {
		xKPrintf("FAIL timer: %s\n", why);
		return (1);
	}
	Sleep(2);
	if (AvailMem(MEMF_ANY) != before) {
		xKPrintf("FAIL timer: memory leaked\n");
		return (1);
	}
	KPutStr("PASS timer\n");
	return (0);
}
