/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * RAM:, AmigaDOS's ram-handler: files and directories in memory on the
 * volume "Ram Disk".  A boot module: its romtag lists the device RAM,
 * and DOS starts the handler on first use; it then lists the volume.
 *
 * A node is a file or directory with its name (at most 30 bytes, kept
 * as given, looked up ignoring ASCII case), protection, date and
 * comment.  A directory's entries are a list in creation order.  A
 * file's bytes are a chain of MEMF_ANY chunks holding them in order;
 * each new chunk is about the size of the file so far (64 to 8,192
 * bytes), so small files stay small and large ones take few chunks.
 * Deleting or truncating a file frees its chunks.
 *
 * A lock's fl_Key is its node.  An exclusive lock or MODE_NEWFILE handle
 * excludes every other use of the node; an object in use cannot be
 * deleted.  The R, W and D protection bits are enforced on reads,
 * writes and deletes.  fib_DiskKey is the entry ExNext returned last,
 * as FFS's header block is: deleting that entry ends the scan.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

#define NAMEMAX		30
#define COMMAX		79
#define CMIN		64L
#define CMAX		8192L

struct rchunk {
	struct rchunk *c_Next;
	ULONG c_Size;			/* bytes of c_Data */
	UBYTE c_Data[4];
};
#define CHDR	(sizeof(struct rchunk) - 4)

struct rnode {
	struct rnode *n_Next;		/* in the directory */
	struct rnode *n_Parent;
	struct rnode *n_List;		/* a directory's entries */
	struct rchunk *n_Data;		/* a file's bytes */
	LONG n_Type;
	LONG n_Prot;
	ULONG n_Size;
	struct DateStamp n_Date;
	char *n_Comment;
	UWORD n_Use, n_Excl;
	char n_Name[NAMEMAX + 1];
};

struct rfile {
	struct rfile *f_Next;
	struct rnode *f_Node;
	ULONG f_Pos;
	int f_Excl, f_Dirty;
};

static struct rnode root;
static struct rfile *files;
static struct DosList *vol;
static struct MsgPort *port;
static ULONG used;			/* bytes allocated */
static int nlocks;
static LONG err;
static char volname[] = "Ram Disk";

static APTR
ralloc(n)
ULONG n;
{
	register APTR p;

	if ((p = AllocMem(n, MEMF_ANY | MEMF_CLEAR)) == NULL)
		err = ERROR_NO_FREE_STORE;
	else
		used += n;
	return (p);
}

static void
rfree(p, n)
APTR p;
ULONG n;
{
	FreeMem(p, n);
	used -= n;
}

static int
slen(s)
register char *s;
{
	register int n;

	for (n = 0; s[n] != '\0'; n++)
		;
	return (n);
}

static int
lower(c)
register int c;
{
	return (c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c);
}

/* The entry of directory d named by the n bytes at name, or NULL. */
static struct rnode *
lookup(d, name, n)
struct rnode *d;
char *name;
register int n;
{
	register struct rnode *e;
	register int i;

	for (e = d->n_List; e != NULL; e = e->n_Next) {
		for (i = 0; i < n && lower(e->n_Name[i]) == lower(name[i]); i++)
			;
		if (i == n && e->n_Name[n] == '\0')
			break;
	}
	return (e);
}

/* Walk the len bytes of path from node d ('/' alone or doubled is the
 * parent): what it names, or NULL with err set. */
static struct rnode *
walk(d, path, len)
register struct rnode *d;
register char *path;
int len;
{
	register char *p, *end;

	end = path + len;
	for (p = path; p < end; p++)
		if (*p == ':')
			path = p + 1;
	while (path < end) {
		if (d->n_Type < 0) {
			err = ERROR_OBJECT_WRONG_TYPE;
			return (NULL);
		}
		if (*path == '/') {
			if ((d = d->n_Parent) == NULL) {
				err = ERROR_OBJECT_NOT_FOUND;
				return (NULL);
			}
			path++;
			continue;
		}
		for (p = path; p < end && *p != '/'; p++)
			;
		if ((d = lookup(d, path, (int)(p - path))) == NULL) {
			err = p == end ? ERROR_OBJECT_NOT_FOUND :
			    ERROR_DIR_NOT_FOUND;
			return (NULL);
		}
		path = p < end ? p + 1 : p;
	}
	return (d);
}

#define LKNODE(l)	((l) != 0 ? \
	(struct rnode *)((struct FileLock *)(l))->fl_Key : &root)

/* The directory path's last part goes in, or NULL with err set; *name
 * and *n are that part. */
static struct rnode *
parent(lock, path, name, n)
LONG lock;
char *path, **name;
int *n;
{
	register char *p, *s;
	register struct rnode *d;

	for (p = s = path; *p != '\0'; p++)
		if (*p == ':' || *p == '/')
			s = p + 1;
	*name = s;
	*n = p - s;
	if (*n == 0 || *n > NAMEMAX) {
		err = *n == 0 ? ERROR_OBJECT_WRONG_TYPE :
		    ERROR_INVALID_COMPONENT_NAME;
		return (NULL);
	}
	if ((d = walk(LKNODE(lock), path, (int)(s - path))) != NULL &&
	    d->n_Type < 0) {
		err = ERROR_OBJECT_WRONG_TYPE;
		d = NULL;
	}
	return (d);
}

static void
setname(e, name, n)
register struct rnode *e;
register char *name;
register int n;
{
	register int i;

	for (i = 0; i < n; i++)
		e->n_Name[i] = name[i];
	e->n_Name[n] = '\0';
}

/* Put e last in directory d. */
static void
link(d, e)
struct rnode *d, *e;
{
	register struct rnode **pp;

	for (pp = &d->n_List; *pp != NULL; pp = &(*pp)->n_Next)
		;
	*pp = e;
	e->n_Next = NULL;
	e->n_Parent = d;
	DateStamp(&d->n_Date);
}

static void
unlink(e)
struct rnode *e;
{
	register struct rnode **pp;

	for (pp = &e->n_Parent->n_List; *pp != e; pp = &(*pp)->n_Next)
		;
	*pp = e->n_Next;
	DateStamp(&e->n_Parent->n_Date);
}

/* A new entry of directory d of the given type, or NULL with err set. */
static struct rnode *
create(d, name, n, type)
struct rnode *d;
char *name;
int n;
LONG type;
{
	register struct rnode *e;

	if (lookup(d, name, n) != NULL) {
		err = ERROR_OBJECT_EXISTS;
		return (NULL);
	}
	if ((e = (struct rnode *)ralloc((ULONG)sizeof(*e))) == NULL)
		return (NULL);
	e->n_Type = type;
	setname(e, name, n);
	DateStamp(&e->n_Date);
	link(d, e);
	return (e);
}

/* May node e be used, exclusively if excl is set? */
static int
canuse(e, excl)
register struct rnode *e;
int excl;
{
	if (excl ? e->n_Use != 0 : e->n_Excl != 0) {
		err = ERROR_OBJECT_IN_USE;
		return (0);
	}
	e->n_Use++;
	if (excl)
		e->n_Excl++;
	return (1);
}

static void
unuse(e, excl)
register struct rnode *e;
int excl;
{
	e->n_Use--;
	if (excl)
		e->n_Excl--;
}

static struct FileLock *
newlock(e, mode)
struct rnode *e;
LONG mode;
{
	register struct FileLock *l;
	int excl;

	excl = mode == EXCLUSIVE_LOCK;
	if (e == NULL || !canuse(e, excl))
		return (NULL);
	if ((l = (struct FileLock *)ralloc((ULONG)sizeof(*l))) == NULL) {
		unuse(e, excl);
		return (NULL);
	}
	l->fl_Key = (LONG)e;
	l->fl_Access = excl ? EXCLUSIVE_LOCK : SHARED_LOCK;
	l->fl_Task = port;
	l->fl_Volume = (BPTR)vol;
	nlocks++;
	return (l);
}

static void
freelock(l)
register struct FileLock *l;
{
	unuse((struct rnode *)l->fl_Key, l->fl_Access == EXCLUSIVE_LOCK);
	rfree(l, (ULONG)sizeof(*l));
	nlocks--;
}

/* The link holding the chunk with byte pos of e, *off into it; past
 * the chunks, the empty link at the end. */
static struct rchunk **
cseek(e, pos, off)
struct rnode *e;
ULONG pos, *off;
{
	register struct rchunk **pp;

	for (pp = &e->n_Data; *pp != NULL && pos >= (*pp)->c_Size;
	    pp = &(*pp)->c_Next)
		pos -= (*pp)->c_Size;
	*off = pos;
	return (pp);
}

static void
cfree(c)
register struct rchunk *c;
{
	register struct rchunk *n;

	for (; c != NULL; c = n) {
		n = c->c_Next;
		rfree(c, CHDR + c->c_Size);
	}
}

/* Cut e to size bytes, freeing the chunks past them. */
static void
trunc(e, size)
register struct rnode *e;
ULONG size;
{
	register struct rchunk **pp;
	ULONG off;

	pp = cseek(e, size, &off);
	if (off != 0)
		pp = &(*pp)->c_Next;
	cfree(*pp);
	*pp = NULL;
	e->n_Size = size;
}

/* Copy len bytes between buf and e at pos (wr: into e, growing it; buf
 * NULL writes zeros): the bytes moved, fewer when memory runs out. */
static LONG
rw(e, pos, buf, len, wr)
register struct rnode *e;
ULONG pos;
char *buf;
LONG len;
int wr;
{
	register struct rchunk **pp, *c;
	register ULONG k;
	register UBYTE *p;
	register UWORD i;
	ULONG off;
	LONG n;

	pp = cseek(e, pos, &off);
	for (n = 0; n < len; n += k) {
		if ((c = *pp) == NULL) {
			k = e->n_Size > len - n ? e->n_Size : len - n;
			k = k < CMIN ? CMIN : k > CMAX ? CMAX : (k + 15) & ~15L;
			if ((c = (struct rchunk *)ralloc(CHDR + k)) == NULL)
				break;
			c->c_Size = k;
			*pp = c;
		}
		k = c->c_Size - off;
		if (k > len - n)
			k = len - n;
		p = c->c_Data + (UWORD)off;
		if (buf == NULL)
			for (i = 0; i < (UWORD)k; i++)
				p[i] = 0;
		else {
			if (wr)
				CopyMem(buf, p, k);
			else
				CopyMem(p, buf, k);
			buf = (char *)PtrStep(buf, (LONG)k);
		}
		off += k;
		if (off == c->c_Size) {
			pp = &c->c_Next;
			off = 0;
		}
		if (wr && pos + n + k > e->n_Size)
			e->n_Size = pos + n + k;
	}
	return (n);
}

/* Open (act FINDINPUT), create or truncate (FINDOUTPUT), or open or
 * create (FINDUPDATE) the file path names from lock. */
static struct rfile *
fopen(act, lock, path)
LONG act, lock;
char *path;
{
	register struct rnode *e;
	register struct rfile *f;
	struct rnode *d;
	char *name;
	int n, excl;

	excl = act == ACTION_FINDOUTPUT;
	e = walk(LKNODE(lock), path, slen(path));
	if (e == NULL && act != ACTION_FINDINPUT && err == ERROR_OBJECT_NOT_FOUND
	    && (d = parent(lock, path, &name, &n)) != NULL)
		e = create(d, name, n, (LONG)ST_FILE);
	if (e == NULL)
		return (NULL);
	if (e->n_Type != ST_FILE) {
		err = ERROR_OBJECT_WRONG_TYPE;
		return (NULL);
	}
	if (excl && (e->n_Prot & FIBF_WRITE)) {
		err = ERROR_WRITE_PROTECTED;
		return (NULL);
	}
	if ((f = (struct rfile *)ralloc((ULONG)sizeof(*f))) == NULL)
		return (NULL);
	if (!canuse(e, excl)) {
		rfree(f, (ULONG)sizeof(*f));
		return (NULL);
	}
	if (excl && e->n_Size != 0) {
		trunc(e, 0L);
		f->f_Dirty = 1;
	}
	f->f_Node = e;
	f->f_Excl = excl;
	f->f_Next = files;
	files = f;
	return (f);
}

static void
fclose(f)
register struct rfile *f;
{
	register struct rfile **pp;

	for (pp = &files; *pp != f; pp = &(*pp)->f_Next)
		;
	*pp = f->f_Next;
	if (f->f_Dirty)
		DateStamp(&f->f_Node->n_Date);
	unuse(f->f_Node, f->f_Excl);
	rfree(f, (ULONG)sizeof(*f));
}

/* Read (wr 0) or write at the handle's position. */
static LONG
fio(f, buf, len, wr)
register struct rfile *f;
char *buf;
LONG len;
int wr;
{
	register struct rnode *e;
	LONG n;

	e = f->f_Node;
	if (e->n_Prot & (wr ? FIBF_WRITE : FIBF_READ)) {
		err = wr ? ERROR_WRITE_PROTECTED : ERROR_READ_PROTECTED;
		return (-1);
	}
	if (!wr && len > e->n_Size - f->f_Pos)
		len = e->n_Size - f->f_Pos;
	if (len <= 0)
		return (0);
	n = rw(e, f->f_Pos, buf, len, wr);
	f->f_Pos += n;
	if (wr)
		f->f_Dirty = 1;
	return (n == 0 ? -1L : n);
}

/* The handle's base for mode: the start, its position or the end. */
static LONG
fbase(f, mode)
register struct rfile *f;
LONG mode;
{
	return (mode == OFFSET_CURRENT ? f->f_Pos : mode == OFFSET_END ?
	    f->f_Node->n_Size : 0L);
}

static LONG
setsize(f, pos, mode)
register struct rfile *f;
LONG pos, mode;
{
	register struct rnode *e;
	register struct rfile *g;
	LONG sz;

	e = f->f_Node;
	sz = fbase(f, mode) + pos;
	if (sz < 0) {
		err = ERROR_SEEK_ERROR;
		return (-1);
	}
	if (e->n_Prot & FIBF_WRITE) {
		err = ERROR_WRITE_PROTECTED;
		return (-1);
	}
	if (sz < e->n_Size)
		trunc(e, (ULONG)sz);
	else if (rw(e, e->n_Size, (char *)NULL, sz - e->n_Size, 1) !=
	    sz - (LONG)e->n_Size)
		return (-1);
	for (g = files; g != NULL; g = g->f_Next)
		if (g->f_Node == e && g->f_Pos > sz)
			g->f_Pos = sz;
	f->f_Dirty = 1;
	return (sz);
}

static LONG
delete(lock, path)
LONG lock;
char *path;
{
	register struct rnode *e;

	if ((e = walk(LKNODE(lock), path, slen(path))) == NULL)
		return (DOSFALSE);
	if (e == &root) {
		err = ERROR_OBJECT_WRONG_TYPE;
		return (DOSFALSE);
	}
	if (e->n_Use != 0) {
		err = ERROR_OBJECT_IN_USE;
		return (DOSFALSE);
	}
	if (e->n_List != NULL) {
		err = ERROR_DIRECTORY_NOT_EMPTY;
		return (DOSFALSE);
	}
	if (e->n_Prot & FIBF_DELETE) {
		err = ERROR_DELETE_PROTECTED;
		return (DOSFALSE);
	}
	unlink(e);
	cfree(e->n_Data);
	if (e->n_Comment != NULL)
		rfree(e->n_Comment, (ULONG)COMMAX + 1);
	rfree(e, (ULONG)sizeof(*e));
	return (DOSTRUE);
}

/* Rename within the volume; a directory may not move into itself. */
static LONG
rename(l1, p1, l2, p2)
LONG l1, l2;
char *p1, *p2;
{
	register struct rnode *e, *d, *x;
	char *name;
	int n;

	if ((e = walk(LKNODE(l1), p1, slen(p1))) == NULL ||
	    (d = parent(l2, p2, &name, &n)) == NULL)
		return (DOSFALSE);
	if (e == &root) {
		err = ERROR_OBJECT_WRONG_TYPE;
		return (DOSFALSE);
	}
	if ((x = lookup(d, name, n)) != NULL && x != e) {
		err = ERROR_OBJECT_EXISTS;
		return (DOSFALSE);
	}
	for (x = d; x != NULL; x = x->n_Parent)
		if (x == e) {
			err = ERROR_OBJECT_IN_USE;
			return (DOSFALSE);
		}
	setname(e, name, n);
	if (d != e->n_Parent) {
		unlink(e);
		link(d, e);
	}
	return (DOSTRUE);
}

static LONG
setcomment(e, s)
register struct rnode *e;
char *s;
{
	register int n;

	if ((n = slen(s)) > COMMAX) {
		err = ERROR_COMMENT_TOO_BIG;
		return (DOSFALSE);
	}
	if (e->n_Comment == NULL && n != 0 &&
	    (e->n_Comment = (char *)ralloc((ULONG)COMMAX + 1)) == NULL)
		return (DOSFALSE);
	if (e->n_Comment != NULL)
		CopyMem(s, e->n_Comment, (ULONG)n + 1);
	return (DOSTRUE);
}

static void
fillfib(fib, e)
register struct FileInfoBlock *fib;
register struct rnode *e;
{
	register char *s, *d;

	fib->fib_DirEntryType = fib->fib_EntryType = e->n_Type;
	for (s = e == &root ? volname : e->n_Name, d = fib->fib_FileName;
	    (*d++ = *s++) != '\0'; )
		;
	fib->fib_Protection = e->n_Prot;
	fib->fib_Size = e->n_Size;
	fib->fib_NumBlocks = (e->n_Size + 511) >> 9;
	fib->fib_Date = e->n_Date;
	for (s = e->n_Comment != NULL ? e->n_Comment : "",
	    d = fib->fib_Comment; (*d++ = *s++) != '\0'; )
		;
	fib->fib_OwnerUID = fib->fib_OwnerGID = 0;
}

/* The directory entry after fib's (its first after Examine). */
static LONG
exnext(d, fib)
register struct rnode *d;
register struct FileInfoBlock *fib;
{
	register struct rnode *e;

	e = d->n_Type > 0 ? d->n_List : NULL;
	if (fib->fib_DiskKey != 0) {
		while (e != NULL && e != (struct rnode *)fib->fib_DiskKey)
			e = e->n_Next;
		if (e != NULL)
			e = e->n_Next;
	}
	if (e == NULL) {
		err = ERROR_NO_MORE_ENTRIES;
		return (DOSFALSE);
	}
	fillfib(fib, e);
	fib->fib_DiskKey = (LONG)e;
	return (DOSTRUE);
}

static void
info(id)
register struct InfoData *id;
{
	id->id_NumSoftErrors = 0;
	id->id_UnitNumber = 0;
	id->id_DiskState = ID_VALIDATED;
	id->id_NumBlocks = id->id_NumBlocksUsed = (used + 511) >> 9;
	id->id_BytesPerBlock = 512;
	id->id_DiskType = ID_DOS_DISK;
	id->id_VolumeNode = (BPTR)vol;
	id->id_InUse = nlocks != 0 || files != NULL ? DOSTRUE : DOSFALSE;
}

static void
packet(dp)
register struct DosPacket *dp;
{
	register struct rfile *f;
	register struct rnode *e;
	LONG r1, a;

	r1 = DOSTRUE;
	err = 0;
	f = (struct rfile *)dp->dp_Arg1;
	switch ((int)dp->dp_Type) {
	case ACTION_LOCATE_OBJECT:
		r1 = (LONG)newlock(walk(LKNODE(dp->dp_Arg1), (char *)dp->dp_Arg2,
		    slen((char *)dp->dp_Arg2)), dp->dp_Arg3);
		break;
	case ACTION_COPY_DIR:
		r1 = (LONG)newlock(LKNODE(dp->dp_Arg1), (LONG)SHARED_LOCK);
		break;
	case ACTION_PARENT:
		e = LKNODE(dp->dp_Arg1)->n_Parent;
		r1 = e == NULL ? 0L : (LONG)newlock(e, (LONG)SHARED_LOCK);
		break;
	case ACTION_FREE_LOCK:
		if (dp->dp_Arg1 != 0)
			freelock((struct FileLock *)dp->dp_Arg1);
		break;
	case ACTION_SAME_LOCK:
		r1 = LKNODE(dp->dp_Arg1) == LKNODE(dp->dp_Arg2) ? DOSTRUE :
		    DOSFALSE;
		break;
	case ACTION_EXAMINE_OBJECT:
		fillfib((struct FileInfoBlock *)dp->dp_Arg2, LKNODE(dp->dp_Arg1));
		((struct FileInfoBlock *)dp->dp_Arg2)->fib_DiskKey = 0;
		break;
	case ACTION_EXAMINE_NEXT:
		r1 = exnext(LKNODE(dp->dp_Arg1),
		    (struct FileInfoBlock *)dp->dp_Arg2);
		break;
	case ACTION_EXAMINE_FH:
		fillfib((struct FileInfoBlock *)dp->dp_Arg2, f->f_Node);
		((struct FileInfoBlock *)dp->dp_Arg2)->fib_DiskKey = 0;
		break;
	case ACTION_FINDINPUT:
	case ACTION_FINDOUTPUT:
	case ACTION_FINDUPDATE:
		if ((f = fopen(dp->dp_Type, dp->dp_Arg2, (char *)dp->dp_Arg3))
		    == NULL)
			r1 = DOSFALSE;
		else
			((struct FileHandle *)dp->dp_Arg1)->fh_Args = (LONG)f;
		break;
	case ACTION_READ:
	case ACTION_WRITE:
		r1 = fio(f, (char *)dp->dp_Arg2, dp->dp_Arg3,
		    dp->dp_Type == ACTION_WRITE);
		break;
	case ACTION_SEEK:
		a = fbase(f, dp->dp_Arg3) + dp->dp_Arg2;
		if (a < 0 || a > f->f_Node->n_Size) {
			r1 = -1;
			err = ERROR_SEEK_ERROR;
			break;
		}
		r1 = f->f_Pos;
		f->f_Pos = a;
		break;
	case ACTION_SET_FILE_SIZE:
		r1 = setsize(f, dp->dp_Arg2, dp->dp_Arg3);
		break;
	case ACTION_END:
		fclose(f);
		break;
	case ACTION_DELETE_OBJECT:
		r1 = delete(dp->dp_Arg1, (char *)dp->dp_Arg2);
		break;
	case ACTION_CREATE_DIR: {
		char *name;
		int n;

		r1 = DOSFALSE;
		if ((e = parent(dp->dp_Arg1, (char *)dp->dp_Arg2, &name, &n))
		    != NULL && (e = create(e, name, n, (LONG)ST_USERDIR)) != NULL)
			r1 = (LONG)newlock(e, (LONG)EXCLUSIVE_LOCK);
		break;
	}
	case ACTION_RENAME_OBJECT:
		r1 = rename(dp->dp_Arg1, (char *)dp->dp_Arg2, dp->dp_Arg3,
		    (char *)dp->dp_Arg4);
		break;
	case ACTION_SET_PROTECT:
	case ACTION_SET_COMMENT:
	case ACTION_SET_DATE:
		r1 = DOSFALSE;
		if ((e = walk(LKNODE(dp->dp_Arg2), (char *)dp->dp_Arg3,
		    slen((char *)dp->dp_Arg3))) == NULL)
			break;
		r1 = DOSTRUE;
		if (dp->dp_Type == ACTION_SET_PROTECT)
			e->n_Prot = dp->dp_Arg4;
		else if (dp->dp_Type == ACTION_SET_DATE)
			e->n_Date = *(struct DateStamp *)dp->dp_Arg4;
		else
			r1 = setcomment(e, (char *)dp->dp_Arg4);
		break;
	case ACTION_INFO:
		info((struct InfoData *)dp->dp_Arg2);
		break;
	case ACTION_DISK_INFO:
		info((struct InfoData *)dp->dp_Arg1);
		break;
	case ACTION_CURRENT_VOLUME:
		r1 = (LONG)vol;
		break;
	case ACTION_IS_FILESYSTEM:
	case ACTION_FLUSH:
		break;
	default:
		r1 = DOSFALSE;
		err = ERROR_ACTION_NOT_KNOWN;
	}
	ReplyPkt(dp, r1, r1 == DOSFALSE || r1 == -1 ? err : 0L);
}

int
main()
{
	register struct DosPacket *dp;

	port = &((struct Process *)FindTask(NULL))->pr_MsgPort;
	dp = WaitPkt();
	root.n_Type = ST_ROOT;
	DateStamp(&root.n_Date);
	if ((vol = MakeDosEntry(volname, (LONG)DLT_VOLUME)) == NULL) {
		ReplyPkt(dp, DOSFALSE, ERROR_NO_FREE_STORE);
		return (0);
	}
	vol->dol_Task = port;
	vol->dol_misc.dol_volume.dol_DiskType = ID_DOS_DISK;
	vol->dol_misc.dol_volume.dol_VolumeDate = root.n_Date;
	AddDosEntry(vol);
	((struct DosList *)dp->dp_Arg3)->dol_Task = port;
	ReplyPkt(dp, DOSTRUE, 0L);
	for (;;)
		packet(WaitPkt());
}

static char ramname[] = "ram-handler";
static char ramid[] = "ram-handler 39.1 (Exec900)";

/* The romtag's init, as (0, seglist, SysBase): list the device RAM. */
static APTR
raminit(zero, seg, sysbase)
APTR zero, seg, sysbase;
{
	register struct DosList *dl;

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 39L)) == NULL)
		return (NULL);
	if ((dl = MakeDosEntry("RAM", (LONG)DLT_DEVICE)) == NULL)
		return (NULL);
	dl->dol_misc.dol_handler.dol_Handler = ramname;
	dl->dol_misc.dol_handler.dol_StackSize = 1024;
	dl->dol_misc.dol_handler.dol_Priority = 10;
	dl->dol_misc.dol_handler.dol_SegList = (BPTR)seg;
	AddDosEntry(dl);
	return ((APTR)dl);
}

struct Resident ramtag = {
	RTC_MATCHWORD, &ramtag, (APTR)(&ramtag + 1), 0, 39,
	NT_UNKNOWN, 0, ramname, ramid, (APTR)raminit
};
