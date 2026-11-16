/* SPDX-License-Identifier: MIT */
/*
 * CreateTask: refused stacks, priority order, and repeated create and
 * self-removal returning every tc_MemEntry record and stack to the free
 * pools.
 */
#include <exec/exec.h>

#define STACKSIZE	2000
#define DONE_BIT	16
#define DONE_MASK	(1L << DONE_BIT)
#define ROUNDS		40

static struct Task *parent;
static volatile int nrun;
static int order[3];

static void
child()
{
	Forbid();
	nrun++;
	Signal(parent, DONE_MASK);
	RemTask(NULL);
}

/* Returning uses the default finalizer, which frees the same way. */
static void
ranked()
{
	order[nrun++] = FindTask(NULL)->tc_Node.ln_Pri;
}

static char *
run()
{
	ULONG stk0, any0;
	struct Task *t;
	int i, bit;

	parent = FindTask(NULL);
	SetTaskPri(parent, 0);
	bit = AllocSignal(DONE_BIT);
	if (bit != DONE_BIT)
		return ("AllocSignal");

	stk0 = AvailMem(MEMF_STACK);
	any0 = AvailMem(MEMF_ANY);
	/* AddTask refuses these; CreateTask frees its MemList. */
	if (CreateTask("bad", 0, child, 64) != NULL ||
	    CreateTask("bad", 0, NULL, STACKSIZE) != NULL)
		return ("invalid CreateTask accepted");
	if (AvailMem(MEMF_STACK) != stk0 || AvailMem(MEMF_ANY) != any0)
		return ("refused CreateTask kept memory");
	for (i = 0; i < ROUNDS; i++) {
		/* Odd size: rounded up and freed with the rounded size. */
		if (CreateTask("child", 0, child, STACKSIZE + 1) == NULL)
			return ("CreateTask");
		while (nrun != i + 1)
			Wait(DONE_MASK);
	}
	if (AvailMem(MEMF_STACK) != stk0 || AvailMem(MEMF_ANY) != any0)
		return ("tc_MemEntry memory not reclaimed");

	/* Higher priorities run first once Permit releases them. */
	nrun = 0;
	Forbid();
	if (CreateTask("p1", 1, ranked, STACKSIZE) == NULL ||
	    CreateTask("p5", 5, ranked, STACKSIZE) == NULL ||
	    CreateTask("p3", 3, ranked, STACKSIZE) == NULL) {
		Permit();
		return ("CreateTask ranked");
	}
	t = FindTask("p3");
	if (t == NULL || SetTaskPri(t, 7) != 3) {
		Permit();
		return ("SetTaskPri");
	}
	Permit();
	if (nrun != 3)
		return ("ranked Tasks did not preempt");
	if (order[0] != 7 || order[1] != 5 || order[2] != 1)
		return ("priority order");
	if (AvailMem(MEMF_STACK) != stk0 || AvailMem(MEMF_ANY) != any0)
		return ("ranked memory not reclaimed");
	FreeSignal(bit);
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL createtask: %s\n", why);
		return (1);
	}
	KPutStr("PASS createtask\n");
	return (0);
}
