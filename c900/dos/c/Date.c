/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C:Date [DD-MMM-YY] [HH:MM[:SS]]: show the date and time, as
 * "Saturday 14-Nov-26 12:34:56", or set the system time and the battery
 * clock to the given date and time; a part not given keeps its value.
 * Years 78 to 99 are 19xx, 00 to 77 20xx.
 */
#include "cargs.h"
#include <devices/timer.h>
#include <resources/battclock.h>
#include <lvo/battclock_lib.h>

APTR BattClockBase;

static char *days[] = {
	"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
	"Saturday"
};
static char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
static char mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static int
ylen(y)
int y;
{
	return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0) ? 366 : 365);
}

static int
mlen(m, y)
int m, y;
{
	return (m == 1 && ylen(y) == 366 ? 29 : mdays[m]);
}

/* Two digits of s into *v: the characters used, or 0. */
static int
num2(s, v)
register char *s;
int *v;
{
	if (s[0] < '0' || s[0] > '9')
		return (0);
	if (s[1] < '0' || s[1] > '9') {
		*v = s[0] - '0';
		return (1);
	}
	*v = (s[0] - '0') * 10 + s[1] - '0';
	return (2);
}

int
main(args, len)
char *args;
LONG len;
{
	char w[4], *bad;
	struct DateStamp ds;
	struct timerequest tr;
	struct RDArgs *rd;
	register char *p;
	register int i, y, m;
	int d, yy, hh, mm, ss, set, k;
	LONG day, sec, a[4];
	BPTR out, old;

	a[0] = a[1] = a[2] = a[3] = 0;
	if ((rd = rdargs("DAY,DATE,TIME,TO=VER/K", a)) == NULL)
		return (RETURN_ERROR);
	DateStamp(&ds);
	day = ds.ds_Days;
	sec = ds.ds_Minute * 60 + ds.ds_Tick / TICKS_PER_SECOND;
	set = 0;
	for (k = 0; k < 3; k++) {
		if ((p = bad = (char *)a[k]) == NULL)
			continue;
		if ((i = num2(p, &hh)) != 0 && p[i] == ':') {
			p += i + 1;
			mm = ss = 0;
			if ((i = num2(p, &mm)) == 0)
				goto bad;
			p += i;
			if (*p == ':' && (i = num2(p + 1, &ss)) != 0)
				p += i + 1;
			if (*p != '\0' || hh > 23 || mm > 59 || ss > 59)
				goto bad;
			sec = hh * 3600L + mm * 60 + ss;
		} else if ((i = num2(p, &d)) != 0 && p[i] == '-') {
			p += i + 1;
			for (m = 0; m < 12; m++)
				if ((p[0] | 0x20) == (months[m * 3] | 0x20) &&
				    (p[1] | 0x20) == (months[m * 3 + 1] | 0x20) &&
				    (p[2] | 0x20) == (months[m * 3 + 2] | 0x20))
					break;
			if (m == 12 || p[3] != '-' || num2(p + 4, &yy) != 2 ||
			    p[6] != '\0')
				goto bad;
			yy += yy < 78 ? 2000 : 1900;
			if (d < 1 || d > mlen(m, yy))
				goto bad;
			for (day = d - 1, y = 1978; y < yy; y++)
				day += ylen(y);
			for (i = 0; i < m; i++)
				day += mlen(i, yy);
		} else
			goto bad;
		set = 1;
	}
	if (set) {
		FreeArgs(rd);
		tr.tr_node.io_Message.mn_ReplyPort = NULL;
		if (OpenDevice(TIMERNAME, (ULONG)UNIT_VBLANK, &tr, 0L) == 0) {
			tr.tr_node.io_Command = TR_SETSYSTIME;
			tr.tr_time.tv_secs = day * 86400L + sec;
			tr.tr_time.tv_micro = 0;
			DoIO(&tr);
			CloseDevice(&tr);
		}
		if ((BattClockBase = OpenResource(BATTCLOCKNAME)) == NULL ||
		    WriteBattClock((ULONG)(day * 86400L + sec)) != 0) {
			PutStr("Date: cannot set the clock\n");
			return (RETURN_ERROR);
		}
		return (RETURN_OK);
	}
	out = NULL;
	if (a[3] != 0 && (out = Open((char *)a[3], MODE_NEWFILE)) == NULL) {
		k = fail((char *)a[3]);
		FreeArgs(rd);
		return (k);
	}
	FreeArgs(rd);
	old = out != NULL ? SelectOutput(out) : NULL;
	for (y = 1978; day >= ylen(y); y++)
		day -= ylen(y);
	for (m = 0; day >= mlen(m, y); m++)
		day -= mlen(m, y);
	for (i = 0; i < 3; i++)
		w[i] = months[m * 3 + i];
	w[3] = '\0';
	Printf("%s %02d-%s-%02d %02ld:%02ld:%02ld\n",
	    days[(int)(ds.ds_Days % 7)], (int)day + 1, w, y % 100, sec / 3600,
	    sec / 60 % 60, sec % 60);
	if (out != NULL) {
		SelectOutput(old);
		Close(out);
	}
	return (RETURN_OK);
bad:
	Printf("Date: bad date or time %s\n", bad);
	FreeArgs(rd);
	return (RETURN_ERROR);
}
