/* SPDX-License-Identifier: MIT */
/*
 * wd.device writes: a multi-sector write to the scratch partition reads
 * back, so does its last sector, a write past it is refused, and the
 * pattern partition, its neighbour, and the exec partition are unchanged.
 */
#include "test.h"
#include "wdtest.h"

#define N	12

static char *
run()
{
	struct IOStdReq *r, *pat, *raw;
	UBYTE *buf, *back, *ex0, *ex1;
	UWORD seed;
	int i;

	buf = dmabuf(N, 0);
	back = dmabuf(N, 0);
	ex0 = dmabuf(4, 0);
	ex1 = dmabuf(4, 0);
	if (buf == NULL || back == NULL || ex0 == NULL || ex1 == NULL)
		return ("no buffers");
	if ((r = wdopen(WD_SLOT(0, 1))) == NULL ||
	    (pat = wdopen(WD_SLOT(0, 0))) == NULL ||
	    (raw = wdopen(WD_RAW(0))) == NULL)
		return ("open");
	if (wdio(raw, CMD_READ, EXEC_BASE, 4, ex0) != 0)
		return ("read exec partition");

	/* A seed that differs between runs on the same image. */
	seed = (UWORD)TickCount * 13 + 1;
	seedfill(buf, seed, N);
	if (wdio(r, CMD_WRITE, 3L, N, buf) != 0 || r->io_Actual != N * WD_SECTOR)
		return ("write");
	r->io_Command = CMD_UPDATE;
	if (DoIO(r) != 0)
		return ("CMD_UPDATE");
	for (i = 0; i < N * WD_SECTOR; i++)
		back[i] = 0;
	if (wdio(r, CMD_READ, 3L, N, back) != 0 || !seedcheck(back, seed, N))
		return ("read back");
	if (wdio(raw, CMD_READ, SCR_BASE + 3, N, back) != 0 ||
	    !seedcheck(back, seed, N))
		return ("raw read back");

	seedfill(buf, seed + 1, 1);
	if (wdio(r, CMD_WRITE, SCR_BLKS - 1, 1, buf) != 0)
		return ("write last sector");
	if (wdio(r, CMD_READ, SCR_BLKS - 1, 1, back) != 0 ||
	    !seedcheck(back, seed + 1, 1))
		return ("last sector read back");
	if (wdio(r, CMD_WRITE, SCR_BLKS, 1, buf) != IOERR_BADADDRESS ||
	    wdio(r, CMD_WRITE, SCR_BLKS - 1, 2, buf) != IOERR_BADADDRESS)
		return ("write past the partition");

	if (wdio(pat, CMD_READ, 0L, N, back) != 0 || patcheck(back, 0L, N) >= 0)
		return ("pattern partition changed");
	if (wdio(pat, CMD_READ, PAT_BLKS - 4, 4, back) != 0 ||
	    patcheck(back, PAT_BLKS - 4, 4) >= 0)
		return ("pattern partition end changed");
	if (wdio(raw, CMD_READ, EXEC_BASE, 4, ex1) != 0)
		return ("reread exec partition");
	for (i = 0; i < 4 * WD_SECTOR; i++)
		if (ex0[i] != ex1[i])
			return ("exec partition changed");
	wdclose(r);
	wdclose(pat);
	wdclose(raw);
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL)
		xKPrintf("FAIL diskwrite: %s\n", why);
	else
		KPutStr("PASS diskwrite\n");
	return (0);
}
