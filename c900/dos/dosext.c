/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * dos.library, AmigaDOS V36: object attributes (SetProtection,
 * SetComment, SetFileDate, SetFileSize), SameLock, multi-directory
 * assigns (AssignAdd, RemAssignList) and the resident segment list
 * (AddSegment, FindSegment, RemSegment), whose head is di_ResList and
 * whose callers Forbid while they use an entry, as on the Amiga.
 */
#include "dosint.h"

LONG dnamepkt();
LONG xIoErr();
LONG xAssignLock();

LONG
xSetProtection(db, name, mask)
DB db;
char *name;
LONG mask;
{
	return (dnamepkt(name, (LONG)ACTION_SET_PROTECT, mask, 1));
}

LONG
xSetComment(db, name, comment)
DB db;
char *name, *comment;
{
	return (dnamepkt(name, (LONG)ACTION_SET_COMMENT, (LONG)comment, 1));
}

LONG
xSetFileDate(db, name, date)
DB db;
char *name;
struct DateStamp *date;
{
	return (dnamepkt(name, (LONG)ACTION_SET_DATE, (LONG)date, 1));
}

/* The new size, or -1. */
LONG
xSetFileSize(db, file, pos, mode)
DB db;
BPTR file;
LONG pos, mode;
{
	register struct FileHandle *fh;

	fh = (struct FileHandle *)file;
	if (fh->fh_Type == NULL)
		return (dfail(ERROR_ACTION_NOT_KNOWN) - 1);
	return (xDoPkt(db, fh->fh_Type, (LONG)ACTION_SET_FILE_SIZE, fh->fh_Args,
	    pos, mode, 0L, 0L));
}

LONG
xSameLock(db, l1, l2)
DB db;
BPTR l1, l2;
{
	register struct FileLock *a, *b;

	a = (struct FileLock *)l1;
	b = (struct FileLock *)l2;
	if (a == b)
		return (LOCK_SAME);
	if (a == NULL || b == NULL || a->fl_Task != b->fl_Task ||
	    a->fl_Volume != b->fl_Volume)
		return (LOCK_DIFFERENT);
	return (xDoPkt(db, a->fl_Task, (LONG)ACTION_SAME_LOCK, l1, l2, 0L, 0L,
	    0L) != DOSFALSE ? LOCK_SAME : LOCK_SAME_VOLUME);
}

/* Unlock and free an assign's further directories. */
void
dfreeal(al)
register struct AssignList *al;
{
	register struct AssignList *n;

	for (; al != NULL; al = n) {
		n = al->al_Next;
		xUnLock(DOSBase, al->al_Lock);
		FreeVec(al);
	}
}

/* The directory assign name, the DosList locked for writing. */
static struct DosList *
dfindas(name)
char *name;
{
	register struct DosList *dl;

	dl = xFindDosEntry(DOSBase, xLockDosList(DOSBase, LDF_WRITE), name,
	    LDF_ASSIGNS);
	if (dl == NULL || dl->dol_Type != DLT_DIRECTORY) {
		xUnLockDosList(DOSBase, LDF_WRITE);
		dseterr(ERROR_OBJECT_NOT_FOUND);
		return (NULL);
	}
	return (dl);
}

/* Add lock as a further directory of the assign; the assign owns it. */
LONG
xAssignAdd(db, name, lock)
DB db;
char *name;
BPTR lock;
{
	register struct AssignList *al, **pp;
	register struct DosList *dl;

	if ((al = (struct AssignList *)AllocVec((ULONG)sizeof(*al),
	    MEMF_PUBLIC | MEMF_CLEAR)) == NULL)
		return (dfail(ERROR_NO_FREE_STORE));
	if ((dl = dfindas(name)) == NULL) {
		FreeVec(al);
		return (DOSFALSE);
	}
	al->al_Lock = lock;
	for (pp = &dl->dol_misc.dol_assign.dol_List; *pp != NULL;
	    pp = &(*pp)->al_Next)
		;
	*pp = al;
	xUnLockDosList(db, LDF_WRITE);
	return (DOSTRUE);
}

/* Remove the assign's first directory that is lock's object, unlocking
 * the assign's lock; the assign goes when it has none left. */
LONG
xRemAssignList(db, name, lock)
DB db;
char *name;
BPTR lock;
{
	register struct DosList *dl;
	register struct AssignList *al, **pp;
	BPTR old;

	if ((dl = dfindas(name)) == NULL)
		return (DOSFALSE);
	pp = &dl->dol_misc.dol_assign.dol_List;
	old = NULL;
	if (xSameLock(db, dl->dol_Lock, lock) == LOCK_SAME) {
		old = dl->dol_Lock;
		if ((al = *pp) == NULL) {
			xUnLockDosList(db, LDF_WRITE);
			return (xAssignLock(db, name, (BPTR)NULL));
		}
		dl->dol_Lock = al->al_Lock;
		dl->dol_Task = ((struct FileLock *)al->al_Lock)->fl_Task;
		*pp = al->al_Next;
		FreeVec(al);
	} else
		for (; (al = *pp) != NULL; pp = &al->al_Next)
			if (xSameLock(db, al->al_Lock, lock) == LOCK_SAME) {
				old = al->al_Lock;
				*pp = al->al_Next;
				FreeVec(al);
				break;
			}
	xUnLockDosList(db, LDF_WRITE);
	if (old == NULL)
		return (dfail(ERROR_OBJECT_NOT_FOUND));
	xUnLock(db, old);
	return (DOSTRUE);
}

#define RESLIST	(*(struct Segment **)&ddi()->di_ResList)

/* system is the entry's seg_UC: 0, CMD_SYSTEM or CMD_INTERNAL. */
LONG
xAddSegment(db, name, seg, system)
DB db;
char *name;
BPTR seg;
LONG system;
{
	register struct Segment *s;
	register LONG n;

	n = dstrlen(name) + 1;
	if ((s = (struct Segment *)AllocVec((ULONG)sizeof(*s) + n,
	    MEMF_PUBLIC)) == NULL)
		return (dfail(ERROR_NO_FREE_STORE));
	CopyMem(name, s->seg_Name, (ULONG)n);
	s->seg_UC = system;
	s->seg_Seg = seg;
	Forbid();
	s->seg_Next = (BPTR)RESLIST;
	RESLIST = s;
	Permit();
	return (DOSTRUE);
}

/* The next entry after start (NULL: the first) named name, a system one
 * (seg_UC < 0) if system is set, else a user one. */
struct Segment *
xFindSegment(db, name, start, system)
DB db;
char *name;
register struct Segment *start;
LONG system;
{
	Forbid();
	start = start == NULL ? RESLIST : (struct Segment *)start->seg_Next;
	for (; start != NULL; start = (struct Segment *)start->seg_Next)
		if ((start->seg_UC < 0) == (system != 0) &&
		    dnamecmp(name, (char *)start->seg_Name, 255) == 0)
			break;
	Permit();
	return (start);
}

/* Remove and unload an unused user entry. */
LONG
xRemSegment(db, seg)
DB db;
struct Segment *seg;
{
	register struct Segment **pp;

	Forbid();
	for (pp = &RESLIST; *pp != NULL; pp = (struct Segment **)&(*pp)->seg_Next)
		if (*pp == seg && seg->seg_UC == 0) {
			*pp = (struct Segment *)seg->seg_Next;
			Permit();
			xUnLoadSeg(db, seg->seg_Seg);
			FreeVec(seg);
			return (DOSTRUE);
		}
	Permit();
	return (dfail(ERROR_OBJECT_IN_USE));
}

/* A copy of a command path (cli_CommandDir), for a new CLI. */
BPTR
dpathdup(p)
register struct PathNode *p;
{
	struct PathNode *h;
	register struct PathNode **pp, *n;

	h = NULL;
	for (pp = &h; p != NULL; p = (struct PathNode *)p->pn_Next) {
		if ((n = (struct PathNode *)AllocVec((ULONG)sizeof(*n),
		    MEMF_PUBLIC | MEMF_CLEAR)) == NULL)
			break;
		n->pn_Lock = xDupLock(DOSBase, p->pn_Lock);
		*pp = n;
		pp = (struct PathNode **)&n->pn_Next;
	}
	return ((BPTR)h);
}

void
dpathfree(p)
register struct PathNode *p;
{
	register struct PathNode *n;

	for (; p != NULL; p = n) {
		n = (struct PathNode *)p->pn_Next;
		xUnLock(DOSBase, p->pn_Lock);
		FreeVec(p);
	}
}
