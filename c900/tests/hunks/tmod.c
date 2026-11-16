/* SPDX-License-Identifier: MIT */
/*
 * tmod.library: a boot module for tests/bootmod.c.  An RTF_AUTOINIT
 * romtag in the data hunk; the functions count calls in the base and in
 * a bss long, and return a string from the data hunk.
 */
#include <exec/exec.h>

struct TmodBase {
	struct Library tb_Lib;
	APTR tb_Seg;
	ULONG tb_Calls;
};

static char tmname[] = "tmod.library";
static char tmid[] = "tmod 1.0 (boot module test)";
static ULONG tmbss;

static struct Library *
tmopen(lib, ver)
struct Library *lib;
ULONG ver;
{
	lib->lib_OpenCnt++;
	return (lib);
}

static APTR
tmclose(lib)
struct Library *lib;
{
	lib->lib_OpenCnt--;
	return (NULL);
}

static APTR
tmnull()
{
	return (NULL);
}

static LONG
tmadd(tb, a, b)
struct TmodBase *tb;
LONG a, b;
{
	tb->tb_Calls++;
	tmbss++;
	return (a + b);
}

static ULONG
tmcount(tb)
struct TmodBase *tb;
{
	return (tmbss + (tb->tb_Calls << 16));
}

static char *
tmidstr(tb)
struct TmodBase *tb;
{
	return (tmid);
}

static struct Library *
tminit(lib, seg, sysbase)
struct Library *lib;
APTR seg;
struct ExecBase *sysbase;
{
	if (sysbase != SysBase)
		return (NULL);
	lib->lib_Node.ln_Name = tmname;
	lib->lib_IdString = (APTR)tmid;
	lib->lib_Version = 1;
	((struct TmodBase *)lib)->tb_Seg = seg;
	KPutStr("tmod: init\n");
	return (lib);
}

static APTR tmfuncs[] = {
	(APTR)tmopen, (APTR)tmclose, (APTR)tmnull, (APTR)tmnull,
	(APTR)tmadd, (APTR)tmcount, (APTR)tmidstr, (APTR)-1L
};

static struct {
	ULONG it_Size;
	APTR it_Funcs, it_Struct, it_Init;
} tminittab = {
	sizeof(struct TmodBase), (APTR)tmfuncs, NULL, (APTR)tminit
};

struct Resident tmtag = {
	RTC_MATCHWORD, &tmtag, (APTR)(&tmtag + 1), RTF_AUTOINIT, 1,
	NT_LIBRARY, 0, tmname, tmid, (APTR)&tminittab
};
