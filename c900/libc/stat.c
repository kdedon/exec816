/* SPDX-License-Identifier: MIT */
/*
 * stat, fstat, access and chdir over Lock, Examine and ExamineFH.  The
 * mode comes from the entry type and the owner's protection bits (set
 * bits forbid, as on the Amiga); times are the entry's date; a
 * descriptor that cannot be examined (a console, NIL:) is a character
 * device.
 */
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "libcint.h"

static struct FileInfoBlock *
newfib()
{
	register struct FileInfoBlock *fib;

	if ((fib = (struct FileInfoBlock *)AllocMem((ULONG)sizeof(*fib),
	    MEMF_PUBLIC)) == NULL)
		errno = ENOMEM;
	return (fib);
}

static void
endfib(fib)
struct FileInfoBlock *fib;
{
	FreeMem(fib, (ULONG)sizeof(*fib));
}

/* Examine the object name names into fib: 0, or -1 with errno. */
static int
examine(name, fib)
char *name;
struct FileInfoBlock *fib;
{
	register BPTR l;
	register int r;

	if ((l = Lock(name, SHARED_LOCK)) == NULL)
		return (_doserr());
	r = Examine(l, fib) == DOSFALSE ? _doserr() : 0;
	UnLock(l);
	return (r);
}

static void
fillstat(fib, st)
register struct FileInfoBlock *fib;
register struct stat *st;
{
	register int m;
	register struct DateStamp *ds;

	m = 0;
	if (!(fib->fib_Protection & FIBF_READ))
		m |= 0444;
	if (!(fib->fib_Protection & FIBF_WRITE))
		m |= 0200;
	if (!(fib->fib_Protection & FIBF_EXECUTE))
		m |= 0100;
	st->st_mode = (fib->fib_DirEntryType > 0 ? S_IFDIR | 0111 : S_IFREG) | m;
	st->st_ino = (ino_t)fib->fib_DiskKey;
	st->st_nlink = 1;
	st->st_size = fib->fib_Size;
	ds = &fib->fib_Date;
	st->st_mtime = AMIGAEPOCH + ds->ds_Days * 86400L + ds->ds_Minute * 60L +
	    ds->ds_Tick / TICKS_PER_SECOND;
	st->st_atime = st->st_ctime = st->st_mtime;
}

static void
zstat(st)
struct stat *st;
{
	register char *p;
	register int n;

	for (p = (char *)st, n = sizeof(*st); n > 0; n--)
		*p++ = 0;
}

int
stat(name, st)
char *name;
struct stat *st;
{
	register struct FileInfoBlock *fib;
	register int r;

	if ((fib = newfib()) == NULL)
		return (-1);
	zstat(st);
	if ((r = examine(name, fib)) == 0)
		fillstat(fib, st);
	endfib(fib);
	return (r);
}

int
fstat(fd, st)
int fd;
struct stat *st;
{
	register struct FileInfoBlock *fib;
	register BPTR fh;

	if ((fh = _fdfh(fd)) == NULL || (fib = newfib()) == NULL)
		return (-1);
	zstat(st);
	if (IsInteractive(fh) == DOSFALSE && ExamineFH(fh, fib) != DOSFALSE)
		fillstat(fib, st);
	else {
		st->st_mode = S_IFCHR | 0666;
		st->st_nlink = 1;
	}
	endfib(fib);
	return (0);
}

/* Existence, then the owner's R, W and X bits as stat reports them. */
int
access(name, amode)
char *name;
int amode;
{
	register struct FileInfoBlock *fib;
	struct stat st;
	register int r;

	if ((fib = newfib()) == NULL)
		return (-1);
	if ((r = examine(name, fib)) == 0) {
		zstat(&st);
		fillstat(fib, &st);
		if ((amode & 4) && !(st.st_mode & 0400) ||
		    (amode & 2) && !(st.st_mode & 0200) ||
		    (amode & 1) && !(st.st_mode & 0100)) {
			errno = EACCES;
			r = -1;
		}
	}
	endfib(fib);
	return (r);
}

/* The startup directory is the caller's; exit returns to it. */
int
chdir(name)
char *name;
{
	register struct FileInfoBlock *fib;
	register BPTR l;
	int r;

	if ((fib = newfib()) == NULL)
		return (-1);
	if ((l = Lock(name, SHARED_LOCK)) == NULL)
		r = _doserr();
	else if (Examine(l, fib) == DOSFALSE)
		r = _doserr();
	else if (fib->fib_DirEntryType <= 0) {
		errno = ENOTDIR;
		r = -1;
	} else
		r = 0;
	endfib(fib);
	if (r != 0) {
		if (l != NULL)
			UnLock(l);
		return (r);
	}
	CurrentDir(l);
	if (_cwdlk != NULL)
		UnLock(_cwdlk);
	_cwdlk = l;
	return (0);
}
