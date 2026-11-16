/* SPDX-License-Identifier: MIT */
/*
 * wd.device bounce path: reads into, and writes from, buffers at odd,
 * even-unaligned and stack and data-hunk addresses go one sector at a
 * time through the bounce buffer and match the aligned results; the bytes
 * around the buffer are untouched.  The emulator's DMA does not wrap, so
 * the bounce buffer's contents show which path a read took.  The
 * interrupt stack depth of the completions is measured.
 */
#include "test.h"
#include "wdtest.h"

#define N	3
#define BOUNCE	((UBYTE *)MKPTR(SEGBASE(CMDSEG), CMD_BUF))

static UBYTE dbuf[N * WD_SECTOR + 2];	/* the program's data hunk */

#define INTSTK		512		/* kernel/interrupts.c INTSTKSIZE */
#define PAINT		0x6b

/* Paint the interrupt stack below its top, or measure the painted bytes
 * since overwritten. */
static int
intstack(paint)
int paint;
{
	register UBYTE *p;
	register int i;

	p = (UBYTE *)MKPTR(SEGBASE(STACKSEG), kintstk - INTSTK);
	if (paint) {
		Disable();
		for (i = 0; i < INTSTK - 64; i++)
			p[i] = PAINT;
		Enable();
		return (0);
	}
	for (i = 0; i < INTSTK - 64 && p[i] == PAINT; i++)
		;
	return (INTSTK - i);
}

static char *
check(r, buf, what)
struct IOStdReq *r;
UBYTE *buf;
char *what;
{
	buf[-1] = 0xa5;
	buf[N * WD_SECTOR] = 0x5a;
	if (wdio(r, CMD_READ, 7L, N, buf) != 0 || r->io_Actual != N * WD_SECTOR)
		return (what);
	if (patcheck(buf, 7L, N) >= 0)
		return (what);
	if (buf[-1] != 0xa5 || buf[N * WD_SECTOR] != 0x5a)
		return (what);
	/* The last sector went through the bounce buffer. */
	if (patcheck(BOUNCE, 7L + N - 1, 1) >= 0)
		return (what);
	return (NULL);
}

static char *
run()
{
	struct IOStdReq *r, *s;
	UBYTE sbuf[N * WD_SECTOR + 4];
	UBYTE *odd, *even, *back;
	UWORD seed;
	char *why;
	int i;

	odd = dmabuf(N + 1, 1);
	even = dmabuf(N + 1, 2);
	back = dmabuf(N, 0);
	if (odd == NULL || even == NULL || back == NULL)
		return ("no buffers");
	if ((r = wdopen(WD_SLOT(0, 0))) == NULL ||
	    (s = wdopen(WD_SLOT(0, 1))) == NULL)
		return ("open");
	xKPrintf("odd %lx even %lx stack %lx data %lx\n", kphys(odd),
	    kphys(even), kphys(sbuf + 1), kphys(dbuf + 1));
	intstack(1);
	if ((why = check(r, odd, "odd read")) != NULL ||
	    (why = check(r, even, "even unaligned read")) != NULL ||
	    (why = check(r, sbuf + 1, "stack read")) != NULL ||
	    (why = check(r, dbuf + 1, "data read")) != NULL)
		return (why);
	for (i = 0; i < WD_SECTOR; i++)
		BOUNCE[i] = 0;
	if (wdio(r, CMD_READ, 7L, N, back) != 0 || patcheck(back, 7L, N) >= 0)
		return ("aligned read");
	for (i = 0; i < WD_SECTOR; i++)
		if (BOUNCE[i] != 0)
			return ("aligned read used the bounce buffer");

	seed = (UWORD)TickCount * 5 + 3;
	seedfill(odd, seed, N);
	if (wdio(s, CMD_WRITE, 20L, N, odd) != 0)
		return ("odd write");
	if (wdio(s, CMD_READ, 20L, N, back) != 0 || !seedcheck(back, seed, N))
		return ("odd write read back");
	seedfill(back, seed + 9, N);
	if (wdio(s, CMD_WRITE, 30L, N, back) != 0)
		return ("aligned write");
	if (wdio(s, CMD_READ, 30L, N, even) != 0 || !seedcheck(even, seed + 9, N))
		return ("even unaligned read back");
	i = intstack(0);
	xKPrintf("interrupt stack used %d bytes\n", i);
	if (i > INTSTK - 64)
		return ("interrupt stack nearly full");
	wdclose(r);
	wdclose(s);
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL)
		xKPrintf("FAIL diskalign: %s\n", why);
	else
		KPutStr("PASS diskalign\n");
	return (0);
}
