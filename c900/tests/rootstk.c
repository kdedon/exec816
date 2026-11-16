/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * A 1 KB root stack on the default 1 MB machine, set in the image
 * (mkdisk.py --rootstack=1): the root program runs within it, and the
 * pool ends 3 KB higher (rootstk.h).
 */
#include "test.h"
#include "rootstk.h"

int
main()
{
	char *why;

	if ((why = rootcheck("rootstk")) != NULL)
		xKPrintf("FAIL rootstk: %s\n", why);
	else
		KPutStr("PASS rootstk\n");
	return (0);
}
