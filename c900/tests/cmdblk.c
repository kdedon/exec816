/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * CMDSEG is a writable alias of the disk command area at phys 0x080000,
 * the first CMDLEN bytes of the kernel's text page.
 */
#include "test.h"

int
main()
{
	register UWORD *cmd, *txt;
	ULONG b;
	char *why;

	b = SEGBASE(CMDSEG);
	cmd = (UWORD *)b;
	b = SEGBASE(KTEXTSEG);
	txt = (UWORD *)b;
	why = NULL;
	if (kphys((APTR)cmd) != CMDPHYS)
		why = "kphys(CMDSEG) is not the command area";
	else {
		cmd[CMD_FD / 2] = 0x5aa5;
		cmd[(CMDLEN - 2) / 2] = 0xa55a;
		if (txt[CMD_FD / 2] != 0x5aa5 ||
		    txt[(CMDLEN - 2) / 2] != 0xa55a)
			why = "writes through CMDSEG not seen through text";
	}
	if (why != NULL)
		xKPrintf("FAIL cmdblk: %s\n", why);
	else
		KPutStr("PASS cmdblk\n");
	return (0);
}
