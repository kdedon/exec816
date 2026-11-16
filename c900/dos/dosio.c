/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * dos.library files and locks: each call resolves the name's device
 * with GetDeviceProc and sends the AmigaDOS packet to its handler.  The
 * handler gets the lock to start from (the current directory, an
 * assign's, or NULL for its root) and the name after the device part.
 * Read, Write and Seek are one packet each, after settling what the
 * buffered calls (dosbuf.c) left in the handle's buffer, so a C
 * library's read/write/lseek map onto Read/Write/Seek directly.
 *
 * "NIL:" is a handle with no handler (reads end at once, writes vanish)
 * and "*" the process's console, as on the Amiga.
 */
#include "dosint.h"

struct MsgPort *xDeviceProc();
LONG xIoErr();

#define PKT(port, a, x1, x2, x3) \
	xDoPkt(DOSBase, port, (LONG)(a), (LONG)(x1), (LONG)(x2), (LONG)(x3), \
	    0L, 0L)

/* After r from one directory of an assign, the error to try its next
 * directory on (not found), else 0. */
LONG
dnextdir(dvp, r)
struct DevProc *dvp;
LONG r;
{
	LONG e;

	e = xIoErr(DOSBase);
	return (r == DOSFALSE && (dvp->dvp_Flags & DVPF_ASSIGN) &&
	    (e == ERROR_OBJECT_NOT_FOUND || e == ERROR_DIR_NOT_FOUND) ? e : 0L);
}

/* The end of the directories: IoErr becomes the last one's error e. */
void
dlastdir(e)
LONG e;
{
	if (e != 0 && xIoErr(DOSBase) == ERROR_NO_MORE_ENTRIES)
		dseterr(e);
}

/* Send a packet with (lock, rest, a3) for name, or with pre set (0,
 * lock, rest, a3), trying each directory of a multi-directory assign
 * while the name is not found; DOSFALSE when it does not resolve. */
LONG
dnamepkt(name, action, a3, pre)
char *name;
LONG action, a3;
int pre;
{
	register struct DevProc *dvp;
	char *rest, *c;
	LONG r, e;

	c = dcolon(name);
	rest = c != NULL ? c + 1 : name;
	r = e = 0;
	for (dvp = NULL;
	    (dvp = xGetDevProc(DOSBase, name, dvp)) != NULL; ) {
		if (pre)
			r = xDoPkt(DOSBase, dvp->dvp_Port, action, 0L,
			    (LONG)dvp->dvp_Lock, (LONG)rest, a3, 0L);
		else
			r = PKT(dvp->dvp_Port, action, dvp->dvp_Lock, rest, a3);
		if ((e = dnextdir(dvp, r)) == 0) {
			xFreeDevProc(DOSBase, dvp);
			return (r);
		}
	}
	dlastdir(e);
	return (r);
}

BPTR
xOpen(db, name, mode)
DB db;
char *name;
LONG mode;
{
	register struct FileHandle *fh;
	register struct DevProc *dvp;
	struct MsgPort *port;
	struct Process *me;
	char *rest, *c;
	LONG act, r, e;

	if ((fh = (struct FileHandle *)xAllocDosObj(db, (ULONG)DOS_FILEHANDLE,
	    NULL)) == NULL)
		return (NULL);
	if (dnamecmp(name, "NIL", 4) == 0 && name[3] == ':') {
		((struct DFH *)fh)->f_Mode = BUF_FULL;
		return ((BPTR)fh);
	}
	act = mode == MODE_OLDFILE ? ACTION_FINDINPUT :
	    mode == MODE_NEWFILE ? ACTION_FINDOUTPUT : ACTION_FINDUPDATE;
	dvp = NULL;
	if (name[0] == '*' && name[1] == '\0') {
		me = dme();
		port = me != NULL ? (struct MsgPort *)me->pr_ConsoleTask : NULL;
		fh->fh_Type = port;
		r = PKT(port, act, fh, 0, name);
	} else {
		c = dcolon(name);
		rest = c != NULL ? c + 1 : name;
		r = e = 0;
		while ((dvp = xGetDevProc(db, name, dvp)) != NULL) {
			port = fh->fh_Type = dvp->dvp_Port;
			r = PKT(port, act, fh, dvp->dvp_Lock, rest);
			if ((e = dnextdir(dvp, r)) == 0)
				break;
		}
		if (dvp == NULL)
			dlastdir(e);
		else
			xFreeDevProc(db, dvp);
	}
	if (r == DOSFALSE) {
		xFreeDosObj(db, (ULONG)DOS_FILEHANDLE, fh);
		return (NULL);
	}
	if (fh->fh_Port == NULL)
		((struct DFH *)fh)->f_Mode = BUF_FULL;
	else if ((me = dme()) != NULL && me->pr_ConsoleTask == NULL)
		dconlink((struct DProc *)me, (struct DFH *)fh);	/* first console */
	return ((BPTR)fh);
}

/* Make f, an interactive open, d's console, or with f NULL forget d's
 * console open.  The link is two-way so that whichever of f's Close and
 * d's end comes first breaks it; Forbid, as the Close may be another
 * process's. */
void
dconlink(d, f)
register struct DProc *d;
register struct DFH *f;
{
	Forbid();
	if (d->dp_ConFH != NULL)
		d->dp_ConFH->f_Con = NULL;
	if ((d->dp_ConFH = f) != NULL) {
		f->f_Con = d;
		d->dp_Proc.pr_ConsoleTask = (APTR)f->f_FH.fh_Type;
	}
	Permit();
}

LONG
xClose(db, file)
DB db;
BPTR file;
{
	register struct DFH *f;
	register struct DProc *d;
	LONG r;

	if ((f = (struct DFH *)file) == NULL)
		return (DOSTRUE);
	r = dflush(f);
	if (f->f_Flags & DFF_OWNBUF)
		FreeVec((APTR)f->f_FH.fh_Buf);
	/* The console this open set ends with it, unless since changed. */
	Forbid();
	if ((d = f->f_Con) != NULL) {
		if (d->dp_Proc.pr_ConsoleTask == (APTR)f->f_FH.fh_Type)
			d->dp_Proc.pr_ConsoleTask = NULL;
		d->dp_ConFH = NULL;
	}
	Permit();
	if (f->f_FH.fh_Type != NULL &&
	    PKT(f->f_FH.fh_Type, ACTION_END, f->f_FH.fh_Args, 0, 0) == DOSFALSE)
		r = DOSFALSE;
	xFreeDosObj(db, (ULONG)DOS_FILEHANDLE, f);
	return (r);
}

/* An unbuffered ACTION_READ or ACTION_WRITE of len bytes: NIL: (no
 * handler) reads nothing and takes every write. */
LONG
dfhio(f, act, buf, len)
register struct DFH *f;
LONG act;
APTR buf;
LONG len;
{
	f->f_Flags &= ~(DFF_EOF | DFF_UNEOF);
	if (f->f_FH.fh_Type == NULL)
		return (act == ACTION_READ ? 0L : len);
	return (PKT(f->f_FH.fh_Type, act, f->f_FH.fh_Args, buf, len));
}

/* The bytes read, 0 at the end, -1 on an error.  Read-ahead a buffered
 * call left comes first; an interactive handle returns just that, as a
 * console returns one line. */
LONG
xRead(db, file, buf, len)
DB db;
BPTR file;
APTR buf;
LONG len;
{
	register struct DFH *f;
	register LONG n, r;

	if ((f = (struct DFH *)file) == NULL)
		return (-1);
	n = 0;
	if (f->f_Flags & DFF_WRITE) {
		if (dflush(f) == DOSFALSE)
			return (-1);
	} else if ((n = f->f_FH.fh_End - f->f_FH.fh_Pos) > 0) {
		if (n > len)
			n = len;
		CopyMem((char *)f->f_FH.fh_Buf + (UWORD)f->f_FH.fh_Pos, buf,
		    (ULONG)n);
		f->f_FH.fh_Pos += n;
		if (n == len || f->f_FH.fh_Port != NULL)
			return (n);
		buf = (APTR)((char *)buf + (UWORD)n);
		len -= n;
	} else
		n = 0;
	r = dfhio(f, (LONG)ACTION_READ, buf, len);
	return (r < 0 ? (n > 0 ? n : r) : n + r);
}

/* Pending buffered output goes first; a file's read-ahead is given back
 * (a console's input and output are separate streams). */
LONG
xWrite(db, file, buf, len)
DB db;
BPTR file;
APTR buf;
LONG len;
{
	register struct DFH *f;

	if ((f = (struct DFH *)file) == NULL)
		return (-1);
	if (((f->f_Flags & DFF_WRITE) || f->f_FH.fh_Port == NULL) &&
	    dflush(f) == DOSFALSE)
		return (-1);
	return (dfhio(f, (LONG)ACTION_WRITE, buf, len));
}

/* The old position, or -1: the logical one, less unread read-ahead. */
LONG
xSeek(db, file, pos, mode)
DB db;
BPTR file;
LONG pos, mode;
{
	register struct DFH *f;
	register LONG adj, r;

	f = (struct DFH *)file;
	adj = 0;
	if (f->f_Flags & DFF_WRITE) {
		if (dflush(f) == DOSFALSE)
			return (-1);
	} else if ((adj = f->f_FH.fh_End - f->f_FH.fh_Pos) > 0) {
		f->f_FH.fh_Pos = f->f_FH.fh_End = 0;
		if (mode == OFFSET_CURRENT)
			pos -= adj;
	} else
		adj = 0;
	if (f->f_FH.fh_Type == NULL)
		return (0);
	r = PKT(f->f_FH.fh_Type, ACTION_SEEK, f->f_FH.fh_Args, pos, mode);
	return (r == -1 ? r : r - adj);
}

LONG
xIsInteractive(db, file)
DB db;
BPTR file;
{
	return (((struct FileHandle *)file)->fh_Port != NULL ? DOSTRUE :
	    DOSFALSE);
}

/* Cooked (0) or raw (1) console input. */
LONG
xSetMode(db, file, mode)
DB db;
BPTR file;
LONG mode;
{
	register struct FileHandle *fh;

	fh = (struct FileHandle *)file;
	return (PKT(fh->fh_Type, ACTION_SCREEN_MODE, mode, 0, 0));
}

LONG
xWaitForChar(db, file, timeout)
DB db;
BPTR file;
LONG timeout;
{
	register struct FileHandle *fh;

	fh = (struct FileHandle *)file;
	return (PKT(fh->fh_Type, ACTION_WAIT_CHAR, timeout, 0, 0));
}

LONG
xExamineFH(db, file, fib)
DB db;
BPTR file;
struct FileInfoBlock *fib;
{
	register struct FileHandle *fh;

	fh = (struct FileHandle *)file;
	return (PKT(fh->fh_Type, ACTION_EXAMINE_FH, fh->fh_Args, fib, 0));
}

BPTR
xLock(db, name, mode)
DB db;
char *name;
LONG mode;
{
	return ((BPTR)dnamepkt(name, (LONG)ACTION_LOCATE_OBJECT, mode, 0));
}

/* The handler of lock, or the default filesystem's for NULL (its root). */
struct MsgPort *
dlport(lock)
BPTR lock;
{
	if (lock != NULL)
		return (((struct FileLock *)lock)->fl_Task);
	return (xDeviceProc(DOSBase, ":"));
}

void
xUnLock(db, lock)
DB db;
BPTR lock;
{
	if (lock != NULL)
		PKT(dlport(lock), ACTION_FREE_LOCK, lock, 0, 0);
}

BPTR
xDupLock(db, lock)
DB db;
BPTR lock;
{
	if (lock == NULL)
		return (NULL);
	return ((BPTR)PKT(dlport(lock), ACTION_COPY_DIR, lock, 0, 0));
}

BPTR
xParentDir(db, lock)
DB db;
BPTR lock;
{
	return ((BPTR)PKT(dlport(lock), ACTION_PARENT, lock, 0, 0));
}

LONG
xExamine(db, lock, fib)
DB db;
BPTR lock;
struct FileInfoBlock *fib;
{
	return (PKT(dlport(lock), ACTION_EXAMINE_OBJECT, lock, fib, 0));
}

LONG
xExNext(db, lock, fib)
DB db;
BPTR lock;
struct FileInfoBlock *fib;
{
	return (PKT(dlport(lock), ACTION_EXAMINE_NEXT, lock, fib, 0));
}

LONG
xInfo(db, lock, id)
DB db;
BPTR lock;
struct InfoData *id;
{
	return (PKT(dlport(lock), ACTION_INFO, lock, id, 0));
}

BPTR
xCurrentDir(db, lock)
DB db;
BPTR lock;
{
	register struct Process *p;
	BPTR o;

	p = dme();
	o = p->pr_CurrentDir;
	p->pr_CurrentDir = lock;
	return (o);
}

/* Lock name in directory dir (NULL: the default filesystem's root). */
BPTR
dlockin(dir, name)
BPTR dir;
char *name;
{
	return ((BPTR)PKT(dlport(dir), ACTION_LOCATE_OBJECT, dir, name,
	    SHARED_LOCK));
}

BPTR
xCreateDir(db, name)
DB db;
char *name;
{
	return ((BPTR)dnamepkt(name, (LONG)ACTION_CREATE_DIR, 0L, 0));
}

LONG
xDeleteFile(db, name)
DB db;
char *name;
{
	return (dnamepkt(name, (LONG)ACTION_DELETE_OBJECT, 0L, 0));
}

/* Both names must resolve to the same handler. */
LONG
xRename(db, from, to)
DB db;
char *from, *to;
{
	register struct DevProc *d1, *d2;
	char *c;
	LONG r;

	if ((d1 = xGetDevProc(db, from, (struct DevProc *)NULL)) == NULL)
		return (DOSFALSE);
	r = DOSFALSE;
	if ((d2 = xGetDevProc(db, to, (struct DevProc *)NULL)) != NULL) {
		if (d1->dvp_Port != d2->dvp_Port)
			dseterr(ERROR_RENAME_ACROSS_DEVICES);
		else {
			if ((c = dcolon(from)) != NULL)
				from = c + 1;
			if ((c = dcolon(to)) != NULL)
				to = c + 1;
			r = xDoPkt(db, d1->dvp_Port, (LONG)ACTION_RENAME_OBJECT,
			    (LONG)d1->dvp_Lock, (LONG)from, (LONG)d2->dvp_Lock,
			    (LONG)to, 0L);
		}
		xFreeDevProc(db, d2);
	}
	xFreeDevProc(db, d1);
	return (r);
}

/* The absolute name of lock, "Volume:dir/name", built from the leaf up
 * with ParentDir and Examine. */
LONG
xNameFromLock(db, lock, buf, len)
DB db;
BPTR lock;
char *buf;
LONG len;
{
	register struct FileInfoBlock *fib;
	register char *p, *s;
	register int n;
	BPTR l, up;
	int first;
	LONG r;

	if (len < 2 || (fib = (struct FileInfoBlock *)xAllocDosObj(db,
	    (ULONG)DOS_FIB, NULL)) == NULL)
		return (dfail(ERROR_LINE_TOO_LONG));
	p = buf + (UWORD)len - 1;
	*p = '\0';
	l = lock != NULL ? xDupLock(db, lock) : xLock(db, ":", SHARED_LOCK);
	r = l != NULL ? DOSTRUE : DOSFALSE;
	for (first = 1; l != NULL; first = 0) {
		if (xExamine(db, l, fib) == DOSFALSE) {
			r = DOSFALSE;
			up = NULL;
		} else {
			up = xParentDir(db, l);
			n = dstrlen(fib->fib_FileName) + (up == NULL || !first);
			if (p - buf < n) {
				r = dfail(ERROR_LINE_TOO_LONG);
				xUnLock(db, up);
				up = NULL;
			} else {
				if (up == NULL)
					*--p = ':';
				else if (!first)
					*--p = '/';
				for (s = fib->fib_FileName; *s != '\0'; s++)
					;
				while (s > fib->fib_FileName)
					*--p = *--s;
			}
		}
		xUnLock(db, l);
		l = up;
	}
	for (s = buf; (*s++ = *p++) != '\0'; )
		;
	xFreeDosObj(db, (ULONG)DOS_FIB, fib);
	return (r);
}
