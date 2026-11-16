/* SPDX-License-Identifier: MIT */
/*
 * battclock read, at whatever time the clock holds: ReadBattClock agrees
 * with the chip's raw registers decoded independently, and the clock
 * advances by one second at a time.  The seed only makes emulator runs
 * repeatable.
 */
/* EMUFLAGS: --rtc=2026-11-12T10:20:30 */
#include "test.h"
#include "rtctest.h"

static char *
run()
{
	UBYTE r[NREG];
	int f[6], i;
	ULONG t, t1, t2, u, t0;

	t1 = ReadBattClock();
	if (!rawimg(r))
		return ("raw registers never read the same twice");
	t2 = ReadBattClock();
	if (!rawdec(r, &u, f)) {
		KPutStr("rtcread: raw");
		for (i = 0; i < NREG; i++)
			xKPrintf(" %x", (int)r[i]);
		KPutStr("\n");
		return (t1 == 0 && t2 == 0 ? "no valid time (clock unset?)" :
		    "valid read of an invalid image");
	}
	xKPrintf("rtcread: %lu = %d-%02d-%02d %02d:%02d:%02d\n", t1,
	    f[0], f[1], f[2], f[3], f[4], f[5]);
	if (t1 == 0 || t2 == 0)
		return ("valid image read as no time");
	if (t2 < t1 || t2 - t1 > 1 || u < t1 || u > t2)
		return ("decoded time disagrees with the registers");
	/* The emulator's clock runs on executed instructions: spin. */
	t0 = TickCount;
	while ((t = ReadBattClock()) == t2)
		if (TickCount - t0 > 3 * RTCHZ)
			return ("clock not running");
	if (t != (t2 == BC_MAXTIME ? 0L : t2 + 1))
		return ("second step");
	return (NULL);
}

int
main()
{
	char *why;

	BattClockBase = OpenResource(BATTCLOCKNAME);
	if ((why = run()) != NULL) {
		xKPrintf("FAIL rtcread: %s\n", why);
		return (1);
	}
	KPutStr("PASS rtcread\n");
	return (0);
}
