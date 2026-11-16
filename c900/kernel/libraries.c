/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Libraries, resources and resident modules, after Amiga Exec V39:
 * MakeFunctions, MakeLibrary, InitStruct, the library list calls,
 * SetFunction, SumLibrary, InitResident, FindResident, InitCode and
 * the resource list.  A vector is "jp addr" in the long segmented form
 * (docs/platform.md).  List changes and library entry calls run under
 * Forbid, as on the Amiga.
 */
#include "kernel.h"

static char execname[] = "exec.library";

/* The base execvec.s reserves must hold a struct ExecBase. */
typedef char kbasesize[sizeof(struct ExecBase) == EXECBASESIZE ? 1 : -1];
static char execid[] = "exec 39.0 (Exec900)";

/* Store "jp f" in v. */
static void
setvec(v, f)
register struct JumpVec *v;
APTR f;
{
	v->jv_Op = JP_OP;
	v->jv_Fn = (APTR)((ULONG)f | LONGSEG);
}

/* Vectors below target from an array of pointers ending in -1, or, with
 * disp, of WORD offsets from disp ending in -1.  Returns their bytes. */
ULONG
xMakeFunctions(target, fa, disp)
APTR target, fa;
char *disp;
{
	register struct JumpVec *v;
	register WORD *w;
	register APTR *p;
	APTR f;

	v = (struct JumpVec *)target;
	w = (WORD *)fa;
	p = (APTR *)fa;
	for (;;) {
		if (disp != NULL) {
			if (*w == -1)
				break;
			f = (APTR)(disp + *w++);
		} else if ((f = *p++) == (APTR)-1L)
			break;
		setvec(--v, f);
	}
	return (POFF(target) - POFF(v));
}

/* Amiga's byte-coded initializer: ddssnnnn, dd 00 copy, 01 repeat,
 * 10 copy at a byte offset, 11 copy at a 24-bit offset; ss the item size
 * (long, word, byte); nnnn the count less one.  Word and long items and
 * each command start word-aligned; a zero command ends the table. */
void
xInitStruct(table, mem, size)
APTR table, mem;
ULONG size;
{
	register UBYTE *s, *d;
	register int c, n, i, w;

	d = (UBYTE *)mem;
	while (size-- != 0)
		*d++ = 0;
	s = (UBYTE *)table;
	d = (UBYTE *)mem;
	while ((c = *s++) != 0) {
		n = (c & 0x0F) + 1;
		w = 4 >> ((c >> 4) & 3);
		if ((c & 0xC0) == 0x80)
			d = (UBYTE *)mem + *s++;
		else if ((c & 0xC0) == 0xC0) {
			d = (UBYTE *)mem + ((UWORD)s[1] << 8 | s[2]);
			s += 3;
		}
		if (w > 1)
			s += POFF(s) & 1;
		while (n-- != 0) {
			for (i = 0; i < w; i++)
				*d++ = s[i];
			if ((c & 0xC0) != 0x40)
				s += w;
		}
		if ((c & 0xC0) == 0x40)
			s += w;
		s += POFF(s) & 1;
	}
}

/* An allocated library: the vectors from funcs (a -1 first word selects
 * offsets from funcs, as MakeFunctions with disp), lib_NegSize rounded
 * to a long, init through InitStruct, then libinit(lib, seg, SysBase),
 * whose result is returned. */
struct Library *
xMakeLibrary(funcs, init, libinit, size, seg)
APTR funcs, init, libinit;
ULONG size;
APTR seg;
{
	register struct Library *lib;
	register UWORD neg;
	register WORD *w;
	char *disp;

	disp = NULL;
	neg = 0;
	if (*(WORD *)funcs == -1) {
		disp = (char *)funcs;
		funcs = (APTR)(disp + 2);
		for (w = (WORD *)funcs; *w != -1; w++)
			neg += LIB_VECTSIZE;
	} else
		for (w = (WORD *)funcs; *(APTR *)w != (APTR)-1L; w += 2)
			neg += LIB_VECTSIZE;
	neg = (neg + 3) & ~3;
	lib = (struct Library *)xAllocMem((ULONG)neg + size,
	    (ULONG)(MEMF_PUBLIC | MEMF_CLEAR));
	if (lib == NULL)
		return (NULL);
	lib = (struct Library *)((char *)lib + neg);
	xMakeFunctions(lib, funcs, disp);
	if (init != NULL)
		xInitStruct(init, lib, 0L);
	lib->lib_NegSize = neg;
	lib->lib_PosSize = (UWORD)size;
	if (libinit != NULL)
		lib = (*(struct Library *(*)())libinit)(lib, seg, SysBase);
	return (lib);
}

/* AddLibrary, AddDevice: sum the vectors and list lib by priority. */
void
kaddlib(list, lib, type)
struct List *list;
register struct Library *lib;
int type;
{
	xForbid();
	lib->lib_Node.ln_Type = type;
	lib->lib_Flags |= LIBF_CHANGED;
	xSumLibrary(lib);
	xEnqueue(list, &lib->lib_Node);
	xPermit();
}

void
xAddLibrary(lib)
struct Library *lib;
{
	kaddlib(&LibList, lib, NT_LIBRARY);
}

/* RemLibrary and RemDevice: the library's Expunge decides. */
APTR
xRemLibrary(lib)
register struct Library *lib;
{
	APTR (*fn)();
	APTR r;

	xForbid();
	fn = LIBVEC(lib, LIB_EXPUNGE);
	r = (*fn)(lib);
	xPermit();
	return (r);
}

struct Library *
xOpenLibrary(name, ver)
char *name;
ULONG ver;
{
	register struct Library *lib;
	APTR (*fn)();

	xForbid();
	lib = (struct Library *)xFindName(&LibList, name);
	if (lib != NULL && lib->lib_Version < ver)
		lib = NULL;
	if (lib != NULL) {
		fn = LIBVEC(lib, LIB_OPEN);
		lib = (struct Library *)(*fn)(lib, ver);
	}
	xPermit();
	return (lib);
}

struct Library *
xOldOpenLib(name)
char *name;
{
	return (xOpenLibrary(name, 0L));
}

void
xCloseLibrary(lib)
register struct Library *lib;
{
	APTR (*fn)();

	if (lib == NULL)
		return;
	xForbid();
	fn = LIBVEC(lib, LIB_CLOSE);
	(*fn)(lib);
	xPermit();
}

/* The three words of a vector change with interrupts off, so no caller
 * runs a half-written jump. */
APTR
xSetFunction(lib, off, f)
register struct Library *lib;
LONG off;
APTR f;
{
	register struct JumpVec *v;
	APTR old;

	v = (struct JumpVec *)((char *)lib + (WORD)off);
	xDisable();
	lib->lib_Flags |= LIBF_CHANGED;
	old = (APTR)((ULONG)v->jv_Fn & ~LONGSEG);
	setvec(v, f);
	xEnable();
	xSumLibrary(lib);
	return (old);
}

/* With LIBF_SUMUSED: the sum of the vector area's longs.  A changed
 * library takes the new sum; otherwise a mismatch raises AN_LibChkSum. */
void
xSumLibrary(lib)
register struct Library *lib;
{
	register ULONG *p;
	register UWORD n;
	ULONG sum;

	if ((lib->lib_Flags & LIBF_SUMUSED) == 0)
		return;
	xForbid();
	sum = 0;
	p = (ULONG *)((char *)lib - lib->lib_NegSize);
	for (n = lib->lib_NegSize >> 2; n != 0; n--)
		sum += *p++;
	if (lib->lib_Flags & LIBF_CHANGED) {
		lib->lib_Sum = sum;
		lib->lib_Flags &= ~LIBF_CHANGED;
	} else if (lib->lib_Sum != sum)
		xAlert(AN_LibChkSum);
	xPermit();
}

void
xAddResource(res)
APTR res;
{
	xForbid();
	xEnqueue(&ResourceList, (struct Node *)res);
	xPermit();
}

void
xRemResource(res)
APTR res;
{
	xForbid();
	xRemove((struct Node *)res);
	xPermit();
}

APTR
xOpenResource(name)
char *name;
{
	APTR r;

	xForbid();
	r = (APTR)xFindName(&ResourceList, name);
	xPermit();
	return (r);
}

struct Resident *
xFindResident(name)
char *name;
{
	register struct Resident **rp;
	register char *a, *b;

	for (rp = ResModules; *rp != NULL; rp++)
		for (a = (*rp)->rt_Name, b = name; *a == *b; a++, b++)
			if (*a == '\0')
				return (*rp);
	return (NULL);
}

/* Without RTF_AUTOINIT: rt_Init(0L, seg, SysBase).  With it, rt_Init is
 * { dataSize, vectors, structure, initFunction } for MakeLibrary, and
 * the result is added as rt_Type says. */
APTR
xInitResident(rt, seg)
register struct Resident *rt;
APTR seg;
{
	register ULONG *it;
	register struct Library *lib;

	if ((rt->rt_Flags & RTF_AUTOINIT) == 0)
		return ((*(APTR (*)())rt->rt_Init)(0L, seg, SysBase));
	it = (ULONG *)rt->rt_Init;
	lib = xMakeLibrary((APTR)it[1], (APTR)it[2], (APTR)it[3], it[0], seg);
	if (lib != NULL)
		switch (rt->rt_Type) {
		case NT_DEVICE:
			xAddDevice((struct Device *)lib);
			break;
		case NT_LIBRARY:
			xAddLibrary(lib);
			break;
		case NT_RESOURCE:
			xAddResource((APTR)lib);
			break;
		}
	return ((APTR)lib);
}

void
xInitCode(cls, ver)
ULONG cls, ver;
{
	register struct Resident **rp;

	for (rp = ResModules; *rp != NULL; rp++)
		if (((*rp)->rt_Flags & cls) && (*rp)->rt_Version >= ver)
			xInitResident(*rp, NULL);
}

/* A resident driver's or resource's static base: vectors from funcs. */
void
kinitlib(lib, name, funcs)
register struct Library *lib;
char *name;
APTR funcs;
{
	lib->lib_Node.ln_Name = name;
	lib->lib_NegSize = (UWORD)xMakeFunctions(lib, funcs, NULL);
	lib->lib_Version = 1;
}

/* exec.library's own Open, Close, and the Expunge and reserved vectors
 * every resident shares. */
struct Library *
kexecopen(lib)
register struct Library *lib;
{
	lib->lib_OpenCnt++;
	return (lib);
}

APTR
kexecclose(lib)
struct Library *lib;
{
	lib->lib_OpenCnt--;
	return (NULL);
}

APTR
knull()
{
	return (NULL);
}

/* An unimplemented exec.library vector, from kunimpl with the end of the
 * vector called: a recoverable alert naming the LVO, raised through the
 * Alert vector (kalertv) so a patched Alert sees it; the call returns 0. */
ULONG
kbadlvo(end)
char *end;
{
	extern void kalertv();

	kalertv(AN_BadLVO | (POFF(&ExecLib) + LIB_VECTSIZE - POFF(end)));
	return (0L);
}

void
kexecinit()
{
	register struct Library *lib;

	lib = &ExecLib.LibNode;
	lib->lib_Node.ln_Name = execname;
	lib->lib_Flags = LIBF_SUMUSED;
	lib->lib_NegSize = SYSBASE_NEGSIZE;
	lib->lib_PosSize = sizeof(struct ExecBase);
	lib->lib_Version = 39;
	lib->lib_IdString = (APTR)execid;
	xAddLibrary(lib);
}
