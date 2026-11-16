/* SPDX-License-Identifier: MIT */
/*
 * Messages: the examples/messages.act exchange.  A worker publishes a named
 * port, the root finds it and sends requests that come back through its own
 * port in FIFO order; a one-way message is marked NT_FREEMSG.  Also the
 * public registry and a PA_IGNORE port.
 */
#include "test.h"

#define NREQ		3
#define STACKSIZE	2000

struct Request {
	struct Message rq_Msg;
	UWORD rq_Value;
	UWORD rq_Seq;
};

static char service[] = "ECHO";
static struct Task *parent;
static struct MsgPort *echo;
static ULONG readymask, donemask;
static struct Request req[NREQ];
static struct Message oneway;
static int peekfail;

/* Serve NREQ requests and one one-way message, then retire the port. */
static void
worker()
{
	register struct Message *m, *h;
	register int i;

	if ((echo = CreateMsgPort()) != NULL) {
		echo->mp_Node.ln_Name = service;
		AddPort(echo);
	}
	Signal(parent, readymask);
	if (echo != NULL) {
		for (i = 0; i < NREQ + 1; i++) {
			h = WaitPort(echo);
			m = GetMsg(echo);
			if (h != m || m->mn_Node.ln_Type != NT_MESSAGE)
				peekfail = 1;
			if (m != &oneway) {
				((struct Request *)m)->rq_Value++;
				((struct Request *)m)->rq_Seq = i;
			}
			ReplyMsg(m);
		}
		RemPort(echo);
		DeleteMsgPort(echo);
	}
	Forbid();
	Signal(parent, donemask);
	RemTask(NULL);
}

static char *
exchange()
{
	struct MsgPort *replies, *dest;
	struct Message *m;
	int i;

	if ((replies = CreateMsgPort()) == NULL)
		return ("CreateMsgPort");
	if (replies->mp_SigTask != FindTask(NULL) ||
	    replies->mp_Node.ln_Type != NT_MSGPORT ||
	    (replies->mp_Flags & PF_ACTION) != PA_SIGNAL ||
	    replies->mp_SigBit < SIGB_USER_FIRST || GetMsg(replies) != NULL)
		return ("CreateMsgPort fields");
	for (i = 0; i < NREQ; i++) {
		req[i].rq_Msg.mn_Length = sizeof(struct Request);
		req[i].rq_Msg.mn_ReplyPort = replies;
		req[i].rq_Value = 41 + i;
	}
	oneway.mn_Length = sizeof(struct Message);
	oneway.mn_ReplyPort = NULL;

	if (CreateTask("echo receiver", 0, worker, STACKSIZE) == NULL)
		return ("CreateTask");
	Wait(readymask);
	if (echo == NULL)
		return ("worker CreateMsgPort");

	/* Hold Forbid across discovery and use; queue every request before
	 * the worker runs. */
	Forbid();
	if ((dest = FindPort(service)) != echo) {
		Permit();
		return ("FindPort ECHO");
	}
	for (i = 0; i < NREQ; i++)
		PutMsg(dest, &req[i].rq_Msg);
	PutMsg(dest, &oneway);
	Permit();

	for (i = 0; i < NREQ; i++) {
		WaitPort(replies);
		m = GetMsg(replies);
		if (m != &req[i].rq_Msg)
			return ("replies out of order");
		if (m->mn_Node.ln_Type != NT_REPLYMSG)
			return ("reply not NT_REPLYMSG");
		if (req[i].rq_Value != 42 + i || req[i].rq_Seq != i)
			return ("requests not served in FIFO order");
	}
	Wait(donemask);
	if (peekfail)
		return ("WaitPort did not return the head");
	if (oneway.mn_Node.ln_Type != NT_FREEMSG)
		return ("one-way reply not NT_FREEMSG");
	if (GetMsg(replies) != NULL)
		return ("extra reply");
	if (FindPort(service) != NULL)
		return ("RemPort ECHO");
	DeleteMsgPort(replies);
	DeleteMsgPort(NULL);
	return (NULL);
}

static char *
registry()
{
	static struct MsgPort lo, hi;
	static struct Message m1, m2;
	static char loname[] = "low", hiname[] = "high";

	lo.mp_Node.ln_Name = loname;
	lo.mp_Node.ln_Pri = -5;
	lo.mp_Flags = PA_IGNORE;
	hi.mp_Node.ln_Name = hiname;
	hi.mp_Node.ln_Pri = 5;
	hi.mp_Flags = PA_IGNORE;
	AddPort(&lo);
	AddPort(&hi);
	if (SysBase->PortList.lh_Head != &hi.mp_Node ||
	    hi.mp_Node.ln_Succ != &lo.mp_Node)
		return ("AddPort priority order");
	if (FindPort("low") != &lo || FindPort("high") != &hi ||
	    FindPort("HIGH") != NULL || FindPort("hig") != NULL)
		return ("FindPort");

	/* PA_IGNORE queues without a signal, FIFO. */
	m1.mn_ReplyPort = m2.mn_ReplyPort = NULL;
	SetSignal(0L, 0xFFFF0000L);
	PutMsg(&lo, &m1);
	PutMsg(&lo, &m2);
	if (SetSignal(0L, 0L) & 0xFFFF0000L)
		return ("PA_IGNORE signalled");
	if (WaitPort(&lo) != &m1 || GetMsg(&lo) != &m1 ||
	    GetMsg(&lo) != &m2 || GetMsg(&lo) != NULL)
		return ("PA_IGNORE queue");
	if (m1.mn_Node.ln_Type != NT_MESSAGE)
		return ("GetMsg changed the type");

	RemPort(&hi);
	if (FindPort("high") != NULL || FindPort("low") != &lo)
		return ("RemPort high");
	RemPort(&lo);
	if (FindPort("low") != NULL)
		return ("RemPort low");
	return (NULL);
}

int
main()
{
	char *why;
	int ready, done;
	ULONG avail;

	parent = FindTask(NULL);
	avail = AvailMem(0);
	ready = AllocSignal(-1);
	done = AllocSignal(-1);
	if (ready == -1 || done == -1)
		why = "AllocSignal";
	else {
		readymask = 1L << ready;
		donemask = 1L << done;
		if ((why = exchange()) == NULL)
			why = registry();
	}
	if (done != -1)
		FreeSignal(done);
	if (ready != -1)
		FreeSignal(ready);
	if (why == NULL && AvailMem(0) != avail)
		why = "ports leaked memory";
	if (why != NULL) {
		xKPrintf("FAIL messages: %s\n", why);
		return (1);
	}
	KPutStr("PASS messages\n");
	return (0);
}
