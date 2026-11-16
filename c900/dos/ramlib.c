/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * ramlib: libraries and devices loaded from disk, as AmigaOS 2.0's
 * ramlib.  dos.library patches exec's OpenLibrary, OldOpenLibrary and
 * OpenDevice: a name not on the library or device list is loaded from
 * LIBS: or DEVS: (or as given, with a path), its first romtag goes to
 * InitResident, and the open is tried again.  A process loads in its own
 * context; a plain Task asks the ramlib process, unless it holds one of
 * DOS's semaphores, which the ramlib process could not then obtain.
 * Loads are serialised by one semaphore, so concurrent opens load a
 * library once.
 *
 * The seglists loaded here are recorded.  The patched CloseLibrary,
 * CloseDevice, RemLibrary and RemDevice, and the low-memory handler that
 * expunges every recorded library, unload a recorded seglist when the
 * library's Close or Expunge returns it.
 *
 * The patched Alert shows a recoverable alert through intuition.library,
 * as Amiga Exec's Alert does, so the kernel carries no code for it.
 */
#include "dosint.h"
#include <exec/execbase.h>

/* The ramlib process's stack: a load runs LoadSeg and the library's
 * init on it. */
#define RLSTACK		2048L

/* intuition.library's DisplayAlert. */
#define LVODISPALERT	(-90)

/* A seglist ramlib loaded, and the library or device it made. */
struct rlseg {
	struct rlseg *r_Next;
	struct Library *r_Lib;
	BPTR r_Seg;
	int r_Dev;
};

/* A plain Task's request to the ramlib process. */
struct rlmsg {
	struct Message m_Msg;
	char *m_Name;
	int m_Dev;
	int m_Found;
};

static struct SignalSemaphore rlsem;
static struct rlseg *rlsegs;
static UWORD rlgen;		/* seglists rlgone has unloaded */
static struct MsgPort rlport;
static struct Process *rlproc;
static struct Interrupt rlmemint;
static char rlname[] = "ramlib";

struct Library *(*rloldlib)();
static BYTE (*rlolddev)();
static APTR (*rloldrem)(), (*rloldrdev)();
static void (*rloldalert)();
struct SignalSemaphore rlalsem;	/* the alert display's turn */

/* Whether list holds name. */
static int
rlfind(list, name)
struct List *list;
char *name;
{
	register int r;

	Forbid();
	r = FindName(list, name) != NULL;
	Permit();
	return (r);
}

/* The first romtag in seg's hunks, or NULL. */
static struct Resident *
rltag(seg)
BPTR seg;
{
	register UWORD *w;
	register UWORD *e;
	register APTR s;

	for (s = (APTR)seg; s != NULL; s = *(APTR *)s) {
		w = (UWORD *)s + 2;
		e = (UWORD *)((char *)s + (UWORD)((ULONG *)s)[-1] - 10);
		for (; w <= e; w++)
			if (*w == RTC_MATCHWORD && *(UWORD **)(w + 1) == w)
				return ((struct Resident *)w);
	}
	return (NULL);
}

/* Initialise seg's romtag and record seg; unload it when it holds none,
 * names a module already resident, or its init fails. */
static void
rlinit(seg, dev)
BPTR seg;
int dev;
{
	register struct Resident *rt;
	register struct rlseg *r;
	register struct List *list;
	struct Library *lib, *l;

	list = dev ? &SysBase->DeviceList : &SysBase->LibList;
	r = NULL;
	if ((rt = rltag(seg)) == NULL || rlfind(list, rt->rt_Name) ||
	    (r = (struct rlseg *)AllocMem((ULONG)sizeof(*r), MEMF_PUBLIC))
	    == NULL || (lib = (struct Library *)InitResident(rt, seg))
	    == NULL) {
		if (r != NULL)
			FreeMem(r, (ULONG)sizeof(*r));
		xUnLoadSeg(DOSBase, seg);
		return;
	}
	Forbid();
	if ((l = (struct Library *)FindName(list, rt->rt_Name)) != NULL)
		lib = l;
	r->r_Lib = lib;
	r->r_Seg = seg;
	r->r_Dev = dev;
	r->r_Next = rlsegs;
	rlsegs = r;
	Permit();
}

/* Load name (a library, or with dev a device) unless another load put
 * it on its list first: whether it is there now. */
static int
rldo(name, dev)
char *name;
int dev;
{
	register struct List *list;
	register char *path, *base;
	register LONG n;
	BPTR seg;
	int found;

	list = dev ? &SysBase->DeviceList : &SysBase->LibList;
	base = xFilePart(DOSBase, name);
	ObtainSemaphore(&rlsem);
	if (!(found = rlfind(list, base))) {
		path = name;
		if (base == name) {
			n = dstrlen(name);
			if ((path = (char *)AllocMem((ULONG)n + 6, MEMF_PUBLIC))
			    != NULL) {
				CopyMem(dev ? "DEVS:" : "LIBS:", path, 5L);
				CopyMem(name, path + 5, (ULONG)n + 1);
			}
		}
		if (path != NULL && (seg = xLoadSeg(DOSBase, path)) != NULL) {
			rlinit(seg, dev);
			found = rlfind(list, base);
		}
		if (path != NULL && path != name)
			FreeMem(path, (ULONG)n + 6);
	}
	ReleaseSemaphore(&rlsem);
	return (found);
}

static LONG
rlmain(args, len)
char *args;
LONG len;
{
	register struct rlmsg *m;
	register int sig;

	sig = AllocSignal(-1L);
	Forbid();
	rlport.mp_SigBit = sig;
	rlport.mp_SigTask = FindTask(NULL);
	rlport.mp_Flags = PA_SIGNAL;
	Permit();
	for (;;) {
		WaitPort(&rlport);
		while ((m = (struct rlmsg *)GetMsg(&rlport)) != NULL) {
			m->m_Found = rldo(m->m_Name, m->m_Dev);
			ReplyMsg(&m->m_Msg);
		}
	}
}

/* The ramlib process, started on first use: 0 when it cannot start.
 * Requests wait on its port (PA_IGNORE) until it takes a signal. */
static int
rlstart()
{
	struct TagItem t[5];

	Forbid();
	if (rlproc == NULL) {
		t[0].ti_Tag = NP_Entry;
		t[0].ti_Data = (ULONG)rlmain;
		t[1].ti_Tag = NP_Name;
		t[1].ti_Data = (ULONG)rlname;
		t[2].ti_Tag = NP_StackSize;
		t[2].ti_Data = RLSTACK;
		t[3].ti_Tag = NP_CopyVars;
		t[3].ti_Data = FALSE;
		t[4].ti_Tag = TAG_DONE;
		rlproc = xCreateNewProc(DOSBase, t);
	}
	Permit();
	return (rlproc != NULL);
}

/* Load name for the caller: in its own context when it is a process or
 * holds a DOS semaphore, else through the ramlib process. */
static int
rlload(name, dev)
char *name;
int dev;
{
	register struct Task *me;
	register struct DosInfo *di;
	struct MsgPort *mp;
	struct rlmsg m;

	me = FindTask(NULL);
	di = ddi();
	if (me->tc_Node.ln_Type == NT_PROCESS ||
	    di->di_DevLock.ss_Owner == me || di->di_EntryLock.ss_Owner == me ||
	    di->di_DeleteLock.ss_Owner == me || rlsem.ss_Owner == me)
		return (rldo(name, dev));
	if (!rlstart() || (mp = CreateMsgPort()) == NULL)
		return (0);
	m.m_Msg.mn_Node.ln_Type = NT_MESSAGE;
	m.m_Msg.mn_ReplyPort = mp;
	m.m_Msg.mn_Length = sizeof(m);
	m.m_Name = name;
	m.m_Dev = dev;
	m.m_Found = 0;
	PutMsg(&rlport, &m.m_Msg);
	WaitPort(mp);
	GetMsg(mp);
	DeleteMsgPort(mp);
	return (m.m_Found);
}

/* A seglist a library's Close or Expunge returned: unload it if ramlib
 * loaded it. */
static void
rlgone(seg)
BPTR seg;
{
	register struct rlseg **pp, *r;

	if (seg == NULL)
		return;
	Forbid();
	for (pp = &rlsegs; (r = *pp) != NULL; pp = &r->r_Next)
		if (r->r_Seg == seg) {
			*pp = r->r_Next;
			rlgen++;
			break;
		}
	Permit();
	if (r != NULL) {
		xUnLoadSeg(DOSBase, seg);
		FreeMem(r, (ULONG)sizeof(*r));
	}
}

/* The open, then, when name is not on the list (checked in the same
 * Forbid, so a load that ends in between is seen), a load and the open
 * of its file part. */
static struct Library *
rlopenlib(name, ver)
char *name;
ULONG ver;
{
	register struct Library *lib;
	register char *base;
	int found;

	base = xFilePart(DOSBase, name);
	Forbid();
	if ((lib = (*rloldlib)(name, ver)) == NULL)
		found = rlfind(&SysBase->LibList, base);
	Permit();
	if (lib == NULL && (found ? base != name : rlload(name, 0)))
		lib = (*rloldlib)(base, ver);
	return (lib);
}

static struct Library *
rloldopen(name)
char *name;
{
	return (rlopenlib(name, 0L));
}

static BYTE
rlopendev(name, unit, ior, flags)
char *name;
ULONG unit;
struct IORequest *ior;
ULONG flags;
{
	register char *base;
	register BYTE e;
	int found;

	base = xFilePart(DOSBase, name);
	Forbid();
	if ((e = (*rlolddev)(name, unit, ior, flags)) != 0)
		found = rlfind(&SysBase->DeviceList, base);
	Permit();
	if (e != 0 && (found ? base != name : rlload(name, 1)))
		e = (*rlolddev)(base, unit, ior, flags);
	return (e);
}

static void
rlcloselib(lib)
register struct Library *lib;
{
	BPTR seg;

	if (lib == NULL)
		return;
	Forbid();
	seg = (BPTR)(*(APTR (*)())((char *)lib + LIB_CLOSE))(lib);
	Permit();
	rlgone(seg);
}

static void
rlclosedev(ior)
register struct IORequest *ior;
{
	register struct Device *dev;
	BPTR seg;

	Forbid();
	seg = NULL;
	if ((dev = ior->io_Device) != NULL) {
		seg = (BPTR)(*(APTR (*)())((char *)dev + LIB_CLOSE))(dev, ior);
		ior->io_Device = NULL;
		ior->io_Unit = NULL;
	}
	Permit();
	rlgone(seg);
}

/* The seglist RemLibrary returns for a library ramlib loaded is already
 * unloaded. */
static APTR
rlremlib(lib)
struct Library *lib;
{
	APTR seg;

	rlgone((BPTR)(seg = (*rloldrem)(lib)));
	return (seg);
}

static APTR
rlremdev(dev)
struct Library *dev;
{
	APTR seg;

	rlgone((BPTR)(seg = (*rloldrdev)(dev)));
	return (seg);
}

/* Low memory: expunge every library and device ramlib loaded; those
 * still open set LIBF_DELEXP and go at their last close.  An Expunge
 * may close other recorded libraries, which then unload, so the walk
 * starts again whenever a seglist went. */
static LONG
rlmemh(mhd, data, sysbase)
struct MemHandlerData *mhd;
APTR data, sysbase;
{
	register struct rlseg *r;
	UWORD g, g0;

	g0 = rlgen;
	for (r = rlsegs; r != NULL; ) {
		g = rlgen;
		rlgone((BPTR)(*(r->r_Dev ? rloldrdev : rloldrem))(r->r_Lib));
		r = rlgen != g ? rlsegs : r->r_Next;
	}
	return (rlgen != g0 ? MEM_ALL_DONE : MEM_DID_NOTHING);
}

/* Amiga Exec's recoverable alert text: lines of a 2-byte x, a y, the
 * text and a continuation byte.  The last line is formatted. */
static char rlguru[] = "\0\46\17Recoverable Alert. \0\377\0\352\17\
Press left mouse button to continue.\0\377\0\216\36";
static char rlgurufmt[] = "Guru Meditation #%08lx.%08lx";
static char rlintname[] = "intuition.library";
static char rlinput[] = "input.device";

/*
 * Exec's Alert, then for a recoverable alert from a Task outside Forbid
 * and Disable and on its own stack (an interrupt runs on another),
 * intuition's DisplayAlert, 40 lines high, when intuition.library is on
 * the library list; it is never loaded for an alert.  The answer is
 * ignored, as on the Amiga.  Alerts are shown one at a time, in turn
 * (rlalsem).  A Task the display may wait for does not wait for its
 * turn: one already showing an alert, the ramlib process and the
 * input.device Task; theirs stays console text while another is shown.
 */
static void
rlalert(num)
ULONG num;
{
	register struct Task *me;
	register struct Library *ib;
	ULONG a[2];
	char buf[sizeof(rlguru) + sizeof(rlgurufmt) + 8];
	char *p;

	(*rloldalert)(num);
	me = FindTask(NULL);
	p = buf;
	if (me->tc_TDNestCnt >= 0 || me->tc_IDNestCnt >= 0 ||
	    (UWORD)(ULONG)p < (UWORD)(ULONG)me->tc_SPLower ||
	    (UWORD)(ULONG)p > (UWORD)(ULONG)me->tc_SPUpper ||
	    rlalsem.ss_Owner == me ||
	    (ib = (*rloldlib)(rlintname, 0L)) == NULL)
		return;
	if (!AttemptSemaphore(&rlalsem)) {
		if (me == &rlproc->pr_Task || me == FindTask(rlinput)) {
			CloseLibrary(ib);
			return;
		}
		ObtainSemaphore(&rlalsem);
	}
	a[0] = num;
	a[1] = (ULONG)me;
	CopyMem(rlguru, p, (ULONG)sizeof(rlguru) - 1);
	p += sizeof(rlguru) - 1;
	RawDoFmt(rlgurufmt, (APTR)a, (APTR)dnumput, (APTR)&p);
	*p = 0;
	(*(BOOL (*)())((char *)ib + LVODISPALERT))(ib, 0L, buf, 40);
	ReleaseSemaphore(&rlalsem);
	CloseLibrary(ib);
}

/* From dos.library's init. */
void
rlsetup()
{
	InitSemaphore(&rlsem);
	InitSemaphore(&rlalsem);
	rlport.mp_Node.ln_Type = NT_MSGPORT;
	rlport.mp_Node.ln_Name = rlname;
	rlport.mp_Flags = PA_IGNORE;
	NewList(&rlport.mp_MsgList);
	rloldlib = (struct Library *(*)())SetFunction(SysBase,
	    LVOOpenLibrary, rlopenlib);
	SetFunction(SysBase, LVOOldOpenLibrary, rloldopen);
	rlolddev = (BYTE (*)())SetFunction(SysBase, LVOOpenDevice, rlopendev);
	SetFunction(SysBase, LVOCloseLibrary, rlcloselib);
	SetFunction(SysBase, LVOCloseDevice, rlclosedev);
	rloldrem = (APTR (*)())SetFunction(SysBase, LVORemLibrary, rlremlib);
	rloldrdev = (APTR (*)())SetFunction(SysBase, LVORemDevice, rlremdev);
	rloldalert = (void (*)())SetFunction(SysBase, LVOAlert, rlalert);
	rlmemint.is_Node.ln_Type = NT_INTERRUPT;
	rlmemint.is_Node.ln_Name = rlname;
	rlmemint.is_Code = (int (*)())rlmemh;
	AddMemHandler(&rlmemint);
}
