/* SPDX-License-Identifier: MIT */
/*
 * serial.device receive on unit 0, the console line: a pending CMD_READ is
 * filled by the receive interrupt from scripted input, later bytes wait in
 * the ring, an SERF_EOFMODE read ends at its terminator and completes
 * quick, SDCMD_QUERY counts buffered bytes, CMD_CLEAR drops them, and
 * AbortIO ends a pending read.  The input is held until the mark is
 * printed, then fed as fast as the receiver takes it.
 */
/* EMUFLAGS: --input-mark=SERREAD-GO --input=\ih\ie\il\il\io\ia\ib\i.\ix\iy\iz */
#include "test.h"
#include "sertest.h"

static char buf[32];

static char *
run(x)
register struct IOExtSer *x;
{
	int i;

	if (x->io_Baud != SER_DEFBAUD || x->io_ReadLen != 8 ||
	    x->io_StopBits != 1 || (x->io_SerFlags & SERF_XDISABLED) == 0)
		return ("open did not report the console parameters");

	/* Pending until the bytes arrive. */
	sersend(x, CMD_READ, buf, 5L);
	Sleep(2);
	if (CheckIO(x) != NULL)
		return ("read completed with no input");
	xKPrintf("SERREAD-GO\n");
	if (!serwait(x, 500))
		return ("read never completed");
	if (x->IOSer.io_Error != 0 || x->IOSer.io_Actual != 5 ||
	    !sereq(buf, "hello", 5))
		return ("read returned the wrong bytes");

	/* The rest lands in the ring. */
	for (i = 0; i < 500; i++) {
		x->IOSer.io_Command = SDCMD_QUERY;
		DoIO(x);
		if (x->IOSer.io_Actual >= 6)
			break;
		Sleep(1);
	}
	if (x->IOSer.io_Actual != 6)
		return ("query did not count six buffered bytes");

	x->io_SerFlags |= SERF_EOFMODE;
	x->io_TermArray.TermArray0 = 0x2e2e2e2eL;
	x->io_TermArray.TermArray1 = 0x2e2e2e2eL;
	x->IOSer.io_Command = SDCMD_SETPARAMS;
	if (DoIO(x) != 0)
		return ("SETPARAMS for EOF mode failed");
	if (sercmd(x, CMD_READ, buf, 20L) != 0 || x->IOSer.io_Actual != 3 ||
	    !sereq(buf, "ab.", 3))
		return ("EOF-mode read did not stop at its terminator");
	if ((x->IOSer.io_Flags & IOF_QUICK) == 0)
		return ("a buffered read did not complete quick");

	x->IOSer.io_Command = SDCMD_QUERY;
	DoIO(x);
	if (x->IOSer.io_Actual != 3)
		return ("query did not count the three bytes left");
	if (sercmd(x, CMD_CLEAR, NULL, 0L) != 0)
		return ("CMD_CLEAR failed");
	x->IOSer.io_Command = SDCMD_QUERY;
	DoIO(x);
	if (x->IOSer.io_Actual != 0)
		return ("CMD_CLEAR left bytes in the ring");

	/* Abort of a read nothing will satisfy. */
	sersend(x, CMD_READ, buf, 4L);
	Sleep(2);
	if (CheckIO(x) != NULL)
		return ("read completed after CMD_CLEAR");
	AbortIO(x);
	if (WaitIO(x) != IOERR_ABORTED || x->IOSer.io_Actual != 0)
		return ("AbortIO did not end the read with IOERR_ABORTED");
	AbortIO(x);
	if (CheckIO(x) == NULL)
		return ("a second AbortIO disturbed the collected read");
	return (NULL);
}

int
main()
{
	struct IOExtSer *x;
	char *why;
	int err;

	if ((x = seropen(0L, 0, &err)) == NULL) {
		xKPrintf("FAIL serialread: OpenDevice unit 0: %d\n", err);
		return (1);
	}
	why = run(x);
	serclose(x);
	if (why != NULL) {
		xKPrintf("FAIL serialread: %s\n", why);
		return (1);
	}
	xKPrintf("PASS serialread\n");
	return (0);
}
