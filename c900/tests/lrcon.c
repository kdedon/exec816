/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The LR console, read back from the card: the boot transcript on the
 * screen, textcon.device escapes as character and MDA attribute words,
 * the CRTC cursor, kernel output sharing the screen, and scrolling.
 * Without an LR console (the emulator) it only checks that none is
 * claimed.
 */
#include "test.h"
#include "condisp.h"

#define CELL(r, c)	(((UWORD *)SEGBASE(LRSEG))[(r) * 80 + (c)])

static int fails;

static void
fail(why)
char *why;
{
	xKPrintf("FAIL lrcon: %s\n", why);
	fails++;
}

static void
want(r, c, w, why)
int r, c;
UWORD w;
char *why;
{
	if (CELL(r, c) != w) {
		fail(why);
		xKPrintf(" %d,%d %x\n", r, c, CELL(r, c));
	}
}

/* Row r starts with s. */
static int
rowis(r, s)
int r;
register char *s;
{
	register int c;

	for (c = 0; *s != '\0'; c++, s++)
		if ((CELL(r, c) & 0xff) != *s)
			return (0);
	return (1);
}

static int
crtc(r)
int r;
{
	outb(0x0410, r);
	return (inb(0x0413) & 0xff);
}

static struct IOStdReq *con;

static void
put(s)
char *s;
{
	con->io_Command = CMD_WRITE;
	con->io_Data = (UBYTE *)s;
	con->io_Length = -1;
	DoIO(con);
}

int
main()
{
	struct MsgPort *port;
	register struct ConDisplay *cd;
	register int r;

	cd = conask(0);
	if (cd == NULL || cd->cd_Card != CDC_LR) {
		xKPrintf("lrcon: no LR console\nPASS lrcon\n");
		return (0);
	}
	if (cd->cd_Disp == NULL) {
		xKPrintf("FAIL lrcon: LR card without its display module\n");
		return (1);
	}
	for (r = 0; r < 25 && !rowis(r, "Exec900 "); r++)
		;
	if (r == 25)
		fail("boot banner not on the screen");

	port = CreateMsgPort();
	con = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
	if (OpenDevice(TEXTCONNAME, CONU_STANDARD, con, 0) != 0) {
		xKPrintf("FAIL lrcon: open\n");
		return (1);
	}
	put("\fAB\033[7mC\033[m\033[3;5Hq\033[2;1H\033[4mU\033[1mV\033[0m");
	want(0, 0, 0x0741, "plain A");
	want(0, 1, 0x0742, "plain B");
	want(0, 2, 0x7043, "inverse C");
	want(2, 4, 0x0771, "CUP q");
	want(1, 0, 0x0155, "underline U");
	want(1, 1, 0x0956, "bold underline V");
	want(1, 2, 0x0720, "blank after");
	r = crtc(14) << 8 | crtc(15);
	if (r != 82)
		fail("hardware cursor");

	put("\033[1;2H\033[K");
	want(0, 1, 0x0720, "EL");
	want(0, 0, 0x0741, "EL kept column 0");

	/* Kernel output lands at the console cursor. */
	put("\033[5;1H");
	xKPrintf("lrcon: kprintf\n");
	if (!rowis(4, "lrcon: kprintf"))
		fail("kprintf on the screen");

	/* Fill past the bottom: the screen scrolls. */
	put("\033[25;1H");
	for (r = 0; r < 3; r++)
		xKPrintf("lrcon: line %d\n", r);
	if (!rowis(22, "lrcon: line 1") || !rowis(23, "lrcon: line 2"))
		fail("scroll");
	want(24, 0, 0x0720, "scrolled-in row blank");

	CloseDevice(con);
	if (fails == 0)
		xKPrintf("PASS lrcon\n");
	return (fails);
}
