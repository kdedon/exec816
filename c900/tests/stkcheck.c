/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Stack probe: AddTask writes a guard word at tc_SPLower; a switch with
 * the guard overwritten raises AN_StackProbe once and rewrites it; the
 * tick raises it while the interrupted SP is outside the Task's bounds.
 */
#include "test.h"

#define STACKSIZE	1024

static void
child()
{
}

static char *
run()
{
	register struct Task *me, *t;
	register UWORD *g;
	APTR up;
	UWORD save;
	ULONG t0;

	me = FindTask(NULL);
	g = (UWORD *)me->tc_SPLower;
	if (*g == 0)
		return ("no root guard");
	/* Lower priority: the child has not run when its guard is read. */
	t = CreateTask("probe", -1, child, STACKSIZE);
	if (t == NULL)
		return ("CreateTask");
	if (*(UWORD *)t->tc_SPLower != *g)
		return ("AddTask wrote no guard");
	Sleep(2);

	SysBase->LastAlert[0] = 0;
	save = *g;
	*g = 0;
	Yield();
	if (SysBase->LastAlert[0] != AN_StackProbe ||
	    SysBase->LastAlert[1] != (ULONG)me)
		return ("switch with a broken guard did not alert");
	if (*g != save)
		return ("guard not rewritten");
	SysBase->LastAlert[0] = 0;
	Yield();
	if (SysBase->LastAlert[0] != 0)
		return ("intact guard alerted");

	/* Under Forbid only the tick can see the SP outside its bounds. */
	Forbid();
	up = me->tc_SPUpper;
	me->tc_SPUpper = me->tc_SPLower;
	t0 = *(volatile ULONG *)&TickCount;
	while (*(volatile ULONG *)&TickCount == t0)
		;
	me->tc_SPUpper = up;
	Permit();
	if (SysBase->LastAlert[0] != AN_StackProbe)
		return ("tick with SP out of bounds did not alert");
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL stkcheck: %s\n", why);
		return (1);
	}
	KPutStr("PASS stkcheck\n");
	return (0);
}
