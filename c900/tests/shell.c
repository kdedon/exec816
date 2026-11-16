/* SPDX-License-Identifier: MIT */
/*
 * The shell through SystemTagList and Execute: command results, an
 * unknown command, an explicit path, > (into a file) and < redirection,
 * a script that stops at a result over the fail level, Run, then an
 * interactive shell on CON: reading typed lines until EndCLI.  The
 * typed "KPut PASS shel" prints no PASS line; cursor up recalls it from
 * CON:'s history and the typed "l" completes the PASS line.
 */
/* EMUFLAGS: --input-mark=SHELL-GO --input=\iK\iP\iu\it\i\t\iP\iA\iS\iS\i\t\is\ih\ie\il\i\r\i\^[\i[\iA\il\i\r\iE\in\id\iC\iL\iI\i\r */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

static char *why;

#define CHECK(c, w)	if (!(c)) { why = w; return (1); }

static LONG
sys(cmd)
char *cmd;
{
	struct TagItem t[1];

	t[0].ti_Tag = TAG_DONE;
	return (SystemTagList(cmd, t));
}

static int
stest()
{
	struct TagItem t[3];
	BPTR in, out;
	char buf[16];

	CHECK(sys("RetCode 7") == 7, "a command's result");
	CHECK(sys("  C:RetCode 3") == 3, "an explicit path");
	CHECK(sys("NoSuchCommand") == RETURN_ERROR, "an unknown command");
	CHECK(sys("; a comment") == 0, "a comment");
	CHECK(sys("RetCode 0 >NIL: words") == 0, "> redirection");
	CHECK(sys("RetCode 4 <NIL:") == 4, "< redirection");
	CHECK(sys("RetCode 0 >SYS:nodir/x") == RETURN_ERROR,
	    "> to a missing directory");
	CHECK(sys("RetCode 0 >SYS:redir hello") == 0 &&
	    (in = Open("SYS:redir", MODE_OLDFILE)) != NULL, "> to a file");
	CHECK(Read(in, buf, 16L) == 6 && buf[0] == 'h' && buf[5] == '\n',
	    "what > wrote");
	Close(in);
	CHECK(DeleteFile("SYS:redir") == DOSTRUE, "DeleteFile");
	CHECK(sys("Execute S:Fail") == 20, "a script stops at the fail level");
	CHECK(sys("Run RetCode 1") == 0, "Run");
	Delay(10L);

	CHECK((in = Open("CON:", MODE_OLDFILE)) != NULL &&
	    (out = Open("CON:", MODE_NEWFILE)) != NULL, "Open CON:");
	t[0].ti_Tag = SYS_Input;
	t[0].ti_Data = (ULONG)in;
	t[1].ti_Tag = SYS_Output;
	t[1].ti_Data = (ULONG)out;
	t[2].ti_Tag = TAG_DONE;
	CHECK(SystemTagList("RetCode 0 shell: interactive", t) == 0,
	    "SystemTagList with streams");
	FPuts(out, "\nSHELL-GO\n");
	CHECK(Execute("", in, out) == DOSTRUE, "Execute");
	Close(out);
	Close(in);
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
		KPutStr("FAIL shell: no dos.library\n");
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
		KPutStr("FAIL shell: CreateNewProc\n");
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	if (s.s_RC != 0) {
		xKPrintf("FAIL shell: %s\n", why);
		return (1);
	}
	return (0);
}
