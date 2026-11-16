/* SPDX-License-Identifier: MIT */
/* FLOPPY: pattern */
/*
 * wd.device floppy drive 2 with a pattern image attached: geometry,
 * medium ready, reads at both ends, a write read back, the end of the
 * medium refused, and no media change counted.
 */
#include "test.h"
#include "wdtest.h"

static char *
run()
{
	struct IOStdReq *r;
	struct WDGeometry g;
	UBYTE *buf, *odd;
	UWORD seed;

	buf = dmabuf(4, 0);
	odd = dmabuf(2, 1);
	if (buf == NULL || odd == NULL)
		return ("no buffers");
	if ((r = wdopen(WD_RAW(2))) == NULL)
		return ("open floppy");
	r->io_Command = WD_GETGEOMETRY;
	r->io_Data = (UBYTE *)&g;
	r->io_Length = sizeof(g);
	if (DoIO(r) != 0 || g.wg_Sectors != WD_FDBLOCKS ||
	    g.wg_Flags != (WDGF_DURABLE | WDGF_REMOVABLE) || g.wg_Generation != 1)
		return ("geometry");
	r->io_Command = WD_CHANGESTATE;
	r->io_Actual = 0xdeadL;
	if (DoIO(r) != 0 || r->io_Actual != 0)
		return ("change state with a medium");
	if (wdio(r, CMD_READ, 0L, 4, buf) != 0 || patcheck(buf, 0L, 4) >= 0)
		return ("read blocks 0-3");
	if (wdio(r, CMD_READ, WD_FDBLOCKS - 2, 2, odd) != 0 ||
	    patcheck(odd, WD_FDBLOCKS - 2, 2) >= 0)
		return ("read the last blocks");
	if (wdio(r, CMD_READ, WD_FDBLOCKS, 1, buf) != IOERR_BADADDRESS)
		return ("read past the medium");
	seed = (UWORD)TickCount;
	seedfill(buf, seed, 2);
	if (wdio(r, CMD_WRITE, 100L, 2, buf) != 0)
		return ("write");
	seedfill(buf, 0, 2);
	if (wdio(r, CMD_READ, 100L, 2, buf) != 0 || !seedcheck(buf, seed, 2))
		return ("read back");
	r->io_Command = WD_CHANGENUM;
	if (DoIO(r) != 0 || r->io_Actual != 0)
		return ("change count");
	wdclose(r);
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL)
		xKPrintf("FAIL floppy: %s\n", why);
	else
		KPutStr("PASS floppy\n");
	return (0);
}
