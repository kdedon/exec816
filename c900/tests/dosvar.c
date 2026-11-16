/* SPDX-License-Identifier: MIT */
/*
 * Variables: local ones (text and binary reads, truncation and IoErr's
 * length, GVF_DONT_NULL_TERM, replacing, FindVar ignoring case,
 * aliases), a child process's copy (NP_CopyVars) and its absence;
 * globals in dos.library's list while there is no ENV:, then as files
 * once ENV: is assigned (GVF_SAVE_VAR also into ENVARC:), and the list
 * again when the assign goes.
 */
#define TNAME	"dosvar"
#include "dostest.h"
#include <dos/var.h>

static char buf[40];
static struct Task *parent;
static int childok;

/* GetVar into buf (cleared to '#'): its result. */
static LONG
gv(name, size, flags)
char *name;
LONG size, flags;
{
	register int i;

	for (i = 0; i < sizeof(buf); i++)
		buf[i] = '#';
	return (GetVar(name, buf, size, flags));
}

static LONG
child(args, len)
char *args;
LONG len;
{
	childok = gv("v1", 40L, 0L) == 1 && buf[0] == 'x' &&
	    SetVar("v1", "child", -1L, 0L) == DOSTRUE;
	Signal(parent, SIGBREAKF_CTRL_E);
	return (0);
}

static LONG
nochild(args, len)
char *args;
LONG len;
{
	childok = gv("v1", 40L, GVF_LOCAL_ONLY) == -1;
	Signal(parent, SIGBREAKF_CTRL_E);
	return (0);
}

static int
spawn(fn, copy)
LONG (*fn)();
int copy;
{
	struct TagItem t[3];

	childok = 0;
	parent = FindTask(NULL);
	SetSignal(0L, SIGBREAKF_CTRL_E);
	t[0].ti_Tag = NP_Entry;
	t[0].ti_Data = (ULONG)fn;
	t[1].ti_Tag = NP_CopyVars;
	t[1].ti_Data = copy;
	t[2].ti_Tag = TAG_DONE;
	if (CreateNewProc(t) == NULL)
		return (0);
	Wait(SIGBREAKF_CTRL_E);
	return (childok);
}

/* The file name's first bytes as a string in buf. */
static char *
cat(name)
char *name;
{
	BPTR fh;
	LONG n;

	buf[0] = '\0';
	if ((fh = Open(name, MODE_OLDFILE)) != NULL) {
		n = Read(fh, buf, 39L);
		buf[n > 0 ? (int)n : 0] = '\0';
		Close(fh);
	}
	return (buf);
}

static int
dtest()
{
	struct LocalVar *v;
	BPTR lk;

	CHECK(SetVar("v1", "hello\nworld", -1L, 0L) == DOSTRUE, "SetVar");
	CHECK(gv("v1", 40L, 0L) == 5 && same(buf, "hello") && IoErr() == 11,
	    "GetVar stops at a newline");
	CHECK(gv("V1", 40L, GVF_BINARY_VAR) == 11 && buf[11] == '\0',
	    "GetVar binary, any case");
	CHECK(gv("v1", 4L, 0L) == 3 && same(buf, "hel") && IoErr() == 11,
	    "GetVar truncates");
	CHECK(gv("v1", 4L, GVF_BINARY_VAR | GVF_DONT_NULL_TERM) == 4 &&
	    buf[3] == 'l' && buf[4] == '#', "GVF_DONT_NULL_TERM");
	CHECK((v = FindVar("V1", (ULONG)LV_VAR)) != NULL && v->lv_Len == 11,
	    "FindVar");
	CHECK(SetVar("v1", "xyz", 1L, 0L) == DOSTRUE && gv("v1", 40L, 0L) == 1 &&
	    same(buf, "x") && FindVar("v1", (ULONG)LV_VAR) != v, "replace");
	CHECK(SetVar("al", "dir", -1L, (LONG)LV_ALIAS) == DOSTRUE &&
	    gv("al", 40L, 0L) == -1 && IoErr() == ERROR_OBJECT_NOT_FOUND,
	    "an alias is not a variable");
	CHECK(gv("al", 40L, (LONG)LV_ALIAS) == 3 &&
	    FindVar("al", (ULONG)LV_ALIAS) != NULL &&
	    FindVar("al", (ULONG)LV_VAR) == NULL, "an alias");
	CHECK(gv("v1", 40L, GVF_GLOBAL_ONLY) == -1, "GVF_GLOBAL_ONLY");

	CHECK(spawn(child, 1) && gv("v1", 40L, 0L) == 1 && same(buf, "x"),
	    "a child's copy");
	CHECK(spawn(nochild, 0), "NP_CopyVars FALSE");

	CHECK(SetVar("g1", "global", -1L, GVF_GLOBAL_ONLY) == DOSTRUE &&
	    gv("g1", 40L, 0L) == 6 && same(buf, "global"), "a global");
	CHECK(SetVar("g1", "local", -1L, 0L) == DOSTRUE &&
	    gv("g1", 40L, 0L) == 5 && gv("g1", 40L, GVF_GLOBAL_ONLY) == 6,
	    "a local hides a global");
	CHECK(DeleteVar("g1", 0L) == DOSTRUE && gv("g1", 40L, 0L) == 6,
	    "DeleteVar the local");
	CHECK(gv("g1", 40L, GVF_LOCAL_ONLY) == -1, "GVF_LOCAL_ONLY");
	CHECK(DeleteVar("nonesuch", 0L) == DOSFALSE &&
	    IoErr() == ERROR_OBJECT_NOT_FOUND, "DeleteVar of nothing");

	DeleteFile("SYS:envt/g2");
	DeleteFile("SYS:envt/g3");
	DeleteFile("SYS:envarc/g3");
	DeleteFile("SYS:envt");
	DeleteFile("SYS:envarc");
	CHECK((lk = CreateDir("SYS:envt")) != NULL &&
	    AssignLock("ENV", lk) == DOSTRUE, "assign ENV:");
	CHECK((lk = CreateDir("SYS:envarc")) != NULL &&
	    AssignLock("ENVARC", lk) == DOSTRUE, "assign ENVARC:");
	CHECK(SetVar("g2", "filed\n", -1L, GVF_GLOBAL_ONLY) == DOSTRUE &&
	    same(cat("SYS:envt/g2"), "filed\n"), "SetVar into ENV:");
	CHECK(gv("g2", 40L, 0L) == 5 && same(buf, "filed"), "GetVar from ENV:");
	CHECK(gv("g2", 3L, 0L) == 2 && same(buf, "fi") && IoErr() == 6,
	    "GetVar from ENV: truncates");
	CHECK(gv("g1", 40L, GVF_GLOBAL_ONLY) == -1, "ENV: hides the list");
	CHECK(SetVar("g3", "saved", -1L, GVF_GLOBAL_ONLY | GVF_SAVE_VAR) ==
	    DOSTRUE && same(cat("SYS:envarc/g3"), "saved") &&
	    same(cat("SYS:envt/g3"), "saved"), "GVF_SAVE_VAR");
	CHECK(DeleteVar("g2", GVF_GLOBAL_ONLY) == DOSTRUE &&
	    gv("g2", 40L, 0L) == -1 && IoErr() == ERROR_OBJECT_NOT_FOUND,
	    "DeleteVar in ENV:");
	CHECK(DeleteVar("g3", GVF_GLOBAL_ONLY | GVF_SAVE_VAR) == DOSTRUE &&
	    Lock("SYS:envarc/g3", SHARED_LOCK) == NULL, "DeleteVar in ENVARC:");
	CHECK(AssignLock("ENV", NULL) == DOSTRUE &&
	    AssignLock("ENVARC", NULL) == DOSTRUE, "remove the assigns");
	CHECK(gv("g1", 40L, GVF_GLOBAL_ONLY) == 6, "the list again");
	CHECK(DeleteVar("g1", GVF_GLOBAL_ONLY) == DOSTRUE &&
	    gv("g1", 40L, 0L) == -1, "DeleteVar a global");
	CHECK(DeleteFile("SYS:envt") == DOSTRUE &&
	    DeleteFile("SYS:envarc") == DOSTRUE, "clean up");
	return (0);
}
