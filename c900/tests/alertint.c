/* SPDX-License-Identifier: MIT */
/*
 * Recoverable alerts through intuition.library (dos.library's Alert
 * patch).  LIBS: holds a fake intuition.library (hunks/fakeint.c) that
 * records DisplayAlert and TimedDisplayAlert calls.
 *
 * With no intuition.library loaded an alert is console text and does
 * not load it.  Once it is loaded, a recoverable alert from a Task
 * outside Forbid and Disable calls DisplayAlert once with Amiga Exec's
 * arguments (0, the alert text, 40), whatever it answers, and so does
 * the kernel's AN_BadLVO from an unimplemented vector.  Alerts under
 * Forbid, under Disable and from an interrupt server do not;
 * TimedDisplayAlert is never called.  Two Tasks' alerts at once are
 * both shown, one after the other.  The test ends in a dead-end alert,
 * which stops the system before intuition.
 */
/* EXPECT: exec900: alert 80001006 task root */
#include "test.h"
#include <exec/alerts.h>
#include <dos/dos.h>
#include "hunks/fakeint.h"

struct DosLibrary *DOSBase;

#define ASTACK		2048L

static struct FakeIBase *fi;
static struct Interrupt alertint;
static volatile int alerted;
static volatile int alertsdone;

static int
same(a, b)
register char *a, *b;
{
	while (*a == *b && *a != '\0')
		a++, b++;
	return (*a == *b);
}

static int
nlib(name)
char *name;
{
	register struct Node *n;
	register int k;

	k = 0;
	Forbid();
	for (n = SysBase->LibList.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (same(n->ln_Name, name))
			k++;
	Permit();
	return (k);
}

/* One recoverable alert from the tick interrupt. */
static int
alertsrv(data, n)
APTR data;
UWORD n;
{
	if (!alerted) {
		Alert(0x1004L);
		alerted = 1;
	}
	return (0);
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

/* Amiga Exec's text for alert num from Task t. */
static char want[] = "\0\46\17Recoverable Alert. \0\377\0\352\17\
Press left mouse button to continue.\0\377\0\216\36\
Guru Meditation #00000000.00000000\0";

static char *
text(num, t)
ULONG num;
struct Task *t;
{
	register UWORD i, n;

	n = sizeof(want);
	hex8(want + n - 19, num);
	hex8(want + n - 10, (ULONG)t);
	if (fi->fi_Len != n)
		return ("alert text length");
	for (i = 0; i < n; i++)
		if (fi->fi_Text[i] != (UBYTE)want[i])
			return ("alert text");
	return (NULL);
}

/* Two Tasks' recoverable alerts. */
static void
alerta()
{
	Alert(0x1021L);
	alertsdone++;
}

static void
alertb()
{
	Alert(0x1022L);
	alertsdone++;
}

/* Until *p is nonzero, or 300 ticks: whether it is. */
static int
until(p)
int (*p)();
{
	ULONG t0;

	for (t0 = TickCount; !(*p)() && TickCount - t0 < 300; )
		Sleep(1);
	return ((*p)());
}

static struct Task *ta, *tb;
static UWORD calls0;

static int
aheld()
{
	return ((SetSignal(0L, 0L) & SIGBREAKF_CTRL_F) != 0);
}

/* tb's alert reached exec and tb waits for its turn. */
static int
bwaits()
{
	return (SysBase->LastAlert[0] == 0x1022L && tb->tc_State == TS_WAIT);
}

static int
bothdone()
{
	return (alertsdone == 2);
}

/* While ta's alert is on the display, tb's waits, then is shown. */
static char *
queued()
{
	register char *why;

	calls0 = fi->fi_Calls;
	SetSignal(0L, SIGBREAKF_CTRL_F);
	fi->fi_Hold = FindTask(NULL);
	if ((ta = CreateTask("alerta", 0L, alerta, ASTACK)) == NULL)
		return ("CreateTask alerta");
	if (!until(aheld))
		return ("first alert not on the display");
	if ((tb = CreateTask("alertb", 0L, alertb, ASTACK)) == NULL)
		return ("CreateTask alertb");
	if (!until(bwaits))
		return ("second alert did not wait");
	if (fi->fi_Calls != calls0 + 1 || alertsdone != 0)
		return ("second alert shown while the first was");
	Signal(ta, SIGBREAKF_CTRL_C);
	if (!until(bothdone))
		return ("queued alerts did not finish");
	if (fi->fi_Calls != calls0 + 2 || fi->fi_Overlap)
		return ("queued alerts not shown one after the other");
	if (fi->fi_Tasks[calls0] != ta || fi->fi_Tasks[calls0 + 1] != tb)
		return ("queued alerts shown out of turn");
	if ((why = text(0x1022L, tb)) != NULL)
		return (why);
	return (NULL);
}

static char *
run()
{
	struct Task *me;
	ULONG t0;
	char *why;

	me = FindTask(NULL);
	/* No intuition: console only, and nothing is loaded. */
	Alert(0x1001L);
	if (SysBase->LastAlert[0] != 0x1001L ||
	    SysBase->LastAlert[1] != (ULONG)me)
		return ("LastAlert without intuition");
	if (nlib("intuition.library") != 0)
		return ("the alert loaded intuition.library");

	if ((fi = (struct FakeIBase *)OpenLibrary("intuition.library", 0L))
	    == NULL)
		return ("OpenLibrary intuition.library");
	if (fi->fi_Calls != 0 || fi->fi_Lib.lib_OpenCnt != 1)
		return ("fake intuition's initial state");

	/* A Task's recoverable alert, with either answer. */
	Alert(0x1002L);
	if (fi->fi_Calls != 1)
		return ("recoverable alert did not reach DisplayAlert");
	if (fi->fi_Num != 0 || fi->fi_Height != 40 || fi->fi_Task != me)
		return ("DisplayAlert arguments");
	if (fi->fi_TD >= 0 || fi->fi_ID >= 0)
		return ("DisplayAlert called in Forbid or Disable");
	if (fi->fi_Opens != 2 || fi->fi_Lib.lib_OpenCnt != 1)
		return ("intuition not opened and closed around the call");
	if ((why = text(0x1002L, me)) != NULL)
		return (why);
	if (SysBase->LastAlert[0] != 0x1002L)
		return ("LastAlert with intuition");
	fi->fi_Answer = FALSE;
	Alert(0x01001003L);
	if (fi->fi_Calls != 2 || (why = text(0x01001003L, me)) != NULL)
		return ("second alert");

	/* Forbid, Disable, an interrupt: console only. */
	Forbid();
	Alert(0x1011L);
	Permit();
	Disable();
	Alert(0x1012L);
	Enable();
	if (fi->fi_Calls != 2 || SysBase->LastAlert[0] != 0x1012L)
		return ("alert in Forbid or Disable reached DisplayAlert");
	alertint.is_Node.ln_Name = "alertint";
	alertint.is_Code = alertsrv;
	AddIntServer(INTV_TICK, &alertint);
	for (t0 = TickCount; !alerted && TickCount - t0 < 300; )
		Sleep(1);
	RemIntServer(INTV_TICK, &alertint);
	if (!alerted || SysBase->LastAlert[0] != 0x1004L)
		return ("no alert from the interrupt");
	if (fi->fi_Calls != 2)
		return ("alert from an interrupt reached DisplayAlert");

	/* An unimplemented vector, through the Alert vector. */
	(*(ULONG (*)())((char *)SysBase + LVODebug))(0L);
	if (SysBase->LastAlert[0] != (AN_BadLVO | -LVODebug))
		return ("no AN_BadLVO");
	if (fi->fi_Calls != 3)
		return ("AN_BadLVO did not reach DisplayAlert");
	if ((why = text(AN_BadLVO | -LVODebug, me)) != NULL)
		return (why);

	if ((why = queued()) != NULL)
		return (why);
	if (fi->fi_Timed != 0)
		return ("TimedDisplayAlert reached intuition");
	return (NULL);
}

int
main()
{
	char *why;

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 36L))
	    == NULL) {
		KPutStr("FAIL alertint: no dos.library\n");
		return (1);
	}
	if ((why = run()) != NULL) {
		xKPrintf("FAIL alertint: %s\n", why);
		return (1);
	}
	KPutStr("alertint: recoverable alerts checked\n");
	fi->fi_Refuse = TRUE;
	Alert(AT_DeadEnd | 0x1006L);
	KPutStr("FAIL alertint: dead-end alert returned\n");
	return (1);
}
