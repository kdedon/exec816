/* SPDX-License-Identifier: MIT */
/*
 * The shell's built-ins and the C: commands, through S:ShellX
 * (tests/sys/ShellX): .key arguments and defaults, If/Else/EndIf with
 * every condition, Skip/Lab (also BACK) and EndSkip, redirected
 * built-ins (Echo, Fault, Why, Failat, Stack, Ask), Stack's command
 * stack, Path, Resident, a nested Execute, then Copy, MakeDir, Rename,
 * Protect, Assign (multi-directory), Delete ALL, Info, Version, Date and
 * Wait, and NewShell.  The script reports failures with C:KPut; this
 * test then checks what the redirected commands wrote, two shells
 * running scripts at once (Run), Quit's code through a nested script, a
 * failure ending nested scripts, and a missing /A argument.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

static char *why;
static char buf[400];

#define CHECK(c, w)	if (!(c)) { why = w; return (1); }

static LONG
sys(cmd)
char *cmd;
{
	struct TagItem t[1];

	t[0].ti_Tag = TAG_DONE;
	return (SystemTagList(cmd, t));
}

/* The file's text in buf. */
static int
slurp(name)
char *name;
{
	BPTR fh;
	LONG n;

	buf[0] = '\0';
	if ((fh = Open(name, MODE_OLDFILE)) == NULL)
		return (0);
	n = Read(fh, buf, (LONG)sizeof(buf) - 1);
	Close(fh);
	buf[n < 0 ? 0 : n] = '\0';
	return (n >= 0);
}

static int
is(name, text)
char *name;
register char *text;
{
	register char *p;

	if (!slurp(name))
		return (0);
	for (p = buf; *p == *text && *p != '\0'; p++, text++)
		;
	return (*p == *text);
}

static int
has(name, text)
char *name, *text;
{
	register char *p, *a, *b;

	if (!slurp(name))
		return (0);
	for (p = buf; *p != '\0'; p++) {
		for (a = p, b = text; *a == *b && *b != '\0'; a++, b++)
			;
		if (*b == '\0')
			return (1);
	}
	return (0);
}

static int
stest()
{
	int n;

	CHECK(SelectOutput(Open("CON:", MODE_NEWFILE)) == NULL &&
	    Output() != NULL, "an output on CON:");
	CHECK(sys("Execute S:ShellX One SW OPT two") == 0, "S:ShellX's result");
	CHECK(is("RAM:echo", "hello  world again\n"), "Echo");
	CHECK(is("RAM:noline", "x"), "Echo NOLINE");
	CHECK(is("RAM:fault", "Fault 205: object not found\n"), "Fault");
	CHECK(is("RAM:why", "Last command failed because object not found\n"),
	    "Why");
	CHECK(is("RAM:failat", "Fail limit: 21\n"), "Failat");
	CHECK(is("RAM:stack", "Current stack size is 4000 bytes\n"), "Stack");
	CHECK(is("RAM:path", "Current directory\nRam Disk:bin\nC:\n"), "Path");
	CHECK(has("RAM:resident", "RC2"), "the Resident list");
	CHECK(has("RAM:inner/cli", "\n"), "the nested script's <$$>");
	CHECK(is("RAM:protect", "RAM:dir2/echo2 ----rwed\n"), "Protect");
	CHECK(has("RAM:assign", "XT           Ram Disk:dir\n") &&
	    has("RAM:assign", "             + Ram Disk:dir2\n") &&
	    has("RAM:assign", "             + Ram Disk:inner\n") &&
	    has("RAM:assign", "Ram Disk [Mounted]") &&
	    has("RAM:assign", "LIBS         <SYS:Libs>"), "the Assign list");
	CHECK(has("RAM:info", "Ram Disk") && has("RAM:info", "RAM:"), "Info");
	CHECK(is("RAM:version", "dos.library 39.1\n"), "Version");
	CHECK(is("RAM:verfile", "Shell 39.2 (14.11.2026)\n"), "Version FILE");
	CHECK(has("RAM:date", ":"), "Date");
	for (n = 0; n < 50 && !has("RAM:newshell", "\n"); n++)
		Delay(10L);
	CHECK(n < 50, "NewShell FROM");
	CHECK(sys("Execute S:ShellW") == 0 && is("RAM:runok", "ok\n") &&
	    is("RAM:waited", "done\n"), "two shells' scripts at once");
	CHECK(sys("Execute S:ShellQ") == 7, "Quit");
	CHECK(sys("Execute S:ShellF") == 20, "a failure ends nested scripts");
	CHECK(sys("Execute S:ShellX") == 20, "a missing /A argument");
	return (0);
}

static LONG
pmain(args, len)
char *args;
LONG len;
{
	return ((LONG)stest());
}

struct dsync {
	struct Task *s_Task;
	LONG s_RC;
};

static void
pdone(rc, s)
LONG rc;
struct dsync *s;
{
	s->s_RC = rc;
	Signal(s->s_Task, SIGBREAKF_CTRL_F);
}

int
main()
{
	struct TagItem t[4];
	struct dsync s;

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 36L))
	    == NULL) {
		KPutStr("FAIL shellx: no dos.library\n");
		return (1);
	}
	s.s_Task = FindTask(NULL);
	s.s_RC = -1;
	SetSignal(0L, SIGBREAKF_CTRL_F);
	t[0].ti_Tag = NP_Entry;
	t[0].ti_Data = (ULONG)pmain;
	t[1].ti_Tag = NP_ExitCode;
	t[1].ti_Data = (ULONG)pdone;
	t[2].ti_Tag = NP_ExitData;
	t[2].ti_Data = (ULONG)&s;
	t[3].ti_Tag = TAG_DONE;
	if (CreateNewProc(t) == NULL) {
		KPutStr("FAIL shellx: CreateNewProc\n");
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	if (s.s_RC != 0) {
		xKPrintf("FAIL shellx: %s\n", why);
		return (1);
	}
	KPutStr("PASS shellx\n");
	return (0);
}
