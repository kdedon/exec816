/* SPDX-License-Identifier: MIT */
/*
 * wd.device floppy drive 2 with no medium: the unit opens, the change
 * state reports no medium, and a read or write fails with WDERR_ABSENT
 * without transferring anything.
 */
#include "test.h"
#include "wdtest.h"

static char *
run()
{
	struct IOStdReq *r;
	UBYTE *buf;
	int err;

	if ((buf = dmabuf(1, 0)) == NULL)
		return ("no buffer");
	if ((r = wdopen(WD_RAW(2))) == NULL)
		return ("open floppy");
	r->io_Command = WD_CHANGESTATE;
	if (DoIO(r) != 0 || r->io_Actual != 1)
		return ("change state without a medium");
	if ((err = wdio(r, CMD_READ, 0L, 1, buf)) != WDERR_ABSENT ||
	    r->io_Actual != 0) {
		xKPrintf("read error %d\n", err);
		return ("read");
	}
	if (wdio(r, CMD_WRITE, 0L, 1, buf) != WDERR_ABSENT)
		return ("write");
	wdclose(r);
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL)
		xKPrintf("FAIL nofloppy: %s\n", why);
	else
		KPutStr("PASS nofloppy\n");
	return (0);
}
