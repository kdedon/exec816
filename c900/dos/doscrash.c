/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The Task crash requester, as AmigaOS 2.0+ shows it ("Software Failure",
 * Suspend or Reboot).  dos.library's init makes dtrap the default
 * tc_TrapCode (SysBase->TaskTrapCode); AddTask copies it, so Tasks added
 * before that keep the kernel's.
 *
 * dtrap holds a Task whose trap came in Task context: not NMI, the frame
 * within its own stack, outside Forbid and Disable, with interrupts on.
 * Any other trap goes to the previous default, the dead end.  To hold,
 * dtrap points the frame at dheld (dosasm.s), which leaves trap state on
 * the Task's stack (from its top when less than DCROOM is left below the
 * frame) and calls dcrash.  dcrash prints the trap line and a held line,
 * hands the crash to the crash process when intuition.library is on the
 * library list, and waits on no signal for good: the Task keeps its
 * memory, stack, locks and semaphores.
 *
 * The crash process (DCSTACK, started on the first such crash) shows each
 * crash in turn, taking its turn on the alert semaphore.  A requester
 * process (DCRSTACK) opens the requester with BuildEasyRequestArgs and waits on
 * SysReqHandler; if the window has not opened within DCWAIT seconds
 * (timer.device), the crash process abandons it, since the held Task may
 * own a lock the window open waits for, and shows DisplayAlert instead.
 * A requester process that opens its window late closes it and ends.  A
 * crash of input.device goes to DisplayAlert directly.  Reboot is the
 * dead-end alert; Suspend leaves the Task held.
 */
#include "dosint.h"
#include <exec/alerts.h>
#include <devices/timer.h>

#define DCROOM		768	/* least stack dcrash runs on below the frame */
#define DCSTACK		3072L	/* the crash process's stack: DisplayAlert */
#define DCRSTACK	2560L	/* a requester process's stack */
#define DCWAIT		2L	/* seconds for the requester's window */
#define DCGUARD		0xC900	/* the kernel's stack guard word */
#define FCW_SYS		0x4000
#define FCW_VINVI	0x1800
#define FCW_EPA		0x2000

/* intuition.library's vectors. */
#define LVOFREESYSREQ	(-372)
#define LVOBUILDEASY	(-594)
#define LVOSYSREQH	(-600)
#define LVODISPALERT	(-90)

/* intuition's struct EasyStruct. */
struct dceasy {
	ULONG es_StructSize;
	ULONG es_Flags;
	char *es_Title;
	char *es_TextFormat;
	char *es_GadgetFormat;
};

/* A held Task's crash, on its stack, for the crash process. */
struct dcmsg {
	struct Message m_Msg;
	struct Task *m_Task;
	ULONG m_Num;
};

/* One requester, shared by the crash and requester processes: freed by
 * the crash process, or by the requester process once abandoned. */
struct dcreq {
	struct Message r_Msg;
	ULONG r_Num;		/* the requester's argument */
	struct Task *r_Waiter;
	ULONG r_Sig;
	UWORD r_State;		/* DC_ */
	UWORD r_Gone;		/* abandoned */
	LONG r_Answer;		/* 1 Suspend, 0 Reboot, -1 none */
};
#define DC_BUILD	0
#define DC_OPEN		1
#define DC_DONE		2

void dheld();

static void (*dcold)();
static struct MsgPort dcport;
static struct Process *dcproc;
static struct Process *dcrunner;
static struct dcreq *dccur;

static char dcname[] = "crash";
static char dcrname[] = "crash requester";
static char dcint[] = "intuition.library";
static char dcinput[] = "input.device";
static struct dceasy dces = {
	sizeof(struct dceasy), 0L, "Software Failure",
	"Program failed (error #%08lx).\nWait for disk activity to finish.",
	"Suspend|Reboot"
};

/* DisplayAlert's text: lines of a 2-byte x, a y, the text and a
 * continuation byte.  The last line is formatted. */
static char dcalert[] = "\0\46\17Software Failure.   Left mouse button: \
Suspend.   Right: Reboot.\0\377\0\46\36";
static char dcalfmt[] = "Program failed (error #%08lx).  \
Wait for disk activity to finish.";

void
dtrap(n, fp)
ULONG n;
register struct TrapFrame *fp;
{
	register struct Task *me;
	register UWORD sp;

	me = FindTask(NULL);
	sp = (UWORD)(ULONG)fp + sizeof(*fp);
	if (n == TRAP_NMI || me->tc_TDNestCnt >= 0 ||
	    me->tc_IDNestCnt >= 0 || (fp->tf_FCW & FCW_VINVI) != FCW_VINVI ||
	    (UWORD)(ULONG)fp < (UWORD)(ULONG)me->tc_SPLower ||
	    sp > (UWORD)(ULONG)me->tc_SPUpper) {
		(*dcold)(n, fp);
		return;
	}
	if (sp - (UWORD)(ULONG)me->tc_SPLower < DCROOM)
		sp = (UWORD)(ULONG)me->tc_SPUpper;
	*(UWORD *)me->tc_SPLower = DCGUARD;
	fp->tf_Regs[0] = n >> 16;
	fp->tf_Regs[1] = n;
	fp->tf_Regs[2] = fp->tf_PCSeg;
	fp->tf_Regs[3] = fp->tf_PC;
	fp->tf_Regs[4] = sp;
	fp->tf_Regs[5] = fp->tf_FCW;
	fp->tf_FCW = (fp->tf_FCW & FCW_EPA) | 0x8000 | FCW_SYS | FCW_VINVI;
	fp->tf_PCSeg = 0x8000 | (UWORD)((ULONG)dheld >> 16);
	fp->tf_PC = (UWORD)(ULONG)dheld;
}

/* Start a process at entry; NULL when it cannot start. */
static struct Process *
dcstart(entry, name, stack)
LONG (*entry)();
char *name;
LONG stack;
{
	struct TagItem t[6];

	t[0].ti_Tag = NP_Entry;
	t[0].ti_Data = (ULONG)entry;
	t[1].ti_Tag = NP_Name;
	t[1].ti_Data = (ULONG)name;
	t[2].ti_Tag = NP_StackSize;
	t[2].ti_Data = stack;
	t[3].ti_Tag = NP_CopyVars;
	t[3].ti_Data = FALSE;
	t[4].ti_Tag = NP_CurrentDir;
	t[4].ti_Data = 0L;
	t[5].ti_Tag = TAG_DONE;
	return (xCreateNewProc(DOSBase, t));
}

/* The requester process: the requester for the dcreq on its port. */
static LONG
dcreqmain(args, len)
char *args;
LONG len;
{
	register struct Process *me;
	register struct dcreq *r;
	register struct Library *ib;
	register APTR w;
	LONG a;

	me = (struct Process *)FindTask(NULL);
	WaitPort(&me->pr_MsgPort);
	r = (struct dcreq *)GetMsg(&me->pr_MsgPort);
	w = NULL;
	if ((ib = (*rloldlib)(dcint, 0L)) != NULL)
		w = (*(APTR (*)())((char *)ib + LVOBUILDEASY))(ib, NULL, &dces,
		    NULL, &r->r_Num);
	a = -1;
	if (w != NULL) {
		Forbid();
		if (!r->r_Gone) {
			r->r_State = DC_OPEN;
			Signal(r->r_Waiter, r->r_Sig);
		}
		Permit();
		if (w == (APTR)1L)
			a = 1;
		else {
			if (!r->r_Gone)
				while ((a = (*(LONG (*)())((char *)ib +
				    LVOSYSREQH))(ib, w, NULL, TRUE)) < 0)
					;
			(*(void (*)())((char *)ib + LVOFREESYSREQ))(ib, w);
		}
	}
	Forbid();
	if (r->r_Gone)
		FreeMem(r, (ULONG)sizeof(*r));
	else {
		r->r_Answer = a;
		r->r_State = DC_DONE;
		Signal(r->r_Waiter, r->r_Sig);
	}
	Permit();
	if (ib != NULL)
		CloseLibrary(ib);
	return (0);
}

/* The requester for num: its answer, or -1 when its window did not open
 * in time or at all. */
static LONG
dcrequest(num, tr, sig)
ULONG num;
register struct timerequest *tr;
ULONG sig;
{
	register struct dcreq *r;
	register struct Process *p;
	ULONG tsig;
	LONG a;

	if ((r = (struct dcreq *)AllocMem((ULONG)sizeof(*r),
	    MEMF_PUBLIC | MEMF_CLEAR)) == NULL)
		return (-1);
	r->r_Num = num;
	r->r_Waiter = FindTask(NULL);
	r->r_Sig = sig;
	r->r_Msg.mn_Node.ln_Type = NT_MESSAGE;
	r->r_Msg.mn_Length = sizeof(*r);
	SetSignal(0L, sig);
	Forbid();
	if ((p = dcstart(dcreqmain, dcrname, DCRSTACK)) != NULL) {
		dcrunner = p;
		dccur = r;
	}
	Permit();
	if (p == NULL) {
		FreeMem(r, (ULONG)sizeof(*r));
		return (-1);
	}
	PutMsg(&p->pr_MsgPort, &r->r_Msg);
	tsig = 1L << tr->tr_node.io_Message.mn_ReplyPort->mp_SigBit;
	tr->tr_node.io_Command = TR_ADDREQUEST;
	tr->tr_time.tv_secs = DCWAIT;
	tr->tr_time.tv_micro = 0;
	SendIO(&tr->tr_node);
	for (;;) {
		Wait(sig | tsig);
		Forbid();
		if (r->r_State == DC_BUILD && CheckIO(&tr->tr_node) != NULL)
			r->r_Gone = TRUE;
		Permit();
		if (r->r_Gone || r->r_State != DC_BUILD)
			break;
	}
	if (CheckIO(&tr->tr_node) == NULL)
		AbortIO(&tr->tr_node);
	WaitIO(&tr->tr_node);
	a = -1;
	if (!r->r_Gone) {
		while (r->r_State != DC_DONE)
			Wait(sig);
		a = r->r_Answer;
		FreeMem(r, (ULONG)sizeof(*r));
	}
	Forbid();
	dcrunner = NULL;
	dccur = NULL;
	Permit();
	return (a);
}

/* One held Task's crash: the requester or the alert, then Reboot's dead
 * end. */
static void
dcshow(m, tr, sig)
struct dcmsg *m;
struct timerequest *tr;
ULONG sig;
{
	register struct Library *ib;
	register int turn;
	ULONG num;
	LONG a;
	char buf[sizeof(dcalert) + sizeof(dcalfmt) + 8];
	char *p;

	num = m->m_Num;
	if ((ib = (*rloldlib)(dcint, 0L)) == NULL)
		return;
	if ((turn = rlalsem.ss_Owner != m->m_Task) != 0)
		ObtainSemaphore(&rlalsem);
	a = -1;
	if (tr != NULL && m->m_Task != FindTask(dcinput))
		a = dcrequest(num, tr, sig);
	if (a < 0) {
		p = buf;
		CopyMem(dcalert, p, (ULONG)sizeof(dcalert) - 1);
		p += sizeof(dcalert) - 1;
		RawDoFmt(dcalfmt, (APTR)&num, (APTR)dnumput, (APTR)&p);
		*p = 0;
		a = (*(BOOL (*)())((char *)ib + LVODISPALERT))(ib, 0L, buf,
		    40);
	}
	if (turn)
		ReleaseSemaphore(&rlalsem);
	CloseLibrary(ib);
	if (a == 0)
		Alert(num);
}

/* The crash process: each crash on dcport, in turn. */
static LONG
dcmain(args, len)
char *args;
LONG len;
{
	register struct dcmsg *m;
	register struct timerequest *tr;
	struct MsgPort *tp;
	ULONG sig;

	tr = NULL;
	if ((tp = CreateMsgPort()) != NULL &&
	    (tr = (struct timerequest *)CreateIORequest(tp,
	    (ULONG)sizeof(*tr))) != NULL &&
	    OpenDevice(TIMERNAME, (ULONG)UNIT_VBLANK, &tr->tr_node, 0L) != 0)
		tr = NULL;
	sig = 1L << AllocSignal(-1L);
	Forbid();
	dcport.mp_SigBit = AllocSignal(-1L);
	dcport.mp_SigTask = FindTask(NULL);
	dcport.mp_Flags = PA_SIGNAL;
	Permit();
	for (;;) {
		WaitPort(&dcport);
		while ((m = (struct dcmsg *)GetMsg(&dcport)) != NULL)
			dcshow(m, tr, sig);
	}
}

/*
 * The held Task, from dheld on its own stack with interrupts on: print
 * the trap, hand it to the crash process, wait for good.  A crash of the
 * crash process itself leaves its queue for the next one; a crash of the
 * requester process ends its requester.
 */
void
dcrash(n, pcseg, pc, fcw)
ULONG n;
UWORD pcseg, pc, fcw;
{
	register struct Task *me;
	struct dcmsg m;

	me = FindTask(NULL);
	m.m_Num = AT_DeadEnd | n;
	xKPrintf("exec900: trap %lu pc %x:%x fcw %x\n", n,
	    (pcseg >> 8) & 0x7F, pc, fcw);
	xKPrintf("exec900: task %s alert %08lx held\n",
	    me->tc_Node.ln_Name, m.m_Num);
	Forbid();
	if (me == (struct Task *)dcproc) {
		dcport.mp_Flags = PA_IGNORE;
		dcproc = NULL;
	} else if (me == (struct Task *)dcrunner) {
		dccur->r_Answer = -1;
		dccur->r_State = DC_DONE;
		Signal(dccur->r_Waiter, dccur->r_Sig);
	} else if (FindName(&SysBase->LibList, dcint) != NULL &&
	    (dcproc != NULL || (dcproc = dcstart(dcmain, dcname, DCSTACK)) != NULL)) {
		m.m_Msg.mn_Node.ln_Type = NT_MESSAGE;
		m.m_Msg.mn_Length = sizeof(m);
		m.m_Task = me;
		PutMsg(&dcport, &m.m_Msg);
	}
	Permit();
	Wait(0L);
}

/* From dos.library's init. */
void
dcsetup()
{
	dcport.mp_Node.ln_Type = NT_MSGPORT;
	dcport.mp_Node.ln_Name = dcname;
	dcport.mp_Flags = PA_IGNORE;
	NewList(&dcport.mp_MsgList);
	dcold = (void (*)())SysBase->TaskTrapCode;
	SysBase->TaskTrapCode = (APTR)dtrap;
}
