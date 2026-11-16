/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C:Copy FROM/M,TO/A,ALL/S,QUIET/S,BUF=BUFFER/K/N,CLONE/S,DATES/S,
 * NOPRO/S,COM/S,NOREQ/S: copy a file to a file, or files into a directory; a
 * directory's files (with ALL, its subdirectories too) into a
 * directory, made if missing, listing them unless QUIET.  Protection
 * bits are copied unless NOPRO; dates with DATES, comments with COM,
 * both with CLONE.  BUF and NOREQ are accepted and ignored.
 */
#include "cargs.h"

#define BUFSIZE	4096L

static int all, quiet, dates, com, nopro, list;
static char *buf;

struct cscan {
	struct FileInfoBlock c_Fib;
	char c_From[256];
	char c_To[256];
};

/* Copy file from (fib describes it) to to: 0 after a failure. */
static int
cfile(from, to, fib)
char *from, *to;
register struct FileInfoBlock *fib;
{
	BPTR in, out;
	LONG n;
	int ok;

	if ((in = Open(from, MODE_OLDFILE)) == NULL)
		return (!fail(from));
	if ((out = Open(to, MODE_NEWFILE)) == NULL) {
		Close(in);
		return (!fail(to));
	}
	ok = 1;
	while ((n = Read(in, buf, BUFSIZE)) > 0)
		if (Write(out, buf, n) != n || brk()) {
			ok = 0;
			break;
		}
	if (n < 0 || !ok) {
		ok = 0;
		fail(n < 0 ? from : to);
	}
	Close(out);
	Close(in);
	if (!ok)
		return (0);
	if (!nopro)
		SetProtection(to, fib->fib_Protection);
	if (dates)
		SetFileDate(to, &fib->fib_Date);
	if (com && fib->fib_Comment[0] != '\0')
		SetComment(to, fib->fib_Comment);
	if (list)
		Printf("  %s..copied\n", fib->fib_FileName);
	return (1);
}

/* Copy what directory from holds into directory to, made if missing. */
static int
cdir(from, to)
char *from, *to;
{
	register struct cscan *c;
	register int ok;
	BPTR lk;

	if ((lk = Lock(to, SHARED_LOCK)) == NULL)
		lk = CreateDir(to);
	if (lk == NULL)
		return (!fail(to));
	UnLock(lk);
	if ((c = (struct cscan *)AllocVec((ULONG)sizeof(*c), MEMF_ANY)) ==
	    NULL || (lk = Lock(from, SHARED_LOCK)) == NULL) {
		FreeVec(c);
		return (!fail(from));
	}
	ok = Examine(lk, &c->c_Fib) != DOSFALSE;
	while (ok && ExNext(lk, &c->c_Fib) != DOSFALSE && !brk()) {
		scopy(c->c_From, from, (int)sizeof(c->c_From));
		AddPart(c->c_From, c->c_Fib.fib_FileName, (ULONG)sizeof(c->c_From));
		scopy(c->c_To, to, (int)sizeof(c->c_To));
		AddPart(c->c_To, c->c_Fib.fib_FileName, (ULONG)sizeof(c->c_To));
		if (c->c_Fib.fib_DirEntryType < 0)
			ok = cfile(c->c_From, c->c_To, &c->c_Fib);
		else if (all) {
			if (list)
				Printf("%s (Dir)\n", c->c_Fib.fib_FileName);
			ok = cdir(c->c_From, c->c_To);
		}
	}
	UnLock(lk);
	FreeVec(c);
	return (ok);
}

int
main(args, len)
char *args;
LONG len;
{
	static struct FileInfoBlock fib;
	static char dst[256];
	struct RDArgs *rd;
	LONG a[10];
	register char **nm, *to;
	register int todir, ok;
	BPTR lk;

	for (ok = 0; ok < 10; ok++)
		a[ok] = 0;
	if ((rd = rdargs(
	    "FROM/M,TO/A,ALL/S,QUIET/S,BUF=BUFFER/K/N,CLONE/S,DATES/S,NOPRO/S,COM/S,NOREQ/S",
	    a)) == NULL)
		return (RETURN_ERROR);
	all = a[2] != 0;
	quiet = a[3] != 0;
	dates = a[5] || a[6];
	com = a[5] || a[8];
	nopro = a[7] != 0;
	to = (char *)a[1];
	if ((nm = (char **)a[0]) == NULL) {
		PrintFault(ERROR_REQUIRED_ARG_MISSING, "Copy");
		FreeArgs(rd);
		return (RETURN_ERROR);
	}
	todir = nm[1] != NULL;
	if ((lk = Lock(to, SHARED_LOCK)) != NULL) {
		todir |= Examine(lk, &fib) != DOSFALSE && fib.fib_DirEntryType > 0;
		UnLock(lk);
	}
	if ((buf = (char *)AllocMem(BUFSIZE, MEMF_ANY)) == NULL) {
		PrintFault(ERROR_NO_FREE_STORE, "Copy");
		FreeArgs(rd);
		return (RETURN_FAIL);
	}
	for (ok = 1; ok && *nm != NULL; nm++) {
		if ((lk = Lock(*nm, SHARED_LOCK)) == NULL ||
		    Examine(lk, &fib) == DOSFALSE) {
			UnLock(lk);
			ok = !fail(*nm);
			break;
		}
		UnLock(lk);
		list = !quiet && (fib.fib_DirEntryType > 0 || todir);
		if (fib.fib_DirEntryType > 0)
			ok = cdir(*nm, to);
		else if (todir) {
			scopy(dst, to, (int)sizeof(dst));
			AddPart(dst, FilePart(*nm), (ULONG)sizeof(dst));
			ok = cfile(*nm, dst, &fib);
		} else
			ok = cfile(*nm, to, &fib);
	}
	FreeMem(buf, BUFSIZE);
	FreeArgs(rd);
	return (ok ? RETURN_OK : RETURN_ERROR);
}
