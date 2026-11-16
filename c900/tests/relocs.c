/* SPDX-License-Identifier: MIT */
/*
 * A hunk file with thousands of RELOC32 entries: two code hunks, a data
 * hunk and a bss hunk, where every third long of each loaded hunk is
 * relocated against one of the four, some with bit 31 set.  The file is
 * built in memory, loaded through InternalLoadSeg from memory and,
 * written to RAM:, through LoadSeg; every long of every hunk is checked.
 * The file cut short inside its relocation tables must fail to load.
 * The loads may lose no memory.  Marks 1-2 and 3-4 (port 0x0F00 + 2N, for
 * simrun -marks) time the two loads.
 */
#define TNAME	"relocs"
#include "dostest.h"
#include <dos/doshunks.h>

void outb();

#define NH	4
#define BSSH	2
#define ALLOCVEC	((APTR)((char *)SysBase + LVOAllocMem))
#define FREEVEC		((APTR)((char *)SysBase + LVOFreeMem))

/* Hunk sizes in longs; hunk BSSH is bss. */
static UWORD hlongs[NH] = { 3072, 2048, 1024, 1536 };

struct MemFile {
	UBYTE *mf_Ptr;
	ULONG mf_Left;
};

static UBYTE *file;
static ULONG fsize;
static ULONG nrel;

void
perfmark(n)
int n;
{
	outb(0x0F00 + 2 * n, 0);
}

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

static APTR memfuncs[3] = { (APTR)mread };

/* Long i of hunk h: relocated against hunk *t at offset *off, or not. */
static int
isrel(h, i, t, off)
int h;
UWORD i;
int *t;
UWORD *off;
{
	if (h == BSSH || i % 3 != 0)
		return (0);
	*t = (i / 3 * 5 + h) % NH;
	*off = (UWORD)(((ULONG)i * 52 + h * 4) % ((ULONG)hlongs[*t] << 2));
	return (1);
}

/* The long a file holds at long i of hunk h. */
static ULONG
fval(h, i)
int h;
UWORD i;
{
	int t;
	UWORD off;

	if (isrel(h, i, &t, &off))
		return ((i & 8 ? 0x80000000L : 0L) | off);
	return (0x5A000000L ^ ((ULONG)i * 0x10003L) ^ (ULONG)h << 20);
}

/* Build the file in memory. */
static int
build()
{
	register ULONG *q;
	register UWORD i;
	ULONG *cnt;
	int h, t, g;
	UWORD off;

	fsize = 4 * (5L + NH) + 4 * 2L * NH;
	for (h = 0; h < NH; h++)
		if (h != BSSH)
			fsize += ((ULONG)hlongs[h] << 2) * 2 + 4 * (2L * NH + 2);
	if ((file = (UBYTE *)AllocMem(fsize, MEMF_PUBLIC)) == NULL)
		return (-1);
	q = (ULONG *)file;
	*q++ = HUNK_HEADER;
	*q++ = 0;
	*q++ = NH;
	*q++ = 0;
	*q++ = NH - 1;
	for (h = 0; h < NH; h++)
		*q++ = hlongs[h];
	nrel = 0;
	for (h = 0; h < NH; h++) {
		if (h == BSSH) {
			*q++ = HUNK_BSS;
			*q++ = hlongs[h];
			*q++ = HUNK_END;
			continue;
		}
		*q++ = h == 0 || h == 3 ? HUNK_CODE : HUNK_DATA;
		*q++ = hlongs[h];
		for (i = 0; i < hlongs[h]; i++)
			*q++ = fval(h, i);
		*q++ = HUNK_RELOC32;
		for (g = 0; g < NH; g++) {
			cnt = q;
			*q++ = 0;
			*q++ = g;
			for (i = 0; i < hlongs[h]; i++)
				if (isrel(h, i, &t, &off) && t == g) {
					*q++ = (ULONG)i << 2;
					(*cnt)++;
				}
			nrel += *cnt;
		}
		*q++ = 0;
		*q++ = HUNK_END;
	}
	fsize = (UWORD)((UBYTE *)q - file);
	return (0);
}

/* Check every long of every hunk of seg. */
static int
verify(seg)
APTR seg;
{
	register ULONG *p, *hp;
	char *base[NH];
	ULONG v;
	UWORD i;
	int h, t;
	UWORD off;

	h = 0;
	for (p = (ULONG *)seg; p != NULL; p = (ULONG *)*p) {
		if (h == NH || p[-1] - 8 != (ULONG)hlongs[h] << 2)
			return (-1);
		base[h++] = (char *)(p + 1);
	}
	if (h != NH)
		return (-1);
	for (h = 0; h < NH; h++) {
		hp = (ULONG *)base[h];
		for (i = 0; i < hlongs[h]; i++, hp++) {
			if (h == BSSH)
				v = 0;
			else if (isrel(h, i, &t, &off))
				v = (fval(h, i) & 0x80000000L) |
				    (ULONG)(base[t] + off);
			else
				v = fval(h, i);
			if (*hp != v)
				return (-1);
		}
	}
	return (0);
}

static int
dtest()
{
	struct MemFile mf;
	ULONG before, cut, rt;
	APTR seg;
	BPTR fh, bseg;

	memfuncs[1] = ALLOCVEC;
	memfuncs[2] = FREEVEC;
	CHECK(build() == 0, "no memory for the file");
	CHECK(nrel > 2000, "too few relocations");
	xKPrintf("relocs: %ld bytes, %ld relocations\n", fsize, nrel);
	CHECK((fh = Open("RAM:relocs", MODE_NEWFILE)) != NULL, "Open");
	CHECK(Write(fh, file, fsize) == fsize, "Write");
	Close(fh);
	before = AvailMem(MEMF_ANY);

	mf.mf_Ptr = file;
	mf.mf_Left = fsize;
	perfmark(1);
	seg = InternalLoadSeg(&mf, NULL, memfuncs, NULL);
	perfmark(2);
	CHECK(seg != NULL, "InternalLoadSeg from memory");
	CHECK(verify(seg) == 0, "a long after InternalLoadSeg");
	InternalUnLoadSeg(seg, FREEVEC);

	/* Cuts inside hunk 0's relocation table and in the last hunk's. */
	rt = 4 * (5L + NH) + 8 + ((ULONG)hlongs[0] << 2) + 4;
	for (cut = rt + 4; cut < fsize; cut += (fsize - rt) / 7 | 2) {
		mf.mf_Ptr = file;
		mf.mf_Left = cut;
		CHECK(InternalLoadSeg(&mf, NULL, memfuncs, NULL) == NULL,
		    "a cut file loaded");
	}

	perfmark(3);
	bseg = LoadSeg("RAM:relocs");
	perfmark(4);
	CHECK(bseg != NULL, "LoadSeg");
	CHECK(verify((APTR)bseg) == 0, "a long after LoadSeg");
	UnLoadSeg(bseg);
	CHECK(AvailMem(MEMF_ANY) == before, "memory lost");
	CHECK(DeleteFile("RAM:relocs"), "DeleteFile");
	FreeMem(file, fsize);
	return (0);
}
