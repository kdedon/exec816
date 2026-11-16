/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Diagnostic console output on the polled SCC: KPutStr, RawPutChar and
 * KPrintf, and the formatter RawDoFmt.  Conversions: %s %c %d %u %x %ld
 * %lu %lx %p %%, with an optional '-', '0' and field width.  KPutStr and
 * KPrintf are serialised with Forbid once Tasks run.
 */
#include "kernel.h"

static void
kputs(s)
register char *s;
{
	if (s == NULL)
		s = "(null)";
	while (*s)
		con_putc(*s++);
}

void
xKPutStr(s)
char *s;
{
	register struct Task *t;

	if ((t = ThisTask) != NULL)
		xForbid();
	kputs(s);
	if (t != NULL)
		xPermit();
}

/* exec.library's private RawPutChar: one console byte.  NUL is dropped,
 * so RawDoFmt's terminator prints nothing. */
void
kputc(c)
int c;
{
	if (c != 0)
		con_putc(c);
}

/* RawDoFmt's output procedure and its data. */
struct FmtOut {
	void (*fo_Proc)();
	APTR fo_Data;
};

static void
kout(o, c)
register struct FmtOut *o;
int c;
{
	register void (*proc)();

	proc = o->fo_Proc;
	(*proc)(c, o->fo_Data);
}

static void
kpad(o, n, c)
struct FmtOut *o;
register int n;
int c;
{
	while (n-- > 0)
		kout(o, c);
}

/* Unsigned v in base 10 or 16, in a field of width w. */
static void
knum(o, v, base, neg, w, zero, left)
struct FmtOut *o;
ULONG v;
int base, neg, w, zero, left;
{
	char buf[12];
	register int n;
	register int d;

	n = 0;
	do {
		d = (int)(v % (ULONG)base);
		buf[n++] = d < 10 ? '0' + d : 'a' + d - 10;
		v /= (ULONG)base;
	} while (v != 0L);
	w -= n + (neg != 0);
	if (!left && !zero)
		kpad(o, w, ' ');
	if (neg)
		kout(o, '-');
	if (!left && zero)
		kpad(o, w, '0');
	while (n > 0)
		kout(o, buf[--n]);
	if (left)
		kpad(o, w, ' ');
}

/*
 * Amiga RawDoFmt: each character of fmt, its arguments taken from data,
 * goes to proc(c, pdata), then a NUL.  An int or char argument is one
 * word of data and a long or pointer two, as MWC passes K&R arguments.
 * Returns the address after the last argument used.
 */
APTR
xRawDoFmt(fmt, data, proc, pdata)
char *fmt;
APTR data;
APTR proc;
APTR pdata;
{
	register char *f;
	register char *ap;
	struct FmtOut out;
	char *s;
	ULONG v;
	LONG sv;
	int c, w, zero, left, lng, neg, n;

	out.fo_Proc = (void (*)())proc;
	out.fo_Data = pdata;
	ap = (char *)data;
	for (f = fmt; (c = *f++) != 0; ) {
		if (c != '%') {
			kout(&out, c);
			continue;
		}
		zero = left = lng = 0;
		w = 0;
		if (*f == '-') {
			left = 1;
			f++;
		}
		if (*f == '0') {
			zero = 1;
			f++;
		}
		while (*f >= '0' && *f <= '9')
			w = w * 10 + *f++ - '0';
		if (*f == 'l') {
			lng = 1;
			f++;
		}
		neg = 0;
		switch (c = *f++) {
		case 0:
			f--;
			break;
		case 'c':
			n = *(int *)ap;
			ap += sizeof (int);
			if (!left)
				kpad(&out, w - 1, ' ');
			kout(&out, n);
			if (left)
				kpad(&out, w - 1, ' ');
			break;
		case 's':
			s = *(char **)ap;
			ap += sizeof (char *);
			if (s == NULL)
				s = "(null)";
			for (n = 0; s[n] != 0; n++)
				;
			if (!left)
				kpad(&out, w - n, ' ');
			while (*s)
				kout(&out, *s++);
			if (left)
				kpad(&out, w - n, ' ');
			break;
		case 'd':
			if (lng) {
				sv = *(LONG *)ap;
				ap += sizeof (LONG);
			} else {
				sv = *(int *)ap;
				ap += sizeof (int);
			}
			if (sv < 0L) {
				neg = 1;
				v = (ULONG)-sv;
			} else
				v = (ULONG)sv;
			knum(&out, v, 10, neg, w, zero, left);
			break;
		case 'u':
		case 'x':
			if (lng) {
				v = *(ULONG *)ap;
				ap += sizeof (ULONG);
			} else {
				v = (ULONG)*(UWORD *)ap;
				ap += sizeof (UWORD);
			}
			knum(&out, v, c == 'x' ? 16 : 10, 0, w, zero, left);
			break;
		case 'p':
			v = *(ULONG *)ap;
			ap += sizeof (APTR);
			knum(&out, v, 16, 0, 8, 1, 0);
			break;
		default:
			kout(&out, c);
			break;
		}
	}
	kout(&out, 0);
	return ((APTR)ap);
}

/*
 * K&R varargs: arguments sit above fmt on the stack, an int or char in one
 * word, a long or pointer in two.
 */
void
xKPrintf(fmt, args)
char *fmt;
int args;
{
	register struct Task *t;

	if ((t = ThisTask) != NULL)
		xForbid();
	xRawDoFmt(fmt, (APTR)&args, (APTR)kputc, NULL);
	if (t != NULL)
		xPermit();
}
