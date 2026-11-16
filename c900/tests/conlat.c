/* SPDX-License-Identifier: MIT */
/*
 * Console drawing keeps interrupts serviced: serial input arrives
 * back to back at 9600 baud (one byte per 1.04 ms, three bytes of SCC
 * FIFO) while textcon.device scrolls the screen, and every byte must
 * reach serial.device unit 0 with no overrun.  A drawing step that held
 * VI off for more than about 3 ms would lose bytes on hardware (the Go
 * model paces its input, so there it checks delivery only).  Ticks are
 * counted against the battery clock, between two second boundaries: a
 * tick lost to a long VI-off section shows as fewer than 100 a second.
 * Without a display the writes go to the serial line.
 */
/* EMUFLAGS: --input-mark=CONLAT-GO --input=\ia\ib\ic\id\ie\if\ig\ih\ii\ij\ik\il\im\in\io\ip\iq\ir\is\it\iu\iv\iw\ix\iy\iz\iA\iB\iC\iD\iE\iF\iG\iH\iI\iJ\iK\iL\iM\iN\iO\iP\iQ\iR\iS\iT\iU\iV\iW\iX\iY\iZ\i0\i1\i2\i3\i4\i5\i6\i7\i8\i9\ia\ib\ic\id\ie\if\ig\ih\ii\ij\ik\il\im\in\io\ip\iq\ir\is\it\iu\iv\iw\ix\iy\iz\iA\iB\iC\iD\iE\iF\iG\iH\iI\iJ\iK\iL\iM\iN\iO\iP\iQ\iR\iS\iT\iU\iV\iW\iX\iY\iZ\i0\i1\i2\i3\i4\i5\i6\i7\i8\i9 */
#include "test.h"
#include "sertest.h"
#include <devices/textcon.h>
#include <resources/battclock.h>

APTR BattClockBase;

#define NIN	124
#define NLINE	80

static char buf[NIN + 2];
static char line[] = "conlat: scrolling the console while serial input arrives .......\n";
static char pat[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

/* The tick at which the battery clock's second changes; its time. */
static ULONG
edge(t)
ULONG *t;
{
	ULONG a, b;
	int n;

	a = ReadBattClock();
	for (n = 0; n < 300 && (b = ReadBattClock()) == a; n++)
		Sleep(1);
	*t = b;
	return (TickCount);
}

static int
cwrite(r, s)
struct IOStdReq *r;
char *s;
{
	r->io_Command = CMD_WRITE;
	r->io_Data = (UBYTE *)s;
	r->io_Length = -1;
	return (DoIO(r));
}

static char *
run(x, r)
struct IOExtSer *x;
struct IOStdReq *r;
{
	ULONG t0, t1, s0, s1;
	int i;

	cwrite(r, "\033[25;1H");
	sersend(x, CMD_READ, buf, (ULONG)NIN);
	t0 = edge(&s0);
	xKPrintf("CONLAT-GO\n");
	for (i = 0; i < NLINE; i++) {
		cwrite(r, line);
		if (i % 10 == 9)
			xKPrintf("conlat: %d\n", i + 1);	/* serial stays live */
	}
	t1 = edge(&s1);
	xKPrintf("conlat: %d lines; %ld ticks in %ld s\n", NLINE, t1 - t0,
	    s1 - s0);
	if (s0 != 0 && s1 > s0 + 1 && t1 - t0 < (s1 - s0) * 95)
		return ("ticks lost while drawing");
	if (!serwait(x, 500))
		return ("input did not all arrive");
	if (x->IOSer.io_Error != 0)
		return ("receive error (overrun)");
	if (x->IOSer.io_Actual != NIN || !sereq(buf, pat, 62) ||
	    !sereq(buf + 62, pat, 62))
		return ("input corrupted");
	return (NULL);
}

int
main()
{
	struct IOExtSer *x;
	struct MsgPort *port;
	struct IOStdReq *r;
	char *why;
	int err;

	BattClockBase = OpenResource(BATTCLOCKNAME);
	if ((x = seropen(0L, 0, &err)) == NULL) {
		xKPrintf("FAIL conlat: serial open %d\n", err);
		return (1);
	}
	port = CreateMsgPort();
	r = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
	if (OpenDevice(TEXTCONNAME, CONU_STANDARD, r, 0) != 0) {
		xKPrintf("FAIL conlat: console open\n");
		return (1);
	}
	why = run(x, r);
	CloseDevice(r);
	serclose(x);
	if (why != NULL) {
		xKPrintf("FAIL conlat: %s\n", why);
		return (1);
	}
	xKPrintf("PASS conlat\n");
	return (0);
}
