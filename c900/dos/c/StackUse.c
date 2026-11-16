/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C:StackUse: each Task's stack bounds, size and deepest use, then the
 * stack pool's free space.  Use is measured from the zero bytes still at
 * a stack's low end: CreateTask, CreateNewProc and RunCommand clear their
 * stacks and boot clears the root Task's.  The first 8 bytes (AddTask's
 * guard word, or an allocator's chunk header on a stack allocated without
 * MEMF_CLEAR) are not counted.  A process running a command on a stack of
 * its own (RunCommand) also shows its process stack, found on tc_MemEntry.
 */
#include "cargs.h"
#include <exec/execbase.h>

#define MAXT	40
#define NAMELEN	20
#define SKIP	8

struct stk {
	char s_Name[NAMELEN];
	UBYTE *s_Lower;
	UWORD s_Size;
	UWORD s_Used;
	char *s_State;
};

static struct stk st[MAXT];
static int nst;

static void
add(name, lo, hi, state)
char *name;
UBYTE *lo, *hi;
char *state;
{
	register struct stk *s;

	if (nst >= MAXT)
		return;
	s = &st[nst++];
	scopy(s->s_Name, name != NULL ? name : "", NAMELEN);
	s->s_Lower = lo;
	s->s_Size = (UWORD)(hi - lo);
	s->s_State = state;
}

/* The size of the block at p on t's tc_MemEntry, or 0. */
static UWORD
blksize(t, p)
struct Task *t;
APTR p;
{
	register struct Node *n;
	register struct MemList *ml;
	register int i;

	for (n = t->tc_MemEntry.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
		ml = (struct MemList *)n;
		for (i = 0; i < ml->ml_NumEntries; i++)
			if (ml->ml_ME[i].me_Addr == p)
				return ((UWORD)ml->ml_ME[i].me_Length);
	}
	return (0);
}

static void
task(t, state)
register struct Task *t;
char *state;
{
	register struct Process *pr;
	UWORD n;

	add(t->tc_Node.ln_Name, (UBYTE *)t->tc_SPLower,
	    (UBYTE *)t->tc_SPUpper, state);
	if (t->tc_Node.ln_Type != NT_PROCESS)
		return;
	pr = (struct Process *)t;
	if ((APTR)pr->pr_StackBase != NULL &&
	    (APTR)pr->pr_StackBase != t->tc_SPLower &&
	    (n = blksize(t, (APTR)pr->pr_StackBase)) != 0)
		add(t->tc_Node.ln_Name, (UBYTE *)pr->pr_StackBase,
		    (UBYTE *)pr->pr_StackBase + n, "process");
}

static void
list(l, state)
struct List *l;
char *state;
{
	register struct Node *n;

	for (n = l->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		task((struct Task *)n, state);
}

/* Bytes from the first nonzero byte past SKIP to the stack's top. */
static UWORD
used(s)
register struct stk *s;
{
	register UWORD *w, *e;
	register UBYTE *b;

	if (s->s_Size <= SKIP)
		return (s->s_Size);
	w = (UWORD *)(s->s_Lower + SKIP);
	e = (UWORD *)(s->s_Lower + (s->s_Size & ~1));
	while (w < e && *w == 0)
		w++;
	b = (UBYTE *)w;
	if (w < e && *b == 0)
		b++;
	return ((UWORD)(s->s_Lower + s->s_Size - b));
}

int
main(args, len)
char *args;
LONG len;
{
	register int i;
	register struct stk *s;
	ULONG lo;

	nst = 0;
	Forbid();
	Disable();
	task(FindTask(NULL), "run");
	list(&SysBase->TaskReady, "ready");
	list(&SysBase->TaskWait, "wait");
	Enable();
	for (i = 0; i < nst; i++)
		st[i].s_Used = used(&st[i]);
	Permit();
	PutStr("Task                 Lower  Size  Used  Free State\n");
	for (i = 0; i < nst; i++) {
		s = &st[i];
		lo = (ULONG)s->s_Lower;
		Printf("%-20s %5lx %5ld %5ld %5ld %s\n", s->s_Name,
		    lo & 0xffffL, (LONG)s->s_Size,
		    (LONG)s->s_Used, (LONG)(s->s_Size - s->s_Used), s->s_State);
	}
	Printf("Stack pool: %ld of %ld bytes free, largest %ld\n",
	    AvailMem(MEMF_STACK), AvailMem(MEMF_STACK | MEMF_TOTAL),
	    AvailMem(MEMF_STACK | MEMF_LARGEST));
	return (RETURN_OK);
}
