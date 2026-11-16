/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The console's terminal emulator.  It owns the cursor, the attributes
 * and the escape parser, and draws through a struct Display.  The
 * repertoire is the ECMA-48 subset of Amiga console.device that a fixed
 * screen without scroll-down support can carry (docs/internals.md):
 *
 *   C0   BEL BS HT LF VT(up) FF(clear) CR, CAN/SUB cancel, ESC; 0x9B CSI
 *   ESC  [ c(reset) D(index) E(next line)
 *   CSI  A B C D E F H f J K M S m n s u h l, SP p (cursor), SP q
 *
 * The backend's cursor is hidden before any cell or position changes and
 * shown again when the call ends, so a backend drawing it in the cells
 * never has it overwritten.  No entry allocates or waits; callers that
 * share a VT serialise with VI off.
 */
#include <exec/types.h>
#include "vt.h"

#define BEL	0x07
#define BS	0x08
#define HT	0x09
#define LF	0x0a
#define VTAB	0x0b
#define FF	0x0c
#define CR	0x0d
#define CAN	0x18
#define SUB	0x1a
#define ESC	0x1b
#define DEL	0x7f
#define CSI	0x9b

#define S_NORM	0
#define S_ESC	1
#define S_CSI	2

#define PARMAX	9999

static void
hide(v)
register struct VT *v;
{
	if (v->vt_Flags & VTF_SHOWN) {
		(*v->vt_Disp->dp_Cursor)(v->vt_Row, v->vt_Col, 0);
		v->vt_Flags &= ~VTF_SHOWN;
	}
}

static void
show(v)
register struct VT *v;
{
	if ((v->vt_Flags & (VTF_CURSOR | VTF_SHOWN)) == VTF_CURSOR) {
		(*v->vt_Disp->dp_Cursor)(v->vt_Row, v->vt_Col, 1);
		v->vt_Flags |= VTF_SHOWN;
	}
}

static void
erase(v, row, col, n)
register struct VT *v;
int row, col, n;
{
	if (n > 0)
		(*v->vt_Disp->dp_Erase)(row, col, n, DA_NORMAL);
}

static void
scrollup(v, top)
register struct VT *v;
int top;
{
	(*v->vt_Disp->dp_Scroll)(top, v->vt_Rows - 1, DA_NORMAL);
}

static void
moveto(v, row, col)
register struct VT *v;
int row, col;
{
	if (row < 0)
		row = 0;
	else if (row >= v->vt_Rows)
		row = v->vt_Rows - 1;
	if (col < 0)
		col = 0;
	else if (col >= v->vt_Cols)
		col = v->vt_Cols - 1;
	hide(v);
	v->vt_Row = row;
	v->vt_Col = col;
	v->vt_Flags &= ~VTF_WRAP;
}

/* Down one line, scrolling at the bottom. */
static void
vtindex(v)
register struct VT *v;
{
	hide(v);
	v->vt_Flags &= ~VTF_WRAP;
	if (v->vt_Row + 1 < v->vt_Rows)
		v->vt_Row++;
	else
		scrollup(v, 0);
}

static void
clear(v)
register struct VT *v;
{
	register int r;

	hide(v);
	for (r = 0; r < v->vt_Rows; r++)
		erase(v, r, 0, v->vt_Cols);
	v->vt_Row = v->vt_Col = 0;
	v->vt_Flags &= ~VTF_WRAP;
}

static void
graphic(v, c)
register struct VT *v;
int c;
{
	hide(v);
	if (v->vt_Flags & VTF_WRAP) {
		v->vt_Col = 0;
		vtindex(v);
	}
	(*v->vt_Disp->dp_Put)(v->vt_Row, v->vt_Col, c, v->vt_Attr);
	if (v->vt_Col + 1 < v->vt_Cols)
		v->vt_Col++;
	else if (v->vt_Flags & VTF_AUTOWRAP)
		v->vt_Flags |= VTF_WRAP;
}

/* A C0 control; ESC, CAN and SUB are the parser's. */
static void
control(v, c)
register struct VT *v;
int c;
{
	register int n;

	switch (c) {
	case BEL:
		if (v->vt_Bell != NULL)
			(*v->vt_Bell)();
		break;
	case BS:
		moveto(v, v->vt_Row, v->vt_Col - (v->vt_Flags & VTF_WRAP ? 0 : 1));
		break;
	case HT:
		n = (v->vt_Col + 8) & ~7;
		moveto(v, v->vt_Row, n);
		break;
	case LF:
		vtindex(v);
		if (v->vt_Flags & VTF_LNM)
			v->vt_Col = 0;
		break;
	case VTAB:
		moveto(v, v->vt_Row - 1, v->vt_Col);
		break;
	case FF:
		clear(v);
		break;
	case CR:
		moveto(v, v->vt_Row, 0);
		break;
	}
}

static void
reset(v)
register struct VT *v;
{
	v->vt_Flags = (v->vt_Flags & VTF_SHOWN) | VTF_AUTOWRAP | VTF_LNM |
	    VTF_CURSOR;
	v->vt_Attr = DA_NORMAL;
	v->vt_State = S_NORM;
	v->vt_SRow = v->vt_SCol = 0;
	clear(v);
}

/* Decimal n into p; the end. */
static char *
putnum(p, n)
register char *p;
register unsigned n;
{
	char b[6];
	register int i;

	i = 0;
	do
		b[i++] = '0' + n % 10;
	while ((n /= 10) != 0);
	while (i > 0)
		*p++ = b[--i];
	return (p);
}

/* CSI a;b<fin>, with the intermediate if fin is preceded by one. */
static void
reply(v, a, b, rows, cols, inter, fin)
register struct VT *v;
int a, b, rows, cols, inter, fin;
{
	char buf[32];
	register char *p;

	if (v->vt_Reply == NULL)
		return;
	p = buf;
	*p++ = (char)CSI;
	p = putnum(p, a);
	*p++ = ';';
	p = putnum(p, b);
	if (rows > 0) {
		*p++ = ';';
		p = putnum(p, rows);
		*p++ = ';';
		p = putnum(p, cols);
	}
	if (inter)
		*p++ = inter;
	*p++ = fin;
	*p = '\0';
	(*v->vt_Reply)(v, buf);
}

static void
sgr(v)
register struct VT *v;
{
	register int i;

	if (v->vt_NPar == 0)
		v->vt_Attr = DA_NORMAL;
	for (i = 0; i < v->vt_NPar; i++) {
		switch (v->vt_Par[i]) {
		case 0:
			v->vt_Attr = DA_NORMAL;
			break;
		case 1:
			v->vt_Attr |= DA_BOLD;
			break;
		case 4:
			v->vt_Attr |= DA_UNDERLINE;
			break;
		case 5:
			v->vt_Attr |= DA_BLINK;
			break;
		case 7:
			v->vt_Attr |= DA_INVERSE;
			break;
		case 22:
			v->vt_Attr &= ~DA_BOLD;
			break;
		case 24:
			v->vt_Attr &= ~DA_UNDERLINE;
			break;
		case 25:
			v->vt_Attr &= ~DA_BLINK;
			break;
		case 27:
			v->vt_Attr &= ~DA_INVERSE;
			break;
		}
	}
}

static void
mode(v, on)
register struct VT *v;
int on;
{
	register int i, f;

	for (i = 0; i < v->vt_NPar; i++) {
		f = 0;
		if (v->vt_Priv == '?' && v->vt_Par[i] == 7)
			f = VTF_AUTOWRAP;
		else if (v->vt_Priv == 0 && v->vt_Par[i] == 20)
			f = VTF_LNM;
		if (on)
			v->vt_Flags |= f;
		else
			v->vt_Flags &= ~f;
	}
}

static void
dispatch(v, fin)
register struct VT *v;
int fin;
{
	register int n, p0;

	p0 = v->vt_NPar > 0 ? v->vt_Par[0] : 0;
	n = p0 == 0 ? 1 : p0;
	if (v->vt_Inter == ' ') {
		if (fin == 'p') {		/* Amiga cursor on/off */
			hide(v);
			if (v->vt_NPar > 0 && p0 == 0)
				v->vt_Flags &= ~VTF_CURSOR;
			else
				v->vt_Flags |= VTF_CURSOR;
		} else if (fin == 'q')		/* window status request */
			reply(v, 1, 1, v->vt_Rows, v->vt_Cols, ' ', 'r');
		return;
	}
	if (v->vt_Inter != 0)
		return;
	if (v->vt_Priv != 0 && fin != 'h' && fin != 'l')
		return;
	switch (fin) {
	case 'A':
		moveto(v, v->vt_Row - n, v->vt_Col);
		break;
	case 'B':
		moveto(v, v->vt_Row + n, v->vt_Col);
		break;
	case 'C':
		moveto(v, v->vt_Row, v->vt_Col + n);
		break;
	case 'D':
		moveto(v, v->vt_Row, v->vt_Col - n);
		break;
	case 'E':
		moveto(v, v->vt_Row + n, 0);
		break;
	case 'F':
		moveto(v, v->vt_Row - n, 0);
		break;
	case 'H':
	case 'f':
		moveto(v, n - 1, v->vt_NPar > 1 && v->vt_Par[1] != 0 ?
		    v->vt_Par[1] - 1 : 0);
		break;
	case 'J':
		hide(v);
		v->vt_Flags &= ~VTF_WRAP;
		if (p0 == 0) {
			erase(v, v->vt_Row, v->vt_Col, v->vt_Cols - v->vt_Col);
			for (n = v->vt_Row + 1; n < v->vt_Rows; n++)
				erase(v, n, 0, v->vt_Cols);
		} else if (p0 == 1) {
			for (n = 0; n < v->vt_Row; n++)
				erase(v, n, 0, v->vt_Cols);
			erase(v, v->vt_Row, 0, v->vt_Col + 1);
		} else if (p0 == 2)
			for (n = 0; n < v->vt_Rows; n++)
				erase(v, n, 0, v->vt_Cols);
		break;
	case 'K':
		hide(v);
		v->vt_Flags &= ~VTF_WRAP;
		if (p0 == 0)
			erase(v, v->vt_Row, v->vt_Col, v->vt_Cols - v->vt_Col);
		else if (p0 == 1)
			erase(v, v->vt_Row, 0, v->vt_Col + 1);
		else if (p0 == 2)
			erase(v, v->vt_Row, 0, v->vt_Cols);
		break;
	case 'M':
		hide(v);
		v->vt_Flags &= ~VTF_WRAP;
		if (n > v->vt_Rows - v->vt_Row)
			n = v->vt_Rows - v->vt_Row;
		while (n-- > 0)
			scrollup(v, v->vt_Row);
		break;
	case 'S':
		hide(v);
		if (n > v->vt_Rows)
			n = v->vt_Rows;
		while (n-- > 0)
			scrollup(v, 0);
		break;
	case 'm':
		sgr(v);
		break;
	case 'n':
		if (p0 == 6)
			reply(v, v->vt_Row + 1, v->vt_Col + 1, 0, 0, 0, 'R');
		break;
	case 's':
		v->vt_SRow = v->vt_Row;
		v->vt_SCol = v->vt_Col;
		break;
	case 'u':
		moveto(v, v->vt_SRow, v->vt_SCol);
		break;
	case 'h':
		mode(v, 1);
		break;
	case 'l':
		mode(v, 0);
		break;
	}
}

static void
escape(v, c)
register struct VT *v;
int c;
{
	v->vt_State = S_NORM;
	switch (c) {
	case '[':
		v->vt_State = S_CSI;
		v->vt_NPar = 0;
		v->vt_Par[0] = 0;
		v->vt_Inter = 0;
		v->vt_Priv = 0;
		break;
	case 'c':
		reset(v);
		break;
	case 'D':
		vtindex(v);
		break;
	case 'E':
		vtindex(v);
		v->vt_Col = 0;
		break;
	}
}

/* One byte of a control sequence. */
static void
csibyte(v, c)
register struct VT *v;
int c;
{
	register UWORD *p;

	if (c >= '0' && c <= '9') {
		if (v->vt_NPar == 0)
			v->vt_NPar = 1;
		if (v->vt_NPar <= VT_NPAR) {
			p = &v->vt_Par[v->vt_NPar - 1];
			*p = *p * 10 + (c - '0');
			if (*p > PARMAX)
				*p = PARMAX;
		}
	} else if (c == ';') {
		if (v->vt_NPar == 0)
			v->vt_NPar = 1;
		if (v->vt_NPar < VT_NPAR)
			v->vt_Par[v->vt_NPar] = 0;
		if (v->vt_NPar <= VT_NPAR)
			v->vt_NPar++;
	} else if (c >= 0x3c && c <= 0x3f) {
		v->vt_Priv = c;
	} else if (c >= 0x20 && c <= 0x2f) {
		v->vt_Inter = c;
	} else if (c >= 0x40 && c <= 0x7e) {
		v->vt_State = S_NORM;
		if (v->vt_NPar > VT_NPAR)
			v->vt_NPar = VT_NPAR;
		dispatch(v, c);
	} else
		v->vt_State = S_NORM;
}

static void
vtbyte(v, c)
register struct VT *v;
register int c;
{
	c &= 0xff;
	if (c == CAN || c == SUB) {
		v->vt_State = S_NORM;
		return;
	}
	if (c == ESC) {
		v->vt_State = S_ESC;
		return;
	}
	if (c == CSI) {
		v->vt_State = S_ESC;
		c = '[';
	}
	if (c < 0x20) {
		control(v, c);
		return;
	}
	switch (v->vt_State) {
	case S_ESC:
		escape(v, c);
		return;
	case S_CSI:
		csibyte(v, c);
		return;
	}
	if (c == DEL || (c >= 0x80 && c < 0xa0))
		return;
	graphic(v, c);
}

/* Bind v to dp, which the caller has initialised (clear, home). */
void
vt_init(v, dp)
register struct VT *v;
struct Display *dp;
{
	v->vt_Disp = dp;
	v->vt_Reply = NULL;
	v->vt_Bell = NULL;
	v->vt_Rows = dp->dp_Rows;
	v->vt_Cols = dp->dp_Cols;
	v->vt_Row = v->vt_Col = 0;
	v->vt_Flags = VTF_AUTOWRAP | VTF_LNM | VTF_CURSOR;
	v->vt_Attr = DA_NORMAL;
	v->vt_State = S_NORM;
	v->vt_SRow = v->vt_SCol = 0;
	show(v);
}

void
vt_putc(v, c)
register struct VT *v;
int c;
{
	vtbyte(v, c);
	show(v);
}

#define DRAWN(c)	(((c) >= 0x20 && (c) < DEL) || (c) >= 0xa0)

/* n bytes of s, or up to the NUL when n is -1.  With autowrap, a run of
 * drawn characters goes to the backend a row at a time. */
void
vt_write(v, s, n)
register struct VT *v;
register UBYTE *s;
long n;
{
	register int k, c;
	struct Display *dp;
	int room;

	dp = v->vt_Disp;
	while (n != 0) {
		c = *s;
		if (n < 0 && c == '\0')
			break;
		if (v->vt_State != S_NORM || !DRAWN(c) ||
		    (v->vt_Flags & VTF_AUTOWRAP) == 0) {
			vtbyte(v, c);
			s++;
			if (n > 0)
				n--;
			continue;
		}
		hide(v);
		if (v->vt_Flags & VTF_WRAP) {
			v->vt_Col = 0;
			vtindex(v);
		}
		room = v->vt_Cols - v->vt_Col;
		if (n > 0 && n < room)
			room = (int)n;
		for (k = 1; k < room && DRAWN(s[k]); k++)
			;
		if (dp->dp_Text != NULL)
			(*dp->dp_Text)(v->vt_Row, v->vt_Col, s, k, v->vt_Attr);
		else
			for (c = 0; c < k; c++)
				(*dp->dp_Put)(v->vt_Row, v->vt_Col + c, s[c],
				    v->vt_Attr);
		if (v->vt_Col + k < v->vt_Cols)
			v->vt_Col += k;
		else {
			v->vt_Col = v->vt_Cols - 1;
			v->vt_Flags |= VTF_WRAP;
		}
		s += k;
		if (n > 0)
			n -= k;
	}
	show(v);
}
