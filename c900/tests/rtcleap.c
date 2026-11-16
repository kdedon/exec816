/* SPDX-License-Identifier: MIT */
/*
 * battclock calendar: 2026 rolls into 2027, 28 February rolls to 1 March
 * in that common year and to 29 February in leap years 2028 and 2000, 1999
 * rolls into 2000 through the two-digit year, and 2077-12-31 23:59:59 is
 * the last time decoded.  The clock is then put back to the time it held
 * plus the run; an invalid time is replaced by 1978-01-01 plus the run.
 *
 * The common year is reached by a year carry, and 28 February is then set
 * directly in the chip, keeping D10's leap selection as the carry left it.
 * The chip reads 00 as the Gregorian rule; the emulator reads it as "this
 * year is leap" and counts it down at each carry.  Both then give 2027 a
 * 28-day February.
 */
/* EMUFLAGS: --rtc=2026-11-12T10:20:30 */
#include "test.h"
#include "rtctest.h"

#define DEC2026	1546300798L	/* 2026-12-31 23:59:58 */
#define MAR2027	1551398400L	/* 2027-03-01 00:00:00 */
#define FEB2028	1582934398L	/* 2028-02-28 23:59:58 */
#define FEB2000	699321598L	/* 2000-02-28 23:59:58 */
#define DEC1999	694223998L	/* 1999-12-31 23:59:58 */

/* Spin until the clock reads at least want; the value, or 0 after 4 s. */
static ULONG
reach(want)
ULONG want;
{
	ULONG t0, t;

	t0 = TickCount;
	while ((t = ReadBattClock()) < want)
		if (TickCount - t0 > 4 * RTCHZ)
			return (0L);
	return (t);
}

/* Set t, then check that the clock reaches t + 2 in one step each. */
static int
roll(t)
ULONG t;
{
	if (WriteBattClock(t) != 0)
		return (0);
	return (reach(t + 1) == t + 1 && reach(t + 2) == t + 2);
}

/* In the chip: 28 February 23:59:58 of the year it holds, 24-hour. */
static void
setfeb28()
{
	static UBYTE r[] = { 8, 5, 9, 5, 3, 0x0a, 0, 8, 2, 2, 0 };
	int i;

	rawbeg(RB_STOP);
	r[8] = (rawrd(8, RB_STOP) & 0x0c) | 2;
	for (i = 0; i < sizeof (r); i++)
		if (i != 6)
			rawwr(i, (int)r[i], RB_STOP);
	rawwr(13, 0, RB_STOP);
	rawend();
}

static char *
run()
{
	ULONG t;

	if (!roll(DEC2026))
		return ("2026 to 2027");
	setfeb28();
	if ((t = reach(MAR2027)) < MAR2027 || t > MAR2027 + 1)
		return ("2027-02-28 to 03-01");
	if (!roll(FEB2028))
		return ("2028-02-28 to 02-29");
	if (!roll(FEB2000))
		return ("2000-02-28 to 02-29");
	if (!roll(DEC1999))
		return ("1999 to 2000");
	if (WriteBattClock(BC_MAXTIME) != 0 || ReadBattClock() != BC_MAXTIME)
		return ("2077-12-31 23:59:59");
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
		xKPrintf("FAIL rtcleap: %s\n", why);
	if (bad)
		KPutStr("FAIL rtcleap: restore\n");
	if (why != NULL || bad)
		return (1);
	KPutStr("PASS rtcleap\n");
	return (0);
}
