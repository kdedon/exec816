/* SPDX-License-Identifier: MIT */
/*
 * getenv over DOS variables: a name putenv added to environ first, then
 * GetVar's (the process's local variable, else the global one, from
 * ENV: when it is assigned).  The value is copied into one malloc
 * buffer that the next call replaces.
 */
#include "libcint.h"

extern char **environ;
char *malloc();
void free();

char *
getenv(name)
char *name;
{
	static char *val;
	register char *p, *q, **ep;
	char c[1];
	LONG n;

	for (ep = environ; ep != NULL && (p = *ep) != NULL; ep++) {
		for (q = name; *q != '\0' && *q == *p; q++, p++)
			;
		if (*q == '\0' && *p == '=')
			return (p + 1);
	}
	if (GetVar(name, c, 1L, 0L) < 0)
		return (NULL);
	n = IoErr() + 1;
	if (val != NULL)
		free(val);
	if ((val = malloc((unsigned)n)) == NULL)
		return (NULL);
	if (GetVar(name, val, n, 0L) < 0)
		return (NULL);
	return (val);
}
