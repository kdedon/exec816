/* SPDX-License-Identifier: MIT */
/*
 * The COHERENT system calls with no AmigaDOS counterpart: each fails
 * with ENOSYS.  User and group ids are 0, the process id is a constant,
 * umask keeps nothing and sync has nothing to do (DOS writes through).
 */
#include <errno.h>
#include "libcint.h"

#define PID	1

static int
nosys()
{
	errno = ENOSYS;
	return (-1);
}

int acct() { return (nosys()); }
int alarm() { return (nosys()); }
int alarm2() { return (nosys()); }
int bpt() { return (nosys()); }
int chmod() { return (nosys()); }
int chown() { return (nosys()); }
int chroot() { return (nosys()); }
int dup() { return (nosys()); }
int execve() { return (nosys()); }
int fcntl() { return (nosys()); }
int fork() { return (nosys()); }
int getrlimit() { return (nosys()); }
int halt() { return (nosys()); }
int ioctl() { return (nosys()); }
int kill() { return (nosys()); }
int link() { return (nosys()); }
int lock() { return (nosys()); }
int mknod() { return (nosys()); }
int mount() { return (nosys()); }
int mpx() { return (nosys()); }
int nice() { return (nosys()); }
int pause() { return (nosys()); }
int pipe() { return (nosys()); }
int poll() { return (nosys()); }
int profil() { return (nosys()); }
int ptrace() { return (nosys()); }
int reboot() { return (nosys()); }
int setgid() { return (nosys()); }
int setpgrp() { return (nosys()); }
int setrlimit() { return (nosys()); }
int setuid() { return (nosys()); }
int sload() { return (nosys()); }
int stime() { return (nosys()); }
int suload() { return (nosys()); }
long times() { return ((long)nosys()); }
int umount() { return (nosys()); }
int uname() { return (nosys()); }
int unique() { return (nosys()); }
int utime() { return (nosys()); }
int wait() { return (nosys()); }

char *
brk()
{
	nosys();
	return ((char *)-1L);
}

char *
sbrk()
{
	errno = ENOMEM;
	return ((char *)-1L);
}

int (*
signal())()
{
	nosys();
	return ((int (*)())-1L);
}

int getpid() { return (PID); }
int getuid() { return (0); }
int geteuid() { return (0); }
int getgid() { return (0); }
int getegid() { return (0); }
int umask() { return (022); }
int sync() { return (0); }

/* No core dump to make: the program ends with RETURN_FAIL, unflushed. */
void
abort()
{
	_exit(RETURN_FAIL);
}
