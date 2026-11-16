/* SPDX-License-Identifier: MIT */
/* C:StkSize N: RETURN_OK when this command runs on an N-byte stack. */
#include <exec/exec.h>
#include <dos/dos.h>
#include <lvo/dos_lib.h>

int
main(args, len)
char *args;
LONG len;
{
	register struct Task *t;
	LONG n;

	t = FindTask(NULL);
	if (StrToLong(args, &n) < 0 ||
	    (char *)t->tc_SPUpper - (char *)t->tc_SPLower != n)
		return (RETURN_ERROR);
	return (RETURN_OK);
}
