/* SPDX-License-Identifier: MIT */
/* atexit: at most 32 functions, called by exit in reverse order. */
#include "libcint.h"

#define NATEXIT	32

static void (*fns[NATEXIT])();
static int nfns;

static void
runexit()
{
	while (nfns > 0)
		(*fns[--nfns])();
}

int
atexit(f)
void (*f)();
{
	if (nfns >= NATEXIT)
		return (-1);
	fns[nfns++] = f;
	_exithk = runexit;
	return (0);
}
