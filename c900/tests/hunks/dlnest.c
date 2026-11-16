/* SPDX-License-Identifier: MIT */
/*
 * dlnest.library, for tests/ramlib.c: a disk library that opens two
 * other disk libraries, dlt.library and dltlongname.library, in its init
 * and closes them in its Expunge, as asl.library does gadtools.library.
 */
#include <exec/exec.h>
#include <lvo/exec_lib.h>

struct NestBase {
	struct Library nb_Lib;
	APTR nb_Seg;
	struct Library *nb_A, *nb_B;
};

static char dnname[] = "dlnest.library";
static char dnid[] = "dlnest 1.0 (nested expunge test)";

static struct Library *
dnopen(lib, ver)
struct Library *lib;
ULONG ver;
{
	lib->lib_OpenCnt++;
	lib->lib_Flags &= ~LIBF_DELEXP;
	return (lib);
}

static void
dnfree(lib)
register struct Library *lib;
{
	FreeMem((char *)lib - lib->lib_NegSize,
	    (ULONG)(lib->lib_NegSize + lib->lib_PosSize));
}

static APTR
dnexpunge(lib)
register struct Library *lib;
{
	register struct NestBase *nb;
	APTR seg;

	if (lib->lib_OpenCnt != 0) {
		lib->lib_Flags |= LIBF_DELEXP;
		return (NULL);
	}
	nb = (struct NestBase *)lib;
	seg = nb->nb_Seg;
	Remove(&lib->lib_Node);
	CloseLibrary(nb->nb_A);
	CloseLibrary(nb->nb_B);
	dnfree(lib);
	return (seg);
}

static APTR
dnclose(lib)
struct Library *lib;
{
	if (--lib->lib_OpenCnt == 0 && (lib->lib_Flags & LIBF_DELEXP))
		return (dnexpunge(lib));
	return (NULL);
}

static APTR
dnnull()
{
	return (NULL);
}

static struct Library *
dninit(lib, seg, sysbase)
struct Library *lib;
APTR seg, sysbase;
{
	register struct NestBase *nb;

	nb = (struct NestBase *)lib;
	lib->lib_Node.ln_Name = dnname;
	lib->lib_IdString = (APTR)dnid;
	lib->lib_Version = 1;
	nb->nb_Seg = seg;
	nb->nb_A = OpenLibrary("dlt.library", 1L);
	nb->nb_B = OpenLibrary("dltlongname.library", 1L);
	if (nb->nb_A == NULL || nb->nb_B == NULL) {
		CloseLibrary(nb->nb_A);
		CloseLibrary(nb->nb_B);
		dnfree(lib);
		return (NULL);
	}
	return (lib);
}

static APTR dnfuncs[] = {
	(APTR)dnopen, (APTR)dnclose, (APTR)dnexpunge, (APTR)dnnull, (APTR)-1L
};

static struct {
	ULONG it_Size;
	APTR it_Funcs, it_Struct, it_Init;
} dninittab = {
	sizeof(struct NestBase), (APTR)dnfuncs, NULL, (APTR)dninit
};

struct Resident dntag = {
	RTC_MATCHWORD, &dntag, (APTR)(&dntag + 1), RTF_AUTOINIT, 1,
	NT_LIBRARY, 0, dnname, dnid, (APTR)&dninittab
};
