/* SPDX-License-Identifier: MIT */
/*
 * The C library's DOS layer: state shared between its files.
 */
#ifndef LIBCINT_H
#define LIBCINT_H

#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <lvo/exec_lib.h>
#include <lvo/dos_lib.h>

#define NFD	20		/* stdio's _NFILE */

#define FD_OPEN		1
#define FD_STD		2	/* the caller's stream: never closed */
#define FD_APPEND	4

struct ufd {
	BPTR fd_FH;
	int fd_Flags;
};

extern struct DosLibrary *DOSBase;
extern int errno;
extern struct ufd _fds[];
extern APTR _mpool;		/* malloc's pool, or NULL */
extern BPTR _cwdlk;		/* chdir's lock, or NULL */
extern BPTR _cwdorig;		/* the current directory at startup */
extern void (*_exithk)();	/* atexit's runner, or NULL */

BPTR _fdfh();
int _doserr();
void _fdinit();
void _fdexit();
void _exit();

/* Seconds from 1970-01-01 to AmigaDOS's epoch, 1978-01-01. */
#define AMIGAEPOCH	252460800L

#endif
