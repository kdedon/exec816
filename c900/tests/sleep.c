/* SPDX-License-Identifier: MIT */
/*
 * Sleep: the requested ticks elapse, Sleep(0) returns, and sleepers wake
 * in deadline order regardless of the order they went to sleep.
 */
#include "test.h"
#include <exec/exec.h>

#define STACKSIZE	1024
#define DONE_BIT	16
#define DONE_MASK	(1L << DONE_BIT)
#define NSLEEPERS	3

static struct Task *parent;
static volatile int nwoken;
static int order[NSLEEPERS];
static ULONG woke[NSLEEPERS];
static ULONG start;

static void
sleeper(i, ticks)
int i;
ULONG ticks;
{
	Sleep(ticks);
	Forbid();
	woke[i] = TickCount - start;
	order[nwoken++] = i;
	Signal(parent, DONE_MASK);
}

/* Started in this order; they must wake 1, 2, 0. */
static void
s0()
{
	sleeper(0, 30L);
}

static void
s1()
{
	sleeper(1, 10L);
}

static void
s2()
{
	sleeper(2, 20L);
}

static char *
run()
{
	ULONG t0, dt;
	int bit;

	parent = FindTask(NULL);
	SetTaskPri(parent, 0);

	Sleep(0);
	Sleep(1);
	t0 = TickCount;
	Sleep(10);
	dt = TickCount - t0;
	if (dt < 10 || dt > 12)
		return ("Sleep(10) duration");

	bit = AllocSignal(DONE_BIT);
	if (bit != DONE_BIT)
		return ("AllocSignal");
	Forbid();
	start = TickCount;
	if (CreateTask("s0", 1, s0, STACKSIZE) == NULL ||
	    CreateTask("s1", 1, s1, STACKSIZE) == NULL ||
	    CreateTask("s2", 1, s2, STACKSIZE) == NULL) {
		Permit();
		return ("CreateTask");
	}
	Permit();
	while (nwoken != NSLEEPERS)
		Wait(DONE_MASK);
	FreeSignal(bit);

	if (order[0] != 1 || order[1] != 2 || order[2] != 0)
		return ("wake order");
	if (woke[1] < 10 || woke[2] < 20 || woke[0] < 30 || woke[0] > 33)
		return ("wake times");
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL sleep: %s\n", why);
		return (1);
	}
	KPutStr("PASS sleep\n");
	return (0);
}
