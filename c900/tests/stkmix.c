/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The stacks of a booted Workbench system on 1 MB, as MEMF_STACK blocks:
 * the DOS handlers and Boot process, the Workbench's Tasks with an open
 * menu, a Shell, then 4,000-byte command stacks until one fails.  The
 * default 63 KB segment must fit at least nine commands; the heap must
 * still be usable, and the rest of the region by ordinary memory.
 */
#include "test.h"

#define NFIX	10
#define CMDSTK	4000L
#define MAXCMD	32

static ULONG fixed[NFIX] = {
	1024L, 1280L, 1024L, 1536L,		/* CON:, boot volume, RAM:, Boot */
	4096L, 3584L, 2048L, 1536L, 3072L,	/* input, Workbench, console,
						 * WB handler, menu */
	2048L					/* Shell */
};
static UBYTE *fp[NFIX];
static UBYTE *cp[MAXCMD];

int
main()
{
	register int i, n, cmds;
	ULONG up, stk, left, spare;
	UBYTE *d;

	up = AvailMem(MEMF_UPPER);
	stk = AvailMem(MEMF_STACK);
	for (i = 0; i < NFIX; i++)
		if ((fp[i] = (UBYTE *)AllocMem(fixed[i], MEMF_STACK)) == NULL) {
			xKPrintf("FAIL stkmix: system stack %d\n", i);
			return (1);
		}
	for (n = 0; n < MAXCMD; n++)
		if ((cp[n] = (UBYTE *)AllocMem(CMDSTK, MEMF_STACK)) == NULL)
			break;
	left = AvailMem(MEMF_STACK);
	spare = AvailMem(MEMF_LARGEST | MEMF_STACK);
	d = NULL;
	if (spare != 0)
		d = (UBYTE *)AllocMem(spare, MEMF_STACK);
	xKPrintf("stkmix: segment from %x, region %lu, heap %lu: %d commands,"
	    " %lu stack bytes left\n", kstklow, stk, up, n, left);
	if (d != NULL)
		FreeMem(d, spare);
	cmds = n;
	while (--n >= 0)
		FreeMem(cp[n], CMDSTK);
	for (i = 0; i < NFIX; i++)
		FreeMem(fp[i], fixed[i]);
	if (AvailMem(MEMF_STACK) != stk || AvailMem(MEMF_UPPER) != up) {
		KPutStr("FAIL stkmix: memory not returned\n");
		return (1);
	}
	if (kstklow <= STKSEGMIN + 0x300 && cmds < 9) {
		KPutStr("FAIL stkmix: fewer than nine commands\n");
		return (1);
	}
	KPutStr("PASS stkmix\n");
	return (0);
}
