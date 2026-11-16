/* SPDX-License-Identifier: MIT */
/*
 * The HR display backend's cells, taken from the console (without a card,
 * as on the emulator, it only passes): glyphs, attributes, the substitute
 * glyph, both bitmap halves, erase and the cursor, checked in the bitmap
 * in whichever mode the card reports.
 */
#include "test.h"
#include "hrtest.h"

static char *
cells()
{
	static char hello[] = "Hello, HR";
	register int i;

	for (i = 0; hello[i] != 0; i++)
		PUT(0, i, hello[i], DA_NORMAL);
	for (i = 0; hello[i] != 0; i++)
		if (!cellis(0, i, hello[i], DA_NORMAL))
			return ("put");
	PUT(1, 0, 'B', DA_INVERSE);
	PUT(1, 1, 'B', DA_BOLD);
	PUT(1, 2, 'B', DA_UNDERLINE);
	PUT(1, 3, 'B', DA_INVERSE | DA_UNDERLINE | DA_BOLD);
	PUT(1, 4, 'B', DA_BLINK);
	if (!cellis(1, 0, 'B', DA_INVERSE) || !cellis(1, 1, 'B', DA_BOLD) ||
	    !cellis(1, 2, 'B', DA_UNDERLINE) ||
	    !cellis(1, 3, 'B', DA_INVERSE | DA_UNDERLINE | DA_BOLD) ||
	    !cellis(1, 4, 'B', DA_NORMAL))
		return ("attributes");
	/* Corners, the second bitmap half, and a colour row crossing it. */
	PUT(rows - 1, cols - 1, 'Z', DA_NORMAL);
	PUT(rows - 1, 0, 'z', DA_NORMAL);
	PUT(0, cols - 1, 'q', DA_NORMAL);
	PUT(rows / 2, cols - 1, '#', DA_NORMAL);
	PUT(12, 63, '<', DA_NORMAL);
	PUT(12, 64, '>', DA_NORMAL);
	if (!cellis(rows - 1, cols - 1, 'Z', DA_NORMAL) ||
	    !cellis(rows - 1, 0, 'z', DA_NORMAL) ||
	    !cellis(0, cols - 1, 'q', DA_NORMAL) ||
	    !cellis(rows / 2, cols - 1, '#', DA_NORMAL) ||
	    !cellis(12, 63, '<', DA_NORMAL) || !cellis(12, 64, '>', DA_NORMAL))
		return ("edges");
	PUT(2, 0, 0xe9, DA_NORMAL);
	if (!cellis(2, 0, '?', DA_NORMAL))
		return ("substitute");
	/* A run of cells, plain (ending at the last column) and inverse. */
	(*hd->dp_Text)(4, cols - 4, (UBYTE *)"ab\001d", 4, DA_NORMAL);
	(*hd->dp_Text)(7, 2, (UBYTE *)"xy", 2, DA_INVERSE);
	if (!cellis(4, cols - 4, 'a', DA_NORMAL) ||
	    !cellis(4, cols - 3, 'b', DA_NORMAL) ||
	    !cellis(4, cols - 2, '?', DA_NORMAL) ||
	    !cellis(4, cols - 1, 'd', DA_NORMAL) ||
	    !cellis(7, 2, 'x', DA_INVERSE) || !cellis(7, 3, 'y', DA_INVERSE))
		return ("text");
	return (NULL);
}

static char *
erase()
{
	ERASE(0, 1, 3, DA_NORMAL);
	if (!cellis(0, 0, 'H', DA_NORMAL) || !cellis(0, 1, ' ', DA_NORMAL) ||
	    !cellis(0, 3, ' ', DA_NORMAL) || !cellis(0, 4, 'o', DA_NORMAL))
		return ("erase");
	ERASE(1, 0, 3, DA_INVERSE);
	if (!cellis(1, 0, ' ', DA_INVERSE) || !cellis(1, 2, ' ', DA_INVERSE) ||
	    !cellis(1, 3, 'B', DA_INVERSE | DA_UNDERLINE | DA_BOLD))
		return ("inverse erase");
	ERASE(3, 0, cols, DA_INVERSE);
	if (!cellis(3, 0, ' ', DA_INVERSE) ||
	    !cellis(3, cols - 1, ' ', DA_INVERSE))
		return ("inverse row");
	ERASE(3, 0, cols, DA_NORMAL);
	if (!cellis(3, 0, ' ', DA_NORMAL) || !cellis(3, cols - 1, ' ', DA_NORMAL))
		return ("row erase");
	return (NULL);
}

static char *
cursor()
{
	CURSOR(4, 5, 1);
	if (!cellff(4, 5))
		return ("cursor on");
	PUT(4, 5, 'C', DA_NORMAL);
	if (!cellis(4, 5, 'C', DA_INVERSE))
		return ("put at cursor");
	CURSOR(4, 5, 0);
	if (!cellis(4, 5, 'C', DA_NORMAL))
		return ("cursor off");
	CURSOR(5, 3, 1);
	CURSOR(5, 4, 1);
	if (!cellis(5, 3, ' ', DA_NORMAL) || !cellff(5, 4))
		return ("cursor move");
	ERASE(5, 0, 8, DA_NORMAL);
	if (!cellff(5, 4))
		return ("erase at cursor");
	CURSOR(5, 4, 0);
	if (!cellis(5, 4, ' ', DA_NORMAL))
		return ("cursor off 2");
	return (NULL);
}

int
main()
{
	char *err;

	if (!hrstart("hrdisplay"))
		return (0);
	if ((err = cells()) == NULL && (err = erase()) == NULL)
		err = cursor();
	return (hrend("hrdisplay", err));
}
