/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * RAM above the MMU's reach (0x40-0x7E, the most the ROM's memory test
 * accepts in the emulator): kboot's runs 08-36, 38-3E and 40-7E give 50
 * identity heap pages from 0x0A up, pages 0x30 and 0x31 and the first
 * pages above 0x3F in the relocation slots, and the other pages above
 * 0x3F in the physical-only pool, all reachable through the windows.
 */
/* EMUFLAGS: --ram=08-7E */
#include "test.h"
#include "memmap.h"

int
main()
{
	char *why;
	int high;	/* pages above 0x3F in relocation slots */

	high = relocslots() - 2;
	if ((why = mapcheck(52 + high, 2 + high, 63 - high)) == NULL &&
	    (kphys((APTR)SEGBASE(RELOCSEG0)) != 0x300000L ||
	    kphys((APTR)SEGBASE(0x07)) != 0x400000L ||
	    kphys((APTR)SEGBASE(0x08)) != 0x410000L))
		why = "relocation slots hold the wrong pages";
	if (why == NULL)
		why = poolcheck(63 - high);
	if (why == NULL)
		why = mapcheck(52 + high, 2 + high, 63 - high);
	if (why != NULL)
		xKPrintf("FAIL highmem: %s\n", why);
	else
		KPutStr("PASS highmem\n");
	return (0);
}
