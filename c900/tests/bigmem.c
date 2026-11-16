/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * 3 MB machine (RAM card 0x18-0x35): 44 heap pages from 0x0A up, pages
 * 0x30 and 0x31 relocated to slots behind the kernel's segments,
 * everything else identity-mapped, no pool.
 */
/* EMUFLAGS: --ram=08-35 */
#include "test.h"
#include "memmap.h"

int
main()
{
	char *why;

	if ((why = mapcheck(44, 2, 0)) == NULL) {
		if (kphys((APTR)SEGBASE(RELOCSEG0)) != 0x300000L ||
		    kphys((APTR)SEGBASE(RELOCSEG0 + 1)) != 0x310000L)
			why = "pages 0x30 and 0x31 not in the first slots";
	}
	if (why != NULL)
		xKPrintf("FAIL bigmem: %s\n", why);
	else
		KPutStr("PASS bigmem\n");
	return (0);
}
