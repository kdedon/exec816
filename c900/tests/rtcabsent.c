/* SPDX-License-Identifier: MIT */
/*
 * battclock with no chip fitted: reads give 0, sets and resets report
 * BCERR_NOCLOCK, and the tick keeps running.
 */
/* EMUFLAGS: --rtc=none */
#include "test.h"
#include <exec/exec.h>
#include <resources/battclock.h>

APTR BattClockBase;

static char *
run()
{
	ULONG t0;
	int i;

	for (i = 0; i < 3; i++)
		if (ReadBattClock() != 0)
			return ("ReadBattClock");
	if (WriteBattClock(1542018030L) != BCERR_NOCLOCK)
		return ("WriteBattClock");
	if (ResetBattClock() != BCERR_NOCLOCK)
		return ("ResetBattClock");
	if (WriteBattClock(BC_MAXTIME + 1) != BCERR_RANGE)
		return ("range");
	if (ReadBattClock() != 0)
		return ("ReadBattClock after set");
	t0 = TickCount;
	Sleep(10);
	if (TickCount - t0 < 10 || TickCount - t0 > 12)
		return ("tick");
	return (NULL);
}

int
main()
{
	char *why;

	BattClockBase = OpenResource(BATTCLOCKNAME);
	if ((why = run()) != NULL) {
		xKPrintf("FAIL rtcabsent: %s\n", why);
		return (1);
	}
	KPutStr("PASS rtcabsent\n");
	return (0);
}
