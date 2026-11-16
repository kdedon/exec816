/* SPDX-License-Identifier: MIT */
/*
 * Helpers for the lance.device tests: SANA-II requests on one reply
 * port, CopyMem buffer hooks and a counting packet filter.
 */
#ifndef LANTEST_H
#define LANTEST_H

#include "test.h"
#include <exec/exec.h>
#include <devices/sana2.h>

#define LANCENAME	"lance.device"
#define TIMEOUT		200		/* ticks for one request */
#define LNTIMEDOUT	99
#define ADDRLEN		6
#define HDRLEN		14

static struct MsgPort *lnport;
static ULONG lnfiltered;
static struct Hook lnhook;
static struct TagItem lntags[4];

static int
lncopy(to, from, n)
APTR to, from;
ULONG n;
{
	CopyMem(from, to, n);
	return (1);
}

static ULONG
lnfilter(h, ior, data)
struct Hook *h;
struct IOSana2Req *ior;
APTR data;
{
	lnfiltered++;
	return (1);
}

/* A request for OpenDevice; filter adds the counting packet filter. */
static struct IOSana2Req *
lnreq(filter)
int filter;
{
	register struct IOSana2Req *r;

	if (lnport == NULL && (lnport = CreateMsgPort()) == NULL)
		return (NULL);
	r = (struct IOSana2Req *)CreateIORequest(lnport,
	    (ULONG)sizeof(struct IOSana2Req));
	if (r == NULL)
		return (NULL);
	lnhook.h_Entry = lnfilter;
	lntags[0].ti_Tag = S2_CopyToBuff;
	lntags[0].ti_Data = (ULONG)lncopy;
	lntags[1].ti_Tag = S2_CopyFromBuff;
	lntags[1].ti_Data = (ULONG)lncopy;
	lntags[2].ti_Tag = filter ? S2_PacketFilter : TAG_IGNORE;
	lntags[2].ti_Data = (ULONG)&lnhook;
	lntags[3].ti_Tag = TAG_DONE;
	r->ios2_BufferManagement = (APTR)lntags;
	return (r);
}

/* Another request of the opener of base. */
static struct IOSana2Req *
lncopyreq(base)
struct IOSana2Req *base;
{
	register struct IOSana2Req *r;

	r = (struct IOSana2Req *)CreateIORequest(lnport,
	    (ULONG)sizeof(struct IOSana2Req));
	if (r != NULL) {
		r->ios2_Req.io_Device = base->ios2_Req.io_Device;
		r->ios2_Req.io_Unit = base->ios2_Req.io_Unit;
		r->ios2_BufferManagement = base->ios2_BufferManagement;
	}
	return (r);
}

static void
lnset(r, cmd, type, data, len)
register struct IOSana2Req *r;
int cmd;
ULONG type;
APTR data;
ULONG len;
{
	r->ios2_Req.io_Command = cmd;
	r->ios2_Req.io_Flags = 0;
	r->ios2_PacketType = type;
	r->ios2_Data = data;
	r->ios2_DataLength = len;
	r->ios2_WireError = 0;
}

static int
lndo(r, cmd)
struct IOSana2Req *r;
int cmd;
{
	r->ios2_Req.io_Command = cmd;
	return (DoIO((struct IORequest *)r));
}

/* WaitIO with a deadline: LNTIMEDOUT after aborting it. */
static int
lnwait(r)
struct IOSana2Req *r;
{
	register int t;

	for (t = 0; CheckIO((struct IORequest *)r) == NULL; t++) {
		if (t == TIMEOUT) {
			AbortIO((struct IORequest *)r);
			WaitIO((struct IORequest *)r);
			return (LNTIMEDOUT);
		}
		Sleep(1);
	}
	return (WaitIO((struct IORequest *)r));
}

static int
lnsame(a, b, n)
register UBYTE *a, *b;
register int n;
{
	while (n-- > 0)
		if (*a++ != *b++)
			return (0);
	return (1);
}

#endif
