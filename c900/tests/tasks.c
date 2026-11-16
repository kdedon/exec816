/* SPDX-License-Identifier: MIT */
/*
 * Task admission and retirement (port of examples/tasks.act): three
 * workers on caller stacks, the default finalizer, explicit RemTask(NULL),
 * a custom finalizer, Forbid during admission and FindTask.
 */
#include <exec/exec.h>

#define NWORKERS	3
#define STACKSIZE	1024

static struct Task workers[NWORKERS];
static APTR stacks[NWORKERS];
static volatile UBYTE progress[NWORKERS];
static char wname[] = "worker";

static void
first()
{
	progress[0] = 1;
}

static void
second()
{
	progress[1] = 1;
	RemTask(NULL);
	progress[1] = 0xFF;
}

static void
third()
{
	progress[2] = 1;
}

static void
finalizer()
{
	progress[2] = 2;
	RemTask(NULL);
	progress[2] = 0xFF;
}

static char *
prepare(t, i)
register struct Task *t;
int i;
{
	register UBYTE *p;

	p = (UBYTE *)AllocMem(STACKSIZE, MEMF_STACK);
	if (p == NULL)
		return ("stack allocation");
	stacks[i] = (APTR)p;
	t->tc_Node.ln_Type = NT_TASK;
	t->tc_Node.ln_Name = wname;
	t->tc_Node.ln_Pri = -128;
	t->tc_SPLower = (APTR)p;
	t->tc_SPUpper = (APTR)(p + STACKSIZE);
	/* One explicit initial SP; the others default to tc_SPUpper. */
	t->tc_SPReg = i == 2 ? (APTR)(p + STACKSIZE - 2) : NULL;
	NewList(&t->tc_MemEntry);
	return (NULL);
}

static char *
run()
{
	struct Task *me, *found, *added[NWORKERS];
	char *why;
	int i, old, sum;

	if (ExecVersion() != EXEC_TASKS_VERSION)
		return ("ExecVersion");
	me = FindTask(NULL);
	if (me == NULL || me->tc_State != TS_RUN)
		return ("FindTask(NULL)");
	SetTaskPri(me, 0);
	for (i = 0; i < NWORKERS; i++)
		if ((why = prepare(&workers[i], i)) != NULL)
			return (why);

	Forbid();
	added[0] = AddTask(&workers[0], first, NULL);
	added[1] = AddTask(&workers[1], second, NULL);
	added[2] = AddTask(&workers[2], third, finalizer);
	found = FindTask(wname);
	old = 0;
	if (found != NULL)
		old = SetTaskPri(found, 127);
	Yield();
	sum = progress[0] + progress[1] + progress[2];
	Permit();

	for (i = 0; i < NWORKERS; i++)
		if (added[i] != &workers[i])
			return ("AddTask result");
	if (found != &workers[0] && found != &workers[1] &&
	    found != &workers[2])
		return ("FindTask by name");
	if (old != -128)
		return ("SetTaskPri old priority");
	if (sum != 0)
		return ("worker ran under Forbid");

	for (i = 0; i < NWORKERS; i++)
		while (workers[i].tc_State != TS_REMOVED)
			Sleep(1);
	if (FindTask(wname) != NULL)
		return ("FindTask after removal");
	if (progress[0] != 1 || progress[1] != 1 || progress[2] != 2)
		return ("worker progress");
	if (AddTask(NULL, first, NULL) != NULL)
		return ("AddTask(NULL)");
	for (i = 0; i < NWORKERS; i++)
		FreeMem(stacks[i], STACKSIZE);
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL tasks: %s\n", why);
		return (1);
	}
	KPutStr("PASS tasks\n");
	return (0);
}
