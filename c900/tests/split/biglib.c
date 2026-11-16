/* SPDX-License-Identifier: MIT */
/*
 * big.library for tests/bighunk.c: an RTF_AUTOINIT romtag in hunk 0's
 * group whose vectors point into every code hunk of the generated parts
 * (genbig.py).  Expunge returns the seglist for the caller to unload.
 */
#include <exec/exec.h>

typedef unsigned long UL;

struct BigBase {
	struct Library bb_Lib;
	APTR bb_Seg;
};

extern UL bchain0(), bent0(), bent1(), bent2();

static char bbname[] = "big.library";
static char bbid[] = "big 1.0 (multi-hunk test)";

static struct Library *
bbopen(lib, ver)
struct Library *lib;
ULONG ver;
{
	lib->lib_OpenCnt++;
	return (lib);
}

static APTR
bbexpunge(lib)
struct Library *lib;
{
	APTR seg;

	if (lib->lib_OpenCnt != 0) {
		lib->lib_Flags |= LIBF_DELEXP;
		return (NULL);
	}
	seg = ((struct BigBase *)lib)->bb_Seg;
	Remove(lib);
	FreeMem((char *)lib - lib->lib_NegSize,
	    (ULONG)(lib->lib_NegSize + lib->lib_PosSize));
	return (seg);
}

static APTR
bbclose(lib)
struct Library *lib;
{
	if (--lib->lib_OpenCnt == 0 && (lib->lib_Flags & LIBF_DELEXP))
		return (bbexpunge(lib));
	return (NULL);
}

static APTR
bbnull()
{
	return (NULL);
}

static UL
bbchain(bb, v)
struct BigBase *bb;
UL v;
{
	return (bchain0(v));
}

static struct Library *
bbinit(lib, seg, sysbase)
struct Library *lib;
APTR seg;
struct ExecBase *sysbase;
{
	lib->lib_Node.ln_Name = bbname;
	lib->lib_IdString = (APTR)bbid;
	lib->lib_Version = 1;
	((struct BigBase *)lib)->bb_Seg = seg;
	return (lib);
}

static APTR bbfuncs[] = {
	(APTR)bbopen, (APTR)bbclose, (APTR)bbexpunge, (APTR)bbnull,
	(APTR)bbchain, (APTR)bent0, (APTR)bent1, (APTR)bent2, (APTR)-1L
};

static struct {
	ULONG it_Size;
	APTR it_Funcs, it_Struct, it_Init;
} bbinittab = {
	sizeof(struct BigBase), (APTR)bbfuncs, NULL, (APTR)bbinit
};

struct Resident bbtag = {
	RTC_MATCHWORD, &bbtag, (APTR)(&bbtag + 1), RTF_AUTOINIT, 1,
	NT_LIBRARY, 0, bbname, bbid, (APTR)&bbinittab
};
