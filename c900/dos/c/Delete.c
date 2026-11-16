/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C:Delete FILE/M/A,ALL/S,QUIET/S,FORCE/S: delete each name; with ALL a
 * directory's contents first, listing what goes unless QUIET.  FORCE
 * clears the protection bits first.  A directory is read one entry
 * ahead, so an entry is deleted after ExNext has moved past it.
 */
#include "cargs.h"

static int all, quiet, force;

struct dscan {
	struct FileInfoBlock d_Fib;
	char d_Path[256];
	char d_Prev[108];
};

static int del();

/* Delete what directory path holds: 0 after a failure or break. */
static int
deldir(path)
char *path;
{
	register struct dscan *d;
	register int ok;
	BPTR lk;

	if ((d = (struct dscan *)AllocVec((ULONG)sizeof(*d), MEMF_ANY)) ==
	    NULL || (lk = Lock(path, SHARED_LOCK)) == NULL) {
		FreeVec(d);
		return (!fail(path));
	}
	ok = Examine(lk, &d->d_Fib) != DOSFALSE;
	d->d_Prev[0] = '\0';
	while (ok) {
		if (ExNext(lk, &d->d_Fib) == DOSFALSE)
			d->d_Fib.fib_FileName[0] = '\0';
		if (d->d_Prev[0] != '\0') {
			scopy(d->d_Path, path, (int)sizeof(d->d_Path));
			AddPart(d->d_Path, d->d_Prev, (ULONG)sizeof(d->d_Path));
			ok = del(d->d_Path);
		}
		if (d->d_Fib.fib_FileName[0] == '\0' || brk())
			break;
		scopy(d->d_Prev, d->d_Fib.fib_FileName, (int)sizeof(d->d_Prev));
	}
	UnLock(lk);
	FreeVec(d);
	return (ok);
}

static int
del(name)
char *name;
{
	static struct FileInfoBlock fib;
	static char buf[80];
	BPTR lk;
	int dir;

	if ((lk = Lock(name, SHARED_LOCK)) == NULL)
		return (!fail(name));
	dir = Examine(lk, &fib) != DOSFALSE && fib.fib_DirEntryType > 0;
	UnLock(lk);
	if (dir && all && !deldir(name))
		return (0);
	if (force)
		SetProtection(name, 0L);
	if (DeleteFile(name) == DOSFALSE) {
		Fault(IoErr(), NULL, buf, (LONG)sizeof(buf));
		Printf("%s  Not Deleted - %s\n", name, buf);
		return (0);
	}
	if (all && !quiet)
		Printf("%s  Deleted\n", name);
	return (1);
}

int
main(args, len)
char *args;
LONG len;
{
	struct RDArgs *rd;
	LONG a[4];
	register char **w;
	int rc;

	a[0] = a[1] = a[2] = a[3] = 0;
	if ((rd = rdargs("FILE/M/A,ALL/S,QUIET/S,FORCE/S", a)) == NULL)
		return (RETURN_ERROR);
	all = a[1] != 0;
	quiet = a[2] != 0;
	force = a[3] != 0;
	rc = RETURN_OK;
	for (w = (char **)a[0]; *w != NULL; w++)
		if (!del(*w))
			rc = RETURN_WARN;
	FreeArgs(rd);
	return (rc);
}
