/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Checks of the logical address space shared by the memmap, bigmem and
 * highmem tests: the stack segment directly after the image, the heap
 * starting where it ends (a partial page, then whole pages), identity
 * mapping and kphys, distinct RAM behind every heap segment, the windows
 * and the physical pool.
 */

#define MAXHDR	64

static struct MemHeader *hdr[MAXHDR];
static UWORD *blk[MAXHDR];

static ULONG
hdrbytes()
{
	return (((ULONG)sizeof(struct MemHeader) + 7L) & ~7L);
}

/* The heap's upper-RAM MemHeaders, in MemList order. */
static int
heaphdrs()
{
	register struct Node *n;
	register int k;

	k = 0;
	for (n = SysBase->MemList.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if ((((struct MemHeader *)n)->mh_Attributes & MEMF_UPPER) &&
		    k < MAXHDR)
			hdr[k++] = (struct MemHeader *)n;
	return (k);
}

/* The first 256-byte page past the stack segment, where the heap starts. */
static UWORD
heapstart()
{
	return (kmmutab[STACKSEG] + 0x100);
}

/* Relocation slots: 0x04, 0x05, 0x07 and 0x08, and 0x09 once the heap
 * starts above it. */
static int
relocslots()
{
	return ((heapstart() >> 8) > 0x09 ? 5 : 4);
}

/* The stack segment's RAM span starts at this 1 KB step; 0 for the
 * default, the 63 KB segment. */
static UWORD stkexpect;

/*
 * RAM pages 0x0A and up give nabove heap pages when the stack segment ends
 * in page 0x09; the heap holds those above its real start, nreloc of them
 * in relocation slots, plus the rest of the page the stack segment ends
 * in, and the pool npool pages.  Every heap page is distinct RAM at the
 * physical page kphys reports.
 */
static char *
mapcheck(nabove, nreloc, npool)
int nabove, nreloc, npool;
{
	register int i, j, k, seg, rel;
	int npages, first, tails;
	ULONG ph, tail, stk;
	UWORD local;	/* a frame address: segment 0x3F */
	UWORD *w;
	UBYTE *base;	/* the start of a heap region */

	stk = ((ULONG)kmmutab[STACKSEG] << 8) + kstklow;
	if (stkexpect == 0)
		stkexpect = STKSEGMIN;
	if (stk != ((kphys((APTR)kend) + 255L) & ~255L) ||
	    (kstklow & ~0x3ff) != stkexpect || (kstklow & 0x3ff) > 0x300)
		return ("the stack segment does not follow the image");
	if (AvailMem(MEMF_STACK | MEMF_TOTAL) != (ULONG)(STKPOOLTOP - kstklow))
		return ("the stack pool does not span the segment's RAM");
	first = (heapstart() + 0xff) >> 8;
	tail = ((ULONG)first << 16) - ((ULONG)heapstart() << 8);
	npages = nabove - (first - 0x0a);
	if (HeapPages != npages || HeapTail != (UWORD)tail)
		return ("wrong heap page count or tail");
	if (PhysPages != npool || PhysFree != npool)
		return ("wrong pool page count");
	if ((k = heaphdrs()) != npages + (tail != 0))
		return ("MemList does not match the heap pages");
	/* The first region holds its own MemHeader and every other one. */
	if (AvailMem(MEMF_UPPER | MEMF_TOTAL) !=
	    (ULONG)npages * 0x10000L + tail - hdrbytes())
		return ("AvailMem total does not match the heap pages");
	rel = 0;
	tails = 0;
	for (i = 0; i < k; i++) {
		base = hdr[i]->mh_Lower - (i == 0 ? (int)hdrbytes() : 0);
		seg = (int)(PSEG(base) >> 24);
		ph = kphys((APTR)base);
		if (POFF(base) != 0) {
			/* The partial page: first in the list, identity. */
			tails++;
			if (i != 0 || seg != first - 1 ||
			    POFF(base) != 0x10000L - tail ||
			    ph != ((ULONG)seg << 16) + POFF(base))
				return ("partial page is not the stack's end");
		} else if ((ph & 0xffffL) != 0)
			return ("heap page does not start at offset 0");
		else if (ph == (ULONG)seg << 16)
			;
		else if (seg >= RELOCSEG0 && seg <= RELOCSEGN) {
			rel++;
			if ((ph >> 16) < NMMUSEG && !(ph >> 16 == KTEXTSEG ||
			    ph >> 16 == KDATASEG))
				return ("relocated page without a collision");
		} else
			return ("heap page not identity-mapped");
	}
	if (rel != nreloc || tails != (tail != 0))
		return ("wrong relocated page count");

	/* One block per page, tagged with its index: aliased pages would
	 * overwrite each other's tags. */
	Forbid();
	for (i = 0; i < k; i++)
		if ((blk[i] = (UWORD *)kalloc(hdr[i], 16L, 0)) == NULL) {
			Permit();
			return ("a heap page has no room for 16 bytes");
		}
	for (i = 0; i < k; i++) {
		blk[i][0] = 0xa500 + i;
		blk[i][7] = ~(0xa500 + i);
	}
	for (i = 0; i < k; i++)
		if (blk[i][0] != 0xa500 + i ||
		    blk[i][7] != (UWORD)~(0xa500 + i)) {
			Permit();
			return ("heap pages alias or are not RAM");
		}

	/* A window onto a block's physical address sees the block. */
	w = (UWORD *)kwinmap(0, kphys((APTR)blk[0]));
	j = PSEG(w) == SEGBASE(WINSEG0) && *w == 0xa500;
	*w = 0x5a5a;
	j = j && blk[0][0] == 0x5a5a;
	kwinunmap(0);
	for (i = 0; i < k; i++)
		kdeallocate(hdr[i], (APTR)blk[i], 16L);
	Permit();
	if (!j || kmmutab[WINSEG0] != MMU_NONE)
		return ("window 0 does not show the block");

	if (kphys((APTR)SysBase) < ((ULONG)KPHYSPAGE << 16) ||
	    kphys((APTR)SysBase) >= stk)
		return ("kernel data is not below the stack segment");
	if (kphys((APTR)&local) < stk ||
	    kphys((APTR)&local) >= stk + 0x10000L - kstklow)
		return ("a frame is not in the stack segment's RAM");
	if (kphys(NULL) != 0xffffffffL || kphys((APTR)0x40000000L) !=
	    0xffffffffL)
		return ("kphys maps segment 0 or 0x40");
	if (kmmutab[NULLSEG] != MMU_NONE || kmmutab[HRSEG1] != MMU_NONE)
		return ("segment 0 or 1 is mapped");
	xKPrintf("memmap: stack %lx, %d heap pages + %lu bytes, ", stk, npages,
	    tail);
	xKPrintf("%d relocated, %d pool, last at %lx\n", rel, npool,
	    kphys((APTR)hdr[k - 1]->mh_Lower));
	return (NULL);
}

/*
 * Every pool page through window 0: tag each, then check every tag, so a
 * page that aliases another or a heap page shows.  Then return them all.
 */
static UBYTE poolpg[256];

static char *
poolcheck(npool)
int npool;
{
	register int i, n, pg;
	register UWORD *w;

	for (n = 0; (pg = kallocphys()) >= 0; n++) {
		if (n >= 256 || pg < NMMUSEG)
			return ("pool page below 0x40");
		poolpg[n] = pg;
	}
	if (n != npool || PhysFree != 0)
		return ("wrong pool page count from kallocphys");
	Forbid();
	for (i = 0; i < n; i++) {
		w = (UWORD *)kwinmap(0, (ULONG)poolpg[i] << 16);
		w[0] = 0xc900 + poolpg[i];
		w[0x7fff] = ~(0xc900 + poolpg[i]);
	}
	for (i = 0; i < n; i++) {
		w = (UWORD *)kwinmap(0, (ULONG)poolpg[i] << 16);
		if (w[0] != 0xc900 + poolpg[i] ||
		    w[0x7fff] != (UWORD)~(0xc900 + poolpg[i])) {
			Permit();
			return ("pool pages alias or are not RAM");
		}
	}
	/* Window 1 onto a 1 KB-aligned base inside a page. */
	w = (UWORD *)kwinmap(1, ((ULONG)poolpg[0] << 16) + 0x0c02L);
	if (POFF(w) != 0x0002L || kmmutab[WINSEG1] !=
	    (((UWORD)poolpg[0] << 8) | 0x0c)) {
		Permit();
		return ("window 1 base or offset");
	}
	w[-1] = 0x1357;
	w = (UWORD *)kwinmap(0, (ULONG)poolpg[0] << 16);
	if (w[0x0c00 / 2] != 0x1357) {
		Permit();
		return ("window 1 and window 0 disagree");
	}
	kwinunmap(0);
	kwinunmap(1);
	Permit();
	for (i = 0; i < n; i++)
		kfreephys(poolpg[i]);
	if (PhysFree != npool)
		return ("kfreephys did not return the pages");
	xKPrintf("pool: %d pages %x-%x through the windows\n", n, poolpg[0],
	    poolpg[n - 1]);
	return (NULL);
}
