/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * mathieeedoubtrans.library: IEEE double transcendentals, a boot module
 * over the C library's libm.  libm's arithmetic runs on the C runtime,
 * which uses the EPU when one is fitted; Sqrt is the EPU's FSQR then.
 */
#include <exec/exec.h>
#include <exec/execbase.h>
#include <exec/resident.h>
#include <lvo/exec_lib.h>

static char mtname[] = "mathieeedoubtrans.library";
static char mtid[] = "mathieeedoubtrans 39.1 (Exec900)";

extern APTR MtFuncs[];
int errno;			/* libm sets it; nothing reads it */
double atan(), sin(), cos(), tan(), sinh(), cosh(), tanh(), exp(), log();
double pow(), sqrt(), asin(), acos(), log10();
long mtesqrt();

struct Library *
mtinit(lib, seg, sysbase)
struct Library *lib;
APTR seg, sysbase;
{
	lib->lib_Node.ln_Name = mtname;
	lib->lib_IdString = (APTR)mtid;
	lib->lib_Version = 39;
	lib->lib_Revision = 1;
	if (SysBase->AttnFlags & AFF_68881)
		SetFunction(lib, -96L, (APTR)mtesqrt);
	return (lib);
}

struct Library *
mtopen(lib, ver)
struct Library *lib;
ULONG ver;
{
	lib->lib_OpenCnt++;
	return (lib);
}

/* A boot module stays loaded. */
APTR
mtclose(lib)
struct Library *lib;
{
	lib->lib_OpenCnt--;
	return (NULL);
}

APTR
mtexpunge(lib)
struct Library *lib;
{
	return (NULL);
}

LONG
mtnull()
{
	return (0);
}

#define ONE(name, fn) \
double name(lib, x) struct Library *lib; double x; { return (fn(x)); }

ONE(mtatan, atan)
ONE(mtsin, sin)
ONE(mtcos, cos)
ONE(mttan, tan)
ONE(mtsinh, sinh)
ONE(mtcosh, cosh)
ONE(mttanh, tanh)
ONE(mtexp, exp)
ONE(mtlog, log)
ONE(mtsqrt, sqrt)
ONE(mtasin, asin)
ONE(mtacos, acos)
ONE(mtlog10, log10)

double
mtsincos(lib, cp, x)
struct Library *lib;
double *cp;
double x;
{
	*cp = cos(x);
	return (sin(x));
}

/* arg raised to exp, as Amiga's IEEEDPPow(exp, arg). */
double
mtpow(lib, e, x)
struct Library *lib;
double e, x;
{
	return (pow(x, e));
}

LONG
mttieee(lib, x)
struct Library *lib;
double x;
{
	float f;

	f = x;
	return (*(LONG *)&f);
}

double
mtfieee(lib, n)
struct Library *lib;
LONG n;
{
	return (*(float *)&n);
}

static struct {
	ULONG it_Size;
	APTR it_Funcs, it_Struct, it_Init;
} mtinittab = {
	sizeof(struct Library), (APTR)MtFuncs, NULL, (APTR)mtinit
};

struct Resident mttag = {
	RTC_MATCHWORD, &mttag, (APTR)(&mttag + 1), RTF_AUTOINIT, 39,
	NT_LIBRARY, 0, mtname, mtid, (APTR)&mtinittab
};
