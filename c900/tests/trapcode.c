/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * tc_TrapCode: AddTask installs the default; a Task's own handler gets the
 * trap number and frame of a NULL read and of sc 5, and returning resumes
 * the next instruction.  nullfault checks that the default halts.
 */
#include "test.h"

#define STACKSIZE	1024
#define DONE_BIT	16
#define DONE_MASK	(1L << DONE_BIT)

static struct Task *parent;
static volatile int ntrap;
static volatile ULONG lastnum;
static volatile UWORD lastseg, lastid;

/* The logical segment of a program address: its hunk's. */
#define SEGOF(p)	((UWORD)(PSEG(p) >> 24))
static volatile APTR lastdata;
static char *childwhy;

/* sc 5; ret, run from the data segment. */
static UWORD scfive[] = { 0x7F05, 0x9E08 };

static void
handler(num, fp)
ULONG num;
struct TrapFrame *fp;
{
	ntrap++;
	lastnum = num;
	lastseg = (fp->tf_PCSeg >> 8) & 0x7F;
	lastid = fp->tf_Id;
	lastdata = FindTask(NULL)->tc_TrapData;
}

static UWORD
readnull()
{
	return (*(volatile UWORD *)NULL);
}

static void
child()
{
	register struct Task *me;

	me = FindTask(NULL);
	if (me->tc_TrapCode != SysBase->TaskTrapCode)
		childwhy = "AddTask did not install the default tc_TrapCode";
	else {
		me->tc_TrapCode = (APTR)handler;
		me->tc_TrapData = (APTR)me;
		ntrap = 0;
		readnull();
		if (ntrap != 1 || lastnum != TRAP_SEGMENT ||
		    lastdata != (APTR)me)
			childwhy = "child trap";
	}
	Signal(parent, DONE_MASK);
}

static char *
run()
{
	register struct Task *me;

	me = FindTask(NULL);
	parent = me;
	if (me->tc_TrapCode != SysBase->TaskTrapCode ||
	    SysBase->TaskTrapCode == NULL)
		return ("root tc_TrapCode");
	me->tc_TrapCode = (APTR)handler;
	me->tc_TrapData = NULL;

	readnull();
	if (ntrap != 1 || lastnum != TRAP_SEGMENT || lastseg != SEGOF(readnull))
		return ("NULL read");
	readnull();
	if (ntrap != 2)
		return ("second NULL read");

	(*(void (*)())(APTR)scfive)();
	if (ntrap != 3 || lastnum != TRAP_SC + 5 || lastid != 0x7F05 ||
	    lastseg != SEGOF(scfive))
		return ("sc 5");

	if (AllocSignal(DONE_BIT) != DONE_BIT)
		return ("AllocSignal");
	if (CreateTask("trapper", 1, child, STACKSIZE) == NULL)
		return ("CreateTask");
	Wait(DONE_MASK);
	FreeSignal(DONE_BIT);
	if (childwhy != NULL)
		return (childwhy);
	me->tc_TrapCode = SysBase->TaskTrapCode;
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL trapcode: %s (%d traps, last %lu)\n", why,
		    ntrap, lastnum);
		return (1);
	}
	KPutStr("PASS trapcode\n");
	return (0);
}
