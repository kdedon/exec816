/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The 63 KB stack segment a 3 MB machine gets: RAM from offset 0x0400 to
 * 0x0700 up, still directly after the image, the heap after it (mapcheck),
 * the whole pool allocatable, and a Task running on the pool's largest
 * block down into the RAM the move added, which a moved root stack and
 * every frame address reach.  Then a write to the page below the new
 * low end takes the segment trap, as below the 32 KB segment.
 */
/* EMUFLAGS: --ram=08-35 */
/* EXPECT: exec900: alert 80000002 task root */
#include "test.h"
#include "memmap.h"

#define LEVEL	200			/* bytes of frame per level */

static struct Task t;
static UBYTE *low;			/* the deepest frame byte reached */
static UBYTE *floor;			/* stop this far above tc_SPLower */
static volatile int ok;

/* Fill a frame per level until the next would pass floor; check every
 * frame on the way back. */
static int
dive(n)
int n;
{
	UBYTE f[LEVEL];
	register int i;
	register int bad;

	for (i = 0; i < LEVEL; i++)
		f[i] = (UBYTE)(n + i);
	if (f - LEVEL - 64 > floor)
		bad = dive(n + 1);
	else {
		low = f;
		bad = 0;
	}
	for (i = 0; i < LEVEL; i++)
		if (f[i] != (UBYTE)(n + i))
			bad = 1;
	return (bad);
}

static void
body()
{
	ok = dive(0) == 0 ? 1 : -1;
}

static char *
run()
{
	register struct Node *n;
	register struct MemHeader *mh;
	ULONG size, largest;
	UBYTE *p;
	char *why;

	if ((why = mapcheck(44, 2, 0)) != NULL)
		return (why);
	if (kstklow < STKSEGMIN || kstklow > STKSEGMIN + 0x300)
		return ("the stack segment did not grow to 63 KB");
	mh = NULL;
	for (n = SysBase->MemList.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (((struct MemHeader *)n)->mh_Attributes & MEMF_STACK)
			mh = (struct MemHeader *)n;
	if (mh == NULL || POFF(mh->mh_Lower) != kstklow ||
	    POFF(mh->mh_Upper) != STKPOOLTOP)
		return ("the pool's MemHeader");
	/* The interrupt stack holds the pool's low end. */
	if (kintstk != kstklow + 512)
		return ("interrupt stack not at the pool's low end");

	/* Every free byte, as one block, then a Task on it. */
	largest = AvailMem(MEMF_STACK | MEMF_LARGEST);
	size = AvailMem(MEMF_STACK);
	if (largest < 0x8000L || (p = (UBYTE *)AllocMem(largest, MEMF_STACK))
	    == NULL)
		return ("the pool's largest block");
	if (AllocMem(8L, MEMF_STACK) != NULL && size == largest)
		return ("8 bytes past the whole pool");
	if (POFF(p) >= STKSEGLOW || kphys((APTR)p) !=
	    ((ULONG)kmmutab[STACKSEG] << 8) + POFF(p))
		return ("the largest block is not in the added RAM");
	t.tc_Node.ln_Type = NT_TASK;
	t.tc_Node.ln_Name = "pooltask";
	t.tc_Node.ln_Pri = 1;
	t.tc_SPLower = (APTR)p;
	t.tc_SPUpper = (APTR)(p + (UWORD)(largest & ~1L));
	NewList(&t.tc_MemEntry);
	floor = p + IRQHEADROOM;
	if (AddTask(&t, body, NULL) == NULL)
		return ("AddTask on the whole pool");
	while (ok == 0)
		;
	if (ok < 0)
		return ("a frame changed under the Task");
	if (POFF(low) >= STKSEGLOW || low > floor + LEVEL + 64 + 64)
		return ("the Task did not reach the added RAM");
	xKPrintf("stkpool: segment from %x, pool %lu, Task to %lx\n", kstklow,
	    largest, POFF(low));
	FreeMem(p, largest);
	if (AvailMem(MEMF_STACK) != size)
		return ("pool not returned");
	return (NULL);
}

int
main()
{
	register UWORD *g;
	char *why;
	UWORD v;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL stkpool: %s\n", why);
		return (0);
	}
	g = (UWORD *)MKPTR(SEGBASE(STACKSEG), kstklow - 2);
	v = *g;
	xKPrintf("stkpool: read %x below the stack RAM; writing\n", v);
	*g = v;
	KPutStr("FAIL stkpool: wrote below the stack RAM without a trap\n");
	return (0);
}
