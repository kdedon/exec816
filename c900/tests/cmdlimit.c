/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * CMDSEG's limit ends at CMDLEN: the first byte past it takes the segment
 * trap instead of writing over kernel text.
 */
/* EXPECT: exec900: alert 80000002 task root */
#include "test.h"

int
main()
{
	register UWORD *cmd;
	ULONG b;

	b = SEGBASE(CMDSEG);
	cmd = (UWORD *)b;
	KPutStr("cmdlimit: writing past the command area\n");
	cmd[CMDLEN / 2] = 0;
	KPutStr("FAIL cmdlimit: wrote past CMDLEN without a trap\n");
	return (0);
}
