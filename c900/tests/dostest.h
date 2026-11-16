/* SPDX-License-Identifier: MIT */
/*
 * Shared code for DOS tests: the test body runs as a process (DOS calls
 * that need one: IoErr, variables, the current directory), and main
 * reports its result.  A test defines TNAME and int dtest(), which
 * returns 0 or sets why and returns nonzero (CHECK).
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

static char *why;
static int dtest();

#define CHECK(c, w)	if (!(c)) { why = w; return (1); }

static int
same(a, b)
register char *a, *b;
{
	while (*a == *b && *a != '\0')
		a++, b++;
	return (*a == *b);
}

static LONG
dtpmain(args, len)
char *args;
LONG len;
{
	return ((LONG)dtest());
}

struct dtsync {
	struct Task *s_Task;
	LONG s_RC;
};

static void
dtdone(rc, s)
LONG rc;
struct dtsync *s;
{
	s->s_RC = rc;
	Signal(s->s_Task, SIGBREAKF_CTRL_F);
}

int
main()
{
	struct TagItem t[4];
	struct dtsync s;

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 36L))
	    == NULL) {
		xKPrintf("FAIL %s: no dos.library\n", TNAME);
		return (1);
	}
	s.s_Task = FindTask(NULL);
	s.s_RC = -1;
	SetSignal(0L, SIGBREAKF_CTRL_F);
	t[0].ti_Tag = NP_Entry;
	t[0].ti_Data = (ULONG)dtpmain;
	t[1].ti_Tag = NP_ExitCode;
	t[1].ti_Data = (ULONG)dtdone;
	t[2].ti_Tag = NP_ExitData;
	t[2].ti_Data = (ULONG)&s;
	t[3].ti_Tag = TAG_DONE;
	if (CreateNewProc(t) == NULL) {
		xKPrintf("FAIL %s: CreateNewProc\n", TNAME);
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	if (s.s_RC != 0) {
		xKPrintf("FAIL %s: %s\n", TNAME, why);
		return (1);
	}
	xKPrintf("PASS %s\n", TNAME);
	return (0);
}
