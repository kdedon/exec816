/* SPDX-License-Identifier: MIT */
/*
 * keyboard.device with no key pressed: the device opens on unit 0 only,
 * the matrix is clear, a read waits and AbortIO ends it, kbd_getc returns
 * -1 at once, unsupported commands are refused, and no keyboard interrupt
 * goes unclaimed.
 */
#include "test.h"
#include "kbdtest.h"

static UBYTE mx[KBD_MATRIXSIZE + 2];
static struct InputEvent ev[2];

static char *
run(r)
register struct IOStdReq *r;
{
	register int i;

	if (sizeof(struct InputEvent) != 22)
		return ("struct InputEvent is not 22 bytes");
	for (i = 0; i < sizeof mx; i++)
		mx[i] = 0xa5;
	if (kbdcmd(r, KBD_READMATRIX, mx, (ULONG)sizeof mx) != 0 ||
	    r->io_Actual != KBD_MATRIXSIZE)
		return ("KBD_READMATRIX did not return 16 bytes");
	for (i = 0; i < KBD_MATRIXSIZE; i++)
		if (mx[i] != 0)
			return ("a key is down in the matrix");
	if (mx[KBD_MATRIXSIZE] != 0xa5)
		return ("KBD_READMATRIX wrote past the matrix");
	if (kbdcmd(r, KBD_READEVENT, ev, 10L) != IOERR_BADLENGTH)
		return ("a short KBD_READEVENT was not IOERR_BADLENGTH");
	if (kbdcmd(r, KBD_ADDRESETHANDLER, NULL, 0L) != IOERR_NOCMD)
		return ("KBD_ADDRESETHANDLER was accepted");
	if (kbdcmd(r, CMD_CLEAR, NULL, 0L) != 0)
		return ("CMD_CLEAR failed");

	kbdsend(r, KBD_READEVENT, ev, (ULONG)sizeof ev);
	Sleep(20);
	if (CheckIO(r) != NULL)
		return ("a read completed with no key");
	for (i = 0; i < 100; i++)
		if (kbd_getc() != -1)
			return ("kbd_getc returned a character");
	AbortIO(r);
	if (WaitIO(r) != IOERR_ABORTED || r->io_Actual != 0)
		return ("AbortIO did not end the read with IOERR_ABORTED");
	if (IntStray(INTV_KBD) != 0)
		return ("stray keyboard interrupts");
	if (KbdIntrs != 0 || KbdPolled != 0 || KbdLost != 0)
		return ("keyboard bytes counted with no key");
	return (NULL);
}

int
main()
{
	struct IOStdReq *r, *r1;
	char *why;
	int err;

	if ((r1 = kbdopen(1L, &err)) != NULL || err != IOERR_OPENFAIL) {
		xKPrintf("FAIL kbdnone: unit 1 opened (%d)\n", err);
		return (1);
	}
	if ((r = kbdopen(0L, &err)) == NULL) {
		xKPrintf("FAIL kbdnone: OpenDevice unit 0: %d\n", err);
		return (1);
	}
	why = run(r);
	kbdclose(r);
	if (why != NULL) {
		xKPrintf("FAIL kbdnone: %s\n", why);
		return (1);
	}
	xKPrintf("PASS kbdnone\n");
	return (0);
}
