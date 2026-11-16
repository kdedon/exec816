/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Amiga hunk loading: dos.library's InternalLoadSeg and InternalUnLoadSeg,
 * here in exec.library because exec loads dos.library itself.  The caller
 * supplies the file I/O as Amiga's funcarray: read(fh, buf, len), which
 * returns the bytes read, alloc(size, flags) and free(mem, size).
 *
 * A segment is one allocation: its size, the link, then the hunk.  The
 * seglist points at the first link; a link holds the next segment's link
 * address or NULL, as Amiga's BPTR chain but with native pointers.  A
 * relocation replaces bits 30-16 of the long with the target hunk's
 * segment and adds its base offset to the low word without carry
 * (docs/platform.md).  Relocation offsets are read RBUF longs at a time
 * into a buffer after the hunk table.
 */
#include "kernel.h"
#include <dos/doshunks.h>

#define MAXHUNK		0xFFF0L		/* hunk plus 8 bytes below 64 KB */
#define HSIZE(p)	(*(ULONG *)((char *)(p) - 8) - 8)
#define RBUF		128

struct lsctx {
	APTR c_fh;
	LONG (*c_rd)();
	int c_bad;
};

static void
lsread(c, p, n)
register struct lsctx *c;
APTR p;
ULONG n;
{
	if ((*c->c_rd)(c->c_fh, p, n) != (LONG)n)
		c->c_bad = 1;
}

/* The next long, or 0 once the file is short. */
static ULONG
lsget(c)
struct lsctx *c;
{
	ULONG v;

	v = 0;
	lsread(c, &v, 4L);
	return (v);
}

APTR
xInLoadSeg(fh, table, fn, stack)
APTR fh, table;
LONG *fn, *stack;
{
	struct lsctx c;
	register struct lsctx *cp;
	register char *p;
	register UWORD *w;
	register ULONG *r;
	ULONG *h;
	ULONG k, o, sz, hs;
	UWORD n, i, m, ts, to;
	APTR seg, *link;
	APTR (*al)();

	cp = &c;
	cp->c_fh = fh;
	cp->c_rd = (LONG (*)())fn[0];
	cp->c_bad = 0;
	al = (APTR (*)())fn[1];
	seg = NULL;
	if (lsget(cp) != HUNK_HEADER || lsget(cp) != 0)
		return (NULL);
	n = (UWORD)lsget(cp);
	lsget(cp);
	lsget(cp);
	hs = ((ULONG)n + RBUF) << 2;
	if ((h = (ULONG *)xAllocMem(hs, (ULONG)MEMF_PUBLIC)) == NULL)
		return (NULL);
	link = &seg;
	for (i = 0; i < n; i++) {
		o = lsget(cp);
		k = o << 2;
		if (k > MAXHUNK)
			goto fail;
		p = (char *)(*al)(k + 8, MEMF_PUBLIC | MEMF_CLEAR | (o >> 29 & 6));
		if (p == NULL)
			goto fail;
		*(ULONG *)p = k + 8;
		p += 4;
		*link = (APTR)p;
		link = (APTR *)p;
		h[i] = (ULONG)(p + 4);
	}
	for (i = 0; i < n && !cp->c_bad; ) {
		p = (char *)h[i];
		sz = HSIZE(p);
		switch ((int)lsget(cp)) {
		case HUNK_CODE:
		case HUNK_DATA:
			if ((k = lsget(cp) << 2) > sz)
				goto fail;
			lsread(cp, p, k);
			break;
		case HUNK_BSS:
			lsget(cp);
			break;
		case HUNK_RELOC32:
			while ((k = lsget(cp)) != 0) {
				if ((o = lsget(cp)) >= n)
					goto fail;
				w = (UWORD *)&h[(UWORD)o];
				ts = w[0];
				to = w[1];
				while (k != 0) {
					m = k < RBUF ? (UWORD)k : RBUF;
					k -= m;
					lsread(cp, r = &h[n], (ULONG)m << 2);
					for (; m != 0; m--) {
						o = *r++;
						if ((o & 1) || o + 4 > sz)
							goto fail;
						w = (UWORD *)(p + (UWORD)o);
						w[0] = (w[0] & 0x8000) | ts;
						w[1] += to;
					}
				}
			}
			break;
		case HUNK_END:
			i++;
			break;
		default:
			goto fail;
		}
	}
	if (cp->c_bad) {
fail:
		xInUnLoadSeg(seg, (APTR)fn[2]);
		seg = NULL;
	}
	xFreeMem(h, hs);
	return (seg);
}

LONG
xInUnLoadSeg(seg, fr)
APTR seg;
void (*fr)();
{
	register ULONG *p;

	while ((p = (ULONG *)seg) != NULL) {
		seg = (APTR)*p;
		(*fr)(p - 1, p[-1]);
	}
	return (-1L);
}
