/* SPDX-License-Identifier: MIT */
/*
 * Semaphores under preemption: equal-priority workers time-sliced by the
 * tick and a higher-priority Task woken every tick mix exclusive, nested,
 * shared and Attempt holds of one semaphore.  Each hold checks the
 * exclusion rules and the counts; at the end the semaphore must be free.
 * An Obtain under Disable must leave VI off.
 */
#include "test.h"
#include <exec/exec.h>

#define STACKSIZE	1024
#define NWORK		4
#define TICKS		200
#define DONE_BIT	16
#define DONE_MASK	(1L << DONE_BIT)

static struct Task *root;
static struct SignalSemaphore sem;
static volatile int stop;
static int ndone;
static int excl, shrd;
static ULONG holds, busy, queued;
static char *bad;

static void
fail(why)
char *why;
{
	if (bad == NULL)
		bad = why;
}

/* Count a hold in or out; Disable keeps the counters whole. */
static void
enter(sh)
int sh;
{
	Disable();
	if (excl != 0 || (!sh && shrd != 0))
		fail("exclusion broken");
	if (sh)
		shrd++;
	else
		excl++;
	holds++;
	if (sem.ss_QueueCount >= sem.ss_NestCount)
		queued++;
	Enable();
}

static void
leave(sh)
int sh;
{
	Disable();
	if (sh)
		shrd--;
	else
		excl--;
	Enable();
}

static void
refused()
{
	Disable();
	busy++;
	Enable();
}

static void
dwell(n)
register int n;
{
	while (n-- > 0)
		;
}

static void
exclusive(me, n)
struct Task *me;
int n;
{
	enter(0);
	if (sem.ss_Owner != me || sem.ss_NestCount != 1)
		fail("exclusive owner");
	dwell(n);
	ObtainSemaphore(&sem);
	ObtainSemaphoreShared(&sem);
	if (!AttemptSemaphore(&sem) || !AttemptSemaphoreShared(&sem))
		fail("owner Attempt");
	if (sem.ss_Owner != me || sem.ss_NestCount != 5)
		fail("nested count");
	dwell(n);
	ReleaseSemaphore(&sem);
	ReleaseSemaphore(&sem);
	ReleaseSemaphore(&sem);
	ReleaseSemaphore(&sem);
	if (sem.ss_Owner != me || sem.ss_NestCount != 1)
		fail("nested release");
	leave(0);
	ReleaseSemaphore(&sem);
}

static void
shared(n)
int n;
{
	enter(1);
	if (sem.ss_Owner != NULL || sem.ss_NestCount < 1)
		fail("shared owner");
	dwell(n);
	leave(1);
	ReleaseSemaphore(&sem);
}

static void
step(me, k, n)
struct Task *me;
int k, n;
{
	switch (k % 6) {
	case 0:
		ObtainSemaphore(&sem);
		exclusive(me, n);
		break;
	case 1:
	case 4:
		ObtainSemaphoreShared(&sem);
		shared(n);
		break;
	case 2:
		if (AttemptSemaphore(&sem))
			exclusive(me, n);
		else
			refused();
		break;
	case 3:
		if (AttemptSemaphoreShared(&sem))
			shared(n);
		else
			refused();
		break;
	case 5:
		ObtainSemaphoreShared(&sem);
		ObtainSemaphoreShared(&sem);
		shared(n);
		ReleaseSemaphore(&sem);
		break;
	}
}

static void
finish()
{
	Forbid();
	ndone++;
	Signal(root, DONE_MASK);
	RemTask(NULL);
}

static void
worker()
{
	register struct Task *me;
	register int k;

	me = FindTask(NULL);
	for (k = (int)me->tc_UserData; !stop; k++)
		step(me, k, 20 + k % 7 * 15);
	finish();
}

static void
ticker()
{
	register struct Task *me;
	register int k;

	me = FindTask(NULL);
	for (k = 0; !stop; k++) {
		Sleep(1);
		step(me, k, 5);
	}
	finish();
}

static char *
run()
{
	struct Task *t;
	ULONG fcw;
	register int i;

	root = FindTask(NULL);
	if (AllocSignal(DONE_BIT) != DONE_BIT)
		return ("AllocSignal");
	InitSemaphore(&sem);
	SetTaskPri(root, 2);
	for (i = 0; i < NWORK; i++) {
		if ((t = CreateTask("semwork", 0, worker, STACKSIZE)) == NULL)
			return ("CreateTask");
		t->tc_UserData = (APTR)(ULONG)i;
	}
	if (CreateTask("semtick", 1, ticker, STACKSIZE) == NULL)
		return ("CreateTask");
	Sleep(TICKS);
	stop = 1;
	SetTaskPri(root, -1);
	while (ndone < NWORK + 1)
		Wait(DONE_MASK);
	if (bad != NULL)
		return (bad);
	if (excl != 0 || shrd != 0)
		return ("holders left");
	if (sem.ss_QueueCount != -1 || sem.ss_NestCount != 0 ||
	    sem.ss_Owner != NULL || !IsMinListEmpty(&sem.ss_WaitQueue))
		return ("not free at the end");
	xKPrintf("semstress: %lu holds, %lu busy Attempts, %lu with waiters\n",
	    holds, busy, queued);
	if (busy == 0 || queued == 0)
		return ("no contention");

	Disable();
	ObtainSemaphore(&sem);
	AttemptSemaphoreShared(&sem);
	ReleaseSemaphore(&sem);
	ReleaseSemaphore(&sem);
	fcw = intoff();
	intrest(fcw);
	Enable();
	if (fcw & 0x1800)
		return ("a semaphore call under Disable enabled interrupts");
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL semstress: %s\n", why);
		return (1);
	}
	KPutStr("PASS semstress\n");
	return (0);
}
