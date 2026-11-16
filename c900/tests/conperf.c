/* SPDX-License-Identifier: MIT */
/*
 * Console output throughput: lines through KPrintf (exec's RawPutChar, a
 * byte at a time) and through textcon.device CMD_WRITE, first on a
 * cleared screen and then with every line scrolling, timed in ticks and
 * reported as lines per second.  Without a display card the same lines
 * go to the serial line alone.  Each phase is bracketed by a mark
 * written straight to the SCC (not drawn), "@@conperf N", for the
 * emulator's C900_PROF_MARK profiler, and by a write to port
 * 0x0F00 + 2N, as tests/perf.c marks its phases.
 */
#include "test.h"
#include "condisp.h"

#define NLINE		32
#define SCC_STAT	0x0101
#define SCC_DATA	0x0111
#define SCC_RR1		0x0103

static char line[] =
    "conperf: the quick brown fox jumps over the lazy dog\n";

static void
sccput(c)
int c;
{
	register int i;

	for (i = 0; i < 20000 && (inb(SCC_STAT) & 4) == 0; i++)
		;
	outb(SCC_DATA, c);
}

static void
mark(n)
int n;
{
	register char *s;
	register int i;

	/* Queued serial output still going would load the next phase. */
	for (i = 0; i < 300 && (inb(SCC_RR1) & 1) == 0; i++)
		Sleep(1);
	outb(0x0F00 + 2 * n, 0);
	Disable();
	for (s = "@@conperf "; *s != '\0'; s++)
		sccput(*s);
	sccput('0' + n / 10);
	sccput('0' + n % 10);
	sccput('\r');
	sccput('\n');
	Enable();
}

static struct IOStdReq *wr;

static void
conw(s)
char *s;
{
	wr->io_Command = CMD_WRITE;
	wr->io_Data = (APTR)s;
	wr->io_Length = -1;
	DoIO(wr);
}

static ULONG ticks[4];

static void
report(what, i, n)
char *what;
int i, n;
{
	ULONG t;

	t = ticks[i];
	xKPrintf("conperf: %s %u lines %lu ticks", what, n, t);
	if (t != 0)
		xKPrintf(", %lu lines/s", (ULONG)n * 100L / t);
	xKPrintf("\n");
}

int
main()
{
	struct MsgPort *mp;
	struct ConDisplay *cd;
	register int i, rows;
	ULONG t;

	cd = conask(0);
	rows = cd != NULL && cd->cd_Disp != NULL ? cd->cd_Disp->dp_Rows : 25;
	xKPrintf("conperf: card %u mode %u rows %u out %lx\n",
	    cd != NULL ? cd->cd_Card : 0, cd != NULL ? cd->cd_Mode : 0, rows,
	    cd != NULL ? (ULONG)cd->cd_Out : 0L);
	if ((mp = CreateMsgPort()) == NULL ||
	    (wr = (struct IOStdReq *)CreateIORequest(mp,
	    sizeof(struct IOStdReq))) == NULL ||
	    OpenDevice(TEXTCONNAME, CONU_STANDARD, wr, 0) != 0) {
		xKPrintf("FAIL conperf: textcon.device\n");
		return (1);
	}

	/* KPrintf on a cleared screen, no scroll. */
	conw("\033[H\033[J");
	mark(1);
	t = TickCount;
	for (i = 0; i < rows - 1; i++)
		xKPrintf("%s", line);
	ticks[0] = TickCount - t;
	mark(2);
	/* KPrintf, every line scrolling. */
	t = TickCount;
	for (i = 0; i < NLINE; i++)
		xKPrintf("%s", line);
	ticks[1] = TickCount - t;
	mark(3);

	/* CMD_WRITE, the same two. */
	conw("\033[H\033[J");
	mark(4);
	t = TickCount;
	for (i = 0; i < rows - 1; i++)
		conw(line);
	ticks[2] = TickCount - t;
	mark(5);
	t = TickCount;
	for (i = 0; i < NLINE; i++)
		conw(line);
	ticks[3] = TickCount - t;
	mark(6);

	report("kprintf", 0, rows - 1);
	report("kprintf scrolling", 1, NLINE);
	report("write", 2, rows - 1);
	report("write scrolling", 3, NLINE);
	CloseDevice(wr);
	DeleteIORequest(wr);
	DeleteMsgPort(mp);
	xKPrintf("PASS conperf\n");
	return (0);
}
