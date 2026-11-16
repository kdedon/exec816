/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * battclock: the OKI MSM58321 clock, bit-banged through CIO U70.
 *
 *	PB0-PB3	D0-D3 (the chip drives them while READ is high)
 *	PB4	READ		PB5	WRITE
 *	PB6	ADDR WRITE	PB7	STOP (level)
 *	PC1	/CS (active low)
 *
 * Port B is shared with the Centronics data lines and the CIO with the
 * tick and the keyboard; docs/hardware.md "CIO U70 sharing" has the rules.
 * A transaction runs under Forbid with /CS asserted throughout and puts
 * Port B's data and direction back afterwards.  Port C is changed only
 * under intoff, with D7-D4 write-protecting every bit but PC1.
 *
 * The calls are battclock.resource's vectors, each with the resource base
 * as its first argument.
 *
 * The chip's BUSY line is not wired, so a read takes the 13 time
 * registers twice and accepts them only when both passes agree.  A set
 * holds STOP, reloads the registers, resets the divider and reads back
 * before releasing STOP.
 */
#include "kernel.h"
#include <resources/battclock.h>

#define P_MCCR		0x0003
#define P_PCDD		0x000d
#define P_PBDATA	0x001d
#define P_PCDATA	0x001f
#define P_PBMS		0x0051
#define P_PBDD		0x0057

#define MCCR_PBE	0x80
#define PB_D		0x0f
#define PB_READ		0x10
#define PB_WRITE	0x20
#define PB_ADWR		0x40
#define PB_STOP		0x80
#define PC_CS		0x02
#define PC_WPCS		0xd0	/* write-protect PC0, PC2, PC3 */

#define R_S1		0
#define R_S10		1
#define R_MI1		2
#define R_MI10		3
#define R_H1		4
#define R_H10		5
#define R_W		6
#define R_D1		7
#define R_D10		8
#define R_MO1		9
#define R_MO10		10
#define R_Y1		11
#define R_Y10		12
#define R_RESET		13
#define NTREG		13

#define H10_24H		0x08
#define H10_PM		0x04

#define TRIES		4
#define BASEYEAR	1978
#define WRAPYEAR	78	/* yy >= 78 is 19yy, else 20yy */
#define DAYSECS		86400L

/* Image pairs that disagreed: a carry landed inside a pass. */
ULONG BattRetries;

static int savepb, savedd;

static UBYTE mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static UWORD mcum[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

/* 1978-2077 holds one century year, 2000, and it is leap. */
static int
mlen(m, y)
int m, y;
{
	if (m == 2 && (y & 3) == 0)
		return (29);
	return ((int)mdays[m - 1]);
}

static void
pcsel(cs)
int cs;
{
	register int s;

	s = intoff();
	outb(P_PCDATA, PC_WPCS | (inb(P_PCDATA) & 0x0d) | cs);
	intrest(s);
}

static void
begin()
{
	xForbid();
	savepb = inb(P_PBDATA);
	savedd = inb(P_PBDD);
	outb(P_PBDATA, 0);
	outb(P_PBDD, 0);
	pcsel(0);
}

static void
end()
{
	outb(P_PBDATA, 0);
	pcsel(PC_CS);
	outb(P_PBDD, savedd);
	outb(P_PBDATA, savepb);
	xPermit();
}

static void
rtcaddr(reg, hold)
int reg, hold;
{
	outb(P_PBDATA, hold | reg);
	outb(P_PBDATA, hold | reg | PB_ADWR);
	outb(P_PBDATA, hold | reg);
}

static int
rtcrd(reg, hold)
int reg, hold;
{
	register int v;

	rtcaddr(reg, hold);
	outb(P_PBDD, PB_D);
	outb(P_PBDATA, hold | PB_READ);
	v = inb(P_PBDATA) & PB_D;
	outb(P_PBDATA, hold);
	outb(P_PBDD, 0);
	return (v);
}

static void
rtcwr(reg, v, hold)
int reg, v, hold;
{
	rtcaddr(reg, hold);
	outb(P_PBDATA, hold | v);
	outb(P_PBDATA, hold | v | PB_WRITE);
	outb(P_PBDATA, hold | v);
}

static void
readregs(buf, hold)
register UBYTE *buf;
int hold;
{
	register int i;

	for (i = 0; i < NTREG; i++)
		buf[i] = rtcrd(i, hold);
}

static int
same(a, b)
register UBYTE *a, *b;
{
	register int i;

	for (i = 0; i < NTREG; i++)
		if (a[i] != b[i])
			return (0);
	return (1);
}

/*
 * A register image as seconds; 0 when a field is out of range.  That
 * rejects an absent chip too: all-zero reads as hour 0 in 12-hour mode,
 * all-ones as S1 = 15.  D10's leap selection is ignored.
 */
static int
decode(r, tp)
register UBYTE *r;
ULONG *tp;
{
	register int h, m, y, d;
	ULONG days;

	if (r[R_S1] > 9 || r[R_S10] > 5 || r[R_MI1] > 9 || r[R_MI10] > 5 ||
	    r[R_H1] > 9 || r[R_D1] > 9 || r[R_MO1] > 9 || r[R_MO10] > 1 ||
	    r[R_Y1] > 9 || r[R_Y10] > 9)
		return (0);
	h = (r[R_H10] & 3) * 10 + r[R_H1];
	if ((r[R_H10] & H10_24H) == 0) {
		if (h < 1 || h > 12)
			return (0);
		if (h == 12)
			h = 0;
		if (r[R_H10] & H10_PM)
			h += 12;
	} else if (h > 23)
		return (0);
	m = r[R_MO10] * 10 + r[R_MO1];
	if (m < 1 || m > 12)
		return (0);
	y = r[R_Y10] * 10 + r[R_Y1];
	y += y >= WRAPYEAR ? 1900 : 2000;
	d = (r[R_D10] & 3) * 10 + r[R_D1];
	if (d < 1 || d > mlen(m, y))
		return (0);
	days = 365L * (y - BASEYEAR) + (y - 1) / 4 - (BASEYEAR - 1) / 4 +
	    mcum[m - 1] + d - 1;
	if (m > 2 && (y & 3) == 0)
		days++;
	*tp = days * DAYSECS + h * 3600L + (r[R_MI10] * 10 + r[R_MI1]) * 60L +
	    r[R_S10] * 10 + r[R_S1];
	return (1);
}

/* Seconds as a 24-hour register image; D10's leap selection 00 is the
 * Gregorian calendar. */
static void
encode(t, r)
ULONG t;
register UBYTE *r;
{
	register int y, m, n, len;
	UWORD days;

	days = (UWORD)(t / DAYSECS);
	r[R_W] = days % 7;	/* 1978-01-01 was a Sunday */
	t %= DAYSECS;
	n = (int)(t / 3600L);
	r[R_H10] = n / 10 | H10_24H;
	r[R_H1] = n % 10;
	n = (int)(t % 3600L);
	r[R_MI10] = n / 600;
	r[R_MI1] = (n / 60) % 10;
	r[R_S10] = (n % 60) / 10;
	r[R_S1] = n % 10;
	for (y = BASEYEAR; days >= (len = (y & 3) ? 365 : 366); y++)
		days -= len;
	for (m = 1; days >= (len = mlen(m, y)); m++)
		days -= len;
	n = days + 1;
	r[R_D10] = n / 10;
	r[R_D1] = n % 10;
	r[R_MO10] = m / 10;
	r[R_MO1] = m % 10;
	r[R_Y10] = (y % 100) / 10;
	r[R_Y1] = y % 10;
}

static ULONG
bcread()
{
	UBYTE a[NTREG], b[NTREG];
	ULONG t;
	register int try, ok;

	begin();
	for (try = 0, ok = 0; try < TRIES && !ok; try++) {
		readregs(a, 0);
		readregs(b, 0);
		if (!(ok = same(a, b)))
			BattRetries++;
	}
	end();
	if (!ok || !decode(a, &t))
		return (0L);
	return (t);
}

static int
bcwrite(base, t)
APTR base;
ULONG t;
{
	UBYTE r[NTREG], v[NTREG];
	register int i;

	if (t > BC_MAXTIME)
		return (BCERR_RANGE);
	encode(t, r);
	begin();
	outb(P_PBDATA, PB_STOP);
	for (i = 0; i < NTREG; i++)
		rtcwr(i, (int)r[i], PB_STOP);
	rtcwr(R_RESET, 0, PB_STOP);
	readregs(v, PB_STOP);
	outb(P_PBDATA, 0);
	end();
	return (same(r, v) ? 0 : BCERR_NOCLOCK);
}

static int
bcreset(base)
APTR base;
{
	return (bcwrite(base, 0L));
}

static char *
put2(p, n)
register char *p;
int n;
{
	*p++ = '0' + n / 10;
	*p++ = '0' + n % 10;
	return (p);
}

static struct {
	struct JumpVec lv_Vec[3];
	struct Library lv_Base;
} bclib;
static char bcname[] = BATTCLOCKNAME;

static APTR bcfuncs[] = {
	(APTR)bcreset, (APTR)bcread, (APTR)bcwrite, (APTR)-1L
};

void kbattinit();

struct Resident batttag = {
	RTC_MATCHWORD, &batttag, (APTR)(&batttag + 1), RTF_COLDSTART, 1,
	NT_RESOURCE, 0, bcname, NULL, (APTR)kbattinit
};

/*
 * Boot: list the resource, enable Port B as a bit port if the ROM has
 * not, make PC1 an output driving /CS high, and report the clock.
 */
void
kbattinit()
{
	register int s, yy;
	register char *p;
	UBYTE r[NTREG];
	char buf[20];
	ULONG t;

	kinitlib(&bclib.lv_Base, bcname, bcfuncs);
	bclib.lv_Base.lib_Node.ln_Type = NT_RESOURCE;
	xAddResource((APTR)&bclib.lv_Base);
	s = intoff();
	if ((inb(P_MCCR) & MCCR_PBE) == 0) {
		outb(P_PBMS, 0);
		outb(P_PBDD, 0);
		outb(P_PBDATA, 0);
		outb(P_MCCR, inb(P_MCCR) | MCCR_PBE);
	}
	outb(P_PCDATA, PC_WPCS | (inb(P_PCDATA) & 0x0d) | PC_CS);
	outb(P_PCDD, inb(P_PCDD) & ~PC_CS);
	intrest(s);

	if ((t = bcread()) == 0) {
		xKPrintf("battclock: no clock\n");
		return;
	}
	encode(t, r);
	p = buf;
	yy = r[R_Y10] * 10 + r[R_Y1];
	p = put2(p, yy >= WRAPYEAR ? 19 : 20);
	p = put2(p, yy);
	*p++ = '-';
	p = put2(p, r[R_MO10] * 10 + r[R_MO1]);
	*p++ = '-';
	p = put2(p, r[R_D10] * 10 + r[R_D1]);
	*p++ = ' ';
	p = put2(p, (r[R_H10] & 3) * 10 + r[R_H1]);
	*p++ = ':';
	p = put2(p, r[R_MI10] * 10 + r[R_MI1]);
	*p++ = ':';
	p = put2(p, r[R_S10] * 10 + r[R_S1]);
	*p = '\0';
	xKPrintf("battclock: %s\n", buf);
}
