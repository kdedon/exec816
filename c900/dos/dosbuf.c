/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * dos.library buffered I/O, as AmigaDOS V36: FGetC, UnGetC, FGets,
 * FRead, FPutC, FPuts, FWrite, Flush, SetVBuf and the formatted and
 * default-output calls built on them.  The buffer belongs to the
 * handle (fh_Buf, fh_Pos, fh_End, struct DFH), so every user of a
 * handle shares it, and Read, Write, Seek and Close settle it first
 * (dosio.c).  It is allocated on the first buffered call: 512 bytes
 * for a file, 256 for a console.  A console's output is line buffered
 * (BUF_LINE), anything else's fully (BUF_FULL).
 */
#include "dosint.h"

#define F(fh)	((struct DFH *)(fh))
#define BUF(f)	((UBYTE *)(f)->f_FH.fh_Buf)
#define POS(f)	((f)->f_FH.fh_Pos)
#define END(f)	((f)->f_FH.fh_End)

/* Write out pending output, or give back read-ahead: a file seeks back
 * to the logical position.  DOSFALSE when the write fails. */
LONG
dflush(f)
register struct DFH *f;
{
	register LONG n;

	n = POS(f);
	if (f->f_Flags & DFF_WRITE) {
		POS(f) = 0;
		if (n > 0 && dfhio(f, (LONG)ACTION_WRITE, BUF(f), n) != n)
			return (DOSFALSE);
	} else if (n < END(f)) {
		n -= END(f);
		POS(f) = END(f) = 0;
		if (f->f_FH.fh_Port == NULL && f->f_FH.fh_Type != NULL)
			xDoPkt(DOSBase, f->f_FH.fh_Type, (LONG)ACTION_SEEK,
			    f->f_FH.fh_Args, n, OFFSET_CURRENT, 0L, 0L);
	}
	return (DOSTRUE);
}

/* The handle's buffer, allocated on first use; NULL without memory. */
static UBYTE *
dbuf(f)
register struct DFH *f;
{
	register UBYTE *b;

	if ((b = BUF(f)) == NULL) {
		if (f->f_Size == 0)
			f->f_Size = f->f_FH.fh_Port != NULL ? DFCONSIZE : DFBUFSIZE;
		if ((b = (UBYTE *)AllocVec((ULONG)f->f_Size, MEMF_PUBLIC))
		    == NULL) {
			dseterr(ERROR_NO_FREE_STORE);
			return (NULL);
		}
		f->f_FH.fh_Buf = (LONG)b;
		f->f_Flags |= DFF_OWNBUF;
		POS(f) = END(f) = 0;
	}
	return (b);
}

/* Refill the buffer for reading: the bytes now in it, 0 at the end, -1
 * on an error. */
static LONG
dfill(f)
register struct DFH *f;
{
	register LONG n;

	if (f->f_Flags & DFF_WRITE) {
		if (dflush(f) == DOSFALSE)
			return (-1);
		f->f_Flags &= ~DFF_WRITE;
	}
	if (dbuf(f) == NULL)
		return (-1);
	n = dfhio(f, (LONG)ACTION_READ, BUF(f), f->f_Size);
	POS(f) = 0;
	END(f) = n > 0 ? n : 0;
	return (n);
}

/* Put the buffer in write state: NULL without one. */
static UBYTE *
dwbuf(f)
register struct DFH *f;
{
	if (!(f->f_Flags & DFF_WRITE)) {
		if (POS(f) < END(f))
			dflush(f);
		if (dbuf(f) == NULL)
			return (NULL);
		POS(f) = END(f) = 0;
		f->f_Flags |= DFF_WRITE;
	}
	return (BUF(f));
}

/* n bytes from s through the buffer: n, or -1 on an error.  A block at
 * least a buffer long goes straight out. */
LONG
dbwrite(f, s, n)
register struct DFH *f;
register UBYTE *s;
LONG n;
{
	register UBYTE *d, *e;
	register LONG k;
	LONG left;
	int nl;

	if (f == NULL || dwbuf(f) == NULL)
		return (-1);
	nl = 0;
	for (left = n; left > 0; left -= k) {
		if (POS(f) == 0 && left >= f->f_Size)
			return (dfhio(f, (LONG)ACTION_WRITE, s, left) == left ?
			    n : -1L);
		k = f->f_Size - POS(f);
		if (k > left)
			k = left;
		d = BUF(f) + (UWORD)POS(f);
		POS(f) += k;
		for (e = d + (UWORD)k; d < e; )
			if ((*d++ = *s++) == '\n')
				nl = 1;
		if (POS(f) == f->f_Size && dflush(f) == DOSFALSE)
			return (-1);
	}
	if ((f->f_Mode == BUF_NONE || (nl && f->f_Mode == BUF_LINE)) &&
	    dflush(f) == DOSFALSE)
		return (-1);
	return (n);
}

/* One byte out: the byte, or -1.  While the byte cannot end the
 * buffer's run, it is only stored. */
static LONG
dputc(f, c)
register struct DFH *f;
int c;
{
	register LONG n;
	UBYTE b;

	if (f != NULL && (f->f_Flags & DFF_WRITE) &&
	    (n = POS(f) + 1) < f->f_Size && f->f_Mode != BUF_NONE &&
	    (c != '\n' || f->f_Mode == BUF_FULL)) {
		BUF(f)[(UWORD)POS(f)] = c;
		POS(f) = n;
		return ((LONG)(c & 0xFF));
	}
	b = c;
	return (dbwrite(f, &b, 1L) == 1 ? (LONG)b : -1L);
}

LONG
xFGetC(db, fh)
DB db;
BPTR fh;
{
	register struct DFH *f;

	f = F(fh);
	if (f->f_Flags & DFF_UNEOF) {
		f->f_Flags &= ~DFF_UNEOF;
		f->f_Flags |= DFF_EOF;
		return (-1);
	}
	if (POS(f) >= END(f) || (f->f_Flags & DFF_WRITE)) {
		if (dfill(f) <= 0) {
			f->f_Flags |= DFF_EOF;
			return (-1);
		}
	}
	return ((LONG)BUF(f)[(UWORD)POS(f)++]);
}

/* Push back the last byte read (character -1) or character: what was
 * pushed back, or 0 when nothing can be.  After the end, -1 makes the
 * next FGetC return the end again. */
LONG
xUnGetC(db, fh, c)
DB db;
BPTR fh;
LONG c;
{
	register struct DFH *f;

	f = F(fh);
	if (c == -1 && (f->f_Flags & DFF_EOF)) {
		f->f_Flags &= ~DFF_EOF;
		f->f_Flags |= DFF_UNEOF;
		return (-1);
	}
	if ((f->f_Flags & DFF_WRITE) || BUF(f) == NULL || POS(f) <= 0)
		return (0);
	POS(f)--;
	if (c != -1)
		BUF(f)[(UWORD)POS(f)] = c;
	return ((LONG)BUF(f)[(UWORD)POS(f)]);
}

/* Up to len - 1 bytes through the first newline, NUL-terminated; NULL at
 * the end with nothing read. */
char *
xFGets(db, fh, buf, len)
DB db;
BPTR fh;
char *buf;
ULONG len;
{
	register char *p;
	register LONG c;

	for (p = buf; len > 1 && (c = xFGetC(db, fh)) >= 0; len--)
		if ((*p++ = c) == '\n')
			break;
	*p = '\0';
	return (p == buf ? NULL : buf);
}

/* number blocks of blocklen bytes: the whole blocks read. */
LONG
xFRead(db, fh, block, blocklen, number)
DB db;
BPTR fh;
UBYTE *block;
ULONG blocklen, number;
{
	register struct DFH *f;
	register LONG k, left, n;

	f = F(fh);
	if (blocklen == 0)
		return (0);
	n = 0;
	for (left = blocklen * number; left > 0; left -= k) {
		if (POS(f) >= END(f) || (f->f_Flags & DFF_WRITE)) {
			if (dbuf(f) != NULL && left >= f->f_Size &&
			    !(f->f_Flags & DFF_WRITE)) {
				k = dfhio(f, (LONG)ACTION_READ, block, left);
				if (k > 0)
					n += k;
				break;
			}
			if (dfill(f) <= 0)
				break;
		}
		k = END(f) - POS(f);
		if (k > left)
			k = left;
		CopyMem(BUF(f) + (UWORD)POS(f), block, (ULONG)k);
		POS(f) += k;
		block += (UWORD)k;
		n += k;
	}
	return (n / (LONG)blocklen);
}

LONG
xFPutC(db, fh, c)
DB db;
BPTR fh;
LONG c;
{
	return (dputc(F(fh), (int)c));
}

/* 0, or -1 on an error. */
LONG
xFPuts(db, fh, s)
DB db;
BPTR fh;
char *s;
{
	register LONG n;

	n = dstrlen(s);
	return (dbwrite(F(fh), (UBYTE *)s, n) == n ? 0L : -1L);
}

/* The whole blocks written, 0 on an error. */
LONG
xFWrite(db, fh, block, blocklen, number)
DB db;
BPTR fh;
UBYTE *block;
ULONG blocklen, number;
{
	return (dbwrite(F(fh), block, (LONG)(blocklen * number)) < 0 ? 0L :
	    (LONG)number);
}

LONG
xFlush(db, fh)
DB db;
BPTR fh;
{
	return (fh != NULL ? dflush(F(fh)) : DOSTRUE);
}

/* Use buff (or a new buffer when buff is NULL) of size bytes, if either
 * is given, and buffering mode type: 0, or -1 on a failure. */
LONG
xSetVBuf(db, fh, buff, type, size)
DB db;
BPTR fh;
UBYTE *buff;
LONG type, size;
{
	register struct DFH *f;
	int own;

	f = F(fh);
	if (type < BUF_LINE || type > BUF_NONE || dflush(f) == DOSFALSE)
		return (-1);
	POS(f) = END(f) = 0;
	f->f_Flags &= ~DFF_WRITE;
	if (buff != NULL || size > 0) {
		if (size <= 0)
			size = DFBUFSIZE;
		own = buff == NULL;
		if (own && (buff = (UBYTE *)AllocVec((ULONG)size,
		    MEMF_PUBLIC)) == NULL)
			return (-1);
		if (f->f_Flags & DFF_OWNBUF)
			FreeVec(BUF(f));
		f->f_Flags = (f->f_Flags & ~DFF_OWNBUF) | (own ? DFF_OWNBUF : 0);
		f->f_FH.fh_Buf = (LONG)buff;
		f->f_Size = size;
	}
	f->f_Mode = type;
	return (0);
}

/* Formatted output: RawDoFmt's bytes through the buffer. */
struct dfmt {
	struct DFH *f_File;
	LONG f_Count;
};

static void
dfput(c, f)
int c;
register struct dfmt *f;
{
	if (c == 0)
		return;
	if (f->f_Count >= 0 && dputc(f->f_File, c) >= 0)
		f->f_Count++;
	else
		f->f_Count = -1;
}

/* The bytes written, or -1. */
LONG
xVFPrintf(db, fh, fmt, argv)
DB db;
BPTR fh;
char *fmt;
APTR argv;
{
	struct dfmt f;

	f.f_File = F(fh);
	f.f_Count = fh != NULL ? 0 : -1;
	RawDoFmt(fmt, argv, (APTR)dfput, (APTR)&f);
	return (f.f_Count);
}

LONG
xVPrintf(db, fmt, argv)
DB db;
char *fmt;
APTR argv;
{
	return (xVFPrintf(db, xOutput(db), fmt, argv));
}

LONG
xPutStr(db, s)
DB db;
char *s;
{
	return (xFPuts(db, xOutput(db), s));
}

LONG
xWriteChars(db, buf, len)
DB db;
char *buf;
ULONG len;
{
	return (dbwrite(F(xOutput(db)), (UBYTE *)buf, (LONG)len));
}

/* A field width: a digit or a letter (A = 10). */
static int
dwidth(p)
char **p;
{
	register int c;

	c = *(*p)++;
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'A' && c <= 'Z')
		return (c - 'A' + 10);
	if (c >= 'a' && c <= 'z')
		return (c - 'a' + 10);
	(*p)--;
	return (0);
}

/*
 * BCPL's writef: each %S (string), %Tn (string padded to n), %C
 * (character), %N (decimal), %In (decimal in n), %Un (unsigned in n) or
 * %Xn (hex, n digits) takes the next long of argv; %$ skips one.  The
 * bytes written, or -1.
 */
LONG
xVFWritef(db, fh, fmt, argv)
DB db;
BPTR fh;
char *fmt;
LONG *argv;
{
	struct dfmt f;
	char spec[8], *p;
	register char *q;
	register int c, w;
	LONG a[1];

	f.f_File = F(fh);
	f.f_Count = fh != NULL ? 0 : -1;
	for (p = fmt; (c = *p++) != '\0'; ) {
		if (c != '%') {
			dfput(c, &f);
			continue;
		}
		if ((c = *p++) >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		q = spec;
		*q++ = '%';
		w = 0;
		switch (c) {
		case '$':
			argv++;
			continue;
		case 'X':
			*q++ = '0';
			w = dwidth(&p);
			break;
		case 'T':
			*q++ = '-';
		case 'I':
		case 'U':
			w = dwidth(&p);
		case 'S':
		case 'C':
		case 'N':
			break;
		default:
			dfput('%', &f);
			if (c == '\0')
				return (f.f_Count);
			dfput(c, &f);
			continue;
		}
		if (w >= 10)
			*q++ = '0' + w / 10;
		if (w > 0)
			*q++ = '0' + w % 10;
		if (c == 'S' || c == 'T' || c == 'C')
			*q++ = c == 'C' ? 'c' : 's';
		else {
			*q++ = 'l';
			*q++ = c == 'U' ? 'u' : c == 'X' ? 'x' : 'd';
		}
		*q = '\0';
		a[0] = *argv++;
		if (c == 'C')
			a[0] = a[0] << 16;	/* %c takes a word */
		RawDoFmt(spec, (APTR)a, (APTR)dfput, (APTR)&f);
	}
	return (f.f_Count);
}
