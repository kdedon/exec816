/* SPDX-License-Identifier: MIT */
/*
 * The LC display backend, taken from the console.  Without the card no
 * NVI has been counted and nothing else runs.  With it: init, cells,
 * attributes, a scroll, an erase across a row boundary and the cursor,
 * checked by reading the 8568's RAM and registers back; the 'A' glyph in
 * a 16-byte slot, with R8, R9, R11, R23 and R29 giving the mode
 * framebuf.resource describes: 8-line cells at 640x200, or interlaced
 * 16-line cells at 640x400 (lcdisp400); and no NVI raised by any of it.
 * First a framebuf.resource owner reprograms the chip (bitmap mode,
 * colours, interlace, cell height, display and attribute addresses,
 * 16 KB RAM) and overwrites the font, and FreeFB must give the console
 * its registers and font back.
 */
/* LC: card */
#include "test.h"
#include <resources/framebuf.h>
#include "condisp.h"

APTR FrameBufBase;

#ifndef TNAME
#define TNAME		"lcdisp"
#endif

#define LC_ADDR		0x0181
#define LC_DATA		0x0183
#define LC_READY	0x80

#define V_ATTR		0x1000
#define V_FONT		0x2000
#define CELL(r, c)	((r) * 80 + (c))

#define A_NORM		0x0e
#define A_BOLD		0x0f
#define A_INV		0x4e

static struct Display *d;
static int il;			/* 640x400 interlaced */
static int fh;			/* dp_Font rasters per glyph */

static int lchung;

static void
rdy()
{
	register unsigned n;

	for (n = 30000; n != 0; n--)
		if (inb(LC_ADDR) & LC_READY)
			return;
	lchung = 1;
}

static int
rreg(r)
int r;
{
	outb(LC_ADDR, r);
	rdy();
	return (inb(LC_DATA) & 0xff);
}

static void
wreg(r, v)
int r, v;
{
	outb(LC_ADDR, r);
	rdy();
	outb(LC_DATA, v);
}

/* One VRAM byte: the update address loads the data register. */
static int
vrd(a)
unsigned a;
{
	int v;

	Disable();
	wreg(18, a >> 8);
	wreg(19, a & 0xff);
	v = rreg(31);
	rdy();
	Enable();
	return (v);
}

/* The driver caches the register index and update address, so it is not
 * used after a readback until dp_Init has run again. */
static char *
check()
{
	register int i;
	register UBYTE *g;

	g = d->dp_Font + ('A' - 0x20) * fh;
	(*d->dp_Init)();
	(*d->dp_Put)(5, 6, 'B', DA_BOLD);
	(*d->dp_Put)(0, 0, 'A', DA_NORMAL);
	(*d->dp_Put)(1, 5, 'C', DA_NORMAL);
	(*d->dp_Put)(24, 79, 'Z', DA_INVERSE);
	(*d->dp_Scroll)(0, 24, DA_NORMAL);
	(*d->dp_Put)(22, 77, 'X', DA_NORMAL);
	(*d->dp_Put)(22, 78, 'E', DA_INVERSE);
	(*d->dp_Put)(23, 1, 'E', DA_INVERSE);
	(*d->dp_Put)(23, 2, 'Y', DA_NORMAL);
	(*d->dp_Erase)(22, 78, 4, DA_NORMAL);
	(*d->dp_Cursor)(3, 4, 1);

	if (vrd(CELL(0, 5)) != 'C' || vrd(CELL(0, 0)) != ' ')
		return ("scrolled text");
	if (vrd(CELL(4, 6)) != 'B' || vrd(V_ATTR + CELL(4, 6)) != A_BOLD)
		return ("scrolled bold cell");
	if (vrd(CELL(23, 79)) != 'Z' || vrd(V_ATTR + CELL(23, 79)) != A_INV)
		return ("scrolled inverse cell");
	if (vrd(CELL(24, 79)) != ' ' || vrd(V_ATTR + CELL(24, 79)) != A_NORM)
		return ("new bottom row");
	if (vrd(CELL(22, 78)) != ' ' || vrd(CELL(23, 1)) != ' ' ||
	    vrd(V_ATTR + CELL(23, 1)) != A_NORM)
		return ("erase over a row boundary");
	if (vrd(CELL(22, 77)) != 'X' || vrd(CELL(23, 2)) != 'Y')
		return ("erase extent");
	Disable();
	i = (rreg(14) << 8) | rreg(15);
	if (i != CELL(3, 4) || (rreg(10) & 0x7f) != fh - 2 ||
	    (rreg(11) & 0x1f) != fh - 1) {
		Enable();
		return ("cursor");
	}
	if ((rreg(9) & 0x1f) != fh - 1 || (rreg(23) & 0x1f) != fh ||
	    (rreg(29) & 0x1f) != fh - 1 || (rreg(8) & 3) != (il ? 3 : 0)) {
		Enable();
		return ("mode (R8, R9, R23, R29)");
	}
	rdy();
	Enable();

	(*d->dp_Init)();
	if (vrd(CELL(4, 6)) != ' ' || vrd(V_ATTR + CELL(4, 6)) != A_NORM)
		return ("cleared cell");
	for (i = 0; i < fh; i++)
		if (vrd(V_FONT + 'A' * 16 + i) != g[i])
			return ("glyph A");
	if ((fh < 16 && vrd(V_FONT + 'A' * 16 + fh) != 0) ||
	    vrd(V_FONT + 15) != 0)
		return ("glyph padding");
	(*d->dp_Init)();
	return (lchung ? "8568 never ready" : NULL);
}

/* Registers an owner changes: the bits the chip reads back (the rest
 * read 1), the console's value at 640x200 and at 640x400, and the
 * owner's, which is neither. */
static UBYTE lentreg[] = {
	8, 0x03, 0x00, 0x03, 0x01,	9, 0x1f, 0x07, 0x0f, 0x0b,
	11, 0x1f, 0x07, 0x0f, 0x0c,	12, 0xff, 0x00, 0x00, 0x10,
	13, 0xff, 0x00, 0x00, 0x20,	20, 0xff, 0x10, 0x10, 0x30,
	21, 0xff, 0x00, 0x00, 0x40,	23, 0x1f, 0x08, 0x10, 0x0c,
	25, 0xff, 0x47, 0x47, 0x80,	26, 0xff, 0xf0, 0xf0, 0x2f,
	29, 0x1f, 0x07, 0x0f, 0x0b
};
#define LENTN		5

/* Lent: the owner's registers, 0x0000-0x30CF filled with 0xFF in
 * 255-byte pieces, 16 KB RAM; FreeFB must undo all of it. */
static char *
lent()
{
	static char me[] = TNAME;
	register int i;
	register UBYTE *g;

	if (AllocFB(me) != NULL)
		return ("AllocFB refused");
	Disable();
	for (i = 0; i < sizeof lentreg; i += LENTN)
		wreg(lentreg[i], lentreg[i + 4]);
	wreg(18, 0);
	wreg(19, 0);
	wreg(24, 0x20);
	wreg(31, 0xff);
	rdy();
	Enable();
	for (i = 0; i < 49; i++) {
		Disable();
		wreg(30, 255);
		rdy();
		Enable();
	}
	Disable();
	wreg(28, 0xe0);
	rdy();
	Enable();
	FreeFB();
	Disable();
	for (i = 0; i < sizeof lentreg; i += LENTN)
		if ((rreg(lentreg[i]) & lentreg[i + 1]) !=
		    lentreg[i + (il ? 3 : 2)])
			break;
	if (i == sizeof lentreg && (rreg(28) & 0xf0) != 0x30)
		i = -1;
	rdy();
	Enable();
	if (i != sizeof lentreg) {
		xKPrintf("lcdisp: R%d not restored\n", i < 0 ? 28 : lentreg[i]);
		return ("register not restored");
	}
	g = d->dp_Font + ('A' - 0x20) * fh;
	for (i = 0; i < fh; i++)
		if (vrd(V_FONT + 'A' * 16 + i) != g[i])
			return ("glyph A not restored");
	if ((fh < 16 && vrd(V_FONT + 'A' * 16 + fh) != 0) ||
	    vrd(V_FONT + 0x01 * 16) != 0 || vrd(V_FONT + 0x7f * 16 + 3) != 0)
		return ("font area not cleared");
	return (lchung ? "8568 never ready" : NULL);
}

int
main()
{
	ULONG nvi, t0;
	register struct ConDisplay *cd;
	register struct FBMode *m;
	register int i;
	char *why;

	nvi = IntStray(INTV_NVI);
	cd = conask(0);
	if (cd == NULL || cd->cd_Card != CDC_LC) {
		if (nvi != 0)
			xKPrintf("FAIL %s: NVI without an LC card\n", TNAME);
		else
			xKPrintf("%s: no LC card\nPASS %s\n", TNAME, TNAME);
		return (0);
	}
	if ((d = cd->cd_Disp) == NULL) {
		xKPrintf("FAIL %s: LC card without its display module\n", TNAME);
		return (0);
	}
	if ((FrameBufBase = OpenResource(FRAMEBUFNAME)) == NULL) {
		xKPrintf("FAIL %s: no framebuf.resource\n", TNAME);
		return (0);
	}
	m = GetFBInfo()->fbi_Modes;
	if (m->fbm_Width != 640 || (m->fbm_Height != 200 &&
	    m->fbm_Height != 400) || m->fbm_Depth != 1 ||
	    m->fbm_BytesPerRow != 80 || m->fbm_Half[0] != NULL) {
		xKPrintf("FAIL %s: framebuf.resource's LC mode\n", TNAME);
		return (0);
	}
	il = m->fbm_Height == 400;
	fh = il ? 16 : 8;
	xKPrintf("lcdisp: 640x%d, %d-line cells\n", m->fbm_Height, fh);
	if ((why = lent()) != NULL) {
		xKPrintf("FAIL %s: %s\n", TNAME, why);
		return (0);
	}
	conask(1);
	if ((why = check()) != NULL) {
		xKPrintf("FAIL %s: %s\n", TNAME, why);
		return (0);
	}
	t0 = TickCount;
	for (i = 0; i < 25; i++)
		(*d->dp_Scroll)(0, 24, DA_NORMAL);
	xKPrintf("lcdisp: 25 full scrolls in %lu ticks\n", TickCount - t0);
	if (IntStray(INTV_NVI) != nvi) {
		xKPrintf("FAIL %s: %lu NVI(s) during display use\n", TNAME,
		    IntStray(INTV_NVI) - nvi);
		return (0);
	}
	xKPrintf("PASS %s\n", TNAME);
	return (0);
}
