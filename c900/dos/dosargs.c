/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * dos.library argument parsing, as AmigaDOS V36: ReadArgs, FreeArgs,
 * FindArg and ReadItem.  A template is comma-separated items, each a
 * name with "=" aliases (or none) and the modifiers /A (required), /K
 * (keyword only), /S (switch), /T (toggle), /N (number), /M (the rest
 * of the words) and /F (the rest of the line).  ReadArgs parses the
 * command line (GetArgStr) or the caller's CSource and fills one LONG
 * per item: a string, a LONG * for /N, DOSTRUE for /S, a
 * NULL-terminated string array for /M.  Items not given are left as
 * the caller set them.  Everything lives in one block on RDA_DAList
 * (or the caller's RDA_Buffer), which FreeArgs releases.
 */
#include "dosint.h"

#define TA_A	1
#define TA_K	2
#define TA_S	4
#define TA_T	8
#define TA_N	16
#define TA_M	32
#define TA_F	64
#define TA_SET	128		/* given */

#define RDAF_ALLOC	0x80000000L	/* private: ReadArgs made the RDArgs */
#define PROMPTMAX	256		/* a line typed after "?" */

static int
dlc(c)
register int c;
{
	return (c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c);
}

/* Whether template name t (up to "=", "/", "," or NUL) is s, ignoring
 * case. */
static int
dkeyeq(t, s)
register char *t, *s;
{
	for (; *s != '\0'; t++, s++)
		if (dlc(*t) != dlc(*s))
			return (0);
	return (*t == '\0' || *t == '=' || *t == '/' || *t == ',');
}

/* The index of the item keyword names in template, or -1. */
LONG
xFindArg(db, tpl, key)
DB db;
char *tpl, *key;
{
	register char *p;
	register LONG n;

	for (n = 0, p = tpl; ; n++) {
		for (;;) {
			if (dkeyeq(p, key))
				return (n);
			while (*p != '\0' && *p != '=' && *p != '/' &&
			    *p != ',')
				p++;
			if (*p != '=')
				break;
			p++;
		}
		while (*p != '\0' && *p != ',')
			p++;
		if (*p++ == '\0')
			return (-1);
	}
}

/* The next byte of cs, or of fh (cs NULL); -1 at the end. */
static LONG
drget(cs, fh)
register struct CSource *cs;
BPTR fh;
{
	if (cs == NULL)
		return (xFGetC(DOSBase, fh));
	if (cs->CS_CurChr >= cs->CS_Length)
		return (-1);
	return ((LONG)cs->CS_Buffer[(UWORD)cs->CS_CurChr++]);
}

static void
drunget(cs, fh, c)
register struct CSource *cs;
BPTR fh;
LONG c;
{
	if (cs == NULL)
		xUnGetC(DOSBase, fh, -1L);
	else if (c >= 0)
		cs->CS_CurChr--;
}

/*
 * The next word of cs (Input() when NULL) into buf, at most max bytes
 * with its NUL.  Blanks separate words; "=" is a word of its own
 * (ITEM_EQUAL); a word starting with a quote runs to the next, inside
 * which *" is a quote, *N a newline, *E an escape and *x x.  A newline
 * or the end gives ITEM_NOTHING and stays unread.
 */
LONG
xReadItem(db, buf, max, cs)
DB db;
char *buf;
LONG max;
struct CSource *cs;
{
	register char *p;
	register LONG c, n;
	BPTR fh;

	fh = cs == NULL ? xInput(db) : NULL;
	if (max <= 0)
		return (ITEM_ERROR);
	*buf = '\0';
	do
		c = drget(cs, fh);
	while (c == ' ' || c == '\t');
	if (c <= 0 || c == '\n') {
		drunget(cs, fh, c);
		return (ITEM_NOTHING);
	}
	if (c == '=')
		return (ITEM_EQUAL);
	p = buf;
	n = max - 1;
	if (c == '"') {
		for (;;) {
			if ((c = drget(cs, fh)) == '"')
				break;
			if (c == '*' && (c = drget(cs, fh)) > 0 && c != '\n') {
				if (c == 'N' || c == 'n')
					c = '\n';
				else if (c == 'E' || c == 'e')
					c = 0x1B;
			} else if (c <= 0 || c == '\n')
				n = 0;
			if (n-- <= 0) {
				drunget(cs, fh, c);
				*p = '\0';
				return (ITEM_ERROR);
			}
			*p++ = c;
		}
		*p = '\0';
		return (ITEM_QUOTED);
	}
	do {
		if (n-- <= 0) {
			*p = '\0';
			return (ITEM_ERROR);
		}
		*p++ = c;
		c = drget(cs, fh);
	} while (c > 0 && c != ' ' && c != '\t' && c != '\n' && c != '=');
	if (c != ' ' && c != '\t')
		drunget(cs, fh, c);
	*p = '\0';
	return (ITEM_UNQUOTED);
}

/* n bytes on rda's DAList, cleared; NULL without memory. */
static UBYTE *
dralloc(rda, n)
register struct RDArgs *rda;
LONG n;
{
	register LONG *p;

	if ((p = (LONG *)AllocVec((ULONG)n + 4, MEMF_PUBLIC | MEMF_CLEAR))
	    == NULL) {
		dseterr(ERROR_NO_FREE_STORE);
		return (NULL);
	}
	*p = rda->RDA_DAList;
	rda->RDA_DAList = (LONG)p;
	return ((UBYTE *)(p + 1));
}

void
xFreeArgs(db, rda)
DB db;
register struct RDArgs *rda;
{
	register LONG *p, *n;

	if (rda == NULL)
		return;
	for (p = (LONG *)rda->RDA_DAList; p != NULL; p = n) {
		n = (LONG *)*p;
		FreeVec(p);
	}
	rda->RDA_DAList = 0;
	if (rda->RDA_Flags & RDAF_ALLOC)
		xFreeDosObj(db, (ULONG)DOS_RDARGS, rda);
}

/* Whether cs holds just "?" from its current byte. */
static int
dquery(cs)
register struct CSource *cs;
{
	register UBYTE *p, *e;

	p = cs->CS_Buffer + (UWORD)cs->CS_CurChr;
	e = cs->CS_Buffer + (UWORD)cs->CS_Length;
	while (p < e && (*p == ' ' || *p == '\t'))
		p++;
	if (p == e || *p++ != '?')
		return (0);
	while (p < e && (*p == ' ' || *p == '\t'))
		p++;
	return (p == e || *p == '\n' || *p == '\0');
}

/* The template's items into fl[]: their count, or -1 for a bad one. */
static int
dtpl(tpl, fl)
char *tpl;
UBYTE *fl;
{
	static char mods[] = "AKSTNMF";
	register char *p, *m;
	register int n, f;

	for (n = 0, p = tpl; ; n++) {
		while (*p != '\0' && *p != '/' && *p != ',')
			p++;
		for (f = 0; *p == '/'; p += 2) {
			for (m = mods; *m != '\0' && dlc(*m) != dlc(p[1]); m++)
				;
			if (*m == '\0')
				return (-1);
			f |= 1 << (int)(m - mods);
		}
		if (fl != NULL)
			fl[n] = f;
		if (*p++ != ',')
			return (*--p == '\0' ? n + 1 : -1);
	}
}

/* The rest of cs's line from byte at, blanks trimmed, at d. */
static void
drest(cs, at, d)
register struct CSource *cs;
LONG at;
register char *d;
{
	register UBYTE *p, *e;

	p = cs->CS_Buffer + (UWORD)at;
	for (e = p; e < cs->CS_Buffer + (UWORD)cs->CS_Length && *e != '\n' &&
	    *e != '\0'; e++)
		;
	cs->CS_CurChr = e - cs->CS_Buffer;
	while (p < e && (*p == ' ' || *p == '\t' || *p == '='))
		p++;
	while (e > p && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	while (p < e)
		*d++ = *p++;
	*d = '\0';
}

/* Skip blanks in cs: the next byte's index. */
static LONG
dblank(cs)
register struct CSource *cs;
{
	while (cs->CS_CurChr < cs->CS_Length &&
	    (cs->CS_Buffer[(UWORD)cs->CS_CurChr] == ' ' ||
	    cs->CS_Buffer[(UWORD)cs->CS_CurChr] == '\t'))
		cs->CS_CurChr++;
	return (cs->CS_CurChr);
}

struct RDArgs *
xReadArgs(db, tpl, arr, rda)
DB db;
char *tpl;
LONG *arr;
struct RDArgs *rda;
{
	struct CSource line;
	register struct CSource *cs;
	register UBYTE *fl;
	register char *sp;
	register int k;
	char *send;
	LONG *multi, *num, v, t, at, err, need, len;
	int n, nm, mi, f;
	UBYTE *blk;
	char *s;

	if (rda == NULL) {
		if ((rda = (struct RDArgs *)xAllocDosObj(db, (ULONG)DOS_RDARGS,
		    NULL)) == NULL)
			return (NULL);
		rda->RDA_Flags = RDAF_ALLOC;
	}
	rda->RDA_DAList = 0;
	cs = &rda->RDA_Source;
	if (cs->CS_Buffer == NULL) {
		cs = &line;
		line.CS_Buffer = (UBYTE *)xGetArgStr(db);
		if (line.CS_Buffer == NULL)
			line.CS_Buffer = (UBYTE *)"";
		line.CS_Length = dstrlen((char *)line.CS_Buffer);
		line.CS_CurChr = 0;
	}
	err = ERROR_NO_FREE_STORE;
	if (((rda->RDA_Flags & RDAF_STDIN) || (!(rda->RDA_Flags &
	    RDAF_NOPROMPT) && dquery(cs)))) {
		if ((blk = dralloc(rda, (LONG)PROMPTMAX)) == NULL)
			goto fail;
		for (;;) {
			if (!(rda->RDA_Flags & RDAF_STDIN)) {
				xFPuts(db, xOutput(db), tpl);
				xFPuts(db, xOutput(db), ": ");
				xFlush(db, xOutput(db));
			}
			if (xFGets(db, xInput(db), (char *)blk, (ULONG)PROMPTMAX)
			    == NULL)
				*blk = '\0';
			cs = &line;
			line.CS_Buffer = blk;
			line.CS_Length = dstrlen((char *)blk);
			line.CS_CurChr = 0;
			if (rda->RDA_ExtHelp == NULL || (rda->RDA_Flags &
			    RDAF_STDIN) || !dquery(cs))
				break;
			xFPuts(db, xOutput(db), (char *)rda->RDA_ExtHelp);
		}
	}
	err = ERROR_BAD_TEMPLATE;
	if ((n = dtpl(tpl, (UBYTE *)NULL)) < 0)
		goto fail;
	/* the block: /M's array, /N's longs, the item flags, the strings */
	len = cs->CS_Length - cs->CS_CurChr;
	nm = 0;
	need = 0;
	for (s = tpl; *s != '\0'; s++)
		if (s[0] == '/' && (s[1] == 'M' || s[1] == 'm'))
			nm++;
		else if (s[0] == '/' && (s[1] == 'N' || s[1] == 'n'))
			need++;
	if (nm > 1)
		goto fail;
	if (nm != 0) {
		nm = len / 2 + 2;
		need += nm;
	}
	need = (nm + 1 + need) * 4 + ((n + 1) & ~1) + len + 2;
	err = ERROR_NO_FREE_STORE;
	if ((blk = rda->RDA_Buffer) != NULL && rda->RDA_BufSiz >= need + 1)
		blk = (UBYTE *)(((ULONG)blk + 1) & ~1L);
	else if ((rda->RDA_Flags & RDAF_NOALLOC) ||
	    (blk = dralloc(rda, need)) == NULL)
		goto fail;
	multi = (LONG *)blk;
	num = multi + nm + 1;
	fl = (UBYTE *)blk + (UWORD)(need - ((n + 1) & ~1) - len - 2);
	dtpl(tpl, fl);
	sp = (char *)fl + ((n + 1) & ~1);
	send = sp + (UWORD)len + 2;
	nm = 0;
	for (mi = 0; mi < n && !(fl[mi] & TA_M); mi++)
		;
	for (;;) {
		at = dblank(cs);
		t = xReadItem(db, sp, (LONG)(send - sp), cs);
		err = ERROR_UNMATCHED_QUOTES;
		if (t == ITEM_ERROR)
			goto fail;
		if (t == ITEM_NOTHING)
			break;
		err = ERROR_TOO_MANY_ARGS;
		if (t == ITEM_EQUAL)
			goto fail;
		k = t == ITEM_UNQUOTED ? (int)xFindArg(db, tpl, sp) : -1;
		if (k >= 0) {
			f = fl[k];
			if (f & (TA_S | TA_T)) {
				arr[k] = (f & TA_S) || arr[k] == 0 ? DOSTRUE : 0;
				fl[k] |= TA_SET;
				continue;
			}
			if (!(f & TA_F)) {
				if ((t = xReadItem(db, sp, (LONG)(send - sp), cs)) ==
				    ITEM_EQUAL)
					t = xReadItem(db, sp, (LONG)(send - sp), cs);
				err = ERROR_KEY_NEEDS_ARG;
				if (t <= 0)
					goto fail;
			} else
				at = cs->CS_CurChr;
		} else {
			for (k = 0; k < n; k++)
				if (!((f = fl[k]) & (TA_S | TA_T | TA_K)) &&
				    ((f & TA_M) || !(f & TA_SET)))
					break;
			if (k == n)
				goto fail;
			f = fl[k];
		}
		if (f & TA_F)
			drest(cs, at, sp);
		v = (LONG)sp;
		if (f & TA_N) {
			t = xStrToLong(db, sp, num);
			err = ERROR_BAD_NUMBER;
			if (t < 0 || sp[(UWORD)t] != '\0')
				goto fail;
			v = (LONG)num;
			num++;
		}
		if (f & TA_M)
			multi[nm++] = v;
		else
			arr[k] = v;
		fl[k] |= TA_SET;
		sp += dstrlen(sp) + 1;
		if (f & TA_F)
			break;
	}
	/* a required item after /M takes /M's last word, as on the Amiga */
	for (k = n - 1; k > mi; k--)
		if ((fl[k] & (TA_A | TA_K | TA_S | TA_T | TA_SET)) == TA_A &&
		    nm > 0 && !((fl[k] ^ fl[mi]) & TA_N)) {
			arr[k] = multi[--nm];
			fl[k] |= TA_SET;
		}
	if (mi < n) {
		if (nm > 0) {
			multi[nm] = 0;
			arr[mi] = (LONG)multi;
		} else
			fl[mi] &= ~TA_SET;
	}
	err = ERROR_REQUIRED_ARG_MISSING;
	for (k = 0; k < n; k++)
		if ((fl[k] & (TA_A | TA_SET)) == TA_A)
			goto fail;
	return (rda);
fail:
	xFreeArgs(db, rda);
	dseterr(err);
	return (NULL);
}
