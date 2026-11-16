/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * wd.device: the hard disks and floppies of the C900 disk controller.
 * The request model follows upstream's ideplus.device
 * (docs/reference/ideplus-device.md): IOStdReq, a zero-based sector in
 * io_Offset, a read-only raw unit and writable partition units, the
 * 12-byte GET_GEOMETRY record.  The controller protocol is COHERENT's wd
 * driver: a 16-byte command block per controller at a fixed physical
 * address, armed by 0xFF in its status byte and started by the doorbell;
 * the completion arrives on INTV_DISK.
 *
 * Both controllers share the doorbell, the interrupt and the bounce
 * buffer, so one request is in flight at a time, from one FIFO queue.
 * The handler completes it and starts the next.  Open and Close run
 * under OpenDevice's Forbid; BeginIO and AbortIO run unlocked and change
 * queue state with VI off.
 */
#include "kernel.h"
#include <bootinfo.h>
#include <devices/wd.h>

#define WDIO		0x0500		/* doorbell: 1 starts, 0 resets IEO */

#define OP_TDR		0x00		/* test drive ready */
#define OP_READ		0x08
#define OP_WRITE	0x0A

/* Command block bytes. */
#define CB_OP		0
#define CB_LUNHI	1		/* lun << 5 | sector bits 20-16 */
#define CB_MID		2
#define CB_LOW		3
#define CB_COUNT	4
#define CB_DMAHI	6
#define CB_DMAMID	7
#define CB_DMALOW	8
#define CB_STATUS	12		/* 0xFF arms; completion overwrites */
#define CB_SIZE		16
#define CB_ARMED	0xFF

/* Completion status, low 7 bits; 0 is success. */
#define ST_NOSENSE	0x70		/* no floppy */
#define ST_MEDIA	0x73
#define ST_CHANGED	0x76
#define ST_PROTECT	0x77
#define ST_NOTREADY	0x12		/* 0x92: drive not ready */

#define MAXCHUNK	128		/* sectors per command (a byte count) */
#define MAXLBA		0x200000L	/* 21-bit sector address */
#define TIMEBASE	300		/* ticks per command ... */
#define TIMESECT	10		/* ... plus ticks per sector */

/* An open unit, shared by its opens and freed by the last close. */
struct WDUnit {
	struct MinNode wu_Node;
	UWORD wu_Unit;			/* the OpenDevice unit number */
	ULONG wu_Base;			/* first physical sector */
	ULONG wu_Count;			/* sectors */
	UWORD wu_OpenCnt;
	UBYTE wu_Drive;
	UBYTE wu_Flags;			/* WDGF_ */
};

struct WDBase {
	struct Device wb_Device;
	struct List wb_Queue;		/* waiting requests, FIFO */
	struct IOStdReq *wb_Active;	/* the one issued request */
	ULONG wb_Done;			/* its sectors already transferred */
	UWORD wb_Chunk;			/* sectors in the issued command */
	UWORD wb_Ticks;			/* ticks before the command times out */
	UBYTE wb_Bounce;		/* the command uses the bounce buffer */
	UBYTE wb_Retry;			/* a media change was absorbed */
	UBYTE wb_Offline;		/* a command timed out */
	UBYTE wb_Status;		/* last completion status */
	UWORD wb_BootInfo;		/* kboot's partition table is valid */
	UWORD wb_Changes[WD_NDRIVES];
	ULONG wb_HDCount;		/* raw hard-disk size */
	struct Interrupt wb_Int;
	struct Interrupt wb_Tick;
	struct MinList wb_Units;	/* open units */
};

static struct {
	struct JumpVec lv_Vec[6];
	struct WDBase lv_Base;
} wdbaselib;
#define wdbase wdbaselib.lv_Base
static char wdname[] = WDNAME;

/* Completions that matched no issued command (kboot leaves one). */
ULONG WdStray;

extern struct bootinfo bootinfo;

#define CMDBLK(d)	((UBYTE *)MKPTR(SEGBASE(CMDSEG), \
			    (d) >= WD_NHD ? CMD_FD : CMD_HD))
#define BOUNCE		((UBYTE *)MKPTR(SEGBASE(CMDSEG), CMD_BUF))
#define UNITOF(r)	((struct WDUnit *)(r)->io_Unit)

static void
copy(src, dst)
register UBYTE *src, *dst;
{
	register UWORD *s, *d;
	register int n;

	if (((UWORD)POFF(src) | (UWORD)POFF(dst)) & 1) {
		for (n = WD_SECTOR; n != 0; n--)
			*dst++ = *src++;
		return;
	}
	s = (UWORD *)src;
	d = (UWORD *)dst;
	for (n = WD_SECTOR / 2; n != 0; n--)
		*d++ = *s++;
}

/* The active request's buffer at sector i. */
static UBYTE *
bufat(r, i)
struct IOStdReq *r;
ULONG i;
{
	return (r->io_Data + (UWORD)((UWORD)i << 9));
}

/* Issue the next command of the active request.  VI is off. */
static void
issue()
{
	register struct WDBase *wb;
	register struct IOStdReq *r;
	register UBYTE *cb;
	register int i;
	struct WDUnit *u;
	ULONG lba, left, phys, dma;
	UWORD n;
	int op;

	wb = &wdbase;
	r = wb->wb_Active;
	u = UNITOF(r);
	cb = CMDBLK(u->wu_Drive);
	for (i = 0; i < CB_SIZE; i++)
		cb[i] = 0;
	lba = 0;
	dma = 0;
	n = 0;
	wb->wb_Bounce = 0;
	if (r->io_Command == WD_CHANGESTATE)
		op = OP_TDR;
	else {
		op = r->io_Command == CMD_READ ? OP_READ : OP_WRITE;
		left = (r->io_Length >> 9) - wb->wb_Done;
		lba = u->wu_Base + r->io_Offset + wb->wb_Done;
		phys = kphys((APTR)bufat(r, wb->wb_Done));
		if (phys != 0xffffffffL && (phys & 0x1ffL) == 0) {
			/* One command stays inside a physical 64 KB page;
			 * a stack-segment buffer can cross one. */
			n = (UWORD)((0x10000L - (phys & 0xffffL)) >> 9);
			if (left < n)
				n = (UWORD)left;
			dma = phys;
		} else {
			/* The DMA address wraps in its 512-byte window:
			 * bounce one sector at a time. */
			n = 1;
			dma = CMDPHYS + CMD_BUF;
			wb->wb_Bounce = 1;
			if (op == OP_WRITE)
				copy(bufat(r, wb->wb_Done), BOUNCE);
		}
	}
	cb[CB_LUNHI] = (UBYTE)(((lba >> 16) & 0x1f) | ((u->wu_Drive & 1) << 5));
	cb[CB_MID] = (UBYTE)(lba >> 8);
	cb[CB_LOW] = (UBYTE)lba;
	cb[CB_COUNT] = (UBYTE)n;
	cb[CB_DMAHI] = (UBYTE)(dma >> 16);
	cb[CB_DMAMID] = (UBYTE)(dma >> 8);
	cb[CB_DMALOW] = (UBYTE)dma;
	cb[CB_STATUS] = CB_ARMED;
	cb[CB_OP] = op;
	wb->wb_Chunk = n;
	wb->wb_Ticks = TIMEBASE + TIMESECT * n;
	outw(WDIO, 1);
}

/* Start the next queued request if none is active.  VI is off. */
static void
startnext()
{
	register struct WDBase *wb;

	wb = &wdbase;
	if (wb->wb_Active != NULL || wb->wb_Offline)
		return;
	wb->wb_Active = (struct IOStdReq *)xRemHead(&wb->wb_Queue);
	if (wb->wb_Active == NULL)
		return;
	wb->wb_Done = 0;
	wb->wb_Retry = 0;
	issue();
}

/* Reply to the active request and start the next.  VI is off. */
static void
finish(err)
int err;
{
	register struct IOStdReq *r;

	r = wdbase.wb_Active;
	wdbase.wb_Active = NULL;
	wdbase.wb_Ticks = 0;
	r->io_Error = err;
	xReplyMsg(&r->io_Message);
	startnext();
}

static int
maperr(st)
int st;
{
	switch (st) {
	case ST_NOSENSE:
	case ST_NOTREADY:
		return (WDERR_ABSENT);
	case ST_PROTECT:
		return (WDERR_PROTECTED);
	case ST_MEDIA:
		return (WDERR_MEDIA);
	}
	return (WDERR_DEVICE);
}

/*
 * INTV_DISK handler.  A completion is the active request's only when its
 * block is no longer armed; anything else (kboot's last polled read, a
 * completion after a timeout) is counted and dismissed.
 */
static int
wdintr(wb, num)
register struct WDBase *wb;
UWORD num;
{
	register struct IOStdReq *r;
	struct WDUnit *u;
	int st;

	r = wb->wb_Active;
	if (r == NULL ||
	    (st = CMDBLK(UNITOF(r)->wu_Drive)[CB_STATUS]) == CB_ARMED) {
		WdStray++;
		outw(WDIO, 0);
		return (0);
	}
	outw(WDIO, 0);
	u = UNITOF(r);
	wb->wb_Status = st;
	st &= 0x7f;
	if (st == ST_CHANGED && (u->wu_Flags & WDGF_REMOVABLE)) {
		/* The request is for the medium now in the drive: run it
		 * again once, from the start. */
		if (!wb->wb_Retry) {
			wb->wb_Changes[u->wu_Drive]++;
			wb->wb_Retry = 1;
			wb->wb_Done = 0;
			issue();
			return (0);
		}
		r->io_Actual = 0;
		finish(WDERR_CHANGED);
		return (0);
	}
	if (r->io_Command == WD_CHANGESTATE) {
		r->io_Actual = st != 0;
		finish(0);
		return (0);
	}
	if (st != 0) {
		r->io_Actual = 0;
		finish(maperr(st));
		return (0);
	}
	if (wb->wb_Bounce && r->io_Command == CMD_READ)
		copy(BOUNCE, bufat(r, wb->wb_Done));
	wb->wb_Done += wb->wb_Chunk;
	if (wb->wb_Done < (r->io_Length >> 9)) {
		issue();
		return (0);
	}
	r->io_Actual = r->io_Length;
	finish(0);
	return (0);
}

/* Tick server: a command without completion in time fails, and the
 * controller goes offline with every queued request. */
static int
wdtick(wb, num)
register struct WDBase *wb;
UWORD num;
{
	register struct IOStdReq *r;

	if (wb->wb_Active == NULL || wb->wb_Ticks == 0 || --wb->wb_Ticks != 0)
		return (0);
	wb->wb_Offline = 1;
	r = wb->wb_Active;
	wb->wb_Active = NULL;
	r->io_Actual = 0;
	r->io_Error = WDERR_TIMEOUT;
	xReplyMsg(&r->io_Message);
	while ((r = (struct IOStdReq *)xRemHead(&wb->wb_Queue)) != NULL) {
		r->io_Actual = 0;
		r->io_Error = WDERR_OFFLINE;
		xReplyMsg(&r->io_Message);
	}
	return (0);
}

static void
wdopen(dev, ior, unit, flags)
struct Device *dev;
struct IORequest *ior;
ULONG unit, flags;
{
	register struct WDUnit *u;
	register struct bipart *bp;
	struct WDUnit w;
	int drive, part;

	if (flags != 0 || unit >= WD_UNIT(WD_NDRIVES, 0) ||
	    ior->io_Message.mn_Length < sizeof(struct IOStdReq)) {
		ior->io_Error = IOERR_OPENFAIL;
		return;
	}
	drive = (int)(unit >> 5);
	part = (int)unit & 0x1f;
	w.wu_Base = 0;
	if (drive < WD_NHD) {
		if (part > WD_NSLOT) {
			ior->io_Error = IOERR_OPENFAIL;
			return;
		}
		if (part == 0) {
			w.wu_Count = wdbase.wb_HDCount;
			w.wu_Flags = WDGF_READONLY | WDGF_DURABLE;
		} else {
			bp = &bootinfo.bi_part[part - 1];
			if (!wdbase.wb_BootInfo || bp->bcount == 0 ||
			    bp->bstart >= MAXLBA ||
			    bp->bcount > MAXLBA - bp->bstart) {
				ior->io_Error = IOERR_OPENFAIL;
				return;
			}
			w.wu_Base = bp->bstart;
			w.wu_Count = bp->bcount;
			w.wu_Flags = WDGF_DURABLE;
		}
	} else {
		if (part != 0) {
			ior->io_Error = IOERR_OPENFAIL;
			return;
		}
		w.wu_Count = WD_FDBLOCKS;
		w.wu_Flags = WDGF_DURABLE | WDGF_REMOVABLE;
	}
	for (u = (struct WDUnit *)wdbase.wb_Units.mlh_Head;
	    u->wu_Node.mln_Succ != NULL;
	    u = (struct WDUnit *)u->wu_Node.mln_Succ)
		if (u->wu_Unit == (UWORD)unit)
			break;
	if (u->wu_Node.mln_Succ == NULL) {
		u = (struct WDUnit *)xAllocMem((ULONG)sizeof(struct WDUnit),
		    (ULONG)MEMF_PUBLIC);
		if (u == NULL) {
			ior->io_Error = IOERR_OPENFAIL;
			return;
		}
		u->wu_Unit = (UWORD)unit;
		u->wu_Drive = drive;
		u->wu_OpenCnt = 0;
		xAddTail((struct List *)&wdbase.wb_Units,
		    (struct Node *)&u->wu_Node);
	}
	u->wu_Base = w.wu_Base;
	u->wu_Count = w.wu_Count;
	u->wu_Flags = w.wu_Flags;
	u->wu_OpenCnt++;
	ior->io_Unit = (struct Unit *)u;
	dev->dd_Library.lib_OpenCnt++;
}

static APTR
wdclose(dev, ior)
struct Device *dev;
struct IORequest *ior;
{
	register struct WDUnit *u;

	u = UNITOF(ior);
	if (--u->wu_OpenCnt == 0) {
		xRemove((struct Node *)&u->wu_Node);
		xFreeMem((APTR)u, (ULONG)sizeof(struct WDUnit));
	}
	dev->dd_Library.lib_OpenCnt--;
	return (NULL);
}

/* Queue a request for the controller. */
static void
enqueue(r)
register struct IOStdReq *r;
{
	int s;

	s = intoff();
	if (wdbase.wb_Offline) {
		intrest(s);
		r->io_Error = WDERR_OFFLINE;
		xTermIO((struct IORequest *)r);
		return;
	}
	r->io_Flags &= ~IOF_QUICK;
	xAddTail(&wdbase.wb_Queue, &r->io_Message.mn_Node);
	startnext();
	intrest(s);
}

static void
wdbegin(dev, ior)
struct Device *dev;
struct IORequest *ior;
{
	register struct IOStdReq *r;
	register struct WDUnit *u;
	register struct WDGeometry *g;
	ULONG n;
	int err;

	IOBEGIN(ior);
	r = (struct IOStdReq *)ior;
	u = UNITOF(r);
	err = 0;
	r->io_Actual = 0;
	switch (r->io_Command) {
	case CMD_WRITE:
		if (u->wu_Flags & WDGF_READONLY) {
			err = WDERR_PROTECTED;
			break;
		}
		/* FALLTHROUGH */
	case CMD_READ:
		if (r->io_Length == 0 ||
		    (r->io_Length & (WD_SECTOR - 1)) != 0) {
			err = IOERR_BADLENGTH;
			break;
		}
		n = r->io_Length >> 9;
		if (r->io_Offset >= u->wu_Count ||
		    n > u->wu_Count - r->io_Offset) {
			err = IOERR_BADADDRESS;
			break;
		}
		enqueue(r);
		return;
	case WD_CHANGESTATE:
		enqueue(r);
		return;
	case CMD_UPDATE:		/* writes complete on the medium */
	case CMD_CLEAR:			/* nothing is cached */
		break;
	case WD_GETGEOMETRY:
		if (r->io_Length < sizeof(struct WDGeometry)) {
			err = IOERR_BADLENGTH;
			break;
		}
		g = (struct WDGeometry *)r->io_Data;
		g->wg_Bytes = WD_SECTOR;
		g->wg_Flags = u->wu_Flags;
		g->wg_Sectors = u->wu_Count;
		g->wg_Generation = (ULONG)wdbase.wb_Changes[u->wu_Drive] + 1;
		r->io_Actual = sizeof(struct WDGeometry);
		break;
	case WD_CHANGENUM:
		r->io_Actual = wdbase.wb_Changes[u->wu_Drive];
		break;
	default:
		err = IOERR_NOCMD;
	}
	r->io_Error = err;
	xTermIO(ior);
}

/* A queued request is unlinked and replied; the issued one completes. */
static LONG
wdabort(dev, ior)
struct Device *dev;
struct IORequest *ior;
{
	register struct Node *n;
	LONG r;
	int s;

	r = -1;
	s = intoff();
	for (n = wdbase.wb_Queue.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (n == &ior->io_Message.mn_Node) {
			xRemove(n);
			((struct IOStdReq *)ior)->io_Actual = 0;
			ior->io_Error = IOERR_ABORTED;
			xReplyMsg(&ior->io_Message);
			r = 0;
			break;
		}
	intrest(s);
	return (r);
}

static APTR wdbasefuncs[] = {
	(APTR)wdopen, (APTR)wdclose, (APTR)knull, (APTR)knull,
	(APTR)wdbegin, (APTR)wdabort, (APTR)-1L
};

void kwdinit();

struct Resident wdtag = {
	RTC_MATCHWORD, &wdtag, (APTR)(&wdtag + 1), RTF_COLDSTART, 1,
	NT_DEVICE, 0, wdname, NULL, (APTR)kwdinit
};

/* Boot: the raw hard-disk size is the highest end of kboot's slots; the
 * controller cannot report it. */
void
kwdinit()
{
	register struct WDBase *wb;
	register struct bipart *bp;
	register int i;
	ULONG end;
	UBYTE *cb;

	wb = &wdbase;
	kinitlib(&wb->wb_Device.dd_Library, wdname, wdbasefuncs);
	xNewList(&wb->wb_Queue);
	xNewList((struct List *)&wb->wb_Units);
	wb->wb_HDCount = MAXLBA;
	wb->wb_BootInfo = bivalid();
	if (wb->wb_BootInfo) {
		wb->wb_HDCount = 0;
		for (i = 0; i < BI_NPART; i++) {
			bp = &bootinfo.bi_part[i];
			end = bp->bstart + bp->bcount;
			if (bp->bcount != 0 && end <= MAXLBA &&
			    end > wb->wb_HDCount)
				wb->wb_HDCount = end;
		}
	}
	cb = (UBYTE *)MKPTR(SEGBASE(CMDSEG), 0);
	for (i = 0; i < CMD_FD + CB_SIZE; i++)
		cb[i] = 0;
	wb->wb_Int.is_Node.ln_Name = wdname;
	wb->wb_Int.is_Data = (APTR)wb;
	wb->wb_Int.is_Code = wdintr;
	xSetIntVector((UWORD)INTV_DISK, &wb->wb_Int);
	wb->wb_Tick.is_Node.ln_Pri = 0;
	wb->wb_Tick.is_Node.ln_Name = wdname;
	wb->wb_Tick.is_Data = (APTR)wb;
	wb->wb_Tick.is_Code = wdtick;
	xAddIntServer((UWORD)INTV_TICK, &wb->wb_Tick);
	xAddDevice(&wb->wb_Device);
}
