/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The stack segment has RAM from kstklow up.  The page below it can be
 * read, but a write there (a stack overflowing the pool's low end) takes
 * the segment trap.
 */
/* EXPECT: exec900: alert 80000002 task root */
#include "test.h"

int
main()
{
	register UWORD *g;
	UWORD v;

	g = (UWORD *)MKPTR(SEGBASE(STACKSEG), kstklow - 2);
	v = *g;
	xKPrintf("stkguard: read %x below the stack RAM; writing\n", v);
	*g = v;
	KPutStr("FAIL stkguard: wrote below the stack RAM without a trap\n");
	return (0);
}
