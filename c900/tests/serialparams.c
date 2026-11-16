/* SPDX-License-Identifier: MIT */
/*
 * serial.device parameters and units: SDCMD_QUERY reports the modem
 * lines, the console line refuses a rate or framing change, SETPARAMS on
 * line 1 programs PCLK/(32*baud) - 2 into the baud-rate generator, bad
 * parameters and a busy unit are refused, CMD_RESET restores 9600, opens
 * are exclusive unless shared, and units kboot did not find (the LR
 * board's, absent here) do not open.
 */
#include "test.h"
#include "sertest.h"

#define LINE0	0x0100
#define LINE1	0x0120

static char buf[8];

static int
setp(x)
struct IOExtSer *x;
{
	x->IOSer.io_Command = SDCMD_SETPARAMS;
	return (DoIO(x));
}

static char *
console(x)
register struct IOExtSer *x;
{
	x->IOSer.io_Command = SDCMD_QUERY;
	if (DoIO(x) != 0)
		return ("query failed");
	if ((x->io_Status & (SERST_CTS | SERST_CD | SERST_RTS | SERST_DTR)) !=
	    (SERST_CTS | SERST_CD | SERST_RTS | SERST_DTR))
		return ("query did not report the console's modem lines");
	x->io_Baud = 19200L;
	if (setp(x) != SerErr_BaudMismatch)
		return ("the console line accepted a rate change");
	x->io_Baud = SER_DEFBAUD;
	x->io_ReadLen = 7;
	if (setp(x) != SerErr_InvParam)
		return ("the console line accepted 7 bits");
	x->io_ReadLen = 8;
	x->io_RBufLen = 1024L;
	if (setp(x) != 0)
		return ("the console line refused its own parameters");
	if (sertc(LINE0) != 17 && sertc(LINE0) != 11)
		return ("the console's time constant changed");
	return (NULL);
}

static char *
line1(x)
register struct IOExtSer *x;
{
	struct IOExtSer *y;
	ULONG pclk;
	int err;

	pclk = sertc(LINE0) == 11 ? 4000000L : 6000000L;
	x->io_Baud = 1200L;
	x->io_ReadLen = x->io_WriteLen = 7;
	x->io_StopBits = 2;
	x->io_SerFlags |= SERF_PARTY_ON;
	if (setp(x) != 0)
		return ("SETPARAMS 1200 7E2 failed");
	if (sertc(LINE1) != (int)(pclk / (32L * 1200L) - 2))
		return ("1200 baud programmed the wrong time constant");
	x->io_Baud = 300000L;
	if (setp(x) != SerErr_BaudMismatch)
		return ("an unreachable rate was accepted");
	x->io_Baud = 0L;
	if (setp(x) != SerErr_BaudMismatch)
		return ("rate 0 was accepted");
	x->io_Baud = 2400L;
	x->io_StopBits = 3;
	if (setp(x) != SerErr_InvParam)
		return ("3 stop bits were accepted");
	x->io_StopBits = 1;
	x->io_SerFlags |= SERF_7WIRE;
	if (setp(x) != SerErr_InvParam)
		return ("SERF_7WIRE was accepted");
	x->io_SerFlags &= ~SERF_7WIRE;
	x->io_ExtFlags = SEXTF_MSPON;
	if (setp(x) != SerErr_InvParam)
		return ("mark/space parity was accepted");
	x->io_ExtFlags = 0;
	x->io_RBufLen = 100000L;
	if (setp(x) != SerErr_InvParam)
		return ("an oversized buffer was accepted");
	x->io_RBufLen = SER_DEFBUF;
	if (sertc(LINE1) != (int)(pclk / (32L * 1200L) - 2))
		return ("a refused SETPARAMS changed the line");

	/* Busy while a read is queued. */
	if ((y = seropen(1L, SERF_SHARED, &err)) != NULL) {
		serclose(y);
		return ("a shared open joined an exclusive one");
	}
	if (err != SerErr_DevBusy)
		return ("a second open did not report SerErr_DevBusy");
	sersend(x, CMD_READ, buf, 4L);
	Sleep(1);
	y = (struct IOExtSer *)CreateIORequest(
	    x->IOSer.io_Message.mn_ReplyPort, sizeof(struct IOExtSer));
	if (y == NULL)
		return ("no request");
	*y = *x;
	y->IOSer.io_Command = SDCMD_SETPARAMS;
	err = DoIO(y);
	y->IOSer.io_Command = CMD_RESET;
	if (err != SerErr_DevBusy) {
		DoIO(y);
		DeleteIORequest(y);
		return ("SETPARAMS with a read queued was accepted");
	}
	/* CMD_RESET aborts the read and restores 9600. */
	if (DoIO(y) != 0) {
		DeleteIORequest(y);
		return ("CMD_RESET failed");
	}
	DeleteIORequest(y);
	if (WaitIO(x) != IOERR_ABORTED)
		return ("CMD_RESET did not abort the queued read");
	if (sertc(LINE1) != sertc(LINE0))
		return ("CMD_RESET did not restore the console's 9600");
	x->IOSer.io_Command = 99;
	if (DoIO(x) != IOERR_NOCMD)
		return ("an unknown command was accepted");
	x->IOSer.io_Command = CMD_STOP;
	if (DoIO(x) != IOERR_NOCMD)
		return ("CMD_STOP was accepted");
	return (NULL);
}

static char *
units()
{
	struct IOExtSer *x, *y;
	struct IORequest *r;
	int err, u;

	for (u = 2; u <= 6; u++) {
		if ((x = seropen((ULONG)u, 0, &err)) != NULL) {
			serclose(x);
			return ("an absent line opened");
		}
		if (err != IOERR_OPENFAIL)
			return ("an absent line did not report IOERR_OPENFAIL");
	}
	if ((x = seropen(1L, SERF_SHARED, &err)) == NULL)
		return ("shared open failed");
	if ((y = seropen(1L, SERF_SHARED, &err)) == NULL) {
		serclose(x);
		return ("a second shared open failed");
	}
	serclose(y);
	y = seropen(1L, 0, &err);
	serclose(x);
	if (y != NULL) {
		serclose(y);
		return ("an exclusive open joined a shared one");
	}
	r = CreateIORequest(CreateMsgPort(), sizeof(struct IOStdReq));
	if (r == NULL)
		return ("no request");
	err = OpenDevice(SERIALNAME, 1L, r, 0);
	DeleteMsgPort(r->io_Message.mn_ReplyPort);
	DeleteIORequest(r);
	if (err != IOERR_BADLENGTH)
		return ("an IOStdReq-sized request opened");
	return (NULL);
}

int
main()
{
	struct IOExtSer *x;
	char *why;
	int err;
	ULONG before;

	before = AvailMem(0);
	if ((x = seropen(0L, 0, &err)) == NULL) {
		xKPrintf("FAIL serialparams: OpenDevice unit 0: %d\n", err);
		return (1);
	}
	why = console(x);
	serclose(x);
	if (why == NULL) {
		if ((x = seropen(1L, 0, &err)) == NULL)
			why = "OpenDevice unit 1 failed";
		else {
			why = line1(x);
			serclose(x);
		}
	}
	if (why == NULL)
		why = units();
	if (why == NULL && AvailMem(0) != before)
		why = "memory leaked";
	if (why != NULL) {
		xKPrintf("FAIL serialparams: %s\n", why);
		return (1);
	}
	xKPrintf("PASS serialparams\n");
	return (0);
}
