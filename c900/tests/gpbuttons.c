/* SPDX-License-Identifier: MIT */
/*
 * GetGPButtons on the HR mouse, polled by a Task that has not opened
 * gameport.device while another Task holds unit 0 with a read pending:
 * left, then right and middle, each pressed and released.  The holder
 * must also have had the button events.  Without an HR card the device
 * is absent; tests/sermouse.c covers the serial backend.
 */
/* MOUSE: 180000000:0:0:L,190000000:0:0,200000000:0:0:RM,210000000:0:0 */
#include "test.h"
#include "gptest.h"
#include "condisp.h"

#define LIMIT	2000

static char *
run()
{
	char *err;

	Forbid();
	GamePortBase = (struct Device *)FindName(&SysBase->DeviceList,
	    GAMEPORTNAME);
	Permit();
	if (GamePortBase == NULL)
		return ("no gameport.device");
	if ((err = gpholder()) != NULL)
		return (err);
	if (GetGPButtons() != 0)
		return ("a button is down at the start");
	xKPrintf("gpbuttons: ready\n");
	if ((err = gpwait("gpbuttons", (ULONG)IEQUALIFIER_LEFTBUTTON,
	    LIMIT)) != NULL ||
	    (err = gpwait("gpbuttons", 0L, LIMIT)) != NULL ||
	    (err = gpwait("gpbuttons", (ULONG)(IEQUALIFIER_RBUTTON |
	    IEQUALIFIER_MIDBUTTON), LIMIT)) != NULL ||
	    (err = gpwait("gpbuttons", 0L, LIMIT)) != NULL)
		return (err);
	Sleep(2);
	if ((err = gprelease()) != NULL)
		return (err);
	xKPrintf("gpbuttons: holder had %lu events\n", gpevents);
	return (gpevents < 4 ? "the holder missed button events" : NULL);
}

int
main()
{
	struct ConDisplay *cd;
	char *err;

	if ((cd = conask(0)) == NULL || cd->cd_Card != CDC_HR) {
		xKPrintf("gpbuttons: no HR card\nPASS gpbuttons\n");
		return (0);
	}
	if ((err = run()) != NULL) {
		xKPrintf("FAIL gpbuttons: %s\n", err);
		return (1);
	}
	xKPrintf("PASS gpbuttons\n");
	return (0);
}
