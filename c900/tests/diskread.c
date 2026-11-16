/* SPDX-License-Identifier: MIT */
/*
 * wd.device reads: geometry of a partition and the raw drive, single and
 * multi-sector DMA reads of the pattern partition, the same blocks through
 * the raw unit, the refused requests (extent, length, raw write, unknown
 * command, empty slot, floppy partition), and kboot's latched completion
 * dismissed as stray.
 */
#include "test.h"
#include "wdtest.h"

static char *
run()
{
	struct IOStdReq *r, *raw;
	struct WDGeometry g;
	UBYTE *buf;
	long bad;
	int err;

	if (WdStray == 0)
		return ("kboot's latched completion was not dismissed");
	if ((buf = dmabuf(8, 0)) == NULL)
		return ("no buffer");
	if ((r = wdopen(WD_SLOT(0, 0))) == NULL)
		return ("open pattern slot");
	if ((raw = wdopen(WD_RAW(0))) == NULL)
		return ("open raw drive");

	r->io_Command = WD_GETGEOMETRY;
	r->io_Data = (UBYTE *)&g;
	r->io_Length = sizeof(g);
	if (DoIO(r) != 0 || r->io_Actual != sizeof(g))
		return ("geometry");
	if (g.wg_Bytes != WD_SECTOR || g.wg_Sectors != PAT_BLKS ||
	    g.wg_Flags != WDGF_DURABLE || g.wg_Generation == 0)
		return ("partition geometry fields");
	raw->io_Command = WD_GETGEOMETRY;
	raw->io_Data = (UBYTE *)&g;
	raw->io_Length = sizeof(g);
	if (DoIO(raw) != 0 || g.wg_Sectors != DISK_BLKS ||
	    g.wg_Flags != (WDGF_READONLY | WDGF_DURABLE))
		return ("raw geometry");

	if ((err = wdio(r, CMD_READ, 0L, 1, buf)) != 0) {
		xKPrintf("read error %d\n", err);
		return ("read block 0");
	}
	if (r->io_Actual != WD_SECTOR || patcheck(buf, 0L, 1) >= 0)
		return ("block 0 contents");
	if (wdio(r, CMD_READ, 5L, 8, buf) != 0 || r->io_Actual != 8 * WD_SECTOR)
		return ("read blocks 5-12");
	if ((bad = patcheck(buf, 5L, 8)) >= 0) {
		xKPrintf("byte %ld wrong\n", bad);
		return ("blocks 5-12 contents");
	}
	if (wdio(r, CMD_READ, PAT_BLKS - 1, 1, buf) != 0 ||
	    patcheck(buf, PAT_BLKS - 1, 1) >= 0)
		return ("last block");
	if (wdio(raw, CMD_READ, PAT_BASE + 3, 2, buf) != 0 ||
	    patcheck(buf, 3L, 2) >= 0)
		return ("raw read of the partition");

	if (wdio(r, CMD_READ, PAT_BLKS, 1, buf) != IOERR_BADADDRESS)
		return ("read past the partition");
	if (wdio(r, CMD_READ, PAT_BLKS - 1, 2, buf) != IOERR_BADADDRESS)
		return ("read across the partition end");
	wdset(r, CMD_READ, 0L, 1, buf);
	r->io_Length = 100;
	if (DoIO(r) != IOERR_BADLENGTH)
		return ("odd length");
	if (wdio(r, CMD_READ, 0L, 0, buf) != IOERR_BADLENGTH)
		return ("zero length");
	if (wdio(raw, CMD_WRITE, SCR_BASE, 1, buf) != WDERR_PROTECTED)
		return ("raw write accepted");
	if (wdio(r, 99, 0L, 1, buf) != IOERR_NOCMD)
		return ("unknown command");
	r->io_Command = CMD_UPDATE;
	if (DoIO(r) != 0)
		return ("CMD_UPDATE");
	if (wdopen(WD_SLOT(0, 2)) != NULL)
		return ("empty slot opened");
	if (wdopen(WD_UNIT(2, 1)) != NULL)
		return ("floppy partition opened");
	if (wdopen(WD_UNIT(4, 0)) != NULL)
		return ("drive 4 opened");
	xKPrintf("wd stray completions %lu\n", WdStray);
	wdclose(r);
	wdclose(raw);
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL)
		xKPrintf("FAIL diskread: %s\n", why);
	else
		KPutStr("PASS diskread\n");
	return (0);
}
