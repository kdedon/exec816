/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Memory pools, as Amiga Exec V39.  A puddle is one AllocMem block that
 * starts with the MemHeader managing the rest of it; a request over the
 * threshold is its own AllocMem block, behind a 16-byte link.  The caller
 * serialises calls on one pool, unless it was created MEMF_SEM_PROTECTED:
 * that pool's handle is a Locked, whose calls hold the semaphore and run
 * on the unprotected Pool inside it.
 */
#include "kernel.h"

#define HDR	32L		/* MemHeader, rounded to the granule */
#define BIGHDR	16L

struct Pool {
	UWORD pl_Locked;	/* the handle is a Locked */
	struct MinList pl_Puddles;
	struct MinList pl_Large;
	ULONG pl_Reqs;
	ULONG pl_Size;		/* puddle block bytes, header included */
	ULONG pl_Thresh;
};

struct Locked {
	UWORD lk_Locked;	/* 1, at pl_Locked */
	struct SignalSemaphore lk_Sem;
	struct Pool lk_Pool;
};

#define LK(pl)	((struct Locked *)(pl))

struct Large {
	struct MinNode lg_Node;
	ULONG lg_Size;		/* block bytes, link included */
};

/* A puddle holds at most one segment, so a larger puddle size is cut to
 * that and the threshold with it. */
APTR
xCreatePool(reqs, puddle, thresh)
ULONG reqs, puddle, thresh;
{
	register struct Pool *pl;
	register struct Locked *lk;

	if (thresh > puddle)
		return (NULL);
	puddle = ((puddle + 7L) & ~7L) + HDR;
	if (puddle > 0x10000L) {
		puddle = 0x10000L;
		if (thresh > puddle - HDR)
			thresh = puddle - HDR;
	}
	lk = (struct Locked *)xAllocMem(reqs & MEMF_SEM_PROTECTED ?
	    (ULONG)sizeof(struct Locked) : (ULONG)sizeof(struct Pool),
	    reqs & ~(MEMF_CLEAR | MEMF_SEM_PROTECTED));
	if (lk == NULL)
		return (NULL);
	pl = (struct Pool *)lk;
	if (reqs & MEMF_SEM_PROTECTED) {
		reqs &= ~MEMF_SEM_PROTECTED;
		lk->lk_Locked = 1;
		xInitSemaphore(&lk->lk_Sem);
		pl = &lk->lk_Pool;
	}
	pl->pl_Locked = 0;
	xNewList((struct List *)&pl->pl_Puddles);
	xNewList((struct List *)&pl->pl_Large);
	pl->pl_Reqs = reqs;
	pl->pl_Size = puddle;
	pl->pl_Thresh = thresh;
	return ((APTR)lk);
}

APTR
xAllocPooled(pl, size)
register struct Pool *pl;
ULONG size;
{
	register struct MemHeader *mh;
	register struct Large *lg;
	APTR p;

	if (pl->pl_Locked) {
		xObtainSem(&LK(pl)->lk_Sem);
		p = xAllocPooled(&LK(pl)->lk_Pool, size);
		xReleaseSem(&LK(pl)->lk_Sem);
		return (p);
	}
	if (size == 0)
		return (NULL);
	if (size > pl->pl_Thresh) {
		lg = (struct Large *)xAllocMem(size + BIGHDR, pl->pl_Reqs);
		if (lg == NULL)
			return (NULL);
		lg->lg_Size = size + BIGHDR;
		xAddHead((struct List *)&pl->pl_Large, (struct Node *)lg);
		return ((APTR)((UBYTE *)lg + BIGHDR));
	}
	for (mh = (struct MemHeader *)pl->pl_Puddles.mlh_Head;
	    mh->mh_Node.ln_Succ != NULL;
	    mh = (struct MemHeader *)mh->mh_Node.ln_Succ)
		if ((p = kalloc(mh, size, 0)) != NULL)
			goto got;
	mh = (struct MemHeader *)xAllocMem(pl->pl_Size,
	    pl->pl_Reqs & ~MEMF_CLEAR);
	if (mh == NULL)
		return (NULL);
	kmhinit(mh, (APTR)((UBYTE *)mh + HDR), POFF(mh) + pl->pl_Size);
	mh->mh_Attributes = (UWORD)pl->pl_Reqs;
	xAddHead((struct List *)&pl->pl_Puddles, (struct Node *)mh);
	p = kalloc(mh, size, 0);
got:
	if (pl->pl_Reqs & MEMF_CLEAR)
		kmemclr(p, size);
	return (p);
}

/* An empty puddle goes back to the system. */
void
xFreePooled(pl, p, size)
register struct Pool *pl;
APTR p;
ULONG size;
{
	register struct MemHeader *mh;
	register struct Large *lg;

	if (pl->pl_Locked) {
		xObtainSem(&LK(pl)->lk_Sem);
		xFreePooled(&LK(pl)->lk_Pool, p, size);
		xReleaseSem(&LK(pl)->lk_Sem);
		return;
	}
	if (size > pl->pl_Thresh) {
		lg = (struct Large *)((UBYTE *)p - BIGHDR);
		xRemove((struct Node *)lg);
		xFreeMem((APTR)lg, lg->lg_Size);
		return;
	}
	for (mh = (struct MemHeader *)pl->pl_Puddles.mlh_Head;
	    mh->mh_Node.ln_Succ != NULL;
	    mh = (struct MemHeader *)mh->mh_Node.ln_Succ)
		if (PSEG(p) == PSEG(mh) && POFF(p) >= POFF(mh->mh_Lower) &&
		    POFF(p) < mh->mh_Upper) {
			kdeallocate(mh, p, size);
			if (mh->mh_Free == pl->pl_Size - HDR) {
				xRemove((struct Node *)mh);
				xFreeMem((APTR)mh, pl->pl_Size);
			}
			return;
		}
	kpanic("FreePooled: bad pointer");
}

void
xDeletePool(pl)
register struct Pool *pl;
{
	register struct Node *n;
	register struct Locked *lk;

	if ((lk = LK(pl)) == NULL)
		return;
	if (lk->lk_Locked) {
		xObtainSem(&lk->lk_Sem);
		pl = &lk->lk_Pool;
	}
	while ((n = xRemHead((struct List *)&pl->pl_Puddles)) != NULL)
		xFreeMem((APTR)n, pl->pl_Size);
	while ((n = xRemHead((struct List *)&pl->pl_Large)) != NULL)
		xFreeMem((APTR)n, ((struct Large *)n)->lg_Size);
	if (lk->lk_Locked) {
		xReleaseSem(&lk->lk_Sem);
		xFreeMem((APTR)lk, (ULONG)sizeof(struct Locked));
	} else
		xFreeMem((APTR)pl, (ULONG)sizeof(struct Pool));
}

/* AllocPooled of the size plus VECHDR, the total stored first; the long
 * of padding keeps the result on the granule.  Only AllocPooled and
 * FreePooled take a protected pool's semaphore. */
#define VECHDR	8L

APTR
xAllocVecPool(pl, size)
struct Pool *pl;
ULONG size;
{
	register ULONG *v;

	if (pl == NULL || size == 0 || size > 0xFFFFFFFFL - VECHDR)
		return (NULL);
	size += VECHDR;
	if ((v = (ULONG *)xAllocPooled(pl, size)) == NULL)
		return (NULL);
	v[0] = size;
	return ((APTR)(v + 2));
}

void
xFreeVecPool(pl, p)
struct Pool *pl;
APTR p;
{
	register ULONG *v;

	if (p == NULL)
		return;
	v = (ULONG *)p - 2;
	xFreePooled(pl, (APTR)v, v[0]);
}
