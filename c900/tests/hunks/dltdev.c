/* SPDX-License-Identifier: MIT */
/*
 * dlt.device, the disk-loaded device of tests/ramlib.c (DEVS:dlt.device).
 * Unit 0 only.  CMD_READ completes at once with io_Actual 42; any other
 * command fails with IOERR_NOCMD.  Expunge is the Amiga device's, as in
 * dlt.library.
 */
#include <exec/exec.h>
#include <lvo/exec_lib.h>

struct DltDev {
	struct Device dd_Dev;
	APTR dd_Seg;
};

static char ddname[] = "dlt.device";
static char ddid[] = "dlt.device 1.0 (disk device test)";

static void
ddopen(dev, ior, unit, flags)
struct Library *dev;
struct IORequest *ior;
ULONG unit, flags;
{
	if (unit != 0) {
		ior->io_Error = IOERR_OPENFAIL;
		return;
	}
	ior->io_Unit = NULL;
	dev->lib_OpenCnt++;
	dev->lib_Flags &= ~LIBF_DELEXP;
}

static APTR
ddexpunge(dev)
register struct Library *dev;
{
	APTR seg;

	if (dev->lib_OpenCnt != 0) {
		dev->lib_Flags |= LIBF_DELEXP;
		return (NULL);
	}
	seg = ((struct DltDev *)dev)->dd_Seg;
	Remove(&dev->lib_Node);
	FreeMem((char *)dev - dev->lib_NegSize,
	    (ULONG)(dev->lib_NegSize + dev->lib_PosSize));
	return (seg);
}

static APTR
ddclose(dev, ior)
struct Library *dev;
struct IORequest *ior;
{
	if (--dev->lib_OpenCnt == 0 && (dev->lib_Flags & LIBF_DELEXP))
		return (ddexpunge(dev));
	return (NULL);
}

static APTR
ddnull()
{
	return (NULL);
}

static void
ddbegin(dev, ior)
struct Library *dev;
register struct IOStdReq *ior;
{
	ior->io_Error = 0;
	if (ior->io_Command == CMD_READ)
		ior->io_Actual = 42;
	else
		ior->io_Error = IOERR_NOCMD;
	if ((ior->io_Flags & IOF_QUICK) == 0)
		ReplyMsg(&ior->io_Message);
}

static LONG
ddabort(dev, ior)
struct Library *dev;
struct IORequest *ior;
{
	return (-1L);
}

static struct Library *
ddinit(dev, seg, sysbase)
struct Library *dev;
APTR seg, sysbase;
{
	dev->lib_Node.ln_Name = ddname;
	dev->lib_IdString = (APTR)ddid;
	dev->lib_Version = 1;
	((struct DltDev *)dev)->dd_Seg = seg;
	return (dev);
}

static APTR ddfuncs[] = {
	(APTR)ddopen, (APTR)ddclose, (APTR)ddexpunge, (APTR)ddnull,
	(APTR)ddbegin, (APTR)ddabort, (APTR)-1L
};

static struct {
	ULONG it_Size;
	APTR it_Funcs, it_Struct, it_Init;
} ddinittab = {
	sizeof(struct DltDev), (APTR)ddfuncs, NULL, (APTR)ddinit
};

struct Resident ddtag = {
	RTC_MATCHWORD, &ddtag, (APTR)(&ddtag + 1), RTF_AUTOINIT, 1,
	NT_DEVICE, 0, ddname, ddid, (APTR)&ddinittab
};
