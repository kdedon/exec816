/* SPDX-License-Identifier: MIT */
/*
 * InternalLoadSeg and InternalUnLoadSeg: tests/hunks/seghunk from the
 * boot volume through the kernel's reader, its seglist, relocations
 * between its code, data and bss hunks, its entry, and the unload; then
 * the same file from memory cut short at every long, which must fail
 * and free everything.  No memory may be lost.
 */
#include "test.h"

struct SegInfo {
	APTR si_Code;
	APTR si_Data;
	APTR si_Bss;
	APTR si_BssM1;
	char *si_Str;
	LONG si_Sum;
	ULONG si_Zero;
};

struct MemFile {
	UBYTE *mf_Ptr;
	ULONG mf_Left;
};

static LONG
mread(mf, buf, len)
register struct MemFile *mf;
APTR buf;
ULONG len;
{
	if (len > mf->mf_Left)
		len = mf->mf_Left;
	CopyMem(mf->mf_Ptr, buf, len);
	mf->mf_Ptr += (UWORD)len;
	mf->mf_Left -= len;
	return ((LONG)len);
}

/* exec.library's AllocMem and FreeMem vectors take the loader's alloc
 * and free arguments as they are. */
#define ALLOCVEC	((APTR)((char *)SysBase + LVOAllocMem))
#define FREEVEC		((APTR)((char *)SysBase + LVOFreeMem))

static APTR memfuncs[3] = { (APTR)mread };

/* Hunk n of seg (its data), and its size in *size. */
static char *
hunk(seg, n, size)
APTR seg;
int n;
ULONG *size;
{
	register ULONG *p;

	for (p = (ULONG *)seg; p != NULL && n > 0; n--)
		p = (ULONG *)*p;
	if (p == NULL)
		return (NULL);
	*size = p[-1] - 8;
	return ((char *)(p + 1));
}

static int
inside(a, h, size)
APTR a;
char *h;
ULONG size;
{
	return (PSEG(a) == PSEG(h) && POFF(a) >= POFF(h) &&
	    POFF(a) < POFF(h) + size);
}

static char *
run()
{
	struct BootFile *bf;
	struct MemFile mf;
	struct SegInfo *si;
	struct SegInfo *(*entry)();
	APTR seg;
	char *code, *data, *bss, *a, *b;
	UBYTE *file;
	ULONG before, csize, dsize, bsize, fsize, cut;

	memfuncs[1] = ALLOCVEC;
	memfuncs[2] = FREEVEC;
	before = AvailMem(MEMF_ANY);
	if ((bf = kbfinit()) == NULL)
		return ("no boot volume");
	if (kbfopen(bf, "seghunk") != 0) {
		kbfdone(bf);
		return ("no seghunk on the boot volume");
	}
	seg = InternalLoadSeg(bf, NULL, BootFuncs, NULL);
	fsize = bf->bf_Size;
	file = (UBYTE *)AllocMem(fsize, MEMF_PUBLIC);
	if (file != NULL && kbfopen(bf, "seghunk") == 0 &&
	    kbfread(bf, file, fsize) != (LONG)fsize)
		return ("reading seghunk");
	kbfdone(bf);
	if (seg == NULL || file == NULL)
		return ("InternalLoadSeg");
	code = hunk(seg, 0, &csize);
	data = hunk(seg, 1, &dsize);
	bss = hunk(seg, 2, &bsize);
	if (code == NULL || data == NULL || bss == NULL ||
	    hunk(seg, 3, &cut) != NULL)
		return ("seglist is not three hunks");
	if (csize < 128 || dsize < 84 || bsize != 16)
		return ("hunk sizes");
	if (PSEG(code) == SEGBASE(KTEXTSEG) || TypeOfMem(code) == 0)
		return ("code hunk is not heap memory");
	entry = (struct SegInfo *(*)())code;
	si = (*entry)(21L);
	if (!inside(si, data, dsize) || si->si_Code != (APTR)code ||
	    !inside(si->si_Data, data, dsize) || si->si_Str != si->si_Data ||
	    !inside(si->si_Bss, bss, bsize) ||
	    si->si_BssM1 != (APTR)((char *)si->si_Bss - 4))
		return ("relocated pointers");
	for (a = si->si_Str, b = "seghunk data"; *a == *b; a++, b++)
		if (*a == '\0')
			break;
	if (*a != *b)
		return ("data string");
	if (si->si_Zero != 0)
		return ("bss not cleared");
	if (si->si_Sum != 42 + 'h' || ((LONG *)si->si_Bss)[2] != 21)
		return ("entry results");
	InternalUnLoadSeg(seg, FREEVEC);

	for (cut = 0; cut < fsize; cut += 2) {
		mf.mf_Ptr = file;
		mf.mf_Left = cut;
		if ((seg = InternalLoadSeg(&mf, NULL, memfuncs, NULL)) != NULL)
			return ("a cut file loaded");
	}
	mf.mf_Ptr = file;
	mf.mf_Left = fsize;
	if ((seg = InternalLoadSeg(&mf, NULL, memfuncs, NULL)) == NULL)
		return ("loading from memory");
	InternalUnLoadSeg(seg, FREEVEC);
	FreeMem(file, fsize);
	if (AvailMem(MEMF_ANY) != before)
		return ("memory lost");
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL loadseg: %s\n", why);
		return (1);
	}
	KPutStr("PASS loadseg\n");
	return (0);
}
