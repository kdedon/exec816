/* SPDX-License-Identifier: MIT */
/*
 * Libraries and devices from disk (dos.library's ramlib).  The root
 * program is a plain Task.  dlt.library, dlfail.library and
 * tmod2.library (tmod's hunk) are in LIBS:, dlt.device in DEVS:, and
 * tmod.library is a boot module.
 *
 * Two processes and the root Task open dlt.library at once: it is loaded
 * and initialised once.  A low-memory AllocMem expunges it and frees its
 * segments; RemLibrary while open defers the expunge to the last close.
 * A path name loads its file part, and a name longer than 14 bytes
 * finds its truncated V7 file.  Missing names fail cleanly, a
 * failing init leaves no memory behind, a file whose romtag is already
 * resident is unloaded, the device loads, works and is expunged, and a
 * plain Task holding the DosList lock loads in its own context.
 * dlnest.library opens dlt.library and dltlongname.library and closes
 * them in its Expunge: a low-memory expunge that unloads them from
 * inside its walk frees every segment.
 */
#include "test.h"
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>
#include <lvo/dlt_lib.h>

struct DosLibrary *DOSBase;
struct Library *DltBase;

static struct Library *pbase[2], *pmiss[2];
static struct Task *roottask;

static int
same(a, b)
register char *a, *b;
{
	while (*a == *b && *a != '\0')
		a++, b++;
	return (*a == *b);
}

/* The entries called name on list. */
static int
count(list, name)
struct List *list;
char *name;
{
	register struct Node *n;
	register int k;

	k = 0;
	Forbid();
	for (n = list->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (same(n->ln_Name, name))
			k++;
	Permit();
	return (k);
}

#define NLIB(name)	count(&SysBase->LibList, name)
#define NDEV(name)	count(&SysBase->DeviceList, name)
#define AVAIL()		AvailMem(MEMF_ANY)

static LONG
popen(i)
int i;
{
	pbase[i] = OpenLibrary("dlt.library", 1L);
	pmiss[i] = OpenLibrary("nonesuch.library", 0L);
	return (0);
}

static LONG
p0main(args, len)
char *args;
LONG len;
{
	return (popen(0));
}

static LONG
p1main(args, len)
char *args;
LONG len;
{
	return (popen(1));
}

static void
pdone(rc, sig)
LONG rc, sig;
{
	Signal(roottask, sig);
}

static struct Process *
start(entry, name, sig)
LONG (*entry)();
char *name;
ULONG sig;
{
	struct TagItem t[5];

	t[0].ti_Tag = NP_Entry;
	t[0].ti_Data = (ULONG)entry;
	t[1].ti_Tag = NP_Name;
	t[1].ti_Data = (ULONG)name;
	t[2].ti_Tag = NP_ExitCode;
	t[2].ti_Data = (ULONG)pdone;
	t[3].ti_Tag = NP_ExitData;
	t[3].ti_Data = sig;
	t[4].ti_Tag = TAG_DONE;
	return (CreateNewProc(t));
}

/* Expunge every disk library: an allocation that cannot succeed. */
static void
lowmem()
{
	APTR p;
	ULONG n;

	n = AvailMem(MEMF_ANY) + 0x20000L;
	if ((p = AllocMem(n, MEMF_ANY)) != NULL)
		FreeMem(p, n);
}

static char *
concurrent()
{
	struct Library *lib;
	ULONG got;

	if (NLIB("dlt.library") != 0)
		return ("dlt.library resident before the test");
	if (start(p0main, "dlt0", SIGBREAKF_CTRL_E) == NULL ||
	    start(p1main, "dlt1", SIGBREAKF_CTRL_F) == NULL)
		return ("CreateNewProc");
	lib = OpenLibrary("dlt.library", 1L);
	for (got = 0; got != (SIGBREAKF_CTRL_E | SIGBREAKF_CTRL_F); )
		got |= Wait(SIGBREAKF_CTRL_E | SIGBREAKF_CTRL_F);
	if (lib == NULL)
		return ("OpenLibrary from a plain Task");
	if (pbase[0] == NULL || pbase[1] == NULL)
		return ("OpenLibrary from a process");
	if (pbase[0] != lib || pbase[1] != lib)
		return ("concurrent opens got different bases");
	if (pmiss[0] != NULL || pmiss[1] != NULL)
		return ("missing library opened from a process");
	DltBase = lib;
	if (NLIB("dlt.library") != 1 || DltInits() != 1 ||
	    lib->lib_OpenCnt != 3)
		return ("concurrent opens loaded more than once");
	if (DltAdd(40, 2) != 42 || DltSeg() == NULL)
		return ("dlt.library calls");
	CloseLibrary(lib);
	CloseLibrary(lib);
	CloseLibrary(lib);
	if (NLIB("dlt.library") != 1 || lib->lib_OpenCnt != 0)
		return ("closed library not kept");
	return (NULL);
}

static char *
expunge()
{
	struct Library *lib, *l2;
	ULONG m0, m1;

	lowmem();
	if (NLIB("dlt.library") != 0)
		return ("low memory did not expunge dlt.library");
	m0 = AVAIL();
	/* Load again; the low-memory expunge gives back every byte. */
	if ((lib = OpenLibrary("dlt.library", 1L)) == NULL)
		return ("reload");
	if ((m1 = AVAIL()) >= m0)
		return ("load took no memory");
	CloseLibrary(lib);
	lowmem();
	if (NLIB("dlt.library") != 0 || AVAIL() != m0)
		return ("expunge did not free the segments");
	/* RemLibrary while open: the last close expunges. */
	lib = OpenLibrary("dlt.library", 1L);
	if (lib == NULL || AVAIL() != m1)
		return ("second reload");
	if (RemLibrary(lib) != NULL || (lib->lib_Flags & LIBF_DELEXP) == 0 ||
	    NLIB("dlt.library") != 1)
		return ("RemLibrary while open");
	CloseLibrary(lib);
	if (NLIB("dlt.library") != 0 || AVAIL() != m0)
		return ("LIBF_DELEXP expunge at last close");
	/* A path name: its file part is the library's name. */
	if ((lib = OpenLibrary("LIBS:dlt.library", 1L)) == NULL)
		return ("OpenLibrary LIBS:dlt.library");
	l2 = OpenLibrary("dlt.library", 0L);
	DltBase = lib;
	if (l2 != lib || NLIB("dlt.library") != 1 || DltInits() != 1 ||
	    lib->lib_OpenCnt != 2)
		return ("path and plain names differ");
	if (OpenLibrary("dlt.library", 2L) != NULL ||
	    OpenLibrary("LIBS:dlt.library", 2L) != NULL ||
	    NLIB("dlt.library") != 1)
		return ("version 2 of dlt.library");
	CloseLibrary(l2);
	CloseLibrary(lib);
	if (RemLibrary(lib) == NULL || NLIB("dlt.library") != 0 ||
	    AVAIL() != m0)
		return ("RemLibrary after the last close");
	/* A name longer than a V7 entry: the file keeps 14 bytes of it. */
	if ((lib = OpenLibrary("dltlongname.library", 1L)) == NULL ||
	    NLIB("dltlongname.library") != 1)
		return ("OpenLibrary dltlongname.library");
	CloseLibrary(lib);
	if (RemLibrary(lib) == NULL || AVAIL() != m0)
		return ("dltlongname.library expunge");
	return (NULL);
}

/* Delayed expunges: the low-memory walk reaches dlnest.library, the
 * newest seglist, whose Expunge unloads the two next on the list. */
static char *
nested()
{
	struct Library *dlt, *nest;
	ULONG m0;

	m0 = AVAIL();
	dlt = OpenLibrary("dlt.library", 1L);
	if ((nest = OpenLibrary("dlnest.library", 1L)) == NULL || dlt == NULL ||
	    NLIB("dlt.library") != 1 || NLIB("dltlongname.library") != 1 ||
	    dlt->lib_OpenCnt != 2)
		return ("OpenLibrary dlnest.library");
	lowmem();
	if (NLIB("dlnest.library") != 1 || NLIB("dlt.library") != 1 ||
	    NLIB("dltlongname.library") != 1 ||
	    (dlt->lib_Flags & LIBF_DELEXP) == 0)
		return ("low memory expunged an open library");
	if (OpenLibrary("dlnest.library", 1L) != nest)
		return ("reopen dlnest.library");
	CloseLibrary(nest);
	CloseLibrary(nest);
	CloseLibrary(dlt);
	if (NLIB("dlt.library") != 1 || dlt->lib_OpenCnt != 1)
		return ("dlt.library closed by its user");
	lowmem();
	if (NLIB("dlnest.library") != 0 || NLIB("dlt.library") != 0 ||
	    NLIB("dltlongname.library") != 0)
		return ("nested expunge left a library");
	if (AVAIL() != m0)
		return ("nested expunge did not free the segments");
	return (NULL);
}

static char *
failures()
{
	struct IOStdReq io;
	ULONG m0;

	/* The first use of DEVS: binds its late assign. */
	io.io_Message.mn_ReplyPort = NULL;
	io.io_Message.mn_Length = sizeof(io);
	OpenDevice("nonesuch.device", 0L, (struct IORequest *)&io, 0L);
	m0 = AVAIL();
	if (OpenLibrary("nonesuch.library", 0L) != NULL ||
	    OpenLibrary("LIBS:nonesuch.library", 0L) != NULL ||
	    OldOpenLibrary("nonesuch.library") != NULL)
		return ("missing library opened");
	if (OpenDevice("nonesuch.device", 0L, (struct IORequest *)&io, 0L) !=
	    IOERR_OPENFAIL || io.io_Error != IOERR_OPENFAIL ||
	    io.io_Device != NULL)
		return ("missing device opened");
	if (AVAIL() != m0)
		return ("a missing name kept memory");
	if (OpenLibrary("dlfail.library", 0L) != NULL ||
	    NLIB("dlfail.library") != 0)
		return ("failing init opened");
	if (AVAIL() != m0)
		return ("failing init not unloaded");
	return (NULL);
}

static char *
coexist()
{
	struct Library *t1, *t2;
	ULONG m0;

	m0 = AVAIL();
	t1 = OpenLibrary("tmod.library", 1L);
	t2 = OpenLibrary("LIBS:tmod.library", 1L);
	if (t1 == NULL || t2 != t1 || NLIB("tmod.library") != 1)
		return ("boot module opened twice");
	CloseLibrary(t2);
	CloseLibrary(t1);
	/* tmod2.library holds tmod.library's romtag, already resident. */
	if (OpenLibrary("tmod2.library", 0L) != NULL ||
	    NLIB("tmod.library") != 1 || NLIB("tmod2.library") != 0)
		return ("resident romtag initialised again");
	if (AVAIL() != m0)
		return ("resident romtag's file not unloaded");
	return (NULL);
}

static char *
device()
{
	struct MsgPort *mp;
	struct IOStdReq *io;
	ULONG m0, m1;
	char *why;

	why = NULL;
	if ((mp = CreateMsgPort()) == NULL || (io = (struct IOStdReq *)
	    CreateIORequest(mp, (ULONG)sizeof(*io))) == NULL)
		return ("no request");
	m0 = AVAIL();
	if (OpenDevice("dlt.device", 1L, (struct IORequest *)io, 0L) !=
	    IOERR_OPENFAIL || NDEV("dlt.device") != 1)
		why = "unit 1 of dlt.device";
	else if ((m1 = AVAIL()) >= m0)
		why = "device load took no memory";
	else if (OpenDevice("dlt.device", 0L, (struct IORequest *)io, 0L) != 0
	    || AVAIL() != m1)
		why = "OpenDevice dlt.device";
	else {
		io->io_Command = CMD_READ;
		if (DoIO((struct IORequest *)io) != 0 || io->io_Actual != 42)
			why = "dlt.device CMD_READ";
		CloseDevice((struct IORequest *)io);
		if (why == NULL && (io->io_Device != NULL ||
		    NDEV("dlt.device") != 1))
			why = "CloseDevice";
		lowmem();
		if (why == NULL && (NDEV("dlt.device") != 0 || AVAIL() != m0))
			why = "device expunge";
	}
	DeleteIORequest((struct IORequest *)io);
	DeleteMsgPort(mp);
	return (why);
}

/* A plain Task holding the DosList lock loads in its own context. */
static char *
locked()
{
	struct Library *lib;

	if (LockDosList(LDF_ALL | LDF_WRITE) == NULL)
		return ("LockDosList");
	lib = OpenLibrary("dlt.library", 1L);
	UnLockDosList(LDF_ALL | LDF_WRITE);
	if (lib == NULL)
		return ("OpenLibrary holding the DosList lock");
	CloseLibrary(lib);
	if (RemLibrary(lib) == NULL || NLIB("dlt.library") != 0)
		return ("expunge after the locked load");
	return (NULL);
}

int
main()
{
	char *why;

	roottask = FindTask(NULL);
	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 36L))
	    == NULL) {
		KPutStr("FAIL ramlib: no dos.library\n");
		return (1);
	}
	if ((why = concurrent()) == NULL && (why = expunge()) == NULL &&
	    (why = nested()) == NULL && (why = failures()) == NULL && (why = coexist()) == NULL &&
	    (why = device()) == NULL)
		why = locked();
	if (why != NULL) {
		xKPrintf("FAIL ramlib: %s\n", why);
		return (1);
	}
	KPutStr("PASS ramlib\n");
	return (0);
}
