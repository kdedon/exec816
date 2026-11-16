/* SPDX-License-Identifier: MIT */
/*
 * keyboard.device with injected keys (sim only).  The first batch arrives
 * by interrupt while a KBD_READEVENT waits: Amiga raw codes translated
 * from the keyboard's, the up prefix, the shift, control and caps-lock
 * qualifiers, a repeat, the matrix and the console's characters "hhA",
 * Ctrl-C and "B".  The second batch is polled by kbd_getc with VI off:
 * caps lock off again, the cursor-up sequence, "1" and the keypad "00";
 * its events, read afterwards, carry the keypad qualifier.
 */
/* KEYS: 150000000:0x23,150000000:0x23,150000000:0xa3,150000000:0x2a,150000000:0x1e,150000000:0x9e,150000000:0xaa,150000000:0x1d,150000000:0x2e,150000000:0xae,150000000:0x9d,150000000:0x3a,150000000:0xba,150000000:0x30,150000000:0xb0,190000000:0x3a,190000000:0xba,190000000:0x5f,190000000:0xdf,190000000:0x02,190000000:0x82,190000000:0x53,190000000:0xd3 */
/* EMUKEYS: 1000:23,1000:23,1000:a3,1000:2a,1000:1e,1000:9e,1000:aa,1000:1d,1000:2e,1000:ae,1000:9d,1000:3a,1000:ba,1000:30,1000:b0,3000000:3a,3000000:ba,3000000:5f,3000000:df,3000000:02,3000000:82,3000000:53,3000000:d3 */
/* HWMARK: kbdkeys: ready */
#include "test.h"
#include "kbdtest.h"
#include <devices/newmouse.h>
#include <devices/rawkeycodes.h>

/* The AROS NewMouse extension, checked while this test builds. */
#if IECLASS_NEWMOUSE != 0x16 || IECLASS_MAX != IECLASS_NEWMOUSE || \
    NM_WHEEL_UP != RAWKEY_NM_WHEEL_UP || NM_WHEEL_DOWN != RAWKEY_NM_WHEEL_DOWN || \
    NM_WHEEL_LEFT != RAWKEY_NM_WHEEL_LEFT || \
    NM_WHEEL_RIGHT != RAWKEY_NM_WHEEL_RIGHT || \
    NM_BUTTON_FOURTH != RAWKEY_NM_BUTTON_FOURTH
#error NewMouse codes
#endif

#define NEV1	15
#define NEV2	8
#define POLLS	200000L

#define SH	IEQUALIFIER_LSHIFT
#define CT	IEQUALIFIER_CONTROL
#define CL	IEQUALIFIER_CAPSLOCK
#define KP	IEQUALIFIER_NUMERICPAD

/* H H H^ LShift A A^ LShift^ Ctrl C C^ Ctrl^ Caps Caps^ B B^ */
static UBYTE code1[NEV1] = {
	0x25, 0x25, 0xa5, 0x60, 0x20, 0xa0, 0xe0, 0x63, 0x33, 0xb3, 0xe3,
	0x62, 0xe2, 0x35, 0xb5
};
static UWORD qual1[NEV1] = {
	0, IEQUALIFIER_REPEAT, 0, SH, SH, SH, 0, CT, CT, CT, 0,
	0, CL, CL, CL
};
static char cook1[] = { 'h', 'h', 'A', 0x03, 'B' };
static char cook2[] = { 0x1b, '[', 'A', '1', '0', '0' };

/* Caps Caps^ Up Up^ 1 1^ KP00 KP00^ */
static UBYTE code2[NEV2] = {
	0x62, 0xe2, 0x4c, 0xcc, 0x01, 0x81, 0x0e, 0x8e
};
static UWORD qual2[NEV2] = { CL, 0, 0, 0, 0, 0, KP, KP };

static struct InputEvent ev[NEV1];
static UBYTE mx[KBD_MATRIXSIZE];

static char *
getn(want, n)
register char *want;
int n;
{
	register int i, c;

	for (i = 0; i < n; i++) {
		if ((c = kbd_getc()) != (want[i] & 0xff)) {
			xKPrintf("kbdkeys: character %d is %d, want %d\n", i, c,
			    want[i] & 0xff);
			return ("wrong cooked character");
		}
	}
	if (kbd_getc() != -1)
		return ("extra cooked character");
	return (NULL);
}

static char *
events(ie, code, qual, n)
register struct InputEvent *ie;
UBYTE *code;
UWORD *qual;
int n;
{
	register int i;

	for (i = 0; i < n; i++, ie++) {
		if (ie->ie_Class != IECLASS_RAWKEY || ie->ie_Code != code[i] ||
		    ie->ie_Qualifier != qual[i]) {
			xKPrintf("kbdkeys: event %d class %d code %x qual %x\n",
			    i, ie->ie_Class, ie->ie_Code, ie->ie_Qualifier);
			return ("wrong event");
		}
	}
	return (NULL);
}

static char *
run(r)
register struct IOStdReq *r;
{
	register int got, i;
	register long n;
	ULONG t0, p0;
	char *why;
	char buf[8];

	/* A read waiting when the first batch arrives. */
	got = 0;
	t0 = TickCount;
	xKPrintf("kbdkeys: ready\n");
	while (got < NEV1) {
		kbdsend(r, KBD_READEVENT, &ev[got],
		    (ULONG)((NEV1 - got) * sizeof(struct InputEvent)));
		while (CheckIO(r) == NULL)
			if (TickCount - t0 > 1500) {
				AbortIO(r);
				WaitIO(r);
				xKPrintf("kbdkeys: %d events\n", got);
				return ("first batch did not arrive");
			}
		WaitIO(r);
		if (r->io_Error != 0 || r->io_Actual == 0 ||
		    r->io_Actual % sizeof(struct InputEvent) != 0)
			return ("KBD_READEVENT result");
		got += r->io_Actual / sizeof(struct InputEvent);
	}
	xKPrintf("kbdkeys: batch 1 at tick %lu, %lu by interrupt\n",
	    TickCount, KbdIntrs);
	if ((why = events(ev, code1, qual1, NEV1)) != NULL)
		return (why);
	if (KbdIntrs == 0)
		return ("no byte came by interrupt");
	if (kbdcmd(r, KBD_READMATRIX, mx, (ULONG)sizeof mx) != 0)
		return ("KBD_READMATRIX failed");
	for (i = 0; i < KBD_MATRIXSIZE; i++)
		if (mx[i] != 0)
			return ("a key is still down in the matrix");
	if ((why = getn(cook1, sizeof cook1)) != NULL)
		return (why);

	/* The second batch by polling, with VI off. */
	p0 = KbdPolled;
	got = 0;
	Disable();
	for (n = 0; n < POLLS && got < sizeof cook2; n++)
		if ((i = kbd_getc()) != -1)
			buf[got++] = i;
	Enable();
	xKPrintf("kbdkeys: batch 2 at tick %lu, %lu polled\n", TickCount,
	    KbdPolled - p0);
	if (got != sizeof cook2)
		return ("second batch did not arrive");
	for (i = 0; i < got; i++)
		if (buf[i] != cook2[i])
			return ("wrong polled character");
	if (KbdPolled - p0 != NEV2)
		return ("the second batch was not all polled");
	if (kbdcmd(r, KBD_READEVENT, ev,
	    (ULONG)(NEV1 * sizeof(struct InputEvent))) != 0 ||
	    r->io_Actual != NEV2 * sizeof(struct InputEvent))
		return ("second batch events");
	if ((why = events(ev, code2, qual2, NEV2)) != NULL)
		return (why);
	if (IntStray(INTV_KBD) != 0)
		return ("stray keyboard interrupts");
	if (KbdLost != 0)
		return ("bytes lost");
	return (NULL);
}

int
main()
{
	struct IOStdReq *r;
	char *why;
	int err;

	if ((r = kbdopen(0L, &err)) == NULL) {
		xKPrintf("FAIL kbdkeys: OpenDevice unit 0: %d\n", err);
		return (1);
	}
	why = run(r);
	kbdclose(r);
	if (why != NULL) {
		xKPrintf("FAIL kbdkeys: %s\n", why);
		return (1);
	}
	xKPrintf("PASS kbdkeys\n");
	return (0);
}
