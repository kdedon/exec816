/* SPDX-License-Identifier: MIT */
/*
 * A program for tests/loadseg.c with code, data and bss hunks.  start
 * is the first function, so it is the code hunk's entry.  Its pointers
 * cross every pair of hunks: instruction operands (bit 31 set) and data
 * pointers (bit 31 clear), one with a negative addend.
 */
#include <exec/exec.h>

struct SegInfo {
	APTR si_Code;			/* start */
	APTR si_Data;			/* segdata */
	APTR si_Bss;			/* segbss */
	APTR si_BssM1;			/* &segbss[-1] */
	char *si_Str;
	LONG si_Sum;
	ULONG si_Zero;			/* bss before start ran */
};

LONG segtwice();

char segdata[] = "seghunk data";
LONG segbss[4];
static LONG (*segfn)() = segtwice;
static char *segstr = segdata;
static LONG *segpbss = segbss;
static struct SegInfo seginfo = {
	(APTR)0, (APTR)segdata, (APTR)segbss, (APTR)&segbss[-1]
};

struct SegInfo *
start(cookie)
LONG cookie;
{
	register struct SegInfo *si;

	si = &seginfo;
	si->si_Zero = segbss[0] | segbss[1] | segbss[2] | segbss[3];
	si->si_Code = (APTR)start;
	segpbss[2] = cookie;
	si->si_Sum = (*segfn)(segbss[2]) + segstr[3];
	si->si_Str = segstr;
	KPutStr("seghunk: ran\n");
	return (si);
}

LONG
segtwice(v)
LONG v;
{
	return (v + v);
}
