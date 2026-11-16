/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * What the C: commands share: ReadArgs with the error reported, string
 * copy, fault report and Ctrl-C.  Each command includes this once.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <lvo/dos_lib.h>

#define WORDMAX	108

LONG Printf();

/* ReadArgs of the command line against tpl into arr: NULL, reported,
 * on an error. */
static struct RDArgs *
rdargs(tpl, arr)
char *tpl;
LONG *arr;
{
	register struct RDArgs *rd;

	if ((rd = ReadArgs(tpl, arr, (struct RDArgs *)NULL)) == NULL)
		PrintFault(IoErr(), NULL);
	return (rd);
}

static void
scopy(d, s, n)
register char *d, *s;
register int n;
{
	while (--n > 0 && *s != '\0')
		*d++ = *s++;
	*d = '\0';
}

/* Report IoErr for name; RETURN_ERROR. */
static int
fail(name)
char *name;
{
	PrintFault(IoErr(), name);
	return (RETURN_ERROR);
}

/* Ctrl-C: report ***Break; nonzero. */
static int
brk()
{
	if (CheckSignal(SIGBREAKF_CTRL_C) == 0)
		return (0);
	PrintFault(ERROR_BREAK, NULL);
	return (1);
}
