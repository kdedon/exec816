/* SPDX-License-Identifier: MIT */
/*
 * C:DosPass, the command tests/sys/Startup-Sequence runs as
 * "DosPass one two" after "CD SYS:C" and "Stack 6000": checks what the shell and
 * RunCommand give a command, then prints the dosboot test's PASS line
 * on the kernel console, which reaches the serial transcript whether or
 * not a display takes textcon.device's output.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <lvo/dos_lib.h>

static char *
check(args, len)
char *args;
LONG len;
{
	register struct CommandLineInterface *cli;
	register struct Task *t;
	register char *a, *b;
	char buf[40];
	BPTR fh, lk;
	LONG n;

	for (a = args, b = "one two\n"; *a == *b && *b != '\0'; a++, b++)
		;
	if (*b != '\0' || len != 8)
		return ("arguments");
	if ((cli = Cli()) == NULL)
		return ("no CLI");
	t = FindTask(NULL);
	if (cli->cli_DefaultStack * 4 != 6000 ||
	    (char *)t->tc_SPUpper - (char *)t->tc_SPLower != 6000)
		return ("the stack Startup-Sequence set");
	if (Input() == NULL || IsInteractive(Output()) != DOSTRUE)
		return ("streams");
	GetProgramName(buf, (LONG)sizeof(buf));
	for (a = buf, b = "DosPass"; *a == *b && *b != '\0'; a++, b++)
		;
	if (*a != *b)
		return ("program name");
	GetCurrentDirName(buf, (LONG)sizeof(buf));
	for (a = buf, b = "System:C"; *a == *b && *b != '\0'; a++, b++)
		;
	if (*a != *b)
		return ("current directory");
	if ((fh = Open("ENV:Language", MODE_OLDFILE)) == NULL)
		return ("ENV: from ENVARC:");
	n = Read(fh, buf, (LONG)sizeof(buf));
	Close(fh);
	if (n != 8 || buf[0] != 'e')
		return ("ENV:Language");
	if ((lk = Lock("T:", SHARED_LOCK)) == NULL)
		return ("T:");
	NameFromLock(lk, buf, (LONG)sizeof(buf));
	UnLock(lk);
	for (a = buf, b = "Ram Disk:T"; *a == *b && *b != '\0'; a++, b++)
		;
	if (*a != *b)
		return ("T: is Ram Disk:T");
	return (NULL);
}

int
main(args, len)
char *args;
LONG len;
{
	char *why;

	if ((why = check(args, len)) != NULL) {
		KPutStr("FAIL dosboot: DosPass: ");
		KPutStr(why);
		KPutStr("\n");
		return (RETURN_FAIL);
	}
	KPutStr("PASS dosboot\n");
	return (RETURN_OK);
}
