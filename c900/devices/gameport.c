/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * gameport.device unit 0: a mouse in the classic Amiga interface, from
 * one of two backends chosen at load.
 *
 * HR: the card's J2 mouse.  The card has two free-running 10-bit
 * quadrature counters (X at 0x0400, Y at 0x0402; right and down count
 * up) and the three buttons as levels in bits 15:13, and no interrupt.
 * A server on the 100 Hz tick samples both ports and differences the
 * counts modulo 1024.
 *
 * Serial: a Mouse Systems mouse on SCC U74 channel A, serial.device unit
 * 1, which this device opens exclusively at load and keeps.  Packets are
 * five bytes at 1200 8N1: 0x80 | {L,M,R} (0 = pressed), then dX1, dY1,
 * dX2, dY2, signed, Y up.  A reader Task decodes them and feeds each half
 * packet as one sample, Y negated to the Amiga's down-positive.
 *
 * While the controller type is GPCT_MOUSE each sample's button changes
 * and motion that meets the trigger become InputEvents on a short
 * queue; GPD_READEVENT requests take whole events from it.  A full
 * queue folds further motion into its newest event.  The tick counts
 * the trigger timeout for both backends.
 *
 * GetGPButtons, the first vector after AbortIO, returns the buttons
 * held now, from the port (HR) or the reader's last packet (serial),
 * for any Task and without an open unit or a controller type.
 *
 * Open and Close run under OpenDevice's Forbid.  BeginIO and AbortIO run
 * unlocked; the queue, the trigger and the pending list are shared with
 * the server and the reader, so they are read and changed with VI off.
 * The device is its own boot module.  The kernel loads it with the HR
 * display module; on any other machine it loads when Modules lists it.
 * A second load finds the device present and does nothing.
 */
#include <exec/exec.h>
#include <devices/gameport.h>
#include <devices/serial.h>
#include <resources/framebuf.h>

int inw();
int intoff();
void intrest();

#define XPORT		0x0400
#define YPORT		0x0402
#define MCOUNT		0x03ff
#define MSIGN		0x0200
#define MBLEFT		0x8000
#define MBMID		0x4000
#define MBRIGHT		0x2000
#define MBALL		(MBLEFT | MBMID | MBRIGHT)

#define GPQN		8		/* events queued; a power of two */
#define GPPRI		0		/* after the scheduler and timer.device */
#define IESIZE		sizeof(struct InputEvent)
#define GPHZ		100		/* tick rate */

#define SMLINE		1		/* serial.device unit: U74 channel A */
#define SMBAUD		1200
#define SMPRI		10		/* the reader, above ordinary Tasks */
#define SMSTACK		768L
#define SMSYNC		0xf8		/* header: (byte & SMSYNC) == SMHDR */
#define SMHDR		0x80
#define SMLEFT		0x04		/* header button bits, 0 = pressed */
#define SMMID		0x02
#define SMRIGHT		0x01

/* The start of BeginIO, as on the Amiga: ior is pending. */
#define IOBEGIN(ior)	((ior)->io_Error = 0, \
			    (ior)->io_Message.mn_Node.ln_Type = NT_MESSAGE)

struct GPBase {
	struct Device gp_Device;
	struct Unit gp_Unit;
	struct List gp_Pending;
	struct Interrupt gp_Int;
	struct GamePortTrigger gp_Trig;
	struct InputEvent gp_Q[GPQN];
	UWORD gp_Head;
	UWORD gp_Count;
	UWORD gp_LastX;
	UWORD gp_LastY;
	UWORD gp_Buttons;
	WORD gp_AccX;
	WORD gp_AccY;
	UWORD gp_Idle;			/* ticks since the last event */
	UWORD gp_SButtons;		/* serial: buttons last decoded */
	BYTE gp_CType;
	BYTE gp_Serial;			/* the serial backend */
};

/* Allocated when a backend is found; the device exists only then. */
static struct GPBase *gpbase;
static char gpname[] = GAMEPORTNAME;
APTR FrameBufBase;

/* The serial backend's line, open from load on. */
static struct MsgPort smport;
static struct IOExtSer smio;
static UBYTE smbuf[2];


/* Signed motion between two 10-bit counts. */
static int
gpdiff(now, was)
UWORD now, was;
{
	register int d;

	d = (now - was) & MCOUNT;
	if (d & MSIGN)
		d -= MCOUNT + 1;
	return (d);
}

static UWORD
gpqual(b)
register UWORD b;
{
	register UWORD q;

	q = IEQUALIFIER_RELATIVEMOUSE;
	if (b & MBLEFT)
		q |= IEQUALIFIER_LEFTBUTTON;
	if (b & MBMID)
		q |= IEQUALIFIER_MIDBUTTON;
	if (b & MBRIGHT)
		q |= IEQUALIFIER_RBUTTON;
	return (q);
}

/* Queue one event carrying the motion accumulated so far.  VI off. */
static void
emit(gp, class, code)
register struct GPBase *gp;
int class, code;
{
	register struct InputEvent *e;
	ULONG t;

	if (gp->gp_Count == GPQN) {
		e = &gp->gp_Q[(gp->gp_Head + GPQN - 1) & (GPQN - 1)];
		e->ie_X += gp->gp_AccX;
		e->ie_Y += gp->gp_AccY;
	} else {
		e = &gp->gp_Q[(gp->gp_Head + gp->gp_Count) & (GPQN - 1)];
		gp->gp_Count++;
		e->ie_NextEvent = NULL;
		e->ie_Class = class;
		e->ie_SubClass = 0;
		e->ie_Code = code;
		e->ie_Qualifier = gpqual(gp->gp_Buttons);
		e->ie_X = gp->gp_AccX;
		e->ie_Y = gp->gp_AccY;
		t = SysBase->ex_TickCount;
		e->ie_TimeStamp.tv_secs = t / GPHZ;
		e->ie_TimeStamp.tv_micro = (t % GPHZ) * (1000000L / GPHZ);
	}
	gp->gp_AccX = gp->gp_AccY = 0;
	gp->gp_Idle = 0;
}

/* Move as many queued events as fit into ior's buffer.  VI off. */
static void
gpcopy(gp, ior)
register struct GPBase *gp;
register struct IOStdReq *ior;
{
	register struct InputEvent *d;
	register UWORD n, i;

	n = (UWORD)(ior->io_Length / IESIZE);
	if (n > gp->gp_Count)
		n = gp->gp_Count;
	d = (struct InputEvent *)ior->io_Data;
	for (i = 0; i < n; i++) {
		d[i] = gp->gp_Q[gp->gp_Head];
		d[i].ie_NextEvent = i + 1 < n ? &d[i + 1] : NULL;
		gp->gp_Head = (gp->gp_Head + 1) & (GPQN - 1);
	}
	gp->gp_Count -= n;
	ior->io_Actual = (ULONG)n * IESIZE;
	ior->io_Error = 0;
}

static void
gpbutton(gp, b, mask, code)
register struct GPBase *gp;
UWORD b, mask;
int code;
{
	if (((b ^ gp->gp_Buttons) & mask) == 0)
		return;
	gp->gp_Buttons ^= mask;
	if (b & mask) {
		if (gp->gp_Trig.gpt_Keys & GPTF_DOWNKEYS)
			emit(gp, IECLASS_RAWMOUSE, code);
	} else if (gp->gp_Trig.gpt_Keys & GPTF_UPKEYS)
		emit(gp, IECLASS_RAWMOUSE, code | IECODE_UP_PREFIX);
}

static int
gpmoved(acc, delta)
register int acc;
register UWORD delta;
{
	if (acc < 0)
		acc = -acc;
	return (acc != 0 && (UWORD)acc >= (delta ? delta : 1));
}

/* One sample: the buttons now and the motion since the last.  VI off. */
static void
gpfeed(gp, b, dx, dy)
register struct GPBase *gp;
UWORD b;
int dx, dy;
{
	gp->gp_AccX += dx;
	gp->gp_AccY += dy;
	gpbutton(gp, b, MBLEFT, IECODE_LBUTTON);
	gpbutton(gp, b, MBRIGHT, IECODE_RBUTTON);
	gpbutton(gp, b, MBMID, IECODE_MBUTTON);
	if (gpmoved(gp->gp_AccX, gp->gp_Trig.gpt_XDelta) ||
	    gpmoved(gp->gp_AccY, gp->gp_Trig.gpt_YDelta))
		emit(gp, IECLASS_RAWMOUSE, IECODE_NOBUTTON);
}

/* Reply the pending reads the queue can fill.  VI off. */
static void
gpreply(gp)
register struct GPBase *gp;
{
	register struct IORequest *ior;

	while (gp->gp_Count != 0 && (ior = (struct IORequest *)
	    gp->gp_Pending.lh_Head)->io_Message.mn_Node.ln_Succ != NULL) {
		Remove(&ior->io_Message.mn_Node);
		gpcopy(gp, (struct IOStdReq *)ior);
		ReplyMsg(&ior->io_Message);
	}
}

static int
gpserv(gp, n)
register struct GPBase *gp;
UWORD n;
{
	register UWORD x, y;

	if (gp->gp_CType != GPCT_MOUSE)
		return (0);
	gp->gp_Idle++;
	if (!gp->gp_Serial) {
		x = inw(XPORT);
		y = inw(YPORT) & MCOUNT;
		gpfeed(gp, x & MBALL, gpdiff(x & MCOUNT, gp->gp_LastX),
		    gpdiff(y, gp->gp_LastY));
		gp->gp_LastX = x & MCOUNT;
		gp->gp_LastY = y;
	}
	if (gp->gp_Trig.gpt_Timeout != 0 &&
	    gp->gp_Idle >= gp->gp_Trig.gpt_Timeout)
		emit(gp, IECLASS_TIMER, IECODE_NOBUTTON);
	gpreply(gp);
	return (0);
}

/* Adopt the current counts (HR) and buttons as the reference, empty
 * the queue.  VI off. */
static void
gporigin(gp)
register struct GPBase *gp;
{
	register UWORD x;

	if (gp->gp_Serial)
		gp->gp_Buttons = gp->gp_SButtons;
	else {
		x = inw(XPORT);
		gp->gp_LastX = x & MCOUNT;
		gp->gp_Buttons = x & MBALL;
		gp->gp_LastY = inw(YPORT) & MCOUNT;
	}
	gp->gp_AccX = gp->gp_AccY = 0;
	gp->gp_Idle = 0;
	gp->gp_Head = gp->gp_Count = 0;
}

static void
gpopen(dev, ior, unit, flags)
struct Device *dev;
register struct IORequest *ior;
ULONG unit, flags;
{
	if (unit != 0 || ior->io_Message.mn_ReplyPort == NULL) {
		ior->io_Error = IOERR_OPENFAIL;
		return;
	}
	ior->io_Unit = &gpbase->gp_Unit;
	gpbase->gp_Unit.unit_OpenCnt++;
	dev->dd_Library.lib_OpenCnt++;
}

static APTR
gpnull()
{
	return (NULL);
}

static void
gpmisuse()
{
	KPutStr("gameport.device: CloseDevice with requests pending\n");
	Alert(AT_DeadEnd | AN_GamePortDev);
}

/* Closing with a request of this binding pending is misuse, as in
 * timer.device.  The last close frees the port: sampling stops. */
static APTR
gpclose(dev, ior)
struct Device *dev;
struct IORequest *ior;
{
	register struct Node *n;
	register struct GPBase *gp;
	struct MsgPort *port;
	int s;

	gp = gpbase;
	port = ior->io_Message.mn_ReplyPort;
	s = intoff();
	for (n = gp->gp_Pending.lh_Head; n->ln_Succ != NULL;
	    n = n->ln_Succ)
		if (((struct Message *)n)->mn_ReplyPort == port)
			gpmisuse();
	if (--gp->gp_Unit.unit_OpenCnt == 0) {
		gp->gp_CType = GPCT_NOCONTROLLER;
		gp->gp_Head = gp->gp_Count = 0;
	}
	intrest(s);
	dev->dd_Library.lib_OpenCnt--;
	return (NULL);
}

static void
gpbegin(dev, ior)
struct Device *dev;
register struct IOStdReq *ior;
{
	register struct GPBase *gp;
	register struct IORequest *r;
	UBYTE *data;
	int err, s;

	IOBEGIN(ior);
	gp = gpbase;
	data = ior->io_Data;
	err = 0;
	ior->io_Actual = 0;
	switch (ior->io_Command) {
	case GPD_READEVENT:
		if (ior->io_Length < IESIZE) {
			err = IOERR_BADLENGTH;
			break;
		}
		s = intoff();
		if (gp->gp_CType != GPCT_MOUSE) {
			intrest(s);
			err = GPDERR_SETCTYPE;
			break;
		}
		if (gp->gp_Count != 0) {
			gpcopy(gp, ior);
			intrest(s);
			TermIO((struct IORequest *)ior);
			return;
		}
		ior->io_Flags &= ~IOF_QUICK;
		AddTail(&gp->gp_Pending, &ior->io_Message.mn_Node);
		intrest(s);
		return;
	case GPD_ASKCTYPE:
		if (ior->io_Length < 1) {
			err = IOERR_BADLENGTH;
			break;
		}
		*data = gp->gp_CType;
		ior->io_Actual = 1;
		break;
	case GPD_SETCTYPE:
		if (ior->io_Length < 1) {
			err = IOERR_BADLENGTH;
			break;
		}
		switch ((BYTE)*data) {
		case GPCT_MOUSE:
			s = intoff();
			gporigin(gp);
			gp->gp_CType = GPCT_MOUSE;
			intrest(s);
			break;
		case GPCT_NOCONTROLLER:
		case GPCT_ALLOCATED:
			gp->gp_CType = *data;
			break;
		default:
			err = GPDERR_SETCTYPE;
		}
		ior->io_Actual = err ? 0 : 1;
		break;
	case GPD_ASKTRIGGER:
		if (ior->io_Length < sizeof(struct GamePortTrigger)) {
			err = IOERR_BADLENGTH;
			break;
		}
		s = intoff();
		*(struct GamePortTrigger *)data = gp->gp_Trig;
		intrest(s);
		ior->io_Actual = sizeof(struct GamePortTrigger);
		break;
	case GPD_SETTRIGGER:
		if (ior->io_Length < sizeof(struct GamePortTrigger)) {
			err = IOERR_BADLENGTH;
			break;
		}
		s = intoff();
		gp->gp_Trig = *(struct GamePortTrigger *)data;
		gp->gp_Idle = 0;
		intrest(s);
		ior->io_Actual = sizeof(struct GamePortTrigger);
		break;
	case CMD_CLEAR:
		s = intoff();
		gp->gp_Head = gp->gp_Count = 0;
		gp->gp_AccX = gp->gp_AccY = 0;
		intrest(s);
		break;
	case CMD_FLUSH:
		s = intoff();
		while ((r = (struct IORequest *)RemHead(&gp->gp_Pending))
		    != NULL) {
			r->io_Error = IOERR_ABORTED;
			ReplyMsg(&r->io_Message);
		}
		intrest(s);
		break;
	default:
		err = IOERR_NOCMD;
	}
	ior->io_Error = err;
	TermIO((struct IORequest *)ior);
}

/* The first terminal commit wins: a request no longer pending is left
 * alone. */
static LONG
gpabort(dev, ior)
struct Device *dev;
register struct IORequest *ior;
{
	register struct Node *n;
	LONG r;
	int s;

	r = -1;
	s = intoff();
	for (n = gpbase->gp_Pending.lh_Head; n->ln_Succ != NULL;
	    n = n->ln_Succ)
		if (n == &ior->io_Message.mn_Node) {
			Remove(n);
			ior->io_Error = IOERR_ABORTED;
			ReplyMsg(&ior->io_Message);
			r = 0;
			break;
		}
	intrest(s);
	return (r);
}

/* GetGPButtons(): IEQUALIFIER_LEFTBUTTON, _MIDBUTTON, _RBUTTON. */
static ULONG
gpbuttons(dev)
struct Device *dev;
{
	register struct GPBase *gp;

	gp = gpbase;
	return ((ULONG)(gpqual(gp->gp_Serial ? gp->gp_SButtons :
	    (UWORD)inw(XPORT) & MBALL) & ~IEQUALIFIER_RELATIVEMOUSE));
}

static APTR gpfuncs[] = {
	(APTR)gpopen, (APTR)gpclose, (APTR)gpnull, (APTR)gpnull,
	(APTR)gpbegin, (APTR)gpabort, (APTR)gpbuttons, (APTR)-1L
};

static void gpinit();

struct Resident gptag = {
	RTC_MATCHWORD, &gptag, (APTR)(&gptag + 1), RTF_COLDSTART, 1,
	NT_DEVICE, 0, gpname, NULL, (APTR)gpinit
};

/* Read n bytes into smbuf; FALSE on a line error or a short read. */
static int
smread(n)
int n;
{
	smio.IOSer.io_Command = CMD_READ;
	smio.IOSer.io_Data = (APTR)smbuf;
	smio.IOSer.io_Length = n;
	return (DoIO((struct IORequest *)&smio) == 0 &&
	    smio.IOSer.io_Actual == n);
}

static int
smdelta(c)
register int c;
{
	c &= 0xff;
	return (c & 0x80 ? c - 0x100 : c);
}

/* One half packet from smbuf.  Y up becomes the Amiga's down. */
static void
smhalf(gp, b)
register struct GPBase *gp;
UWORD b;
{
	int s;

	s = intoff();
	gp->gp_SButtons = b;
	if (gp->gp_CType == GPCT_MOUSE) {
		gpfeed(gp, b, smdelta(smbuf[0]), -smdelta(smbuf[1]));
		gpreply(gp);
	}
	intrest(s);
}

/* The reader: 1200 8N1, then packets for ever.  The header is hunted a
 * byte at a time; a line error drops the packet. */
static void
smtask()
{
	register struct GPBase *gp;
	register int h;
	UWORD b;

	gp = gpbase;
	smport.mp_SigBit = AllocSignal(-1);
	smport.mp_SigTask = FindTask(NULL);
	smport.mp_Flags = PA_SIGNAL;
	smio.IOSer.io_Command = SDCMD_SETPARAMS;
	smio.io_Baud = SMBAUD;
	smio.io_RBufLen = SER_MINBUF;
	smio.io_ReadLen = smio.io_WriteLen = 8;
	smio.io_StopBits = 1;
	smio.io_SerFlags = SERF_XDISABLED;
	if (DoIO((struct IORequest *)&smio) != 0)
		KPutStr("gameport.device: serial line not set\n");
	for (;;) {
		if (!smread(1) || ((h = smbuf[0]) & SMSYNC) != SMHDR)
			continue;
		b = 0;
		if ((h & SMLEFT) == 0)
			b |= MBLEFT;
		if ((h & SMMID) == 0)
			b |= MBMID;
		if ((h & SMRIGHT) == 0)
			b |= MBRIGHT;
		if (smread(2)) {
			smhalf(gp, b);
			if (smread(2))
				smhalf(gp, b);
		}
	}
}

/* Claim the serial mouse's line: TRUE when it is ours. */
static int
smopen()
{
	NewList(&smport.mp_MsgList);
	smport.mp_Node.ln_Type = NT_MSGPORT;
	smport.mp_Flags = PA_IGNORE;
	smio.IOSer.io_Message.mn_ReplyPort = &smport;
	smio.IOSer.io_Message.mn_Length = sizeof smio;
	smio.io_SerFlags = 0;
	return (OpenDevice(SERIALNAME, (ULONG)SMLINE,
	    (struct IORequest *)&smio, 0L) == 0);
}

/* HR with an HR card, otherwise the serial mouse.  The base comes from
 * MakeLibrary, cleared. */
static void
gpinit()
{
	register struct GPBase *gp;
	register struct Node *n;
	int serial;

	Forbid();
	n = FindName(&SysBase->DeviceList, gpname);
	Permit();
	if (n != NULL)
		return;
	serial = (FrameBufBase = OpenResource(FRAMEBUFNAME)) == NULL ||
	    GetFBInfo()->fbi_Card != FBC_HR;
	if (serial && !smopen()) {
		KPutStr("gameport.device: serial line 1 not available\n");
		return;
	}
	gp = (struct GPBase *)MakeLibrary((APTR)gpfuncs, NULL, NULL,
	    (ULONG)sizeof(struct GPBase), NULL);
	if (gp == NULL) {
		KPutStr("gameport.device: no memory\n");
		if (serial)
			CloseDevice((struct IORequest *)&smio);
		return;
	}
	gpbase = gp;
	gp->gp_Serial = serial;
	gp->gp_Device.dd_Library.lib_Node.ln_Name = gpname;
	gp->gp_Device.dd_Library.lib_Version = 1;
	NewList(&gp->gp_Pending);
	gp->gp_CType = GPCT_NOCONTROLLER;
	gp->gp_Trig.gpt_Keys = GPTF_DOWNKEYS | GPTF_UPKEYS;
	gp->gp_Trig.gpt_Timeout = 0;
	gp->gp_Trig.gpt_XDelta = 1;
	gp->gp_Trig.gpt_YDelta = 1;
	gp->gp_Int.is_Node.ln_Pri = GPPRI;
	gp->gp_Int.is_Node.ln_Name = gpname;
	gp->gp_Int.is_Data = (APTR)gp;
	gp->gp_Int.is_Code = gpserv;
	if (serial && CreateTask(gpname, (LONG)SMPRI, (TASKPROC)smtask,
	    SMSTACK) == NULL) {
		KPutStr("gameport.device: no reader\n");
		CloseDevice((struct IORequest *)&smio);
		return;
	}
	AddIntServer((UWORD)INTV_TICK, &gp->gp_Int);
	AddDevice(&gp->gp_Device);
}
