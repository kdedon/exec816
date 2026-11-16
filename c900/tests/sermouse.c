/* SPDX-License-Identifier: MIT */
/*
 * gameport.device on the serial mouse: listed in Modules on a machine
 * without an HR card, it owns serial.device unit 1 (U74 channel A), so
 * that unit refuses OpenDevice.  Once the test sends a byte on the line,
 * the peer sends two junk bytes and five Mouse Systems packets: motion,
 * left down, left up, right and middle down, both up with motion.  Each
 * half packet with motion is one event, Y is negated to down-positive,
 * and every button change is one event, in order.  Then GetGPButtons,
 * polled while another Task holds the unit with a read pending, follows
 * three more packets, each sent on request: left, right and middle, none.
 */
/* SEND: 1 0510870503020083000000008700000000840000000087ff040000/8300000000/8400000000/8700000000 */
#include "test.h"
#include "sertest.h"
#include "gptest.h"
#include "condisp.h"

void outb();

#define SCCA_DATA	0x0131		/* U74 channel A, WR8 */
#define NEV		8

static struct IOStdReq *ior;

static struct {
	UWORD code;
	UWORD qual;
	WORD x;
	WORD y;
} want[NEV] = {
	{ IECODE_NOBUTTON, 0, 5, -3 },
	{ IECODE_NOBUTTON, 0, 2, 0 },
	{ IECODE_LBUTTON, IEQUALIFIER_LEFTBUTTON, 0, 0 },
	{ IECODE_LBUTTON | IECODE_UP_PREFIX, 0, 0, 0 },
	{ IECODE_RBUTTON, IEQUALIFIER_RBUTTON, 0, 0 },
	{ IECODE_MBUTTON, IEQUALIFIER_RBUTTON | IEQUALIFIER_MIDBUTTON, 0, 0 },
	{ IECODE_RBUTTON | IECODE_UP_PREFIX, IEQUALIFIER_MIDBUTTON, -1, -4 },
	{ IECODE_MBUTTON | IECODE_UP_PREFIX, 0, 0, 0 }
};

static int
cmd(c, data, len)
int c;
APTR data;
ULONG len;
{
	ior->io_Command = c;
	ior->io_Data = (UBYTE *)data;
	ior->io_Length = len;
	return (DoIO(ior));
}

/* Unit 1 is the mouse's, shared or not. */
static char *
owned()
{
	struct IOExtSer *x;
	int err;

	if ((x = seropen(1L, 0, &err)) != NULL) {
		serclose(x);
		return ("serial unit 1 opened");
	}
	if (err != SerErr_DevBusy)
		return ("serial unit 1: not busy");
	if ((x = seropen(1L, SERF_SHARED, &err)) != NULL) {
		serclose(x);
		return ("serial unit 1 opened shared");
	}
	return (err != SerErr_DevBusy ? "shared unit 1: not busy" : NULL);
}

/* Start a read of up to NEV events; TRUE when it completes in time. */
static int
readev(ev, wait)
struct InputEvent *ev;
int wait;
{
	ior->io_Command = GPD_READEVENT;
	ior->io_Data = (UBYTE *)ev;
	ior->io_Length = NEV * sizeof ev[0];
	SendIO(ior);
	for (; CheckIO(ior) == NULL; wait--) {
		if (wait == 0) {
			AbortIO(ior);
			WaitIO(ior);
			return (FALSE);
		}
		Sleep(1);
	}
	return (WaitIO(ior) == 0);
}

static char *
input()
{
	struct InputEvent ev[NEV];
	register struct InputEvent *e;
	struct GamePortTrigger t;
	int got, n;
	BYTE ct;

	ct = GPCT_MOUSE;
	if (cmd(GPD_SETCTYPE, &ct, 1L) != 0)
		return ("set mouse");
	t.gpt_Keys = GPTF_DOWNKEYS | GPTF_UPKEYS;
	t.gpt_Timeout = 0;
	t.gpt_XDelta = 1;
	t.gpt_YDelta = 1;
	if (cmd(GPD_SETTRIGGER, &t, (ULONG)sizeof t) != 0)
		return ("set trigger");
	outb(SCCA_DATA, 'G');
	for (got = 0; got < NEV; ) {
		if (!readev(ev, 2000)) {
			xKPrintf("sermouse: %d events\n", got);
			return ("no input");
		}
		n = (int)(ior->io_Actual / sizeof ev[0]);
		for (e = ev; e < ev + n && got < NEV; e++, got++) {
			xKPrintf("sermouse: code %x qual %x dx %d dy %d\n",
			    e->ie_Code, e->ie_Qualifier, e->ie_X, e->ie_Y);
			if (e->ie_Class != IECLASS_RAWMOUSE ||
			    e->ie_Code != want[got].code ||
			    e->ie_Qualifier != (want[got].qual |
			    IEQUALIFIER_RELATIVEMOUSE) ||
			    e->ie_X != want[got].x || e->ie_Y != want[got].y)
				return ("wrong event");
		}
		if (e != ev + n)
			return ("too many events");
	}
	if (readev(ev, 20))
		return ("an event after the last packet");
	return (NULL);
}

/* GetGPButtons through the next packets, the unit held by gpholder. */
static char *
polled()
{
	static ULONG want[3] = {
		IEQUALIFIER_LEFTBUTTON,
		IEQUALIFIER_RBUTTON | IEQUALIFIER_MIDBUTTON,
		0
	};
	char *err;
	int i;

	GamePortBase = ior->io_Device;
	if (GetGPButtons() != 0)
		return ("a button is down after the first packets");
	if ((err = gpholder()) != NULL)
		return (err);
	for (i = 0; i < 3; i++) {
		outb(SCCA_DATA, 'G');
		if ((err = gpwait("sermouse", want[i], 2000)) != NULL)
			return (err);
	}
	Sleep(2);
	if ((err = gprelease()) != NULL)
		return (err);
	xKPrintf("sermouse: holder had %lu events\n", gpevents);
	return (gpevents != 6 ? "the holder's events" : NULL);
}

int
main()
{
	struct MsgPort *port;
	struct ConDisplay *cd;
	char *err;

	if ((cd = conask(0)) != NULL && cd->cd_Card == CDC_HR) {
		xKPrintf("sermouse: HR card\nPASS sermouse\n");
		return (0);
	}
	port = CreateMsgPort();
	ior = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
	if (ior == NULL) {
		xKPrintf("FAIL sermouse: CreateIORequest\n");
		return (1);
	}
	if (OpenDevice(GAMEPORTNAME, 0, ior, 0) != 0) {
		xKPrintf("FAIL sermouse: no gameport.device\n");
		return (1);
	}
	if ((err = owned()) == NULL && (err = input()) == NULL)
		err = polled();
	CloseDevice(ior);
	if (err != NULL) {
		xKPrintf("FAIL sermouse: %s\n", err);
		return (1);
	}
	xKPrintf("PASS sermouse\n");
	return (0);
}
