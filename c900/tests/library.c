/* SPDX-License-Identifier: MIT */
/*
 * Libraries through their jump tables: exec.library itself, a test
 * library built by MakeLibrary with InitStruct data and a libInit,
 * OpenLibrary versions, the LIBF_DELEXP expunge at last close,
 * SetFunction, SumLibrary catching a changed vector, MakeFunctions with
 * word offsets, the unimplemented-vector alert, InitResident with and
 * without RTF_AUTOINIT, FindResident and the resource calls.
 */
#include "test.h"
#include <exec/exec.h>
#include <exec/initializers.h>
#include <resources/battclock.h>

struct TestLib {
	struct Library tl_Lib;
	UWORD tl_Calls;
	ULONG tl_Magic;
	APTR tl_Seg;
};

#define TL_MAGIC	36	/* offsetof(struct TestLib, tl_Magic) */
#define POFF(p)		((ULONG)(p) & 0xFFFFL)
#define LVOTestAdd	(-30)
#define LVOTestSub	(-36)

static char libname[] = "test.library";
static struct TestLib *TestBase;
static int expunged;
static int badinit;

#define TestAdd(a, b) (*(LONG (*)())((char *)TestBase + LVOTestAdd)) \
	(TestBase, (LONG)(a), (LONG)(b))
#define TestSub(a, b) (*(LONG (*)())((char *)TestBase + LVOTestSub)) \
	(TestBase, (LONG)(a), (LONG)(b))

static struct Library *
lopen(lib, ver)
struct Library *lib;
ULONG ver;
{
	lib->lib_OpenCnt++;
	lib->lib_Flags &= ~LIBF_DELEXP;
	return (lib);
}

static APTR
lexpunge(lib)
struct Library *lib;
{
	APTR seg;

	if (lib->lib_OpenCnt != 0) {
		lib->lib_Flags |= LIBF_DELEXP;
		return (NULL);
	}
	seg = ((struct TestLib *)lib)->tl_Seg;
	Remove(lib);
	FreeMem((char *)lib - lib->lib_NegSize,
	    lib->lib_NegSize + lib->lib_PosSize);
	expunged++;
	return (seg);
}

static APTR
lclose(lib)
struct Library *lib;
{
	if (--lib->lib_OpenCnt == 0 && (lib->lib_Flags & LIBF_DELEXP))
		return (lexpunge(lib));
	return (NULL);
}

static APTR
lnull()
{
	return (NULL);
}

static LONG
ladd(lib, a, b)
struct TestLib *lib;
LONG a, b;
{
	lib->tl_Calls++;
	return (a + b);
}

static LONG
lsub(lib, a, b)
struct TestLib *lib;
LONG a, b;
{
	lib->tl_Calls++;
	return (a - b);
}

static LONG
lmul(lib, a, b)
struct TestLib *lib;
LONG a, b;
{
	lib->tl_Calls++;
	return (a * b);
}

static struct Library *
linit(lib, seg, sysbase)
struct Library *lib;
APTR seg;
struct ExecBase *sysbase;
{
	if (sysbase != SysBase)
		badinit++;
	lib->lib_Node.ln_Name = libname;
	((struct TestLib *)lib)->tl_Seg = seg;
	return (lib);
}

static APTR funcs[] = {
	(APTR)lopen, (APTR)lclose, (APTR)lexpunge, (APTR)lnull,
	(APTR)ladd, (APTR)lsub, (APTR)-1L
};

static UWORD inittab[] = {
	INITBYTE(8, NT_LIBRARY),		/* lib_Node.ln_Type */
	INITBYTE(14, LIBF_SUMUSED),		/* lib_Flags */
	INITWORD(20, 3),			/* lib_Version */
	INITWORD(22, 7),			/* lib_Revision */
	INITLONG(TL_MAGIC, 0x12345678L),
	0
};

static APTR autoinit[] = {
	(APTR)sizeof(struct TestLib), (APTR)funcs, (APTR)inittab,
	(APTR)linit
};

static struct Resident autotag = {
	RTC_MATCHWORD, &autotag, (APTR)(&autotag + 1), RTF_AUTOINIT, 3,
	NT_LIBRARY, 0, libname, NULL, (APTR)autoinit
};

static APTR
plaininit(zero, seg, sysbase)
ULONG zero;
APTR seg;
struct ExecBase *sysbase;
{
	if (zero != 0 || sysbase != SysBase)
		return (NULL);
	return (seg);
}

static struct Resident plaintag = {
	RTC_MATCHWORD, &plaintag, (APTR)(&plaintag + 1), RTF_COLDSTART, 1,
	NT_UNKNOWN, 0, libname, NULL, (APTR)plaininit
};

static char *
execlib()
{
	struct Library *lib;
	UWORD *v;

	if (*(struct ExecBase **)0x31000000L != SysBase)
		return ("SysBase is not the first data long");
	lib = &SysBase->LibNode;
	v = (UWORD *)((char *)lib - 6);
	if (v[0] != 0x5E08 || (v[1] & 0x8000) == 0)
		return ("exec vector -6 is not a long-form jp");
	if (lib->lib_NegSize != SYSBASE_NEGSIZE ||
	    lib->lib_Node.ln_Type != NT_LIBRARY || lib->lib_Version != 39)
		return ("exec.library fields");
	if (OpenLibrary("exec.library", 40) != NULL)
		return ("exec.library opened above its version");
	if (OpenLibrary("exec.library", 39) != lib || lib->lib_OpenCnt != 1)
		return ("OpenLibrary exec.library");
	CloseLibrary(lib);
	if (lib->lib_OpenCnt != 0)
		return ("CloseLibrary exec.library");
	if (OldOpenLibrary("exec.library") != lib)
		return ("OldOpenLibrary");
	CloseLibrary(lib);
	CloseLibrary(NULL);

	/* An unimplemented vector alerts with its LVO and returns 0. */
	SysBase->LastAlert[0] = 0;
	if ((*(ULONG (*)())((char *)SysBase + LVODebug))(0L) != 0 ||
	    SysBase->LastAlert[0] != (AN_BadLVO | 114))
		return ("unimplemented vector");
	return (NULL);
}

static char *
testlib()
{
	struct TestLib *tl;
	struct Library *lib;
	APTR old;
	UWORD *v;

	tl = (struct TestLib *)MakeLibrary(funcs, inittab, linit,
	    sizeof(struct TestLib), 0x1234L);
	if (tl == NULL || badinit)
		return ("MakeLibrary");
	lib = &tl->tl_Lib;
	if (lib->lib_NegSize != 36 || lib->lib_PosSize != sizeof(*tl) ||
	    lib->lib_Node.ln_Type != NT_LIBRARY || lib->lib_Version != 3 ||
	    lib->lib_Revision != 7 || tl->tl_Magic != 0x12345678L ||
	    lib->lib_Flags != LIBF_SUMUSED || tl->tl_Seg != (APTR)0x1234L)
		return ("MakeLibrary or InitStruct fields");
	AddLibrary(lib);
	if (OpenLibrary(libname, 4) != NULL)
		return ("OpenLibrary above the version");
	TestBase = (struct TestLib *)OpenLibrary(libname, 3);
	if (TestBase != tl ||
	    OpenLibrary(libname, 0) != lib || lib->lib_OpenCnt != 2)
		return ("OpenLibrary");
	if (TestAdd(40, 2) != 42 || TestSub(40, 2) != 38 || tl->tl_Calls != 2)
		return ("calls through the vectors");

	/* SetFunction: the old function back, the new one called. */
	SysBase->LastAlert[0] = 0;
	old = SetFunction(lib, LVOTestAdd, lmul);
	if (old != (APTR)ladd || TestAdd(6, 7) != 42)
		return ("SetFunction");
	if (SetFunction(lib, LVOTestAdd, old) != (APTR)lmul ||
	    TestAdd(6, 7) != 13)
		return ("SetFunction back");
	SumLibrary(lib);
	if (SysBase->LastAlert[0] != 0 || (lib->lib_Flags & LIBF_CHANGED))
		return ("SumLibrary after SetFunction");

	/* A vector changed behind Exec's back. */
	v = (UWORD *)((char *)lib + LVOTestSub);
	v[2] ^= 0x0100;
	SumLibrary(lib);
	if (SysBase->LastAlert[0] != AN_LibChkSum)
		return ("SumLibrary missed a changed vector");
	v[2] ^= 0x0100;
	SysBase->LastAlert[0] = 0;
	SumLibrary(lib);
	if (SysBase->LastAlert[0] != 0 || TestSub(5, 3) != 2)
		return ("SumLibrary after repair");

	/* RemLibrary while open only flags it; the last close expunges. */
	if (RemLibrary(lib) != NULL || expunged != 0 ||
	    (lib->lib_Flags & LIBF_DELEXP) == 0)
		return ("RemLibrary while open");
	CloseLibrary(lib);
	if (expunged != 0 || lib->lib_OpenCnt != 1)
		return ("expunged with an open left");
	CloseLibrary(lib);
	if (expunged != 1 || OpenLibrary(libname, 0) != NULL)
		return ("LIBF_DELEXP expunge at last close");
	return (NULL);
}

/* MakeFunctions with word offsets from a base, into a table on the
 * stack: lsub at -6, lmul at -12. */
static char *
relative()
{
	struct {
		UWORD vec[9];
		struct TestLib lib;
	} t;
	WORD offs[3];

	offs[0] = (WORD)(POFF(lsub) - POFF(ladd));
	offs[1] = (WORD)(POFF(lmul) - POFF(ladd));
	offs[2] = -1;
	if (MakeFunctions(&t.lib, offs, (APTR)ladd) != 12)
		return ("MakeFunctions size");
	TestBase = &t.lib;
	t.lib.tl_Calls = 0;
	if ((*(LONG (*)())((char *)TestBase - 6))(TestBase, 9L, 4L) != 5 ||
	    (*(LONG (*)())((char *)TestBase - 12))(TestBase, 9L, 4L) != 36 ||
	    t.lib.tl_Calls != 2)
		return ("MakeFunctions with offsets");
	return (NULL);
}

static char *
residents()
{
	struct Resident *rt;
	struct Library *lib;
	struct Node res;

	if ((rt = FindResident("timer.device")) == NULL ||
	    rt->rt_Type != NT_DEVICE || rt->rt_MatchTag != rt ||
	    FindResident("nosuch.library") != NULL)
		return ("FindResident");
	if (InitResident(&plaintag, 0x99L) != (APTR)0x99L)
		return ("InitResident without RTF_AUTOINIT");
	expunged = 0;
	lib = (struct Library *)InitResident(&autotag, 0x55L);
	if (lib == NULL || lib->lib_Version != 3 ||
	    OpenLibrary(libname, 3) != lib)
		return ("InitResident with RTF_AUTOINIT");
	CloseLibrary(lib);
	if (RemLibrary(lib) != (APTR)0x55L || expunged != 1)
		return ("RemLibrary of a closed library");

	if (OpenResource(BATTCLOCKNAME) == NULL)
		return ("OpenResource battclock.resource");
	res.ln_Name = "test.resource";
	res.ln_Pri = 0;
	AddResource(&res);
	if (OpenResource("test.resource") != (APTR)&res)
		return ("OpenResource");
	RemResource(&res);
	if (OpenResource("test.resource") != NULL)
		return ("RemResource");
	return (NULL);
}

int
main()
{
	char *why;
	ULONG before;

	before = AvailMem(MEMF_ANY);
	if ((why = execlib()) == NULL && (why = testlib()) == NULL &&
	    (why = relative()) == NULL)
		why = residents();
	if (why == NULL && AvailMem(MEMF_ANY) != before)
		why = "memory leaked";
	if (why != NULL) {
		xKPrintf("FAIL library: %s\n", why);
		return (1);
	}
	KPutStr("PASS library\n");
	return (0);
}
