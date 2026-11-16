/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The stacks of a booted Workbench system, as MEMF_STACK blocks, shared by
 * stkmix and stkbig: the DOS handlers and Boot process, the Workbench's
 * Tasks with an open menu and a Shell (21,248 bytes), then 4,000-byte
 * command stacks until one fails.
 */

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

/* The fixed stacks, then commands: their count, or -1. */
static int
mixalloc()
{
	register int i, n;

	for (i = 0; i < NFIX; i++)
		if ((fp[i] = (UBYTE *)AllocMem(fixed[i], MEMF_STACK)) == NULL) {
			while (--i >= 0)
				FreeMem(fp[i], fixed[i]);
			return (-1);
		}
	for (n = 0; n < MAXCMD; n++)
		if ((cp[n] = (UBYTE *)AllocMem(CMDSTK, MEMF_STACK)) == NULL)
			break;
	return (n);
}

static void
mixfree(n)
register int n;
{
	register int i;

	while (--n >= 0)
		FreeMem(cp[n], CMDSTK);
	for (i = 0; i < NFIX; i++)
		FreeMem(fp[i], fixed[i]);
}
