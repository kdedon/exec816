/* SPDX-License-Identifier: MIT */
/*
 * serial.device transmit on unit 0, the console line: CMD_WRITE of a
 * counted and of a NUL-terminated buffer completes, a write that fits the
 * idle transmitter completes quick, a write longer than one pump burst
 * stays pending and finishes, AbortIO ends a partly sent write,
 * SDCMD_BREAK lasts its io_BrkTime, and CMD_FLUSH aborts a queued break
 * and the write behind it.  The PASS line itself reaches the transcript
 * through CMD_WRITE, after a long write.  Timing holds at the line's real
 * rate as well as on the emulator, which sends at once.
 */
#include "test.h"
#include "sertest.h"

static char line[] = "serialwrite: counted\r\n";
static char nul[] = "serialwrite: terminated\r\n";
static char big[160];
static char tail[] = "\r\nPASS serialwrite\r\n";

/* Wait, bounded, until line 0 has sent everything (RR1 All Sent). */
static void
drain()
{
	register int i;

	for (i = 0; i < 50 && (inb(0x0103) & 1) == 0; i++)
		Sleep(1);
}

/* A long write ending in text, as one buffer. */
static int
fillbig(end)
char *end;
{
	register int i, n;

	for (i = 0; i < 100; i++)
		big[i] = 'a' + i % 26;
	for (n = 0; end[n] != 0; n++)
		big[i++] = end[n];
	return (i);
}

static char *
run(x, y, z)
register struct IOExtSer *x, *y, *z;
{
	int i, n;
	ULONG t0, dt;

	if (sercmd(x, CMD_WRITE, line, (ULONG)(sizeof(line) - 1)) != 0 ||
	    x->IOSer.io_Actual != sizeof(line) - 1)
		return ("counted write failed");
	if (sercmd(x, CMD_WRITE, nul, 0xFFFFFFFFL) != 0 ||
	    x->IOSer.io_Actual != sizeof(nul) - 1)
		return ("NUL-terminated write failed");

	/* Quick only when the transmitter takes every byte at once: an empty
	 * write, or one byte once the line is idle. */
	if (sercmd(x, CMD_WRITE, line, 0L) != 0 ||
	    (x->IOSer.io_Flags & IOF_QUICK) == 0)
		return ("an empty write did not complete quick");
	drain();
	if (sercmd(x, CMD_WRITE, "\r", 1L) != 0 || x->IOSer.io_Actual != 1 ||
	    (x->IOSer.io_Flags & IOF_QUICK) == 0)
		return ("a one-byte write to an idle line did not complete quick");

	/* Longer than a burst: pending, then finished by the tick. */
	n = fillbig("\r\n");
	sersend(x, CMD_WRITE, big, (ULONG)n);
	if (CheckIO(x) != NULL)
		return ("a long write completed at once");
	if (!serwait(x, 50))
		return ("a long write never completed");
	if (x->IOSer.io_Error != 0 || x->IOSer.io_Actual != n)
		return ("a long write ended short");

	/* Abort part way, once some bytes have gone. */
	sersend(x, CMD_WRITE, big, (ULONG)n);
	for (i = 0; i < 20 && x->IOSer.io_Actual == 0; i++)
		Sleep(1);
	AbortIO(x);
	if (WaitIO(x) != IOERR_ABORTED || x->IOSer.io_Actual == 0 ||
	    x->IOSer.io_Actual >= n)
		return ("AbortIO did not end a partly sent write");
	if (sercmd(x, CMD_WRITE, "\r\n", 2L) != 0)
		return ("write after an abort failed");

	/* A 50 ms break: 6 ticks, plus at most one to start and end it. */
	x->io_BrkTime = 50000L;
	x->IOSer.io_Command = SDCMD_SETPARAMS;
	if (DoIO(x) != 0)
		return ("SETPARAMS of io_BrkTime failed");
	x->IOSer.io_Command = SDCMD_BREAK;
	t0 = TickCount;
	if (DoIO(x) != 0)
		return ("SDCMD_BREAK failed");
	dt = TickCount - t0;
	if (dt < 5 || dt > 8) {
		xKPrintf("break took %lu ticks\n", dt);
		return ("SDCMD_BREAK did not last io_BrkTime");
	}

	/* Flush a queued break and the write behind it. */
	x->IOSer.io_Command = SDCMD_BREAK;
	SendIO(x);
	sersend(y, CMD_WRITE, line, (ULONG)(sizeof(line) - 1));
	Sleep(1);
	if (CheckIO(x) != NULL || CheckIO(y) != NULL)
		return ("break or queued write completed early");
	z->IOSer.io_Command = SDCMD_QUERY;
	DoIO(z);
	if ((z->io_Status & SERST_WROTEBRK) == 0)
		return ("query did not report the break");
	if (sercmd(z, CMD_FLUSH, NULL, 0L) != 0)
		return ("CMD_FLUSH failed");
	if (WaitIO(x) != IOERR_ABORTED)
		return ("CMD_FLUSH did not abort the break");
	if (WaitIO(y) != IOERR_ABORTED || y->IOSer.io_Actual != 0)
		return ("CMD_FLUSH did not abort the queued write");
	z->IOSer.io_Command = SDCMD_QUERY;
	DoIO(z);
	if (z->io_Status & SERST_WROTEBRK)
		return ("the break outlived CMD_FLUSH");
	return (NULL);
}

int
main()
{
	struct IOExtSer *x, *y, *z;
	char *why;
	int err, n;

	if ((x = seropen(0L, SERF_SHARED, &err)) == NULL ||
	    (y = seropen(0L, SERF_SHARED, &err)) == NULL ||
	    (z = seropen(0L, SERF_SHARED, &err)) == NULL) {
		xKPrintf("FAIL serialwrite: OpenDevice unit 0: %d\n", err);
		return (1);
	}
	why = run(x, y, z);
	if (why != NULL) {
		xKPrintf("FAIL serialwrite: %s\n", why);
		serclose(z);
		serclose(y);
		serclose(x);
		return (1);
	}
	n = fillbig(tail);
	if (sercmd(x, CMD_WRITE, big, (ULONG)n) != 0 ||
	    x->IOSer.io_Actual != n) {
		xKPrintf("FAIL serialwrite: the final write failed\n");
		return (1);
	}
	serclose(z);
	serclose(y);
	serclose(x);
	return (0);
}
