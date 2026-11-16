/* SPDX-License-Identifier: MIT */
/*
 * The C: commands' and the shell's ReadArgs templates: Echo with
 * NOLINE, FIRST, LEN and TO; "?" answered from the command's input;
 * Type with NUMBER, HEX, TO and errors; Dir with ALL, FILES, DIRS and a
 * pattern; the shell's CD with a quoted name and too many arguments;
 * Set, Get, Unset, Setenv, Getenv, Unsetenv, Alias, Unalias, $name and
 * ${name} substitution, *$ and an alias's []; MakeDir, Rename,
 * Delete, Protect, Wait and Date templates.
 */
#define TNAME	"cmdargs"
#include "dostest.h"

static char buf[160];

static char vscript[] = "Set greet hi there\nEcho $greet!\nEcho ${greet}x *$greet\n\
Alias ec Echo [] END\nec mid\nGet greet\nGet nosuch\nIf WARN\n\
Echo warned\nEndIf\nUnset greet\nEcho $greet\nSetenv g1 global\n\
Getenv g1\nUnsetenv g1\nGetenv g1\nUnalias ec\nSet a1 x\nSet\n";
static char vwant[] = "hi there!\nhi therex $greet\nmid END\nhi there\nwarned\n\
$greet\nglobal\na1              x\n";
static char *made[] = {
	"SYS:dt/s/g", "SYS:dt/s", "SYS:dt/f1", "SYS:dt", "SYS:tin", "SYS:ao",
	"SYS:sin", NULL
};

static LONG
sys(cmd, in, out)
char *cmd;
BPTR in, out;
{
	struct TagItem t[3];

	t[0].ti_Tag = in != NULL ? SYS_Input : TAG_IGNORE;
	t[0].ti_Data = (ULONG)in;
	t[1].ti_Tag = out != NULL ? SYS_Output : TAG_IGNORE;
	t[1].ti_Data = (ULONG)out;
	t[2].ti_Tag = TAG_DONE;
	return (SystemTagList(cmd, t));
}

static int
put(name, s)
char *name, *s;
{
	BPTR fh;
	int r;

	if ((fh = Open(name, MODE_NEWFILE)) == NULL)
		return (0);
	r = FPuts(fh, s) == 0;
	return (Close(fh) == DOSTRUE && r);
}

/* SYS:ao's contents in buf. */
static char *
ao()
{
	BPTR fh;
	LONG n;

	buf[0] = '\0';
	if ((fh = Open("SYS:ao", MODE_OLDFILE)) != NULL) {
		n = Read(fh, buf, (LONG)sizeof(buf) - 1);
		buf[n > 0 ? (int)n : 0] = '\0';
		Close(fh);
	}
	return (buf);
}

/* Whether s holds c. */
static int
has(s, c)
register char *s;
int c;
{
	for (; *s != '\0'; s++)
		if (*s == c)
			return (1);
	return (0);
}

/* Run cmd with its output into SYS:ao: its result. */
static LONG
run(cmd)
char *cmd;
{
	BPTR out;
	LONG rc;

	if ((out = Open("SYS:ao", MODE_NEWFILE)) == NULL)
		return (-2);
	rc = sys(cmd, (BPTR)NULL, out);
	Close(out);
	return (rc);
}

static int
dtest()
{
	register char **p;
	BPTR lk, in, out;

	for (p = made; *p != NULL; p++)
		DeleteFile(*p);
	CHECK(sys("Echo hello  world TO SYS:ao", NULL, NULL) == 0 &&
	    same(ao(), "hello world\n"), "Echo TO");
	CHECK(run("Echo hello world FIRST 7 NOLINE") == 0 &&
	    same(ao(), "world"), "Echo FIRST NOLINE");
	CHECK(run("Echo abcdef LEN=2") == 0 && same(ao(), "ef\n"), "Echo LEN");
	CHECK(run("Echo \"a  b\" LEN 3 FIRST 2") == 0 && same(ao(), "  b\n"),
	    "Echo FIRST LEN");
	CHECK(put("SYS:sin", "x y\n") && (in = Open("SYS:sin", MODE_OLDFILE))
	    != NULL && (out = Open("SYS:ao", MODE_NEWFILE)) != NULL,
	    "streams for ?");
	CHECK(sys("Echo ?", in, out) == 0, "Echo ?");
	Close(out);
	Close(in);
	CHECK(same(ao(), "/M,NOLINE/S,FIRST/K/N,LEN/K/N,TO/K: x y\n"),
	    "Echo ?'s prompt and answer");

	CHECK(put("SYS:tin", "ab\ncd\n"), "Type's file");
	CHECK(run("Type SYS:tin NUMBER") == 0 &&
	    same(ao(), "    1 ab\n    2 cd\n"), "Type NUMBER");
	CHECK(run("Type OPT H SYS:tin") == 0 && same(ao(),
	    "0000: 61620a63 640a0000 00000000 00000000 ab.cd.\n"), "Type HEX");
	CHECK(sys("Type SYS:tin SYS:tin TO SYS:ao", NULL, NULL) == 0 &&
	    same(ao(), "ab\ncd\nab\ncd\n"), "Type two files TO");
	CHECK(run("Type SYS:nonesuch") == RETURN_ERROR, "Type a missing file");
	CHECK(run("Type") == RETURN_ERROR &&
	    same(ao(), "required argument missing\n"), "Type without FROM");

	CHECK((lk = CreateDir("SYS:dt")) != NULL, "CreateDir dt");
	UnLock(lk);
	CHECK((lk = CreateDir("SYS:dt/s")) != NULL, "CreateDir dt/s");
	UnLock(lk);
	CHECK(put("SYS:dt/f1", "123") && put("SYS:dt/s/g", ""), "Dir's files");
	CHECK(run("Dir SYS:dt ALL") == 0 &&
	    same(ao(), "  s (dir)\n    g  0\n  f1  3\n"), "Dir ALL");
	CHECK(run("Dir SYS:dt FILES") == 0 && same(ao(), "  f1  3\n"),
	    "Dir FILES");
	CHECK(run("Dir SYS:dt OPT D") == 0 && same(ao(), "  s (dir)\n"),
	    "Dir OPT D");
	CHECK(run("Dir SYS:dt/#?1") == 0 && same(ao(), "  f1  3\n"),
	    "Dir with a pattern");

	CHECK(run("MakeDir") == RETURN_ERROR &&
	    same(ao(), "required argument missing\n"), "MakeDir without NAME");
	CHECK(run("MakeDir SYS:md1 SYS:md2") == 0 &&
	    run("Rename SYS:md1 SYS:md2 AS SYS:dt") == 0 &&
	    (lk = Lock("SYS:dt/md2", SHARED_LOCK)) != NULL, "Rename FROM/M");
	UnLock(lk);
	CHECK(run("Delete SYS:dt/md1 SYS:dt/md2 QUIET") == 0 &&
	    Lock("SYS:dt/md1", SHARED_LOCK) == NULL, "Delete FILE/M");
	CHECK(run("Protect SYS:dt/f1 FLAGS=rwd") == 0 &&
	    run("Protect SYS:dt/f1") == 0 && same(ao(), "SYS:dt/f1 ----rw-d\n"),
	    "Protect FLAGS=");
	CHECK(run("Wait 0 SECS") == 0, "Wait /N SECS");
	CHECK(sys("Date TO SYS:ao", NULL, NULL) == 0 && has(ao(), '-') &&
	    has(buf, ':'), "Date TO");
	CHECK(put("SYS:sin", "CD \"SYS:dt\"\nDir FILES\nCD a b\n") &&
	    (in = Open("SYS:sin", MODE_OLDFILE)) != NULL &&
	    (out = Open("SYS:ao", MODE_NEWFILE)) != NULL, "a script");
	CHECK(Execute("", in, out) == DOSTRUE, "Execute");
	Close(out);
	Close(in);
	CHECK(same(ao(), "  f1  3\nwrong number of arguments\n"),
	    "CD with a quoted name, then too many");

	CHECK(put("SYS:sin", vscript) && (in = Open("SYS:sin", MODE_OLDFILE))
	    != NULL && (out = Open("SYS:ao", MODE_NEWFILE)) != NULL,
	    "the variables script");
	CHECK(Execute("", in, out) == DOSTRUE, "Execute variables");
	Close(out);
	Close(in);
	CHECK(same(ao(), vwant), "variables, $ and aliases");

	for (p = made; *p != NULL; p++)
		CHECK(DeleteFile(*p) == DOSTRUE, "clean up");
	return (0);
}
