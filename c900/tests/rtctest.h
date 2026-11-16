/* SPDX-License-Identifier: MIT */
/*
 * Shared by the battclock tests: raw MSM58321 register access beside the
 * driver, a decoder independent of the driver's, and saving the clock
 * across a test that sets it.
 */
#include <exec/exec.h>
#include <resources/battclock.h>

APTR BattClockBase;

#define P_PBDATA	0x001d
#define P_PCDATA	0x001f
#define P_PBDD		0x0057
#define RB_READ		0x10
#define RB_WRITE	0x20
#define RB_ADWR		0x40
#define RB_STOP		0x80

#define NREG		13
#define RTCHZ		100L	/* TickCount rate */

static int rawpb, rawdd;

/* Select the chip with interrupts off; hold is 0 or RB_STOP. */
static void
rawbeg(hold)
int hold;
{
	Disable();
	rawpb = inb(P_PBDATA);
	rawdd = inb(P_PBDD);
	outb(P_PBDATA, 0);
	outb(P_PBDD, 0);
	outb(P_PCDATA, 0xd0 | (inb(P_PCDATA) & 0x0d));
	outb(P_PBDATA, hold);
}

static void
rawend()
{
	outb(P_PBDATA, 0);
	outb(P_PCDATA, 0xd0 | (inb(P_PCDATA) & 0x0d) | 0x02);
	outb(P_PBDD, rawdd);
	outb(P_PBDATA, rawpb);
	Enable();
}

static void
rawadr(reg, hold)
int reg, hold;
{
	outb(P_PBDATA, hold | reg);
	outb(P_PBDATA, hold | reg | RB_ADWR);
	outb(P_PBDATA, hold | reg);
}

static int
rawrd(reg, hold)
int reg, hold;
{
	int v;

	rawadr(reg, hold);
	outb(P_PBDD, 0x0f);
	outb(P_PBDATA, hold | RB_READ);
	v = inb(P_PBDATA) & 0x0f;
	outb(P_PBDATA, hold);
	outb(P_PBDD, 0);
	return (v);
}

static void
rawwr(reg, v, hold)
int reg, v, hold;
{
	rawadr(reg, hold);
	outb(P_PBDATA, hold | v);
	outb(P_PBDATA, hold | v | RB_WRITE);
	outb(P_PBDATA, hold | v);
}

/* The 13 time registers, two passes agreeing; 0 if they never do. */
static int
rawimg(r)
UBYTE *r;
{
	UBYTE b[NREG];
	int i, try, ok;

	for (try = 0; try < 4; try++) {
		rawbeg(0);
		for (i = 0; i < NREG; i++)
			r[i] = rawrd(i, 0);
		for (i = 0; i < NREG; i++)
			b[i] = rawrd(i, 0);
		rawend();
		for (ok = 1, i = 0; i < NREG; i++)
			if (r[i] != b[i])
				ok = 0;
		if (ok)
			return (1);
	}
	return (0);
}

static int
isleap(y)
int y;
{
	return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

static int
rtcmlen(m, y)
int m, y;
{
	static UBYTE ml[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	return (m == 2 && isleap(y) ? 29 : ml[m - 1]);
}

/*
 * A register image as seconds since 1978-01-01, counting day by day
 * rather than by the driver's formula; 0 when a field is out of range.
 * f gets year, month, day, hour, minute and second.
 */
static int
rawdec(r, tp, f)
UBYTE *r;
ULONG *tp;
int *f;
{
	ULONG days;
	int h, i;

	if (r[0] > 9 || r[1] > 5 || r[2] > 9 || r[3] > 5 || r[4] > 9 ||
	    r[7] > 9 || r[9] > 9 || r[10] > 1 || r[11] > 9 || r[12] > 9)
		return (0);
	h = (r[5] & 3) * 10 + r[4];
	if (r[5] & 8) {
		if (h > 23)
			return (0);
	} else {
		if (h < 1 || h > 12)
			return (0);
		h = h % 12 + (r[5] & 4 ? 12 : 0);
	}
	f[0] = r[12] * 10 + r[11];
	f[0] += f[0] >= 78 ? 1900 : 2000;
	f[1] = r[10] * 10 + r[9];
	f[2] = (r[8] & 3) * 10 + r[7];
	f[3] = h;
	f[4] = r[3] * 10 + r[2];
	f[5] = r[1] * 10 + r[0];
	if (f[1] < 1 || f[1] > 12 || f[2] < 1 || f[2] > rtcmlen(f[1], f[0]))
		return (0);
	days = f[2] - 1;
	for (i = 1; i < f[1]; i++)
		days += rtcmlen(i, f[0]);
	for (i = 1978; i < f[0]; i++)
		days += isleap(i) ? 366 : 365;
	*tp = ((days * 24 + h) * 60 + f[4]) * 60 + f[5];
	return (1);
}

/* The clock as found, at a second boundary, and the tick it was seen. */
static ULONG clkorig, clkt0;

/*
 * Remember the clock.  An invalid image (or 1978-01-01 00:00:00) is
 * remembered as 0, so it is put back as 1978-01-01 plus the run.
 */
static void
clksave()
{
	ULONG t, n;

	t = ReadBattClock();
	clkt0 = TickCount;
	if (t != 0)
		while ((n = ReadBattClock()) == t && TickCount - clkt0 <= 3 * RTCHZ)
			;
	clkorig = t == 0 || n == t ? t : n;
	clkt0 = TickCount;
	if (t == 0)
		KPutStr("battclock held no valid time: restoring 1978-01-01\n");
}

/*
 * Put the clock back to its saved time plus the whole seconds since, set
 * on a tick boundary so the second keeps its phase to about one tick.
 * 0 on success.
 */
static int
clkrest()
{
	ULONG n, t;

	n = (TickCount - clkt0) / RTCHZ + 1;
	while (TickCount - clkt0 < n * RTCHZ)
		;
	if (WriteBattClock(clkorig + n) != 0)
		return (-1);
	t = ReadBattClock();
	xKPrintf("battclock restored: %lu (saved + %lu s)\n", t, n);
	return (t == clkorig + n || t == clkorig + n + 1 ? 0 : -1);
}
