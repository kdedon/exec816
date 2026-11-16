/* SPDX-License-Identifier: MIT */
/*
 * Memory: the examples/memory.act exchange plus alignment, CLEAR,
 * coalescing, AllocVec, AvailMem queries, the stack pool, accepted and
 * rejected flags, moving to the next region on exhaustion, a 65000-byte
 * block, AllocEntry/FreeEntry and the low-memory handlers.
 */
#include "test.h"

#define NBLK	40
#define BLK	4096L

/* Every region's attributes besides BANK0 or UPPER. */
#define RAM	(MEMF_PUBLIC | MEMF_FAST | MEMF_LOCAL | MEMF_DMA24 | MEMF_KICK)

static APTR blk[NBLK];

static int
zero(p, n)
register UBYTE *p;
register ULONG n;
{
	for (; n != 0; n--)
		if (*p++ != 0)
			return (0);
	return (1);
}

static void
fill(p, n, v)
register UBYTE *p;
register ULONG n;
int v;
{
	for (; n != 0; n--)
		*p++ = v;
}

/* Upper-RAM regions with room for one BLK. */
static int
regions()
{
	register struct Node *n;
	int k;

	k = 0;
	for (n = SysBase->MemList.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if ((((struct MemHeader *)n)->mh_Attributes & MEMF_UPPER) &&
		    ((struct MemHeader *)n)->mh_Free >= BLK)
			k++;
	return (k);
}

static char *
example()
{
	UBYTE *buf;
	ULONG avail;

	avail = AvailMem(0);
	if (AvailMem(MEMF_LARGEST | MEMF_LINEAR) != AvailMem(MEMF_LARGEST))
		return ("LINEAR changed a query");
	buf = (UBYTE *)AllocMem(513, MEMF_PUBLIC | MEMF_CLEAR);
	if (buf == NULL)
		return ("AllocMem 513");
	if (!zero(buf, 513L))
		return ("513 not clear");
	buf[512] = 0x5a;
	if (AvailMem(0) != avail - 520)
		return ("513 not rounded to 520");
	FreeMem(buf, 513);
	if ((buf = (UBYTE *)AllocVec(0x10001L, MEMF_LINEAR | MEMF_CLEAR)) ==
	    NULL)
		return ("LINEAR AllocVec");
	FreeVec(buf);
	if (AvailMem(0) != avail)
		return ("example leaked");
	return (NULL);
}

static char *
basics()
{
	UBYTE *a, *b, *c;
	ULONG avail, largest, total;
	int i;

	avail = AvailMem(MEMF_ANY);
	largest = AvailMem(MEMF_LARGEST);
	total = AvailMem(MEMF_TOTAL);
	if (avail == 0 || largest == 0 || largest > avail || total < avail)
		return ("AvailMem snapshot");
	if (AvailMem(MEMF_LARGEST | MEMF_TOTAL) != total)
		return ("LARGEST|TOTAL is not TOTAL");

	/* Alignment for every small size. */
	for (i = 1; i <= 17; i++) {
		if ((a = (UBYTE *)AllocMem(i, MEMF_ANY)) == NULL)
			return ("small AllocMem");
		if ((POFF(a) & 7) != 0)
			return ("misaligned block");
		if (AvailMem(0) != avail - ((i + 7) & ~7))
			return ("size not rounded to 8");
		FreeMem(a, i);
	}

	/* CLEAR clears reused, dirty storage. */
	a = (UBYTE *)AllocMem(300, 0);
	fill(a, 300L, 0xa5);
	FreeMem(a, 300);
	b = (UBYTE *)AllocMem(300, MEMF_CLEAR);
	if (b != a)
		return ("first fit did not reuse the freed block");
	if (!zero(b, 300L))
		return ("CLEAR");
	FreeMem(b, 300);

	/* Free the middle, then each neighbour: all three coalesce.  All
	 * over 128 bytes, which go to the stack region first. */
	a = (UBYTE *)AllocMem(140, 0);
	b = (UBYTE *)AllocMem(200, 0);
	c = (UBYTE *)AllocMem(300, 0);
	if (a == NULL || b == NULL || c == NULL)
		return ("AllocMem a, b, c");
	if (POFF(b) != POFF(a) + 144 || POFF(c) != POFF(b) + 200)
		return ("blocks not adjacent");
	FreeMem(b, 200);
	FreeMem(a, 140);
	FreeMem(c, 300);
	if (AvailMem(0) != avail || AvailMem(MEMF_LARGEST) != largest)
		return ("coalescing");

	/* AllocVec keeps its own size. */
	a = (UBYTE *)AllocVec(1000, MEMF_PUBLIC | MEMF_CLEAR);
	if (a == NULL || (POFF(a) & 7) != 0 || !zero(a, 1000L))
		return ("AllocVec");
	if (AvailMem(0) != avail - 1008)
		return ("AllocVec size");
	a[999] = 1;
	FreeVec(a);
	FreeVec(NULL);
	if (AllocVec(0, 0) != NULL)
		return ("AllocVec 0");
	if (AvailMem(0) != avail || AvailMem(MEMF_TOTAL) != total)
		return ("AllocVec leaked");
	return (NULL);
}

static char *
flags()
{
	static ULONG bad[] = {
		MEMF_BANK0 | MEMF_UPPER, MEMF_CHIP, 0x00200000L,
		MEMF_BANK0 | MEMF_LINEAR
	};
	static ULONG good[] = {
		MEMF_FAST, MEMF_LOCAL, MEMF_DMA24, MEMF_KICK, MEMF_REVERSE,
		MEMF_LINEAR, MEMF_LARGEST, MEMF_TOTAL, RAM | MEMF_UPPER
	};
	ULONG avail;
	APTR p;
	int i;

	avail = AvailMem(0);
	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		if (AllocMem(16, bad[i]) != NULL || AllocVec(16, bad[i]) != NULL)
			return ("invalid flags accepted");
	for (i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
		if ((p = AllocMem(16, good[i])) == NULL)
			return ("valid flags rejected");
		FreeMem(p, 16);
	}
	if (AllocMem(0, 0) != NULL)
		return ("AllocMem 0");
	if (AllocMem(AvailMem(MEMF_LARGEST) + 8, 0) != NULL)
		return ("AllocMem beyond LARGEST");
	if ((p = AllocMem(16, MEMF_PUBLIC | MEMF_NO_EXPUNGE)) == NULL)
		return ("NO_EXPUNGE rejected");
	FreeMem(p, 16);
	FreeMem(NULL, 0);
	if (AvailMem(0) != avail)
		return ("failed calls changed the heap");
	return (NULL);
}

static char *
stack()
{
	UBYTE *s, *u;
	ULONG stk, up;
	int local;

	stk = AvailMem(MEMF_STACK);
	up = AvailMem(MEMF_UPPER);
	if (stk == 0)
		return ("no stack pool");
	s = (UBYTE *)AllocMem(512, MEMF_STACK | MEMF_CLEAR);
	u = (UBYTE *)AllocMem(512, MEMF_PUBLIC);
	if (s == NULL || u == NULL)
		return ("AllocMem stack/upper");
	if (PSEG(s) != SEGBASE(STACKSEG) || POFF(s) < STKPOOLLOW ||
	    POFF(s) + 512 > STKPOOLTOP)
		return ("MEMF_STACK block outside the stack pool");
	if (PSEG(u) == SEGBASE(STACKSEG))
		return ("upper block in the stack segment");
	if (AvailMem(MEMF_STACK) != stk - 512 || AvailMem(MEMF_UPPER) != up - 512)
		return ("pools not separate");
	if (TypeOfMem(s) != (RAM | MEMF_BANK0) ||
	    TypeOfMem(u) != (RAM | MEMF_UPPER))
		return ("TypeOfMem");
	if (TypeOfMem(&local) != 0 || TypeOfMem(SysBase) != 0)
		return ("TypeOfMem outside the heap");
	FreeMem(s, 512);
	FreeMem(u, 512);
	if (AvailMem(MEMF_STACK) != stk || AvailMem(MEMF_UPPER) != up)
		return ("stack pool leaked");
	return (NULL);
}

/* Fill the first region with BLK blocks until one lands in another. */
static char *
exhaust()
{
	ULONG avail, seg;
	int i, n;

	if (regions() < 2) {
		xKPrintf("memory: one upper region; exhaustion skipped\n");
		return (NULL);
	}
	avail = AvailMem(0);
	seg = 0;
	n = 0;
	for (i = 0; i < NBLK; i++) {
		if ((blk[i] = AllocMem(BLK, 0)) == NULL)
			return ("AllocMem BLK");
		n = i + 1;
		if (i == 0)
			seg = PSEG(blk[0]);
		else if (PSEG(blk[i]) != seg)
			break;
	}
	if (i == NBLK)
		return ("allocation never left the first region");
	if (POFF(blk[i - 1]) + BLK > 0x10000L)
		return ("block crosses a segment");
	for (i = 0; i < n; i++)
		FreeMem(blk[i], BLK);
	if (AvailMem(0) != avail)
		return ("exhaustion leaked");
	return (NULL);
}

static char *
big()
{
	UBYTE *p;
	ULONG avail;

	avail = AvailMem(0);
	if (AvailMem(MEMF_LARGEST) < 65000L) {
		xKPrintf("memory: largest %lu; 65000-byte block skipped\n",
		    AvailMem(MEMF_LARGEST));
		return (NULL);
	}
	if ((p = (UBYTE *)AllocMem(65000L, MEMF_CLEAR)) == NULL)
		return ("AllocMem 65000");
	if (PSEG(p + 64999) != PSEG(p) || POFF(p) + 65000L > 0x10000L)
		return ("65000-byte block crosses a segment");
	if (p[0] != 0 || p[32768] != 0 || p[64999] != 0)
		return ("65000-byte block not clear");
	p[64999] = 0x5a;
	FreeMem(p, 65000L);
	if (AvailMem(0) != avail)
		return ("65000-byte block leaked");
	return (NULL);
}

/* AllocEntry: every block or none; a failure returns the failing entry's
 * requirements with bit 31 set. */
static char *
entries()
{
	struct {
		struct Node n;
		UWORD num;
		struct MemEntry me[3];
	} req;
	register struct MemList *ml;
	ULONG avail, stk;

	avail = AvailMem(0);
	stk = AvailMem(MEMF_STACK);
	req.num = 3;
	req.me[0].me_Reqs = MEMF_PUBLIC | MEMF_CLEAR;
	req.me[0].me_Length = 200;
	req.me[1].me_Reqs = MEMF_STACK;
	req.me[1].me_Length = 512;
	req.me[2].me_Reqs = 0;
	req.me[2].me_Length = 0;
	ml = AllocEntry(&req);
	if ((ULONG)ml & 0x80000000L)
		return ("AllocEntry");
	if (ml->ml_NumEntries != 3 || ml->ml_ME[0].me_Length != 200 ||
	    ml->ml_ME[1].me_Length != 512 || ml->ml_ME[2].me_Addr != NULL ||
	    !zero(ml->ml_ME[0].me_Addr, 200L) ||
	    (TypeOfMem(ml->ml_ME[1].me_Addr) & MEMF_STACK) == 0 ||
	    AvailMem(MEMF_STACK) > stk - 512 ||
	    AvailMem(MEMF_STACK) < stk - 512 - 64)	/* the list is small */
		return ("AllocEntry list");
	FreeEntry(ml);
	if (AvailMem(0) != avail || AvailMem(MEMF_STACK) != stk)
		return ("FreeEntry");
	req.me[2].me_Reqs = MEMF_STACK;
	req.me[2].me_Length = 0xf000L;
	ml = AllocEntry(&req);
	if ((ULONG)ml != (0x80000000L | MEMF_STACK))
		return ("AllocEntry failure result");
	if (AvailMem(0) != avail || AvailMem(MEMF_STACK) != stk)
		return ("failed AllocEntry kept memory");
	return (NULL);
}

/* AddMemHandler: a failed allocation calls each handler; MEM_TRY_AGAIN
 * calls it again with MEMHF_RECYCLE, MEM_DID_NOTHING moves on. */
static int mhcalls;
static ULONG mhflags[3];
static ULONG mhsize;

static LONG
mhfirst(mhd, data, sysbase)
struct MemHandlerData *mhd;
APTR data, sysbase;
{
	if (mhcalls < 3)
		mhflags[mhcalls] = mhd->memh_Flags;
	mhcalls++;
	mhsize = mhd->memh_RequestSize;
	return (mhd->memh_Flags & MEMHF_RECYCLE ? MEM_DID_NOTHING :
	    MEM_TRY_AGAIN);
}

static LONG
mhsecond(mhd, data, sysbase)
struct MemHandlerData *mhd;
APTR data, sysbase;
{
	if (mhcalls < 3)
		mhflags[mhcalls] = 0x100 | (ULONG)data;
	mhcalls++;
	return (MEM_ALL_DONE);
}

static char *
handlers()
{
	struct Interrupt a, b;
	ULONG n;
	char *why;

	a.is_Node.ln_Pri = 10;
	a.is_Code = (int (*)())mhfirst;
	b.is_Node.ln_Pri = 0;
	b.is_Code = (int (*)())mhsecond;
	b.is_Data = (APTR)7L;
	AddMemHandler(&b);
	AddMemHandler(&a);
	n = AvailMem(MEMF_LARGEST) + 8;
	why = NULL;
	if (AllocMem(n, MEMF_NO_EXPUNGE) != NULL || mhcalls != 0)
		why = "MEMF_NO_EXPUNGE called a handler";
	else if (AllocMem(n, 0L) != NULL || mhcalls != 3 || mhsize != n ||
	    mhflags[0] != 0 || mhflags[1] != MEMHF_RECYCLE ||
	    mhflags[2] != 0x107)
		why = "handler order or flags";
	RemMemHandler(&a);
	RemMemHandler(&b);
	mhcalls = 0;
	if (why == NULL && (AllocMem(n, 0L) != NULL || mhcalls != 0))
		why = "RemMemHandler";
	return (why);
}

int
main()
{
	char *why;
	ULONG avail;

	avail = AvailMem(0);
	if ((why = example()) == NULL && (why = basics()) == NULL &&
	    (why = stack()) == NULL && (why = exhaust()) == NULL &&
	    (why = big()) == NULL && (why = flags()) == NULL &&
	    (why = entries()) == NULL && (why = handlers()) == NULL) {
		if (AvailMem(0) != avail)
			why = "heap not restored";
	}
	if (why != NULL) {
		xKPrintf("FAIL memory: %s\n", why);
		return (1);
	}
	KPutStr("PASS memory\n");
	return (0);
}
