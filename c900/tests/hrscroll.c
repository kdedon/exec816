/* SPDX-License-Identifier: MIT */
/*
 * HR display scrolling: full and region scrolls with rows of varied
 * length and the cursor checked in the bitmap, four full-screen scrolls
 * timed with full and quarter-width lines, and a banner left for the
 * screen capture.  Without a card it only passes.
 */
#include "test.h"
#include "hrtest.h"

static void
fillrows()
{
	register int r, c;

	for (r = 0; r < rows; r++)
		for (c = 0; c < rlen(r); c++)
			PUT(r, c, rch(r), DA_NORMAL);
}

/* Row r holds row src's pattern (src < 0: blank); the cells just past it
 * are blank. */
static int
rowis(r, src)
int r, src;
{
	register int c, n;

	n = src < 0 ? 0 : rlen(src);
	for (c = 0; c < n; c += n / 3 + 1)
		if (!cellis(r, c, rch(src), DA_NORMAL))
			return (0);
	if (n != 0 && !cellis(r, n - 1, rch(src), DA_NORMAL))
		return (0);
	for (c = n; c < cols && c < n + 2; c++)
		if (!cellis(r, c, ' ', DA_NORMAL))
			return (0);
	return (1);
}

static char *
scroll()
{
	register int r;

	(*hd->dp_Init)();
	fillrows();
	CURSOR(6, 2, 1);
	SCROLL(0, rows - 1, DA_NORMAL);
	if (!cellis(6, 2, rch(7), DA_INVERSE))
		return ("cursor after scroll");
	CURSOR(6, 2, 0);
	for (r = 0; r < rows - 1; r++)
		if (!rowis(r, r + 1)) {
			xKPrintf("row %u\n", r);
			return ("full scroll");
		}
	if (!rowis(rows - 1, -1))
		return ("scroll blank row");
	xKPrintf("hrscroll: full scroll\n");
	/* Rows 2..5 up one inside the screen; rows 0, 1 and 6 stay. */
	SCROLL(2, 5, DA_INVERSE);
	if (!rowis(1, 2) || !rowis(2, 4) || !rowis(4, 6) || !rowis(6, 7) ||
	    !cellis(5, 0, ' ', DA_INVERSE) || !cellis(5, cols - 1, ' ', DA_INVERSE))
		return ("region scroll");
	SCROLL(5, 5, DA_NORMAL);
	if (!rowis(5, -1))
		return ("one-row scroll");
	return (NULL);
}

/* Ticks for n full-screen scrolls of lines of len cells. */
static ULONG
timescroll(len, n)
int len, n;
{
	register int r, c, i;
	ULONG t;

	(*hd->dp_Init)();
	for (r = 0; r < rows; r++)
		for (c = 0; c < len; c++)
			PUT(r, c, 'x', DA_NORMAL);
	t = TickCount;
	for (i = 0; i < n; i++)
		SCROLL(0, rows - 1, DA_NORMAL);
	return (TickCount - t);
}

static void
banner()
{
	static char line[] = "Exec900 HR display 0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
	register int c;

	(*hd->dp_Init)();
	for (c = 0; line[c] != 0; c++)
		PUT(0, c, line[c], c < 7 ? DA_BOLD : DA_NORMAL);
	PUT(1, 0, 'I', DA_INVERSE);
	PUT(1, 1, 'U', DA_UNDERLINE);
	CURSOR(1, 2, 1);
}

int
main()
{
	char *err;
	ULONG full, part;

	if (!hrstart("hrscroll"))
		return (0);
	if ((err = scroll()) == NULL) {
		full = timescroll(cols, 4);
		part = timescroll(cols / 4, 4);
		xKPrintf("hrscroll: 4 scrolls %lu ticks, quarter lines %lu\n",
		    full, part);
		banner();
	}
	return (hrend("hrscroll", err));
}
