/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The Task crash requester without intuition.library: dos.library's
 * default tc_TrapCode holds a Task that takes a segment trap, an
 * extended instruction with no EPU or sc 1, including one with little
 * stack left below the trap.  The Task never runs again and keeps its semaphore;
 * no alert is raised, no crash process starts and another Task keeps
 * running.  The root Task predates dos.library and keeps the kernel's
 * default.
 */
#include "test.h"
#include <dos/dos.h>
#include "crashtest.h"

static struct SignalSemaphore sem;
static volatile ULONG ticks;

static void
ticker()
{
	for (;;) {
		ticks++;
		Sleep(1L);
	}
}

static void
crashsem()
{
	ObtainSemaphore(&sem);
	readnull();
	ran++;
}

/* Leave less than the hold's stack below the frame, then trap. */
static void
deep()
{
	char buf[CSTACK - 300];

	buf[0] = 1;
	buf[sizeof(buf) - 1] = (char)readnull();
	ran += buf[0];
}

static char *
crash(name, fn)
char *name;
void (*fn)();
{
	register struct Task *t;
	ULONG t0;

	if ((t = CreateTask(name, 0L, fn, CSTACK)) == NULL)
		return ("CreateTask");
	if (!until(held, (APTR)t, 200L))
		return ("crashed Task not held");
	t0 = ticks;
	Sleep(20L);
	if (ran != 0)
		return ("crashed Task ran past its trap");
	if (ticks == t0)
		return ("the system stopped after a crash");
	if (!held(t))
		return ("held Task woke");
	return (NULL);
}

static char *
run()
{
	register struct Task *me;
	register char *why;
	ULONG a0;

	me = FindTask(NULL);
	a0 = SysBase->LastAlert[0];
	if (SysBase->TaskTrapCode == NULL ||
	    me->tc_TrapCode == SysBase->TaskTrapCode)
		return ("dos.library did not replace TaskTrapCode");
	InitSemaphore(&sem);
	if (CreateTask("ticker", 1L, ticker, CSTACK) == NULL)
		return ("CreateTask ticker");
	if ((why = crash("crashsem", crashsem)) != NULL)
		return (why);
	if (sem.ss_Owner != FindTask("crashsem"))
		return ("held Task lost its semaphore");
	if (!(SysBase->AttnFlags & AFF_68881) &&
	    (why = crash("crashepa", crashepa)) != NULL)
		return (why);
	if ((why = crash("crashsc", crashsc)) != NULL ||
	    (why = crash("crashdeep", deep)) != NULL)
		return (why);
	if (SysBase->LastAlert[0] != a0)
		return ("a crash raised an alert");
	if (!gone("crash"))
		return ("a crash process started without intuition");
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL crashnoint: %s\n", why);
		return (1);
	}
	KPutStr("PASS crashnoint\n");
	return (0);
}
