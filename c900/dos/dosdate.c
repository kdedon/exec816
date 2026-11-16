/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * dos.library dates and name splitting, as AmigaDOS V36/V39: DateToStr,
 * StrToDate, CompareDates and SplitName.  Day 0 is 1978-01-01, a
 * Sunday.  Two-digit years 78 to 99 are 19xx, 00 to 77 20xx, as C:Date
 * reads them.  There is no locale.library: FORMAT_DEF is FORMAT_DOS and
 * the names are English.
 */
#include "dosint.h"
#include <dos/datetime.h>

static char *ddays[] = {
	"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
	"Saturday"
};
static char dmonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
static char dmlen[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static int
dylen(y)
int y;
{
	return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0) ? 366 : 365);
}

static int
dmdays(m, y)
int m, y;
{
	return (m == 1 && dylen(y) == 366 ? 29 : dmlen[m]);
}

/* Two decimal digits at p. */
static char *
d2(p, v)
register char *p;
int v;
{
	*p++ = '0' + v / 10 % 10;
	*p++ = '0' + v % 10;
	return (p);
}

static void
dscopy(d, s)
register char *d, *s;
{
	while ((*d++ = *s++) != '\0')
		;
}

/* Case-insensitive: s starts with n characters of w; with n -1, s is w. */
static int
dpre(s, w, n)
register char *s, *w;
register int n;
{
	for (; n != 0 && *w != '\0'; n--, s++, w++)
		if ((*s | 0x20) != (*w | 0x20))
			return (0);
	return (n >= 0 || *s == '\0');
}

LONG
xDateToStr(db, dt)
DB db;
register struct DateTime *dt;
{
	struct DateStamp now;
	register char *p;
	LONG days, diff, s;
	int y, m, d, f;

	days = dt->dat_Stamp.ds_Days;
	if (days < 0)
		return (DOSFALSE);
	if (dt->dat_StrDay != NULL)
		dscopy(dt->dat_StrDay, ddays[(int)(days % 7)]);
	if ((p = dt->dat_StrDate) != NULL) {
		diff = 7;
		if (dt->dat_Flags & DTF_SUBST) {
			xDateStamp(db, &now);
			diff = now.ds_Days - days;
		}
		if (diff == 0)
			dscopy(p, "Today");
		else if (diff == 1)
			dscopy(p, "Yesterday");
		else if (diff == -1)
			dscopy(p, "Tomorrow");
		else if ((dt->dat_Flags & DTF_FUTURE ? -diff : diff) > 1 &&
		    (dt->dat_Flags & DTF_FUTURE ? -diff : diff) < 7)
			dscopy(p, ddays[(int)(days % 7)]);
		else {
			for (y = 1978; days >= dylen(y); y++)
				days -= dylen(y);
			for (m = 0; days >= dmdays(m, y); m++)
				days -= dmdays(m, y);
			d = (int)days + 1;
			f = dt->dat_Format;
			if (f == FORMAT_INT) {
				p = d2(p, y);
				*p++ = '-';
				p = d2(p, m + 1);
				*p++ = '-';
				p = d2(p, d);
			} else if (f == FORMAT_USA) {
				p = d2(p, m + 1);
				*p++ = '-';
				p = d2(p, d);
				*p++ = '-';
				p = d2(p, y);
			} else {
				p = d2(p, d);
				*p++ = '-';
				if (f == FORMAT_CDN)
					p = d2(p, m + 1);
				else {
					*p++ = dmonths[m * 3];
					*p++ = dmonths[m * 3 + 1];
					*p++ = dmonths[m * 3 + 2];
				}
				*p++ = '-';
				p = d2(p, y);
			}
			*p = '\0';
		}
	}
	if ((p = dt->dat_StrTime) != NULL) {
		s = dt->dat_Stamp.ds_Tick / TICKS_PER_SECOND;
		p = d2(p, (int)(dt->dat_Stamp.ds_Minute / 60));
		*p++ = ':';
		p = d2(p, (int)(dt->dat_Stamp.ds_Minute % 60));
		*p++ = ':';
		p = d2(p, (int)s);
		*p = '\0';
	}
	return (DOSTRUE);
}

/* A number of 1 to n digits at *pp, or -1. */
static int
dnum(pp, n)
char **pp;
int n;
{
	register char *p;
	register int v;

	p = *pp;
	if (*p < '0' || *p > '9')
		return (-1);
	for (v = 0; n != 0 && *p >= '0' && *p <= '9'; n--, p++)
		v = v * 10 + *p - '0';
	*pp = p;
	return (v);
}

/* A separator at *pp. */
static int
dsep(pp)
char **pp;
{
	if (**pp != '-' && **pp != '/')
		return (0);
	(*pp)++;
	return (1);
}

/* The date at p in format f, or -1. */
static LONG
dparse(p, f, flags)
char *p;
int f, flags;
{
	struct DateStamp now;
	register int i;
	int y, m, d, v;
	LONG days;

	xDateStamp(DOSBase, &now);
	if (dpre(p, "Today", -1))
		return (now.ds_Days);
	if (dpre(p, "Yesterday", -1))
		return (now.ds_Days - 1);
	if (dpre(p, "Tomorrow", -1))
		return (now.ds_Days + 1);
	for (i = 0; i < 7; i++)
		if (dpre(p, ddays[i], -1)) {
			v = (int)(now.ds_Days % 7);
			if (flags & DTF_FUTURE)
				v = i - v;
			else
				v = v - i;
			if (v <= 0)
				v += 7;
			return (flags & DTF_FUTURE ? now.ds_Days + v :
			    now.ds_Days - v);
		}
	y = m = d = -1;
	if (f == FORMAT_INT) {
		if ((y = dnum(&p, 4)) < 0 || !dsep(&p) ||
		    (m = dnum(&p, 2)) < 0 || !dsep(&p))
			return (-1);
		d = dnum(&p, 2);
	} else if (f == FORMAT_USA) {
		if ((m = dnum(&p, 2)) < 0 || !dsep(&p) ||
		    (d = dnum(&p, 2)) < 0 || !dsep(&p))
			return (-1);
		y = dnum(&p, 4);
	} else {
		if ((d = dnum(&p, 2)) < 0 || !dsep(&p))
			return (-1);
		if (f == FORMAT_CDN)
			m = dnum(&p, 2);
		else {
			for (m = 0; m < 12 && !dpre(p, dmonths + m * 3, 3);
			    m++)
				;
			if (m == 12)
				return (-1);
			p += 3;
			m++;
		}
		if (!dsep(&p))
			return (-1);
		y = dnum(&p, 4);
	}
	if (y < 0 || *p != '\0' || m < 1 || m > 12)
		return (-1);
	if (y < 100)
		y += y < 78 ? 2000 : 1900;
	m--;
	if (y < 1978 || d < 1 || d > dmdays(m, y))
		return (-1);
	for (days = d - 1, i = 1978; i < y; i++)
		days += dylen(i);
	for (i = 0; i < m; i++)
		days += dmdays(i, y);
	return (days);
}

LONG
xStrToDate(db, dt)
DB db;
register struct DateTime *dt;
{
	char *p;
	int h, m, s;
	LONG days;

	days = dt->dat_Stamp.ds_Days;
	if (dt->dat_StrDate != NULL &&
	    (days = dparse(dt->dat_StrDate, (int)dt->dat_Format,
	    (int)dt->dat_Flags)) < 0)
		return (DOSFALSE);
	if ((p = dt->dat_StrTime) != NULL) {
		s = 0;
		if ((h = dnum(&p, 2)) < 0 || *p++ != ':' ||
		    (m = dnum(&p, 2)) < 0)
			return (DOSFALSE);
		if (*p == ':' && (p++, (s = dnum(&p, 2)) < 0))
			return (DOSFALSE);
		if (*p != '\0' || h > 23 || m > 59 || s > 59)
			return (DOSFALSE);
		dt->dat_Stamp.ds_Minute = h * 60 + m;
		dt->dat_Stamp.ds_Tick = (LONG)s * TICKS_PER_SECOND;
	}
	dt->dat_Stamp.ds_Days = days;
	return (DOSTRUE);
}

/* Negative when d1 is later, 0 when equal, positive when d2 is. */
LONG
xCompareDates(db, d1, d2)
DB db;
register struct DateStamp *d1, *d2;
{
	if (d1->ds_Days != d2->ds_Days)
		return (d2->ds_Days - d1->ds_Days);
	if (d1->ds_Minute != d2->ds_Minute)
		return (d2->ds_Minute - d1->ds_Minute);
	return (d2->ds_Tick - d1->ds_Tick);
}

/* The component of name from oldpos to sep (at most size - 1 bytes of
 * it) in buf; the position after sep, or -1 at the end of name. */
LONG
xSplitName(db, name, sep, buf, oldpos, size)
DB db;
char *name;
ULONG sep;
register char *buf;
LONG oldpos, size;
{
	register char *p;

	p = name + (UWORD)oldpos;
	for (; *p != '\0' && *p != (char)sep; p++, oldpos++)
		if (size > 1) {
			*buf++ = *p;
			size--;
		}
	if (size > 0)
		*buf = '\0';
	return (*p == '\0' ? -1L : oldpos + 1);
}
