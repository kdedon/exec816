/* SPDX-License-Identifier: MIT */
/*
 * KBD_READMATRIX while keys are held, polled by a Task with its own
 * binding while another Task holds keyboard.device with a KBD_READEVENT
 * pending, as input.device does: Return, then Esc, pressed and released
 * in turn.  Each poll completes quick, in BeginIO, with nothing replied.
 * The holder must also have had the four events.
 */
/* KEYS: 150000000:0x1c,160000000:0x01,170000000:0x9c,180000000:0x81 */
/* EMUKEYS: 1000:1c,3000000:01,6000000:9c,9000000:81 */
/* HWMARK: kbdmatrix: ready */
#include "test.h"
#include "kbdtest.h"
#include <devices/rawkeycodes.h>

#define DONE_BIT	16
#define DONE		(1L << DONE_BIT)
#define LIMIT		2000
#define RET		(1 << (RAWKEY_RETURN & 7))
#define ESC		(1 << (RAWKEY_ESCAPE & 7))

static struct Task *parent, *holder;
static volatile int hstate;		/* 1 holding, 2 stopped, -1 failed */
static volatile int hstop;
static volatile ULONG hevents;

static void
hold()
{
	struct IOStdReq *r;
	struct InputEvent ev;
	ULONG got;
	int err;

	hstop = AllocSignal(-1);
	if (hstop < 0 || (r = kbdopen(0L, &err)) == NULL) {
		hstate = -1;
		Signal(parent, DONE);
		return;
	}
	hstate = 1;
	Signal(parent, DONE);
	for (;;) {
		kbdsend(r, KBD_READEVENT, &ev, (ULONG)sizeof ev);
		got = Wait((1L << r->io_Message.mn_ReplyPort->mp_SigBit) |
		    (1L << hstop));
		if (CheckIO(r) == NULL) {
			AbortIO(r);
			WaitIO(r);
			break;
		}
		WaitIO(r);
		hevents++;
		if (got & (1L << hstop))
			break;
	}
	kbdclose(r);
	hstate = 2;
	Signal(parent, DONE);
}

/* Poll until the matrix byte holding Return and Esc is want. */
static char *
poll(r, want)
register struct IOStdReq *r;
int want;
{
	UBYTE mx[KBD_MATRIXSIZE];
	struct MsgPort *port;
	int n, i;

	port = r->io_Message.mn_ReplyPort;
	for (n = 0; ; n++) {
		if (kbdcmd(r, KBD_READMATRIX, mx, (ULONG)sizeof mx) != 0 ||
		    r->io_Actual != KBD_MATRIXSIZE)
			return ("KBD_READMATRIX failed");
		if (!(r->io_Flags & IOF_QUICK) || !IsListEmpty(&port->mp_MsgList))
			return ("KBD_READMATRIX was not quick");
		for (i = 0; i < KBD_MATRIXSIZE; i++)
			if (i != RAWKEY_RETURN >> 3 && mx[i] != 0)
				return ("another key in the matrix");
		if (mx[RAWKEY_RETURN >> 3] == want)
			break;
		if (n == LIMIT) {
			xKPrintf("kbdmatrix: byte %x, want %x\n",
			    mx[RAWKEY_RETURN >> 3], want);
			return ("matrix");
		}
		Sleep(1);
	}
	xKPrintf("kbdmatrix: byte %x\n", want);
	return (NULL);
}

static char *
run(r)
struct IOStdReq *r;
{
	char *err;

	parent = FindTask(NULL);
	if (AllocSignal(DONE_BIT) != DONE_BIT)
		return ("AllocSignal");
	if ((holder = CreateTask("kbdholder", 0, hold, 2000)) == NULL)
		return ("CreateTask");
	while (hstate == 0)
		Wait(DONE);
	if (hstate != 1)
		return ("holder did not open the device");
	if ((err = poll(r, 0)) != NULL)
		return (err);
	xKPrintf("kbdmatrix: ready\n");
	if ((err = poll(r, RET)) != NULL || (err = poll(r, RET | ESC)) != NULL ||
	    (err = poll(r, ESC)) != NULL || (err = poll(r, 0)) != NULL)
		return (err);
	Sleep(2);
	Signal(holder, 1L << hstop);
	while (hstate == 1)
		Wait(DONE);
	xKPrintf("kbdmatrix: holder had %lu events\n", hevents);
	return (hevents != 4 ? "the holder's events" : NULL);
}

int
main()
{
	struct IOStdReq *r;
	char *why;
	int err;

	if ((r = kbdopen(0L, &err)) == NULL) {
		xKPrintf("FAIL kbdmatrix: OpenDevice unit 0: %d\n", err);
		return (1);
	}
	why = run(r);
	kbdclose(r);
	if (why != NULL) {
		xKPrintf("FAIL kbdmatrix: %s\n", why);
		return (1);
	}
	xKPrintf("PASS kbdmatrix\n");
	return (0);
}
