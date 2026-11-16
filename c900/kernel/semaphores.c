/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Signal semaphores, after Amiga Exec V39; docs/internals.md gives the
 * protocol.  Every change runs under Forbid.  A waiter queues a
 * SemaphoreRequest on its own stack and Waits for SIGF_SINGLE; the
 * releasing Task grants it ownership before signalling, so the waiter
 * returns holding the semaphore.  platform/semfast.s holds the
 * uncontended Obtain, Attempt and Release calls; it comes here for the
 * rest.
 */
#include "kernel.h"

struct List SemaphoreList = {
	(struct Node *)&SemaphoreList.lh_Tail,
	NULL,
	(struct Node *)&SemaphoreList.lh_Head,
	NT_SIGNALSEM,
	0
};

#define SRSHARED	1
#define WAITER(sr)	((ULONG)(sr)->sr_Waiter)
#define BIDMODE(m)	((int)(ULONG)(m)->ssm_Message.mn_Node.ln_Name & SRSHARED)

/* Count a hold for me if the semaphore is free, mine, or (shr) shared.
 * The caller holds Forbid and has not counted the request. */
static int
take(ss, me, shr)
register struct SignalSemaphore *ss;
struct Task *me;
int shr;
{
	if (++ss->ss_QueueCount == 0) {
		if (!shr)
			ss->ss_Owner = me;
	} else if (ss->ss_Owner != me && (!shr || ss->ss_Owner != NULL))
		return (0);
	ss->ss_NestCount++;
	return (1);
}

/* Hand ss to the queued request sr and wake its Task or reply its bid. */
static void
grant(ss, sr, shr)
register struct SignalSemaphore *ss;
register struct SemaphoreRequest *sr;
int shr;
{
	register struct SemaphoreMessage *m;
	ULONG w;

	xRemove((struct Node *)sr);
	ss->ss_NestCount++;
	if ((w = WAITER(sr)) != 0L) {
		if (!shr)
			ss->ss_Owner = sr->sr_Waiter;
		xSignal((struct Task *)(w & ~1L), SIGF_SINGLE);
		return;
	}
	m = (struct SemaphoreMessage *)sr;
	if (!shr)
		ss->ss_Owner = m->ssm_Message.mn_ReplyPort->mp_SigTask;
	m->ssm_Semaphore = ss;
	xReplyMsg((struct Message *)m);
}

void
xInitSemaphore(ss)
register struct SignalSemaphore *ss;
{
	ss->ss_Link.ln_Type = NT_SIGNALSEM;
	xNewList((struct List *)&ss->ss_WaitQueue);
	ss->ss_NestCount = 0;
	ss->ss_Owner = NULL;
	ss->ss_QueueCount = -1;
}

static void
obtain(ss, shr)
register struct SignalSemaphore *ss;
int shr;
{
	register struct Task *me;
	struct SemaphoreRequest sr;

	me = ThisTask;
	xForbid();
	if (!take(ss, me, shr)) {
		sr.sr_Waiter = (struct Task *)((ULONG)me | (ULONG)shr);
		xSetSignal(0L, SIGF_SINGLE);
		xAddTail((struct List *)&ss->ss_WaitQueue, (struct Node *)&sr);
		xWait(SIGF_SINGLE);
	}
	xPermit();
}

void
ksemobtx(ss)
struct SignalSemaphore *ss;
{
	obtain(ss, 0);
}

void
ksemobts(ss)
struct SignalSemaphore *ss;
{
	obtain(ss, SRSHARED);
}

/* The last release grants the next exclusive request, or every shared
 * request at the head of the queue. */
void
ksemrel(ss)
register struct SignalSemaphore *ss;
{
	register struct SemaphoreRequest *sr, *next;
	ULONG w;
	int shr;

	xForbid();
	/* A recoverable alert; the semaphore is left unchanged. */
	if (ss->ss_NestCount <= 0 ||
	    (ss->ss_Owner != NULL && ss->ss_Owner != ThisTask)) {
		xAlert(AN_SemCorrupt);
		xPermit();
		return;
	}
	ss->ss_QueueCount--;
	if (--ss->ss_NestCount == 0) {
		ss->ss_Owner = NULL;
		for (sr = (struct SemaphoreRequest *)ss->ss_WaitQueue.mlh_Head;
		    (next = (struct SemaphoreRequest *)sr->sr_Link.mln_Succ) !=
		    NULL; sr = next) {
			if ((w = WAITER(sr)) == 0L)
				shr = BIDMODE((struct SemaphoreMessage *)sr);
			else
				shr = (int)w & SRSHARED;
			if (!shr && ss->ss_NestCount != 0)
				break;
			grant(ss, sr, shr);
			if (!shr)
				break;
		}
	}
	xPermit();
}

/* Obtain every semaphore on l exclusively.  Busy ones queue their
 * ss_MultipleLink, so one Task at a time may use a given list. */
void
xObtainSemList(l)
struct List *l;
{
	register struct SignalSemaphore *ss;
	register struct Task *me;
	int busy;

	me = ThisTask;
	busy = 0;
	xForbid();
	xSetSignal(0L, SIGF_SINGLE);
	for (ss = (struct SignalSemaphore *)l->lh_Head;
	    ss->ss_Link.ln_Succ != NULL;
	    ss = (struct SignalSemaphore *)ss->ss_Link.ln_Succ)
		if (!take(ss, me, 0)) {
			ss->ss_MultipleLink.sr_Waiter = me;
			xAddTail((struct List *)&ss->ss_WaitQueue,
			    (struct Node *)&ss->ss_MultipleLink);
			busy = 1;
		}
	if (busy)
		for (ss = (struct SignalSemaphore *)l->lh_Head;
		    ss->ss_Link.ln_Succ != NULL;
		    ss = (struct SignalSemaphore *)ss->ss_Link.ln_Succ)
			while (ss->ss_Owner != me)
				xWait(SIGF_SINGLE);
	xPermit();
}

void
xReleaseSemList(l)
struct List *l;
{
	register struct Node *n;

	for (n = l->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		xReleaseSem((struct SignalSemaphore *)n);
}

void
xAddSemaphore(ss)
struct SignalSemaphore *ss;
{
	xInitSemaphore(ss);
	xForbid();
	xEnqueue(&SemaphoreList, (struct Node *)ss);
	xPermit();
}

void
xRemSemaphore(ss)
struct SignalSemaphore *ss;
{
	xForbid();
	xRemove((struct Node *)ss);
	xPermit();
}

struct SignalSemaphore *
xFindSemaphore(name)
char *name;
{
	register struct Node *n;

	xForbid();
	n = xFindName(&SemaphoreList, name);
	xPermit();
	return ((struct SignalSemaphore *)n);
}

/* Bid for ss; the bid is replied when granted.  A queued bid's
 * sr_Waiter, which overlays ln_Type, ln_Pri and the high word of ln_Name,
 * is cleared to mark it as a message. */
void
xProcure(ss, m)
register struct SignalSemaphore *ss;
register struct SemaphoreMessage *m;
{
	xForbid();
	if (take(ss, ThisTask, BIDMODE(m))) {
		m->ssm_Semaphore = ss;
		xReplyMsg((struct Message *)m);
	} else {
		((struct SemaphoreRequest *)m)->sr_Waiter = NULL;
		xAddTail((struct List *)&ss->ss_WaitQueue, (struct Node *)m);
	}
	xPermit();
}

/* Withdraw a queued bid, replying it with ssm_Semaphore NULL, or release
 * a granted one. */
void
xVacate(ss, m)
register struct SignalSemaphore *ss;
register struct SemaphoreMessage *m;
{
	register struct Node *n;

	xForbid();
	for (n = (struct Node *)ss->ss_WaitQueue.mlh_Head; n->ln_Succ != NULL;
	    n = n->ln_Succ)
		if (n == (struct Node *)m) {
			xRemove(n);
			ss->ss_QueueCount--;
			m->ssm_Semaphore = NULL;
			xReplyMsg((struct Message *)m);
			xPermit();
			return;
		}
	xReleaseSem(ss);
	xPermit();
}
