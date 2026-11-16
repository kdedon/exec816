/* SPDX-License-Identifier: MIT */
/*
 * strerror and perror over COHERENT's sys_errlist, which also knows
 * ENOSYS (Exec900's errno for a call it does not provide).
 */
#include <stdio.h>
#include <errno.h>

extern char *sys_errlist[];
extern int sys_nerr;

char *
strerror(e)
int e;
{
	if (e == ENOSYS)
		return ("function not implemented");
	if (e < 0 || e >= sys_nerr)
		return ("bad error number");
	return (sys_errlist[e]);
}

int
perror(s)
char *s;
{
	if (s != NULL && *s != '\0') {
		fputs(s, stderr);
		fputs(": ", stderr);
	}
	fputs(strerror(errno), stderr);
	fputs("\n", stderr);
	return (0);
}
