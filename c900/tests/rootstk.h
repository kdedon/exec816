/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Checks shared by the rootstk tests, on an image whose root stack is
 * 1 KB (mkdisk.py --rootstack=1): the root Task's stack, the idle stack
 * and the pool's top 3 KB higher than by default, the pool spanning the
 * segment's RAM (mapcheck), and the root program's deepest use so far,
 * after boot and a Task round trip, within the 1 KB less the interrupt
 * headroom.
 */
#include "memmap.h"

#define ROOTLOW		0xfc00		/* 0x10000 - 1 KB */
#define STKGUARD	0xC900		/* kernel/tasks.c */

#define CHILDSIG	(1L << 20)

static struct Task *roottask;
static volatile int child;

static void
childbody()
{
	child = 1;
	Signal(roottask, CHILDSIG);
}

/* The bytes of t's stack in use: from the lowest nonzero byte above the
 * guard word to tc_SPUpper (boot cleared the rest). */
static UWORD
stkused(t)
struct Task *t;
{
	register UBYTE *p, *top;

	top = (UBYTE *)t->tc_SPUpper;
	for (p = (UBYTE *)t->tc_SPLower + 2; p < top && *p == 0; p++)
		;
	return ((UWORD)(top - p));
}

static char *
rootcheck(name)
char *name;
{
	register struct MemHeader *mh;
	register struct Node *n;
	UWORD used;
	char *why;

	roottask = FindTask(NULL);
	if (kstkcfg.sc_root != ROOTLOW || POFF(roottask->tc_SPLower) !=
	    ROOTLOW || POFF(roottask->tc_SPUpper) != ROOTSTKTOP)
		return ("the root stack is not the configured 1 KB");
	if (*(UWORD *)roottask->tc_SPLower != STKGUARD)
		return ("the root stack's guard word");
	if ((why = mapcheck(14, 0, 0)) != NULL)
		return (why);
	mh = NULL;
	for (n = SysBase->MemList.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (((struct MemHeader *)n)->mh_Attributes & MEMF_STACK)
			mh = (struct MemHeader *)n;
	if (mh == NULL || POFF(mh->mh_Lower) != kstklow ||
	    POFF(mh->mh_Upper) != ROOTLOW - IDLESTKSIZE)
		return ("the pool does not end at the idle stack");
	if (AvailMem(MEMF_STACK | MEMF_TOTAL) !=
	    (ULONG)(ROOTSTKLOW - IDLESTKSIZE - kstklow) + 3072L)
		return ("the pool did not gain 3 KB");
	if (CreateTask("rootchild", 1L, (TASKPROC)childbody, 1024L) == NULL)
		return ("CreateTask");
	Wait(CHILDSIG);
	if (!child)
		return ("the child did not run");
	used = stkused(roottask);
	xKPrintf("%s: root stack %x-%x, %u bytes used; pool %lu from %x\n",
	    name, ROOTLOW, ROOTSTKTOP, used, AvailMem(MEMF_STACK | MEMF_TOTAL),
	    kstklow);
	if (used + IRQHEADROOM > ROOTSTKTOP - ROOTLOW - 2)
		return ("the root program does not fit 1 KB with headroom");
	if (*(UWORD *)roottask->tc_SPLower != STKGUARD)
		return ("the root stack's guard word");
	return (NULL);
}
