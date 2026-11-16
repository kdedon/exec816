/* SPDX-License-Identifier: MIT */
/*
 * DOS program startup (lib/dstartup.s enters here) and amiga.lib's
 * Printf and FPrintf.  dos.library passes the argument string (the rest
 * of the command line, ending in a newline) and its length, as AmigaDOS
 * does in A0 and D0; main(args, len) gets them, with DOSBase open, and
 * its result is the program's return code.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

LONG
dosstart(args, len)
char *args;
LONG len;
{
	LONG rc;
	int main();

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 36L)) == NULL)
		return ((LONG)RETURN_FAIL);
	rc = main(args, len);
	CloseLibrary((struct Library *)DOSBase);
	return (rc);
}

LONG
Printf(fmt, args)
char *fmt;
int args;
{
	return (VPrintf(fmt, (APTR)&args));
}

LONG
FPrintf(fh, fmt, args)
BPTR fh;
char *fmt;
int args;
{
	return (VFPrintf(fh, fmt, (APTR)&args));
}
