/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Logical address space on the default 1 MB machine: the stack segment
 * after the image, then heap to page 0x17 (14 pages when the stack ends in
 * page 0x09, plus the rest of that page), identity-mapped; kphys agrees,
 * window 0 aliases a heap block, no pool.
 */
#include "test.h"
#include "memmap.h"

int
main()
{
	char *why;

	if ((why = mapcheck(14, 0, 0)) == NULL && kallocphys() != -1)
		why = "kallocphys found a page in an empty pool";
	if (why != NULL)
		xKPrintf("FAIL memmap: %s\n", why);
	else
		KPutStr("PASS memmap\n");
	return (0);
}
