/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Task admission and removal, the scheduler, Forbid/Permit checks, Disable/Enable,
 * Yield, Sleep and the fatal-error paths.  Port of lib/exec/taskpolicy.act
 * and platform/altirraos/tasks.s; docs/internals.md records the departures.
 *
 * Queue changes happen with VI off.  kswitch runs with VI off from the
 * trap stubs; ktick runs inside the tick server (kernel/interrupts.c).
 */
#include "kernel.h"

void kinitidle();
void kinitroot();

#define FRAMESIZE	36
#define FRAMEWORDS	(FRAMESIZE / 2)
#define F_FCW		15
#define F_PCSEG		16
#define F_PCOFF		17

/* The word AddTask writes at tc_SPLower; kswitch checks it. */
#define STKGUARD	0xC900

/* StackSwap (trap.s) writes STKGUARD and uses tc_SPLower's and
 * tc_SPUpper's offsets, 46 and 50; tests/dosproc.c checks them. */

struct Task *ThisTask;
struct Task IdleTask;
struct Task RootTask;
UWORD SysFlags;
UWORD IntDepth;
WORD Quantum = QUANTUM;
UWORD Sleepers;

static char idlename[] = "idle";
static char rootname[] = "root";

/* The Task's stack pointer register value as an offset in STACKSEG. */
#define SPOFF(t)	((UWORD)POFF((t)->tc_SPReg))
#define STKPTR(o)	MKPTR(SEGBASE(STACKSEG), (o))

void
kpanic(msg)
char *msg;
{
	intoff();
	kvidstop();
	xKPrintf("exec900: panic: %s\n", msg);
	emustop();
	for (;;)
		haltcpu();
}

/* KPrintf's Permit must not switch inside kswitch or a trap: IntDepth
 * holds it off. */
void
xAlert(num)
ULONG num;
{
	register struct Task *t;

	t = ThisTask;
	LastAlert[0] = num;
	LastAlert[1] = (ULONG)t;
	IntDepth++;
	if (num & AT_DeadEnd) {
		intoff();
		kvidstop();
	}
	xKPrintf("exec900: alert %08lx task %s\n", num,
	    t != NULL ? t->tc_Node.ln_Name : NULL);
	IntDepth--;
	if (num & AT_DeadEnd) {
		emustop();
		for (;;)
			haltcpu();
	}
}

/* The default tc_TrapCode: a dead-end alert. */
void
ktrapdef(num, fp)
ULONG num;
register struct TrapFrame *fp;
{
	IntDepth++;
	intoff();
	kvidstop();
	xKPrintf("exec900: trap %lu pc %x:%x fcw %x\n", num,
	    (fp->tf_PCSeg >> 8) & 0x7F, fp->tf_PC, fp->tf_FCW);
	xAlert(num | AT_DeadEnd);
}

/* trap.s kinds 1-5: EPA, privileged, segment, NMI, SC n != 0. */
static UBYTE trapnum[] = {
	0, TRAP_EPA, TRAP_PRIV, TRAP_SEGMENT, TRAP_NMI, TRAP_SC
};

/* A trap: the current Task's tc_TrapCode, which returns to resume the
 * frame at off.  NMI is not a Task trap. */
UWORD
kfault(off, kind)
UWORD off, kind;
{
	register struct TrapFrame *fp;
	register struct Task *me;
	ULONG n;

	fp = (struct TrapFrame *)STKPTR(off);
	if (kind == 1) {
		if (kepurec != NULL) {
			fp->tf_PCSeg = 0x8000 | (UWORD)((ULONG)kepurec >> 16);
			fp->tf_PC = (UWORD)(ULONG)kepurec;
			return (off);
		}
		if (kepuswitch(fp))
			return (off);
	}
	n = trapnum[kind];
	if (n == TRAP_SC)
		n += fp->tf_Id & 0xFF;
	me = ThisTask;
	if (me == NULL || n == TRAP_NMI)
		ktrapdef(n, fp);
	(*(void (*)())me->tc_TrapCode)(n, fp);
	return (off);
}

/*
 * Amiga's stack probe: the saved SP within tc_SPLower..tc_SPUpper and, at
 * a switch, the guard word intact.  A broken guard is rewritten.  The
 * alert is not repeated while it is the last one and names this Task: at
 * 9600 baud it outlasts a tick, so a tick alerting on every tick would
 * never let the Task run.  The pool's MMU guard page catches only the
 * lowest stack; this covers every Task.
 */
static void
kstkchk(t, off, guard)
register struct Task *t;
UWORD off;
int guard;
{
	register UWORD *g;

	g = (UWORD *)t->tc_SPLower;
	if (off >= (UWORD)POFF(g) && off <= (UWORD)POFF(t->tc_SPUpper)) {
		if (!guard || *g == STKGUARD)
			return;
		*g = STKGUARD;
	}
	if (LastAlert[0] != AN_StackProbe || LastAlert[1] != (ULONG)t)
		xAlert(AN_StackProbe);
}

/* Make t ready.  VI must be off and t must be on no list. */
void
kready(t)
register struct Task *t;
{
	t->tc_State = TS_READY;
	xEnqueue(&TaskReady, &t->tc_Node);
	if (ThisTask == &IdleTask ||
	    t->tc_Node.ln_Pri > ThisTask->tc_Node.ln_Pri)
		SysFlags |= SF_SAR;
}

/* Switch now if one is pending and the current Task allows it. */
void
kmaybe()
{
	register struct Task *me;
	int s;

	if (SysFlags & SF_CONOUT)
		kvidflush();
	s = intoff();
	me = ThisTask;
	if ((SysFlags & SF_SAR) && IntDepth == 0 &&
	    me->tc_TDNestCnt < 0 && me->tc_IDNestCnt < 0)
		kresched();
	intrest(s);
}

UWORD
kswitch(off)
UWORD off;
{
	register struct Task *old, *t;

	old = ThisTask;
	old->tc_SPReg = STKPTR(off);
	if (old->tc_State != TS_REMOVED)
		kstkchk(old, off, 1);
	if (old->tc_State == TS_RUN && old != &IdleTask) {
		old->tc_State = TS_READY;
		xEnqueue(&TaskReady, &old->tc_Node);
	}
	t = (struct Task *)xRemHead(&TaskReady);
	if (t == NULL)
		t = &IdleTask;
	t->tc_State = TS_RUN;
	ThisTask = t;
	Quantum = QUANTUM;
	SysFlags &= ~SF_SAR;
	return (SPOFF(t));
}

/* The tick server's scheduler work: time, due sleepers and the quantum.
 * The interrupt exit in kintr makes any switch this asks for. */
void
ktick()
{
	register struct Task *t, *next, *me;
	UWORD n;

	TickCount++;
	kstkchk(ThisTask, IntFrame, 0);
	if (Sleepers != 0) {
		n = 0;
		for (t = (struct Task *)TaskWait.lh_Head;
		    (next = (struct Task *)t->tc_Node.ln_Succ) != NULL;
		    t = next) {
			if ((t->tc_SigWait & SIGF_SLEEP) == 0)
				continue;
			if ((LONG)(TickCount - TC_WAKE(t)) >= 0)
				ksignal(t, SIGF_SLEEP);
			else
				n++;
		}
		Sleepers = n;
	}
	me = ThisTask;
	if (me == &IdleTask) {
		if (!IsListEmpty(&TaskReady))
			SysFlags |= SF_SAR;
	} else if (--Quantum <= 0) {
		t = (struct Task *)TaskReady.lh_Head;
		if (t->tc_Node.ln_Succ != NULL &&
		    t->tc_Node.ln_Pri >= me->tc_Node.ln_Pri)
			SysFlags |= SF_SAR;
		else
			Quantum = QUANTUM;
	}
}

/*
 * Build the initial stack below top: the finalizer as the entry's return
 * address, then a trap frame that IRETs into pc.  Returns the frame offset.
 */
static UWORD
kframe(top, pc, fpc)
UWORD top;
TASKPROC pc, fpc;
{
	register UWORD *fp;
	register int i;
	UWORD off;

	off = top - 4;
	*(ULONG *)STKPTR(off) = (ULONG)fpc;
	off -= FRAMESIZE;
	fp = (UWORD *)STKPTR(off);
	for (i = 0; i < F_FCW; i++)
		fp[i] = 0;
	fp[F_FCW] = FCW_TASK;
	fp[F_PCSEG] = (UWORD)((ULONG)pc >> 16);
	fp[F_PCOFF] = (UWORD)(ULONG)pc;
	return (off);
}

static void
kinitrec(t)
register struct Task *t;
{
	t->tc_Flags = 0;
	t->tc_IDNestCnt = -1;
	t->tc_TDNestCnt = -1;
	t->tc_SigAlloc = 0xFFFFL;
	t->tc_SigWait = 0;
	t->tc_SigRecvd = 0;
	t->tc_SigExcept = 0;
	t->tc_ExecPrivate = NULL;
	if (t->tc_TrapCode == NULL)
		t->tc_TrapCode = TaskTrapCode;
	*(UWORD *)t->tc_SPLower = STKGUARD;
}

/* Prepare the private idle Task.  Boot calls this once; it is never queued. */
void
kinitidle()
{
	register struct Task *t;

	t = &IdleTask;
	t->tc_Node.ln_Type = NT_TASK;
	t->tc_Node.ln_Pri = -128;
	t->tc_Node.ln_Name = idlename;
	t->tc_SPLower = STKPTR(kstkcfg.sc_root - IDLESTKSIZE);
	t->tc_SPUpper = STKPTR(kstkcfg.sc_root);
	kinitrec(t);
	t->tc_State = TS_READY;
	xNewList(&t->tc_MemEntry);
	t->tc_SPReg = STKPTR(kframe(kstkcfg.sc_root, (TASKPROC)kidle,
	    (TASKPROC)kfinal));
}

/* Adopt the running boot context as the root Task.  Boot calls this once,
 * before the first tick; the root Task's frame is saved at its first switch. */
void
kinitroot()
{
	register struct Task *t;

	kstkzero();
	t = &RootTask;
	t->tc_Node.ln_Type = NT_TASK;
	t->tc_Node.ln_Pri = 0;
	t->tc_Node.ln_Name = rootname;
	t->tc_SPLower = STKPTR(kstkcfg.sc_root);
	t->tc_SPUpper = STKPTR(ROOTSTKTOP);
	kinitrec(t);
	t->tc_State = TS_RUN;
	t->tc_SPReg = NULL;
	xNewList(&t->tc_MemEntry);
	ThisTask = t;
	Quantum = QUANTUM;
}

void
kidle()
{
	for (;;)
		haltcpu();
}

/* A returning entry with no finalizer lands here. */
void
kfinal()
{
	RemTask(NULL);
}

UWORD
xExecVersion()
{
	return (EXEC_TASKS_VERSION);
}

struct Task *
xAddTask(t, pc, fpc)
register struct Task *t;
TASKPROC pc, fpc;
{
	ULONG top, low;
	int s;

	if (t == NULL || pc == NULL)
		return (NULL);
	top = (ULONG)(t->tc_SPReg != NULL ? t->tc_SPReg : t->tc_SPUpper);
	low = (ULONG)t->tc_SPLower;
	if (PSEG(top) != SEGBASE(STACKSEG) || PSEG(low) != SEGBASE(STACKSEG) ||
	    ((top | low) & 1) != 0 ||
	    POFF(top) < POFF(low) + FRAMESIZE + 4 + IRQHEADROOM)
		return (NULL);
	if (fpc == NULL)
		fpc = (TASKPROC)kfinal;
	kinitrec(t);
	t->tc_SPReg = STKPTR(kframe((UWORD)POFF(top), pc, fpc));
	s = intoff();
	kready(t);
	kmaybe();
	intrest(s);
	return (t);
}

/*
 * As Amiga RemTask, free every MemList on tc_MemEntry.  The Task leaves
 * every list first, since a freed block's first 8 bytes are overwritten.
 * A Task removing itself frees its own record and stack under Forbid and
 * keeps running on them until the switch; nothing allocates before then.
 */
void
xRemTask(t)
register struct Task *t;
{
	register struct Task *me;
	struct Node *ml;
	int s;

	me = ThisTask;
	if (t == NULL)
		t = me;
	if (t == &IdleTask)
		kpanic("RemTask: idle");
	xForbid();
	s = intoff();
	switch (t->tc_State) {
	case TS_READY:
	case TS_WAIT:
		xRemove(&t->tc_Node);
		break;
	case TS_RUN:
		break;
	default:
		kpanic("RemTask: not live");
	}
	t->tc_State = TS_REMOVED;
	t->tc_SigWait = 0;
	t->tc_ExecPrivate = NULL;
	if (t == EpuOwner)
		EpuOwner = NULL;
	intrest(s);
	while ((ml = xRemHead(&t->tc_MemEntry)) != NULL)
		xFreeEntry(ml);
	if (t == me) {
		intoff();
		me->tc_TDNestCnt = -1;
		me->tc_IDNestCnt = -1;
		kresched();
		kpanic("RemTask: resumed");
	}
	xPermit();
}

static int
samename(a, b)
register char *a, *b;
{
	if (a == NULL)
		return (0);
	for (; *a == *b; a++, b++)
		if (*a == 0)
			return (1);
	return (0);
}

struct Task *
xFindTask(name)
char *name;
{
	register struct Task *t;
	int s;

	if (name == NULL)
		return (ThisTask);
	s = intoff();
	t = ThisTask;
	if (t == &IdleTask || !samename(t->tc_Node.ln_Name, name)) {
		t = (struct Task *)xFindName(&TaskReady, name);
		if (t == NULL)
			t = (struct Task *)xFindName(&TaskWait, name);
	}
	intrest(s);
	return (t);
}

int
xSetTaskPri(t, pri)
register struct Task *t;
int pri;
{
	register struct Task *me, *head;
	int old, s;

	s = intoff();
	me = ThisTask;
	old = t->tc_Node.ln_Pri;
	t->tc_Node.ln_Pri = pri;
	if (t->tc_State == TS_READY) {
		xRemove(&t->tc_Node);
		xEnqueue(&TaskReady, &t->tc_Node);
	}
	head = (struct Task *)TaskReady.lh_Head;
	if (head->tc_Node.ln_Succ != NULL &&
	    head->tc_Node.ln_Pri > me->tc_Node.ln_Pri)
		SysFlags |= SF_SAR;
	kmaybe();
	intrest(s);
	return (old);
}

/* amiga.lib CreateTask: the record and the stack are one AllocEntry list
 * on tc_MemEntry, which RemTask frees.  The stack must be MEMF_STACK. */
struct Task *
xCreateTask(name, pri, entry, size)
char *name;
LONG pri;
TASKPROC entry;
ULONG size;
{
	register struct Task *t;
	register struct MemList *ml;
	UBYTE *stk;
	struct {
		struct Node fml_Node;
		UWORD fml_NumEntries;
		struct MemEntry fml_ME[2];
	} fml;

	size = (size + 3) & ~3L;
	fml.fml_NumEntries = 2;
	fml.fml_ME[0].me_Reqs = MEMF_PUBLIC | MEMF_CLEAR;
	fml.fml_ME[0].me_Length = sizeof(struct Task);
	fml.fml_ME[1].me_Reqs = MEMF_STACK | MEMF_CLEAR;
	fml.fml_ME[1].me_Length = size;
	ml = xAllocEntry(&fml);
	if ((ULONG)ml & 0x80000000L)
		return (NULL);
	t = (struct Task *)ml->ml_ME[0].me_Addr;
	stk = (UBYTE *)ml->ml_ME[1].me_Addr;
	t->tc_SPLower = (APTR)stk;
	t->tc_SPUpper = (APTR)(stk + (UWORD)size);
	t->tc_SPReg = t->tc_SPUpper;
	t->tc_Node.ln_Type = NT_TASK;
	t->tc_Node.ln_Pri = pri;
	t->tc_Node.ln_Name = name;
	xNewList(&t->tc_MemEntry);
	xAddHead(&t->tc_MemEntry, &ml->ml_Node);
	if (xAddTask(t, entry, (TASKPROC)NULL) == NULL) {
		xFreeEntry(ml);
		return (NULL);
	}
	return (t);
}

/* Forbid and Permit are in platform/semfast.s; they come here to panic. */
void
knestbad(permit)
int permit;
{
	kpanic(permit ? "Permit: not forbidden" : "Forbid: nesting overflow");
}

void
xDisable()
{
	register struct Task *me;

	intoff();
	me = ThisTask;
	if (me->tc_IDNestCnt == 127)
		kpanic("Disable: nesting overflow");
	me->tc_IDNestCnt++;
}

void
xEnable()
{
	register struct Task *me;

	me = ThisTask;
	if (me->tc_IDNestCnt < 0)
		kpanic("Enable: not disabled");
	/* Inside a handler VI stays off until its IRET. */
	if (--me->tc_IDNestCnt < 0 && IntDepth == 0) {
		if ((SysFlags & SF_SAR) && me->tc_TDNestCnt < 0)
			kresched();
		intrest(FCW_TASK);
	}
}

/* Give the CPU to the next ready Task of equal or higher priority. */
void
xYield()
{
	register struct Task *me;
	int s;

	s = intoff();
	me = ThisTask;
	if (me->tc_TDNestCnt >= 0 || me->tc_IDNestCnt >= 0)
		SysFlags |= SF_SAR;
	else
		kresched();
	intrest(s);
}

void
xSleep(ticks)
ULONG ticks;
{
	register struct Task *me;
	int s;

	if (ticks == 0) {
		xYield();
		return;
	}
	s = intoff();
	me = ThisTask;
	TC_WAKE(me) = TickCount + ticks;
	me->tc_SigRecvd &= ~SIGF_SLEEP;
	Sleepers++;
	xWait(SIGF_SLEEP);
	intrest(s);
}
