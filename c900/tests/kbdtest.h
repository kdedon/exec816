/* SPDX-License-Identifier: MIT */
/* Shared keyboard.device test code. */
#include <exec/exec.h>
#include <devices/keyboard.h>

static struct IOStdReq *
kbdopen(unit, err)
ULONG unit;
int *err;
{
	struct MsgPort *port;
	struct IOStdReq *r;

	*err = 1;
	if ((port = CreateMsgPort()) == NULL)
		return (NULL);
	r = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
	if (r == NULL) {
		DeleteMsgPort(port);
		return (NULL);
	}
	if ((*err = OpenDevice(KEYBOARDNAME, unit, r, 0)) != 0) {
		DeleteIORequest(r);
		DeleteMsgPort(port);
		return (NULL);
	}
	return (r);
}

static void
kbdclose(r)
struct IOStdReq *r;
{
	struct MsgPort *port;

	port = r->io_Message.mn_ReplyPort;
	CloseDevice(r);
	DeleteIORequest(r);
	DeleteMsgPort(port);
}

static int
kbdcmd(r, cmd, data, len)
struct IOStdReq *r;
int cmd;
APTR data;
ULONG len;
{
	r->io_Command = cmd;
	r->io_Data = (UBYTE *)data;
	r->io_Length = len;
	return (DoIO(r));
}

static void
kbdsend(r, cmd, data, len)
struct IOStdReq *r;
int cmd;
APTR data;
ULONG len;
{
	r->io_Command = cmd;
	r->io_Data = (UBYTE *)data;
	r->io_Length = len;
	SendIO(r);
}
