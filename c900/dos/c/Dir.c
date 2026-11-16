/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C:Dir DIR,OPT/K,ALL/S,DIRS/S,FILES/S,INTER/S: list a directory (the
 * current one without DIR), directories first, as "name (dir)", then
 * files with their sizes.  A DIR whose last part has wildcards lists
 * what matches it.  ALL (OPT A) lists each directory's contents after
 * its name, indented; DIRS (OPT D) shows only directories, FILES (OPT
 * F) only files.  INTER (OPT I) is not supported.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <lvo/dos_lib.h>

static UBYTE pat[128];
static char path[128];
static int all, only, haspat;
static char indent[] = "                    ";

/* List directory lk: 0, or the error that stopped it. */
static LONG
list(lk, depth)
BPTR lk;
int depth;
{
	register struct FileInfoBlock *fib;
	register int pass, dir;
	LONG a[3], r;
	BPTR sub, old;
	char *in;

	if ((fib = (struct FileInfoBlock *)AllocDosObject((ULONG)DOS_FIB,
	    NULL)) == NULL)
		return (IoErr());
	in = indent + sizeof(indent) - 1 - 2 * (depth < 9 ? depth + 1 : 10);
	a[0] = (LONG)in;
	r = 0;
	for (pass = 1; pass >= 0 && r == 0; pass--) {
		if (Examine(lk, fib) == DOSFALSE) {
			r = IoErr();
			break;
		}
		while (ExNext(lk, fib) != DOSFALSE) {
			if (CheckSignal(SIGBREAKF_CTRL_C)) {
				r = ERROR_BREAK;
				break;
			}
			dir = fib->fib_DirEntryType > 0;
			if (dir != pass || (haspat &&
			    !MatchPatternNoCase(pat, fib->fib_FileName)))
				continue;
			a[1] = (LONG)fib->fib_FileName;
			a[2] = fib->fib_Size;
			if (only == 0 || only == (dir ? 'D' : 'F'))
				VPrintf(dir ? "%s%s (dir)\n" : "%s%s  %ld\n",
				    (APTR)a);
			if (dir && all) {
				old = CurrentDir(lk);
				sub = Lock(fib->fib_FileName, SHARED_LOCK);
				CurrentDir(old);
				if (sub != NULL) {
					r = list(sub, depth + 1);
					UnLock(sub);
					if (r != 0)
						break;
				}
			}
		}
		if (r == 0 && IoErr() != ERROR_NO_MORE_ENTRIES)
			r = IoErr();
	}
	FreeDosObject((ULONG)DOS_FIB, fib);
	return (r);
}

int
main(args, len)
char *args;
LONG len;
{
	struct RDArgs *rd;
	LONG a[6], r;
	register char *d, *s, *e;
	BPTR lk;

	a[0] = (LONG)"";
	a[1] = a[2] = a[3] = a[4] = a[5] = 0;
	if ((rd = ReadArgs("DIR,OPT/K,ALL/S,DIRS/S,FILES/S,INTER/S", a,
	    (struct RDArgs *)NULL)) == NULL) {
		PrintFault(IoErr(), NULL);
		return (RETURN_ERROR);
	}
	all = a[2] != 0;
	only = a[3] ? 'D' : a[4] ? 'F' : 0;
	for (s = (char *)a[1]; s != NULL && *s != '\0'; s++)
		if (*s == 'a' || *s == 'A')
			all = 1;
		else if (*s == 'd' || *s == 'D')
			only = 'D';
		else if (*s == 'f' || *s == 'F')
			only = 'F';
	s = (char *)a[0];
	if ((lk = Lock(s, SHARED_LOCK)) == NULL &&
	    (haspat = ParsePatternNoCase(FilePart(s), pat,
	    (LONG)sizeof(pat)) > 0)) {
		e = PathPart(s);
		for (d = path; s < e && d < path + sizeof(path) - 1; )
			*d++ = *s++;
		*d = '\0';
		s = (char *)a[0];
		lk = Lock(path, SHARED_LOCK);
	}
	if (lk == NULL) {
		PrintFault(IoErr(), s);
		FreeArgs(rd);
		return (RETURN_ERROR);
	}
	r = list(lk, 0);
	UnLock(lk);
	FreeArgs(rd);
	if (r != 0) {
		PrintFault(r, r == ERROR_BREAK ? NULL : "Dir");
		return (r == ERROR_BREAK ? RETURN_WARN : RETURN_ERROR);
	}
	return (RETURN_OK);
}
