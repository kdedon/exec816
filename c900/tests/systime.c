/* SPDX-License-Identifier: MIT */
/*
 * timer.device system time: seeded from the battery clock at boot,
 * GetSysTime and TR_GETSYSTIME monotonic and at the tick's rate,
 * AddTime, SubTime and CmpTime, TR_SETSYSTIME, and UNIT_WAITUNTIL
 * (past, future, moved by TR_SETSYSTIME, AbortIO).  Opening with no
 * reply port gives TimerBase, as Amiga programs do.
 */
/* EMUFLAGS: --rtc=2026-11-12T10:20:30 */
#include "test.h"
#include <exec/exec.h>
#include <devices/timer.h>
#include <resources/battclock.h>

APTR BattClockBase;
struct Device *TimerBase;

static int
tveq(tv, s, us)
struct timeval *tv;
ULONG s, us;
{
	return (tv->tv_secs == s && tv->tv_micro == us);
}

static void
tvset(tv, s, us)
struct timeval *tv;
ULONG s, us;
{
	tv->tv_secs = s;
	tv->tv_micro = us;
}

static char *
arith()
{
	struct timeval a, b;

	tvset(&a, 1L, 999999L);
	tvset(&b, 2L, 1L);
	AddTime(&a, &b);
	if (!tveq(&a, 4L, 0L))
		return ("AddTime carry");
	tvset(&a, 0L, 500000L);
	tvset(&b, 0L, 499999L);
	AddTime(&a, &b);
	if (!tveq(&a, 0L, 999999L))
		return ("AddTime");
	tvset(&a, 4L, 0L);
	tvset(&b, 2L, 1L);
	SubTime(&a, &b);
	if (!tveq(&a, 1L, 999999L))
		return ("SubTime borrow");
	tvset(&a, 1L, 0L);
	tvset(&b, 2L, 500000L);
	SubTime(&a, &b);
	if (!tveq(&a, 0xFFFFFFFEL, 500000L))
		return ("SubTime negative");
	tvset(&b, 0xFFFFFFFEL, 500000L);
	tvset(&a, 5L, 5L);
	AddTime(&a, &b);
	if (!tveq(&a, 3L, 500005L))
		return ("AddTime negative");
	tvset(&b, 3L, 500005L);
	SubTime(&a, &b);
	if (!tveq(&a, 0L, 0L))
		return ("SubTime equal");
	tvset(&a, 7L, 10L);
	tvset(&b, 7L, 10L);
	if (CmpTime(&a, &b) != 0)
		return ("CmpTime equal");
	b.tv_micro = 11;
	if (CmpTime(&a, &b) != 1 || CmpTime(&b, &a) != -1)
		return ("CmpTime micro");
	tvset(&b, 6L, 999999L);
	if (CmpTime(&a, &b) != -1 || CmpTime(&b, &a) != 1)
		return ("CmpTime secs");
	return (NULL);
}

static char *
run()
{
	struct timerequest st, *tr;
	struct timeval a, b, c, set, saved;
	struct MsgPort *port;
	ULONG t0, dt, bc, us;
	char *why;
	int i, sub;

	SetTaskPri(FindTask(NULL), 0);
	if (INTB_VERTB != INTV_TICK)
		return ("INTB_VERTB");

	/* A binding with no reply port, for the base functions. */
	st.tr_node.io_Message.mn_ReplyPort = NULL;
	if (OpenDevice(TIMERNAME, (ULONG)UNIT_MICROHZ, &st, 0) != 0)
		return ("OpenDevice without a port");
	TimerBase = st.tr_node.io_Device;
	if (TimerBase == NULL)
		return ("io_Device");
	if ((why = arith()) != NULL)
		return (why);

	/* Seeded from the battery clock at boot. */
	BattClockBase = OpenResource(BATTCLOCKNAME);
	GetSysTime(&a);
	if (BattClockBase != NULL && (bc = ReadBattClock()) != 0 &&
	    (a.tv_secs > bc + 1 || a.tv_secs + 3 < bc))
		return ("seed from battclock");
	if (a.tv_micro >= 1000000L)
		return ("tv_micro range");

	/* Monotonic; TR_GETSYSTIME agrees.  sub: two reads within one tick
	 * differed. */
	sub = 0;
	for (i = 0; i < 300; i++) {
		if (i % 30 == 0)
			Sleep(1);
		t0 = TickCount;
		GetSysTime(&b);
		if (CmpTime(&b, &a) > 0 || b.tv_micro >= 1000000L)
			return ("system time went backwards");
		if (TickCount == t0 && CmpTime(&b, &a) != 0 && i % 30 != 0)
			sub = 1;
		a = b;
	}
	KPutStr(sub ? "systime: sub-tick resolution\n" :
	    "systime: tick resolution\n");
	if ((port = CreateMsgPort()) == NULL)
		return ("CreateMsgPort");
	tr = (struct timerequest *)CreateIORequest(port,
	    sizeof(struct timerequest));
	if (tr == NULL || OpenDevice(TIMERNAME, (ULONG)UNIT_WAITUNTIL, tr, 0))
		return ("OpenDevice WAITUNTIL");
	GetSysTime(&a);
	tr->tr_node.io_Command = TR_GETSYSTIME;
	if (DoIO(tr) != 0 || (tr->tr_node.io_Flags & IOF_QUICK) == 0)
		return ("TR_GETSYSTIME");
	GetSysTime(&c);
	if (CmpTime(&tr->tr_time, &a) > 0 || CmpTime(&c, &tr->tr_time) > 0)
		return ("TR_GETSYSTIME order");

	/* The rate: 50 ticks are 500 ms, to within a tick. */
	Sleep(1);
	GetSysTime(&a);
	t0 = TickCount;
	Sleep(50);
	GetSysTime(&b);
	dt = (TickCount - t0) * 10000L;
	SubTime(&b, &a);
	if (b.tv_secs != 0 || b.tv_micro + 10000L < dt ||
	    b.tv_micro > dt + 10000L)
		return ("system time rate");

	/* TR_SETSYSTIME. */
	GetSysTime(&saved);
	t0 = TickCount;
	tr->tr_node.io_Command = TR_SETSYSTIME;
	tvset(&tr->tr_time, 1000000L, 250000L);
	set = tr->tr_time;
	if (DoIO(tr) != 0)
		return ("TR_SETSYSTIME");
	GetSysTime(&a);
	SubTime(&a, &set);
	if (a.tv_secs != 0 || a.tv_micro > 20000L)
		return ("time after TR_SETSYSTIME");

	/* UNIT_WAITUNTIL: past is quick, future waits until its time. */
	tr->tr_node.io_Command = TR_ADDREQUEST;
	GetSysTime(&tr->tr_time);
	tr->tr_time.tv_secs--;
	if (DoIO(tr) != 0 || (tr->tr_node.io_Flags & IOF_QUICK) == 0)
		return ("WAITUNTIL past");
	GetSysTime(&tr->tr_time);
	tvset(&b, 0L, 100000L);
	AddTime(&tr->tr_time, &b);
	b = tr->tr_time;
	dt = TickCount;
	if (DoIO(tr) != 0)
		return ("WAITUNTIL");
	GetSysTime(&a);
	if (CmpTime(&a, &b) > 0 || TickCount - dt > 12 ||
	    CmpTime(&tr->tr_time, &b) != 0)
		return ("WAITUNTIL time");

	/* Setting the time forward past a target ends its wait. */
	GetSysTime(&tr->tr_time);
	tr->tr_time.tv_secs += 10;
	SendIO(tr);
	Sleep(2);
	if (CheckIO(tr) != NULL)
		return ("WAITUNTIL early");
	GetSysTime(&st.tr_time);
	st.tr_time.tv_secs += 20;
	st.tr_node.io_Command = TR_SETSYSTIME;
	dt = TickCount;
	if (DoIO(&st) != 0)
		return ("TR_SETSYSTIME forward");
	if (WaitIO(tr) != 0 || TickCount - dt > 2)
		return ("WAITUNTIL after TR_SETSYSTIME");

	/* Setting it back delays a target; AbortIO ends the wait. */
	tr->tr_node.io_Command = TR_ADDREQUEST;
	GetSysTime(&tr->tr_time);
	tvset(&b, 0L, 200000L);
	AddTime(&tr->tr_time, &b);
	b = tr->tr_time;
	SendIO(tr);
	GetSysTime(&st.tr_time);
	st.tr_time.tv_secs -= 100;
	if (DoIO(&st) != 0)
		return ("TR_SETSYSTIME back");
	Sleep(30);
	if (CheckIO(tr) != NULL)
		return ("WAITUNTIL after setting back");
	dt = TickCount;
	if (AbortIO(tr) != 0 || WaitIO(tr) != IOERR_ABORTED ||
	    TickCount - dt > 1 || CmpTime(&tr->tr_time, &b) != 0)
		return ("AbortIO WAITUNTIL");
	if (AbortIO(tr) == 0)
		return ("AbortIO of a finished request");

	/* Put the time back: saved plus the ticks since. */
	us = (TickCount - t0) * 10000L;
	tvset(&b, us / 1000000L, us % 1000000L);
	AddTime(&saved, &b);
	st.tr_time = saved;
	DoIO(&st);
	CloseDevice(tr);
	DeleteIORequest(tr);
	DeleteMsgPort(port);
	CloseDevice(&st);
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
		xKPrintf("FAIL systime: %s\n", why);
		return (1);
	}
	Sleep(2);
	if (AvailMem(MEMF_ANY) != before) {
		xKPrintf("FAIL systime: memory leaked\n");
		return (1);
	}
	KPutStr("PASS systime\n");
	return (0);
}
