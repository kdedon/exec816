/* SPDX-License-Identifier: MIT */
/*
 * Memory pools: churn through several puddles, the threshold path, a
 * block over 64 KB, MEMF_CLEAR, and AvailMem restored by FreePooled of
 * every block and by DeletePool with blocks still out.  AllocVecPooled
 * and FreeVecPooled go through the same paths.
 */
/* EMUFLAGS: --ram=08-35 */
#include "test.h"

#define NBLK	64
#define PUDDLE	4096L
#define THRESH	1024L

static UBYTE *blk[NBLK];
static ULONG len[NBLK];

static ULONG seed = 12345L;
static char *why;

static ULONG
rnd(n)
ULONG n;
{
	seed = seed * 1103515245L + 12345L;
	return ((seed >> 16) % n);
}

/* Each block's first and last byte carry its index. */
static int
intact(i)
int i;
{
	return (blk[i][0] == (UBYTE)i && blk[i][len[i] - 1] == (UBYTE)~i);
}

static char *
churn(pool)
APTR pool;
{
	register int i, k;

	for (k = 0; k < 2000; k++) {
		i = (int)rnd((ULONG)NBLK);
		if (blk[i] != NULL) {
			if (!intact(i))
				return ("pooled block overwritten");
			FreePooled(pool, blk[i], len[i]);
			blk[i] = NULL;
			continue;
		}
		/* Mostly puddle-sized, some over the threshold. */
		len[i] = rnd(8L) == 0 ? THRESH + 1 + rnd(3000L) : 2 + rnd(THRESH - 1);
		if ((blk[i] = (UBYTE *)AllocPooled(pool, len[i])) == NULL)
			return ("AllocPooled");
		if ((POFF(blk[i]) & 7) != 0)
			return ("pooled block misaligned");
		blk[i][0] = i;
		blk[i][len[i] - 1] = ~i;
	}
	for (i = 0; i < NBLK; i++)
		if (blk[i] != NULL && !intact(i))
			return ("pooled block overwritten");
	return (NULL);
}

/* Vector blocks: puddle and threshold sizes, freed out of order, then
 * DeletePool with some still out.  Returns AvailMem to its start. */
static char *
vec(reqs, avail)
ULONG reqs, avail;
{
	APTR pool;
	ULONG empty;
	register UBYTE *b;
	register int i, k;

	if (AllocVecPooled(NULL, 16) != NULL)
		return ("AllocVecPooled NULL pool");
	if ((pool = CreatePool(reqs, PUDDLE, THRESH)) == NULL)
		return ("CreatePool vec");
	empty = AvailMem(0);
	if (AllocVecPooled(pool, 0) != NULL)
		return ("AllocVecPooled size 0");
	FreeVecPooled(pool, NULL);
	for (i = 0; i < NBLK; i++) {
		len[i] = i % 5 == 0 ? THRESH - 4 + i * 7 : 1 + i * 13;
		if ((b = blk[i] = (UBYTE *)AllocVecPooled(pool, len[i])) == NULL)
			return ("AllocVecPooled");
		if ((POFF(b) & 7) != 0)
			return ("vector block misaligned");
		for (k = 0; k < len[i]; k++) {
			if ((reqs & MEMF_CLEAR) && b[k] != 0)
				return ("CLEAR pool vector block not clear");
			b[k] = (UBYTE)(i + k);
		}
	}
	/* Odd blocks first, then even ones downward. */
	for (i = 1; i < NBLK; i += 2) {
		for (b = blk[i], k = 0; k < len[i]; k++)
			if (b[k] != (UBYTE)(i + k))
				return ("vector block overwritten");
		FreeVecPooled(pool, blk[i]);
	}
	for (i = NBLK - 2; i >= 0; i -= 2) {
		for (b = blk[i], k = 0; k < len[i]; k++)
			if (b[k] != (UBYTE)(i + k))
				return ("vector block overwritten");
		FreeVecPooled(pool, blk[i]);
		blk[i] = NULL;
	}
	if (AvailMem(0) != empty)
		return ("FreeVecPooled did not return the puddles");
	for (i = 0; i < NBLK; i++)
		if ((blk[i] = (UBYTE *)AllocVecPooled(pool, len[i])) == NULL)
			return ("AllocVecPooled again");
	DeletePool(pool);
	for (i = 0; i < NBLK; i++)
		blk[i] = NULL;
	if (AvailMem(0) != avail)
		return ("DeletePool leaked vector blocks");
	return (NULL);
}

static char *
run()
{
	APTR pool;
	UBYTE *big, *c;
	ULONG avail;
	int i;

	avail = AvailMem(0);
	if (CreatePool(0, 100, 200) != NULL)
		return ("CreatePool threshold over puddle size");
	if ((pool = CreatePool(MEMF_PUBLIC, PUDDLE, THRESH)) == NULL)
		return ("CreatePool");
	if ((why = churn(pool)) != NULL)
		return (why);
	for (i = 0; i < NBLK; i++)
		if (blk[i] != NULL) {
			FreePooled(pool, blk[i], len[i]);
			blk[i] = NULL;
		}
	/* Only the pool header is left: empty puddles were returned. */
	if (AvailMem(0) != avail - 40)
		return ("FreePooled did not return the puddles");

	/* Over 64 KB, and DeletePool with blocks still out. */
	if ((big = (UBYTE *)AllocPooled(pool, 150000L)) == NULL)
		return ("AllocPooled 150000");
	if (kphys(PtrStep(big, 149999L)) - kphys(big) != 149999L)
		return ("150000-byte pooled block not contiguous");
	if ((why = churn(pool)) != NULL)
		return (why);
	DeletePool(pool);
	for (i = 0; i < NBLK; i++)
		blk[i] = NULL;
	if (AvailMem(0) != avail)
		return ("DeletePool leaked");

	/* A CLEAR pool clears reused puddle space. */
	pool = CreatePool(MEMF_CLEAR, PUDDLE, THRESH);
	c = (UBYTE *)AllocPooled(pool, 500);
	for (i = 0; i < 500; i++)
		c[i] = 0xa5;
	FreePooled(pool, c, 500);
	AllocPooled(pool, 8);
	c = (UBYTE *)AllocPooled(pool, 500);
	for (i = 0; i < 500; i++)
		if (c[i] != 0)
			return ("CLEAR pool block not clear");
	DeletePool(pool);
	if (AvailMem(0) != avail)
		return ("CLEAR pool leaked");

	if ((why = vec(0L, avail)) != NULL ||
	    (why = vec(MEMF_CLEAR | MEMF_SEM_PROTECTED, avail)) != NULL)
		return (why);
	return (NULL);
}

int
main()
{
	if ((why = run()) != NULL) {
		xKPrintf("FAIL pools: %s\n", why);
		return (1);
	}
	KPutStr("PASS pools\n");
	return (0);
}
