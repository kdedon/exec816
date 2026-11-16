/* SPDX-License-Identifier: MIT */
/*
 * gameport.device on the HR mouse.  Without an HR card the device must be
 * absent.  With one: unit and controller-type checks, the trigger, a
 * timeout event, abort and flush, then motion and button events from the
 * injected mouse, and the controller type freed by the last close.
 */
/* MOUSE: 180000000:30:-20,184000000:0:0:L,188000000:0:0,192000000:0:0:R,196000000:0:0 */
#include "test.h"
#include <exec/exec.h>
#include <devices/gameport.h>
#include "condisp.h"

static struct IOStdReq *ior;

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

static char *
setup()
{
	struct GamePortTrigger t, u;
	struct InputEvent ev;
	BYTE ct;

	if (cmd(GPD_ASKCTYPE, &ct, 1L) != 0 || ct != GPCT_NOCONTROLLER)
		return ("initial controller type");
	ct = GPCT_RELJOYSTICK;
	if (cmd(GPD_SETCTYPE, &ct, 1L) != GPDERR_SETCTYPE)
		return ("joystick type accepted");
	if (cmd(GPD_READEVENT, &ev, (ULONG)sizeof ev) != GPDERR_SETCTYPE)
		return ("read without a controller");
	ct = GPCT_MOUSE;
	if (cmd(GPD_SETCTYPE, &ct, 1L) != 0)
		return ("set mouse");
	ct = 0;
	if (cmd(GPD_ASKCTYPE, &ct, 1L) != 0 || ct != GPCT_MOUSE)
		return ("ask mouse");
	if (cmd(GPD_READEVENT, &ev, (ULONG)sizeof ev - 1) != IOERR_BADLENGTH)
		return ("short read");
	t.gpt_Keys = GPTF_DOWNKEYS | GPTF_UPKEYS;
	t.gpt_Timeout = 3;
	t.gpt_XDelta = 1;
	t.gpt_YDelta = 1;
	if (cmd(GPD_SETTRIGGER, &t, (ULONG)sizeof t) != 0)
		return ("set trigger");
	if (cmd(GPD_ASKTRIGGER, &u, (ULONG)sizeof u) != 0 ||
	    u.gpt_Keys != t.gpt_Keys || u.gpt_Timeout != 3 ||
	    u.gpt_XDelta != 1 || u.gpt_YDelta != 1)
		return ("ask trigger");
	if (cmd(GPD_READEVENT, &ev, (ULONG)sizeof ev) != 0 ||
	    ior->io_Actual != sizeof ev || ev.ie_Class != IECLASS_TIMER ||
	    ev.ie_NextEvent != NULL)
		return ("timeout event");
	t.gpt_Timeout = 0;
	if (cmd(GPD_SETTRIGGER, &t, (ULONG)sizeof t) != 0)
		return ("clear timeout");
	return (cmd(CMD_CLEAR, NULL, 0L) != 0 ? "clear" : NULL);
}

static char *
cancel()
{
	struct IOStdReq *two;
	struct InputEvent ev, ev2;

	ior->io_Command = GPD_READEVENT;
	ior->io_Data = (UBYTE *)&ev;
	ior->io_Length = sizeof ev;
	SendIO(ior);
	Sleep(2);
	if (CheckIO(ior) != NULL)
		return ("read completed without input");
	AbortIO(ior);
	if (WaitIO(ior) != IOERR_ABORTED)
		return ("abort");
	two = (struct IOStdReq *)CreateIORequest(ior->io_Message.mn_ReplyPort,
	    sizeof(struct IOStdReq));
	if (two == NULL)
		return ("CreateIORequest");
	two->io_Device = ior->io_Device;
	two->io_Unit = ior->io_Unit;
	two->io_Command = GPD_READEVENT;
	two->io_Data = (UBYTE *)&ev2;
	two->io_Length = sizeof ev2;
	SendIO(two);
	if (cmd(CMD_FLUSH, NULL, 0L) != 0 || WaitIO(two) != IOERR_ABORTED)
		return ("flush");
	DeleteIORequest(two);
	return (cmd(CMD_CLEAR, NULL, 0L) != 0 ? "clear" : NULL);
}

/* Bits: left down, left up, right down, right up. */
#define SEEN_ALL	0x0f

static char *
input()
{
	struct InputEvent ev[4];
	register struct InputEvent *e;
	int seen, x, y, n;
	UWORD wait;

	seen = x = y = 0;
	xKPrintf("gameport: ready\n");
	while (seen != SEEN_ALL || x != 30 || y != -20) {
		ior->io_Command = GPD_READEVENT;
		ior->io_Data = (UBYTE *)ev;
		ior->io_Length = sizeof ev;
		SendIO(ior);
		for (wait = 0; CheckIO(ior) == NULL; wait++) {
			if (wait == 2000) {
				AbortIO(ior);
				WaitIO(ior);
				xKPrintf("gameport: x %d y %d seen %x\n", x, y, seen);
				return ("no input");
			}
			Sleep(1);
		}
		if (WaitIO(ior) != 0)
			return ("read");
		n = (int)(ior->io_Actual / sizeof ev[0]);
		if (n < 1 || n > 4 || ior->io_Actual % sizeof ev[0] != 0)
			return ("io_Actual");
		for (e = ev; e < ev + n; e++) {
			if (e->ie_NextEvent != (e + 1 < ev + n ? e + 1 : NULL))
				return ("event chain");
			if (e->ie_Class != IECLASS_RAWMOUSE ||
			    !(e->ie_Qualifier & IEQUALIFIER_RELATIVEMOUSE))
				return ("event class");
			x += e->ie_X;
			y += e->ie_Y;
			xKPrintf("gameport: code %x qual %x dx %d dy %d t %lu.%lu\n",
			    e->ie_Code, e->ie_Qualifier, e->ie_X, e->ie_Y,
			    e->ie_TimeStamp.tv_secs, e->ie_TimeStamp.tv_micro);
			switch (e->ie_Code) {
			case IECODE_LBUTTON:
				if (!(e->ie_Qualifier & IEQUALIFIER_LEFTBUTTON))
					return ("left qualifier");
				seen |= 1;
				break;
			case IECODE_LBUTTON | IECODE_UP_PREFIX:
				if (e->ie_Qualifier & IEQUALIFIER_LEFTBUTTON)
					return ("left up qualifier");
				seen |= 2;
				break;
			case IECODE_RBUTTON:
				if (!(e->ie_Qualifier & IEQUALIFIER_RBUTTON))
					return ("right qualifier");
				seen |= 4;
				break;
			case IECODE_RBUTTON | IECODE_UP_PREFIX:
				seen |= 8;
				break;
			case IECODE_NOBUTTON:
				break;
			default:
				return ("unexpected code");
			}
		}
	}
	return (NULL);
}

int
main()
{
	struct MsgPort *port;
	struct ConDisplay *cd;
	char *err;
	BYTE ct;

	port = CreateMsgPort();
	ior = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
	if (ior == NULL) {
		xKPrintf("FAIL gameport: CreateIORequest\n");
		return (1);
	}
	if (OpenDevice(GAMEPORTNAME, 0, ior, 0) != 0) {
		if ((cd = conask(0)) != NULL && cd->cd_Card == CDC_HR)
			xKPrintf("FAIL gameport: no device with an HR card\n");
		else
			xKPrintf("gameport: no HR card\nPASS gameport\n");
		return (0);
	}
	if ((err = setup()) == NULL && (err = cancel()) == NULL)
		err = input();
	if (err == NULL) {
		CloseDevice(ior);
		if (OpenDevice(GAMEPORTNAME, 1, ior, 0) == 0)
			err = "unit 1 opened";
		else if (OpenDevice(GAMEPORTNAME, 0, ior, 0) != 0)
			err = "reopen";
		else if (cmd(GPD_ASKCTYPE, &ct, 1L) != 0 ||
		    ct != GPCT_NOCONTROLLER)
			err = "controller type kept after the last close";
	}
	if (err != NULL) {
		xKPrintf("FAIL gameport: %s\n", err);
		return (1);
	}
	CloseDevice(ior);
	xKPrintf("PASS gameport\n");
	return (0);
}
