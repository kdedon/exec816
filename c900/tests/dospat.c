/* SPDX-License-Identifier: MIT */
/*
 * Pattern matching: ParsePattern's result and overflow, MatchPattern
 * with ? #x #? (a|b) ~x [a-z] [~a-z] % ' and repeated groups, the
 * NoCase forms, * with and without RNF_WILDSTAR; MatchFirst/MatchNext
 * over a tree made on the boot volume: a wild last part, a wild
 * directory part, a plain name, APF_DODIR recursion with APF_DIDDIR, a
 * relative pattern, errors, ap_Buf overflow and a break.
 */
#define TNAME	"dospat"
#include "dostest.h"
#include <dos/dosasl.h>

static UBYTE pb[64];
static struct {
	struct AnchorPath a;
	char b[64];
} apx;
static char *names[] = {
	"SYS:pt/sub/d.c", "SYS:pt/sub/e.o", "SYS:pt/sub", "SYS:pt/a.c",
	"SYS:pt/b.h", "SYS:pt/Cc.C", "SYS:pt", NULL
};

/* Whether pattern p matches s (NoCase when nc). */
static int
mp(p, s, nc)
char *p, *s;
int nc;
{
	if (nc)
		return (ParsePatternNoCase(p, pb, (LONG)sizeof(pb)) >= 0 &&
		    MatchPatternNoCase(pb, s) != DOSFALSE);
	return (ParsePattern(p, pb, (LONG)sizeof(pb)) >= 0 &&
	    MatchPattern(pb, s) != DOSFALSE);
}

static struct AnchorPath *
anchor(len)
int len;
{
	register struct AnchorPath *ap;

	ap = &apx.a;
	ap->ap_Flags = 0;
	ap->ap_BreakBits = 0;
	ap->ap_Strlen = len;
	return (ap);
}

/* MatchFirst then MatchNext to the end, entering directories when dodir
 * is set: the matches, their ap_Buf names after the first in buf, and
 * how often APF_DIDDIR came back. */
static char lbuf[160];
static int diddir;

static int
walk(pat, dodir)
char *pat;
int dodir;
{
	register struct AnchorPath *ap;
	register char *d, *s;
	LONG r;
	int n;

	ap = anchor(64);
	d = lbuf;
	n = 0;
	diddir = 0;
	for (r = MatchFirst(pat, ap); r == 0; r = MatchNext(ap)) {
		if (ap->ap_Flags & APF_DIDDIR) {
			diddir++;
			ap->ap_Flags &= ~APF_DIDDIR;
		} else if (dodir && ap->ap_Info.fib_DirEntryType > 0)
			ap->ap_Flags |= APF_DODIR;
		if (n++ > 0 && d < lbuf + sizeof(lbuf) - 40) {
			if (d != lbuf)
				*d++ = ' ';
			for (s = (char *)ap->ap_Buf; *s != '\0'; )
				*d++ = *s++;
		}
	}
	*d = '\0';
	MatchEnd(ap);
	return (r == ERROR_NO_MORE_ENTRIES ? n : -1);
}

static int
dtest()
{
	register struct AnchorPath *ap;
	register char **p;
	BPTR fh, lk, old;
	struct Process *me;

	CHECK(ParsePattern("abc", pb, 8L) == 0, "ParsePattern plain");
	CHECK(ParsePattern("a?c", pb, 8L) == 1, "ParsePattern wild");
	CHECK(ParsePattern("abcdefgh", pb, 4L) == -1, "ParsePattern overflow");
	CHECK(mp("#?.c", "a.c", 0) && mp("#?.c", ".c", 0) &&
	    mp("#?.c", "a.c.c", 0) && !mp("#?.c", "a.h", 0), "#?");
	CHECK(mp("a#bc", "ac", 0) && mp("a#bc", "abbbc", 0) &&
	    !mp("a#bc", "abd", 0), "#x");
	CHECK(mp("(foo|bar)x", "barx", 0) && !mp("(foo|bar)x", "bazx", 0) &&
	    mp("(a|)b", "b", 0) && mp("(a|)b", "ab", 0), "(a|b)");
	CHECK(!mp("~(#?.info)", "a.info", 0) && mp("~(#?.info)", "a.c", 0) &&
	    mp("x~ay", "xby", 0) && !mp("x~ay", "xay", 0), "~");
	CHECK(mp("[a-c]x", "bx", 0) && !mp("[a-c]x", "dx", 0) &&
	    mp("[~a-c]x", "dx", 0) && !mp("[~a-c]x", "ax", 0) &&
	    mp("[xyz]", "y", 0), "classes");
	CHECK(mp("a%b", "ab", 0) && mp("'?", "?", 0) && !mp("'?", "a", 0) &&
	    !mp("?", "", 0), "% ' ?");
	CHECK(mp("#(ab)c", "ababc", 0) && !mp("#(ab)c", "abac", 0) &&
	    mp("#(a|b)", "abba", 0) && mp("#(a|b)", "", 0), "#(group)");
	CHECK(!mp("A#?.C", "abc.c", 0) && mp("A#?.C", "abc.c", 1) &&
	    mp("[A-C]", "b", 1), "NoCase");
	CHECK(ParsePattern("*", pb, 8L) == 0 && mp("*", "*", 0) &&
	    !mp("*", "x", 0), "* without RNF_WILDSTAR");
	DOSBase->dl_Root->rn_Flags |= RNF_WILDSTAR;
	CHECK(mp("*.c", "a.c", 0), "* with RNF_WILDSTAR");
	DOSBase->dl_Root->rn_Flags &= ~RNF_WILDSTAR;

	for (p = names; *p != NULL; p++)
		DeleteFile(*p);
	CHECK((lk = CreateDir("SYS:pt")) != NULL, "CreateDir pt");
	UnLock(lk);
	CHECK((lk = CreateDir("SYS:pt/sub")) != NULL, "CreateDir sub");
	UnLock(lk);
	for (p = names; *p != NULL; p++)
		if (p != names + 2 && p[1] != NULL) {
			CHECK((fh = Open(*p, MODE_NEWFILE)) != NULL, "a file");
			Close(fh);
		}

	CHECK(walk("SYS:pt/#?.c", 0) == 2 && same(lbuf, "SYS:pt/Cc.C"),
	    "a wild last part");
	CHECK(walk("SYS:pt/#?/#?.c", 0) == 1, "a wild directory part");
	ap = anchor(64);
	CHECK(MatchFirst("SYS:pt/#?/#?.c", ap) == 0 &&
	    same((char *)ap->ap_Buf, "SYS:pt/sub/d.c") &&
	    same(ap->ap_Info.fib_FileName, "d.c") &&
	    (ap->ap_Flags & APF_ITSWILD), "the inner match's path");
	MatchEnd(ap);
	CHECK(MatchFirst("SYS:pt/sub", ap) == 0 &&
	    !(ap->ap_Flags & APF_ITSWILD) &&
	    ap->ap_Info.fib_DirEntryType > 0, "a plain name");
	MatchEnd(ap);
	CHECK(walk("SYS:pt/sub", 1) == 4 && diddir == 1 &&
	    same(lbuf, "SYS:pt/sub/d.c SYS:pt/sub/e.o SYS:pt/sub"),
	    "APF_DODIR into a plain name");
	CHECK(walk("SYS:pt/#?", 1) == 7 && diddir == 1, "APF_DODIR");
	CHECK(walk("SYS:pt/#?.zz", 0) == 0, "no match");
	ap = anchor(64);
	CHECK(MatchFirst("SYS:pt/none", ap) == ERROR_OBJECT_NOT_FOUND &&
	    IoErr() == ERROR_OBJECT_NOT_FOUND, "a missing plain name");
	MatchEnd(ap);
	CHECK(MatchFirst("NOSUCH:#?", ap) == ERROR_DEVICE_NOT_MOUNTED,
	    "a missing device");
	MatchEnd(ap);

	CHECK((lk = Lock("SYS:pt", SHARED_LOCK)) != NULL, "Lock pt");
	old = CurrentDir(lk);
	CHECK(MatchFirst("#?.h", ap) == 0 && same((char *)ap->ap_Buf, "b.h"),
	    "a relative pattern");
	CHECK(MatchNext(ap) == ERROR_NO_MORE_ENTRIES, "its end");
	MatchEnd(ap);
	UnLock(CurrentDir(old));

	ap = anchor(8);
	CHECK(MatchFirst("SYS:pt/#?.h", ap) == ERROR_BUFFER_OVERFLOW,
	    "ap_Buf overflow");
	MatchEnd(ap);
	ap = anchor(0);
	ap->ap_BreakBits = SIGBREAKF_CTRL_C;
	me = (struct Process *)FindTask(NULL);
	CHECK(MatchFirst("SYS:pt/#?", ap) == 0, "MatchFirst before a break");
	Signal(&me->pr_Task, SIGBREAKF_CTRL_C);
	CHECK(MatchNext(ap) == ERROR_BREAK &&
	    ap->ap_FoundBreak == SIGBREAKF_CTRL_C &&
	    !(SetSignal(0L, 0L) & SIGBREAKF_CTRL_C), "a break");
	MatchEnd(ap);

	for (p = names; *p != NULL; p++)
		CHECK(DeleteFile(*p) == DOSTRUE, "DeleteFile");
	return (0);
}
