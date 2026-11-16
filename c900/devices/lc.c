/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The LC card's display backend: a MOS 8568 VDC in 80x25 text, 8x8 cells
 * at 640x200 or, when framebuf.resource describes 640x400 (mkdisk.py
 * --lcmode=400), interlaced 8x16 cells.  The chip's
 * 64 KB of RAM is reached only through its registers: the update address
 * (R18/R19), the data register (R31), and block fill and copy (R30).  It
 * has no character ROM, so lcinit loads a font into it.
 *
 * Every data or block access waits for Update Ready.  The 8568 pulls INTR
 * (the NVI) low on every Update Ready rise and holds it until the status
 * port is read, so each entry runs with VI and NVI off and ends with a
 * status read that finds the chip ready: no LC interrupt is left pending.
 * Blocks run in pieces of at most 255 bytes, with interrupts let in
 * between.  Callers serialize entries.  textcon.device found the card.
 */
#include <exec/exec.h>
#include <resources/framebuf.h>
#include "display.h"

int inb();
void outb();
int intoff();
void intrest();

#define LC_ADDR		0x0181		/* write: register index; read: status */
#define LC_DATA		0x0183
#define LC_READY	0x80		/* status: Update Ready */

/* Polls of Update Ready before the chip counts as hung.  A 255-byte copy
 * takes about 1 ms. */
#define LCPOLL		30000

#define NCOL		80
#define NROW		25
#define NCELL		(NCOL * NROW)
#define PIECE		255

/* RAM layout: text and attribute planes, then 256 glyphs of 16 bytes:
 * the stride while R9 is below 16. */
#define V_TEXT		0x0000
#define V_ATTR		0x1000
#define V_FONT		0x2000
#define V_FONTEND	0x3000
#define GLYPH		16
#define FONTH		8		/* font8x8 rasters; R9 + 1 */
#define FONTLO		0x20		/* first glyph the fonts hold */
#define FONTN		95
#define ILH		400		/* fbm_Height of the interlaced mode */

/* Registers. */
#define R_CURST		10		/* b6:5 cursor mode, b4:0 first line */
#define R_CURS		14
#define R_UPD		18
#define R_LPENV		16		/* R16, R17: light pen, read-only */
#define R_MODE		24		/* b7: block copy, else fill */
#define R_CHARB		28		/* font base, 64 KB RAM */
#define R_COUNT		30		/* writing it starts a block */
#define R_DATA		31
#define R_SRC		32
#define NREG		38

#define M_COPY		0x80
#define CUR_ON		0x06		/* steady, from line 6 */
#define CUR_ON16	0x0e		/* steady, from line 14 */
#define CUR_OFF		0x26		/* mode 01: none */

/* Attribute bits; the foreground is RGBI in b3:0. */
#define A_REV		0x40
#define A_UNDER		0x20
#define A_BLINK		0x10
#define A_WHITE		0x0e
#define A_INT		0x01
#define A_MIXED		0xff		/* lcrowa: the row's cells differ */

/*
 * Every register, written on init: the chip resets R0-R36 to 0.  Timing
 * for 80x25 text in 8x8 cells of 8 lines (R0-R9, R22, R23, R34, R35),
 * display at V_TEXT, attributes at V_ATTR (R20), font at V_FONT with 64 KB
 * RAM (R28), white on black, blink rate 1/32 (R24), attributes on (R25),
 * 5 refresh cycles a line (R36).  R37 (8568 only): positive HSYNC and
 * VSYNC, as after reset and on the C128.
 */
static UBYTE lcregs[NREG] = {
	0x7e, 0x50, 0x66, 0x49, 0x20, 0x00, 0x19, 0x1d,
	0x00, FONTH - 1, CUR_OFF, FONTH - 1, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x78, FONTH,
	0x20, 0x47, 0xf0, 0x00, 0x30, 0x07, 0x00, 0x00,
	0x00, 0x00, 0x7d, 0x64, 0x05, 0xc0
};

/* Interlaced 8x16 cells: interlace sync and video (R8), 16-line cells
 * (R9, R23), cursor end (R11) and underline (R29) on the last lines.  The
 * vertical registers still count 8-line rows per field.  R11 stays within
 * 0..R9. */
static UBYTE lcil[] = {
	8, 0x03,	9, 0x0f,	11, 0x0f,	23, 0x10,	29, 0x0f
};

extern UBYTE font8x8[], font8x16[];

static UBYTE *lcfont = font8x8;	/* FONTLO onwards, lcfh rasters each */
static int lcfh = FONTH;
static int lccuron = CUR_ON;
static int lcok;		/* probed and initialized; cleared if it hangs */
static int lcs;			/* interrupt state the entry found */
static UBYTE lcsel;		/* register index as last written */
static UBYTE lcmode;		/* R24 as last written */
static UWORD lcupd;		/* update address as the chip holds it */
static UBYTE lcrowa[NROW];	/* each row's attribute, or A_MIXED */

/* Wait for Update Ready.  The status read that finds it releases INTR. */
static int
lcrdy()
{
	register unsigned n;

	if (!lcok)
		return (FALSE);
	for (n = LCPOLL; n != 0; n--)
		if (inb(LC_ADDR) & LC_READY)
			return (TRUE);
	lcok = FALSE;
	return (FALSE);
}

static void
lcsetr(r)
int r;
{
	if (lcsel != r) {
		lcsel = r;
		outb(LC_ADDR, r);
	}
}

static void
lcwr(r, v)
int r, v;
{
	lcsetr(r);
	if (lcrdy())
		outb(LC_DATA, v);
}

static void
lcaddr(r, a)
int r;
unsigned a;
{
	lcwr(r, a >> 8);
	lcwr(r + 1, a & 0xff);
}

static void
lcseek(a)
unsigned a;
{
	if (a != lcupd) {
		lcaddr(R_UPD, a);
		lcupd = a;
	}
}

static void
lcsetmode(m)
int m;
{
	if (lcmode != m)
		lcwr(R_MODE, lcmode = m);
}

/* Enter with VI and NVI off; leave with the chip idle and INTR released. */
static void
lcenter()
{
	lcs = intoff();
}

static void
lcleave()
{
	lcrdy();
	intrest(lcs);
}

/* Run n bytes of the block the addresses and R24 already describe, letting
 * interrupts in between pieces. */
static void
lcrun(n)
register unsigned n;
{
	register unsigned k;

	while (n != 0 && lcok) {
		k = n > PIECE ? PIECE : n;
		lcwr(R_COUNT, k);
		lcupd += k;
		n -= k;
		if (n != 0) {
			lcleave();
			lcenter();
		}
	}
}

/* n bytes of v from a: one data write, then a fill of the rest. */
static void
lcfill(a, v, n)
unsigned a, n;
int v;
{
	if (n == 0)
		return;
	lcseek(a);
	lcsetmode(lcmode & ~M_COPY);
	lcwr(R_DATA, v);
	lcupd++;
	lcrun(n - 1);
}

/* Copy n bytes from src down to dst <= src: the chip copies ascending. */
static void
lccopy(dst, src, n)
unsigned dst, src, n;
{
	if (n == 0)
		return;
	lcseek(dst);
	lcaddr(R_SRC, src);
	lcsetmode(lcmode | M_COPY);
	lcrun(n);
}

/* The 8568 attribute for a console one: white, with intensity for bold. */
static int
lcattr(a)
register int a;
{
	register int v;

	v = A_WHITE;
	if (a & DA_INVERSE)
		v |= A_REV;
	if (a & DA_BOLD)
		v |= A_INT;
	if (a & DA_UNDERLINE)
		v |= A_UNDER;
	if (a & DA_BLINK)
		v |= A_BLINK;
	return (v);
}

/* Whether rows r to e all have attribute v throughout. */
static int
lcsame(r, e, v)
register int r, e, v;
{
	for (; r <= e; r++)
		if (lcrowa[r] != v)
			return (FALSE);
	return (TRUE);
}

/*
 * Program every register, R28 first so later addresses are 64 KB ones;
 * load the font; clear the planes; the cursor at home.  This is also
 * cd_Reset, so it assumes nothing a bitmap owner left: interrupts are let
 * in after each glyph, as between block pieces.
 */
static void
lcinit()
{
	register int i, j;
	register UBYTE *g;

	lcok = TRUE;
	lcsel = 0xff;
	lcenter();
	lcwr(R_CHARB, lcregs[R_CHARB]);
	for (i = 0; i < NREG; i++)
		if (i != R_LPENV && i != R_LPENV + 1 && i != R_UPD &&
		    i != R_UPD + 1 && i != R_COUNT && i != R_DATA)
			lcwr(i, lcregs[i]);
	lcmode = lcregs[R_MODE];
	lcupd = ~V_FONT;
	lcfill(V_FONT, 0, V_FONTEND - V_FONT);
	for (i = 0; i < FONTN; i++) {
		lcseek(V_FONT + (FONTLO + i) * GLYPH);
		g = &lcfont[i * lcfh];
		for (j = 0; j < lcfh; j++) {
			lcwr(R_DATA, g[j]);
			lcupd++;
		}
		lcleave();
		lcenter();
	}
	lcfill(V_TEXT, ' ', NCELL);
	lcfill(V_ATTR, lcattr(DA_NORMAL), NCELL);
	for (i = 0; i < NROW; i++)
		lcrowa[i] = lcattr(DA_NORMAL);
	lcwr(R_CURST, lccuron);
	lcaddr(R_CURS, V_TEXT);
	lcleave();
}

/* One cell.  Its attribute goes out only where its row's would change. */
static void
lcput(row, col, c, attr)
int row, col, c, attr;
{
	register unsigned a;
	register int v;

	if (!lcok)
		return;
	a = row * NCOL + col;
	v = lcattr(attr);
	lcenter();
	lcseek(V_TEXT + a);
	lcwr(R_DATA, c & 0xff);
	lcupd++;
	if (lcrowa[row] != v) {
		lcseek(V_ATTR + a);
		lcwr(R_DATA, v);
		lcupd++;
		lcrowa[row] = A_MIXED;
	}
	lcleave();
}

/* n cells from (row, col), possibly over several rows, blank in attr. */
static void
lcerase(row, col, n, attr)
int row, col, n, attr;
{
	register unsigned a;
	register int r, e, v;

	if (!lcok || n <= 0)
		return;
	a = row * NCOL + col;
	if (a + n > NCELL)
		n = NCELL - a;
	e = (a + n - 1) / NCOL;
	v = lcattr(attr);
	lcenter();
	lcfill(V_TEXT + a, ' ', (unsigned)n);
	if (!lcsame(row, e, v)) {
		lcfill(V_ATTR + a, v, (unsigned)n);
		for (r = row; r <= e; r++)
			if (r * NCOL >= a && (r + 1) * NCOL <= a + n)
				lcrowa[r] = v;
			else if (lcrowa[r] != v)
				lcrowa[r] = A_MIXED;
	}
	lcleave();
}

/* Rows top+1 to bot move up one; row bot is blank in attr. */
static void
lcscroll(top, bot, attr)
int top, bot, attr;
{
	register unsigned a, n;
	register int r, v;

	if (!lcok || top > bot)
		return;
	a = top * NCOL;
	n = (bot - top) * NCOL;
	lcenter();
	lccopy(V_TEXT + a, V_TEXT + a + NCOL, n);
	v = lcrowa[top];
	if (n != 0 && (v == A_MIXED || !lcsame(top + 1, bot, v)))
		lccopy(V_ATTR + a, V_ATTR + a + NCOL, n);
	for (r = top; r < bot; r++)
		lcrowa[r] = lcrowa[r + 1];
	lcleave();
	lcerase(bot, 0, NCOL, attr);
}

static void
lccursor(row, col, on)
int row, col, on;
{
	if (!lcok)
		return;
	lcenter();
	lcwr(R_CURST, on ? lccuron : CUR_OFF);
	if (on)
		lcaddr(R_CURS, V_TEXT + row * NCOL + col);
	lcleave();
}

static char lcname[] = "lc";
char vidname[] = "lc.display";

static struct Display lcdisplay = {
	lcname, NCOL, NROW, font8x8, lcinit, lcput, lcscroll, lcerase,
	lccursor
};

APTR FrameBufBase;

/* The console mode is the one framebuf.resource gives an owner. */
struct Display *
vidsetup(cd)
struct ConDisplay *cd;
{
	register int i;

	if (cd->cd_Card != CDC_LC)
		return (NULL);
	if ((FrameBufBase = OpenResource(FRAMEBUFNAME)) != NULL &&
	    GetFBInfo()->fbi_Modes->fbm_Height == ILH) {
		for (i = 0; i < sizeof lcil; i += 2)
			lcregs[lcil[i]] = lcil[i + 1];
		lcdisplay.dp_Font = lcfont = font8x16;
		lcfh = 2 * FONTH;
		lccuron = CUR_ON16;
	}
	return (&lcdisplay);
}
