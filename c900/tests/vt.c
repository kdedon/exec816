/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The terminal emulator on a fake 10x4 display: graphics, deferred wrap,
 * scroll, CR/LF/BS/HT/VT/FF, CSI cursor motion, erase in line and
 * display, delete line, SGR, reports, modes, the cursor protocol (hidden
 * before any cell changes) and malformed sequences.
 */
#include "test.h"
#include "vt.h"

#define ROWS	4
#define COLS	10

static UBYTE chr[ROWS][COLS];
static UBYTE att[ROWS][COLS];
static int curon, currow, curcol, drawnon;
static char rep[40];
static int fails;

static void
fail(why)
char *why;
{
	xKPrintf("FAIL vt: %s\n", why);
	fails++;
}

static int bells;
static void finit() {}
static void fbell() { bells++; }

static void
fput(r, c, ch, a)
int r, c, ch, a;
{
	if (curon && r == currow && c == curcol)
		drawnon++;
	chr[r][c] = ch;
	att[r][c] = a;
}

static void
ferase(r, c, n, a)
int r, c, n, a;
{
	while (n-- > 0) {
		if (curon && r == currow && c == curcol)
			drawnon++;
		chr[r][c] = ' ';
		att[r][c] = a;
		c++;
	}
}

static void
fscroll(top, bot, a)
int top, bot, a;
{
	register int r, c;

	if (curon)
		drawnon++;
	for (r = top; r < bot; r++)
		for (c = 0; c < COLS; c++) {
			chr[r][c] = chr[r + 1][c];
			att[r][c] = att[r + 1][c];
		}
	for (c = 0; c < COLS; c++) {
		chr[bot][c] = ' ';
		att[bot][c] = a;
	}
}

static void
fcursor(r, c, on)
int r, c, on;
{
	curon = on;
	currow = r;
	curcol = c;
}

static char fname[] = "fake";
static struct Display fake = {
	fname, COLS, ROWS, NULL, finit, fput, fscroll, ferase, fcursor
};

static struct VT vt;

static void
freply(v, s)
struct VT *v;
char *s;
{
	register char *d;

	for (d = rep; *s != '\0' && d < rep + sizeof rep - 1; )
		*d++ = *s++;
	*d = '\0';
}

static void
blank()
{
	register int r, c;

	for (r = 0; r < ROWS; r++)
		for (c = 0; c < COLS; c++) {
			chr[r][c] = ' ';
			att[r][c] = 0;
		}
}

static void
out(s)
char *s;
{
	vt_write(&vt, (UBYTE *)s, -1L);
}

/* Row r reads s, padded with blanks. */
static void
row(r, s, why)
int r;
register char *s;
char *why;
{
	register int c;

	for (c = 0; c < COLS; c++) {
		if (chr[r][c] != (*s != '\0' ? *s : ' ')) {
			fail(why);
			xKPrintf(" %d,%d %c\n", r, c, chr[r][c]);
			return;
		}
		if (*s != '\0')
			s++;
	}
}

static void
at(r, c, why)
int r, c;
char *why;
{
	if (vt.vt_Row != r || vt.vt_Col != c || !curon || currow != r ||
	    curcol != c) {
		fail(why);
		xKPrintf(" %d,%d %d %d,%d\n", vt.vt_Row, vt.vt_Col,
		    curon, currow, curcol);
	}
}

static int
same(a, b)
register char *a, *b;
{
	while (*a != '\0' && *a == *b)
		a++, b++;
	return (*a == *b);
}

int
main()
{
	blank();
	vt_init(&vt, &fake);
	vt.vt_Reply = freply;
	at(0, 0, "home after");

	out("\007");
	vt.vt_Bell = fbell;
	out("\007");
	if (bells != 1)
		fail("BEL");
	at(0, 0, "BEL moved");

	out("abc");
	row(0, "abc", "graphics");
	at(0, 3, "cursor after");

	/* Deferred wrap: the tenth column holds the cursor until the next
	 * graphic. */
	out("\rABCDEFGHIJ");
	row(0, "ABCDEFGHIJ", "full row");
	at(0, 9, "cursor stays");
	out("K");
	row(1, "K", "wrapped graphic");
	at(1, 1, "wrap cursor");

	/* LF returns (LNM), BS, HT. */
	out("\nxy\bz\tq");
	row(2, "xz      q", "LF, BS and HT");
	out("\b\b\b\b\b\b\b\b\b\b\b\b");
	at(2, 0, "BS stops");

	/* Scroll at the bottom. */
	out("\n\nbottom\n");
	row(0, "xz      q", "scrolled up one");
	row(2, "bottom", "bottom");
	row(3, "", "new line blank");
	at(3, 0, "cursor on");

	/* FF clears and homes; VT moves up. */
	out("\f");
	row(0, "", "FF clears");
	at(0, 0, "FF homes");
	out("\033[3;5Hp\013q");
	if (chr[2][4] != 'p' || chr[1][5] != 'q')
		fail("CUP and");

	/* Cursor motion, clamped. */
	out("\033[H\033[2B\033[3C");
	at(2, 3, "CUD and");
	out("\033[99A\033[99D");
	at(0, 0, "CUU and");
	out("\033[4;10H\033[2E");
	at(3, 0, "CNL clamps");
	out("\033[F\033[6C");
	at(2, 6, "CPL");
	out("\2332;2H");
	at(1, 1, "8-bit CSI");
	out("\033[0;0H");
	at(0, 0, "zero parameters");

	/* Erase in line and display. */
	out("\f0123456789\033[2;1H0123456789\033[3;1H0123456789");
	out("\033[1;4H\033[K");
	row(0, "012", "EL to end");
	out("\033[2;4H\033[1K");
	row(1, "    456789", "EL to start");
	out("\033[3;1H\033[2K");
	row(2, "", "EL whole line");
	out("\033[2;6H\033[J");
	row(1, "    4", "ED0 line");
	row(3, "", "ED to end, below");
	out("\f0123456789\033[2;1H0123456789\033[2;3H\033[1J");
	row(0, "", "ED to start, above");
	row(1, "   3456789", "ED1 line");
	out("\033[2J");
	row(1, "", "ED all");
	at(1, 2, "ED2 cursor");

	/* Delete line, scroll up. */
	out("\f1\n2\n3\n4\033[2;1H\033[M");
	row(1, "3", "DL pulls lines up");
	row(3, "", "DL blanks the bottom");
	out("\033[S");
	row(0, "3", "SU");

	/* SGR. */
	out("\f\033[1;4mA\033[7mB\033[22;24mC\033[mD\033[0m");
	if (att[0][0] != (DA_BOLD | DA_UNDERLINE) ||
	    att[0][1] != (DA_BOLD | DA_UNDERLINE | DA_INVERSE) ||
	    att[0][2] != DA_INVERSE || att[0][3] != DA_NORMAL)
		fail("SGR attributes");

	/* Reports. */
	out("\033[2;3H\033[6n");
	if (!same(rep, "\2332;3R"))
		fail("cursor position");
	rep[0] = '\0';
	out("\2330 q");
	if (!same(rep, "\2331;1;4;10 r"))
		fail("window status");

	/* Modes: no autowrap, LF without return, Amiga cursor off/on. */
	out("\f\033[?7l0123456789XY");
	row(0, "012345678Y", "no autowrap");
	out("\033[?7h\033[20l\033[Hab\ncd\033[20h");
	if (chr[1][2] != 'c')
		fail("LF without");
	out("\2330 p");
	if (curon)
		fail("Amiga off");
	out("\233 p");
	if (!curon)
		fail("Amiga cursor");

	/* Save and restore; reset. */
	out("\033[3;3H\033[s\033[H\033[u");
	at(2, 2, "CSI s");
	out("\033[7mX\033c");
	row(0, "", "ESC c clears");
	at(0, 0, "RIS home");
	if (vt.vt_Attr != DA_NORMAL)
		fail("RIS attr");

	/* Malformed and ignored: CAN cancels, unknown finals and long
	 * parameter lists do nothing, controls run inside a sequence. */
	out("\033[12\030Z");
	row(0, "Z", "CAN cancels a sequence");
	out("\033[1;2;3;4;5;6;7HQ\033[99999C");
	at(0, 9, "extra parameters,");
	out("\r\033[2\rzW");
	out("\033[?1Pq\033#x\177\200");
	row(0, "Wqx", "ignored");

	/* The cursor was never shown under a cell being drawn. */
	if (drawnon != 0) {
		fail("a cell");
		xKPrintf(" %d\n", drawnon);
	}

	if (fails == 0)
		xKPrintf("PASS vt\n");
	return (fails);
}
