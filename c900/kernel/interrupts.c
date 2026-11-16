/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Interrupt dispatch: one handler or one server chain per interrupt number,
 * in the classic Amiga Exec model.  Upstream has no interrupt interface; its
 * VBI and SIO entries are fixed native code.  docs/internals.md describes
 * the C900 design and its departures.
 *
 * Every vectored interrupt and the NVI enter kintr from trap.s with VI and
 * NVI off, so handlers never nest.  A handler may Signal, PutMsg or
 * ReplyMsg; the exit below switches Tasks when one of those readied a Task
 * that should run.
 */
#include "kernel.h"

/* Frame word holding the identifier the CPU saved. */
#define F_ID		14

/* CIO U70 C/T 3 command/status: clear IP and IUS, gate stays open. */
#define P_CT3CS		0x0019
#define CT3CS_CLIP	0x24

/* LC card 8568 status: reading it releases INTR. */
#define LC_STAT		0x0181

/* More stray interrupts than this between two ticks is a storm: a source
 * nothing claims is holding its line. */
#define STORM		1000

#define NVISLOT		(INTV_COUNT - 1)

/* The interrupt stack, from the MEMF_STACK pool: dispatch, every handler
 * and the switch at exit run on it (trap.s). */
#define INTSTKSIZE	512

UWORD kintstk;

/* The interrupted frame, for the tick's stack probe. */
UWORD IntFrame;

/* The server chain is singly linked through ln_Succ, in priority order and
 * NULL-terminated: a full List per number would cost 14 bytes each. */
struct IntVector {
	struct Interrupt *iv_Handler;
	struct Interrupt *iv_Servers;
};

static struct IntVector intvec[INTV_COUNT];
static UWORD stray[INTV_COUNT];
static UWORD strayrun;

static int tickserv();

static char tickname[] = "tick";
static struct Interrupt tickint = {
	{ NULL, NULL, NT_INTERRUPT, 127, tickname }, NULL, tickserv
};

static int
slotof(n)
UWORD n;
{
	if (n == INTV_NVI)
		return (NVISLOT);
	if (n > 0xFE || (n & 1) != 0) {
		xKPrintf("exec900: interrupt number %x\n", n);
		kpanic("bad interrupt number");
	}
	return (n >> 1);
}

/* The scheduler tick: the first server on INTV_TICK. */
static int
tickserv(data, n)
APTR data;
UWORD n;
{
	outb(P_CT3CS, CT3CS_CLIP);
	strayrun = 0;
	ktick();
	return (0);
}

static void
strayint(slot, id)
int slot;
UWORD id;
{
	stray[slot]++;
	if (++strayrun >= STORM) {
		xKPrintf("exec900: interrupt storm, id %x\n", id);
		kpanic("unclaimed interrupt storm");
	}
}

UWORD
kintr(off, id)
UWORD off, id;
{
	register struct Interrupt *is;
	register struct Task *me;
	register int slot;
	struct Interrupt *next;
	struct IntVector *v;

	slot = id == INTV_NVI ? NVISLOT : (id & 0xFF) >> 1;
	v = &intvec[slot];
	IntFrame = off;
	IntDepth++;
	/* The 8568 shares the NVI and holds INTR until its status is read:
	 * release it before any handler, so one that claims every NVI (a
	 * single-step monitor) cannot leave the line held. */
	if (slot == NVISLOT)
		inb(LC_STAT);
	if ((is = v->iv_Handler) != NULL)
		(*is->is_Code)(is->is_Data, id);
	else {
		for (is = v->iv_Servers; is != NULL; is = next) {
			next = (struct Interrupt *)is->is_Node.ln_Succ;
			if ((*is->is_Code)(is->is_Data, id))
				break;
		}
		if (is == NULL && (slot == NVISLOT || v->iv_Servers == NULL))
			strayint(slot, id);
	}
	IntDepth--;
	me = ThisTask;
	if ((SysFlags & SF_SAR) && IntDepth == 0 &&
	    me->tc_TDNestCnt < 0 && me->tc_IDNestCnt < 0)
		off = kswitch(off);
	return (off);
}

/* Boot: the interrupt stack, empty chains, the tick server, and the 8568
 * released before any interrupt is enabled. */
void
kintinit()
{
	register int i;
	APTR stk;

	if ((stk = xAllocMem((ULONG)INTSTKSIZE, (ULONG)MEMF_STACK)) == NULL)
		kpanic("no interrupt stack");
	kintstk = (UWORD)POFF(stk) + INTSTKSIZE;
	for (i = 0; i < INTV_COUNT; i++) {
		intvec[i].iv_Handler = NULL;
		intvec[i].iv_Servers = NULL;
		stray[i] = 0;
	}
	strayrun = 0;
	inb(LC_STAT);
	xAddIntServer((UWORD)INTV_TICK, &tickint);
}

/* Report stray interrupts; the boot loader's disk reads leave one on
 * INTV_DISK. */
void
kintreport()
{
	register int i;

	for (i = 0; i < INTV_COUNT; i++)
		if (stray[i] != 0)
			xKPrintf("exec900: %u stray interrupt(s) on %x\n",
			    stray[i], i == NVISLOT ? INTV_NVI : i << 1);
}

struct Interrupt *
xSetIntVector(n, is)
UWORD n;
struct Interrupt *is;
{
	register struct IntVector *v;
	struct Interrupt *old;
	int s;

	v = &intvec[slotof(n)];
	if (is != NULL)
		is->is_Node.ln_Type = NT_INTERRUPT;
	s = intoff();
	old = v->iv_Handler;
	v->iv_Handler = is;
	intrest(s);
	return (old);
}

void
xAddIntServer(n, is)
UWORD n;
register struct Interrupt *is;
{
	register struct Interrupt **pp;
	int s;

	pp = &intvec[slotof(n)].iv_Servers;
	is->is_Node.ln_Type = NT_INTERRUPT;
	s = intoff();
	while (*pp != NULL && (*pp)->is_Node.ln_Pri >= is->is_Node.ln_Pri)
		pp = (struct Interrupt **)&(*pp)->is_Node.ln_Succ;
	is->is_Node.ln_Succ = (struct Node *)*pp;
	*pp = is;
	intrest(s);
}

void
xRemIntServer(n, is)
UWORD n;
struct Interrupt *is;
{
	register struct Interrupt **pp;
	int s;

	pp = &intvec[slotof(n)].iv_Servers;
	s = intoff();
	while (*pp != NULL && *pp != is)
		pp = (struct Interrupt **)&(*pp)->is_Node.ln_Succ;
	if (*pp != NULL)
		*pp = (struct Interrupt *)is->is_Node.ln_Succ;
	intrest(s);
}

ULONG
xIntStray(n)
WORD n;
{
	register int i;
	ULONG sum;

	if (n != INTV_ALL)
		return ((ULONG)stray[slotof((UWORD)n)]);
	sum = 0;
	for (i = 0; i < INTV_COUNT; i++)
		sum += stray[i];
	return (sum);
}
