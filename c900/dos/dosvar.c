/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * dos.library variables, as AmigaDOS V36: GetVar, SetVar, DeleteVar and
 * FindVar.  Local variables and aliases are LocalVar nodes on the
 * process's pr_LocalVars (one allocation each: node, name and value);
 * a new process gets a copy of its creator's (NP_CopyVars) and frees
 * its own at exit.  Global variables are the files ENV:name whenever
 * the DosList has an ENV entry, as on the Amiga; without one they are
 * nodes on a list dos.library keeps, so they work before any ENV:
 * exists (docs/platform.md, DOS).
 */
#include "dosint.h"
#include <dos/var.h>

#define VNAMEMAX	56	/* a global's name, for "ENVARC:" + name */

static struct MinList dgvars;		/* globals without ENV: */
static struct SignalSemaphore dgvlock;

void
dvinit()
{
	NewList((struct List *)&dgvars);
	InitSemaphore(&dgvlock);
}

/* The variable name of type on list l, or NULL. */
static struct LocalVar *
dvfind(l, name, type)
struct MinList *l;
char *name;
int type;
{
	register struct LocalVar *v;

	for (v = (struct LocalVar *)l->mlh_Head; v->lv_Node.ln_Succ != NULL;
	    v = (struct LocalVar *)v->lv_Node.ln_Succ)
		if (v->lv_Node.ln_Type == type &&
		    dnamecmp(v->lv_Node.ln_Name, name, 255) == 0)
			return (v);
	return (NULL);
}

/* Set name (of type and flags) to the size bytes at buf on list l. */
static LONG
dvset(l, name, buf, size, flags)
struct MinList *l;
char *name;
UBYTE *buf;
LONG size, flags;
{
	register struct LocalVar *v, *o;
	register LONG n;

	n = dstrlen(name) + 1;
	if ((v = (struct LocalVar *)AllocVec((ULONG)sizeof(*v) + n + size + 1,
	    MEMF_PUBLIC)) == NULL)
		return (dfail(ERROR_NO_FREE_STORE));
	v->lv_Node.ln_Type = (UBYTE)flags;
	v->lv_Node.ln_Name = (char *)(v + 1);
	CopyMem(name, v->lv_Node.ln_Name, (ULONG)n);
	v->lv_Value = (UBYTE *)v->lv_Node.ln_Name + (UWORD)n;
	CopyMem(buf, v->lv_Value, (ULONG)size);
	v->lv_Value[(UWORD)size] = '\0';
	v->lv_Len = size;
	v->lv_Flags = flags & GVF_BINARY_VAR;
	if ((o = dvfind(l, name, (int)(UBYTE)flags)) != NULL) {
		Insert((struct List *)l, &v->lv_Node, &o->lv_Node);
		Remove(&o->lv_Node);
		FreeVec(o);
	} else
		AddTail((struct List *)l, &v->lv_Node);
	return (DOSTRUE);
}

/* A value of len bytes at s into buf of size bytes, as flags say: a
 * text value stops at a newline, and the copy is NUL-terminated unless
 * GVF_DONT_NULL_TERM.  The bytes stored; IoErr is the value's length. */
static LONG
dvout(s, len, buf, size, flags)
register UBYTE *s;
LONG len;
register UBYTE *buf;
LONG size, flags;
{
	register LONG n, max;

	max = flags & GVF_DONT_NULL_TERM ? size : size - 1;
	for (n = 0; n < len && n < max; n++) {
		if (s[(UWORD)n] == '\n' && !(flags & GVF_BINARY_VAR))
			break;
		buf[(UWORD)n] = s[(UWORD)n];
	}
	if (n < size && !(flags & GVF_DONT_NULL_TERM))
		buf[(UWORD)n] = '\0';
	dseterr(len);
	return (n);
}

/* Whether the DosList has the device or assign name. */
static int
denv(name)
char *name;
{
	register struct DosList *dl;

	dl = xFindDosEntry(DOSBase, xLockDosList(DOSBase, LDF_ALL | LDF_READ),
	    name, LDF_ALL);
	xUnLockDosList(DOSBase, LDF_ALL | LDF_READ);
	return (dl != NULL);
}

/* prefix and name in path (VNAMEMAX + 8 bytes); DOSFALSE if too long. */
static LONG
dvpath(path, prefix, name)
register char *path, *prefix, *name;
{
	if (dstrlen(name) > VNAMEMAX)
		return (dfail(ERROR_LINE_TOO_LONG));
	while ((*path = *prefix++) != '\0')
		path++;
	while ((*path++ = *name++) != '\0')
		;
	return (DOSTRUE);
}

/* The file ENV:name into buf, as dvout; -1 when it cannot be read. */
static LONG
dvfget(name, buf, size, flags)
char *name;
UBYTE *buf;
LONG size, flags;
{
	char path[VNAMEMAX + 8];
	register BPTR fh;
	register LONG n, len;

	if (dvpath(path, "ENV:", name) == DOSFALSE ||
	    (fh = xOpen(DOSBase, path, MODE_OLDFILE)) == NULL)
		return (-1);
	len = n = xRead(DOSBase, fh, buf, flags & GVF_DONT_NULL_TERM ? size :
	    size - 1);
	if (n >= 0 && n == size - !(flags & GVF_DONT_NULL_TERM)) {
		xSeek(DOSBase, fh, 0L, OFFSET_END);
		len = xSeek(DOSBase, fh, 0L, OFFSET_CURRENT);
	}
	xClose(DOSBase, fh);
	if (n < 0)
		return (-1);
	n = dvout(buf, n, buf, size, flags);
	dseterr(len);
	return (n);
}

/* Write the file prefix:name. */
static LONG
dvfput(prefix, name, buf, size)
char *prefix, *name;
UBYTE *buf;
LONG size;
{
	char path[VNAMEMAX + 8];
	register BPTR fh;
	register LONG r;

	if (dvpath(path, prefix, name) == DOSFALSE ||
	    (fh = xOpen(DOSBase, path, MODE_NEWFILE)) == NULL)
		return (DOSFALSE);
	r = xWrite(DOSBase, fh, buf, size) == size ? DOSTRUE : DOSFALSE;
	if (xClose(DOSBase, fh) == DOSFALSE)
		r = DOSFALSE;
	return (r);
}

static LONG
dvfdel(prefix, name)
char *prefix, *name;
{
	char path[VNAMEMAX + 8];

	if (dvpath(path, prefix, name) == DOSFALSE)
		return (DOSFALSE);
	return (xDeleteFile(DOSBase, path));
}

/* The calling process's local variables, or NULL for a plain Task. */
static struct MinList *
dvlocal()
{
	register struct Process *p;

	return ((p = dme()) != NULL ? &p->pr_LocalVars : NULL);
}

/*
 * The value of name in buf (size bytes): a local variable or alias
 * (type in the flags' low byte) unless GVF_GLOBAL_ONLY, then a global
 * one unless GVF_LOCAL_ONLY.  The bytes stored, IoErr the value's whole
 * length; -1 (ERROR_OBJECT_NOT_FOUND) when there is none.
 */
LONG
xGetVar(db, name, buf, size, flags)
DB db;
char *name;
UBYTE *buf;
LONG size, flags;
{
	register struct LocalVar *v;
	register struct MinList *l;
	register int type;
	LONG n;

	type = (UBYTE)flags;
	if (!(flags & GVF_GLOBAL_ONLY) && (l = dvlocal()) != NULL &&
	    (v = dvfind(l, name, type)) != NULL)
		return (dvout(v->lv_Value, v->lv_Len, buf, size, flags));
	n = -1;
	if (!(flags & GVF_LOCAL_ONLY) && type == LV_VAR) {
		if (denv("ENV"))
			n = dvfget(name, buf, size, flags);
		else {
			ObtainSemaphoreShared(&dgvlock);
			if ((v = dvfind(&dgvars, name, type)) != NULL)
				n = dvout(v->lv_Value, v->lv_Len, buf, size,
				    flags);
			ReleaseSemaphore(&dgvlock);
		}
	}
	if (n < 0)
		dseterr(ERROR_OBJECT_NOT_FOUND);
	return (n);
}

/* Set name to size bytes at buf (-1: buf is a string): a local
 * variable unless GVF_GLOBAL_ONLY; GVF_SAVE_VAR also writes ENVARC:.
 * A NULL buf deletes it. */
LONG
xSetVar(db, name, buf, size, flags)
DB db;
char *name;
UBYTE *buf;
LONG size, flags;
{
	register struct MinList *l;
	register LONG r;

	if (buf == NULL)
		return (xDeleteVar(db, name, flags));
	if (size < 0)
		size = dstrlen((char *)buf);
	if (!(flags & GVF_GLOBAL_ONLY)) {
		if ((l = dvlocal()) == NULL)
			return (dfail(ERROR_OBJECT_WRONG_TYPE));
		return (dvset(l, name, buf, size, flags));
	}
	if ((UBYTE)flags != LV_VAR)
		return (dfail(ERROR_OBJECT_WRONG_TYPE));
	if (!denv("ENV")) {
		ObtainSemaphore(&dgvlock);
		r = dvset(&dgvars, name, buf, size, flags);
		ReleaseSemaphore(&dgvlock);
		return (r);
	}
	r = dvfput("ENV:", name, buf, size);
	if (r != DOSFALSE && (flags & GVF_SAVE_VAR) && denv("ENVARC"))
		r = dvfput("ENVARC:", name, buf, size);
	return (r);
}

/* Delete name: the local one unless GVF_GLOBAL_ONLY, else the global. */
LONG
xDeleteVar(db, name, flags)
DB db;
char *name;
LONG flags;
{
	register struct LocalVar *v;
	register struct MinList *l;
	register int type;
	LONG r;

	type = (UBYTE)flags;
	l = NULL;
	if (!(flags & GVF_GLOBAL_ONLY))
		l = dvlocal();
	else if (type == LV_VAR) {
		if (denv("ENV")) {
			r = dvfdel("ENV:", name);
			if (r != DOSFALSE && (flags & GVF_SAVE_VAR) &&
			    denv("ENVARC"))
				dvfdel("ENVARC:", name);
			return (r);
		}
		l = &dgvars;
		ObtainSemaphore(&dgvlock);
	}
	r = DOSFALSE;
	if (l != NULL && (v = dvfind(l, name, type)) != NULL) {
		Remove(&v->lv_Node);
		FreeVec(v);
		r = DOSTRUE;
	}
	if (l == &dgvars)
		ReleaseSemaphore(&dgvlock);
	return (r != DOSFALSE ? r : dfail(ERROR_OBJECT_NOT_FOUND));
}

struct LocalVar *
xFindVar(db, name, type)
DB db;
char *name;
ULONG type;
{
	register struct MinList *l;

	if ((l = dvlocal()) == NULL)
		return (NULL);
	return (dvfind(l, name, (int)(UBYTE)type));
}

/* Copy from's local variables onto to's new list (from may be NULL). */
void
dvcopy(from, to)
struct Process *from, *to;
{
	register struct LocalVar *v;

	NewList((struct List *)&to->pr_LocalVars);
	if (from == NULL)
		return;
	for (v = (struct LocalVar *)from->pr_LocalVars.mlh_Head;
	    v->lv_Node.ln_Succ != NULL;
	    v = (struct LocalVar *)v->lv_Node.ln_Succ)
		dvset(&to->pr_LocalVars, v->lv_Node.ln_Name, v->lv_Value,
		    (LONG)v->lv_Len, (LONG)(v->lv_Node.ln_Type | v->lv_Flags));
}

/* Free a process's local variables. */
void
dvfree(p)
struct Process *p;
{
	register struct Node *n;

	while ((n = RemHead((struct List *)&p->pr_LocalVars)) != NULL)
		FreeVec(n);
}
