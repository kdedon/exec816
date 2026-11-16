/* SPDX-License-Identifier: MIT */
/*
 * The Task crash requester through intuition.library.  LIBS: holds a fake
 * intuition.library (hunks/fakeint.c) that records the system requester
 * calls and DisplayAlert.
 *
 * A held Task's crash is shown by another Task with
 * BuildEasyRequestArgs: AmigaOS 2.0's "Software Failure" text and
 * Suspend|Reboot, on the default screen, then SysReqHandler until it
 * answers, then FreeSysRequest; Suspend leaves the Task held.  When the
 * requester cannot open, or its window has not opened after about 2 s,
 * DisplayAlert shows the crash instead; a window that opens late is
 * closed unanswered.  A crash of input.device goes to DisplayAlert
 * directly.  A crash waits while a recoverable alert is on the display.
 * The test ends with Reboot, the dead-end alert.
 */
/* EXPECT: exec900: alert 80000022 task crash */
#include "test.h"
#include <dos/dos.h>
#include <dos/dosextens.h>
#include "hunks/fakeint.h"
#include "crashtest.h"

struct DosLibrary *DOSBase;

static struct FakeIBase *fi;

static int
same(a, b)
register char *a, *b;
{
	while (*a == *b && *a != '\0')
		a++, b++;
	return (*a == *b);
}

static int
builds(n)
ULONG n;
{
	return (fi->fi_Builds == n);
}

static int
frees(n)
ULONG n;
{
	return (fi->fi_Frees == n);
}

static int
alerts(n)
ULONG n;
{
	return (fi->fi_Calls == n);
}

static int
hung()
{
	return (fi->fi_Hung != NULL);
}

static void
hex8(p, v)
register char *p;
ULONG v;
{
	register int i;

	for (i = 7; i >= 0; i--, v >>= 4)
		p[i] = "0123456789abcdef"[(int)(v & 15)];
}

/* The DisplayAlert text for alert num. */
static char want[] = "\0\46\17Software Failure.   Left mouse button: \
Suspend.   Right: Reboot.\0\377\0\46\36Program failed (error #00000000).  \
Wait for disk activity to finish.\0";

static char *
text(num)
ULONG num;
{
	register UWORD i, n;

	n = sizeof(want);
	hex8(want + n - 47, num);
	if (fi->fi_Len != n)
		return ("alert text length");
	for (i = 0; i < n; i++)
		if (fi->fi_Text[i] != (UBYTE)want[i])
			return ("alert text");
	return (NULL);
}

/* Task name, started at fn, crashes and is held. */
static struct Task *
crash(name, fn)
char *name;
void (*fn)();
{
	register struct Task *t;

	if ((t = CreateTask(name, 0L, fn, CSTACK)) == NULL ||
	    !until(held, (APTR)t, 200L))
		return (NULL);
	return (t);
}

/* The requester, answered Suspend after one pending answer. */
static char *
suspend()
{
	register struct Task *t, *me;

	me = FindTask(NULL);
	fi->fi_Pending = 1;
	if ((t = crash("crashseg", crashseg)) == NULL)
		return ("crashseg not held");
	if (!until(frees, (APTR)1L, 300L))
		return ("no requester");
	if (fi->fi_Builds != 1 || fi->fi_Handles != 2 || fi->fi_Easys != 0)
		return ("requester calls");
	if (fi->fi_BuildTask == t || fi->fi_BuildTask == me)
		return ("requester shown by the crashed Task or root");
	if (fi->fi_BuildStack != 2560L)
		return ("requester process stack");
	if ((me = FindTask("crash")) == NULL ||
	    ((struct Process *)me)->pr_StackSize != 3072L)
		return ("crash process stack");
	if (!same(fi->fi_Title, "Software Failure") ||
	    !same(fi->fi_Format, "Program failed (error #%08lx).\n\
Wait for disk activity to finish.") ||
	    !same(fi->fi_Gadgets, "Suspend|Reboot") ||
	    fi->fi_ESSize != sizeof(struct FIEasy) || fi->fi_ESFlags != 0)
		return ("requester EasyStruct");
	if (fi->fi_BuildWin != NULL || fi->fi_BuildIDCMP != 0 ||
	    fi->fi_BuildArg != 0x80000002L)
		return ("requester arguments");
	if (fi->fi_WrongWin || fi->fi_NoWait)
		return ("SysReqHandler or FreeSysRequest arguments");
	if (fi->fi_Calls != 0)
		return ("DisplayAlert with a requester");
	if (!until(gone, (APTR)"crash requester", 100L))
		return ("requester process did not end");
	Sleep(10L);
	if (!held(t) || ran != 0)
		return ("Suspend did not leave the Task held");
	return (NULL);
}

/* No window: DisplayAlert, answered Suspend. */
static char *
nowindow()
{
	register struct Task *t;
	register char *why;

	fi->fi_BuildMode = FIB_FAIL;
	if ((t = crash("crashsc", crashsc)) == NULL)
		return ("crashsc not held");
	if (!until(alerts, (APTR)1L, 300L))
		return ("no DisplayAlert without a window");
	if (fi->fi_Builds != 2 || fi->fi_Frees != 1 || fi->fi_Num != 0 ||
	    fi->fi_Height != 40)
		return ("DisplayAlert without a window: calls");
	if ((why = text(0x80000021L)) != NULL)
		return (why);
	if (fi->fi_Task == t)
		return ("DisplayAlert called by the crashed Task");
	Sleep(10L);
	if (!held(t))
		return ("Suspend from DisplayAlert did not hold");
	return (NULL);
}

/* A window that does not open: DisplayAlert after about 2 s. */
static char *
timeout()
{
	register struct Task *t;
	ULONG t0, dt;

	fi->fi_BuildMode = FIB_HANG;
	if ((t = crash("crashhang", crashseg)) == NULL)
		return ("crashhang not held");
	t0 = TickCount;
	if (!until(hung, NULL, 100L))
		return ("no requester build");
	if (!until(alerts, (APTR)2L, 400L))
		return ("no DisplayAlert after the timeout");
	dt = TickCount - t0;
	if (dt < 150 || dt > 300)
		return ("timeout not about 2 s");
	if (fi->fi_Builds != 3 || !held(t))
		return ("after the timeout");
	fi->fi_BuildMode = FIB_WINDOW;
	Signal(fi->fi_Hung, SIGBREAKF_CTRL_C);
	if (!until(frees, (APTR)2L, 100L))
		return ("late window not closed");
	if (fi->fi_Handles != 2)
		return ("late window answered");
	if (!until(gone, (APTR)"crash requester", 100L))
		return ("abandoned requester process did not end");
	return (NULL);
}

/* input.device: DisplayAlert only. */
static char *
input()
{
	if (crash("input.device", crashseg) == NULL)
		return ("input.device not held");
	if (!until(alerts, (APTR)3L, 300L))
		return ("input.device's crash not on DisplayAlert");
	if (fi->fi_Builds != 3)
		return ("requester for input.device");
	if (text(0x80000002L) != NULL)
		return ("input.device's alert text");
	return (NULL);
}

static void
alerter()
{
	Alert(0x1031L);
}

static int
aheld()
{
	return ((SetSignal(0L, 0L) & SIGBREAKF_CTRL_F) != 0);
}

/* A crash while a recoverable alert is shown waits its turn. */
static char *
turn()
{
	register struct Task *a;

	SetSignal(0L, SIGBREAKF_CTRL_F);
	fi->fi_Hold = FindTask(NULL);
	if ((a = CreateTask("alerter", 0L, alerter, 2048L)) == NULL)
		return ("CreateTask alerter");
	if (!until(aheld, NULL, 300L))
		return ("alert not on the display");
	if (crash("crashturn", crashseg) == NULL)
		return ("crashturn not held");
	Sleep(50L);
	if (fi->fi_Builds != 3)
		return ("requester shown with an alert");
	Signal(a, SIGBREAKF_CTRL_C);
	if (!until(frees, (APTR)3L, 300L))
		return ("requester not shown after the alert");
	if (fi->fi_Builds != 4 || fi->fi_Overlap)
		return ("requester and alert overlap");
	return (NULL);
}

static char *
run()
{
	register char *why;

	if ((fi = (struct FakeIBase *)OpenLibrary("intuition.library", 0L))
	    == NULL)
		return ("OpenLibrary intuition.library");
	if ((why = suspend()) != NULL || (why = nowindow()) != NULL ||
	    (why = timeout()) != NULL || (why = input()) != NULL ||
	    (why = turn()) != NULL)
		return (why);
	if (ran != 0)
		return ("a crashed Task ran past its trap");
	return (NULL);
}

int
main()
{
	char *why;

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 36L))
	    == NULL) {
		KPutStr("FAIL crashint: no dos.library\n");
		return (1);
	}
	if ((why = run()) != NULL) {
		xKPrintf("FAIL crashint: %s (%u builds, %u handles, %u frees, \
%u easys, %u alerts)\n", why, fi->fi_Builds, fi->fi_Handles, fi->fi_Frees,
		    fi->fi_Easys, fi->fi_Calls);
		return (1);
	}
	KPutStr("crashint: crash requester checked\n");
	fi->fi_Gadget = 0;
	crash("crashreboot", crashsc2);
	Sleep(300L);
	KPutStr("FAIL crashint: Reboot returned\n");
	return (1);
}
