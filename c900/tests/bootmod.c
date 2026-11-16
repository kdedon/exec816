/* SPDX-License-Identifier: MIT */
/*
 * Boot modules: tests/hunks/tmod, listed in the boot volume's Modules
 * file, was loaded and its romtag initialised before main().  Open
 * tmod.library, check that its vectors point into its loaded code hunk,
 * call its functions, and close it.
 */
#include "test.h"

struct TmodBase {
	struct Library tb_Lib;
	APTR tb_Seg;
	ULONG tb_Calls;
};

static struct TmodBase *TmodBase;

#define TmAdd(a, b)	(*(LONG (*)())((char *)TmodBase - 30)) \
	(TmodBase, (LONG)(a), (LONG)(b))
#define TmCount()	(*(ULONG (*)())((char *)TmodBase - 36))(TmodBase)
#define TmIdStr()	(*(char *(*)())((char *)TmodBase - 42))(TmodBase)

static char *
run()
{
	register struct JumpVec *v;
	register ULONG *s;
	register char *a, *b;
	int i;

	TmodBase = (struct TmodBase *)OpenLibrary("tmod.library", 1L);
	if (TmodBase == NULL)
		return ("OpenLibrary");
	if (TmodBase->tb_Lib.lib_OpenCnt != 1 ||
	    TmodBase->tb_Lib.lib_NegSize != 7 * LIB_VECTSIZE + 2)
		return ("library base");
	if ((s = (ULONG *)TmodBase->tb_Seg) == NULL)
		return ("no seglist in the base");
	for (i = 1; i <= 7; i++) {
		v = (struct JumpVec *)((char *)TmodBase - i * LIB_VECTSIZE);
		if (v->jv_Op != JP_OP || PSEG(v->jv_Fn) != PSEG(s) ||
		    POFF(v->jv_Fn) < POFF(s + 1) ||
		    POFF(v->jv_Fn) >= POFF(s - 1) + s[-1])
			return ("vector outside the code hunk");
	}
	if (PSEG(s) == SEGBASE(KTEXTSEG) || TypeOfMem(s) == 0)
		return ("code hunk is not heap memory");
	if (TmAdd(40, 2) != 42 || TmAdd(-5, 3) != -2)
		return ("TmAdd");
	if (TmCount() != 0x00020002L)
		return ("TmCount");
	a = TmIdStr();
	b = (char *)TmodBase->tb_Lib.lib_IdString;
	if (a != b)
		return ("id string");
	for (b = "tmod 1.0 (boot module test)"; *a == *b; a++, b++)
		if (*a == '\0')
			break;
	if (*a != *b)
		return ("id string text");
	CloseLibrary((struct Library *)TmodBase);
	if (TmodBase->tb_Lib.lib_OpenCnt != 0)
		return ("CloseLibrary");
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL bootmod: %s\n", why);
		return (1);
	}
	KPutStr("PASS bootmod\n");
	return (0);
}
