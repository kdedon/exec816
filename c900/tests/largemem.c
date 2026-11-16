/* SPDX-License-Identifier: MIT */
/*
 * Blocks over 64 KB on a 3 MB machine (identity heap pages 0x0A-0x2F and
 * 0x32-0x35): placement from offset 0 of identity-mapped, physically
 * contiguous pages, PtrStep, CLEAR, CopyMem across segments, the LARGEST
 * round trip, partial frees, REVERSE, AllocVec, AllocAbs and the CHIP and
 * FAST attributes.
 */
/* EMUFLAGS: --ram=08-35 */
#include "test.h"

#define BIG	204800L		/* 200 KB: three pages and 8 KB */
#define STEP	250L		/* sample stride, not a divisor of 64 KB */

static UBYTE *
at(p, n)
APTR p;
ULONG n;
{
	return ((UBYTE *)PtrStep(p, n));
}

/* Tag every STEP-th byte of [p, p + n) with its index; check them. */
static void
tag(p, n)
APTR p;
ULONG n;
{
	register ULONG i;

	for (i = 0; i < n; i += STEP)
		*at(p, i) = (UBYTE)(i / STEP) ^ 0x5a;
}

static int
tagged(p, n)
APTR p;
ULONG n;
{
	register ULONG i;

	for (i = 0; i < n; i += STEP)
		if (*at(p, i) != ((UBYTE)(i / STEP) ^ 0x5a))
			return (0);
	return (1);
}

static int
cleared(p, n)
APTR p;
ULONG n;
{
	register ULONG i;

	for (i = 0; i < n; i += STEP)
		if (*at(p, i) != 0)
			return (0);
	return (*at(p, n - 1) == 0);
}

/* Identity-mapped from offset 0 and physically contiguous. */
static int
linear(p, n)
APTR p;
ULONG n;
{
	register ULONG k;

	if (POFF(p) != 0)
		return (0);
	for (k = 0; k < n; k += 0x10000L)
		if (kphys(at(p, k)) != (PSEG(p) >> 8) + k)
			return (0);
	return (kphys(at(p, n - 1)) == (PSEG(p) >> 8) + n - 1);
}

static char *
place()
{
	UBYTE *p, *q;
	ULONG avail;

	avail = AvailMem(0);
	if ((p = (UBYTE *)AllocMem(BIG, MEMF_PUBLIC)) == NULL)
		return ("AllocMem 200 KB");
	if (!linear(p, BIG))
		return ("200 KB block not identity-mapped from offset 0");
	if (AvailMem(0) != avail - BIG)
		return ("200 KB block size");
	if (PtrStep(p, 0x10000L) != (APTR)(PSEG(p) + 0x01000000L) ||
	    PtrStep(at(p, 0x10002L), -4L) != (APTR)(PSEG(p) | 0xfffeL))
		return ("PtrStep");
	tag(p, BIG);
	*at(p, 0xffffL) = 0x11;
	*at(p, 0x10000L) = 0x22;
	q = (UBYTE *)MKPTR(PSEG(p) + 0x01000000L, 0);
	if (!tagged(p, BIG) || p[0xffff] != 0x11 || *q != 0x22)
		return ("pattern across segments");
	FreeMem(p, BIG);
	if ((q = (UBYTE *)AllocMem(BIG, MEMF_CLEAR)) != p)
		return ("freed 200 KB block not reused");
	if (!cleared(q, BIG))
		return ("CLEAR of 200 KB");
	FreeMem(q, BIG);
	if (AvailMem(0) != avail)
		return ("200 KB block leaked");
	return (NULL);
}

/* CopyMem between odd, segment-crossing ranges, then an aligned one. */
static char *
copy()
{
	UBYTE *p, *q;
	ULONG avail;

	avail = AvailMem(0);
	p = (UBYTE *)AllocMem(BIG, 0);
	q = (UBYTE *)AllocMem(BIG, 0);
	if (p == NULL || q == NULL)
		return ("AllocMem for CopyMem");
	tag(at(p, 0xfff1L), 90001L);
	CopyMem(at(p, 0xfff1L), at(q, 3L), 90001L);
	if (!tagged(at(q, 3L), 90001L))
		return ("CopyMem odd");
	tag(p, BIG);
	CopyMemQuick(p, q, BIG);
	if (!tagged(q, BIG))
		return ("CopyMemQuick");
	FreeMem(p, BIG);
	FreeMem(q, BIG);
	if (AvailMem(0) != avail)
		return ("CopyMem leaked");
	return (NULL);
}

static char *
largest()
{
	UBYTE *p;
	ULONG avail, big;

	avail = AvailMem(0);
	big = AvailMem(MEMF_LARGEST);
	if (big <= 0x10000L)
		return ("LARGEST does not count runs");
	if (AllocMem(big + 8, 0) != NULL)
		return ("AllocMem LARGEST + 8");
	if ((p = (UBYTE *)AllocMem(big, 0)) == NULL)
		return ("AllocMem LARGEST");
	if (!linear(p, big))
		return ("LARGEST block not contiguous");
	FreeMem(p, big);
	if (AvailMem(0) != avail || AvailMem(MEMF_LARGEST) != big)
		return ("LARGEST round trip leaked");
	xKPrintf("largemem: largest %lu\n", big);
	return (NULL);
}

/* Free a 200 KB block in three pieces, the middle one first. */
static char *
partial()
{
	UBYTE *p;
	ULONG avail, big;

	avail = AvailMem(0);
	big = AvailMem(MEMF_LARGEST);
	if ((p = (UBYTE *)AllocMem(BIG, 0)) == NULL)
		return ("AllocMem for partial frees");
	FreeMem(at(p, 0x8000L), 0x10000L);
	if (AvailMem(0) != avail - BIG + 0x10000L)
		return ("middle free");
	FreeMem(p, 0x8000L);
	FreeMem(at(p, 0x18000L), BIG - 0x18000L);
	if (AvailMem(0) != avail || AvailMem(MEMF_LARGEST) != big)
		return ("partial frees did not restore AvailMem");
	return (NULL);
}

static char *
reverse()
{
	UBYTE *p, *r, *b;
	ULONG avail;

	avail = AvailMem(0);
	p = (UBYTE *)AllocMem(100, 0);
	r = (UBYTE *)AllocMem(100, MEMF_REVERSE);
	if (p == NULL || r == NULL)
		return ("AllocMem REVERSE");
	if (PSEG(r) != SEGBASE(0x35) || POFF(r) != 0x10000L - 104)
		return ("REVERSE not at the top of the highest page");
	b = (UBYTE *)AllocMem(BIG, MEMF_REVERSE);
	if (b == NULL || PSEG(b) != SEGBASE(0x32) || !linear(b, BIG))
		return ("200 KB REVERSE not in the highest run");
	FreeMem(b, BIG);
	FreeMem(r, 100);
	FreeMem(p, 100);
	if (AvailMem(0) != avail)
		return ("REVERSE leaked");
	return (NULL);
}

static char *
vec()
{
	UBYTE *v, *w;
	ULONG avail;

	avail = AvailMem(0);
	if ((v = (UBYTE *)AllocVec(100000L, MEMF_CLEAR)) == NULL)
		return ("AllocVec 100000");
	if (POFF(v) != 8 || !cleared(v, 100000L))
		return ("AllocVec 100000 not clear");
	if (AvailMem(0) != avail - 100008L)
		return ("AllocVec 100000 size");
	tag(v, 100000L);
	FreeVec(v);

	/* AllocAbs across a page boundary; taken space fails. */
	w = (UBYTE *)AllocMem(BIG, 0);
	FreeMem(w, BIG);
	if (AllocAbs(1000, at(w, 0xfe0cL)) != (APTR)at(w, 0xfe0cL))
		return ("AllocAbs across segments");
	if (AllocAbs(8, at(w, 0x10000L)) != NULL)
		return ("AllocAbs over an allocated block");
	FreeMem(at(w, 0xfe0cL), 1000);
	if (AvailMem(0) != avail)
		return ("AllocVec or AllocAbs leaked");
	return (NULL);
}

static char *
attrs()
{
	UBYTE *p;
	ULONG avail;

	avail = AvailMem(0);
	if (AllocMem(16, MEMF_CHIP) != NULL || AllocVec(16, MEMF_CHIP) != NULL ||
	    AvailMem(MEMF_CHIP) != 0 || AvailMem(MEMF_CHIP | MEMF_LARGEST) != 0)
		return ("CHIP memory found");
	if (AvailMem(MEMF_FAST) != avail ||
	    AvailMem(MEMF_FAST | MEMF_LOCAL | MEMF_24BITDMA) != avail)
		return ("FAST is not all RAM");
	if ((p = (UBYTE *)AllocMem(BIG, MEMF_FAST | MEMF_PUBLIC)) == NULL)
		return ("AllocMem FAST 200 KB");
	if ((TypeOfMem(p) & (MEMF_FAST | MEMF_PUBLIC)) !=
	    (MEMF_FAST | MEMF_PUBLIC))
		return ("TypeOfMem without FAST");
	FreeMem(p, BIG);
	if (AvailMem(0) != avail)
		return ("FAST leaked");
	return (NULL);
}

int
main()
{
	char *why;
	ULONG avail;

	avail = AvailMem(0);
	if ((why = place()) == NULL && (why = copy()) == NULL &&
	    (why = largest()) == NULL && (why = partial()) == NULL &&
	    (why = reverse()) == NULL && (why = vec()) == NULL &&
	    (why = attrs()) == NULL && AvailMem(0) != avail)
		why = "heap not restored";
	if (why != NULL) {
		xKPrintf("FAIL largemem: %s\n", why);
		return (1);
	}
	KPutStr("PASS largemem\n");
	return (0);
}
