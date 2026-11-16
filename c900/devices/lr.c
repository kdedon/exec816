/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The LR card: an HD6545 CRTC over 80x25 text cells, the console's
 * display backend.  A cell is one word in the text RAM at physical 0x37
 * (logical LRSEG, identity): attribute in the high (even) byte, character
 * in the low byte.  Only word writes reach the attribute RAM, so every
 * access is a word.  Attributes are the MDA set the card's attribute PLA
 * decodes.  The CRTC table is the boot ROM's.  Everything is polled; block
 * moves and fills are LDIR, which interrupts can enter.  textcon.device
 * found the card and mapped LRSEG.
 */
#include <exec/exec.h>
#include "c900.h"
#include "display.h"

void outb();
void kwmove();
void kwfill();

#define CRTC_IDX	0x0410
#define CRTC_DATA	0x0412
#define LR_CTL		0x0418
#define CTL_OFF		0x00
#define CTL_ON		0x28	/* video and blink enabled */

#define R_CURST		10
#define R_CURPOS	14
#define CUR_ON		0x0b	/* steady, lines 11-12, as the ROM */
#define CUR_OFF		0x20	/* cursor not displayed */

#define NCOL		80
#define NROW		25
#define A_NORM		0x07
#define A_UNDER		0x01
#define A_INV		0x70
#define A_INTENSE	0x08
#define A_BLINK		0x80
#define BLANK		((A_NORM << 8) | ' ')

#define LRTEXT		((UWORD *)((ULONG)LRSEG << 24))

static void
crtc(r, v)
int r, v;
{
	outb(CRTC_IDX, r);
	outb(CRTC_DATA, v);
}

static UWORD
cell(c, attr)
int c, attr;
{
	register UWORD a;

	if (attr & DA_INVERSE)
		a = A_INV;
	else if (attr & DA_UNDERLINE)
		a = A_UNDER;
	else
		a = A_NORM;
	if (attr & DA_BOLD)
		a |= A_INTENSE;
	if (attr & DA_BLINK)
		a |= A_BLINK;
	return ((a << 8) | (c & 0xff));
}

/* The boot ROM's CRTC table, as code: the image's data is scarcer than
 * its text. */
static void
lrinit()
{
	register int r;

	outb(LR_CTL, CTL_OFF);
	kwfill(LRTEXT, (UWORD)BLANK, (UWORD)(NCOL * NROW));
	crtc(0, 0x61);
	crtc(1, 0x50);
	crtc(2, 0x52);
	crtc(3, 0x0f);
	crtc(4, 0x19);
	crtc(5, 0x06);
	crtc(6, 0x19);
	crtc(7, 0x19);
	crtc(8, 0x02);
	crtc(9, 0x0d);
	crtc(R_CURST, CUR_ON);
	crtc(11, 0x0c);
	for (r = 12; r < 16; r++)
		crtc(r, 0);
	outb(LR_CTL, CTL_ON);
}

static void
lrput(row, col, c, attr)
int row, col, c, attr;
{
	LRTEXT[row * NCOL + col] = cell(c, attr);
}

/* n cells of s from col on, all in the row. */
static void
lrtext(row, col, s, n, attr)
int row, col;
register UBYTE *s;
register int n;
int attr;
{
	register UWORD *p, a;

	a = cell(0, attr);
	p = LRTEXT + row * NCOL + col;
	while (n-- > 0)
		*p++ = a | *s++;
}

static void
lrerase(row, col, n, attr)
int row, col, n, attr;
{
	if (n > 0)
		kwfill(LRTEXT + row * NCOL + col, cell(' ', attr & DA_INVERSE),
		    (UWORD)n);
}

static void
lrscroll(top, bot, attr)
int top, bot, attr;
{
	register UWORD *d;

	d = LRTEXT + top * NCOL;
	kwmove(d, d + NCOL, (UWORD)((bot - top) * NCOL));
	kwfill(LRTEXT + bot * NCOL, cell(' ', attr & DA_INVERSE), (UWORD)NCOL);
}

static void
lrcursor(row, col, on)
int row, col, on;
{
	register unsigned a;

	if (on) {
		a = row * NCOL + col;
		crtc(R_CURPOS, a >> 8);
		crtc(R_CURPOS + 1, a & 0xff);
	}
	crtc(R_CURST, on ? CUR_ON : CUR_OFF);
}

static char lrname[] = "lr";
char vidname[] = "lr.display";

static struct Display lrdisplay = {
	lrname, NCOL, NROW, NULL, lrinit, lrput, lrscroll, lrerase,
	lrcursor, lrtext
};

struct Display *
vidsetup(cd)
struct ConDisplay *cd;
{
	return (cd->cd_Card == CDC_LR ? &lrdisplay : NULL);
}
