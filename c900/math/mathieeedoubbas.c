/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * mathieeedoubbas.library: IEEE double basics.  A boot module.  The
 * functions here are the software path, on the C library's floating-point
 * runtime; with an EPU (AttnFlags AFF_68881) init points the arithmetic,
 * conversion and rounding vectors at mathepu.s, as the Amiga library
 * picks its 68881 code at init.
 */
#include <exec/exec.h>
#include <exec/execbase.h>
#include <exec/resident.h>
#include <lvo/exec_lib.h>

#define MBNAME	"mathieeedoubbas.library"

static char mbname[] = MBNAME;
static char mbid[] = "mathieeedoubbas 39.1 (Exec900)";

extern APTR MbFuncs[];
double modf();
long mbefix(), mbeflt(), mbeadd(), mbesub(), mbemul(), mbediv();
long mbefloor(), mbeceil();

static struct {
	WORD lvo;
	long (*fn)();
} epufn[] = {
	{ -30, mbefix }, { -36, mbeflt }, { -66, mbeadd }, { -72, mbesub },
	{ -78, mbemul }, { -84, mbediv }, { -90, mbefloor }, { -96, mbeceil },
	{ 0, NULL }
};

struct Library *
mbinit(lib, seg, sysbase)
struct Library *lib;
APTR seg, sysbase;
{
	register int i;

	lib->lib_Node.ln_Name = mbname;
	lib->lib_IdString = (APTR)mbid;
	lib->lib_Version = 39;
	lib->lib_Revision = 1;
	if (SysBase->AttnFlags & AFF_68881)
		for (i = 0; epufn[i].fn != NULL; i++)
			SetFunction(lib, (LONG)epufn[i].lvo, (APTR)epufn[i].fn);
	return (lib);
}

struct Library *
mbopen(lib, ver)
struct Library *lib;
ULONG ver;
{
	lib->lib_OpenCnt++;
	return (lib);
}

/* A boot module stays loaded. */
APTR
mbclose(lib)
struct Library *lib;
{
	lib->lib_OpenCnt--;
	return (NULL);
}

APTR
mbexpunge(lib)
struct Library *lib;
{
	return (NULL);
}

LONG
mbnull()
{
	return (0);
}

/* Toward zero; out of range gives 0x80000000. */
LONG
mbfix(lib, x)
struct Library *lib;
double x;
{
	return ((LONG)x);
}

double
mbflt(lib, n)
struct Library *lib;
LONG n;
{
	return ((double)n);
}

/* Compared as sign and magnitude, so -0 equals +0.  NaNs are not
 * told apart. */
LONG
mbcmp(lib, a, b)
struct Library *lib;
double a, b;
{
	register ULONG *x, *y;
	register LONG r;
	ULONG xm, ym;

	x = (ULONG *)&a;
	y = (ULONG *)&b;
	xm = x[0] & 0x7FFFFFFFL;
	ym = y[0] & 0x7FFFFFFFL;
	if ((xm | x[1] | ym | y[1]) == 0)
		return (0);
	if ((x[0] ^ y[0]) & 0x80000000L)
		return ((LONG)x[0] < 0 ? -1L : 1L);
	if (xm != ym)
		r = xm < ym ? -1L : 1L;
	else if (x[1] != y[1])
		r = x[1] < y[1] ? -1L : 1L;
	else
		return (0);
	return ((LONG)x[0] < 0 ? -r : r);
}

LONG
mbtst(lib, x)
struct Library *lib;
double x;
{
	register ULONG *w;

	w = (ULONG *)&x;
	if (((w[0] & 0x7FFFFFFFL) | w[1]) == 0)
		return (0);
	return ((LONG)w[0] < 0 ? -1L : 1L);
}

double
mbabs(lib, x)
struct Library *lib;
double x;
{
	*(UWORD *)&x &= 0x7FFF;
	return (x);
}

double
mbneg(lib, x)
struct Library *lib;
double x;
{
	*(UWORD *)&x ^= 0x8000;
	return (x);
}

double
mbadd(lib, a, b)
struct Library *lib;
double a, b;
{
	return (a + b);
}

double
mbsub(lib, a, b)
struct Library *lib;
double a, b;
{
	return (a - b);
}

double
mbmul(lib, a, b)
struct Library *lib;
double a, b;
{
	return (a * b);
}

double
mbdiv(lib, a, b)
struct Library *lib;
double a, b;
{
	return (a / b);
}

/* modf truncates toward zero; step a negative fraction down. */
double
mbfloor(lib, x)
struct Library *lib;
double x;
{
	double r;

	if (modf(x, &r) < 0.0)
		r -= 1.0;
	return (r);
}

double
mbceil(lib, x)
struct Library *lib;
double x;
{
	double r;

	if (modf(x, &r) > 0.0)
		r += 1.0;
	return (r);
}

static struct {
	ULONG it_Size;
	APTR it_Funcs, it_Struct, it_Init;
} mbinittab = {
	sizeof(struct Library), (APTR)MbFuncs, NULL, (APTR)mbinit
};

struct Resident mbtag = {
	RTC_MATCHWORD, &mbtag, (APTR)(&mbtag + 1), RTF_AUTOINIT, 39,
	NT_LIBRARY, 0, mbname, mbid, (APTR)&mbinittab
};
