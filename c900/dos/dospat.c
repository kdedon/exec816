/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * dos.library pattern matching, as AmigaDOS V36/V39: ParsePattern,
 * MatchPattern and their NoCase forms, MatchFirst, MatchNext and
 * MatchEnd.  Patterns: ? (one character), #x (any number of x), #?
 * (anything), (a|b) (either), ~x (anything x does not match), [a-z]
 * and [~a-z] (classes), % (nothing), ' (the next character as itself)
 * and, with RNF_WILDSTAR, * for #?.
 *
 * The parsed form (private) is the pattern's characters with the
 * dosasl.h tokens for the operators; a literal byte 0x80 or above is
 * DP_LIT and the byte.  ParsePattern wraps the operand of ~, and of #
 * when it is not one character, in ( ), so every operand is one
 * character or a group.  The matcher backtracks: a continuation (dk)
 * says what follows the group or repetition being matched, so it
 * recurses only at #, ~, groups and #?, never per character of a run.
 */
#include "dosint.h"
#include <dos/dosasl.h>

#define DP_EMPTY	0x8C	/* % */
#define DP_LIT		0x8D	/* the next byte is a literal */
#define DP_RANGE	0x8E	/* in a class: a-b */

struct dpp {			/* the parser's state */
	UBYTE *p_Src;
	UBYTE *p_Dst, *p_End;
	int p_Wild, p_NoCase;
};

struct dmc {			/* the matcher's: the end of the string */
	UBYTE *m_End;
	int m_NoCase;
};

struct dk {			/* what to match after a group */
	UBYTE *k_P;
	struct dk *k_Next;
	UBYTE *k_From;		/* a repetition: where its pass began */
};

static int
dup(c)
register int c;
{
	if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
		c -= 0x20;
	return (c);
}

static void
dput(pp, c)
register struct dpp *pp;
int c;
{
	if (pp->p_Dst < pp->p_End)
		*pp->p_Dst = c;
	pp->p_Dst++;
}

static void
dlit(pp, c)
register struct dpp *pp;
register int c;
{
	if (pp->p_NoCase)
		c = dup(c);
	if (c >= 0x80)
		dput(pp, DP_LIT);
	dput(pp, c);
}

/* An escaped or plain literal from the source. */
static void
dslit(pp)
register struct dpp *pp;
{
	register int c;

	if ((c = *pp->p_Src++) == '\'' && *pp->p_Src != '\0')
		c = *pp->p_Src++;
	dlit(pp, c);
}

static int
dend(c)
int c;
{
	return (c == '\0' || c == '|' || c == ')');
}

static void delem();

/* Elements up to the end of the source or the group. */
static void
dseq(pp, group)
register struct dpp *pp;
int group;
{
	while (*pp->p_Src != '\0' && !(group && (*pp->p_Src == '|' ||
	    *pp->p_Src == ')')))
		delem(pp);
}

/* An operand of # or ~, as a group unless it is one character. */
static void
dopnd(pp, wrap)
register struct dpp *pp;
int wrap;
{
	register int c;

	c = *pp->p_Src;
	if (wrap || c == '#' || c == '~') {
		dput(pp, P_ORSTART);
		delem(pp);
		dput(pp, P_OREND);
	} else
		delem(pp);
}

static void
delem(pp)
register struct dpp *pp;
{
	register int c;

	switch (c = *pp->p_Src++) {
	case '?':
		dput(pp, P_SINGLE);
		break;
	case '*':
		if (!(DOSBase->dl_Root->rn_Flags & RNF_WILDSTAR)) {
			dlit(pp, c);
			return;
		}
		dput(pp, P_ANY);
		break;
	case '#':
		if (*pp->p_Src == '?') {
			pp->p_Src++;
			dput(pp, P_ANY);
		} else if (dend(*pp->p_Src)) {
			dlit(pp, c);
			return;
		} else {
			dput(pp, P_REPBEG);
			dopnd(pp, 0);
		}
		break;
	case '~':
		if (dend(*pp->p_Src)) {
			dlit(pp, c);
			return;
		}
		dput(pp, P_NOT);
		dopnd(pp, *pp->p_Src != '(');
		break;
	case '(':
		dput(pp, P_ORSTART);
		for (;;) {
			dseq(pp, 1);
			if (*pp->p_Src != '|')
				break;
			pp->p_Src++;
			dput(pp, P_ORNEXT);
		}
		if (*pp->p_Src == ')')
			pp->p_Src++;
		dput(pp, P_OREND);
		break;
	case '[':
		if (*pp->p_Src == '~') {
			pp->p_Src++;
			dput(pp, P_NOTCLASS);
		} else
			dput(pp, P_CLASS);
		while (*pp->p_Src != '\0' && *pp->p_Src != ']') {
			dslit(pp);
			if (*pp->p_Src == '-' && pp->p_Src[1] != ']' &&
			    pp->p_Src[1] != '\0') {
				pp->p_Src++;
				dput(pp, DP_RANGE);
				dslit(pp);
			}
		}
		if (*pp->p_Src == ']')
			pp->p_Src++;
		dput(pp, P_REPEND);
		break;
	case '%':
		dput(pp, DP_EMPTY);
		break;
	default:
		pp->p_Src--;
		dslit(pp);
		return;
	}
	pp->p_Wild = 1;
}

/* pat parsed into buf (len bytes, 2 * strlen(pat) + 2 is enough): 1
 * with wildcards, 0 without, -1 when it does not fit. */
static LONG
dparse(pat, buf, len, nc)
char *pat;
UBYTE *buf;
LONG len;
int nc;
{
	struct dpp pp;

	pp.p_Src = (UBYTE *)pat;
	pp.p_Dst = buf;
	pp.p_End = buf + (UWORD)len;
	pp.p_Wild = 0;
	pp.p_NoCase = nc;
	dseq(&pp, 0);
	dput(&pp, 0);
	return (pp.p_Dst > pp.p_End ? -1L : (LONG)pp.p_Wild);
}

LONG
xParsePattern(db, pat, buf, len)
DB db;
char *pat;
UBYTE *buf;
LONG len;
{
	return (dparse(pat, buf, len, 0));
}

LONG
xParsePatNC(db, pat, buf, len)
DB db;
char *pat;
UBYTE *buf;
LONG len;
{
	return (dparse(pat, buf, len, 1));
}

static UBYTE *dskip();

/* The end of the alternative at p: its P_ORNEXT, P_OREND or NUL. */
static UBYTE *
dalt(p)
register UBYTE *p;
{
	while (*p != 0 && *p != P_ORNEXT && *p != P_OREND)
		p = dskip(p);
	return (p);
}

/* The element after the one at p. */
static UBYTE *
dskip(p)
register UBYTE *p;
{
	switch (*p) {
	case 0:
		return (p);
	case P_REPBEG:
	case P_NOT:
		return (dskip(p + 1));
	case P_ORSTART:
		do
			p = dalt(p + 1);
		while (*p == P_ORNEXT);
		return (*p != 0 ? p + 1 : p);
	case P_CLASS:
	case P_NOTCLASS:
		for (p++; *p != 0 && *p != P_REPEND; p++)
			if (*p == DP_LIT)
				p++;
		return (*p != 0 ? p + 1 : p);
	case DP_LIT:
		return (p + 2);
	}
	return (p + 1);
}

/* Whether the one-character element at p matches c. */
static int
done(m, p, c)
struct dmc *m;
register UBYTE *p;
register int c;
{
	register UBYTE *q;
	register int lo, hi, in;

	if (m->m_NoCase)
		c = dup(c);
	switch (*p) {
	case P_SINGLE:
		return (1);
	case DP_LIT:
		return (p[1] == c);
	case P_CLASS:
	case P_NOTCLASS:
		in = 0;
		for (q = p + 1; *q != 0 && *q != P_REPEND; ) {
			if (*q == DP_LIT)
				q++;
			lo = hi = *q++;
			if (*q == DP_RANGE) {
				if (*++q == DP_LIT)
					q++;
				hi = *q++;
			}
			if (c >= lo && c <= hi)
				in = 1;
		}
		return (in != (*p == P_NOTCLASS));
	}
	return (*p == c);
}

static int dgroup();

/* Whether the pattern at p, then k, matches s to the end. */
static int
dmatch(m, p, s, k)
register struct dmc *m;
register UBYTE *p, *s;
register struct dk *k;
{
	struct dk nk;
	struct dmc sub;
	register UBYTE *q;
	register UWORD i, n;

	for (;;) {
		switch (*p) {
		case 0:
		case P_ORNEXT:
		case P_OREND:
			if (k == NULL)
				return (s == m->m_End);
			if (k->k_From == s)
				return (0);	/* a pass that took nothing */
			p = k->k_P;
			k = k->k_Next;
			continue;
		case P_ANY:
			if (*++p == 0 && k == NULL)
				return (1);
			for (i = m->m_End - s + 1; i-- > 0; )
				if (dmatch(m, p, s + i, k))
					return (1);
			return (0);
		case P_REPBEG:
			q = dskip(++p);
			if (*p != P_ORSTART) {
				for (n = 0; s + n < m->m_End && done(m, p, s[n]);
				    n++)
					;
				for (n++; n-- > 0; )
					if (dmatch(m, q, s + n, k))
						return (1);
				return (0);
			}
			if (dmatch(m, q, s, k))
				return (1);
			nk.k_P = p - 1;
			nk.k_Next = k;
			nk.k_From = s;
			return (dgroup(m, p, s, &nk));
		case P_NOT:
			q = dskip(++p);
			sub.m_NoCase = m->m_NoCase;
			nk.k_P = (UBYTE *)"";
			nk.k_Next = NULL;
			nk.k_From = NULL;
			n = m->m_End - s;
			for (i = 0; i <= n; i++) {
				sub.m_End = s + i;
				if (!dgroup(&sub, p, s, &nk) &&
				    dmatch(m, q, s + i, k))
					return (1);
			}
			return (0);
		case P_ORSTART:
			nk.k_P = dskip(p);
			nk.k_Next = k;
			nk.k_From = NULL;
			return (dgroup(m, p, s, &nk));
		case DP_EMPTY:
			p++;
			continue;
		default:
			if (s >= m->m_End || !done(m, p, *s))
				return (0);
			s++;
			p = dskip(p);
		}
	}
}

/* Whether one alternative of the group at p, then k, matches s. */
static int
dgroup(m, p, s, k)
struct dmc *m;
register UBYTE *p;
UBYTE *s;
struct dk *k;
{
	do
		if (dmatch(m, p + 1, s, k))
			return (1);
	while (*(p = dalt(p + 1)) == P_ORNEXT);
	return (0);
}

static LONG
dmpat(pat, s, nc)
UBYTE *pat;
char *s;
int nc;
{
	struct dmc m;

	m.m_End = (UBYTE *)s + (UWORD)dstrlen(s);
	m.m_NoCase = nc;
	return (dmatch(&m, pat, (UBYTE *)s, (struct dk *)NULL) ? DOSTRUE :
	    DOSFALSE);
}

LONG
xMatchPattern(db, pat, s)
DB db;
UBYTE *pat;
char *s;
{
	return (dmpat(pat, s, 0));
}

LONG
xMatchPatNC(db, pat, s)
DB db;
UBYTE *pat;
char *s;
{
	return (dmpat(pat, s, 1));
}

/*
 * MatchFirst and MatchNext.  The pattern's longest wildcard-free
 * directory prefix is locked once; each later path part is an AChain
 * level searching the directory its parent matched: a pattern level
 * walks it with ExNext, a literal level locks its one name.  APF_DODIR
 * adds a level (DDF_AllBit) that returns everything in the directory
 * just returned; when it is done the directory comes back with
 * APF_DIDDIR.  an_String holds the prefix (the first level) or "",
 * then the parsed part (or the literal name).
 */

/* The parsed part or the literal name of a level. */
static UBYTE *
dapat(a)
struct AChain *a;
{
	return (a->an_String + (UWORD)dstrlen((char *)a->an_String) + 1);
}

/* The name a level matched. */
static char *
daname(a)
register struct AChain *a;
{
	return (a->an_Flags & DDF_PatternBit ? a->an_Info.fib_FileName :
	    (char *)dapat(a));
}

static int
dafinal(a)
register struct AChain *a;
{
	return (a->an_Child == NULL || (a->an_Child->an_Flags & DDF_AllBit));
}

/* The level's next match into an_Info: 0, or an error
 * (ERROR_NO_MORE_ENTRIES when there is none). */
static LONG
dastep(a)
register struct AChain *a;
{
	register BPTR l;
	LONG r;

	if (!(a->an_Flags & DDF_PatternBit)) {
		if (a->an_Flags & DDF_ExaminedBit)
			return (ERROR_NO_MORE_ENTRIES);
		a->an_Flags |= DDF_ExaminedBit;
		if ((l = dlockin(a->an_Lock, (char *)dapat(a))) == NULL)
			return (xIoErr(DOSBase));
		r = xExamine(DOSBase, l, &a->an_Info);
		xUnLock(DOSBase, l);
		return (r != DOSFALSE ? 0L : xIoErr(DOSBase));
	}
	if (!(a->an_Flags & DDF_ExaminedBit)) {
		a->an_Flags |= DDF_ExaminedBit;
		if (xExamine(DOSBase, a->an_Lock, &a->an_Info) == DOSFALSE)
			return (xIoErr(DOSBase));
	}
	while (xExNext(DOSBase, a->an_Lock, &a->an_Info) != DOSFALSE)
		if ((a->an_Flags & DDF_AllBit) || dmpat(dapat(a),
		    a->an_Info.fib_FileName, 1) != DOSFALSE)
			return (0);
	return (xIoErr(DOSBase));
}

/* The match at ap_Last: ap_Info and ap_Buf. */
static LONG
dafound(ap)
register struct AnchorPath *ap;
{
	register struct AChain *a;
	register char *d, *s, *e;

	CopyMem(&ap->ap_Last->an_Info, &ap->ap_Info,
	    (ULONG)sizeof(struct FileInfoBlock));
	if (ap->ap_Strlen <= 0)
		return (0);
	d = (char *)ap->ap_Buf;
	e = d + ap->ap_Strlen - 1;
	for (s = (char *)ap->ap_Base->an_String; *s != '\0' && d < e; )
		*d++ = *s++;
	for (a = ap->ap_Base; ; a = a->an_Child) {
		s = daname(a);
		if (*s != '\0' && d > (char *)ap->ap_Buf && d[-1] != ':' &&
		    d[-1] != '/' && d < e)
			*d++ = '/';
		while (*s != '\0' && d < e)
			*d++ = *s++;
		if (a == ap->ap_Last)
			break;
	}
	*d = '\0';
	return (*s != '\0' ? ERROR_BUFFER_OVERFLOW : 0L);
}

static LONG
danext(ap)
register struct AnchorPath *ap;
{
	register struct AChain *a, *c;
	register ULONG b;
	LONG r;

	for (;;) {
		a = ap->ap_Last;
		if (ap->ap_BreakBits != 0 && (b = SetSignal(0L,
		    (ULONG)ap->ap_BreakBits) & ap->ap_BreakBits) != 0) {
			SetSignal(0L, b);
			ap->ap_FoundBreak = b;
			return (ERROR_BREAK);
		}
		if ((ap->ap_Flags & (APF_DODIR | APF_DIDDIR)) == APF_DODIR &&
		    (a->an_Flags & DDF_ExaminedBit) &&
		    a->an_Info.fib_DirEntryType > 0 && dafinal(a)) {
			ap->ap_Flags &= ~APF_DODIR;
			if ((c = (struct AChain *)AllocVec((ULONG)sizeof(*c) + 2,
			    MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
				ap->ap_Flags |= APF_NOMEMERR;
				return (ERROR_NO_FREE_STORE);
			}
			if ((c->an_Lock = dlockin(a->an_Lock, daname(a))) ==
			    NULL) {
				r = xIoErr(DOSBase);
				FreeVec(c);
				return (r);
			}
			c->an_Flags = DDF_AllBit | DDF_PatternBit;
			c->an_Parent = a;
			a->an_Child = c;
			ap->ap_Last = c;
			ap->ap_Flags |= APF_DirChanged;
			continue;
		}
		ap->ap_Flags &= ~(APF_DODIR | APF_DIDDIR);
		if ((r = dastep(a)) != 0) {
			if (a == ap->ap_Base)
				return (r);
			xUnLock(DOSBase, a->an_Lock);
			a->an_Lock = NULL;
			a->an_Flags &= ~DDF_ExaminedBit;
			ap->ap_Last = c = a->an_Parent;
			ap->ap_Flags |= APF_DirChanged;
			if (a->an_Flags & DDF_AllBit) {
				c->an_Child = NULL;
				FreeVec(a);
				ap->ap_Flags |= APF_DIDDIR;
				return (dafound(ap));
			}
			continue;
		}
		if (dafinal(a))
			return (dafound(ap));
		c = a->an_Child;
		if (a->an_Info.fib_DirEntryType > 0 &&
		    (c->an_Lock = dlockin(a->an_Lock, daname(a))) != NULL) {
			ap->ap_Last = c;
			ap->ap_Flags |= APF_DirChanged;
		}
	}
}

/* Whether the n bytes at s hold a wildcard. */
static int
dwild(s, n)
register char *s;
register int n;
{
	for (; n > 0; n--, s++)
		switch (*s) {
		case '\'':
			s++;
			n--;
			break;
		case '*':
			if (!(DOSBase->dl_Root->rn_Flags & RNF_WILDSTAR))
				break;
		case '?': case '#': case '(': case '|': case '~': case '[':
		case '%':
			return (1);
		}
	return (0);
}

LONG
xMatchFirst(db, pat, ap)
DB db;
char *pat;
register struct AnchorPath *ap;
{
	register struct AChain *a, *up;
	register char *p, *s;
	char *pre, *d, *q;
	int n, k;
	LONG r;
	BPTR l;

	ap->ap_Base = ap->ap_Last = NULL;
	ap->ap_Flags &= ~(APF_ITSWILD | APF_DODIR | APF_DIDDIR |
	    APF_NOMEMERR | APF_DirChanged);
	ap->ap_FoundBreak = 0;
	/* the prefix: whole parts before the first wild part */
	p = dcolon(pat);
	pre = s = p != NULL ? p + 1 : pat;
	for (;;) {
		for (p = s; *p != '\0' && *p != '/'; p++)
			;
		if (*p == '\0' || dwild(s, (int)(p - s)))
			break;
		pre = s = p + 1;
	}
	/* one level per part after it, the prefix in the first */
	up = NULL;
	for (s = pre; ; s = p + 1) {
		for (p = s; *p != '\0' && *p != '/'; p++)
			;
		n = p - s;
		k = up == NULL ? pre - pat : 0;
		if ((a = (struct AChain *)AllocVec((ULONG)sizeof(*a) + k + 4 +
		    3 * n, MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
			ap->ap_Flags |= APF_NOMEMERR;
			r = ERROR_NO_FREE_STORE;
			goto fail;
		}
		if (up == NULL)
			ap->ap_Base = a;
		else
			up->an_Child = a;
		CopyMem(pat, a->an_String, (ULONG)k);
		d = (char *)a->an_String + k + 1;
		q = d + 2 * n + 2;
		CopyMem(s, q, (ULONG)n);
		if ((r = dparse(q, (UBYTE *)d, 2L * n + 2, 1)) < 0) {
			r = ERROR_BUFFER_OVERFLOW;
			goto fail;
		}
		if (r > 0) {
			a->an_Flags = DDF_PatternBit;
			ap->ap_Flags |= APF_ITSWILD;
		} else {
			for (; *q != '\0'; q++) {
				if (*q == '\'' && q[1] != '\0')
					q++;
				*d++ = *q;
			}
			*d = '\0';
		}
		a->an_Parent = up;
		up = a;
		if (*p == '\0')
			break;
	}
	a = ap->ap_Base;
	if (pre == pat)
		l = xDupLock(db, dme()->pr_CurrentDir);
	else {
		pre = (char *)a->an_String;
		l = xLock(db, pre, SHARED_LOCK);
		if (l == NULL) {
			r = xIoErr(db);
			goto fail;
		}
	}
	a->an_Lock = l;
	ap->ap_Last = a;
	ap->ap_Flags |= APF_DirChanged;
	r = danext(ap);
	dseterr(r);
	return (r);
fail:
	xMatchEnd(db, ap);
	dseterr(r);
	return (r);
}

LONG
xMatchNext(db, ap)
DB db;
register struct AnchorPath *ap;
{
	LONG r;

	ap->ap_Flags &= ~APF_DirChanged;
	r = ap->ap_Base != NULL ? danext(ap) : ERROR_NO_MORE_ENTRIES;
	dseterr(r);
	return (r);
}

void
xMatchEnd(db, ap)
DB db;
register struct AnchorPath *ap;
{
	register struct AChain *a, *n;

	for (a = ap->ap_Base; a != NULL; a = n) {
		n = a->an_Child;
		xUnLock(db, a->an_Lock);
		FreeVec(a);
	}
	ap->ap_Base = ap->ap_Last = NULL;
}
