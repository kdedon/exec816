/* SPDX-License-Identifier: MIT */
/*
 * Shared by the HR display tests: the bitmap read back as one bit per
 * pixel, the cells the backend should draw, and the common start.
 */
#include "c900.h"
#include "condisp.h"

static struct Display *hd;		/* the HR backend */
static UBYTE *font;			/* its font, from 0x20 */
static UWORD stride, cellb, rows, cols;

/* The geometry of textcon.device's HR card, or FALSE without one. */
static int
hrgeom(cd)
register struct ConDisplay *cd;
{
	if (cd == NULL || cd->cd_Card != CDC_HR || cd->cd_Disp == NULL)
		return (0);
	hd = cd->cd_Disp;
	font = hd->dp_Font;
	cols = hd->dp_Cols;
	rows = hd->dp_Rows;
	cellb = cd->cd_Mode == CDM_HRCOLOR ? 4 : 1;
	stride = cd->cd_Mode == CDM_HRCOLOR ? 320 : 128;
	return (1);
}

static UBYTE *
bp(l)
ULONG l;
{
	return ((UBYTE *)(((ULONG)(l >= 0x10000L ? HRSEG1 : HRSEG0) << 24) |
	    (l & 0xffffL)));
}

/* A cell's rasters as one bit per pixel: lit = not background. */
static void
cellget(row, col, g)
int row, col;
register UBYTE *g;
{
	register UWORD *w;
	register int b, i, y;
	ULONG l;

	l = (ULONG)row * 16 * stride + (ULONG)col * cellb;
	for (y = 0; y < 16; y++, l += stride) {
		if (cellb == 1) {
			g[y] = *bp(l);
			continue;
		}
		w = (UWORD *)bp(l);
		b = 0;
		for (i = 0; i < 4; i++) {
			if ((w[0] >> (12 - 4 * i)) & 15)
				b |= 0x80 >> i;
			if ((w[1] >> (12 - 4 * i)) & 15)
				b |= 0x08 >> i;
		}
		g[y] = b;
	}
}

static int
want(c, attr, y)
int c, attr, y;
{
	register int b;

	b = font[(c - 0x20) * 16 + y];
	if ((attr & DA_BOLD) && cellb == 1)
		b |= b >> 1;
	if ((attr & DA_UNDERLINE) && y == 15)
		b = 0xff;
	if (attr & DA_INVERSE)
		b = ~b & 0xff;
	return (b);
}

static int
cellis(row, col, c, attr)
int row, col, c, attr;
{
	UBYTE g[16];
	register int y;

	cellget(row, col, g);
	for (y = 0; y < 16; y++)
		if (g[y] != want(c, attr, y)) {
			xKPrintf("cell %u,%u y %u\n", row, col, y);
			return (0);
		}
	return (1);
}

static int
cellff(row, col)
int row, col;
{
	UBYTE g[16];
	register int y;

	cellget(row, col, g);
	for (y = 0; y < 16; y++)
		if (g[y] != 0xff)
			return (0);
	return (1);
}

#define PUT(r, c, ch, a)	(*hd->dp_Put)(r, c, ch, a)
#define ERASE(r, c, n, a)	(*hd->dp_Erase)(r, c, n, a)
#define SCROLL(t, b, a)		(*hd->dp_Scroll)(t, b, a)
#define CURSOR(r, c, on)	(*hd->dp_Cursor)(r, c, on)

/* Row r: rlen(r) cells of rch(r). */
#define rch(r)		('A' + (r) % 26)
#define rlen(r)		(((r) * 7) % cols + 1)

/* Take the HR display from the console.  FALSE (and PASS) without a
 * card. */
static int
hrstart(name)
char *name;
{
	register struct ConDisplay *cd;

	cd = conask(0);
	if (cd == NULL || cd->cd_Card != CDC_HR) {
		xKPrintf("%s: no HR card\nPASS %s\n", name, name);
		return (0);
	}
	if (!hrgeom(cd)) {
		xKPrintf("FAIL %s: HR card without its display module\n", name);
		return (0);
	}
	conask(1);
	xKPrintf("%s: mode %u, %ux%u cells\n", name, cd->cd_Mode, cols, rows);
	(*hd->dp_Init)();
	return (1);
}

static int
hrend(name, err)
char *name, *err;
{
	if (err != NULL) {
		xKPrintf("FAIL %s: %s\n", name, err);
		return (1);
	}
	xKPrintf("PASS %s\n", name);
	return (0);
}
