/* SPDX-License-Identifier: MIT */
/*
 * wd.device timeout: with the completion interrupt swallowed by a
 * replacement handler, the issued request fails with WDERR_TIMEOUT after
 * its deadline, the request queued behind it with WDERR_OFFLINE, and the
 * controller stays offline once the driver's handler is back.
 */
#include "test.h"
#include "wdtest.h"

static int eaten;

static int
eat(data, num)
APTR data;
UWORD num;
{
	eaten++;
	outw(0x0500, 0);
	return (0);
}

static struct Interrupt eatint;

static char *
run()
{
	struct IOStdReq *a, *b;
	struct Interrupt *old;
	UBYTE *buf;
	ULONG t0, dt;

	if ((buf = dmabuf(2, 0)) == NULL)
		return ("no buffer");
	if ((a = wdopen(WD_SLOT(0, 0))) == NULL ||
	    (b = wdopen(WD_SLOT(0, 0))) == NULL)
		return ("open");
	if (wdio(a, CMD_READ, 0L, 1, buf) != 0)
		return ("read before");
	eatint.is_Node.ln_Name = "eat";
	eatint.is_Code = eat;
	old = SetIntVector(INTV_DISK, &eatint);
	wdset(a, CMD_READ, 1L, 1, buf);
	wdset(b, CMD_READ, 2L, 1, buf + WD_SECTOR);
	t0 = TickCount;
	Disable();
	SendIO(a);
	SendIO(b);
	Enable();
	WaitIO(a);
	dt = TickCount - t0;
	WaitIO(b);
	SetIntVector(INTV_DISK, old);
	xKPrintf("timeout after %lu ticks, %d swallowed\n", dt, eaten);
	if (a->io_Error != WDERR_TIMEOUT || a->io_Actual != 0)
		return ("issued request did not time out");
	if (b->io_Error != WDERR_OFFLINE)
		return ("queued request not failed offline");
	if (eaten != 1 || dt < 300)
		return ("deadline");
	if (wdio(a, CMD_READ, 0L, 1, buf) != WDERR_OFFLINE)
		return ("controller not offline");
	a->io_Command = WD_GETGEOMETRY;
	a->io_Data = buf;
	a->io_Length = sizeof(struct WDGeometry);
	if (DoIO(a) != 0)
		return ("geometry while offline");
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL)
		xKPrintf("FAIL disktimeout: %s\n", why);
	else
		KPutStr("PASS disktimeout\n");
	return (0);
}
