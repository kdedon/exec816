/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Large blocks beside the Workbench's stacks on 1 MB: 16 KB of 128-byte
 * blocks, the stack mix's fixed stacks (wbmix.h), a 640x400x4 chunky
 * bitmap's 128,000 bytes and a 70,000-byte AllocMem, each over whole heap
 * pages, then command stacks.  The small blocks stay out of the whole
 * pages, in the stack region, and at least five commands still fit.
 */
#include "test.h"
#include "wbmix.h"

#define NSMALL	128
#define SMALL	128L
#define BITMAP	128000L
#define BIGMEM	70000L

static UBYTE *sp[NSMALL];

int
main()
{
	register int i;
	int n;
	ULONG up, stk, largest;
	UBYTE *bm, *big;
	UWORD low;

	up = AvailMem(MEMF_UPPER);
	stk = AvailMem(MEMF_STACK);
	largest = AvailMem(MEMF_LARGEST);
	for (i = 0; i < NSMALL; i++)
		if ((sp[i] = (UBYTE *)AllocMem(SMALL, MEMF_PUBLIC)) == NULL) {
			KPutStr("FAIL stkbig: small block\n");
			return (1);
		}
	if (AvailMem(MEMF_LARGEST) != largest) {
		KPutStr("FAIL stkbig: small blocks split a whole page\n");
		return (1);
	}
	bm = (UBYTE *)AllocMem(BITMAP, MEMF_PUBLIC | MEMF_CLEAR);
	big = (UBYTE *)AllocMem(BIGMEM, MEMF_PUBLIC);
	if ((n = mixalloc()) < 0) {
		KPutStr("FAIL stkbig: system stacks\n");
		return (1);
	}
	low = kstklow;
	xKPrintf("stkbig: largest %lu, bitmap at %lx, %lu at %lx: %d commands,"
	    " region from %x\n", largest, kphys((APTR)bm), BIGMEM,
	    kphys((APTR)big), n, low);
	mixfree(n);
	if (bm == NULL || big == NULL) {
		KPutStr("FAIL stkbig: a block over 64 KB\n");
		return (1);
	}
	FreeMem(big, BIGMEM);
	FreeMem(bm, BITMAP);
	for (i = 0; i < NSMALL; i++)
		FreeMem(sp[i], SMALL);
	if (AvailMem(MEMF_STACK) != stk || AvailMem(MEMF_UPPER) != up) {
		KPutStr("FAIL stkbig: memory not returned\n");
		return (1);
	}
	if (n < 5) {
		KPutStr("FAIL stkbig: fewer than five commands\n");
		return (1);
	}
	KPutStr("PASS stkbig\n");
	return (0);
}
