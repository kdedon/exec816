/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * dos.library's DosList: devices, volumes and assigns, as AmigaDOS V36,
 * guarded by di_DevLock.  GetDeviceProc resolves a name's device part:
 * it starts a device's handler on first use (CreateNewProc on its
 * dol_SegList, then ACTION_STARTUP) and binds a late assign
 * (AssignLate) on first use.  Handler starts are serialised by
 * di_EntryLock, so a handler is started once.
 */
#include "dosint.h"

void dfreeal();

static struct TagItem *
dtag(t, tag, data)
register struct TagItem *t;
Tag tag;
ULONG data;
{
	t->ti_Tag = tag;
	t->ti_Data = data;
	return (t + 1);
}

static ULONG
dltype(type)
LONG type;
{
	switch ((int)type) {
	case DLT_DEVICE:
		return (LDF_DEVICES);
	case DLT_VOLUME:
		return (LDF_VOLUMES);
	}
	return (LDF_ASSIGNS);
}

struct DosList *
xLockDosList(db, flags)
DB db;
ULONG flags;
{
	register struct DosInfo *di;

	di = ddi();
	if (flags & LDF_WRITE)
		ObtainSemaphore(&di->di_DevLock);
	else
		ObtainSemaphoreShared(&di->di_DevLock);
	/* dol_Next is the first field, so the head reads as an entry. */
	return ((struct DosList *)&di->di_DevInfo);
}

void
xUnLockDosList(db, flags)
DB db;
ULONG flags;
{
	ReleaseSemaphore(&ddi()->di_DevLock);
}

struct DosList *
xNextDosEntry(db, dl, flags)
DB db;
register struct DosList *dl;
ULONG flags;
{
	while ((dl = (struct DosList *)dl->dol_Next) != NULL)
		if (dltype(dl->dol_Type) & flags)
			break;
	return (dl);
}

/* name may end at a colon. */
struct DosList *
xFindDosEntry(db, dl, name, flags)
DB db;
register struct DosList *dl;
char *name;
ULONG flags;
{
	while ((dl = xNextDosEntry(db, dl, flags)) != NULL)
		if (dnamecmp(name, dl->dol_Name, 255) == 0)
			break;
	return (dl);
}

struct DosList *
xMakeDosEntry(db, name, type)
DB db;
char *name;
LONG type;
{
	register struct DosList *dl;
	register char *d;

	dl = (struct DosList *)AllocVec((ULONG)sizeof(*dl) + dstrlen(name) + 1,
	    MEMF_PUBLIC | MEMF_CLEAR);
	if (dl == NULL) {
		dseterr(ERROR_NO_FREE_STORE);
		return (NULL);
	}
	dl->dol_Type = type;
	dl->dol_Name = d = (char *)(dl + 1);
	while ((*d++ = *name++) != '\0')
		;
	return (dl);
}

void
xFreeDosEntry(db, dl)
DB db;
struct DosList *dl;
{
	FreeVec(dl);
}

/* Fails when a device or assign already has the name; volumes may
 * repeat. */
LONG
xAddDosEntry(db, dl)
DB db;
register struct DosList *dl;
{
	register struct DosList *h;
	LONG r;

	h = xLockDosList(db, LDF_WRITE);
	r = DOSTRUE;
	if (dl->dol_Type != DLT_VOLUME &&
	    xFindDosEntry(db, h, dl->dol_Name, LDF_DEVICES | LDF_ASSIGNS))
		r = dfail(ERROR_OBJECT_EXISTS);
	else {
		dl->dol_Next = h->dol_Next;
		h->dol_Next = (BPTR)dl;
	}
	xUnLockDosList(db, LDF_WRITE);
	return (r);
}

LONG
xRemDosEntry(db, dl)
DB db;
struct DosList *dl;
{
	register struct DosList *p;

	for (p = xLockDosList(db, LDF_WRITE); p->dol_Next != NULL;
	    p = (struct DosList *)p->dol_Next)
		if ((struct DosList *)p->dol_Next == dl) {
			p->dol_Next = dl->dol_Next;
			break;
		}
	xUnLockDosList(db, LDF_WRITE);
	return (DOSTRUE);
}

/* Replace or remove the assign name; a new one is made by mk(name). */
static LONG
dassign(name, type, lock, path)
char *name;
LONG type;
BPTR lock;
char *path;
{
	register struct DosList *dl, *h;
	char *p;
	LONG r;

	h = xLockDosList(DOSBase, LDF_WRITE);
	if ((dl = xFindDosEntry(DOSBase, h, name, LDF_DEVICES | LDF_VOLUMES))
	    != NULL) {
		xUnLockDosList(DOSBase, LDF_WRITE);
		return (dfail(ERROR_OBJECT_EXISTS));
	}
	if ((dl = xFindDosEntry(DOSBase, h, name, LDF_ASSIGNS)) != NULL)
		xRemDosEntry(DOSBase, dl);
	r = DOSTRUE;
	if (lock != NULL || path != NULL) {
		p = NULL;
		if (path != NULL &&
		    (p = (char *)AllocVec((ULONG)dstrlen(path) + 1, MEMF_PUBLIC))
		    != NULL)
			CopyMem(path, p, (ULONG)dstrlen(path) + 1);
		if ((h = xMakeDosEntry(DOSBase, name, type)) == NULL ||
		    (path != NULL && p == NULL)) {
			FreeVec(h);
			r = dfail(ERROR_NO_FREE_STORE);
		} else {
			h->dol_Lock = lock;
			if (lock != NULL)
				h->dol_Task = ((struct FileLock *)lock)->fl_Task;
			h->dol_misc.dol_assign.dol_AssignName = (UBYTE *)p;
			xAddDosEntry(DOSBase, h);
		}
	}
	xUnLockDosList(DOSBase, LDF_WRITE);
	if (dl != NULL) {
		xUnLock(DOSBase, dl->dol_Lock);
		dfreeal(dl->dol_misc.dol_assign.dol_List);
		FreeVec(dl->dol_misc.dol_assign.dol_AssignName);
		xFreeDosEntry(DOSBase, dl);
	}
	return (r);
}

/* The assign owns lock from here; NULL removes the assign. */
LONG
xAssignLock(db, name, lock)
DB db;
char *name;
BPTR lock;
{
	return (dassign(name, (LONG)DLT_DIRECTORY, lock, (char *)NULL));
}

LONG
xAssignLate(db, name, path)
DB db;
char *name, *path;
{
	return (dassign(name, (LONG)DLT_LATE, (BPTR)NULL, path));
}

/* Start dl's handler: a process on its seglist, which without one is
 * loaded from the file dol_Handler names and kept, then ACTION_STARTUP
 * (name, dol_Startup, the entry); the handler sets dol_Task. */
static struct MsgPort *
dstart(dl)
register struct DosList *dl;
{
	struct TagItem tags[8];
	register struct TagItem *t;
	register struct Process *p;

	if (dl->dol_misc.dol_handler.dol_SegList == NULL &&
	    (dl->dol_misc.dol_handler.dol_SegList = xLoadSeg(DOSBase,
	    (char *)dl->dol_misc.dol_handler.dol_Handler)) == NULL)
		return (NULL);
	t = dtag(tags, NP_Seglist, (ULONG)dl->dol_misc.dol_handler.dol_SegList);
	t = dtag(t, NP_FreeSeglist, 0L);
	t = dtag(t, NP_StackSize, (ULONG)dl->dol_misc.dol_handler.dol_StackSize);
	t = dtag(t, NP_Priority, (ULONG)dl->dol_misc.dol_handler.dol_Priority);
	t = dtag(t, NP_Name, (ULONG)dl->dol_misc.dol_handler.dol_Handler);
	t = dtag(t, NP_CurrentDir, 0L);
	dtag(t, TAG_DONE, 0L);
	if ((p = xCreateNewProc(DOSBase, tags)) == NULL)
		return (NULL);
	if (xDoPkt(DOSBase, &p->pr_MsgPort, (LONG)ACTION_STARTUP,
	    (LONG)dl->dol_Name, (LONG)dl->dol_misc.dol_handler.dol_Startup,
	    (LONG)dl, 0L, 0L) == DOSFALSE)
		return (NULL);
	return (dl->dol_Task);
}

/* The process's default filesystem: pr_FileSystemTask, else SYS:'s. */
static struct MsgPort *
dfstask()
{
	register struct Process *me;
	register struct DevProc *dvp;
	struct MsgPort *port;

	me = dme();
	if (me != NULL && me->pr_FileSystemTask != NULL)
		return ((struct MsgPort *)me->pr_FileSystemTask);
	if ((dvp = xGetDevProc(DOSBase, "SYS:", (struct DevProc *)NULL)) == NULL)
		return (NULL);
	port = dvp->dvp_Port;
	if (me != NULL)
		me->pr_FileSystemTask = (APTR)port;
	xFreeDevProc(DOSBase, dvp);
	return (port);
}

/* After odp, the next directory of its multi-directory assign: odp
 * again, or NULL (odp freed) with IoErr ERROR_NO_MORE_ENTRIES. */
static struct DevProc *
dnextdvp(odp)
register struct DevProc *odp;
{
	register struct DosList *dl;
	register struct AssignList *al;
	BPTR lk;

	lk = NULL;
	dl = xLockDosList(DOSBase, LDF_READ);
	if (odp->dvp_Flags & DVPF_ASSIGN) {
		while ((dl = (struct DosList *)dl->dol_Next) != NULL &&
		    dl != odp->dvp_DevNode)
			;
		if (dl != NULL) {
			al = dl->dol_misc.dol_assign.dol_List;
			if (odp->dvp_Lock != dl->dol_Lock) {
				while (al != NULL && al->al_Lock != odp->dvp_Lock)
					al = al->al_Next;
				if (al != NULL)
					al = al->al_Next;
			}
			if (al != NULL)
				lk = al->al_Lock;
		}
	}
	xUnLockDosList(DOSBase, LDF_READ);
	if (lk == NULL) {
		xFreeDevProc(DOSBase, odp);
		dseterr(ERROR_NO_MORE_ENTRIES);
		return (NULL);
	}
	odp->dvp_Lock = lk;
	odp->dvp_Port = ((struct FileLock *)lk)->fl_Task;
	return (odp);
}

/* A name without a device part is relative to the current directory (a
 * NULL one is the default filesystem's root); ":x" is the root of the
 * current directory's volume.  An assign's lock stays the assign's. */
struct DevProc *
xGetDevProc(db, name, odp)
DB db;
char *name;
struct DevProc *odp;
{
	register struct DevProc *dvp;
	register struct DosList *dl;
	register struct DosInfo *di;
	struct Process *me;
	struct MsgPort *port;
	char *c, *path;
	BPTR lk;

	if (odp != NULL)
		return (dnextdvp(odp));
	if ((dvp = (struct DevProc *)AllocVec((ULONG)sizeof(*dvp),
	    MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
		dseterr(ERROR_NO_FREE_STORE);
		return (NULL);
	}
	c = dcolon(name);
	if (c == NULL || c == name) {
		me = dme();
		lk = me != NULL ? me->pr_CurrentDir : NULL;
		if (lk != NULL)
			dvp->dvp_Port = ((struct FileLock *)lk)->fl_Task;
		else
			dvp->dvp_Port = dfstask();
		if (c == NULL)
			dvp->dvp_Lock = lk;
		if (dvp->dvp_Port != NULL)
			return (dvp);
		FreeVec(dvp);
		dseterr(ERROR_DEVICE_NOT_MOUNTED);
		return (NULL);
	}
	di = ddi();
	for (;;) {
		dl = xFindDosEntry(db, xLockDosList(db, LDF_READ), name, LDF_ALL);
		if (dl == NULL)
			break;
		port = dl->dol_Task;
		if (dl->dol_Type == DLT_DEVICE && port == NULL &&
		    (dl->dol_misc.dol_handler.dol_SegList != NULL ||
		    dl->dol_misc.dol_handler.dol_Handler != NULL)) {
			xUnLockDosList(db, LDF_READ);
			ObtainSemaphore(&di->di_EntryLock);
			if (dl->dol_Task == NULL)
				dstart(dl);
			ReleaseSemaphore(&di->di_EntryLock);
			if (dl->dol_Task == NULL) {
				xLockDosList(db, LDF_READ);
				dl = NULL;
				break;
			}
			continue;
		}
		if (dl->dol_Type == DLT_LATE) {
			path = (char *)dl->dol_misc.dol_assign.dol_AssignName;
			xUnLockDosList(db, LDF_READ);
			if ((lk = xLock(db, path, SHARED_LOCK)) == NULL) {
				FreeVec(dvp);
				return (NULL);
			}
			xLockDosList(db, LDF_WRITE);
			if (dl->dol_Type == DLT_LATE) {
				dl->dol_Type = DLT_DIRECTORY;
				dl->dol_Lock = lk;
				dl->dol_Task = ((struct FileLock *)lk)->fl_Task;
				lk = NULL;
			}
			xUnLockDosList(db, LDF_WRITE);
			xUnLock(db, lk);
			continue;
		}
		dvp->dvp_Port = port;
		dvp->dvp_DevNode = dl;
		if (dl->dol_Type == DLT_DIRECTORY) {
			dvp->dvp_Lock = dl->dol_Lock;
			dvp->dvp_Flags = DVPF_ASSIGN;
		}
		break;
	}
	xUnLockDosList(db, LDF_READ);
	if (dl == NULL || dvp->dvp_Port == NULL) {
		FreeVec(dvp);
		dseterr(ERROR_DEVICE_NOT_MOUNTED);
		return (NULL);
	}
	return (dvp);
}

void
xFreeDevProc(db, dvp)
DB db;
struct DevProc *dvp;
{
	FreeVec(dvp);
}

/* The port; IoErr holds an assign's lock, as on the Amiga. */
struct MsgPort *
xDeviceProc(db, name)
DB db;
char *name;
{
	register struct DevProc *dvp;
	struct MsgPort *port;

	if ((dvp = xGetDevProc(db, name, (struct DevProc *)NULL)) == NULL)
		return (NULL);
	port = dvp->dvp_Port;
	if (dvp->dvp_Lock != NULL)
		dseterr((LONG)dvp->dvp_Lock);
	xFreeDevProc(db, dvp);
	return (port);
}

LONG
xIsFileSystem(db, name)
DB db;
char *name;
{
	register struct DevProc *dvp;
	LONG r;

	if ((dvp = xGetDevProc(db, name, (struct DevProc *)NULL)) == NULL)
		return (DOSFALSE);
	r = xDoPkt(db, dvp->dvp_Port, (LONG)ACTION_IS_FILESYSTEM, 0L, 0L, 0L,
	    0L, 0L);
	xFreeDevProc(db, dvp);
	return (r);
}
