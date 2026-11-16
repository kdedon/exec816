/* SPDX-License-Identifier: MIT */
/*
 * File descriptors over dos.library handles.  Descriptors 0, 1 and 2
 * are the process's Input(), Output() and error stream (pr_CES, else
 * Output()); they belong to the caller and are never closed.  Handles
 * are unbuffered, so each read, write and lseek is one DOS call.
 */
#include <errno.h>
#include <fcntl.h>
#include "libcint.h"

int errno;
struct ufd _fds[NFD];

/* AmigaDOS error codes (less 100) and their errno values; EIO for the
 * rest. */
static unsigned char errmap[] = {
	ERROR_OBJECT_NOT_FOUND - 100, ENOENT,
	ERROR_DIR_NOT_FOUND - 100, ENOENT,
	ERROR_DEVICE_NOT_MOUNTED - 100, ENOENT,
	ERROR_NO_DEFAULT_DIR - 100, ENOENT,
	ERROR_OBJECT_EXISTS - 100, EEXIST,
	ERROR_DIRECTORY_NOT_EMPTY - 100, EEXIST,
	ERROR_OBJECT_IN_USE - 100, EBUSY,
	ERROR_NO_FREE_STORE - 100, ENOMEM,
	ERROR_DISK_FULL - 100, ENOSPC,
	ERROR_OBJECT_WRONG_TYPE - 100, ENOTDIR,
	ERROR_DISK_WRITE_PROTECTED - 100, EROFS,
	ERROR_WRITE_PROTECTED - 100, EROFS,
	ERROR_DELETE_PROTECTED - 100, EACCES,
	ERROR_READ_PROTECTED - 100, EACCES,
	ERROR_RENAME_ACROSS_DEVICES - 100, EXDEV,
	ERROR_OBJECT_TOO_LARGE - 100, EFBIG,
	ERROR_SEEK_ERROR - 100, EINVAL,
	ERROR_INVALID_COMPONENT_NAME - 100, EINVAL,
	ERROR_INVALID_LOCK - 100, EINVAL,
	ERROR_LINE_TOO_LONG - 100, EINVAL,
	ERROR_ACTION_NOT_KNOWN - 100, ENOSYS,
	ERROR_NOT_IMPLEMENTED - 100, ENOSYS,
	0, EIO
};

/* Set errno from IoErr() and return -1. */
int
_doserr()
{
	register unsigned char *m;
	register LONG e;

	e = IoErr() - 100L;
	for (m = errmap; m[0] != 0 && (LONG)m[0] != e; m += 2)
		;
	errno = m[1];
	return (-1);
}

void
_fdinit()
{
	register struct Process *me;

	me = (struct Process *)FindTask(NULL);
	_fds[0].fd_FH = Input();
	_fds[1].fd_FH = Output();
	_fds[2].fd_FH = me->pr_CES != NULL ? me->pr_CES : Output();
	_fds[0].fd_Flags = _fds[0].fd_FH != NULL ? FD_OPEN | FD_STD : 0;
	_fds[1].fd_Flags = _fds[1].fd_FH != NULL ? FD_OPEN | FD_STD : 0;
	_fds[2].fd_Flags = _fds[2].fd_FH != NULL ? FD_OPEN | FD_STD : 0;
}

void
_fdexit()
{
	register int fd;

	for (fd = 0; fd < NFD; fd++)
		if ((_fds[fd].fd_Flags & (FD_OPEN | FD_STD)) == FD_OPEN)
			Close(_fds[fd].fd_FH);
}

/* The handle of an open descriptor, or NULL with errno EBADF. */
BPTR
_fdfh(fd)
register int fd;
{
	if (fd < 0 || fd >= NFD || !(_fds[fd].fd_Flags & FD_OPEN)) {
		errno = EBADF;
		return (NULL);
	}
	return (_fds[fd].fd_FH);
}

/* O_CREAT|O_TRUNC is MODE_NEWFILE, O_CREAT alone MODE_READWRITE (open or
 * create), anything else MODE_OLDFILE; O_TRUNC on an existing file
 * without O_CREAT also takes MODE_NEWFILE.  The mode is ignored. */
int
open(name, flags, mode)
char *name;
int flags, mode;
{
	register int fd;
	register BPTR fh;
	LONG am;
	BPTR l;

	for (fd = 0; fd < NFD && (_fds[fd].fd_Flags & FD_OPEN); fd++)
		;
	if (fd == NFD) {
		errno = EMFILE;
		return (-1);
	}
	if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL) ||
	    (flags & (O_CREAT | O_TRUNC)) == O_TRUNC) {
		l = Lock(name, SHARED_LOCK);
		if (l != NULL)
			UnLock(l);
		if ((flags & O_CREAT) && l != NULL) {
			errno = EEXIST;
			return (-1);
		}
		if (!(flags & O_CREAT) && l == NULL)
			return (_doserr());
	}
	if (flags & O_TRUNC)
		am = MODE_NEWFILE;
	else if (flags & O_CREAT)
		am = MODE_READWRITE;
	else
		am = MODE_OLDFILE;
	if ((fh = Open(name, am)) == NULL)
		return (_doserr());
	_fds[fd].fd_FH = fh;
	_fds[fd].fd_Flags = FD_OPEN | (flags & O_APPEND ? FD_APPEND : 0);
	return (fd);
}

int
creat(name, mode)
char *name;
int mode;
{
	return (open(name, O_WRONLY | O_CREAT | O_TRUNC, mode));
}

int
close(fd)
register int fd;
{
	register int f;

	if (_fdfh(fd) == NULL)
		return (-1);
	f = _fds[fd].fd_Flags;
	_fds[fd].fd_Flags = 0;
	if (!(f & FD_STD) && Close(_fds[fd].fd_FH) == DOSFALSE)
		return (_doserr());
	return (0);
}

int
read(fd, buf, n)
int fd;
char *buf;
unsigned n;
{
	register BPTR fh;
	register LONG r;

	if ((fh = _fdfh(fd)) == NULL)
		return (-1);
	if ((r = Read(fh, buf, (LONG)n)) < 0)
		return (_doserr());
	return ((int)r);
}

int
write(fd, buf, n)
int fd;
char *buf;
unsigned n;
{
	register BPTR fh;
	register LONG r;

	if ((fh = _fdfh(fd)) == NULL)
		return (-1);
	if ((_fds[fd].fd_Flags & FD_APPEND) &&
	    Seek(fh, 0L, OFFSET_END) == -1L)
		return (_doserr());
	if ((r = Write(fh, buf, (LONG)n)) < 0)
		return (_doserr());
	return ((int)r);
}

/* Seek returns the old position; the new one is computed, or asked for
 * after a seek from the end.  A console cannot seek (ESPIPE). */
long
lseek(fd, off, whence)
int fd;
long off;
int whence;
{
	register BPTR fh;
	register LONG old;

	if ((fh = _fdfh(fd)) == NULL)
		return (-1L);
	if (whence < 0 || whence > 2) {
		errno = EINVAL;
		return (-1L);
	}
	old = Seek(fh, off, whence == 0 ? OFFSET_BEGINNING :
	    whence == 1 ? OFFSET_CURRENT : OFFSET_END);
	if (old == -1L) {
		_doserr();
		if (errno == ENOSYS)
			errno = ESPIPE;
		return (-1L);
	}
	if (whence == 0)
		return (off);
	if (whence == 1)
		return (old + off);
	return (Seek(fh, 0L, OFFSET_CURRENT));
}

int
isatty(fd)
int fd;
{
	register BPTR fh;

	if ((fh = _fdfh(fd)) == NULL)
		return (0);
	return (IsInteractive(fh) != DOSFALSE);
}
