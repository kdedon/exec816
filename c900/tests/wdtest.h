/* SPDX-License-Identifier: MIT */
/*
 * Shared code for the wd.device tests.  tools/mkdisk.py lays out the test
 * disk: bi_part[0] holds pattern blocks, bi_part[1] is scratch, and
 * bi_part[15] spans the drive.
 */
#include "test.h"
#include <devices/wd.h>

#define PAT_BASE	1160L		/* mkdisk.py */
#define PAT_BLKS	64L
#define SCR_BASE	1224L
#define SCR_BLKS	64L
#define EXEC_BASE	136L
#define DISK_BLKS	83776L

/* Byte i of pattern block blk (mkdisk.py pattern()). */
static int
patbyte(blk, i)
ULONG blk;
int i;
{
	if (i < 4)
		return ((int)(blk >> (8 * (3 - i))) & 0xff);
	return ((int)(blk * 31 + i) & 0xff);
}

/* Index of the first wrong byte of n pattern blocks from blk, or -1. */
static long
patcheck(p, blk, n)
UBYTE *p;
ULONG blk;
int n;
{
	register int i;
	int k;

	for (k = 0; k < n; k++, blk++)
		for (i = 0; i < WD_SECTOR; i++)
			if (*p++ != (UBYTE)patbyte(blk, i))
				return ((long)k * WD_SECTOR + i);
	return (-1L);
}

/* Fill n blocks with a seeded pattern; check them. */
static void
seedfill(p, seed, n)
UBYTE *p;
UWORD seed;
int n;
{
	register UWORD i;

	for (i = 0; i < (UWORD)n * WD_SECTOR; i++)
		*p++ = (UBYTE)(seed + i * 7 + (i >> 9));
}

static int
seedcheck(p, seed, n)
UBYTE *p;
UWORD seed;
int n;
{
	register UWORD i;

	for (i = 0; i < (UWORD)n * WD_SECTOR; i++)
		if (*p++ != (UBYTE)(seed + i * 7 + (i >> 9)))
			return (0);
	return (1);
}

/* A buffer of n sectors whose physical address is 512-aligned, plus
 * off bytes, carved from an allocation of n + 1 sectors and off. */
static UBYTE *
dmabuf(n, off)
int n, off;
{
	UBYTE *b;
	UWORD pad;

	b = (UBYTE *)AllocMem((ULONG)(n + 1) * WD_SECTOR + off, MEMF_PUBLIC);
	if (b == NULL)
		return (NULL);
	pad = (UWORD)(WD_SECTOR - (kphys((APTR)b) & (WD_SECTOR - 1))) &
	    (WD_SECTOR - 1);
	return (b + pad + off);
}

/* Open unit on a new port of the calling Task; NULL on failure. */
static struct IOStdReq *
wdopen(unit)
ULONG unit;
{
	struct MsgPort *port;
	struct IOStdReq *r;

	if ((port = CreateMsgPort()) == NULL)
		return (NULL);
	r = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
	if (r == NULL) {
		DeleteMsgPort(port);
		return (NULL);
	}
	if (OpenDevice(WDNAME, unit, r, 0) != 0) {
		DeleteIORequest(r);
		DeleteMsgPort(port);
		return (NULL);
	}
	return (r);
}

static void
wdclose(r)
struct IOStdReq *r;
{
	struct MsgPort *port;

	port = r->io_Message.mn_ReplyPort;
	CloseDevice(r);
	DeleteIORequest(r);
	DeleteMsgPort(port);
}

/* Set up a transfer. */
static void
wdset(r, cmd, blk, n, buf)
struct IOStdReq *r;
int cmd;
ULONG blk;
int n;
UBYTE *buf;
{
	r->io_Command = cmd;
	r->io_Offset = blk;
	r->io_Length = (ULONG)n * WD_SECTOR;
	r->io_Data = buf;
	r->io_Actual = 0xdeadL;
}

static int
wdio(r, cmd, blk, n, buf)
struct IOStdReq *r;
int cmd;
ULONG blk;
int n;
UBYTE *buf;
{
	wdset(r, cmd, blk, n, buf);
	return (DoIO(r));
}
