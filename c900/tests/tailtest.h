/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Disk DMA into the memory the image layout gives back, shared by the
 * tailmem and tailpad tests: the partial heap page after the stack
 * segment, the first whole heap page, and a stack-segment buffer lying
 * across a physical 64 KB boundary (the stack segment starts at a 1 KB
 * boundary, not a page).  Reads match the pattern partition and writes
 * read back.
 *
 * Where the boundary falls depends on the kernel image's size.  It cannot
 * be used within 1 KB of the interrupt stack at the pool's low end or above
 * the pool, so tailpad boots a kernel with 44 KB more bss
 * (kimage/tailpad.c): for any image from about 38 KB to 82 KB one of the
 * two tests has it inside the pool.
 */
#include "wdtest.h"

#define N	4

/* The heap MemHeader of segment seg; with part, the partial page's.  The
 * MemHeaders live in the first region, so a whole page starts at offset 0
 * (32 when it is that region). */
static struct MemHeader *
heaphdr(seg, part)
int seg, part;
{
	register struct Node *n;

	for (n = SysBase->MemList.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if ((((struct MemHeader *)n)->mh_Attributes & MEMF_UPPER) &&
		    PSEG(((struct MemHeader *)n)->mh_Lower) == SEGBASE(seg) &&
		    (POFF(((struct MemHeader *)n)->mh_Lower) > 32) == part)
			return ((struct MemHeader *)n);
	return (NULL);
}

static char *
run()
{
	struct IOStdReq *r, *s;
	struct MemHeader *mh;
	struct MemChunk *mc, *big;
	UBYTE *tail, *page, *stk;
	ULONG heap, base, cross, size;
	int first;

	heap = ((ULONG)kmmutab[STACKSEG] << 8) + 0x10000L;
	first = (int)((heap + 0xffffL) >> 16);

	/* The partial page past the MemHeaders it holds.  Small
	 * allocations come from it first, the root program's hunks among
	 * them; its largest free chunk is the rest of the page. */
	tail = NULL;
	if (HeapTail != 0) {
		if ((mh = heaphdr(first - 1, 1)) == NULL ||
		    POFF(mh) != (heap & 0xffffL))
			return ("no partial heap page");
		big = NULL;
		for (mc = mh->mh_First; mc != NULL; mc = mc->mc_Next)
			if (big == NULL || mc->mc_Bytes > big->mc_Bytes)
				big = mc;
		size = big != NULL ? big->mc_Bytes : 0L;
		tail = (UBYTE *)AllocAbs(size, big);
		if (tail == NULL || POFF(tail) + size != 0x10000L ||
		    size < 4L * WD_SECTOR)
			return ("partial page does not end in a free block");
		tail = (UBYTE *)MKPTR(PSEG(tail), (POFF(tail) + 2L * WD_SECTOR -
		    1) & ~(WD_SECTOR - 1L));
		if (kphys((APTR)tail) != (heap & ~0xffffL) + POFF(tail))
			return ("partial page is not identity-mapped");
	}
	if ((r = wdopen(WD_SLOT(0, 0))) == NULL ||
	    (s = wdopen(WD_SLOT(0, 1))) == NULL)
		return ("open");

	/* The first whole heap page, physical page = segment. */
	if ((mh = heaphdr(first, 0)) == NULL ||
	    (page = (UBYTE *)Allocate(mh, (ULONG)(N + 1) * WD_SECTOR)) == NULL)
		return ("no whole heap page after the stack segment");
	page += (UWORD)(WD_SECTOR - (POFF(page) & (WD_SECTOR - 1))) &
	    (WD_SECTOR - 1);
	if (kphys((APTR)page) != ((ULONG)first << 16) + POFF(page))
		return ("first heap page is not identity-mapped");
	if (wdio(r, CMD_READ, 9L, N, page) != 0 || patcheck(page, 9L, N) >= 0)
		return ("read into the first heap page");
	xKPrintf("tailmem: heap page read at %lx\n", kphys((APTR)page));

	/* One sector at the partial page's second 512-byte boundary. */
	if (tail == NULL)
		KPutStr("tailmem: no partial page\n");
	else {
		if (wdio(r, CMD_READ, 3L, 1, tail) != 0 ||
		    patcheck(tail, 3L, 1) >= 0)
			return ("read into the partial page");
		seedfill(tail, 0x1234, 1);
		if (wdio(s, CMD_WRITE, 40L, 1, tail) != 0 ||
		    wdio(s, CMD_READ, 40L, 1, page) != 0 ||
		    !seedcheck(page, 0x1234, 1))
			return ("write from the partial page");
		xKPrintf("tailmem: partial page %u bytes, sector at %lx\n",
		    HeapTail, kphys((APTR)tail));
	}

	/* Stack-segment sectors on both sides of a physical page boundary,
	 * when the boundary lies in the free stack pool. */
	base = (ULONG)kmmutab[STACKSEG] << 8;
	cross = 0x10000L - (base & 0xffffL);
	size = AvailMem(MEMF_STACK | MEMF_LARGEST);
	stk = (UBYTE *)AllocMem(size, MEMF_STACK);
	if (stk == NULL)
		return ("no stack pool");
	if (cross == 0x10000L || POFF(stk) + 2L * WD_SECTOR > cross ||
	    cross + 2L * WD_SECTOR > POFF(stk) + size)
		xKPrintf("tailmem: no boundary in the stack pool (%lx)\n",
		    cross);
	else {
		stk = (UBYTE *)MKPTR(PSEG(stk), cross - 2L * WD_SECTOR);
		if (((kphys((APTR)stk) + 2L * WD_SECTOR) & 0xffffL) != 0)
			return ("stack buffer is not at a page boundary");
		if (wdio(r, CMD_READ, 20L, N, stk) != 0 ||
		    patcheck(stk, 20L, N) >= 0)
			return ("read across the boundary in the stack");
		seedfill(stk, 0x4321, N);
		if (wdio(s, CMD_WRITE, 44L, N, stk) != 0 ||
		    wdio(s, CMD_READ, 44L, N, page) != 0 ||
		    !seedcheck(page, 0x4321, N))
			return ("write across the boundary in the stack");
		xKPrintf("tailmem: stack sectors at %lx across a page\n",
		    kphys((APTR)stk));
	}
	wdclose(r);
	wdclose(s);
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL)
		xKPrintf("FAIL %s: %s\n", TAILNAME, why);
	else
		xKPrintf("PASS %s\n", TAILNAME);
	return (0);
}
