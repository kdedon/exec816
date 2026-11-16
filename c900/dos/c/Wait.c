/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C:Wait /N,SEC=SECS/S,MIN=MINS/S,UNTIL/K: wait n seconds (1 by
 * default) or minutes, or until a time of day (HH:MM); Ctrl-C ends the
 * wait with RETURN_WARN.
 */
#include "cargs.h"

int
main(args, len)
char *args;
LONG len;
{
	struct DateStamp ds;
	struct RDArgs *rd;
	LONG a[4], n, h, m;
	register char *w;

	a[0] = a[1] = a[2] = a[3] = 0;
	if ((rd = rdargs("/N,SEC=SECS/S,MIN=MINS/S,UNTIL/K", a)) == NULL)
		return (RETURN_ERROR);
	n = a[0] != 0 ? *(LONG *)a[0] : 1;
	if (a[2])
		n *= 60;
	if ((w = (char *)a[3]) != NULL) {
		if ((h = StrToLong(w, &n)) <= 0 || w[(int)h] != ':' ||
		    StrToLong(w + (int)h + 1, &m) <= 0 || n > 23 || m > 59) {
			PutStr("Wait: bad time\n");
			FreeArgs(rd);
			return (RETURN_ERROR);
		}
		DateStamp(&ds);
		n = (n * 60 + m - ds.ds_Minute) * 60 -
		    ds.ds_Tick / TICKS_PER_SECOND;
		if (n <= 0)
			n += 86400L;
	}
	FreeArgs(rd);
	for (; n > 0; n--) {
		Delay((LONG)TICKS_PER_SECOND);
		if (brk())
			return (RETURN_WARN);
	}
	return (RETURN_OK);
}
