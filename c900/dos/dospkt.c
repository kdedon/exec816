/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * dos.library packets, as AmigaDOS: a StandardPacket's message carries
 * the DosPacket in ln_Name.  DoPkt waits on the process's pr_MsgPort; a
 * plain Task gets a temporary port (Amiga requires a process).
 */
#include "dosint.h"

LONG
xDoPkt(db, port, action, a1, a2, a3, a4, a5)
DB db;
struct MsgPort *port;
LONG action, a1, a2, a3, a4, a5;
{
	struct StandardPacket sp;
	register struct DosPacket *dp;
	register struct Process *me;
	struct MsgPort *rp;

	if (port == NULL)
		return (dfail(ERROR_DEVICE_NOT_MOUNTED));
	me = dme();
	if (me != NULL)
		rp = &me->pr_MsgPort;
	else if ((rp = CreateMsgPort()) == NULL)
		return (DOSFALSE);
	dp = &sp.sp_Pkt;
	sp.sp_Msg.mn_Node.ln_Name = (char *)dp;
	sp.sp_Msg.mn_ReplyPort = rp;
	dp->dp_Link = &sp.sp_Msg;
	dp->dp_Port = rp;
	dp->dp_Type = action;
	dp->dp_Arg1 = a1;
	dp->dp_Arg2 = a2;
	dp->dp_Arg3 = a3;
	dp->dp_Arg4 = a4;
	dp->dp_Arg5 = a5;
	PutMsg(port, &sp.sp_Msg);
	while (GetMsg(rp) != &sp.sp_Msg)
		WaitPort(rp);
	if (me == NULL)
		DeleteMsgPort(rp);
	else
		me->pr_Result2 = dp->dp_Res2;
	return (dp->dp_Res1);
}

void
xSendPkt(db, dp, port, rp)
DB db;
register struct DosPacket *dp;
struct MsgPort *port, *rp;
{
	dp->dp_Link->mn_Node.ln_Name = (char *)dp;
	dp->dp_Link->mn_ReplyPort = rp;
	dp->dp_Port = rp;
	PutMsg(port, dp->dp_Link);
}

struct DosPacket *
xWaitPkt(db)
DB db;
{
	register struct MsgPort *mp;
	register struct Message *m;

	mp = &dme()->pr_MsgPort;
	while ((m = GetMsg(mp)) == NULL)
		WaitPort(mp);
	return ((struct DosPacket *)m->mn_Node.ln_Name);
}

/* The handler's reply: dp_Port becomes its own port, as on the Amiga. */
void
xReplyPkt(db, dp, res1, res2)
DB db;
register struct DosPacket *dp;
LONG res1, res2;
{
	struct MsgPort *rp;

	rp = dp->dp_Port;
	dp->dp_Port = &dme()->pr_MsgPort;
	dp->dp_Res1 = res1;
	dp->dp_Res2 = res2;
	PutMsg(rp, dp->dp_Link);
}
