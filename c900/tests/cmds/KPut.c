/* SPDX-License-Identifier: MIT */
/*
 * C:KPut text: print text on a line of its own on the kernel console
 * (the test transcript), without double quotes and with tabs as blanks
 * (typed test input has no blanks).
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <lvo/dos_lib.h>

int
main(args, len)
char *args;
LONG len;
{
	register char *p, *q;

	for (p = q = args; *p != '\0'; p++)
		if (*p != '"')
			*q++ = *p == '\t' ? ' ' : *p;
	*q = '\0';
	KPutStr("\n");
	KPutStr(args);
	return (RETURN_OK);
}
