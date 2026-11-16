/* SPDX-License-Identifier: MIT */
/*
 * ftime (which libc's time() calls) from DateStamp, the battery clock,
 * taken as UTC; and sleep over Delay.
 */
#include <sys/types.h>
#include <sys/timeb.h>
#include "libcint.h"

int
ftime(tb)
register struct timeb *tb;
{
	struct DateStamp ds;

	DateStamp(&ds);
	tb->time = AMIGAEPOCH + ds.ds_Days * 86400L + ds.ds_Minute * 60L +
	    ds.ds_Tick / TICKS_PER_SECOND;
	tb->millitm = (unsigned short)(ds.ds_Tick % TICKS_PER_SECOND * 20);
	tb->timezone = 0;
	tb->dstflag = 0;
	return (0);
}

unsigned
sleep(sec)
unsigned sec;
{
	Delay((LONG)sec * TICKS_PER_SECOND);
	return (0);
}
