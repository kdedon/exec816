/* SPDX-License-Identifier: MIT */
/*
 * MEMF_SEM_PROTECTED pools: three CPU-bound Tasks of equal priority share
 * one MEMF_CLEAR pool under tick preemption, interleaving AllocPooled and
 * FreePooled of puddle and over-threshold blocks.  Each block is checked
 * clear on allocation and intact before it is freed.  Odd-numbered
 * blocks go through AllocVecPooled and FreeVecPooled.  DeletePool with
 * blocks still out returns all memory.
 */
#include "test.h"
#include <exec/exec.h>

#define NTASK		3
#define NBLK		24
#define NOPS		3000
#define PUDDLE		2400L
#define THRESH		600L
#define STACKSIZE	1024
#define DONE_BIT	16
#define DONE_MASK	(1L << DONE_BIT)

static struct Task *parent;
static APTR pool;
static UBYTE *blk[NTASK][NBLK];
static UWORD len[NTASK][NBLK];
static char *fail[NTASK];
static volatile int last, done;
static volatile ULONG switches;

static ULONG
rnd(sp, n)
ULONG *sp;
ULONG n;
{
	*sp = *sp * 1103515245L + 12345L;
	return ((*sp >> 16) % n);
}

/* A block holds its owner's pattern in every byte. */
static int
intact(b, n, v)
register UBYTE *b;
register UWORD n;
register UBYTE v;
{
	while (n-- > 0)
		if (*b++ != v)
			return (0);
	return (1);
}

static void
fill(b, n, v)
register UBYTE *b;
register UWORD n;
register UBYTE v;
{
	while (n-- > 0)
		*b++ = v;
}

static char *
work(t)
int t;
{
	ULONG seed;
	register int i, k;
	UBYTE v;

	seed = 7919L * (t + 1);
	for (k = 0; k < NOPS; k++) {
		i = (int)rnd(&seed, (ULONG)NBLK);
		v = (UBYTE)(t * NBLK + i + 1);
		if (blk[t][i] != NULL) {
			if (!intact(blk[t][i], len[t][i], v))
				return ("pooled block overwritten");
			if (i & 1)
				FreeVecPooled(pool, blk[t][i]);
			else
				FreePooled(pool, blk[t][i], (ULONG)len[t][i]);
			blk[t][i] = NULL;
		} else {
			len[t][i] = rnd(&seed, 8L) == 0 ?
			    THRESH + 1 + rnd(&seed, 1000L) :
			    1 + rnd(&seed, 200L);
			blk[t][i] = (UBYTE *)(i & 1 ?
			    AllocVecPooled(pool, (ULONG)len[t][i]) :
			    AllocPooled(pool, (ULONG)len[t][i]));
			if (blk[t][i] == NULL)
				return ("AllocPooled");
			if (!intact(blk[t][i], len[t][i], 0))
				return ("CLEAR pool block not clear");
			fill(blk[t][i], len[t][i], v);
		}
		if (last != t) {
			last = t;
			switches++;
		}
	}
	for (i = 0; i < NBLK; i++)
		if (blk[t][i] != NULL &&
		    !intact(blk[t][i], len[t][i], (UBYTE)(t * NBLK + i + 1)))
			return ("pooled block overwritten");
	return (NULL);
}

static void
finish(t)
int t;
{
	fail[t] = work(t);
	Forbid();
	done++;
	Signal(parent, DONE_MASK);
}

static void
w0()
{
	finish(0);
}

static void
w1()
{
	finish(1);
}

static void
w2()
{
	finish(2);
}

static char *
run()
{
	static void (*entry[NTASK])() = { w0, w1, w2 };
	ULONG avail;
	int i, bit;

	parent = FindTask(NULL);
	bit = AllocSignal(DONE_BIT);
	if (bit != DONE_BIT)
		return ("AllocSignal");
	avail = AvailMem(0);
	if (CreatePool(MEMF_SEM_PROTECTED, 100, 200) != NULL)
		return ("CreatePool threshold over puddle size");
	if (AvailMem(0) != avail)
		return ("failed CreatePool leaked");
	pool = CreatePool(MEMF_PUBLIC | MEMF_CLEAR | MEMF_SEM_PROTECTED,
	    PUDDLE, THRESH);
	if (pool == NULL)
		return ("CreatePool");
	last = -1;
	for (i = 0; i < NTASK; i++)
		if (CreateTask("sempool", 0, entry[i], STACKSIZE) == NULL)
			return ("CreateTask");
	SetTaskPri(parent, -1);
	while (done != NTASK)
		Wait(DONE_MASK);
	SetTaskPri(parent, 0);
	for (i = 0; i < NTASK; i++)
		if (fail[i] != NULL)
			return (fail[i]);
	/* Only the tick switches the workers; each must have been cut off
	 * many times. */
	if (switches < 3 * NTASK)
		return ("workers did not interleave");
	DeletePool(pool);
	FreeSignal(bit);
	if (AvailMem(0) != avail)
		return ("DeletePool leaked");
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL sempool: %s\n", why);
		return (1);
	}
	xKPrintf("sempool: %ld switches\n", switches);
	KPutStr("PASS sempool\n");
	return (0);
}
