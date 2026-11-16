/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Signals and Wait.  Port of the signal services in lib/exec/taskpolicy.act;
 * the contract is docs/reference/signals.md.  Bits 0..15 are system bits
 * and start allocated.  Signal is also called from the tick.
 */
#include "kernel.h"

#define SIGBIT(n)	(1L << (n))

int
xAllocSignal(n)
register int n;
{
	register struct Task *me;
	ULONG mask;
	int s;

	me = ThisTask;
	if (n == -1) {
		for (n = SIG_COUNT - 1; n >= SIGB_USER_FIRST; n--)
			if ((me->tc_SigAlloc & SIGBIT(n)) == 0)
				break;
	}
	if (n < SIGB_USER_FIRST || n >= SIG_COUNT)
		return (-1);
	mask = SIGBIT(n);
	if (me->tc_SigAlloc & mask)
		return (-1);
	s = intoff();
	me->tc_SigAlloc |= mask;
	me->tc_SigRecvd &= ~mask;
	intrest(s);
	return (n);
}

/* As on Amiga Exec, a low byte of 0xFF (-1, or a UBYTE mp_SigBit of -1)
 * is ignored and nothing else is checked: the bit, modulo 32, is cleared. */
void
xFreeSignal(n)
int n;
{
	if ((n & 0xFF) == 0xFF)
		return;
	ThisTask->tc_SigAlloc &= ~SIGBIT(n & (SIG_COUNT - 1));
}

ULONG
xSetSignal(new, mask)
ULONG new, mask;
{
	register struct Task *me;
	ULONG old;
	int s;

	s = intoff();
	me = ThisTask;
	old = me->tc_SigRecvd;
	me->tc_SigRecvd = (old & ~mask) | (new & mask);
	intrest(s);
	return (old);
}

/* Signal's work, VI off: post t's bits, ready it if they end its Wait,
 * and switch to it now when it should run and the caller allows. */
#define SIGNAL(t, mask, me) \
	t->tc_SigRecvd |= mask; \
	if (t->tc_State == TS_WAIT && (t->tc_SigRecvd & t->tc_SigWait)) { \
		xRemove(&t->tc_Node); \
		kready(t); \
		me = ThisTask; \
		if ((SysFlags & SF_SAR) && IntDepth == 0 && \
		    me->tc_TDNestCnt < 0 && me->tc_IDNestCnt < 0) \
			kresched(); \
	}

/* Signal with VI off: post, WaitIO and the tick call it directly. */
void
ksignal(t, mask)
register struct Task *t;
ULONG mask;
{
	register struct Task *me;

	SIGNAL(t, mask, me);
}

void
xSignal(t, mask)
register struct Task *t;
ULONG mask;
{
	register struct Task *me;
	int s;

	s = intoff();
	SIGNAL(t, mask, me);
	intrest(s);
}

ULONG
xWait(mask)
ULONG mask;
{
	register struct Task *me;
	ULONG got;
	int s;

	s = intoff();
	me = ThisTask;
	while ((me->tc_SigRecvd & mask) == 0) {
		me->tc_SigWait = mask;
		me->tc_State = TS_WAIT;
		xAddTail(&TaskWait, &me->tc_Node);
		kresched();
	}
	me->tc_SigWait = 0;
	got = me->tc_SigRecvd & mask;
	me->tc_SigRecvd &= ~got;
	intrest(s);
	return (got);
}
