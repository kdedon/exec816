/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Exec devices and device I/O.  Port of lib/io/iocore.act and the request
 * paths of lib/io/task-io.inc; the contract is docs/reference/device-io.md
 * and docs/reference/resident-drivers.md.  Devices are Amiga libraries
 * with a jump table, listed by AddDevice in priority order, instead of
 * upstream's generated static route table; docs/internals.md records the
 * departure.
 *
 * As on the Amiga, OpenDevice and CloseDevice hold Forbid around the
 * driver call; submission and AbortIO call the driver directly, and each
 * driver protects its own queues and unit state.  Request state that
 * interrupts complete is read and collected with VI off.
 */
#include "kernel.h"

/* Resident modules compiled into the image, in initialisation order;
 * each driver's romtag points at its init function. */
extern struct Resident timertag, batttag, wdtag, sertag, partag;
extern struct Resident beeptag, kbdtag, contag;

struct Resident *ResModules[] = {
	&batttag,	/* timer.device seeds the system time from it */
	&timertag,
	&wdtag,
	&sertag,
	&partag,	/* resets CIO U66: before beep.device */
	&beeptag,
	&kbdtag,
	&contag,
	NULL
};

void
xAddDevice(dev)
struct Device *dev;
{
	kaddlib(&DeviceList, &dev->dd_Library, NT_DEVICE);
}

/* As Amiga: the device's Open sets io_Error and counts the open. */
BYTE
xOpenDevice(name, unit, ior, flags)
char *name;
ULONG unit;
register struct IORequest *ior;
ULONG flags;
{
	register struct Device *dev;
	APTR (*fn)();
	int err;

	xForbid();
	ior->io_Unit = NULL;
	ior->io_Error = IOERR_OPENFAIL;
	dev = (struct Device *)xFindName(&DeviceList, name);
	if (dev != NULL) {
		ior->io_Device = dev;
		ior->io_Error = 0;
		fn = LIBVEC(dev, LIB_OPEN);
		(*fn)(dev, ior, unit, flags);
	}
	if ((err = ior->io_Error) != 0) {
		ior->io_Device = NULL;
		ior->io_Unit = NULL;
	}
	xPermit();
	return (err);
}

/* The caller has collected every request that borrows this binding.
 * Amiga sets io_Device and io_Unit to -1; that address does not trap on
 * the Z8010, NULL does. */
void
xCloseDevice(ior)
register struct IORequest *ior;
{
	register struct Device *dev;
	APTR (*fn)();

	xForbid();
	if ((dev = ior->io_Device) != NULL) {
		fn = LIBVEC(dev, LIB_CLOSE);
		(*fn)(dev, ior);
		ior->io_Device = NULL;
		ior->io_Unit = NULL;
	}
	xPermit();
}

/* Amiga's SendIO and DoIO: mark ior pending and call the device's
 * BeginIO. */
static void
submit(ior, flags)
register struct IORequest *ior;
int flags;
{
	register struct Device *dev;
	APTR (*fn)();

	ior->io_Flags = flags;
	ior->io_Error = 0;
	ior->io_Message.mn_Node.ln_Type = NT_MESSAGE;
	dev = ior->io_Device;
	fn = LIBVEC(dev, DEV_BEGINIO);
	(*fn)(dev, ior);
}

void
xSendIO(ior)
struct IORequest *ior;
{
	submit(ior, 0);
}

BYTE
xDoIO(ior)
struct IORequest *ior;
{
	submit(ior, IOF_QUICK);
	return (xWaitIO(ior));
}

/* Amiga's test: done when quick or replied.  CreateIORequest makes a new
 * request NT_REPLYMSG, so one never sent is done. */
struct IORequest *
xCheckIO(ior)
struct IORequest *ior;
{
	if ((ior->io_Flags & IOF_QUICK) ||
	    ior->io_Message.mn_Node.ln_Type == NT_REPLYMSG)
		return (ior);
	return (NULL);
}

/* Amiga's WaitIO: a quick request is done; otherwise wait until it is
 * replied and take it off the reply port.  Other messages stay. */
BYTE
xWaitIO(ior)
register struct IORequest *ior;
{
	ULONG mask;
	int s;

	if ((ior->io_Flags & IOF_QUICK) == 0) {
		mask = 1L << ior->io_Message.mn_ReplyPort->mp_SigBit;
		s = intoff();
		while (ior->io_Message.mn_Node.ln_Type != NT_REPLYMSG)
			xWait(mask);
		xRemove(&ior->io_Message.mn_Node);
		intrest(s);
	}
	return (ior->io_Error);
}

/* The driver's result: 0 when it aborted the request. */
LONG
xAbortIO(ior)
register struct IORequest *ior;
{
	register struct Device *dev;
	APTR (*fn)();

	dev = ior->io_Device;
	fn = LIBVEC(dev, DEV_ABORTIO);
	return ((LONG)(*fn)(dev, ior));
}

/* Driver completion, from a Task or a handler: a quick request is done
 * in place, as on the Amiga; any other is replied. */
void
xTermIO(ior)
register struct IORequest *ior;
{
	if ((ior->io_Flags & IOF_QUICK) == 0)
		xReplyMsg(&ior->io_Message);
}

struct IORequest *
xCreateIOReq(port, size)
struct MsgPort *port;
ULONG size;
{
	register struct IORequest *ior;

	if (port == NULL || size < sizeof(struct Message) || size > 0xFFFFL)
		return (NULL);
	ior = (struct IORequest *)xAllocMem(size,
	    (ULONG)(MEMF_PUBLIC | MEMF_CLEAR));
	if (ior == NULL)
		return (NULL);
	ior->io_Message.mn_ReplyPort = port;
	ior->io_Message.mn_Length = (UWORD)size;
	ior->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
	return (ior);
}

/* The caller has collected ior and closed or never opened it. */
void
xDeleteIOReq(ior)
struct IORequest *ior;
{
	if (ior == NULL)
		return;
	xFreeMem((APTR)ior, (ULONG)ior->io_Message.mn_Length);
}
