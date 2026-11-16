/* SPDX-License-Identifier: MIT */
/*
 * The console line's transmit queue: bytes a Task prints with interrupts
 * on are queued, bytes printed with VI off go out polled, and a polled
 * byte never overtakes a queued one.  The EXPECT line interleaves the
 * two in runs; the transcript holds it whole only in order.  Then a
 * burst of queued text: on a paced line (the Go model, the board)
 * KPutStr returns before the line has sent it.
 */
/* EXPECT: kputq: AAAAbbbbbbbCCCdDDDDDDDDeeeeFFFFFFFFFFggHHHHHHHHHHHHiiiJ end */
#include "test.h"

#define SCC_RR1		0x0103
#define ALLSENT		0x01

static char burst[] =
    "kputq: 0123456789012345678901234567890123456789012345678901234567"
    "89012345678901234567890123456789\n";

/* Upper-case runs queued, lower-case runs polled. */
static char runs[] = "AAAAbbbbbbbCCCdDDDDDDDDeeeeFFFFFFFFFFggHHHHHHHHHHHHiiiJ";

int
main()
{
	register char *p;
	register int i;
	char one[2];
	ULONG t, tq, td;

	one[1] = '\0';
	KPutStr("kputq: ");
	for (p = runs; *p != '\0'; p++) {
		one[0] = *p;
		if (*p >= 'a')
			Disable();
		KPutStr(one);
		if (*p >= 'a')
			Enable();
	}
	KPutStr(" end\n");

	t = TickCount;
	KPutStr(burst);
	tq = TickCount - t;
	for (i = 0; i < 100 && (inb(SCC_RR1) & ALLSENT) == 0; i++)
		Sleep(1);
	td = TickCount - t;
	xKPrintf("kputq: burst returned after %lu ticks, sent after %lu\n",
	    tq, td);
	if (td >= 5 && tq * 2 > td)
		xKPrintf("FAIL kputq: queued output waited for the line\n");
	xKPrintf("PASS kputq\n");
	return (0);
}
