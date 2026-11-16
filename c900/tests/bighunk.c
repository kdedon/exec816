/* SPDX-License-Identifier: MIT */
/*
 * Programs and libraries over 64 KB (mkhunk.py --split): bigprog and
 * biglib from the boot volume, each over 100 KB of code in several code
 * hunks that call each other both ways and reach each other's data and
 * bss (tests/split/genbig.py); bigprog's C library members are another.  Run the program's entry; initialise the
 * library's romtag, check that its vectors lie in different code hunks,
 * call them, close and expunge it.  Unloading frees every hunk: no
 * memory may be lost.
 */
#include "test.h"
#include "bigexp.h"

#define FREEVEC		((APTR)((char *)SysBase + LVOFreeMem))
#define MAXSEGS		16

#define BigChain(b, v)	(*(ULONG (*)())((char *)(b) - 30))((b), (ULONG)(v))
#define BigEnt(b, n, v)	(*(ULONG (*)())((char *)(b) - 36 - 6 * (n))) \
	((b), (ULONG)(v))

static char *hunks[MAXSEGS];
static ULONG sizes[MAXSEGS];
static int nhunks;

/* Load name from the boot volume and list its hunks. */
static APTR
load(bf, name)
struct BootFile *bf;
char *name;
{
	register ULONG *p;
	APTR seg;

	if (kbfopen(bf, name) != 0)
		return (NULL);
	seg = InternalLoadSeg(bf, NULL, BootFuncs, NULL);
	nhunks = 0;
	for (p = (ULONG *)seg; p != NULL && nhunks < MAXSEGS; p = (ULONG *)*p) {
		hunks[nhunks] = (char *)(p + 1);
		sizes[nhunks++] = p[-1] - 8;
	}
	return (seg);
}

/* The hunk holding a, or -1. */
static int
hunkof(a)
APTR a;
{
	int i;

	for (i = 0; i < nhunks; i++)
		if (PSEG(a) == PSEG(hunks[i]) && POFF(a) >= POFF(hunks[i]) &&
		    POFF(a) < POFF(hunks[i]) + sizes[i])
			return (i);
	return (-1);
}

/* Hunk sizes: at least `want' hunks, each under 64 KB, over 64 KB in all. */
static char *
shape(want)
int want;
{
	ULONG total;
	int i;

	if (nhunks < want || nhunks >= MAXSEGS)
		return ("hunk count");
	total = 0;
	for (i = 0; i < nhunks; i++) {
		if (sizes[i] > 0xFFF0L || TypeOfMem(hunks[i]) == 0)
			return ("hunk size or memory");
		total += sizes[i];
	}
	if (total <= 0x10000L)
		return ("not over 64 KB");
	return (NULL);
}

static char *
prog(bf)
struct BootFile *bf;
{
	ULONG (*entry)();
	APTR seg;
	char *why;

	if ((seg = load(bf, "bigprog")) == NULL)
		return ("loading bigprog");
	xKPrintf("bighunk: bigprog %d hunks\n", nhunks);
	if ((why = shape(BIGPARTS + 3)) != NULL)
		return (why);
	entry = (ULONG (*)())hunks[0];
	if ((*entry)(BIGVPROG) != (BIGPROG ^ 4660L))
		return ("bigprog result");
	InternalUnLoadSeg(seg, FREEVEC);
	return (NULL);
}

static char *
lib(bf)
struct BootFile *bf;
{
	register UWORD *w;
	register struct JumpVec *v;
	struct Library *lb;
	struct Resident *tag;
	APTR seg;
	UWORD *e;
	char *why;
	int i, h, seen;
	static ULONG want[BIGPARTS] = { BIGENT0, BIGENT1, BIGENT2 };

	if ((seg = load(bf, "biglib")) == NULL)
		return ("loading biglib");
	xKPrintf("bighunk: biglib %d hunks\n", nhunks);
	if ((why = shape(BIGPARTS + 2)) != NULL)
		return (why);
	tag = NULL;
	for (i = 0; i < nhunks && tag == NULL; i++) {
		w = (UWORD *)hunks[i];
		e = (UWORD *)(hunks[i] + (UWORD)sizes[i] - 6);
		for (; w <= e; w++)
			if (*w == RTC_MATCHWORD && *(UWORD **)(w + 1) == w) {
				tag = (struct Resident *)w;
				break;
			}
	}
	if (tag == NULL)
		return ("no romtag");
	if ((lb = (struct Library *)InitResident(tag, seg)) == NULL)
		return ("InitResident");
	if (OpenLibrary("big.library", 1L) != lb)
		return ("OpenLibrary");
	seen = 0;
	for (i = 1; i <= 5 + BIGPARTS; i++) {
		v = (struct JumpVec *)((char *)lb - i * LIB_VECTSIZE);
		if (v->jv_Op != JP_OP || (h = hunkof(v->jv_Fn)) < 0)
			return ("vector outside the seglist");
		seen |= 1 << h;
	}
	for (i = 0, h = 0; i < nhunks; i++)
		if (seen & 1 << i)
			h++;
	if (h < BIGPARTS)
		return ("vectors not in separate code hunks");
	if (BigChain(lb, BIGVLIB) != BIGLIB)
		return ("BigChain result");
	for (i = 0; i < BIGPARTS; i++)
		if (BigEnt(lb, i, BIGVENT) != want[i])
			return ("BigEnt result");
	CloseLibrary(lb);
	if (lb->lib_OpenCnt != 0)
		return ("CloseLibrary");
	if (RemLibrary(lb) != seg)
		return ("RemLibrary did not return the seglist");
	if (OpenLibrary("big.library", 0L) != NULL)
		return ("big.library still listed");
	InternalUnLoadSeg(seg, FREEVEC);
	return (NULL);
}

static char *
run()
{
	struct BootFile *bf;
	ULONG before;
	char *why;

	before = AvailMem(MEMF_ANY);
	if ((bf = kbfinit()) == NULL)
		return ("no boot volume");
	why = prog(bf);
	if (why == NULL)
		why = lib(bf);
	kbfdone(bf);
	if (why == NULL && AvailMem(MEMF_ANY) != before)
		why = "memory lost";
	return (why);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL bighunk: %s\n", why);
		return (1);
	}
	KPutStr("PASS bighunk\n");
	return (0);
}
