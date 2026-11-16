/* SPDX-License-Identifier: MIT */
/*
 * malloc, free and realloc over an exec memory pool, made at the first
 * malloc and deleted at exit, so a program's memory returns to the
 * system however it ends.  Each block carries its size in a long before
 * it.  A block lies in one segment, so the largest is 65,500 bytes.
 */
#include "libcint.h"

#define PUDDLE	2048L
#define THRESH	1024L
#define MAXREQ	65500

char *
malloc(n)
unsigned n;
{
	register ULONG *p;

	if (n == 0 || n > MAXREQ)
		return (NULL);
	if (_mpool == NULL &&
	    (_mpool = CreatePool((ULONG)MEMF_PUBLIC, PUDDLE, THRESH)) == NULL)
		return (NULL);
	if ((p = (ULONG *)AllocPooled(_mpool, (ULONG)n + 4L)) == NULL)
		return (NULL);
	*p = (ULONG)n + 4L;
	return ((char *)(p + 1));
}

void
free(cp)
char *cp;
{
	register ULONG *p;

	if (cp == NULL)
		return;
	p = (ULONG *)cp - 1;
	FreePooled(_mpool, p, *p);
}

char *
realloc(cp, n)
char *cp;
unsigned n;
{
	register char *np;
	register unsigned old;

	if (cp == NULL)
		return (malloc(n));
	if (n == 0) {
		free(cp);
		return (NULL);
	}
	old = (unsigned)(((ULONG *)cp)[-1] - 4L);
	if (n <= old && n + 64 > old)
		return (cp);
	if ((np = malloc(n)) == NULL)
		return (NULL);
	CopyMem(cp, np, (ULONG)(n < old ? n : old));
	free(cp);
	return (np);
}
