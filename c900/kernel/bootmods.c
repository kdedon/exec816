/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Boot modules: a read-only reader for the COHERENT V7 filesystem in
 * kboot's partition slot BOOTSLOT, and the loader for the display card's
 * module, the modules its `Modules' file names and the root program its `Startup' file names
 * (docs/platform.md).  Only the root directory is
 * searched.  On disk, shorts are little endian and longs PDP order (high
 * word first); a block address in the inode is three bytes, high, low,
 * middle.  Inode n is in block 2 + (n - 1) / 8; the root is inode 2.
 */
#include "kernel.h"
#include <devices/wd.h>

#define BLK	512

static ULONG
pdp(p)
register UBYTE *p;
{
	ULONG v;
	register UBYTE *q;

	q = (UBYTE *)&v;
	q[0] = p[1];
	q[1] = p[0];
	q[2] = p[3];
	q[3] = p[2];
	return (v);
}

/* Filesystem block b in buffer i. */
static UBYTE *
kbfblk(bf, b, i)
register struct BootFile *bf;
ULONG b;
int i;
{
	register ULONG *cb;
	register UBYTE *p;

	cb = &bf->bf_Blk[i];
	p = bf->bf_Buf[i];
	if (*cb != b) {
		*cb = b;
		bf->bf_Req.io_Data = p;
		bf->bf_Req.io_Offset = b;
		if (xDoIO((struct IORequest *)bf) != 0)
			bf->bf_Err = 1;
	}
	return (p);
}

/* InternalLoadSeg's read function: up to len bytes, or -1 on an error. */
LONG
kbfread(bf, buf, len)
register struct BootFile *bf;
UBYTE *buf;
ULONG len;
{
	register UWORD k, o;
	ULONG done, pos, n, b;

	pos = bf->bf_Pos;
	if (len > bf->bf_Size - pos)
		len = bf->bf_Size - pos;
	for (done = len; len != 0; len -= k) {
		/* The filesystem block holding the file's block n. */
		if ((n = pos >> 9) < 10)
			n = bf->bf_Addr[(UWORD)n];
		else {
			b = bf->bf_Addr[10];
			if ((n -= 10) >= 128) {
				n -= 128;
				b = pdp(kbfblk(bf, bf->bf_Addr[11], 1) +
				    ((UWORD)(n >> 7) << 2));
			}
			n = pdp(kbfblk(bf, b, 1) + (((UWORD)n & 127) << 2));
		}
		o = (UWORD)pos & (BLK - 1);
		k = BLK - o;
		if (k > len)
			k = len;
		xCopyMem(kbfblk(bf, n, 0) + o, buf, (ULONG)k);
		buf += k;
		pos += k;
	}
	bf->bf_Pos = pos;
	return (bf->bf_Err ? -1L : done);
}

/* Inode ino becomes the open file. */
static void
kbfino(bf, ino)
register struct BootFile *bf;
UWORD ino;
{
	register UBYTE *p, *q;
	register int i;

	--ino;
	p = kbfblk(bf, (ULONG)(2 + (ino >> 3)), 0) + ((ino & 7) << 6);
	bf->bf_Size = pdp(p + 8);
	q = (UBYTE *)bf->bf_Addr;
	for (i = 0, p += 12; i < 13; i++, p += 3, q += 4) {
		q[0] = 0;
		q[1] = p[0];
		q[2] = p[2];
		q[3] = p[1];
	}
	bf->bf_Pos = 0;
}

/* Open name in the root directory: 0, or -1 when it is not there. */
int
kbfopen(bf, name)
register struct BootFile *bf;
register char *name;
{
	UBYTE d[16];
	register UBYTE *a;
	register char *b;

	kbfino(bf, 2);
	while (kbfread(bf, d, 16L) == 16) {
		if ((d[0] | d[1]) == 0)
			continue;
		for (a = d + 2, b = name; *a == *b && *b != 0; a++, b++)
			if (a == d + 15)
				goto found;
		if (*a == *b) {
found:
			kbfino(bf, d[1] << 8 | d[0]);
			return (0);
		}
	}
	return (-1);
}

/* The boot volume's reader, or NULL without one. */
struct BootFile *
kbfinit()
{
	register struct BootFile *bf;
	struct MsgPort *mp;

	bf = (struct BootFile *)xAllocMem((ULONG)sizeof(*bf),
	    MEMF_PUBLIC | MEMF_CLEAR);
	if (bf == NULL)
		return (NULL);
	if ((mp = xCreateMsgPort()) != NULL) {
		bf->bf_Req.io_Message.mn_ReplyPort = mp;
		bf->bf_Req.io_Message.mn_Length = sizeof(bf->bf_Req);
		if (xOpenDevice(WDNAME, WD_SLOT(0, BOOTSLOT),
		    (struct IORequest *)bf, 0L) == 0) {
			bf->bf_Req.io_Command = CMD_READ;
			bf->bf_Req.io_Length = BLK;
			return (bf);
		}
		xDeleteMsgPort(mp);
	}
	xFreeMem(bf, (ULONG)sizeof(*bf));
	return (NULL);
}

void
kbfdone(bf)
register struct BootFile *bf;
{
	xCloseDevice((struct IORequest *)bf);
	xDeleteMsgPort(bf->bf_Req.io_Message.mn_ReplyPort);
	xFreeMem(bf, (ULONG)sizeof(*bf));
}

/* The reader's funcarray for InternalLoadSeg. */
APTR BootFuncs[] = { (APTR)kbfread, (APTR)xAllocMem, (APTR)xFreeMem };

/* The root program the first line of the Startup file names, loaded:
 * its seglist, or NULL. */
static APTR
kroot(bf)
register struct BootFile *bf;
{
	register char *p;
	APTR seg;
	LONG n;

	p = (char *)bf->bf_List;
	if (kbfopen(bf, "Startup") != 0) {
		xKPutStr("exec900: no Startup\n");
		return (NULL);
	}
	n = kbfread(bf, p, 15L);
	p[n < 0 ? 0 : (int)n] = '\0';
	for (; *p != '\0' && *p != '\n'; p++)
		;
	*p = '\0';
	p = (char *)bf->bf_List;
	seg = NULL;
	if (kbfopen(bf, p) == 0)
		seg = xInLoadSeg(bf, NULL, BootFuncs, NULL);
	if (seg == NULL)
		xKPrintf("exec900: root %s not loaded\n", p);
	return (seg);
}

/* Load the boot volume's file name and initialise every romtag in it.
 * A file without one is unloaded. */
static void
kbootload(bf, name)
register struct BootFile *bf;
char *name;
{
	register UWORD *w;
	UWORD *e;
	APTR seg, s;
	int found;

	seg = NULL;
	if (kbfopen(bf, name) == 0)
		seg = xInLoadSeg(bf, NULL, BootFuncs, NULL);
	found = 0;
	for (s = seg; s != NULL; s = *(APTR *)s) {
		w = (UWORD *)s + 2;
		e = (UWORD *)((char *)s + (UWORD)((ULONG *)s)[-1] - 10);
		for (; w <= e; w++)
			if (*w == RTC_MATCHWORD && *(UWORD **)(w + 1) == w) {
				xInitResident(w, seg);
				found++;
			}
	}
	if (!found) {
		xInUnLoadSeg(seg, (APTR)xFreeMem);
		xKPrintf("exec900: module %s not loaded\n", name);
	}
}

/* Load the display module for the console's card and the card's other
 * module (HR: gameport.device), then each module the
 * Modules file (its first 511 bytes) lists, one name per line, skipping
 * blank lines and lines starting with #.  Then load the root program:
 * its seglist, or NULL. */
APTR
kbootmods()
{
	register struct BootFile *bf;
	register char *p;
	char *name;
	APTR seg;

	if ((bf = kbfinit()) == NULL) {
		xKPutStr("exec900: no boot volume\n");
		return (NULL);
	}
	if ((name = kvidmod(0)) != NULL)
		kbootload(bf, name);
	if ((name = kvidmod(1)) != NULL)
		kbootload(bf, name);
	if (kbfopen(bf, "Modules") == 0) {
		kbfread(bf, bf->bf_List, (ULONG)sizeof(bf->bf_List) - 1);
		for (p = (char *)bf->bf_List; *p != '\0'; ) {
			for (name = p; *p != '\0' && *p != '\n'; p++)
				;
			if (*p != '\0')
				*p++ = '\0';
			if (*name != '\0' && *name != '#')
				kbootload(bf, name);
		}
	}
	seg = kroot(bf);
	kbfdone(bf);
	return (seg);
}
