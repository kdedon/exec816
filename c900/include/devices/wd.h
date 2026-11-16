/* SPDX-License-Identifier: MIT */
/*
 * wd.device: the C900 disk controller's hard disks (drives 0-1) and
 * floppies (drives 2-3).  Requests are struct IOStdReq; io_Offset is a
 * zero-based 512-byte sector within the opened unit, as upstream's
 * ideplus.device.  docs/internals.md gives the contract.
 */
#ifndef DEVICES_WD_H
#define DEVICES_WD_H

#include <exec/io.h>

#define WDNAME		"wd.device"

/* Unit numbers: drive in bits 5-6, part in bits 0-4.  Part 0 is the whole
 * drive (raw); part n (1-16) of a hard disk is kboot's bi_part[n - 1].
 * Floppies have part 0 only. */
#define WD_UNIT(d, p)	(((ULONG)(d) << 5) | (ULONG)(p))
#define WD_RAW(d)	WD_UNIT(d, 0)
#define WD_SLOT(d, s)	WD_UNIT(d, (s) + 1)
#define WD_NDRIVES	4
#define WD_NHD		2		/* drives 0-1; 2-3 are floppies */
#define WD_NSLOT	16		/* bi_part slots */

#define WD_SECTOR	512
#define WD_FDBLOCKS	2392L		/* the one C900 floppy format */

/* Commands besides CMD_READ, CMD_WRITE, CMD_UPDATE and CMD_CLEAR. */
#define WD_GETGEOMETRY	12		/* upstream GET_GEOMETRY */
#define WD_CHANGENUM	16		/* io_Actual = media change count */
#define WD_CHANGESTATE	17		/* io_Actual = 0 medium ready, 1 not */

/* WD_GETGEOMETRY's record, upstream's 12-byte Geometry. */
struct WDGeometry {
	UWORD wg_Bytes;		/* 512 */
	UWORD wg_Flags;		/* WDGF_ */
	ULONG wg_Sectors;	/* sectors in the unit */
	ULONG wg_Generation;	/* media generation, nonzero */
};

#define WDGF_READONLY	0x0001		/* upstream FLAG_READONLY */
#define WDGF_DURABLE	0x0002		/* upstream FLAG_DURABLE */
#define WDGF_REMOVABLE	0x0004

/* Device errors (positive io_Error), numbered as upstream's ideplus. */
#define WDERR_ABSENT	1		/* no medium, drive not ready */
#define WDERR_TIMEOUT	2		/* no completion; controller offline */
#define WDERR_DEVICE	3		/* other controller status */
#define WDERR_PROTECTED	4		/* write-protected medium or unit */
#define WDERR_CHANGED	6		/* media change did not clear */
#define WDERR_OFFLINE	7		/* controller offline since a timeout */
#define WDERR_MEDIA	8		/* unrecoverable media error */

#endif
