/* SPDX-License-Identifier: MIT */
/*
 * parallel.device on the Centronics port.  Opens are exclusive unless
 * shared; SETPARAMS checks its fields; a write either reaches a printer
 * whole or, with none attached (BUSY high), ends in ParErr_LineErr within
 * io_Timeout; AbortIO ends a pending write; port B is left at 0 and the
 * battery clock still reads; the driver Task and its stack go away after
 * the last close.  With a printer, an EOF-mode write stops at its
 * terminator.  The sim attaches a printer and checks what it printed.
 */
/* PRINTER: parallel: hello */
#include "test.h"
#include <exec/exec.h>
#include <devices/parallel.h>
#include <resources/battclock.h>

APTR BattClockBase;

static char hello[] = "parallel: hello\r\n";
static char eof[] = "AB.CD";
static char fill[250];

static struct IOExtPar *
paropen(flags, err)
int flags;
int *err;
{
	struct MsgPort *port;
	struct IOExtPar *x;

	*err = 1;
	if ((port = CreateMsgPort()) == NULL)
		return (NULL);
	x = (struct IOExtPar *)CreateIORequest(port, sizeof(struct IOExtPar));
	if (x == NULL) {
		DeleteMsgPort(port);
		return (NULL);
	}
	x->io_ParFlags = flags;
	if ((*err = OpenDevice(PARALLELNAME, 0L, x, 0)) != 0) {
		DeleteIORequest(x);
		DeleteMsgPort(port);
		return (NULL);
	}
	return (x);
}

static void
parclose(x)
struct IOExtPar *x;
{
	struct MsgPort *port;

	if (CheckIO(x) == NULL) {
		AbortIO(x);
		WaitIO(x);
	}
	port = x->IOPar.io_Message.mn_ReplyPort;
	CloseDevice(x);
	DeleteIORequest(x);
	DeleteMsgPort(port);
}

static int
parcmd(x, cmd, data, len)
struct IOExtPar *x;
int cmd;
char *data;
ULONG len;
{
	x->IOPar.io_Command = cmd;
	x->IOPar.io_Data = (UBYTE *)data;
	x->IOPar.io_Length = len;
	return (DoIO(x));
}

static char *
run(x)
register struct IOExtPar *x;
{
	struct IOExtPar *y;
	ULONG t0, dt, c0, c1;
	int err, i;

	if (x->io_Timeout != PAR_DEFTIMEOUT || x->io_ParFlags != 0)
		return ("open did not report the defaults");
	if ((y = paropen(PARF_SHARED, &err)) != NULL || err != ParErr_DevBusy)
		return ("an exclusive open let a second one in");

	x->io_Timeout = 0;
	x->IOPar.io_Command = PDCMD_SETPARAMS;
	if (DoIO(x) != ParErr_InvParam)
		return ("a zero io_Timeout was accepted");
	x->io_Timeout = 300000L;
	x->io_PExtFlags = 1;
	if (DoIO(x) != ParErr_InvParam)
		return ("io_PExtFlags was accepted");
	x->io_PExtFlags = 0;
	if (DoIO(x) != 0 || x->io_Timeout != 300000L)
		return ("SETPARAMS of io_Timeout failed");
	if (parcmd(x, CMD_READ, fill, 1L) != IOERR_NOCMD)
		return ("CMD_READ was not IOERR_NOCMD");
	if (parcmd(x, CMD_WRITE, hello, 0L) != 0 ||
	    (x->IOPar.io_Flags & IOF_QUICK) == 0)
		return ("an empty write did not complete quick");
	x->IOPar.io_Command = PDCMD_QUERY;
	if (DoIO(x) != 0)
		return ("PDCMD_QUERY failed");
	xKPrintf("parallel: query status %d\n", x->io_Status);

	c0 = ReadBattClock();
	t0 = TickCount;
	err = parcmd(x, CMD_WRITE, hello, (ULONG)(sizeof(hello) - 1));
	dt = TickCount - t0;
	xKPrintf("parallel: wrote %lu of %d bytes, error %d, %lu ticks\n",
	    x->IOPar.io_Actual, sizeof(hello) - 1, err, dt);
	if (err == 0) {
		if (x->IOPar.io_Actual != sizeof(hello) - 1)
			return ("a write without error ended short");
	} else if (err == ParErr_LineErr) {
		if (x->IOPar.io_Actual >= sizeof(hello) - 1 || dt < 30 ||
		    dt > 60)
			return ("a write with no printer did not time out in io_Timeout");
	} else
		return ("a write failed with an unexpected error");
	if (err == 0 && ((inb(0x001d) & 0xff) != 0 || (inb(0x0057) & 0xff) != 0))
		return ("port B was not left as zero outputs");
	c1 = ReadBattClock();
	if (c0 != 0 && (c1 < c0 || c1 - c0 > 60))
		return ("the battery clock went wrong around a write");

	if (err == 0) {
		x->io_ParFlags = PARF_EOFMODE;
		x->io_PTermArray.PTermArray0 = 0x2e2e2e2eL;
		x->io_PTermArray.PTermArray1 = 0x2e2e2e2eL;
		x->IOPar.io_Command = PDCMD_SETPARAMS;
		if (DoIO(x) != 0)
			return ("SETPARAMS for EOF mode failed");
		if (parcmd(x, CMD_WRITE, eof, 5L) != 0 ||
		    x->IOPar.io_Actual != 3)
			return ("an EOF-mode write did not stop at its terminator");
		x->io_ParFlags = 0;
		x->IOPar.io_Command = PDCMD_SETPARAMS;
		if (DoIO(x) != 0)
			return ("SETPARAMS leaving EOF mode failed");
		if (parcmd(x, CMD_WRITE, "\r\n", 0xFFFFFFFFL) != 0 ||
		    x->IOPar.io_Actual != 2)
			return ("a NUL-terminated write failed");
	}

	/* Abort a write part way. */
	for (i = 0; i < sizeof(fill); i++)
		fill[i] = '-';
	x->IOPar.io_Command = CMD_WRITE;
	x->IOPar.io_Data = (UBYTE *)fill;
	x->IOPar.io_Length = sizeof(fill);
	t0 = TickCount;
	SendIO(x);
	Sleep(1);
	dt = TickCount - t0;
	AbortIO(x);
	err = WaitIO(x);
	if (err != IOERR_ABORTED || x->IOPar.io_Actual >= sizeof(fill)) {
		xKPrintf("abort after %lu ticks: error %d, %lu bytes\n", dt, err,
		    x->IOPar.io_Actual);
		return ("AbortIO did not end a pending write");
	}
	return (NULL);
}

int
main()
{
	struct IOExtPar *x, *y;
	ULONG avail;
	char *why;
	int err, i;

	BattClockBase = OpenResource(BATTCLOCKNAME);
	avail = AvailMem(MEMF_STACK);
	if ((x = paropen(0, &err)) == NULL) {
		xKPrintf("FAIL parallel: OpenDevice: %d\n", err);
		return (1);
	}
	if (FindTask(PARALLELNAME) == NULL)
		why = "no driver Task while open";
	else
		why = run(x);
	parclose(x);
	if (why == NULL) {
		if ((x = paropen(PARF_SHARED, &err)) == NULL ||
		    (y = paropen(PARF_SHARED, &err)) == NULL)
			why = "two shared opens failed";
		else {
			parclose(y);
			parclose(x);
		}
	}
	for (i = 0; why == NULL && i < 10 && FindTask(PARALLELNAME) != NULL;
	    i++)
		Sleep(1);
	if (why == NULL && (FindTask(PARALLELNAME) != NULL ||
	    AvailMem(MEMF_STACK) != avail))
		why = "the driver Task or its stack outlived the last close";
	if (why != NULL) {
		xKPrintf("FAIL parallel: %s\n", why);
		return (1);
	}
	xKPrintf("PASS parallel\n");
	return (0);
}
