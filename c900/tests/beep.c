/* SPDX-License-Identifier: MIT */
/*
 * beep.device: a tone programs CIO U66 C/T 2 (TC = 187500 / f, square
 * wave on PB0) and lasts its io_Micros in ticks; queued tones play in
 * order; a rest and a zero-length tone are silent; AbortIO and CMD_FLUSH
 * end tones; an out-of-range frequency is refused; con_bell sounds and
 * stops on its own.  Checks read the CIO's registers back, so they hold
 * wherever U66 is modelled as a register file or a chip.
 */
#include "test.h"
#include <exec/exec.h>
#include <devices/beep.h>

#define MCCR		0x0083
#define CT2TCH		0x00b1
#define CT2TCL		0x00b3
#define PBDD		0x00d7

static struct IOBeep *a, *b;

static int
sounding()
{
	return ((inb(MCCR) & 0x20) != 0 && (inb(PBDD) & 0x01) == 0);
}

static int
silent()
{
	return ((inb(MCCR) & 0x20) == 0 && (inb(PBDD) & 0x01) != 0);
}

static int
tc()
{
	return ((inb(CT2TCH) & 0xff) << 8 | (inb(CT2TCL) & 0xff));
}

static void
tone(r, f, us)
struct IOBeep *r;
ULONG f, us;
{
	r->io_Req.io_Command = CMD_WRITE;
	r->io_Freq = f;
	r->io_Micros = us;
}

static struct IOBeep *
open1(err)
int *err;
{
	struct MsgPort *port;
	struct IOBeep *r;

	*err = 1;
	if ((port = CreateMsgPort()) == NULL)
		return (NULL);
	r = (struct IOBeep *)CreateIORequest(port, sizeof(struct IOBeep));
	if (r == NULL)
		return (NULL);
	if ((*err = OpenDevice(BEEPNAME, 0L, r, 0)) != 0)
		return (NULL);
	return (r);
}

static void
close1(r)
struct IOBeep *r;
{
	struct MsgPort *port;

	if (CheckIO(r) == NULL) {
		AbortIO(r);
		WaitIO(r);
	}
	port = r->io_Req.io_Message.mn_ReplyPort;
	CloseDevice(r);
	DeleteIORequest(r);
	DeleteMsgPort(port);
}

static char *
run()
{
	ULONG t0, dt;
	int i;

	if (!silent())
		return ("the speaker is not silent at boot");

	tone(a, 2L, 10000L);
	if (DoIO(a) != BeepErr_Freq)
		return ("2 Hz was not refused");
	tone(a, 20001L, 10000L);
	if (DoIO(a) != BeepErr_Freq)
		return ("20001 Hz was not refused");
	tone(a, 440L, 0L);
	if (DoIO(a) != 0 || (a->io_Req.io_Flags & IOF_QUICK) == 0 ||
	    !silent())
		return ("a zero-length tone did not complete quick and silent");
	a->io_Req.io_Command = CMD_READ;
	if (DoIO(a) != IOERR_NOCMD)
		return ("CMD_READ was not IOERR_NOCMD");

	/* 50 ms: 5 ticks, plus one for the partial first tick. */
	tone(a, 440L, 50000L);
	t0 = TickCount;
	SendIO(a);
	if (!sounding() || tc() != 426)
		return ("440 Hz did not program C/T 2 with 426");
	WaitIO(a);
	dt = TickCount - t0;
	if (a->io_Req.io_Error != 0 || dt < 5 || dt > 7) {
		xKPrintf("tone took %lu ticks\n", dt);
		return ("a 50 ms tone did not last 5-7 ticks");
	}
	if (!silent())
		return ("the speaker sounded after the tone");

	/* Two in order: the second starts when the first ends. */
	tone(a, 1000L, 30000L);
	tone(b, 0L, 30000L);
	SendIO(a);
	SendIO(b);
	if (tc() != 187)
		return ("1000 Hz did not program C/T 2 with 187");
	WaitIO(a);
	if (CheckIO(b) != NULL)
		return ("the queued rest finished with the tone before it");
	if (!silent())
		return ("a rest sounded");
	WaitIO(b);
	if (a->io_Req.io_Error != 0 || b->io_Req.io_Error != 0)
		return ("queued tones failed");

	/* AbortIO of the sounding tone starts the next one. */
	tone(a, 2000L, 5000000L);
	tone(b, 500L, 20000L);
	SendIO(a);
	SendIO(b);
	Sleep(2);
	AbortIO(a);
	if (WaitIO(a) != IOERR_ABORTED)
		return ("AbortIO did not end a sounding tone");
	if (!sounding() || tc() != 375)
		return ("the tone behind an aborted one did not start");
	if (WaitIO(b) != 0 || !silent())
		return ("the tone behind an aborted one did not finish");

	/* CMD_FLUSH from another binding. */
	tone(a, 3000L, 5000000L);
	SendIO(a);
	b->io_Req.io_Command = CMD_FLUSH;
	if (DoIO(b) != 0 || WaitIO(a) != IOERR_ABORTED || !silent())
		return ("CMD_FLUSH did not end a tone");

	/* The bell: on at once, off within 15 ticks. */
	con_bell();
	if (!sounding() || tc() != 426)
		return ("con_bell did not sound 440 Hz");
	for (i = 0; i < 20 && !silent(); i++)
		Sleep(1);
	if (i < 10 || i > 15) {
		xKPrintf("bell took %d ticks\n", i);
		return ("con_bell did not stop after 10-15 ticks");
	}
	return (NULL);
}

int
main()
{
	char *why;
	int err;

	if ((a = open1(&err)) == NULL || (b = open1(&err)) == NULL) {
		xKPrintf("FAIL beep: OpenDevice: %d\n", err);
		return (1);
	}
	why = run();
	close1(b);
	close1(a);
	if (why != NULL) {
		xKPrintf("FAIL beep: %s\n", why);
		return (1);
	}
	xKPrintf("PASS beep\n");
	return (0);
}
