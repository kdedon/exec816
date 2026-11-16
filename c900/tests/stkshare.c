/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The stack region as shared memory of the lowest priority, on 1 MB: an
 * ordinary allocation takes it only once the heap is exhausted and the
 * memory handlers have run, from its top, while stacks still come from
 * its bottom; AvailMem counts it once; pools, AllocVec and the whole
 * region can live there, but no DMA block; freeing everything restores
 * every count.
 */
#include "test.h"

#define MAXBLK	128
#define STKSIZE	4000L
#define DMABLK	15000L		/* about lance.device's block */

static UBYTE *blk[MAXBLK];
static ULONG len[MAXBLK];
static int nblk;
static int hcalls, hfree;
static volatile int ran;
static UBYTE *frame;

static LONG
handler(mhd, data, sysbase)
struct MemHandlerData *mhd;
APTR data, sysbase;
{
	hcalls++;
	if (!hfree || nblk == 0)
		return (MEM_DID_NOTHING);
	hfree = 0;
	nblk--;
	FreeMem((APTR)blk[nblk], len[nblk]);
	return (MEM_ALL_DONE);
}

static void
child()
{
	UBYTE f[16];

	frame = f;
	ran = 1;
}

/* An address in the stack region. */
static int
instk(p)
APTR p;
{
	return (p != NULL && PSEG(p) == SEGBASE(STACKSEG) &&
	    (TypeOfMem(p) & MEMF_STACK) != 0);
}

static ULONG
max(a, b)
ULONG a, b;
{
	return (a > b ? a : b);
}

/* Take the whole heap, largest block first. */
static char *
fillheap()
{
	ULONG n;

	while ((n = AvailMem(MEMF_UPPER | MEMF_LARGEST)) != 0) {
		if (nblk >= MAXBLK)
			return ("too many heap blocks");
		if ((blk[nblk] = (UBYTE *)AllocMem(n, MEMF_UPPER)) == NULL)
			return ("AvailMem(MEMF_UPPER | MEMF_LARGEST) failed");
		len[nblk++] = n;
	}
	return (NULL);
}

static char *
run()
{
	struct Interrupt is;
	struct Task *t;
	APTR pool;
	UBYTE *a, *b, *s, *v, *q, *big;
	ULONG up, stk, any, n;
	int i;
	char *why;

	up = AvailMem(MEMF_UPPER);
	stk = AvailMem(MEMF_STACK);
	any = AvailMem(MEMF_ANY);
	if (any != up + stk)
		return ("AvailMem(MEMF_ANY) does not count the region once");
	if (AvailMem(MEMF_TOTAL) != AvailMem(MEMF_UPPER | MEMF_TOTAL) +
	    AvailMem(MEMF_STACK | MEMF_TOTAL))
		return ("AvailMem(MEMF_TOTAL)");
	if (AvailMem(MEMF_LARGEST) != max(AvailMem(MEMF_UPPER |
	    MEMF_LARGEST), AvailMem(MEMF_STACK | MEMF_LARGEST)))
		return ("AvailMem(MEMF_LARGEST)");
	if (kstklow > STKSEGMIN + 0x300 || stk < 0xd000L)
		return ("the default segment is not 63 KB");

	/* With heap free, ordinary memory is heap, but for small blocks,
	 * which take the region's top while it keeps 20 KB. */
	if ((a = (UBYTE *)AllocMem(256L, MEMF_PUBLIC)) == NULL || instk(a))
		return ("an ordinary block left the heap");
	FreeMem(a, 256L);
	if ((a = (UBYTE *)AllocMem(128L, MEMF_PUBLIC)) == NULL || !instk(a) ||
	    POFF(a) + 128L != STKPOOLTOP)
		return ("a small block is not at the region's top");
	FreeMem(a, 128L);
	if ((a = (UBYTE *)AllocMem(128L, MEMF_PUBLIC | MEMF_24BITDMA)) ==
	    NULL || instk(a))
		return ("a small DMA block took the region");
	FreeMem(a, 128L);

	is.is_Node.ln_Pri = 0;
	is.is_Code = (int (*)())handler;
	is.is_Data = NULL;
	AddMemHandler(&is);
	if ((why = fillheap()) != NULL)
		return (why);
	if (AvailMem(MEMF_STACK) != stk || AvailMem(MEMF_ANY) != stk)
		return ("filling the heap touched the region");
	if (AvailMem(MEMF_LARGEST) != AvailMem(MEMF_STACK | MEMF_LARGEST))
		return ("AvailMem(MEMF_LARGEST) without heap");

	/* A handler that frees heap memory keeps the request in the heap.
	 * The request is not a small block. */
	while (nblk > 1 && len[nblk - 1] <= 128L) {
		nblk--;
		FreeMem((APTR)blk[nblk], len[nblk]);
	}
	hfree = 1;
	n = len[nblk - 1];
	if ((a = (UBYTE *)AllocMem(n, MEMF_PUBLIC)) == NULL || instk(a) ||
	    hcalls != 1)
		return ("the handlers did not run before the spill");
	FreeMem(a, n);
	if ((why = fillheap()) != NULL)
		return (why);

	/* Spilled: from the region's top, after the handlers. */
	hcalls = 0;
	a = (UBYTE *)AllocMem(1000L, MEMF_PUBLIC | MEMF_CLEAR);
	if (!instk(a) || hcalls != 1 || POFF(a) + 1000L != STKPOOLTOP)
		return ("an ordinary block did not spill to the region's top");
	for (i = 0; i < 1000; i++)
		if (a[i] != 0)
			return ("spilled MEMF_CLEAR block not clear");
	if (AvailMem(MEMF_STACK) != stk - 1000L ||
	    AvailMem(MEMF_ANY) != stk - 1000L)
		return ("AvailMem after the spill");
	hcalls = 0;
	if ((b = (UBYTE *)AllocMem(200L, MEMF_NO_EXPUNGE)) == NULL ||
	    !instk(b) || hcalls != 0 || b + 200 != a)
		return ("MEMF_NO_EXPUNGE spill");
	if (AllocMem(16L, MEMF_UPPER) != NULL)
		return ("MEMF_UPPER spilled");
	hcalls = 0;
	if (AllocMem(DMABLK, MEMF_PUBLIC | MEMF_24BITDMA) != NULL ||
	    hcalls != 1)
		return ("a DMA block spilled");

	/* Stacks still come from the bottom. */
	if ((s = (UBYTE *)AllocMem(STKSIZE, MEMF_STACK)) == NULL ||
	    !instk(s) || s + STKSIZE > b)
		return ("a stack after the spill");
	FreeMem(s, STKSIZE);
	if ((t = CreateTask("shared", 1L, (TASKPROC)child, STKSIZE)) == NULL)
		return ("CreateTask after the spill");
	while (!ran)
		;
	if (frame > b || PSEG(frame) != SEGBASE(STACKSEG))
		return ("the Task's stack is not below the spilled blocks");

	/* Pools and AllocVec spill the same way. */
	if ((pool = CreatePool(MEMF_PUBLIC | MEMF_CLEAR, 2048L, 512L)) ==
	    NULL || !instk(pool))
		return ("CreatePool");
	if ((q = (UBYTE *)AllocPooled(pool, 100L)) == NULL || !instk(q) ||
	    q[99] != 0 || (big = (UBYTE *)AllocPooled(pool, 3000L)) == NULL ||
	    !instk(big))
		return ("AllocPooled");
	FreePooled(pool, q, 100L);
	if ((v = (UBYTE *)AllocVec(500L, MEMF_CLEAR)) == NULL || !instk(v) ||
	    v[499] != 0)
		return ("AllocVec");
	FreeVec(v);
	DeletePool(pool);
	if (AvailMem(MEMF_STACK) != stk - 1200L)
		return ("pool or AllocVec memory not returned");

	/* Data can fill the region; then no stack fits. */
	n = AvailMem(MEMF_LARGEST);
	if ((big = (UBYTE *)AllocMem(n, 0L)) == NULL || !instk(big) ||
	    AllocMem(512L, MEMF_STACK) != NULL)
		return ("the whole region as data");
	FreeMem(big, n);

	FreeMem(b, 200L);
	FreeMem(a, 1000L);
	RemMemHandler(&is);
	while (nblk > 0) {
		nblk--;
		FreeMem((APTR)blk[nblk], len[nblk]);
	}
	if (AvailMem(MEMF_STACK) != stk || AvailMem(MEMF_UPPER) != up ||
	    AvailMem(MEMF_ANY) != any)
		return ("the region and heap did not revert");
	xKPrintf("stkshare: heap %lu, region %lu, region low %x\n", up, stk,
	    kstklow);
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL stkshare: %s\n", why);
		return (1);
	}
	KPutStr("PASS stkshare\n");
	return (0);
}
