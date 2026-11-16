/* SPDX-License-Identifier: MIT */
/*
 * dlfail.library, for tests/ramlib.c: a disk library whose init fails.
 * As an Amiga libInit does on failure, it frees the base MakeLibrary
 * allocated and returns NULL; ramlib then unloads the seglist.
 */
#include <exec/exec.h>
#include <lvo/exec_lib.h>

static char dfname[] = "dlfail.library";
static char dfid[] = "dlfail 1.0 (failing init)";

static APTR
dfnull()
{
	return (NULL);
}

static struct Library *
dfinit(lib, seg, sysbase)
register struct Library *lib;
APTR seg, sysbase;
{
	FreeMem((char *)lib - lib->lib_NegSize,
	    (ULONG)(lib->lib_NegSize + lib->lib_PosSize));
	return (NULL);
}

static APTR dffuncs[] = {
	(APTR)dfnull, (APTR)dfnull, (APTR)dfnull, (APTR)dfnull, (APTR)-1L
};

static struct {
	ULONG it_Size;
	APTR it_Funcs, it_Struct, it_Init;
} dfinittab = {
	sizeof(struct Library), (APTR)dffuncs, NULL, (APTR)dfinit
};

struct Resident dftag = {
	RTC_MATCHWORD, &dftag, (APTR)(&dftag + 1), RTF_AUTOINIT, 1,
	NT_LIBRARY, 0, dfname, dfid, (APTR)&dfinittab
};
