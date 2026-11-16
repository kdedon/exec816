/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * A 48 KB stack segment on a 1 MB machine, set in the image (mkdisk.py
 * --stack=48): the segment's RAM from offset 0x4000 to 0x4300 up, the
 * heap 16 KB shorter (mapcheck), a 20 KB CreateTask stack below the old
 * 32 KB low end, and a write below the new low end trapping.
 */
/* EXPECT: exec900: alert 80000002 task root */
#include "test.h"
#include "memmap.h"

#define BIGSTK	20480

static volatile int ok;
static UBYTE *low;

static void
body()
{
	UBYTE f[BIGSTK - 1024];
	register UWORD i;

	for (i = 0; i < sizeof f; i++)
		f[i] = (UBYTE)i;
	for (i = 0; i < sizeof f && f[i] == (UBYTE)i; i++)
		;
	low = f;
	ok = i == sizeof f ? 1 : -1;
}

static char *
run()
{
	struct Task *t;
	char *why;

	stkexpect = 0x4000;
	if ((why = mapcheck(14, 0, 0)) != NULL)
		return (why);
	if ((t = CreateTask("bigstk", 1L, (TASKPROC)body, (ULONG)BIGSTK)) ==
	    NULL)
		return ("CreateTask with a 20 KB stack");
	while (ok == 0)
		;
	if (ok < 0)
		return ("the big frame changed");
	if (POFF(low) >= STKSEGLOW)
		return ("the frame is not below the 32 KB segment's low end");
	xKPrintf("stkcfg: segment from %x, heap tail %u, frame at %lx\n",
	    kstklow, HeapTail, POFF(low));
	return (NULL);
}

int
main()
{
	register UWORD *g;
	char *why;
	UWORD v;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL stkcfg: %s\n", why);
		return (0);
	}
	g = (UWORD *)MKPTR(SEGBASE(STACKSEG), kstklow - 2);
	v = *g;
	*g = v;
	KPutStr("FAIL stkcfg: wrote below the stack RAM without a trap\n");
	return (0);
}
