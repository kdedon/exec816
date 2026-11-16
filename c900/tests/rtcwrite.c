/* SPDX-License-Identifier: MIT */
/*
 * battclock write: a set reads back, carries across a second, minute and
 * hour boundary, is stored in 24-hour mode, refuses a time past 2077, and
 * ResetBattClock goes to 1978-01-01.  A chip left in 12-hour mode by
 * someone else decodes to 24-hour time.  The clock is then put back to
 * the time it held plus the run.
 */
/* EMUFLAGS: --rtc=2026-11-12T10:20:30 */
#include "test.h"
#include "rtctest.h"

#define T1	1684313998L	/* 2031-05-17 08:59:58 */
#define DAY	86400L

#define STOP		0x80

/* Spin until the clock leaves t; the new value, or 0 after 3 s. */
static ULONG
next(t)
ULONG t;
{
	ULONG t0, n;

	t0 = TickCount;
	while ((n = ReadBattClock()) == t)
		if (TickCount - t0 > 300)
			return (0L);
	return (n);
}

static void
cs(lvl)
int lvl;
{
	outb(P_PCDATA, 0xd0 | (inb(P_PCDATA) & 0x0d) | lvl);
}

static void
strobe(v, bit)
int v, bit;
{
	outb(P_PBDATA, STOP | v);
	outb(P_PBDATA, STOP | v | bit);
	outb(P_PBDATA, STOP | v);
}

/* Write or read raw registers with STOP held; *h10 < 0 reads. */
static void
rawhour(h10, h1)
int *h10, h1;
{
	Disable();
	outb(P_PBDATA, 0);
	outb(P_PBDD, 0);
	cs(0);
	outb(P_PBDATA, STOP);
	if (*h10 >= 0) {
		strobe(4, 0x40);
		strobe(h1, 0x20);
		strobe(5, 0x40);
		strobe(*h10, 0x20);
	} else {
		strobe(5, 0x40);
		outb(P_PBDD, 0x0f);
		outb(P_PBDATA, STOP | 0x10);
		*h10 = inb(P_PBDATA) & 0x0f;
		outb(P_PBDATA, STOP);
		outb(P_PBDD, 0);
	}
	outb(P_PBDATA, 0);
	cs(2);
	Enable();
}

/* Force 12-hour h10/h1 and check the decoded hour. */
static int
hour12(h10, h1, want)
int h10, h1, want;
{
	ULONG t;

	rawhour(&h10, h1);
	t = ReadBattClock();
	return (t != 0 && (int)((t % DAY) / 3600L) == want);
}

static char *
run()
{
	ULONG t;
	int h10;

	if (WriteBattClock(T1) != 0)
		return ("WriteBattClock");
	if ((t = ReadBattClock()) != T1)
		return ("read back");
	if ((t = next(t)) != T1 + 1)
		return ("second carry");
	if ((t = next(t)) != T1 + 2)
		return ("minute and hour carry");

	h10 = -1;
	rawhour(&h10, 0);
	if (h10 != 0x08)
		return ("not 24-hour mode");
	if (!hour12(0x05, 1, 23))
		return ("11 PM");
	if (!hour12(0x01, 2, 0))
		return ("12 AM");
	if (!hour12(0x05, 2, 12))
		return ("12 PM");
	if (!hour12(0x00, 9, 9))
		return ("9 AM");
	if (WriteBattClock(T1) != 0 ||
	    (h10 = -1, rawhour(&h10, 0), h10) != 0x08)
		return ("write after 12-hour mode");

	if (WriteBattClock(BC_MAXTIME + 1) != BCERR_RANGE)
		return ("range");
	if ((t = ReadBattClock()) < T1 || t > T1 + 1)
		return ("refused write changed the clock");

	if (ResetBattClock() != 0)
		return ("ResetBattClock");
	if (next(0L) != 1L)
		return ("reset time");
	return (NULL);
}

int
main()
{
	char *why;
	int bad;

	BattClockBase = OpenResource(BATTCLOCKNAME);
	clksave();
	why = run();
	bad = clkrest() != 0;
	if (why != NULL)
		xKPrintf("FAIL rtcwrite: %s\n", why);
	if (bad)
		KPutStr("FAIL rtcwrite: restore\n");
	if (why != NULL || bad)
		return (1);
	KPutStr("PASS rtcwrite\n");
	return (0);
}
