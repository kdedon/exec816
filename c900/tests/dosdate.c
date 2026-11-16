/* SPDX-License-Identifier: MIT */
/*
 * DateToStr in each format, with the weekday and time strings and the
 * DTF_SUBST names (Today, Yesterday, Tomorrow, a weekday in the past
 * week or, with DTF_FUTURE, the coming one); StrToDate of the same
 * strings, the names and bad dates and times; CompareDates; SplitName.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <dos/datetime.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

static char *why;
static char sday[LEN_DATSTRING], sdate[LEN_DATSTRING], stime[LEN_DATSTRING];
static struct DateTime dt;

#define CHECK(c, w)	if (!(c)) { why = w; return (1); }

static int
seq(a, b)
register char *a, *b;
{
	while (*a == *b && *a != '\0')
		a++, b++;
	return (*a == *b);
}

static void
scopy(d, s)
register char *d, *s;
{
	while ((*d++ = *s++) != '\0')
		;
}

/* DateToStr of day, minute, tick in format f with flags fl. */
static int
tostr(day, min, tick, f, fl)
LONG day, min, tick;
int f, fl;
{
	dt.dat_Stamp.ds_Days = day;
	dt.dat_Stamp.ds_Minute = min;
	dt.dat_Stamp.ds_Tick = tick;
	dt.dat_Format = f;
	dt.dat_Flags = fl;
	dt.dat_StrDay = sday;
	dt.dat_StrDate = sdate;
	dt.dat_StrTime = stime;
	sday[0] = sdate[0] = stime[0] = '\0';
	return (DateToStr(&dt) == DOSTRUE);
}

/* StrToDate of date and time (either NULL) in format f with flags fl. */
static int
fromstr(date, time, f, fl)
char *date, *time;
int f, fl;
{
	dt.dat_Stamp.ds_Days = 5;
	dt.dat_Stamp.ds_Minute = 6;
	dt.dat_Stamp.ds_Tick = 7;
	dt.dat_Format = f;
	dt.dat_Flags = fl;
	dt.dat_StrDay = NULL;
	dt.dat_StrDate = NULL;
	dt.dat_StrTime = NULL;
	if (date != NULL) {
		scopy(sdate, date);
		dt.dat_StrDate = sdate;
	}
	if (time != NULL) {
		scopy(stime, time);
		dt.dat_StrTime = stime;
	}
	return (StrToDate(&dt) == DOSTRUE);
}

/* 2026-11-15, a Sunday, is day 17850. */
#define D	17850L

static int
dtest()
{
	struct DateStamp now, a, b;
	char buf[8];
	LONG n, pos;

	CHECK(tostr(0L, 0L, 0L, FORMAT_DOS, 0) && seq(sday, "Sunday") &&
	    seq(sdate, "01-Jan-78") && seq(stime, "00:00:00"), "day 0");
	CHECK(tostr(D, 754L, 2849L, FORMAT_DOS, 0) && seq(sday, "Sunday") &&
	    seq(sdate, "15-Nov-26") && seq(stime, "12:34:56"), "FORMAT_DOS");
	CHECK(tostr(D, 0L, 0L, FORMAT_INT, 0) && seq(sdate, "26-11-15"),
	    "FORMAT_INT");
	CHECK(tostr(D, 0L, 0L, FORMAT_USA, 0) && seq(sdate, "11-15-26"),
	    "FORMAT_USA");
	CHECK(tostr(D, 0L, 0L, FORMAT_CDN, 0) && seq(sdate, "15-11-26"),
	    "FORMAT_CDN");
	CHECK(tostr(D, 0L, 0L, FORMAT_DEF, 0) && seq(sdate, "15-Nov-26"),
	    "FORMAT_DEF");
	CHECK(tostr(8094L, 1439L, 2999L, FORMAT_DOS, 0) &&
	    seq(sday, "Tuesday") && seq(sdate, "29-Feb-00") &&
	    seq(stime, "23:59:59"), "a leap day");
	CHECK(tostr(17928L, 0L, 0L, FORMAT_INT, 0) && seq(sdate, "27-02-01") &&
	    seq(sday, "Monday"), "FORMAT_INT, a later year");
	CHECK(!tostr(-1L, 0L, 0L, FORMAT_DOS, 0), "a negative day");
	dt.dat_Stamp.ds_Days = 0;
	dt.dat_StrDay = dt.dat_StrTime = NULL;
	sday[0] = stime[0] = 'x';
	CHECK(DateToStr(&dt) == DOSTRUE && seq(sdate, "01-Jan-78") &&
	    sday[0] == 'x' && stime[0] == 'x', "NULL strings");

	DateStamp(&now);
	n = now.ds_Days;
	CHECK(tostr(n, 0L, 0L, FORMAT_DOS, DTF_SUBST) && seq(sdate, "Today"),
	    "Today");
	CHECK(tostr(n - 1, 0L, 0L, FORMAT_DOS, DTF_SUBST) &&
	    seq(sdate, "Yesterday"), "Yesterday");
	CHECK(tostr(n + 1, 0L, 0L, FORMAT_DOS, DTF_SUBST) &&
	    seq(sdate, "Tomorrow"), "Tomorrow");
	CHECK(tostr(n - 6, 0L, 0L, FORMAT_DOS, DTF_SUBST) &&
	    seq(sdate, sday) && sdate[0] != '0', "a weekday last week");
	CHECK(tostr(n - 7, 0L, 0L, FORMAT_DOS, DTF_SUBST) &&
	    !seq(sdate, sday), "a week ago");
	CHECK(tostr(n + 3, 0L, 0L, FORMAT_DOS, DTF_SUBST) &&
	    !seq(sdate, sday), "a future weekday without DTF_FUTURE");
	CHECK(tostr(n + 3, 0L, 0L, FORMAT_DOS, DTF_SUBST | DTF_FUTURE) &&
	    seq(sdate, sday), "a weekday next week");
	CHECK(tostr(n - 3, 0L, 0L, FORMAT_DOS, DTF_SUBST | DTF_FUTURE) &&
	    !seq(sdate, sday), "a past weekday with DTF_FUTURE");
	CHECK(tostr(n, 0L, 0L, FORMAT_DOS, 0) && !seq(sdate, "Today"),
	    "Today without DTF_SUBST");

	CHECK(fromstr("15-Nov-26", "12:34:56", FORMAT_DOS, 0) &&
	    dt.dat_Stamp.ds_Days == D && dt.dat_Stamp.ds_Minute == 754 &&
	    dt.dat_Stamp.ds_Tick == 2800, "StrToDate FORMAT_DOS");
	CHECK(fromstr("15-nov-2026", "1:02", FORMAT_DOS, 0) &&
	    dt.dat_Stamp.ds_Days == D && dt.dat_Stamp.ds_Minute == 62 &&
	    dt.dat_Stamp.ds_Tick == 0, "StrToDate case and long year");
	CHECK(fromstr("26-11-15", NULL, FORMAT_INT, 0) &&
	    dt.dat_Stamp.ds_Days == D && dt.dat_Stamp.ds_Minute == 6 &&
	    dt.dat_Stamp.ds_Tick == 7, "StrToDate FORMAT_INT");
	CHECK(fromstr("11-15-26", NULL, FORMAT_USA, 0) &&
	    dt.dat_Stamp.ds_Days == D, "StrToDate FORMAT_USA");
	CHECK(fromstr("15-11-26", NULL, FORMAT_CDN, 0) &&
	    dt.dat_Stamp.ds_Days == D, "StrToDate FORMAT_CDN");
	CHECK(fromstr("29-Feb-00", NULL, FORMAT_DOS, 0) &&
	    dt.dat_Stamp.ds_Days == 8094, "StrToDate leap day");
	CHECK(fromstr(NULL, "23:59", FORMAT_DOS, 0) &&
	    dt.dat_Stamp.ds_Days == 5 && dt.dat_Stamp.ds_Minute == 1439,
	    "StrToDate time only");
	DateStamp(&now);
	n = now.ds_Days;
	CHECK(fromstr("today", NULL, FORMAT_DOS, 0) &&
	    dt.dat_Stamp.ds_Days == n, "StrToDate Today");
	CHECK(fromstr("Yesterday", NULL, FORMAT_DOS, 0) &&
	    dt.dat_Stamp.ds_Days == n - 1, "StrToDate Yesterday");
	CHECK(fromstr("Tomorrow", NULL, FORMAT_DOS, 0) &&
	    dt.dat_Stamp.ds_Days == n + 1, "StrToDate Tomorrow");
	CHECK(tostr(n - 3, 0L, 0L, FORMAT_DOS, 0) &&
	    fromstr(sday, NULL, FORMAT_DOS, 0) &&
	    dt.dat_Stamp.ds_Days == n - 3, "StrToDate a past weekday");
	CHECK(tostr(n + 3, 0L, 0L, FORMAT_DOS, 0) &&
	    fromstr(sday, NULL, FORMAT_DOS, DTF_FUTURE) &&
	    dt.dat_Stamp.ds_Days == n + 3, "StrToDate a future weekday");
	CHECK(!fromstr("30-Feb-00", NULL, FORMAT_DOS, 0) &&
	    !fromstr("15-Nox-26", NULL, FORMAT_DOS, 0) &&
	    !fromstr("15-13-26", NULL, FORMAT_CDN, 0) &&
	    !fromstr("15-Nov-26x", NULL, FORMAT_DOS, 0) &&
	    !fromstr("Todayx", NULL, FORMAT_DOS, 0) &&
	    dt.dat_Stamp.ds_Days == 5, "StrToDate bad dates");
	CHECK(!fromstr(NULL, "24:00", FORMAT_DOS, 0) &&
	    !fromstr(NULL, "12:60", FORMAT_DOS, 0) &&
	    !fromstr(NULL, "12", FORMAT_DOS, 0) &&
	    !fromstr("15-Nov-26", "1:2:3:4", FORMAT_DOS, 0) &&
	    dt.dat_Stamp.ds_Days == 5 && dt.dat_Stamp.ds_Minute == 6,
	    "StrToDate bad times");

	a.ds_Days = b.ds_Days = 10;
	a.ds_Minute = b.ds_Minute = 20;
	a.ds_Tick = b.ds_Tick = 30;
	CHECK(CompareDates(&a, &b) == 0, "CompareDates equal");
	b.ds_Tick = 31;
	CHECK(CompareDates(&a, &b) > 0 && CompareDates(&b, &a) < 0,
	    "CompareDates by tick");
	b.ds_Minute = 19;
	CHECK(CompareDates(&a, &b) < 0, "CompareDates by minute");
	b.ds_Days = 11;
	CHECK(CompareDates(&a, &b) > 0 && CompareDates(&b, &a) < 0,
	    "CompareDates by day");

	CHECK((pos = SplitName("Work:dir/file", (ULONG)':', buf, 0L,
	    (LONG)sizeof(buf))) == 5 && seq(buf, "Work"), "SplitName volume");
	CHECK((pos = SplitName("Work:dir/file", (ULONG)'/', buf, pos,
	    (LONG)sizeof(buf))) == 9 && seq(buf, "dir"), "SplitName dir");
	CHECK(SplitName("Work:dir/file", (ULONG)'/', buf, pos,
	    (LONG)sizeof(buf)) == -1 && seq(buf, "file"), "SplitName last");
	CHECK(SplitName("verylongname/x", (ULONG)'/', buf, 0L, 5L) == 13 &&
	    seq(buf, "very"), "SplitName truncates");
	CHECK(SplitName("a/", (ULONG)'/', buf, 2L, (LONG)sizeof(buf)) == -1 &&
	    buf[0] == '\0', "SplitName at the end");
	return (0);
}

static LONG
pmain(args, len)
char *args;
LONG len;
{
	return ((LONG)dtest());
}

struct dsync {
	struct Task *s_Task;
	LONG s_RC;
};

static void
pdone(rc, s)
LONG rc;
struct dsync *s;
{
	s->s_RC = rc;
	Signal(s->s_Task, SIGBREAKF_CTRL_F);
}

int
main()
{
	struct TagItem t[5];
	struct dsync s;

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 36L))
	    == NULL) {
		KPutStr("FAIL dosdate: no dos.library\n");
		return (1);
	}
	s.s_Task = FindTask(NULL);
	s.s_RC = -1;
	SetSignal(0L, SIGBREAKF_CTRL_F);
	t[0].ti_Tag = NP_Entry;
	t[0].ti_Data = (ULONG)pmain;
	t[1].ti_Tag = NP_ExitCode;
	t[1].ti_Data = (ULONG)pdone;
	t[2].ti_Tag = NP_ExitData;
	t[2].ti_Data = (ULONG)&s;
	t[3].ti_Tag = NP_Name;
	t[3].ti_Data = (ULONG)"dosdate";
	t[4].ti_Tag = TAG_DONE;
	if (CreateNewProc(t) == NULL) {
		KPutStr("FAIL dosdate: CreateNewProc\n");
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	if (s.s_RC != 0) {
		xKPrintf("FAIL dosdate: %s\n", why);
		return (1);
	}
	KPutStr("PASS dosdate\n");
	return (0);
}
