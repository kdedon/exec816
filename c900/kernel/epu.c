/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The Z8070 EPU: boot probe and lazy context switching.
 *
 * Every Task runs with FCW EPA clear except the EPU's owner, so another
 * Task's first extended instruction takes the extended-instruction trap.
 * kfault hands it here: the owner's state goes to its save area and its
 * saved frame loses EPA, the trapping Task's state is loaded and it
 * re-executes the instruction with EPA set.  A save area is a one-entry
 * MemList on tc_MemEntry named epuname, made at the Task's first use, so
 * RemTask frees it.
 */
#include "kernel.h"

#define EPUSAVE		88

struct Task *EpuOwner;
APTR kepurec;			/* probe resume address, while probing */
static UWORD *EpuArea;		/* EpuOwner's save area */
static char epuname[] = "EPU";

static struct MemList epuml;

int kepuprobe();
void kepusave();
void kepuload();

void
kepuinit()
{
	if (kepuprobe()) {
		epuml.ml_NumEntries = 1;
		epuml.ml_ME[0].me_Reqs = MEMF_PUBLIC | MEMF_CLEAR;
		epuml.ml_ME[0].me_Length = EPUSAVE;
		ExecLib.AttnFlags |= AFF_68881;
		xKPrintf("exec900: Z8070 EPU\n");
	}
}

/* An extended-instruction trap.  Returns 1 when the frame is to resume
 * on the EPU, 0 for the Task's tc_TrapCode. */
int
kepuswitch(fp)
register struct TrapFrame *fp;
{
	register struct Task *me;
	register struct MemList *ml;
	UWORD *pc;

	if (!(ExecLib.AttnFlags & AFF_68881) || IntDepth != 0)
		return (0);
	/* The CPU saved the address of the instruction's second word. */
	pc = (UWORD *)MKPTR(SEGBASE((fp->tf_PCSeg >> 8) & 0x7F),
	    (UWORD)(fp->tf_PC - 2));
	if (*pc != fp->tf_Id)
		return (0);
	me = ThisTask;
	if (me != EpuOwner) {
		for (ml = (struct MemList *)me->tc_MemEntry.lh_Head;
		    ml->ml_Node.ln_Succ != NULL;
		    ml = (struct MemList *)ml->ml_Node.ln_Succ)
			if (ml->ml_Node.ln_Name == epuname)
				break;
		if (ml->ml_Node.ln_Succ == NULL) {
			IntDepth++;
			ml = xAllocEntry(&epuml);
			IntDepth--;
			if ((LONG)ml < 0)
				return (0);
			ml->ml_Node.ln_Name = epuname;
			xAddHead(&me->tc_MemEntry, &ml->ml_Node);
		}
		if (EpuOwner != NULL) {
			kepusave(EpuArea);
			((struct TrapFrame *)EpuOwner->tc_SPReg)->tf_FCW &=
			    ~FCW_EPA;
		}
		EpuArea = (UWORD *)ml->ml_ME[0].me_Addr;
		kepuload(EpuArea);
		EpuOwner = me;
	}
	fp->tf_FCW |= FCW_EPA;
	fp->tf_PC -= 2;
	return (1);
}
