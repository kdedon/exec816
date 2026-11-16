/* SPDX-License-Identifier: MIT */
/* C:RetCode N: return N; with a second word, write it to the output. */
#include <exec/exec.h>
#include <dos/dos.h>
#include <lvo/dos_lib.h>

int
main(args, len)
char *args;
LONG len;
{
	register char *p;
	LONG n, k;

	if ((k = StrToLong(args, &n)) < 0)
		return (RETURN_FAIL);
	for (p = args + (int)k; *p == ' '; p++)
		;
	if (*p != '\n' && *p != '\0')
		PutStr(p);
	return ((int)n);
}
