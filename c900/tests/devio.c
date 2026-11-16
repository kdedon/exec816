/* SPDX-License-Identifier: MIT */
/*
 * Device I/O through a small test device defined here, built by
 * MakeLibrary: CMD_READ completes at once, CMD_WRITE is queued and
 * completed by a tick server three ticks later.  Checks OpenDevice
 * admission, DoIO quick and deferred, SendIO with CheckIO and WaitIO,
 * exact collection with two requests on one port, amiga.lib's BeginIO,
 * AbortIO, CloseDevice
 * and the Amiga delayed expunge: RemDevice while open sets LIBF_DELEXP
 * and the last close expunges and frees the device.
 */
#include "test.h"
#include <exec/exec.h>

#define DELAY		3
#define DATALEN		8

struct TestDev {
	struct Device td_Dev;
	struct Unit td_Unit;
	struct List td_Queue;
	struct Interrupt td_Int;
};

static struct TestDev *testdev;
static int expunged;
static char devname[] = "test.device";

/* A queued request's due tick is kept in io_Offset. */
#define DUE(r)	(((struct IOStdReq *)(r))->io_Offset)

static void
dopen(dev, ior, unit, flags)
struct Device *dev;
struct IORequest *ior;
ULONG unit, flags;
{
	struct TestDev *td;

	td = (struct TestDev *)dev;
	if (unit != 0 || flags != 0) {
		ior->io_Error = IOERR_OPENFAIL;
		return;
	}
	ior->io_Unit = &td->td_Unit;
	td->td_Unit.unit_OpenCnt++;
	dev->dd_Library.lib_OpenCnt++;
	dev->dd_Library.lib_Flags &= ~LIBF_DELEXP;
}

static APTR
dexpunge(dev)
struct Device *dev;
{
	struct Library *lib;

	lib = &dev->dd_Library;
	if (lib->lib_OpenCnt != 0) {
		lib->lib_Flags |= LIBF_DELEXP;
		return (NULL);
	}
	Remove(lib);
	RemIntServer(INTV_TICK, &((struct TestDev *)dev)->td_Int);
	FreeMem((char *)lib - lib->lib_NegSize,
	    lib->lib_NegSize + lib->lib_PosSize);
	expunged++;
	return (NULL);
}

static APTR
dclose(dev, ior)
struct Device *dev;
struct IORequest *ior;
{
	((struct TestDev *)dev)->td_Unit.unit_OpenCnt--;
	if (--dev->dd_Library.lib_OpenCnt == 0 &&
	    (dev->dd_Library.lib_Flags & LIBF_DELEXP))
		return (dexpunge(dev));
	return (NULL);
}

static APTR
dnull()
{
	return (NULL);
}

static void
dbegin(dev, ior)
struct Device *dev;
struct IORequest *ior;
{
	struct TestDev *td;
	struct IOStdReq *io;
	UBYTE *p;
	int i;

	td = (struct TestDev *)dev;
	io = (struct IOStdReq *)ior;
	ior->io_Error = 0;
	ior->io_Message.mn_Node.ln_Type = NT_MESSAGE;
	switch (ior->io_Command) {
	case CMD_READ:
		p = io->io_Data;
		for (i = 0; i < (int)io->io_Length; i++)
			p[i] = 0xA0 + i;
		io->io_Actual = io->io_Length;
		TermIO(ior);
		return;
	case CMD_WRITE:
		/* Deferred: clear IOF_QUICK before the server can see it. */
		ior->io_Flags &= ~IOF_QUICK;
		Disable();
		DUE(ior) = TickCount + DELAY;
		AddTail(&td->td_Queue, ior);
		Enable();
		return;
	default:
		ior->io_Error = IOERR_NOCMD;
		TermIO(ior);
	}
}

static LONG
dabort(dev, ior)
struct Device *dev;
struct IORequest *ior;
{
	struct TestDev *td;
	struct Node *n;
	LONG r;

	td = (struct TestDev *)dev;
	r = -1;
	Disable();
	for (n = td->td_Queue.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (n == (struct Node *)ior) {
			Remove(n);
			ior->io_Error = IOERR_ABORTED;
			ReplyMsg(&ior->io_Message);
			r = 0;
			break;
		}
	Enable();
	return (r);
}

/* Completion from interrupt context. */
static int
dserver(td, n)
struct TestDev *td;
UWORD n;
{
	struct IORequest *ior;

	while ((ior = (struct IORequest *)td->td_Queue.lh_Head)
	    ->io_Message.mn_Node.ln_Succ != NULL &&
	    (LONG)(TickCount - DUE(ior)) >= 0) {
		Remove(ior);
		((struct IOStdReq *)ior)->io_Actual =
		    ((struct IOStdReq *)ior)->io_Length;
		ReplyMsg(&ior->io_Message);
	}
	return (0);
}

static APTR funcs[] = {
	(APTR)dopen, (APTR)dclose, (APTR)dexpunge, (APTR)dnull,
	(APTR)dbegin, (APTR)dabort, (APTR)-1L
};

static char *
mkdev()
{
	struct TestDev *td;

	td = (struct TestDev *)MakeLibrary(funcs, NULL, NULL,
	    sizeof(struct TestDev), NULL);
	if (td == NULL)
		return ("MakeLibrary");
	if (td->td_Dev.dd_Library.lib_NegSize != 36 ||
	    td->td_Dev.dd_Library.lib_PosSize != sizeof(struct TestDev))
		return ("MakeLibrary sizes");
	testdev = td;
	td->td_Dev.dd_Library.lib_Node.ln_Name = devname;
	NewList(&td->td_Queue);
	td->td_Int.is_Node.ln_Name = devname;
	td->td_Int.is_Data = (APTR)td;
	td->td_Int.is_Code = dserver;
	AddIntServer(INTV_TICK, &td->td_Int);
	AddDevice(&td->td_Dev);
	if (td->td_Dev.dd_Library.lib_Node.ln_Type != NT_DEVICE)
		return ("AddDevice type");
	return (NULL);
}

static UBYTE buf[DATALEN];

static char *
run()
{
	struct MsgPort *port;
	struct IOStdReq *io, *io2;
	struct IORequest ior3;
	ULONG t0, dt;
	int err;

	char *why;

	if ((why = mkdev()) != NULL)
		return (why);
	if ((port = CreateMsgPort()) == NULL)
		return ("CreateMsgPort");
	if (CreateIORequest(NULL, sizeof(struct IOStdReq)) != NULL)
		return ("CreateIORequest without a port");
	io = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
	io2 = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
	if (io == NULL || io2 == NULL)
		return ("CreateIORequest");
	if (io->io_Message.mn_ReplyPort != port ||
	    io->io_Message.mn_Length != sizeof(struct IOStdReq))
		return ("CreateIORequest fields");
	if (CheckIO(io) != (struct IORequest *)io)
		return ("CheckIO of a request never sent");

	/* Admission. */
	if (OpenDevice("nosuch.device", 0, io, 0) != IOERR_OPENFAIL ||
	    io->io_Device != NULL)
		return ("OpenDevice of a missing device");
	if (OpenDevice(devname, 1, io, 0) != IOERR_OPENFAIL ||
	    io->io_Device != NULL || io->io_Error != IOERR_OPENFAIL)
		return ("OpenDevice of a bad unit");
	if ((err = OpenDevice(devname, 0, io, 0)) != 0)
		return ("OpenDevice");
	if (io->io_Device != &testdev->td_Dev ||
	    io->io_Unit != &testdev->td_Unit ||
	    testdev->td_Dev.dd_Library.lib_OpenCnt != 1)
		return ("OpenDevice binding");

	/* Quick DoIO: no reply. */
	io->io_Command = CMD_READ;
	io->io_Data = buf;
	io->io_Length = DATALEN;
	if (DoIO(io) != 0 || io->io_Actual != DATALEN || buf[3] != 0xA3)
		return ("DoIO CMD_READ");
	if ((io->io_Flags & IOF_QUICK) == 0 || CheckIO(io) == NULL ||
	    !IsListEmpty(&port->mp_MsgList))
		return ("quick DoIO replied");
	io->io_Command = CMD_INVALID;
	if (DoIO(io) != IOERR_NOCMD)
		return ("DoIO bad command");

	/* Deferred DoIO waits for the server's reply and collects it. */
	io->io_Command = CMD_WRITE;
	io->io_Actual = 0;
	t0 = TickCount;
	if (DoIO(io) != 0)
		return ("DoIO CMD_WRITE");
	dt = TickCount - t0;
	if (dt < DELAY || dt > DELAY + 1 || io->io_Actual != DATALEN)
		return ("DoIO CMD_WRITE timing");
	if (!IsListEmpty(&port->mp_MsgList))
		return ("DoIO left its reply");

	/* SendIO, CheckIO, WaitIO. */
	SendIO(io);
	if (CheckIO(io) != NULL)
		return ("CheckIO while pending");
	if (WaitIO(io) != 0)
		return ("WaitIO");
	if (CheckIO(io) != (struct IORequest *)io ||
	    io->io_Message.mn_Node.ln_Type != NT_REPLYMSG ||
	    !IsListEmpty(&port->mp_MsgList))
		return ("CheckIO after WaitIO");

	/* BeginIO calls the driver with io_Flags as given. */
	io->io_Flags = IOF_QUICK;
	BeginIO(io);
	if ((io->io_Flags & IOF_QUICK) || CheckIO(io) != NULL ||
	    WaitIO(io) != 0)
		return ("BeginIO deferred");
	io->io_Command = CMD_READ;
	io->io_Flags = IOF_QUICK;
	io->io_Error = IOERR_NOCMD;
	BeginIO(io);
	if ((io->io_Flags & IOF_QUICK) == 0 || io->io_Error != 0 ||
	    !IsListEmpty(&port->mp_MsgList))
		return ("BeginIO quick");
	io->io_Command = CMD_WRITE;

	/* A borrowed request; WaitIO takes exactly the one it is given. */
	io2->io_Device = io->io_Device;
	io2->io_Unit = io->io_Unit;
	io2->io_Command = CMD_WRITE;
	io2->io_Length = 1;
	SendIO(io);
	Sleep(1);
	SendIO(io2);
	if (WaitIO(io2) != 0)
		return ("WaitIO second request");
	if (CheckIO(io) == NULL || GetMsg(port) != (struct Message *)io)
		return ("first reply not left on the port");
	if (!IsListEmpty(&port->mp_MsgList))
		return ("port not empty");

	/* AbortIO: a pending request completes with IOERR_ABORTED and the
	 * driver returns 0; a finished one is left alone. */
	SendIO(io);
	if (AbortIO(io) != 0 || WaitIO(io) != IOERR_ABORTED)
		return ("AbortIO result");
	if (AbortIO(io) == 0 || io->io_Error != IOERR_ABORTED ||
	    !IsListEmpty(&port->mp_MsgList))
		return ("AbortIO of a finished request");
	io->io_Command = CMD_READ;
	if (DoIO(io) != 0)
		return ("DoIO after AbortIO");

	/* RemDevice while open: the device's Expunge only flags it; the
	 * last close expunges.  A second open in between keeps it. */
	RemDevice(testdev);
	if (expunged != 0 ||
	    !(testdev->td_Dev.dd_Library.lib_Flags & LIBF_DELEXP))
		return ("RemDevice while open");
	ior3.io_Message.mn_ReplyPort = port;
	ior3.io_Message.mn_Length = sizeof(ior3);
	if (OpenDevice(devname, 0, &ior3, 0) != 0 ||
	    (testdev->td_Dev.dd_Library.lib_Flags & LIBF_DELEXP))
		return ("OpenDevice after RemDevice");
	CloseDevice(&ior3);
	if (expunged != 0 || testdev->td_Dev.dd_Library.lib_OpenCnt != 1 ||
	    testdev->td_Unit.unit_OpenCnt != 1)
		return ("CloseDevice of one of two");
	RemDevice(testdev);
	CloseDevice(io);
	if (io->io_Device != NULL || io->io_Unit != NULL)
		return ("CloseDevice");
	if (expunged != 1)
		return ("no expunge at last close");
	if (OpenDevice(devname, 0, &ior3, 0) != IOERR_OPENFAIL)
		return ("OpenDevice after expunge");

	DeleteIORequest(io);
	DeleteIORequest(io2);
	DeleteIORequest(NULL);
	DeleteMsgPort(port);
	return (NULL);
}

int
main()
{
	char *why;
	ULONG before;

	before = AvailMem(MEMF_ANY);
	if ((why = run()) != NULL) {
		xKPrintf("FAIL devio: %s\n", why);
		return (1);
	}
	if (AvailMem(MEMF_ANY) != before) {
		xKPrintf("FAIL devio: memory leaked\n");
		return (1);
	}
	KPutStr("PASS devio\n");
	return (0);
}
