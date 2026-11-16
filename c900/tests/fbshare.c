/* SPDX-License-Identifier: MIT */
/*
 * The HR bitmap lent through framebuf.resource (without an HR card, as
 * on the emulator, it only passes).  The client draws a pattern into both
 * 64 KB halves row by row with FB_ADDR and FB_ROWSPLIT and reads it back;
 * console output while lent (KPrintf, CMD_WRITE, a recoverable alert)
 * leaves the bitmap unchanged; gameport.device still opens; FreeFB clears
 * the bitmap and the console redraws the text printed while lent.
 */
#include "test.h"
#include "hrtest.h"
#include "fbtest.h"
#include <devices/gameport.h>

static char me[] = "fbshare";
static char wrote[] = "fbshare: written while lent\n";

/* Word i of row y. */
#define PAT(y, i)	((UWORD)(((y) << 7) ^ (i) ^ ((i) << 9) ^ 0x5a3c))

/* Draw (or, with check, compare) the pattern on every row: the row's
 * first FB_ROWSPLIT bytes at FB_ADDR, the rest at half 1.  The number of
 * words that differ. */
static ULONG
pattern(check)
int check;
{
	register UWORD *p;
	register UWORD i, n0, n;
	UWORD y;
	ULONG bad;

	bad = 0;
	n = fbm->fbm_BytesPerRow >> 1;
	for (y = 0; y < fbm->fbm_Height; y++) {
		p = (UWORD *)FB_ADDR(fbm, (ULONG)y * fbm->fbm_BytesPerRow);
		n0 = FB_ROWSPLIT(fbm, y) >> 1;
		for (i = 0; i < n; i++, p++) {
			if (i == n0)
				p = (UWORD *)fbm->fbm_Half[1];
			if (!check)
				*p = PAT(y, i);
			else if (*p != PAT(y, i))
				bad++;
		}
	}
	return (bad);
}

/* The whole bitmap memory, as a word sum. */
static UWORD
bitsum()
{
	register UWORD *p;
	register UWORD i, s;
	int h;

	s = 0;
	for (h = 0; h < 2; h++) {
		p = (UWORD *)fbm->fbm_Half[h];
		i = 0;
		do
			s += p[i];
		while (++i != 0x8000);
	}
	return (s);
}

/* Bytes from offset o to the end of the bitmap memory are zero. */
static int
zerofrom(o)
register ULONG o;
{
	for (; o < fbm->fbm_Size; o += 2)
		if (*(UWORD *)FB_ADDR(fbm, o) != 0)
			return (0);
	return (1);
}

/* A text row starts with s. */
static int
rowis(r, s)
int r;
register char *s;
{
	UBYTE g[16];
	register int c, y;

	for (c = 0; *s != '\0' && *s != '\n'; c++, s++) {
		cellget(r, c, g);
		for (y = 0; y < 16; y++)
			if (g[y] != want(*s, DA_NORMAL, y))
				return (0);
	}
	return (1);
}

static int
onscreen(s)
char *s;
{
	register int r;

	for (r = 0; r < rows; r++)
		if (rowis(r, s))
			return (r);
	return (-1);
}

static void
conwrite(s)
char *s;
{
	struct IOStdReq *ior;
	struct MsgPort *mp;

	mp = CreateMsgPort();
	ior = (struct IOStdReq *)CreateIORequest(mp, sizeof(*ior));
	if (ior != NULL && OpenDevice(TEXTCONNAME, CONU_STANDARD, ior, 0) == 0) {
		ior->io_Command = CMD_WRITE;
		ior->io_Data = (APTR)s;
		ior->io_Length = -1;
		DoIO(ior);
		CloseDevice(ior);
	}
	DeleteIORequest(ior);
	DeleteMsgPort(mp);
}

/* gameport.device opens and takes the mouse while the bitmap is lent. */
static int
mouseok()
{
	struct IOStdReq *ior;
	struct MsgPort *mp;
	BYTE ct;
	int ok;

	ok = 0;
	mp = CreateMsgPort();
	ior = (struct IOStdReq *)CreateIORequest(mp, sizeof(*ior));
	if (ior != NULL && OpenDevice(GAMEPORTNAME, 0, ior, 0) == 0) {
		ct = GPCT_MOUSE;
		ior->io_Command = GPD_SETCTYPE;
		ior->io_Data = (APTR)&ct;
		ior->io_Length = 1;
		ok = DoIO(ior) == 0;
		CloseDevice(ior);
	}
	DeleteIORequest(ior);
	DeleteMsgPort(mp);
	return (ok);
}

static char *
share()
{
	register int i;
	ULONG bad;
	UWORD sum;
	int r1, r2, r3;

	if (AllocFB(me) != NULL)
		return ("AllocFB refused");
	pattern(0);
	bad = pattern(1);
	sum = bitsum();
	for (i = 0; i < 6; i++)
		xKPrintf("fbshare: lent line %d\n", i);
	conwrite(wrote);
	Alert(0x1235L);
	i = mouseok();
	xKPrintf("fbshare: pattern %lu bad, sum %x\n", bad, sum);
	if (bad != 0)
		return ("pattern read back");
	if (!i)
		return ("gameport.device while lent");
	if (bitsum() != sum)
		return ("console drew while lent");
	FreeFB();
	r1 = onscreen("fbshare: lent line 5");
	r2 = onscreen(wrote);
	r3 = onscreen("exec900: alert 00001235");
	if (!zerofrom((ULONG)(fbm->fbm_Height - 1) * fbm->fbm_BytesPerRow))
		return ("bitmap not cleared on return");
	xKPrintf("fbshare: replayed rows %d %d %d\n", r1, r2, r3);
	if (r1 < 0 || r2 < 0 || r3 < 0)
		return ("text printed while lent not on the screen");
	return (NULL);
}

int
main()
{
	struct ConDisplay *cd;
	char *err;
	int card;

	if ((err = fbopen(&card)) == NULL) {
		cd = conask(0);
		if (card != CDC_HR) {
			xKPrintf("fbshare: no HR card\nPASS fbshare\n");
			return (0);
		}
		if (!hrgeom(cd))
			err = "HR card without its display module";
		else
			err = share();
	}
	if (err != NULL) {
		xKPrintf("FAIL fbshare: %s\n", err);
		return (1);
	}
	xKPrintf("PASS fbshare\n");
	return (0);
}
