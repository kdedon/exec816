/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Message ports.  Port of lib/exec/portcore.act and platform/altirraos/
 * ports.s; the contract is docs/reference/ports.md.  Queue changes run with
 * VI off, and the PA_SIGNAL notification is posted before VI is restored,
 * so publication and notification are one step for the sender.  The public
 * registry is changed under Forbid.
 */
#include "kernel.h"

/* Publish m on port; the caller has already set its node type. */
static void
post(port, m)
register struct MsgPort *port;
struct Message *m;
{
	register int s;

	s = intoff();
	xAddTail(&port->mp_MsgList, (struct Node *)m);
	if ((port->mp_Flags & PF_ACTION) == PA_SIGNAL)
		ksignal(port->mp_SigTask, 1L << port->mp_SigBit);
	intrest(s);
}

void
xPutMsg(port, m)
struct MsgPort *port;
struct Message *m;
{
	m->mn_Node.ln_Type = NT_MESSAGE;
	post(port, m);
}

struct Message *
xGetMsg(port)
struct MsgPort *port;
{
	register struct Message *m;
	register int s;

	s = intoff();
	m = (struct Message *)xRemHead(&port->mp_MsgList);
	intrest(s);
	return (m);
}

void
xReplyMsg(m)
register struct Message *m;
{
	register struct MsgPort *port;

	if ((port = m->mn_ReplyPort) == NULL) {
		m->mn_Node.ln_Type = NT_FREEMSG;
		return;
	}
	m->mn_Node.ln_Type = NT_REPLYMSG;
	post(port, m);
}

/* Return the head without removing it, waiting while the port is empty. */
struct Message *
xWaitPort(port)
register struct MsgPort *port;
{
	register struct Node *n;

	while ((n = port->mp_MsgList.lh_Head)->ln_Succ == NULL)
		xWait(1L << port->mp_SigBit);
	return ((struct Message *)n);
}

struct MsgPort *
xCreateMsgPort()
{
	register struct MsgPort *port;
	int bit;

	port = (struct MsgPort *)xAllocMem((ULONG)sizeof(struct MsgPort),
	    (ULONG)(MEMF_PUBLIC | MEMF_CLEAR));
	if (port == NULL)
		return (NULL);
	if ((bit = xAllocSignal(-1)) == -1) {
		xFreeMem((APTR)port, (ULONG)sizeof(struct MsgPort));
		return (NULL);
	}
	port->mp_Node.ln_Type = NT_MSGPORT;
	port->mp_Flags = PA_SIGNAL;
	port->mp_SigBit = bit;
	port->mp_SigTask = ThisTask;
	xNewList(&port->mp_MsgList);
	return (port);
}

/* The caller has stopped producers, collected replies and removed the name. */
void
xDeleteMsgPort(port)
struct MsgPort *port;
{
	if (port == NULL)
		return;
	xFreeSignal((int)port->mp_SigBit);
	xFreeMem((APTR)port, (ULONG)sizeof(struct MsgPort));
}

void
xAddPort(port)
register struct MsgPort *port;
{
	port->mp_Node.ln_Type = NT_MSGPORT;
	xNewList(&port->mp_MsgList);
	xForbid();
	xEnqueue(&PortList, (struct Node *)port);
	xPermit();
}

void
xRemPort(port)
struct MsgPort *port;
{
	xForbid();
	xRemove((struct Node *)port);
	xPermit();
}

struct MsgPort *
xFindPort(name)
char *name;
{
	register struct Node *n;

	xForbid();
	n = xFindName(&PortList, name);
	xPermit();
	return ((struct MsgPort *)n);
}
