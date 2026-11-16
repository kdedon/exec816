/* SPDX-License-Identifier: MIT */
/*
 * The boot text replayed: the lines the kernel printed before the
 * display module loaded (the banner, the memory map) are on the screen,
 * above the module's own "console:" line.  The LR text RAM and the HR
 * bitmap are read back; without either it only passes.
 */
#include "test.h"
#include "hrtest.h"

static struct ConDisplay *cd;

/* Row r starts with s.  HR cells are compared with the font's glyphs. */
static int
rowis(r, s)
int r;
register char *s;
{
	UBYTE g[16];
	register int c, y;

	for (c = 0; *s != '\0'; c++, s++) {
		if (cd->cd_Card == CDC_LR) {
			if ((((UWORD *)((ULONG)LRSEG << 24))[r * 80 + c] & 0xff) !=
			    *s)
				return (0);
			continue;
		}
		cellget(r, c, g);
		for (y = 0; y < 16; y++)
			if (g[y] != want(*s, DA_NORMAL, y))
				return (0);
	}
	return (1);
}

/* The first row from r on that starts with s, or -1. */
static int
find(r, s)
register int r;
char *s;
{
	for (; r < rows; r++)
		if (rowis(r, s))
			return (r);
	return (-1);
}

int
main()
{
	int banner, mach, con;

	cd = conask(0);
	if (cd == NULL || (cd->cd_Card != CDC_LR && cd->cd_Card != CDC_HR)) {
		KPutStr("bootlog: no LR or HR card\nPASS bootlog\n");
		return (0);
	}
	if (cd->cd_Disp == NULL) {
		KPutStr("FAIL bootlog: card without its display module\n");
		return (1);
	}
	rows = cd->cd_Disp->dp_Rows;
	if (cd->cd_Card == CDC_HR)
		hrgeom(cd);
	banner = find(0, "Exec900 ");
	mach = banner < 0 ? -1 : find(banner + 1, "mach: ");
	con = mach < 0 ? -1 : find(mach + 1, "console: ");
	xKPrintf("bootlog: banner row %d, memory map row %d, console row %d\n",
	    banner, mach, con);
	if (banner < 0 || mach < 0)
		KPutStr("FAIL bootlog: boot text not on the screen\n");
	else if (con < 0)
		KPutStr("FAIL bootlog: no console line after the boot text\n");
	else
		KPutStr("PASS bootlog\n");
	return (0);
}
