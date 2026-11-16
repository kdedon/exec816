/* SPDX-License-Identifier: GPL-3.0-only */
/* C:MakeDir NAME/M: make each directory. */
#include "cargs.h"

int
main(args, len)
char *args;
LONG len;
{
	struct RDArgs *rd;
	LONG a[1];
	register char **w;
	BPTR lk;
	int rc;

	a[0] = 0;
	if ((rd = rdargs("NAME/M/A", a)) == NULL)
		return (RETURN_ERROR);
	rc = RETURN_OK;
	for (w = (char **)a[0]; *w != NULL; w++)
		if ((lk = CreateDir(*w)) == NULL) {
			Printf("Can't create directory %s\n", *w);
			rc = fail("MakeDir");
		} else
			UnLock(lk);
	FreeArgs(rd);
	return (rc);
}
