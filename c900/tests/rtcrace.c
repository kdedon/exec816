/* SPDX-License-Identifier: MIT */
/*
 * battclock carry race: with the chip running 75 times fast, carries land
 * inside reads across a New Year, yet every read is valid and the clock
 * never steps back or skips; torn image pairs were seen and retried.
 */
/* EMUFLAGS: --rtc=2026-12-31T23:58:30 --rtc-ips=20000 */
#include "test.h"
#include <exec/exec.h>
#include <resources/battclock.h>

APTR BattClockBase;

static char *
run()
{
	ULONG t, prev, r0, start;
	int i;

	r0 = BattRetries;
	start = prev = ReadBattClock();
	for (i = 0; i < 600; i++) {
		if ((t = ReadBattClock()) == 0)
			return ("invalid read");
		if (t < prev || t - prev > 1)
			return ("clock stepped back or skipped");
		prev = t;
	}
	xKPrintf("rtcrace: %lu retries, %lu s elapsed\n", BattRetries - r0,
	    t - 1546300710L);
	if (BattRetries == r0)
		return ("no carry landed inside a read");
	if (start >= 1546300800L || t < 1546300800L)
		return ("did not cross into 2027");
	return (NULL);
}

int
main()
{
	char *why;

	BattClockBase = OpenResource(BATTCLOCKNAME);
	if ((why = run()) != NULL) {
		xKPrintf("FAIL rtcrace: %s\n", why);
		return (1);
	}
	KPutStr("PASS rtcrace\n");
	return (0);
}
