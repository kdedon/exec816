/* SPDX-License-Identifier: MIT */
/*
 * Names: unlink, remove, rmdir, mkdir, rename, getcwd and getwd over
 * DeleteFile, CreateDir, Rename and NameFromLock.  Names are AmigaDOS
 * paths, passed through unchanged.
 */
#include <errno.h>
#include "libcint.h"

int
unlink(name)
char *name;
{
	return (DeleteFile(name) == DOSFALSE ? _doserr() : 0);
}

int
remove(name)
char *name;
{
	return (unlink(name));
}

int
rmdir(name)
char *name;
{
	return (unlink(name));
}

int
mkdir(name, mode)
char *name;
int mode;
{
	register BPTR l;

	if ((l = CreateDir(name)) == NULL)
		return (_doserr());
	UnLock(l);
	return (0);
}

/* As POSIX, an existing new name is replaced (deleted first: not
 * atomic). */
int
rename(from, to)
char *from, *to;
{
	if (Rename(from, to) != DOSFALSE)
		return (0);
	if (IoErr() != ERROR_OBJECT_EXISTS || DeleteFile(to) == DOSFALSE ||
	    Rename(from, to) == DOSFALSE)
		return (_doserr());
	return (0);
}

char *
getcwd(buf, size)
char *buf;
int size;
{
	if (buf == NULL || size <= 0) {
		errno = EINVAL;
		return (NULL);
	}
	if (NameFromLock(((struct Process *)FindTask(NULL))->pr_CurrentDir,
	    buf, (LONG)size) == DOSFALSE) {
		if (IoErr() == ERROR_LINE_TOO_LONG)
			errno = ERANGE;
		else
			_doserr();
		return (NULL);
	}
	return (buf);
}

/* COHERENT's getwd: the current directory in a static buffer. */
char *
getwd()
{
	static char wd[256];

	return (getcwd(wd, sizeof(wd)));
}
