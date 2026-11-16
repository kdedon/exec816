/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * MemHeader allocator and system memory calls.  Port of lib/exec/heapcore.act
 * and heappolicy.act; the contract is docs/reference/memory.md, with the
 * Amiga departures in docs/platform.md.
 *
 * A MemHeader manages one range of one segment.  Pointer arithmetic only
 * changes the offset word and a pointer to offset 0x10000 wraps to 0, so all
 * range arithmetic is done on ULONG offsets and pointers are rebuilt with
 * MKPTR.  mh_Upper is the exclusive end offset, at most 0x10000.  Free chunks
 * are kept in address order and coalesced; Deallocate writes only at the
 * start of the freed block or of its free predecessor.
 *
 * A block over 64 KB spans consecutive identity-mapped segments, so it is
 * physically contiguous.  It is handled as a linear address, seg << 16 |
 * offset, with one piece per segment.
 */
#include "kernel.h"

#define GRAIN		8L
#define GRAINMASK	(~(GRAIN - 1))
#define SEGBYTES	0x10000L
#define VECPREFIX	8L
#define MAXBLOCK	((ULONG)NMMUSEG << 16)

/* Small ordinary blocks and the stack bytes they leave (xAllocMem). */
#define SMALLBLK	128L
#define STKKEEP		20480L

#define ROUND(n)	(((n) + GRAIN - 1) & GRAINMASK)

/* Linear address of a pointer, and back. */
#define LIN(p)		((((ULONG)(p) >> 8) & 0x7f0000L) | POFF(p))
#define UNLIN(l)	MKPTR(((l) << 8) & 0x7f000000L, (l) & 0xffffL)

/* Every region is RAM the CPU and DMA both reach, below 16 MB. */
#define RAMATTR		(MEMF_PUBLIC | MEMF_FAST | MEMF_LOCAL | MEMF_DMA24 | \
			MEMF_KICK)
#define PLACEFLAGS	(RAMATTR | MEMF_CHIP | MEMF_BANK0 | MEMF_UPPER)
#define VALIDFLAGS	(PLACEFLAGS | MEMF_CLEAR | MEMF_LARGEST | \
			MEMF_REVERSE | MEMF_TOTAL | MEMF_LINEAR | MEMF_NO_EXPUNGE)

/* The allocator runs under Forbid once Tasks exist. */
#define LOCK()		if (ThisTask != NULL) xForbid()
#define UNLOCK()	if (ThisTask != NULL) xPermit()

/* p stepped by delta bytes, carrying into the segment number. */
APTR
xPtrStep(p, delta)
APTR p;
LONG delta;
{
	ULONG l;

	l = LIN(p) + delta;
	return (UNLIN(l));
}

/* First fit, or with rev the top of the highest chunk that fits. */
APTR
kalloc(mh, size, rev)
register struct MemHeader *mh;
ULONG size;
int rev;
{
	register struct MemChunk *c, *prev, *rest;
	struct MemChunk *fit, *fprev;
	ULONG bytes;

	if (size == 0 || size > SEGBYTES)
		return (NULL);
	bytes = ROUND(size);
	if (bytes > mh->mh_Free)
		return (NULL);
	fit = fprev = NULL;
	prev = NULL;
	for (c = mh->mh_First; c != NULL; prev = c, c = c->mc_Next) {
		if (c->mc_Bytes < bytes)
			continue;
		fit = c;
		fprev = prev;
		if (!rev)
			break;
	}
	if ((c = fit) == NULL)
		return (NULL);
	mh->mh_Free -= bytes;
	if (c->mc_Bytes == bytes)
		rest = c->mc_Next;
	else if (rev) {
		c->mc_Bytes -= bytes;
		return (MKPTR(PSEG(c), POFF(c) + c->mc_Bytes));
	} else {
		rest = (struct MemChunk *)MKPTR(PSEG(c), POFF(c) + bytes);
		rest->mc_Next = c->mc_Next;
		rest->mc_Bytes = c->mc_Bytes - bytes;
	}
	if (fprev == NULL)
		mh->mh_First = rest;
	else
		fprev->mc_Next = rest;
	return ((APTR)c);
}

void
kdeallocate(mh, p, size)
register struct MemHeader *mh;
APTR p;
ULONG size;
{
	register struct MemChunk *prev, *next, *j;
	ULONG low, high;

	if (size == 0)
		return;
	low = POFF(p) & GRAINMASK;
	high = ROUND(POFF(p) + size);
	if (p == NULL || size > SEGBYTES || PSEG(p) != PSEG(mh->mh_Lower) ||
	    low < POFF(mh->mh_Lower) || high > mh->mh_Upper)
		kpanic("Deallocate: bad block");
	prev = NULL;
	for (next = mh->mh_First; next != NULL && POFF(next) < low;
	    next = next->mc_Next)
		prev = next;
	if ((prev != NULL && POFF(prev) + prev->mc_Bytes > low) ||
	    (next != NULL && high > POFF(next)))
		kpanic("Deallocate: block is free");
	if (prev != NULL && POFF(prev) + prev->mc_Bytes == low) {
		j = prev;
		j->mc_Bytes += high - low;
	} else {
		j = (struct MemChunk *)MKPTR(PSEG(mh->mh_Lower), low);
		j->mc_Bytes = high - low;
		if (prev == NULL)
			mh->mh_First = j;
		else
			prev->mc_Next = j;
	}
	if (next != NULL && high == POFF(next)) {
		j->mc_Bytes += next->mc_Bytes;
		j->mc_Next = next->mc_Next;
	} else
		j->mc_Next = next;
	mh->mh_Free += high - low;
}

/* Caller-owned pools: the caller provides synchronisation. */
APTR
xAllocate(mh, size)
struct MemHeader *mh;
ULONG size;
{
	return (kalloc(mh, size, 0));
}

void
xDeallocate(mh, p, size)
struct MemHeader *mh;
APTR p;
ULONG size;
{
	kdeallocate(mh, p, size);
}

/* Make mh manage [lower, upper offset) as one free chunk. */
void
kmhinit(mh, lower, upper)
register struct MemHeader *mh;
APTR lower;
ULONG upper;
{
	register struct MemChunk *c;

	mh->mh_Node.ln_Type = NT_MEMORY;
	mh->mh_Lower = (UBYTE *)lower;
	mh->mh_Upper = upper;
	mh->mh_Free = upper - POFF(lower);
	c = (struct MemChunk *)lower;
	c->mc_Next = NULL;
	c->mc_Bytes = mh->mh_Free;
	mh->mh_First = c;
}

/*
 * Register [lower_off, upper_off) of base's segment.  Boot calls this
 * before any Task runs.  The MemHeader comes from the regions already
 * registered, so a later page is free from offset 0; the first region
 * holds its own.
 */
struct MemHeader *
kaddmem(base, lower_off, upper_off, attrs, pri, name)
APTR base;
ULONG lower_off, upper_off;
UWORD attrs;
int pri;
char *name;
{
	register struct MemHeader *mh;
	ULONG lo, hi;

	lo = ROUND(POFF(base) + lower_off);
	hi = upper_off & GRAINMASK;
	if (hi > SEGBYTES)
		hi = SEGBYTES;
	if (lo >= hi)
		return (NULL);
	mh = (struct MemHeader *)xAllocMem((ULONG)sizeof(struct MemHeader),
	    (ULONG)MEMF_PUBLIC);
	if (mh == NULL) {
		mh = (struct MemHeader *)MKPTR(PSEG(base), lo);
		lo = ROUND(lo + (ULONG)sizeof(struct MemHeader));
		if (lo >= hi)
			return (NULL);
	}
	kmhinit(mh, MKPTR(PSEG(base), lo), hi);
	mh->mh_Node.ln_Pri = pri;
	mh->mh_Node.ln_Name = name;
	mh->mh_Attributes = attrs | RAMATTR;
	xEnqueue(&ExecLib.MemList, (struct Node *)mh);
	return (mh);
}

static int
badflags(flags)
ULONG flags;
{
	return ((flags & ~VALIDFLAGS) != 0 || ((flags & MEMF_BANK0) &&
	    (flags & (MEMF_UPPER | MEMF_LINEAR))));
}

/* The attributes a region needs: BANK0 selects only the stack region,
 * otherwise AllocMem first tries upper RAM. */
static UWORD
reqof(flags)
ULONG flags;
{
	register UWORD req;

	req = flags & PLACEFLAGS;
	if ((req & MEMF_BANK0) == 0)
		req |= MEMF_UPPER;
	return (req);
}

static struct MemHeader *
memfind(p)
APTR p;
{
	register struct Node *n;
	register struct MemHeader *mh;
	ULONG off;

	off = POFF(p);
	for (n = ExecLib.MemList.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
		mh = (struct MemHeader *)n;
		if (PSEG(p) == PSEG(mh->mh_Lower) &&
		    off >= POFF(mh->mh_Lower) && off < mh->mh_Upper)
			return (mh);
	}
	return (NULL);
}

/* The MemHeader of an identity-mapped segment's offset 0 with attributes
 * req, or NULL. */
static struct MemHeader *
pagemh(l, req)
ULONG l;
UWORD req;
{
	register struct MemHeader *mh;
	APTR q;

	q = UNLIN(l);
	if ((mh = memfind(q)) == NULL || (mh->mh_Attributes & req) != req ||
	    kphys(q) != l)
		return (NULL);
	return (mh);
}

/* Clear n bytes from the word-aligned p, across segments. */
void
kmemclr(p, n)
APTR p;
ULONG n;
{
	register ULONG k;

	while (n != 0) {
		k = SEGBYTES - POFF(p);
		if (k > n)
			k = n;
		kwfill(p, 0, (UWORD)(k >> 1));
		if (k & 1)
			((UBYTE *)p)[(UWORD)k - 1] = 0;
		n -= k;
		p = xPtrStep(p, k);
	}
}

/*
 * Claim the free range [l, l + size), rounded to the granule, from the
 * regions with attributes req: check every piece, then take them.  A
 * range over several segments must lie in identity-mapped pages.
 */
static int
claim(l, size, req)
ULONG l, size;
UWORD req;
{
	register struct MemChunk *c, *prev, *r;
	register struct MemHeader *mh;
	ULONG a, h, n, off, end;
	int multi, doit;

	h = ROUND(l + size);
	l &= GRAINMASK;
	multi = ((h - 1) >> 16) != (l >> 16);
	for (doit = 0; doit < 2; doit++) {
		for (a = l; a < h; a += n) {
			off = a & 0xffffL;
			n = SEGBYTES - off;
			if (n > h - a)
				n = h - a;
			if ((mh = multi ? pagemh(a, req) : memfind(UNLIN(a))) ==
			    NULL || (mh->mh_Attributes & req) != req)
				return (0);
			prev = NULL;
			for (c = mh->mh_First; c != NULL && POFF(c) <= off;
			    prev = c, c = c->mc_Next)
				if (POFF(c) + c->mc_Bytes >= off + n)
					break;
			if (c == NULL || POFF(c) > off)
				return (0);
			if (!doit)
				continue;
			end = POFF(c) + c->mc_Bytes;
			r = c->mc_Next;
			if (end > off + n) {
				r = (struct MemChunk *)MKPTR(PSEG(c), off + n);
				r->mc_Next = c->mc_Next;
				r->mc_Bytes = end - off - n;
			}
			if (POFF(c) < off) {
				c->mc_Bytes = off - POFF(c);
				c->mc_Next = r;
			} else if (prev == NULL)
				mh->mh_First = r;
			else
				prev->mc_Next = r;
			mh->mh_Free -= n;
		}
	}
	return (1);
}

static void
memfree(p, size)
APTR p;
ULONG size;
{
	struct MemHeader *mh;
	ULONG n;

	while (size != 0) {
		n = SEGBYTES - POFF(p);
		if (n > size)
			n = size;
		if ((mh = memfind(p)) == NULL)
			kpanic("FreeMem: bad pointer");
		kdeallocate(mh, p, n);
		size -= n;
		p = xPtrStep(p, n);
	}
}

/*
 * Up to 64 KB from one region, walking the list backwards for REVERSE;
 * more from offset 0 of the first (REVERSE: last) segment that starts a
 * free identity-mapped run.
 */
static APTR
memget(size, flags)
ULONG size, flags;
{
	register struct Node *n;
	register UWORD req;
	register int rev, i;
	ULONG l;
	APTR p;

	req = reqof(flags);
	rev = (flags & MEMF_REVERSE) != 0;
	p = NULL;
	LOCK();
	if (size <= SEGBYTES) {
		for (n = rev ? ExecLib.MemList.lh_TailPred : ExecLib.MemList.lh_Head;
		    n->ln_Succ != NULL && n->ln_Pred != NULL;
		    n = rev ? n->ln_Pred : n->ln_Succ) {
			if ((((struct MemHeader *)n)->mh_Attributes & req) != req)
				continue;
			if ((p = kalloc((struct MemHeader *)n, size, rev)) != NULL)
				break;
		}
	} else
		for (i = 0; i < NMMUSEG; i++) {
			l = (ULONG)(rev ? NMMUSEG - 1 - i : i) << 16;
			if (claim(l, size, req)) {
				p = UNLIN(l);
				break;
			}
		}
	UNLOCK();
	if (p != NULL && (flags & MEMF_CLEAR))
		kmemclr(p, size);
	return (p);
}

/* Low-memory handlers, by priority (Amiga V39). */
static struct List MemHandlers = {
	(struct Node *)&MemHandlers.lh_Tail,
	NULL,
	(struct Node *)&MemHandlers.lh_Head,
	NT_INTERRUPT,
	0
};
static int MemHandBusy;

void
xAddMemHandler(is)
struct Interrupt *is;
{
	xForbid();
	xEnqueue(&MemHandlers, &is->is_Node);
	xPermit();
}

void
xRemMemHandler(is)
struct Interrupt *is;
{
	xForbid();
	xRemove(&is->is_Node);
	xPermit();
}

/*
 * When the heap runs out, each handler in turn is called under Forbid as
 * is_Code(memHandlerData, is_Data, SysBase), then the allocation is
 * retried: MEM_DID_NOTHING moves on without a retry, MEM_TRY_AGAIN calls
 * the same handler again with MEMHF_RECYCLE if the retry fails.  Not
 * with MEMF_NO_EXPUNGE, before Tasks exist, or from inside a handler.
 * A request naming none of BANK0, UPPER and DMA24 then takes the top of
 * the stack region, which only stacks need, so it goes last.  DMA24 stays
 * out: the region's RAM can cross a physical 64 KB page.  Such a request
 * of SMALLBLK bytes or less, without REVERSE, takes the region's top first
 * while the region
 * keeps STKKEEP free: long-lived nodes would otherwise split the page runs
 * of blocks over 64 KB.
 */
APTR
xAllocMem(size, flags)
ULONG size, flags;
{
	register struct Interrupt *is;
	register LONG r;
	struct MemHandlerData mhd;
	APTR p;

	if (size == 0 || size > MAXBLOCK || badflags(flags))
		return (NULL);
	if (size <= SMALLBLK && (flags & (MEMF_BANK0 | MEMF_UPPER |
	    MEMF_DMA24 | MEMF_REVERSE)) == 0 &&
	    ((struct MemHeader *)ExecLib.MemList.lh_TailPred)->mh_Free >=
	    size + STKKEEP &&
	    (p = memget(size, flags | MEMF_BANK0 | MEMF_REVERSE)) != NULL)
		return (p);
	if ((p = memget(size, flags)) != NULL || ThisTask == NULL ||
	    (flags & MEMF_NO_EXPUNGE) || MemHandBusy)
		goto spill;
	mhd.memh_RequestSize = size;
	mhd.memh_RequestFlags = flags;
	mhd.memh_Flags = 0;
	xForbid();
	MemHandBusy = 1;
	for (is = (struct Interrupt *)MemHandlers.lh_Head;
	    is->is_Node.ln_Succ != NULL; ) {
		r = (*(LONG (*)())is->is_Code)(&mhd, is->is_Data, SysBase);
		if (r != MEM_DID_NOTHING && (p = memget(size, flags)) != NULL)
			break;
		if (r == MEM_TRY_AGAIN)
			mhd.memh_Flags = MEMHF_RECYCLE;
		else {
			mhd.memh_Flags = 0;
			is = (struct Interrupt *)is->is_Node.ln_Succ;
		}
	}
	MemHandBusy = 0;
	xPermit();
spill:
	if (p == NULL && (flags & (MEMF_BANK0 | MEMF_UPPER | MEMF_DMA24)) == 0)
		p = memget(size, flags | MEMF_BANK0 | MEMF_REVERSE);
	return (p);
}

/* Amiga AllocAbs: the block at p, which is returned. */
APTR
xAllocAbs(size, p)
ULONG size;
APTR p;
{
	int ok;

	if (size == 0 || size > MAXBLOCK)
		return (NULL);
	LOCK();
	ok = claim(LIN(p), size, 0);
	UNLOCK();
	return (ok ? p : NULL);
}

/* Any part of an allocated block may be freed. */
void
xFreeMem(p, size)
APTR p;
ULONG size;
{
	if (size == 0)
		return;
	LOCK();
	memfree(p, size);
	UNLOCK();
}

/* Amiga CopyMem; CopyMemQuick is the same call.  Moves are split at
 * either pointer's segment end. */
void
xCopyMem(src, dst, size)
APTR src, dst;
ULONG size;
{
	register ULONG n, k;

	while (size != 0) {
		n = SEGBYTES - POFF(src);
		if ((k = SEGBYTES - POFF(dst)) < n)
			n = k;
		if (n > size)
			n = size;
		if (n > 0x8000L)
			n = 0x8000L;
		if (((POFF(src) | POFF(dst) | n) & 1) == 0)
			kwmove(dst, src, (UWORD)(n >> 1));
		else
			kbmove(dst, src, (UWORD)n);
		size -= n;
		src = xPtrStep(src, n);
		dst = xPtrStep(dst, n);
	}
}

/* Bytes of a MemList with n entries. */
#define MLSIZE(n)	((ULONG)sizeof(struct MemList) + \
			((ULONG)(n) - 1) * sizeof(struct MemEntry))

/*
 * Amiga AllocEntry: a new MemList holding one block per entry of ml.  On
 * failure nothing stays allocated and the result is the failing entry's
 * requirements (MEMF_PUBLIC for the list itself) with bit 31 set; no
 * pointer has bit 31 set.
 */
struct MemList *
xAllocEntry(ml)
register struct MemList *ml;
{
	register struct MemList *r;
	register int i;
	ULONG len;
	APTR p;

	r = (struct MemList *)xAllocMem(MLSIZE(ml->ml_NumEntries),
	    (ULONG)(MEMF_PUBLIC | MEMF_CLEAR));
	if (r == NULL)
		return ((struct MemList *)(0x80000000L | MEMF_PUBLIC));
	r->ml_NumEntries = ml->ml_NumEntries;
	for (i = 0; i < ml->ml_NumEntries; i++) {
		len = ml->ml_ME[i].me_Length;
		if (len != 0) {
			p = xAllocMem(len, ml->ml_ME[i].me_Reqs);
			if (p == NULL) {
				len = ml->ml_ME[i].me_Reqs;
				while (--i >= 0)
					xFreeMem(r->ml_ME[i].me_Addr,
					    r->ml_ME[i].me_Length);
				xFreeMem((APTR)r, MLSIZE(r->ml_NumEntries));
				return ((struct MemList *)(0x80000000L | len));
			}
			r->ml_ME[i].me_Addr = p;
		}
		r->ml_ME[i].me_Length = len;
	}
	return (r);
}

/* Free every entry of an AllocEntry list, then the list. */
void
xFreeEntry(ml)
register struct MemList *ml;
{
	register int i;

	for (i = 0; i < ml->ml_NumEntries; i++)
		xFreeMem(ml->ml_ME[i].me_Addr, ml->ml_ME[i].me_Length);
	xFreeMem((APTR)ml, MLSIZE(ml->ml_NumEntries));
}

/* An 8-byte prefix {backing, requested} precedes the caller's block. */
APTR
xAllocVec(size, flags)
ULONG size, flags;
{
	register ULONG *v;
	ULONG backing;

	if (size == 0 || size > MAXBLOCK)
		return (NULL);
	backing = ROUND(size) + VECPREFIX;
	v = (ULONG *)xAllocMem(backing, flags & ~MEMF_CLEAR);
	if (v == NULL)
		return (NULL);
	v[0] = backing;
	v[1] = size;
	v += 2;
	if (flags & MEMF_CLEAR)
		kmemclr((APTR)v, size);
	return ((APTR)v);
}

void
xFreeVec(p)
APTR p;
{
	register ULONG *v;
	ULONG req;

	if (p == NULL)
		return;
	if (POFF(p) < VECPREFIX || (POFF(p) & (GRAIN - 1)) != 0 ||
	    memfind(p) == NULL)
		kpanic("FreeVec: bad pointer");
	v = (ULONG *)p - 2;
	req = v[1];
	if (req == 0 || req > MAXBLOCK || v[0] != ROUND(req) + VECPREFIX)
		kpanic("FreeVec: bad pointer");
	LOCK();
	memfree((APTR)v, v[0]);
	UNLOCK();
}

/*
 * LARGEST also counts runs: whole free identity-mapped pages from offset
 * 0, plus the free chunk at offset 0 of the next page, as AllocMem
 * places a block over 64 KB.
 */
ULONG
xAvailMem(flags)
ULONG flags;
{
	register struct Node *n;
	register struct MemHeader *mh;
	register struct MemChunk *c;
	ULONG total, v, run;
	UWORD req;
	int s;

	if (badflags(flags))
		return (0);
	req = flags & PLACEFLAGS;
	total = run = 0;
	LOCK();
	for (n = ExecLib.MemList.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
		mh = (struct MemHeader *)n;
		if ((mh->mh_Attributes & req) != req)
			continue;
		if (flags & MEMF_TOTAL)
			total += mh->mh_Upper - POFF(mh->mh_Lower);
		else if (flags & MEMF_LARGEST) {
			for (c = mh->mh_First; c != NULL; c = c->mc_Next)
				if (c->mc_Bytes > total)
					total = c->mc_Bytes;
		} else
			total += mh->mh_Free;
	}
	if ((flags & (MEMF_LARGEST | MEMF_TOTAL)) == MEMF_LARGEST)
		for (s = 0; s < NMMUSEG; s++) {
			if ((mh = pagemh((ULONG)s << 16, req)) == NULL) {
				run = 0;
				continue;
			}
			if (mh->mh_Free == SEGBYTES)
				v = run += SEGBYTES;
			else {
				v = run;
				if ((c = mh->mh_First) != NULL && POFF(c) == 0)
					v += c->mc_Bytes;
				run = 0;
			}
			if (v > total)
				total = v;
		}
	UNLOCK();
	return (total);
}

/* MemList is fixed after boot, so no lock is needed. */
ULONG
xTypeOfMem(p)
APTR p;
{
	struct MemHeader *mh;

	if ((mh = memfind(p)) == NULL)
		return (0);
	return ((ULONG)mh->mh_Attributes);
}

/*
 * Amiga CachePreDMA: the physical address of a, for a DMA transfer; the
 * Z8001 has no caches to flush.  *len is cut at the end of a's segment,
 * past which physical memory need not continue.  An unmapped address
 * gives 0xFFFFFFFF and a length of 0.
 */
APTR
xCachePreDMA(a, len, flags)
APTR a;
ULONG *len;
ULONG flags;
{
	register ULONG p;
	ULONG room;

	p = kphys(a);
	room = 0x10000L - POFF(a);
	if (p == 0xffffffffL)
		*len = 0;
	else if (*len > room)
		*len = room;
	return ((APTR)p);
}

void
xCachePostDMA(a, len, flags)
APTR a;
ULONG *len;
ULONG flags;
{
}

