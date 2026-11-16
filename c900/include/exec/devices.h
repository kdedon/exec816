/* SPDX-License-Identifier: MIT */
/*
 * Exec devices, as Amiga <exec/devices.h>: a device is a library whose
 * vectors after the standard four are BeginIO and AbortIO.  Each is
 * called with the device base as its first argument:
 *
 *   -6  Open(dev, ior, ULONG unit, ULONG flags): set io_Error on failure;
 *       on success set io_Unit and count lib_OpenCnt
 *   -12 Close(dev, ior): APTR seglist or NULL
 *   -18 Expunge(dev): APTR seglist or NULL
 *   -24 reserved: returns 0
 *   -30 BeginIO(dev, ior)
 *   -36 AbortIO(dev, ior): LONG, 0 when aborted
 */
#ifndef EXEC_DEVICES_H
#define EXEC_DEVICES_H

#include <exec/libraries.h>
#include <exec/ports.h>
#include <exec/io.h>

struct Device {
	struct Library dd_Library;
};

/* Optional per-unit record a driver may point io_Unit at. */
struct Unit {
	struct MsgPort unit_MsgPort;
	UBYTE unit_flags;
	UBYTE unit_pad;
	UWORD unit_OpenCnt;
};

#define UNITF_ACTIVE	0x01
#define UNITF_INTASK	0x02

/* amiga.lib's BeginIO: the device's -30 vector, io_Flags as given.  ior
 * is evaluated three times. */
#define BeginIO(ior) \
	(*(void (*)())((char *)((struct IORequest *)(ior))->io_Device + \
	    DEV_BEGINIO))(((struct IORequest *)(ior))->io_Device, \
	    (struct IORequest *)(ior))

#endif
