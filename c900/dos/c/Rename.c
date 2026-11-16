/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C:Rename FROM/A/M,TO=AS/A,QUIET/S: rename an object; when the new
 * name is a directory, move the objects into it.
 */
#include "cargs.h"

int
main(args, len)
char *args;
LONG len;
{
	static struct FileInfoBlock fib;
	static char to[256];
	struct RDArgs *rd;
	LONG a[3];
	register char **from;
	register int dir, rc;
	BPTR lk;

	a[0] = a[1] = a[2] = 0;
	if ((rd = rdargs("FROM/A/M,TO=AS/A,QUIET/S", a)) == NULL)
		return (RETURN_ERROR);
	dir = 0;
	if ((lk = Lock((char *)a[1], SHARED_LOCK)) != NULL) {
		dir = Examine(lk, &fib) != DOSFALSE && fib.fib_DirEntryType > 0;
		UnLock(lk);
	}
	rc = RETURN_OK;
	for (from = (char **)a[0]; *from != NULL; from++) {
		scopy(to, (char *)a[1], (int)sizeof(to));
		if (dir)
			AddPart(to, FilePart(*from), (ULONG)sizeof(to));
		if (Rename(*from, to) == DOSFALSE) {
			Printf("Can't rename %s as %s\n", *from, to);
			rc = fail("Rename");
		}
	}
	FreeArgs(rd);
	return (rc);
}
