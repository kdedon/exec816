/* SPDX-License-Identifier: MIT */
/*
 * DOSBoot, the root program that starts DOS as AmigaOS boots: a process
 * opens CON: and Executes S:Startup-Sequence, then reads commands from
 * CON: until EndCLI.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

static LONG
boot(args, len)
char *args;
LONG len;
{
	BPTR in, out;
	LONG r;

	if ((in = Open("CON:", MODE_OLDFILE)) == NULL ||
	    (out = Open("CON:", MODE_NEWFILE)) == NULL)
		return (RETURN_FAIL);
	r = Execute("Execute S:Startup-Sequence", in, out);
	Close(out);
	Close(in);
	return (r == DOSTRUE ? RETURN_OK : RETURN_FAIL);
}

struct dsync {
	struct Task *s_Task;
	LONG s_RC;
};

static void
done(rc, s)
LONG rc;
struct dsync *s;
{
	s->s_RC = rc;
	Signal(s->s_Task, SIGBREAKF_CTRL_F);
}

int
main()
{
	struct TagItem t[5];
	struct dsync s;

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 36L))
	    == NULL) {
		KPutStr("DOSBoot: no dos.library\n");
		return (1);
	}
	s.s_Task = FindTask(NULL);
	s.s_RC = -1;
	SetSignal(0L, SIGBREAKF_CTRL_F);
	t[0].ti_Tag = NP_Entry;
	t[0].ti_Data = (ULONG)boot;
	t[1].ti_Tag = NP_Name;
	t[1].ti_Data = (ULONG)"Boot";
	t[2].ti_Tag = NP_ExitCode;
	t[2].ti_Data = (ULONG)done;
	t[3].ti_Tag = NP_ExitData;
	t[3].ti_Data = (ULONG)&s;
	t[4].ti_Tag = TAG_DONE;
	if (CreateNewProc(t) == NULL) {
		KPutStr("DOSBoot: CreateNewProc failed\n");
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	return (s.s_RC == RETURN_OK ? 0 : 1);
}
