/* SPDX-License-Identifier: MIT */
/*
 * dos.library's core without a filesystem: CreateNewProc, IoErr, a
 * handler started from a DosList device's seglist by ACTION_STARTUP,
 * Open/Read/Write/Seek/Close and Lock through its packets, an assign
 * to its lock, NIL:, DeviceProc of an unknown device, and RunCommand on
 * a stack of its own (StackSwap) with Exit from a nested call.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

static char *why;
static int startups, ends;
static UWORD hseg[6], cseg[6], xseg[6];
static struct FileLock hlock;
static char hname[16], wbuf[8];
static LONG cmdarglen, cmdstk;

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

static void
scopy(d, s)
register char *d, *s;
{
	while ((*d++ = *s++) != '\0')
		;
}

static int
seq(a, b)
register char *a, *b;
{
	while (*a == *b && *a != '\0')
		a++, b++;
	return (*a == *b);
}

/* The FAKE: handler: files hold "hello", locks are hlock. */
static LONG
hmain(args, len)
char *args;
LONG len;
{
	register struct DosPacket *dp;
	register struct Process *me;
	struct DosList *dl;
	LONG r1, r2;

	me = (struct Process *)FindTask(NULL);
	dp = WaitPkt();
	if (dp->dp_Type != ACTION_STARTUP)
		return (1);
	dl = (struct DosList *)dp->dp_Arg3;
	dl->dol_Task = &me->pr_MsgPort;
	hlock.fl_Task = &me->pr_MsgPort;
	startups++;
	ReplyPkt(dp, DOSTRUE, 0L);
	for (;;) {
		dp = WaitPkt();
		r1 = DOSTRUE;
		r2 = 0;
		switch ((int)dp->dp_Type) {
		case ACTION_FINDINPUT:
			scopy(hname, (char *)dp->dp_Arg3);
			if (seq(hname, "missing")) {
				r1 = DOSFALSE;
				r2 = ERROR_OBJECT_NOT_FOUND;
			} else
				((struct FileHandle *)dp->dp_Arg1)->fh_Args = 77;
			break;
		case ACTION_READ:
			r1 = dp->dp_Arg1 == 77 ? 5 : -1;
			CopyMem("hello", (APTR)dp->dp_Arg2, 5L);
			break;
		case ACTION_WRITE:
			CopyMem((APTR)dp->dp_Arg2, wbuf, (ULONG)dp->dp_Arg3);
			r1 = dp->dp_Arg3;
			break;
		case ACTION_SEEK:
			r1 = 3;
			break;
		case ACTION_END:
			ends++;
			break;
		case ACTION_LOCATE_OBJECT:
			scopy(hname, (char *)dp->dp_Arg2);
			r1 = (LONG)&hlock;
			r2 = dp->dp_Arg1;	/* the lock it was relative to */
			break;
		case ACTION_FREE_LOCK:
			break;
		case ACTION_DIE:
			ReplyPkt(dp, DOSTRUE, 0L);
			return (0);
		default:
			r1 = DOSFALSE;
			r2 = ERROR_ACTION_NOT_KNOWN;
		}
		ReplyPkt(dp, r1, r2);
	}
}

static int
depth(n)
int n;
{
	if (n == 0)
		Exit(42L);
	return (depth(n - 1) + 1);
}

/* A command: checks its stack is RunCommand's, then Exits deep down. */
static LONG
cmain(args, len)
char *args;
LONG len;
{
	register struct Task *t;
	char here;

	t = FindTask(NULL);
	cmdarglen = len;
	if (&here >= (char *)t->tc_SPLower && &here < (char *)t->tc_SPUpper &&
	    (char *)t->tc_SPUpper - (char *)t->tc_SPLower == 768 &&
	    *(UWORD *)t->tc_SPLower == 0xC900)
		cmdstk = 1;
	depth(5);
	return (1);
}

static LONG
xmain(args, len)
char *args;
LONG len;
{
	return (len == 2 && args[0] == 'o' ? 7L : 8L);
}

static int
ptest()
{
	register struct Process *me;
	register struct DosList *dl;
	BPTR fh, lk, lk2;
	char buf[8];
	struct Task *t;
	APTR lo, hi;

	me = (struct Process *)FindTask(NULL);
	CHECK(me->pr_Task.tc_Node.ln_Type == NT_PROCESS, "not a process");
	CHECK(Cli() == NULL, "Cli without NP_Cli");
	SetIoErr(123L);
	CHECK(IoErr() == 123, "IoErr");
	CHECK(Input() == NULL && Output() == NULL, "default streams");

	CHECK(DeviceProc("NOPE:") == NULL &&
	    IoErr() == ERROR_DEVICE_NOT_MOUNTED, "unknown device");

	mkseg(hseg, (APTR)hmain);
	CHECK((dl = MakeDosEntry("FAKE", (LONG)DLT_DEVICE)) != NULL,
	    "MakeDosEntry");
	dl->dol_misc.dol_handler.dol_SegList = (BPTR)hseg;
	dl->dol_misc.dol_handler.dol_StackSize = 1024;
	dl->dol_misc.dol_handler.dol_Handler = "fake-handler";
	CHECK(AddDosEntry(dl) == DOSTRUE, "AddDosEntry");
	CHECK(AddDosEntry(dl) == DOSFALSE && IoErr() == ERROR_OBJECT_EXISTS,
	    "AddDosEntry twice");

	CHECK((fh = Open("fake:xyz", MODE_OLDFILE)) != NULL, "Open");
	CHECK(startups == 1 && dl->dol_Task != NULL, "handler start");
	CHECK(seq(hname, "xyz"), "name after the device");
	CHECK(Read(fh, buf, 8L) == 5 && buf[0] == 'h' && buf[4] == 'o', "Read");
	CHECK(Write(fh, "ab", 2L) == 2 && wbuf[1] == 'b', "Write");
	CHECK(Seek(fh, 0L, OFFSET_BEGINNING) == 3, "Seek");
	CHECK(IsInteractive(fh) == DOSFALSE, "IsInteractive");
	CHECK(Close(fh) == DOSTRUE && ends == 1, "Close");
	CHECK(Open("FAKE:missing", MODE_OLDFILE) == NULL &&
	    IoErr() == ERROR_OBJECT_NOT_FOUND, "Open failure");
	CHECK(startups == 1, "handler started twice");

	CHECK((lk = Lock("FAKE:dir", SHARED_LOCK)) == (BPTR)&hlock &&
	    IoErr() == 0, "Lock");
	CHECK(AssignLock("TST", lk) == DOSTRUE, "AssignLock");
	CHECK((lk2 = Lock("tst:sub/x", SHARED_LOCK)) == (BPTR)&hlock &&
	    IoErr() == (LONG)&hlock && seq(hname, "sub/x"),
	    "Lock through an assign");
	CHECK(DeviceProc("TST:") == hlock.fl_Task &&
	    IoErr() == (LONG)&hlock, "DeviceProc of an assign");
	CHECK(AssignLock("FAKE", lk) == DOSFALSE, "assign over a device");
	CHECK(AssignLock("TST", NULL) == DOSTRUE &&
	    DeviceProc("TST:") == NULL, "assign removal");
	UnLock(lk2);

	CHECK((fh = Open("NIL:", MODE_NEWFILE)) != NULL, "NIL:");
	CHECK(Read(fh, buf, 8L) == 0 && Write(fh, "x", 1L) == 1, "NIL: I/O");
	Close(fh);

	t = &me->pr_Task;
	lo = t->tc_SPLower;
	hi = t->tc_SPUpper;
	/* StackSwap (platform/trap.s) has these offsets built in. */
	CHECK((char *)&t->tc_SPLower - (char *)t == 46 &&
	    (char *)&t->tc_SPUpper - (char *)t == 50, "tc_SPLower offset");
	mkseg(cseg, (APTR)cmain);
	CHECK(RunCommand((BPTR)cseg, 768L, "a b\n", 4L) == 42 &&
	    cmdarglen == 4 && cmdstk, "RunCommand and Exit");
	CHECK(t->tc_SPLower == lo && t->tc_SPUpper == hi, "stack restored");
	mkseg(xseg, (APTR)xmain);
	CHECK(RunCommand((BPTR)xseg, 600L, "ok", 2L) == 7, "RunCommand result");

	CHECK(DoPkt(dl->dol_Task, (LONG)ACTION_DIE, 0L, 0L, 0L, 0L, 0L) ==
	    DOSTRUE, "ACTION_DIE");
	return (0);
}

static LONG
pmain(args, len)
char *args;
LONG len;
{
	return ((LONG)ptest());
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
	struct Process *p;

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 36L))
	    == NULL) {
		KPutStr("FAIL dosproc: no dos.library\n");
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
	t[3].ti_Data = (ULONG)"dosproc";
	t[4].ti_Tag = TAG_DONE;
	if ((p = CreateNewProc(t)) == NULL) {
		KPutStr("FAIL dosproc: CreateNewProc\n");
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	if (s.s_RC != 0) {
		xKPrintf("FAIL dosproc: %s\n", why);
		return (1);
	}
	KPutStr("PASS dosproc\n");
	return (0);
}
