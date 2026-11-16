/* SPDX-License-Identifier: MIT */
/*
 * Tick preemption: two CPU-bound Tasks of equal priority both make
 * progress without yielding, and Forbid holds them off.
 */
#include "test.h"
#include <exec/exec.h>

#define STACKSIZE	1024
#define DONE_BIT	16
#define DONE_MASK	(1L << DONE_BIT)

static struct Task *parent;
static volatile ULONG count[2];
static volatile int stop, done;

static void
spin(i)
int i;
{
	while (!stop)
		count[i]++;
	Forbid();
	done++;
	Signal(parent, DONE_MASK);
}

static void
spin0()
{
	spin(0);
}

static void
spin1()
{
	spin(1);
}

static char *
run()
{
	ULONG c0, c1, t0;
	int bit;

	parent = FindTask(NULL);
	SetTaskPri(parent, 0);
	bit = AllocSignal(DONE_BIT);
	if (bit != DONE_BIT)
		return ("AllocSignal");
	if (CreateTask("spin0", 0, spin0, STACKSIZE) == NULL ||
	    CreateTask("spin1", 0, spin1, STACKSIZE) == NULL)
		return ("CreateTask");

	/* Neither worker yields; only the tick can share the CPU. */
	Sleep(40);
	Forbid();
	c0 = count[0];
	c1 = count[1];
	Permit();
	if (c0 == 0 || c1 == 0)
		return ("a busy Task never ran");

	/* Under Forbid the tick must not switch away from this Task. */
	Forbid();
	c0 = count[0];
	c1 = count[1];
	t0 = TickCount;
	while (TickCount - t0 < 3 * 4)
		;
	if (count[0] != c0 || count[1] != c1) {
		Permit();
		return ("switched under Forbid");
	}
	Permit();

	stop = 1;
	while (done != 2)
		Wait(DONE_MASK);
	FreeSignal(bit);
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL preempt: %s\n", why);
		return (1);
	}
	KPutStr("PASS preempt\n");
	return (0);
}
