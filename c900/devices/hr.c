/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * HR display card backend: text cells drawn into the 128 KB bitmap at
 * physical 0x3E0000.  The JP3 jumper fixes the mode: 1024x800 or
 * 1024x1024 mono (1 bit per pixel, 128-byte rasters) or 640x400 RGBI
 * (4 bits per pixel, 320-byte rasters).  The 404H ID tells mono (2) from
 * colour (1) but not 800 from 1024 lines, so kboot's BF_HR1024 decides
 * that, and a boot setting overrides both; textcon.device found the card, mapped the first 64 KB at HRSEG0
 * and the second at HRSEG1, and reports the mode.  A bitmap offset is a
 * linear 17-bit offset split between them.
 *
 * Cells are 8x16 (font8x16.c; other codes draw as '?'): 128x50, 128x64
 * or 80x25.  The card has no
 * display start register, so scrolling copies the bitmap; per text row the
 * driver keeps how many leading cells may be non-blank and copies and
 * clears only those.  The cursor is the cell inverted (XOR); drawing over
 * it keeps it shown.
 */
#include <exec/exec.h>
#include "c900.h"
#include <resources/framebuf.h>
#include "display.h"

void outb();
void kwmove();
void kwfill();
void kglyphs();
void kglyph4();
void kmove2d();
void kfill2d();
void kcomb();
void kxor4();


#define HRM_COLOR	CDM_HRCOLOR
#define SEGBASE(s)	((ULONG)(s) << 24)
#define POFF(p)		((ULONG)(p) & 0xFFFFL)

#define CELLH		16
#define MAXROWS		64
#define SEGLEN		0x10000L

/* Colour mode: cell foreground and background, IRGB. */
#define COLFG		0x7
#define COLBOLD		0xf
#define COLBG		0x0
#define COLCUR		0x7777	/* swaps COLFG and COLBG */

extern UBYTE font8x16[];
static int hrmode;			/* CDM_ */
static UWORD hrstride;			/* bytes per raster */
static UWORD hrcellb;			/* bytes per cell per raster */
static UBYTE hrlen[MAXROWS];		/* per row: cells that may be non-blank */
static ULONG hrrow[MAXROWS + 2];	/* per row: linear offset of raster 0 */
static ULONG *coltab;			/* glyph byte to eight colour pixels */
static int colattr = -1;		/* attributes coltab is for */
static UWORD curow, cucol;
static int cuon;

/* Pixel masks: four mono pixels to four colour nibbles. */
static UWORD nibmask[16] = {
	0x0000, 0x000f, 0x00f0, 0x00ff, 0x0f00, 0x0f0f, 0x0ff0, 0x0fff,
	0xf000, 0xf00f, 0xf0f0, 0xf0ff, 0xff00, 0xff0f, 0xfff0, 0xffff
};

static void hrinit();
static void hrput();
static void hrscroll();
static void hrerase();
static void hrcursor();
static void hrtext();

static struct Display hrdisplay = {
	"hr", 0, 0, font8x16, hrinit, hrput, hrscroll, hrerase, hrcursor,
	hrtext
};

/* The bitmap byte at linear offset l. */
static UBYTE *
hrptr(l)
ULONG l;
{
	if (l >= SEGLEN)
		return ((UBYTE *)SEGBASE(HRSEG1) + (UWORD)l);
	return ((UBYTE *)SEGBASE(HRSEG0) + (UWORD)l);
}

/* Linear offset of raster y of text row row, cell col. */
static ULONG
hroff(row, col, y)
int row, col, y;
{
	register UWORD o;

	o = hrcellb == 1 ? col : col << 2;
	if (y != 0)
		o += y * hrstride;
	return (hrrow[row] + o);
}

/* Fill n bytes (n even, offset even) from linear l with word v. */
static void
lfill(l, v, n)
ULONG l;
UWORD v;
ULONG n;
{
	register ULONG k;

	while (n != 0) {
		k = SEGLEN - (l & 0xffffL);
		if (k > n)
			k = n;
		kwfill(hrptr(l), v, (UWORD)(k >> 1));
		l += k;
		n -= k;
	}
}

/* Move n bytes (even) from linear s down to linear d (d < s). */
static void
lmove(d, s, n)
ULONG d, s, n;
{
	register ULONG k, ks;

	while (n != 0) {
		k = SEGLEN - (d & 0xffffL);
		ks = SEGLEN - (s & 0xffffL);
		if (k > ks)
			k = ks;
		if (k > n)
			k = n;
		kwmove(hrptr(d), hrptr(s), (UWORD)(k >> 1));
		d += k;
		s += k;
		n -= k;
	}
}

/* Bytes (any alignment) in one raster, within one segment. */
static void
bfill(p, v, n)
register UBYTE *p;
int v;
register int n;
{
	if (n <= 0)
		return;
	if (POFF(p) & 1) {
		*p++ = v;
		n--;
	}
	kwfill(p, (UWORD)((v << 8) | (v & 0xff)), (UWORD)(n >> 1));
	if (n & 1)
		p[n - 1] = v;
}

static UBYTE *
glyph(c)
register int c;
{
	c &= 0xff;
	if (c < 0x20 || c >= 0x7f)
		c = '?';
	return (&font8x16[(c - 0x20) << 4]);
}

/* TRUE when all CELLH rasters of the row at linear l lie in one segment,
 * so a pointer can step through them. */
static int
oneseg(l)
ULONG l;
{
	return ((l & 0xffffL) + (ULONG)CELLH * hrstride <= SEGLEN);
}

/* Invert one cell. */
static void
xorcell(row, col)
int row, col;
{
	register int y;
	ULONG l;

	l = hroff(row, col, 0);
	if (hrmode != HRM_COLOR)
		kcomb(hrptr(l), CELLH, 128);
	else if (oneseg(l))
		kxor4(hrptr(l), COLCUR, CELLH, 320);
	else
		for (y = 0; y < CELLH; y++, l += 320)
			kxor4(hrptr(l), COLCUR, 1, 320);
}

/* Whole-bitmap blank, the control register, empty rows, no cursor. */
static void
hrinit()
{
	register int i;

	outb(FBHR_CTL, FBHR_FULL);
	lfill(0L, 0, 2 * SEGLEN);
	for (i = 0; i < MAXROWS; i++)
		hrlen[i] = 0;
	cuon = 0;
}

/* n colour cells of s from col on, in a row within one segment. */
static void
coltext(row, col, s, n, attr)
int row, col;
UBYTE *s;
int n, attr;
{
	register int i;
	register UWORD fg, bg;
	UBYTE *p;
	UWORD t[16];

	if (attr != colattr) {
		fg = (attr & DA_BOLD) ? COLBOLD * 0x1111 : COLFG * 0x1111;
		bg = COLBG * 0x1111;
		if (attr & DA_INVERSE) {
			i = fg;
			fg = bg;
			bg = i;
		}
		for (i = 0; i < 16; i++)
			t[i] = (nibmask[i] & fg) | (~nibmask[i] & bg);
		for (i = 0; i < 256; i++)
			coltab[i] = (ULONG)t[i >> 4] << 16 | t[i & 15];
		colattr = attr;
	}
	for (i = n; i > 0 && s[i - 1] == ' '; i--)
		;
	if ((attr & (DA_INVERSE | DA_UNDERLINE)) != 0)
		i = n;
	if (i != 0 && hrlen[row] < col + i)
		hrlen[row] = col + i;
	p = hrptr(hroff(row, col, 0));
	kglyph4(p, s, n, font8x16, coltab);
	if (attr & DA_UNDERLINE)
		kfill2d(p + (CELLH - 1) * 320, (UWORD)coltab[255], n << 1, 1,
		    320);
	if (cuon && curow == row && cucol >= col && cucol < col + n)
		xorcell(row, cucol);
}

static void
hrput(row, col, c, attr)
int row, col, c, attr;
{
	register UBYTE *g, *p;
	register UWORD *w;
	register int y, b;
	UWORD fg, bg;
	UBYTE ch;

	g = glyph(c);
	if ((c & 0xff) != ' ' || (attr & (DA_INVERSE | DA_UNDERLINE)))
		if (hrlen[row] <= col)
			hrlen[row] = col + 1;
	if (hrmode != HRM_COLOR) {
		p = hrptr(hroff(row, col, 0));
		if (attr == DA_NORMAL) {
			ch = c;
			kglyphs(p, &ch, 1, font8x16);
		} else
			for (y = 0; y < CELLH; y++, p += 128) {
				b = g[y];
				if (attr & DA_BOLD)
					b |= b >> 1;
				if ((attr & DA_UNDERLINE) && y == CELLH - 1)
					b = 0xff;
				if (attr & DA_INVERSE)
					b = ~b;
				*p = b;
			}
	} else if (oneseg(hroff(row, col, 0))) {
		ch = c;
		coltext(row, col, &ch, 1, attr);
		return;
	} else {
		fg = (attr & DA_BOLD) ? COLBOLD * 0x1111 : COLFG * 0x1111;
		bg = COLBG * 0x1111;
		if (attr & DA_INVERSE) {
			b = fg;
			fg = bg;
			bg = b;
		}
		for (y = 0; y < CELLH; y++) {
			b = g[y];
			if ((attr & DA_UNDERLINE) && y == CELLH - 1)
				b = 0xff;
			w = (UWORD *)hrptr(hroff(row, col, y));
			w[0] = (nibmask[b >> 4] & fg) | (~nibmask[b >> 4] & bg);
			w[1] = (nibmask[b & 15] & fg) | (~nibmask[b & 15] & bg);
		}
	}
	if (cuon && curow == row && cucol == col)
		xorcell(row, col);
}

/* n cells of s from col on, all in the row. */
static void
hrtext(row, col, s, n, attr)
int row, col;
register UBYTE *s;
register int n;
int attr;
{
	register int i;

	if (n <= 0)
		return;
	if (hrmode == HRM_COLOR && oneseg(hroff(row, col, 0))) {
		coltext(row, col, s, n, attr);
		return;
	}
	if (hrmode == HRM_COLOR || attr != DA_NORMAL) {
		for (i = 0; i < n; i++)
			hrput(row, col + i, s[i], attr);
		return;
	}
	for (i = n; i > 0 && s[i - 1] == ' '; i--)
		;
	if (i != 0 && hrlen[row] < col + i)
		hrlen[row] = col + i;
	kglyphs(hrptr(hroff(row, col, 0)), s, n, font8x16);
	if (cuon && curow == row && cucol >= col && cucol < col + n)
		xorcell(row, cucol);
}

/* Blank cells col..col+n-1 of row; only DA_INVERSE shows in a blank. */
static void
blank(row, col, n, attr)
int row, col, n, attr;
{
	register UBYTE *p;
	register int y;
	UWORD v;
	ULONG l;

	if (n <= 0)
		return;
	if (hrmode == HRM_COLOR)
		v = (attr & DA_INVERSE) ? COLFG * 0x1111 : COLBG * 0x1111;
	else
		v = (attr & DA_INVERSE) ? 0xffff : 0;
	if (col == 0 && n == hrdisplay.dp_Cols) {
		lfill(hroff(row, 0, 0), v, (ULONG)CELLH * hrstride);
		return;
	}
	l = hroff(row, col, 0);
	if (!oneseg(l)) {
		for (y = 0; y < CELLH; y++, l += hrstride)
			lfill(l, v, (ULONG)n * hrcellb);
		return;
	}
	p = hrptr(l);
	if (hrmode == HRM_COLOR)
		kfill2d(p, v, n << 1, CELLH, 320);
	else if (((col | n) & 1) == 0)
		kfill2d(p, v, n >> 1, CELLH, 128);
	else
		for (y = 0; y < CELLH; y++, p += 128)
			bfill(p, v & 0xff, n);
}

static void
hrerase(row, col, n, attr)
int row, col, n, attr;
{
	if (n <= 0)
		return;
	blank(row, col, n, attr);
	if (attr & DA_INVERSE) {
		if (hrlen[row] < col + n)
			hrlen[row] = col + n;
	} else if (col + n >= hrlen[row] && col < hrlen[row])
		hrlen[row] = col;
	if (cuon && curow == row && cucol >= col && cucol < col + n)
		xorcell(row, cucol);
}

/* Copy the first n cells of rows d + 1 .. d + k up one row. */
static void
rowcopy(d, k, n)
int d, k, n;
{
	register int y;
	UWORD w;
	ULONG ld, ls, len;

	ld = hrrow[d];
	ls = hrrow[d + 1];
	len = (ULONG)k * CELLH * hrstride;
	w = (n * hrcellb + 1) >> 1;
	if ((ld & 0xffffL) + len <= SEGLEN && (ls & 0xffffL) + len <= SEGLEN) {
		kmove2d(hrptr(ld), hrptr(ls), w, k * CELLH, hrstride);
		return;
	}
	for (y = 0; y < k * CELLH; y++, ld += hrstride, ls += hrstride)
		if ((ld & 0xffffL) + (w << 1) <= SEGLEN &&
		    (ls & 0xffffL) + (w << 1) <= SEGLEN)
			kmove2d(hrptr(ld), hrptr(ls), w, 1, hrstride);
		else
			lmove(ld, ls, (ULONG)w << 1);
}

/* Cells a scroll copies onto row r. */
static int
rowneed(r)
register int r;
{
	return (hrlen[r] > hrlen[r + 1] ? hrlen[r] : hrlen[r + 1]);
}

static void
hrscroll(top, bot, attr)
int top, bot, attr;
{
	register int r, n, e;
	int cu;

	cu = cuon && curow >= top && curow <= bot;
	if (cu)
		xorcell(curow, cucol);
	/* Cells past both rows' lengths are blank, so copying the longer
	 * length also clears what the row held.  Rows needing the same
	 * length move together while both sides stay in one segment. */
	for (r = top; r < bot; r = e) {
		n = rowneed(r);
		for (e = r + 1; e < bot && rowneed(e) == n &&
		    (hrrow[r] >> 16) == ((hrrow[e + 1] - 1) >> 16) &&
		    (hrrow[r + 1] >> 16) == ((hrrow[e + 2] - 1) >> 16); e++)
			;
		if (n != 0)
			rowcopy(r, e - r, n);
		for (; r < e; r++)
			hrlen[r] = hrlen[r + 1];
	}
	n = hrdisplay.dp_Cols;
	if ((attr & DA_INVERSE) == 0 && hrlen[bot] < n)
		n = (hrlen[bot] + 1) & ~1;
	blank(bot, 0, n, attr);
	hrlen[bot] = (attr & DA_INVERSE) ? hrdisplay.dp_Cols : 0;
	if (cu)
		xorcell(curow, cucol);
}

static void
hrcursor(row, col, on)
int row, col, on;
{
	if (cuon)
		xorcell(curow, cucol);
	cuon = on;
	if (on) {
		curow = row;
		cucol = col;
		xorcell(row, col);
	}
}

char vidname[] = "hr.display";

/* The geometry of textcon.device's card and mode. */
struct Display *
vidsetup(cd)
struct ConDisplay *cd;
{
	register int r;

	if (cd->cd_Card != CDC_HR)
		return (NULL);
	hrmode = cd->cd_Mode;
	if (hrmode == HRM_COLOR) {
		if (coltab == NULL &&
		    (coltab = (ULONG *)AllocMem(256L * sizeof(ULONG),
		    MEMF_PUBLIC)) == NULL)
			return (NULL);
		hrstride = 320;
		hrcellb = 4;
		hrdisplay.dp_Cols = 80;
		hrdisplay.dp_Rows = 25;
	} else {
		hrstride = 128;
		hrcellb = 1;
		hrdisplay.dp_Cols = 128;
		hrdisplay.dp_Rows = hrmode == CDM_HR1024 ? 64 : 50;
	}
	for (r = 0; r < MAXROWS + 2; r++)
		hrrow[r] = (ULONG)r * CELLH * hrstride;
	return (&hrdisplay);
}
