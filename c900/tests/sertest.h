/* SPDX-License-Identifier: MIT */
/* Shared serial.device test code. */
#include <exec/exec.h>
#include <devices/serial.h>

/* Open unit with serflags on a fresh port; *err gets OpenDevice's result. */
static struct IOExtSer *
seropen(unit, serflags, err)
ULONG unit;
int serflags;
int *err;
{
	struct MsgPort *port;
	struct IOExtSer *x;

	*err = 1;
	if ((port = CreateMsgPort()) == NULL)
		return (NULL);
	x = (struct IOExtSer *)CreateIORequest(port, sizeof(struct IOExtSer));
	if (x == NULL) {
		DeleteMsgPort(port);
		return (NULL);
	}
	x->io_SerFlags = serflags;
	if ((*err = OpenDevice(SERIALNAME, unit, x, 0)) != 0) {
		DeleteIORequest(x);
		DeleteMsgPort(port);
		return (NULL);
	}
	return (x);
}

/* As Amiga requires, a request still pending is aborted and collected
 * before CloseDevice: a test that fails early may leave one. */
static void
serclose(x)
struct IOExtSer *x;
{
	struct MsgPort *port;

	if (CheckIO(x) == NULL) {
		AbortIO(x);
		WaitIO(x);
	}
	port = x->IOSer.io_Message.mn_ReplyPort;
	CloseDevice(x);
	DeleteIORequest(x);
	DeleteMsgPort(port);
}

/* Collect x within ticks; FALSE when it is still pending. */
static int
serwait(x, ticks)
struct IOExtSer *x;
int ticks;
{
	while (CheckIO(x) == NULL) {
		if (ticks-- <= 0)
			return (0);
		Sleep(1);
	}
	WaitIO(x);
	return (1);
}

static int
sercmd(x, cmd, data, len)
struct IOExtSer *x;
int cmd;
char *data;
ULONG len;
{
	x->IOSer.io_Command = cmd;
	x->IOSer.io_Data = (UBYTE *)data;
	x->IOSer.io_Length = len;
	return (DoIO(x));
}

static void
sersend(x, cmd, data, len)
struct IOExtSer *x;
int cmd;
char *data;
ULONG len;
{
	x->IOSer.io_Command = cmd;
	x->IOSer.io_Data = (UBYTE *)data;
	x->IOSer.io_Length = len;
	SendIO(x);
}

static int
sereq(a, b, n)
char *a, *b;
int n;
{
	while (n-- > 0)
		if (*a++ != *b++)
			return (0);
	return (1);
}

/* The time constant a line's baud-rate generator holds. */
static int
sertc(base)
int base;
{
	return ((inb(base + 0x19) & 0xff) | (inb(base + 0x1b) & 0xff) << 8);
}
