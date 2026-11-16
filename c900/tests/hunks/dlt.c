/* SPDX-License-Identifier: MIT */
/*
 * dlt.library: the disk-loaded library of tests/ramlib.c (LIBS:), made
 * as docs/platform.md's recipe says.  Its vectors are DltFuncs,
 * generated from dlt_lib.fd; Open, Close and Expunge are the Amiga
 * library's: Expunge while open sets LIBF_DELEXP, the last Close then
 * expunges, and Expunge frees the base and returns the seglist.
 */
#include <exec/exec.h>
#include <lvo/exec_lib.h>

struct DltBase {
	struct Library db_Lib;
	APTR db_Seg;
};

#ifndef DTNAME
#define DTNAME	"dlt.library"
#endif

static char dtname[] = DTNAME;
static char dtid[] = "dlt 1.0 (disk library test)";
static ULONG dtcount;		/* inits of this loaded copy */

extern APTR DltFuncs[];

struct Library *
dtopen(lib, ver)
struct Library *lib;
ULONG ver;
{
	lib->lib_OpenCnt++;
	lib->lib_Flags &= ~LIBF_DELEXP;
	return (lib);
}

APTR
dtexpunge(lib)
register struct Library *lib;
{
	APTR seg;

	if (lib->lib_OpenCnt != 0) {
		lib->lib_Flags |= LIBF_DELEXP;
		return (NULL);
	}
	seg = ((struct DltBase *)lib)->db_Seg;
	Remove(&lib->lib_Node);
	FreeMem((char *)lib - lib->lib_NegSize,
	    (ULONG)(lib->lib_NegSize + lib->lib_PosSize));
	return (seg);
}

APTR
dtclose(lib)
struct Library *lib;
{
	if (--lib->lib_OpenCnt == 0 && (lib->lib_Flags & LIBF_DELEXP))
		return (dtexpunge(lib));
	return (NULL);
}

APTR
dtnull()
{
	return (NULL);
}

LONG
dtadd(db, a, b)
struct DltBase *db;
LONG a, b;
{
	return (a + b);
}

ULONG
dtinits(db)
struct DltBase *db;
{
	return (dtcount);
}

APTR
dtseg(db)
struct DltBase *db;
{
	return (db->db_Seg);
}

static struct Library *
dtinit(lib, seg, sysbase)
struct Library *lib;
APTR seg, sysbase;
{
	lib->lib_Node.ln_Name = dtname;
	lib->lib_IdString = (APTR)dtid;
	lib->lib_Version = 1;
	((struct DltBase *)lib)->db_Seg = seg;
	dtcount++;
	return (lib);
}

static struct {
	ULONG it_Size;
	APTR it_Funcs, it_Struct, it_Init;
} dtinittab = {
	sizeof(struct DltBase), (APTR)DltFuncs, NULL, (APTR)dtinit
};

struct Resident dttag = {
	RTC_MATCHWORD, &dttag, (APTR)(&dttag + 1), RTF_AUTOINIT, 1,
	NT_LIBRARY, 0, dtname, dtid, (APTR)&dtinittab
};
