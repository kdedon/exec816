/* SPDX-License-Identifier: MIT */
/*
 * A window console handler as AmigaOS has them: the WIN: device process
 * hands each open to a window port by setting fh_Type.  Open of WIN:
 * from a process without a console makes that window port its console,
 * so "*" reaches the window, and NewShell WIN:y opens the window once,
 * its output being "*" on the window, and the new shell's console is
 * that window.  Closing the open that set a console clears it, in
 * whichever process the Close runs; closing another open of the window
 * leaves it, and NewShell's window stays the new shell's only.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

static char *why;
static UWORD hseg[6];
static struct MsgPort *winport;
static int opens, stars, ends, shellcon, lines;
static char cmd[] = "Echo x TO WIN:q\n";

#define CHECK(c, w)	if (!(c)) { why = w; return (1); }

/* A seglist whose first hunk is "jp fn", for code in this program. */
static void
mkseg(s, fn)
register UWORD *s;
APTR fn;
{
	s[0] = s[1] = 0;
	s[2] = 0x5E08;
	s[3] = 0x8000 | (UWORD)((ULONG)fn >> 16 & 0x7F00);
	s[4] = (UWORD)(ULONG)fn;
}

/* One packet to the device port (dev) or the window port. */
static void
pkt(dp, dev)
register struct DosPacket *dp;
int dev;
{
	register struct FileHandle *fh;
	struct Task *t;
	LONG r1;
	char *s, *d;

	r1 = DOSTRUE;
	switch ((int)dp->dp_Type) {
	case ACTION_FINDINPUT:
	case ACTION_FINDOUTPUT:
		fh = (struct FileHandle *)dp->dp_Arg1;
		if (dev) {
			opens++;
			fh->fh_Type = winport;
		} else
			stars++;
		fh->fh_Port = (struct MsgPort *)1L;	/* interactive */
		break;
	case ACTION_READ:
		t = (struct Task *)dp->dp_Port->mp_SigTask;
		if (t->tc_Node.ln_Type == NT_PROCESS &&
		    ((struct Process *)t)->pr_ConsoleTask == (APTR)winport)
			shellcon++;
		r1 = 0;
		if (lines++ == 0)	/* one command line, then the end */
			for (s = cmd, d = (char *)dp->dp_Arg2; *s != '\0'; r1++)
				*d++ = *s++;
		break;
	case ACTION_WRITE:
		r1 = dp->dp_Arg3;
		break;
	case ACTION_END:
		ends++;
		break;
	default:
		r1 = DOSFALSE;
	}
	ReplyPkt(dp, r1, r1 == DOSFALSE ? ERROR_ACTION_NOT_KNOWN : 0L);
}

static LONG
hmain(args, len)
char *args;
LONG len;
{
	register struct DosPacket *dp;
	register struct Process *me;
	struct Message *m;

	me = (struct Process *)FindTask(NULL);
	dp = WaitPkt();
	if (dp->dp_Type != ACTION_STARTUP || (winport = CreateMsgPort()) ==
	    NULL)
		return (1);
	((struct DosList *)dp->dp_Arg3)->dol_Task = &me->pr_MsgPort;
	ReplyPkt(dp, DOSTRUE, 0L);
	for (;;) {
		Wait(1L << me->pr_MsgPort.mp_SigBit |
		    1L << winport->mp_SigBit);
		while ((m = GetMsg(&me->pr_MsgPort)) != NULL)
			pkt((struct DosPacket *)m->mn_Node.ln_Name, 1);
		while ((m = GetMsg(winport)) != NULL)
			pkt((struct DosPacket *)m->mn_Node.ln_Name, 0);
	}
}

static LONG
sys(cmd)
char *cmd;
{
	struct TagItem t[3];

	t[0].ti_Tag = SYS_Input;
	t[0].ti_Data = (ULONG)Open("NIL:", MODE_OLDFILE);
	t[1].ti_Tag = SYS_Output;
	t[1].ti_Data = (ULONG)Open("NIL:", MODE_NEWFILE);
	t[2].ti_Tag = TAG_DONE;
	return (SystemTagList(cmd, t));
}

/* Wait for the window's handles to reach n ends. */
static int
endsat(n)
int n;
{
	int i;

	for (i = 0; i < 100 && ends < n; i++)
		Delay(2L);
	return (ends == n);
}

static int
ctest()
{
	register struct DosList *dl;
	struct TagItem t[4];
	BPTR in, out;
	int n;

	CHECK(GetConsoleTask() == NULL, "a console at the start");
	mkseg(hseg, (APTR)hmain);
	CHECK((dl = MakeDosEntry("WIN", (LONG)DLT_DEVICE)) != NULL,
	    "MakeDosEntry");
	dl->dol_misc.dol_handler.dol_SegList = (BPTR)hseg;
	dl->dol_misc.dol_handler.dol_StackSize = 1024;
	dl->dol_misc.dol_handler.dol_Handler = "win-handler";
	CHECK(AddDosEntry(dl) == DOSTRUE, "AddDosEntry");

	CHECK((in = Open("WIN:x", MODE_OLDFILE)) != NULL && opens == 1 &&
	    ((struct FileHandle *)in)->fh_Type == winport, "Open WIN:");
	CHECK(GetConsoleTask() == winport, "the console is not the window");
	CHECK((out = Open("*", MODE_NEWFILE)) != NULL && stars == 1,
	    "Open * did not reach the window");
	CHECK(Close(out) == DOSTRUE && GetConsoleTask() == winport,
	    "closing * cleared the console");
	CHECK(Close(in) == DOSTRUE && ends == 2, "Close");
	CHECK(GetConsoleTask() == NULL, "Close did not clear the console");
	CHECK(Open("*", MODE_NEWFILE) == NULL && stars == 1,
	    "Open * after Close reached the window");

	/* Output only, the window's handler ending at Close. */
	CHECK((out = Open("WIN:x", MODE_NEWFILE)) != NULL &&
	    GetConsoleTask() == winport, "Open WIN: for output");
	CHECK(Close(out) == DOSTRUE && GetConsoleTask() == NULL,
	    "output Close did not clear the console");

	/* A second open of the window is not the console's. */
	in = Open("WIN:x", MODE_OLDFILE);
	CHECK((out = Open("WIN:w", MODE_NEWFILE)) != NULL && Close(out) ==
	    DOSTRUE && GetConsoleTask() == winport,
	    "closing a duplicate cleared the console");
	CHECK(SetConsoleTask(dl->dol_Task) == winport && Close(in) ==
	    DOSTRUE && GetConsoleTask() == dl->dol_Task,
	    "Close cleared a console set since");
	SetConsoleTask(NULL);

	/* Closed by an asynchronous shell given it as output. */
	n = ends;
	CHECK((out = Open("WIN:a", MODE_NEWFILE)) != NULL, "Open WIN:a");
	t[0].ti_Tag = SYS_Input;
	t[0].ti_Data = (ULONG)Open("NIL:", MODE_OLDFILE);
	t[1].ti_Tag = SYS_Output;
	t[1].ti_Data = (ULONG)out;
	t[2].ti_Tag = SYS_Asynch;
	t[2].ti_Data = DOSTRUE;
	t[3].ti_Tag = TAG_DONE;
	CHECK(SystemTagList("Echo y", t) == 0, "asynchronous SystemTagList");
	CHECK(endsat(n + 1), "the asynchronous shell did not end");
	CHECK(GetConsoleTask() == NULL,
	    "another process's Close did not clear the console");
	opens = stars = ends = 0;

	CHECK(SetConsoleTask(dl->dol_Task) == NULL, "SetConsoleTask");
	CHECK((in = Open("WIN:z", MODE_OLDFILE)) != NULL &&
	    GetConsoleTask() == dl->dol_Task, "a later open took the console");
	Close(in);
	SetConsoleTask(NULL);

	/* The new shell runs Echo TO a second open of its window, then
	 * reads its end: three opens (y, its *, q), three ends. */
	opens = stars = ends = 0;
	CHECK(sys("NewShell WIN:y") == 0, "NewShell");
	CHECK(endsat(3), "the new shell did not end");
	CHECK(opens == 2, "NewShell opened the window twice");
	CHECK(stars == 1, "the new shell's output is not * on the window");
	CHECK(shellcon == 2, "the new shell's console is not the window, "
	    "or Echo TO cleared it");
	return (0);
}

static LONG
pmain(args, len)
char *args;
LONG len;
{
	return ((LONG)ctest());
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
	struct TagItem t[5];
	struct dsync s;

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 36L))
	    == NULL) {
		KPutStr("FAIL conwin: no dos.library\n");
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
	t[3].ti_Tag = NP_Name;
	t[3].ti_Data = (ULONG)"conwin";
	t[4].ti_Tag = TAG_DONE;
	if (CreateNewProc(t) == NULL) {
		KPutStr("FAIL conwin: CreateNewProc\n");
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	if (s.s_RC != 0) {
		xKPrintf("FAIL conwin: %s\n", why);
		return (1);
	}
	KPutStr("PASS conwin\n");
	return (0);
}
