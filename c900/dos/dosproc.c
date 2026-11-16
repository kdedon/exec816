/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * dos.library processes and programs, as AmigaDOS V36: CreateNewProc
 * (and CreateProc), the process's exit, Exit, RunCommand on a stack of
 * its own (exec's StackSwap), LoadSeg and UnLoadSeg on exec's hunk
 * loader, and Execute and SystemTagList, which run the shell
 * (L:Shell-Seg, loaded once) as a CLI process.
 *
 * A program entry is called as LONG entry(char *args, LONG len): the
 * argument string and its length, which AmigaDOS passes in A0 and D0.
 */
#include "dosint.h"

static ULONG dclinums;		/* CLI numbers in use, bit n - 1 for n */

static void dprocent();
BPTR dpathdup();
void dpathfree();

/* The next tag, following TAG_MORE and skipping TAG_IGNORE/TAG_SKIP. */
static struct TagItem *
dnexttag(tp)
struct TagItem **tp;
{
	register struct TagItem *t;

	for (t = *tp; t != NULL; ) {
		switch ((int)t->ti_Tag) {
		case TAG_DONE:
			t = NULL;
			break;
		case TAG_MORE:
			t = (struct TagItem *)t->ti_Data;
			continue;
		case TAG_SKIP:
			t += 1 + (UWORD)t->ti_Data;
			continue;
		case TAG_IGNORE:
			t++;
			continue;
		default:
			*tp = t + 1;
			return (t);
		}
	}
	*tp = NULL;
	return (NULL);
}

static void
dcopy(d, s, n)
register char *d, *s;
register int n;
{
	while (--n > 0 && *s != '\0')
		*d++ = *s++;
	*d = '\0';
}

struct Process *
xCreateNewProc(db, tags)
DB db;
struct TagItem *tags;
{
	register struct DProc *d;
	register struct Process *pr;
	register struct TagItem *t;
	struct Process *me;
	struct CommandLineInterface *cli, *pcli;
	struct MemList *ml;
	struct {
		struct Node fml_Node;
		UWORD fml_NumEntries;
		struct MemEntry fml_ME[2];
	} fml;
	BPTR seg, in, out, dir;
	LONG (*entry)();
	ULONG stack, flags;
	char *name, *args;
	APTR exitcode;
	LONG exitdata, pri;
	int dirset, wantcli, copyvars, conset, n;
	APTR con;
	UBYTE *stk;

	me = dme();
	seg = in = out = NULL;
	entry = NULL;
	stack = DEFSTACK;
	name = "New Process";
	args = NULL;
	exitcode = NULL;
	exitdata = 0;
	pri = 0;
	dirset = wantcli = conset = 0;
	con = NULL;
	copyvars = 1;
	dir = NULL;
	flags = PRF_FREESEGLIST | PRF_CLOSEINPUT | PRF_CLOSEOUTPUT |
	    PRF_FREECURRDIR;
	while ((t = dnexttag(&tags)) != NULL) {
		switch ((int)(t->ti_Tag - NP_Dummy)) {
		case 1:	seg = (BPTR)t->ti_Data; break;
		case 2:	if (!t->ti_Data) flags &= ~PRF_FREESEGLIST; break;
		case 3:	entry = (LONG (*)())t->ti_Data; break;
		case 4:	in = (BPTR)t->ti_Data; break;
		case 5:	out = (BPTR)t->ti_Data; break;
		case 6:	if (!t->ti_Data) flags &= ~PRF_CLOSEINPUT; break;
		case 7:	if (!t->ti_Data) flags &= ~PRF_CLOSEOUTPUT; break;
		case 10: dir = (BPTR)t->ti_Data; dirset = 1; break;
		case 11: stack = t->ti_Data; break;
		case 12: name = (char *)t->ti_Data; break;
		case 13: pri = t->ti_Data; break;
		case 14: con = (APTR)t->ti_Data; conset = 1; break;
		case 17: copyvars = t->ti_Data != 0; break;
		case 18: wantcli = t->ti_Data != 0; break;
		case 21: args = (char *)t->ti_Data; break;
		case 24: exitcode = (APTR)t->ti_Data; break;
		case 25: exitdata = t->ti_Data; break;
		}
	}
	if (entry == NULL && seg == NULL) {
		dseterr(ERROR_REQUIRED_ARG_MISSING);
		return (NULL);
	}
	if (entry != NULL)
		flags &= ~PRF_FREESEGLIST;
	stack = (stack < MINSTACK ? MINSTACK : stack + 3) & ~3L;
	fml.fml_NumEntries = 2;
	fml.fml_ME[0].me_Reqs = MEMF_PUBLIC | MEMF_CLEAR;
	fml.fml_ME[0].me_Length = sizeof(struct DProc);
	fml.fml_ME[1].me_Reqs = MEMF_STACK | MEMF_CLEAR;
	fml.fml_ME[1].me_Length = stack;
	ml = AllocEntry((struct MemList *)&fml);
	if ((ULONG)ml & 0x80000000L) {
		dseterr(ERROR_NO_FREE_STORE);
		return (NULL);
	}
	cli = NULL;
	if (wantcli && (cli = (struct CommandLineInterface *)xAllocDosObj(db,
	    (ULONG)DOS_CLI, NULL)) == NULL) {
		FreeEntry(ml);
		return (NULL);
	}
	d = (struct DProc *)ml->ml_ME[0].me_Addr;
	pr = &d->dp_Proc;
	if (args != NULL) {
		n = dstrlen(args) + 1;
		if ((pr->pr_Arguments = (UBYTE *)AllocVec((ULONG)n, MEMF_PUBLIC))
		    == NULL) {
			xFreeDosObj(db, (ULONG)DOS_CLI, cli);
			FreeEntry(ml);
			dseterr(ERROR_NO_FREE_STORE);
			return (NULL);
		}
		CopyMem(args, pr->pr_Arguments, (ULONG)n);
		flags |= PRF_FREEARGS;
	}
	stk = (UBYTE *)ml->ml_ME[1].me_Addr;
	dcopy(d->dp_Name, name, PNAMELEN);
	pr->pr_Task.tc_Node.ln_Type = NT_PROCESS;
	pr->pr_Task.tc_Node.ln_Pri = pri;
	pr->pr_Task.tc_Node.ln_Name = d->dp_Name;
	pr->pr_Task.tc_SPLower = (APTR)stk;
	pr->pr_Task.tc_SPUpper = (APTR)(stk + (UWORD)stack);
	pr->pr_Task.tc_SPReg = pr->pr_Task.tc_SPUpper;
	NewList(&pr->pr_Task.tc_MemEntry);
	AddHead(&pr->pr_Task.tc_MemEntry, &ml->ml_Node);
	pr->pr_MsgPort.mp_Node.ln_Type = NT_MSGPORT;
	pr->pr_MsgPort.mp_Flags = PA_SIGNAL;
	pr->pr_MsgPort.mp_SigBit = SIGB_DOS;
	pr->pr_MsgPort.mp_SigTask = &pr->pr_Task;
	NewList(&pr->pr_MsgPort.mp_MsgList);
	pr->pr_SegList = seg;
	pr->pr_StackSize = stack;
	pr->pr_StackBase = (BPTR)stk;
	if (!dirset && me != NULL)
		dir = xDupLock(db, me->pr_CurrentDir);
	pr->pr_CurrentDir = dir;
	pr->pr_CIS = in;
	pr->pr_COS = out;
	if (me != NULL) {
		pr->pr_ConsoleTask = me->pr_ConsoleTask;
		pr->pr_FileSystemTask = me->pr_FileSystemTask;
	}
	if (conset)
		pr->pr_ConsoleTask = con;
	if (cli != NULL) {
		pcli = me != NULL ? (struct CommandLineInterface *)me->pr_CLI :
		    NULL;
		if (pcli != NULL) {
			dcopy(cli->cli_SetName, pcli->cli_SetName, CLI_SETNAME);
			dcopy(cli->cli_Prompt, pcli->cli_Prompt, CLI_PROMPT);
			cli->cli_DefaultStack = pcli->cli_DefaultStack;
			cli->cli_CommandDir = dpathdup(pcli->cli_CommandDir);
		}
		cli->cli_StandardInput = cli->cli_CurrentInput = in;
		cli->cli_StandardOutput = cli->cli_CurrentOutput = out;
		cli->cli_Interactive = in != NULL ? xIsInteractive(db, in) :
		    DOSFALSE;
		for (n = 0; n < 31 && (dclinums & 1L << n); n++)
			;
		dclinums |= 1L << n;
		pr->pr_TaskNum = n + 1;
		pr->pr_CLI = (BPTR)cli;
		flags |= PRF_FREECLI;
	}
	pr->pr_Flags = flags;
	pr->pr_ExitCode = (void (*)())exitcode;
	pr->pr_ExitData = exitdata;
	d->dp_Entry = entry != NULL ? entry : (LONG (*)())((char *)seg + 4);
	dvcopy(copyvars ? me : (struct Process *)NULL, pr);
	if (AddTask(&pr->pr_Task, (TASKPROC)dprocent, (TASKPROC)NULL) == NULL) {
		dseterr(ERROR_NO_FREE_STORE);
		return (NULL);
	}
	return (pr);
}

/* amiga's CreateProc: the process's port. */
struct MsgPort *
xCreateProc(db, name, pri, seg, stack)
DB db;
char *name;
LONG pri;
BPTR seg;
LONG stack;
{
	struct TagItem t[5];
	register struct Process *p;

	t[0].ti_Tag = NP_Name;
	t[0].ti_Data = (ULONG)name;
	t[1].ti_Tag = NP_Priority;
	t[1].ti_Data = pri;
	t[2].ti_Tag = NP_Seglist;
	t[2].ti_Data = (ULONG)seg;
	t[3].ti_Tag = NP_StackSize;
	t[3].ti_Data = stack;
	t[4].ti_Tag = TAG_DONE;
	if ((p = xCreateNewProc(db, t)) == NULL)
		return (NULL);
	return (&p->pr_MsgPort);
}

/* A process's end: release what it owns, then return into exec's
 * finalizer, which removes the Task and frees its record and stack. */
static void
dprocend(pr, rc)
register struct Process *pr;
LONG rc;
{
	register LONG f;

	f = pr->pr_Flags;
	if (f & PRF_CLOSEINPUT)
		xClose(DOSBase, pr->pr_CIS);
	if ((f & PRF_CLOSEOUTPUT) && pr->pr_COS != pr->pr_CIS)
		xClose(DOSBase, pr->pr_COS);
	dconlink((struct DProc *)pr, (struct DFH *)NULL);
	if (f & PRF_FREECURRDIR)
		xUnLock(DOSBase, pr->pr_CurrentDir);
	if (f & PRF_FREECLI) {
		dclinums &= ~(1L << (int)(pr->pr_TaskNum - 1));
		dpathfree(((struct CommandLineInterface *)pr->pr_CLI)->
		    cli_CommandDir);
		xFreeDosObj(DOSBase, (ULONG)DOS_CLI, pr->pr_CLI);
	}
	dvfree(pr);
	if (f & PRF_FREEARGS)
		FreeVec(pr->pr_Arguments);
	if (pr->pr_ExitCode != NULL)
		(*pr->pr_ExitCode)(rc, pr->pr_ExitData);
	if (f & PRF_FREESEGLIST)
		xUnLoadSeg(DOSBase, pr->pr_SegList);
}

static void
dprocent()
{
	register struct DProc *d;
	register char *a;
	LONG rc;

	d = (struct DProc *)FindTask(NULL);
	if ((a = (char *)d->dp_Proc.pr_Arguments) == NULL)
		a = "";
	rc = dcall(d->dp_Entry, a, dstrlen(a), &d->dp_Proc.pr_ReturnAddr);
	dprocend(&d->dp_Proc, rc);
}

/* Return rc from the program the process is running: the command
 * RunCommand called, or the process's entry. */
void
xExit(db, rc)
DB db;
LONG rc;
{
	dexitto(&dme()->pr_ReturnAddr, rc);
}

/* Run seg's first hunk in this process on a new MEMF_STACK stack of
 * stack bytes; -1 when the stack cannot be had. */
LONG
xRunCommand(db, seg, stack, args, len)
DB db;
BPTR seg;
LONG stack;
char *args;
LONG len;
{
	register struct Process *me;
	struct StackSwapStruct sss;
	UBYTE *stk, *oargs;
	APTR oret;
	LONG osize, rc;
	APTR entry;

	me = dme();
	stack = (stack < MINSTACK ? MINSTACK : stack + 3) & ~3L;
	if ((stk = (UBYTE *)AllocMem((ULONG)stack, MEMF_STACK | MEMF_CLEAR))
	    == NULL) {
		dseterr(ERROR_NO_FREE_STORE);
		return (-1);
	}
	sss.stk_Lower = (APTR)stk;
	sss.stk_Upper = (ULONG)(stk + (UWORD)stack);
	sss.stk_Pointer = (APTR)(stk + (UWORD)stack);
	oargs = me->pr_Arguments;
	oret = me->pr_ReturnAddr;
	osize = me->pr_StackSize;
	me->pr_Arguments = (UBYTE *)args;
	me->pr_StackSize = stack;
	entry = (APTR)((char *)seg + 4);
	StackSwap(&sss);
	rc = dcall(entry, args, len, &me->pr_ReturnAddr);
	StackSwap(&sss);
	me->pr_Arguments = oargs;
	me->pr_ReturnAddr = oret;
	me->pr_StackSize = osize;
	FreeMem(stk, (ULONG)stack);
	return (rc);
}

/* LoadSeg reads through a 512-byte buffer, which a read of at least its
 * size bypasses once the buffer is empty. */
struct dlsbuf {
	BPTR b_File;
	UWORD b_Pos, b_End;
	UBYTE b_Buf[512];
};

static LONG
dlsread(b, buf, len)
register struct dlsbuf *b;
char *buf;
LONG len;
{
	register UWORD k;
	LONG n, r;

	for (n = 0; len > 0; len -= k, n += k, buf += k) {
		if (b->b_Pos == b->b_End) {
			if (len >= (LONG)sizeof(b->b_Buf)) {
				r = xRead(DOSBase, b->b_File, buf, len);
				return (r < 0 ? -1L : n + r);
			}
			r = xRead(DOSBase, b->b_File, b->b_Buf,
			    (LONG)sizeof(b->b_Buf));
			if (r <= 0)
				return (r < 0 ? -1L : n);
			b->b_Pos = 0;
			b->b_End = r;
		}
		k = b->b_End - b->b_Pos;
		if (k > len)
			k = len;
		CopyMem(b->b_Buf + b->b_Pos, buf, (ULONG)k);
		b->b_Pos += k;
	}
	return (n);
}

BPTR
xDInLoadSeg(db, fh, table, funcs, stack)
DB db;
BPTR fh, table;
LONG *funcs, *stack;
{
	return ((BPTR)InternalLoadSeg(fh, table, funcs, stack));
}

LONG
xDInUnLoadSeg(db, seg, freefn)
DB db;
BPTR seg;
APTR freefn;
{
	return (InternalUnLoadSeg(seg, freefn));
}

BPTR
xLoadSeg(db, name)
DB db;
char *name;
{
	register struct dlsbuf *b;
	LONG funcs[3];
	BPTR seg;

	seg = NULL;
	if ((b = (struct dlsbuf *)AllocMem((ULONG)sizeof(*b), MEMF_PUBLIC))
	    == NULL) {
		dseterr(ERROR_NO_FREE_STORE);
		return (NULL);
	}
	if ((b->b_File = xOpen(db, name, MODE_OLDFILE)) != NULL) {
		b->b_Pos = b->b_End = 0;
		funcs[0] = (LONG)dlsread;
		funcs[1] = (LONG)((char *)SysBase + LVOAllocMem);
		funcs[2] = (LONG)((char *)SysBase + LVOFreeMem);
		seg = InternalLoadSeg((APTR)b, NULL, funcs, NULL);
		xClose(db, b->b_File);
		if (seg == NULL)
			dseterr(ERROR_BAD_HUNK);
	}
	FreeMem(b, (ULONG)sizeof(*b));
	return (seg);
}

void
xUnLoadSeg(db, seg)
DB db;
BPTR seg;
{
	if (seg != NULL)
		InternalUnLoadSeg(seg, (APTR)((char *)SysBase + LVOFreeMem));
}

/* The shell's seglist, loaded on first use and kept. */
static BPTR
dshellseg()
{
	register struct RootNode *rn;
	register struct DosInfo *di;

	rn = DOSBase->dl_Root;
	if (rn->rn_ShellSegment == NULL) {
		di = ddi();
		ObtainSemaphore(&di->di_EntryLock);
		if (rn->rn_ShellSegment == NULL)
			rn->rn_ShellSegment = xLoadSeg(DOSBase, SHELLFILE);
		ReleaseSemaphore(&di->di_EntryLock);
	}
	return (rn->rn_ShellSegment);
}

struct dsync {
	struct Task *s_Task;
	ULONG s_Sig;
	LONG s_RC;
};

static void
dsyncend(rc, s)
LONG rc;
register struct dsync *s;
{
	s->s_RC = rc;
	Signal(s->s_Task, s->s_Sig);
}

/*
 * Run the shell on the command line cmd.  Its commands read in and
 * write out; with more set, the shell then reads further commands from
 * in, as Execute does.  Synchronous (sync set): wait and return the
 * shell's result in *rc; the handles stay the caller's.  Otherwise the
 * shell closes them when it ends.  DOSFALSE when it cannot start.
 */
static LONG
drunshell(cmd, in, out, more, sync, rc)
char *cmd;
BPTR in, out;
int more, sync;
LONG *rc;
{
	struct TagItem t[13];
	register struct TagItem *tp;
	struct CommandLineInterface *cli;
	register struct Process *p;
	struct dsync s;
	BPTR seg;
	int sig;

	if ((seg = dshellseg()) == NULL)
		return (DOSFALSE);
	tp = t;
	tp->ti_Tag = NP_Seglist; (tp++)->ti_Data = (ULONG)seg;
	tp->ti_Tag = NP_FreeSeglist; (tp++)->ti_Data = 0;
	tp->ti_Tag = NP_Cli; (tp++)->ti_Data = 1;
	tp->ti_Tag = NP_Input; (tp++)->ti_Data = (ULONG)in;
	tp->ti_Tag = NP_Output; (tp++)->ti_Data = (ULONG)out;
	tp->ti_Tag = NP_CloseInput; (tp++)->ti_Data = !sync;
	tp->ti_Tag = NP_CloseOutput; (tp++)->ti_Data = !sync;
	tp->ti_Tag = NP_Arguments; (tp++)->ti_Data = (ULONG)cmd;
	tp->ti_Tag = NP_StackSize; (tp++)->ti_Data = SHELLSTACK;
	tp->ti_Tag = NP_Name; (tp++)->ti_Data = (ULONG)"Shell Process";
	sig = -1;
	if (sync) {
		if ((sig = AllocSignal(-1)) < 0)
			return (dfail(ERROR_NO_FREE_STORE));
		s.s_Task = FindTask(NULL);
		s.s_Sig = 1L << sig;
		tp->ti_Tag = NP_ExitCode; (tp++)->ti_Data = (ULONG)dsyncend;
		tp->ti_Tag = NP_ExitData; (tp++)->ti_Data = (ULONG)&s;
	}
	tp->ti_Tag = TAG_DONE;
	Forbid();
	if ((p = xCreateNewProc(DOSBase, t)) != NULL && !more) {
		cli = (struct CommandLineInterface *)p->pr_CLI;
		cli->cli_CurrentInput = NULL;
		cli->cli_Background = DOSTRUE;
	}
	Permit();
	if (p != NULL && sync) {
		Wait(s.s_Sig);
		*rc = s.s_RC;
	}
	if (sig >= 0)
		FreeSignal(sig);
	return (p != NULL ? DOSTRUE : DOSFALSE);
}

/* Run string, then the commands in input (if any), synchronously. */
LONG
xExecute(db, string, in, out)
DB db;
char *string;
BPTR in, out;
{
	LONG rc;

	if (out == NULL)
		out = xOutput(db);
	return (drunshell(string, in, out, in != NULL, 1, &rc));
}

/* The command's result, or -1 when the shell cannot start. */
LONG
xSystemTagList(db, cmd, tags)
DB db;
char *cmd;
struct TagItem *tags;
{
	register struct TagItem *t;
	BPTR in, out;
	int async, gin, gout;
	LONG rc;

	in = out = NULL;
	async = gin = gout = 0;
	while ((t = dnexttag(&tags)) != NULL)
		if (t->ti_Tag == SYS_Input) {
			in = (BPTR)t->ti_Data;
			gin = 1;
		} else if (t->ti_Tag == SYS_Output) {
			out = (BPTR)t->ti_Data;
			gout = 1;
		} else if (t->ti_Tag == SYS_Asynch)
			async = t->ti_Data != 0;
	if (!gin)
		in = async ? xOpen(db, "NIL:", MODE_OLDFILE) : xInput(db);
	if (!gout)
		out = async ? xOpen(db, "*", MODE_NEWFILE) : xOutput(db);
	rc = 0;
	if (drunshell(cmd, in, out, 0, !async, &rc) == DOSFALSE)
		return (-1);
	return (rc);
}
