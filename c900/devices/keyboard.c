/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * keyboard.device and the console's key presses: the C900 keyboard on
 * CIO U70 port A.  Upstream's keyboard is the Atari's, behind its input
 * service, so the model is classic Amiga keyboard.device; docs/internals.md
 * gives the contract and the departures.
 *
 * The keyboard interface board presents one byte at a time: PA6-PA0 the
 * code, PC2 set on release, PA7 (DAV) set while the byte waits.  Port A
 * raises a pattern-match interrupt on PA7 high (vector INTV_KBD).  Each
 * byte is acknowledged by clearing port A's IP and IUS and then pulsing PC3
 * low, which clears the byte and DAV and lets the keyboard send the next.
 *
 * Every byte goes through take(), with VI off: from the interrupt, from
 * kbd_getraw's poll and from a tick server.  It translates the code to an
 * Amiga raw key code, updates the key matrix and qualifiers, and appends
 * the event to two rings: one for KBD_READEVENT, one of key presses for
 * the console, whose keymap is textcon.device's.  Reads are completed
 * only by the interrupt, the tick server and BeginIO, never by
 * kbd_getraw, so the console's poll never replies a message or switches
 * Tasks.  BeginIO and AbortIO run unlocked: the read
 * list and the rings change only with VI off.
 *
 * CIO U70 is shared (docs/hardware.md, "CIO U70 sharing"): MCCR and port
 * C are read-modify-written with VI off; port C writes protect every bit
 * but PC3 and carry PC0-PC2 as read.
 */
#include "kernel.h"
#include <devices/keyboard.h>
#include <devices/rawkeycodes.h>

#define P_MICR		0x0001
#define P_MCCR		0x0003
#define P_PAIV		0x0005
#define P_PCDPP		0x000b
#define P_PCDD		0x000d
#define P_PCSIO		0x000f
#define P_PACS		0x0011
#define P_PADATA	0x001b
#define P_PCDATA	0x001f
#define P_PAMS		0x0041
#define P_PAHS		0x0043
#define P_PADPP		0x0045
#define P_PADD		0x0047
#define P_PASIO		0x0049
#define P_PAPP		0x004b
#define P_PAPT		0x004d
#define P_PAPM		0x004f
#define P_LATCH		0x0205	/* write-only: bit 0 IEEE REN, bit 1 keyboard */

#define MCCR_PAE	0x04
#define PAMS_MATCH	0x1a	/* bit port, single buffered, match only, AND */
#define PA_DAV		0x80
#define PA_CODE		0x7f
#define PACS_CLIPIUS	0x20	/* clear IP and IUS */
#define PACS_SETIE	0xc0
#define PACS_CLRIE	0xe0
#define PC_UP		0x04	/* PC2: the byte is a release */
#define PC_CLR		0x08	/* PC3: low clears the byte and DAV */
#define PC_WPCLR	0x70	/* write-protect PC0-PC2 */
#define PC_KEEP		0x07
#define LATCH_KBD	0x02

#define TAKEMAX		4	/* bytes per interrupt or tick */
#define NEVENT		32	/* raw event ring */
#define NCON		32	/* the console's key presses */
#define NXLATE		0x63	/* keyboard codes 0x01-0x62 */
#define KX_KEYPAD	0x80	/* kxlate: a keypad key */
#define KBDTICKPRI	80

struct KEvent {
	UBYTE ke_Code;
	UBYTE ke_Pad;
	UWORD ke_Qual;
	ULONG ke_Tick;
};

struct KbdBase {
	struct Device kb_Device;
	struct Unit kb_Unit;
	struct Interrupt kb_Int;
	struct Interrupt kb_Tick;
	struct List kb_Reads;
};

/* Bytes taken by the interrupt and by polls; events and key presses
 * dropped because a ring was full. */
ULONG KbdIntrs;
ULONG KbdPolled;
ULONG KbdLost;

static struct {
	struct JumpVec lv_Vec[6];
	struct KbdBase lv_Base;
} kbdbaselib;
#define kbdbase kbdbaselib.lv_Base
static char kbdname[] = KEYBOARDNAME;
static struct KEvent kevent[NEVENT];
static UWORD kehead, kecount;
static UBYTE kccode[NCON], kcqual[NCON];
static UWORD kchead, kccount;
static UBYTE kmatrix[KBD_MATRIXSIZE];
static UWORD kqual;
static int khw;		/* port A programmed */
static UBYTE latch;	/* copy of P_LATCH */

/* Keyboard code (PC XT set 1) to Amiga raw key code, KX_KEYPAD for a
 * keypad key. */
static UBYTE kxlate[NXLATE] = {
	0x00, 0x45, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,	/* 00 */
	0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x41, 0x42,	/* 08 */
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,	/* 10 */
	0x18, 0x19, 0x1a, 0x1b, 0x44, 0x63, 0x20, 0x21,	/* 18 */
	0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,	/* 20 */
	0x2a, 0x00, 0x60, 0x0d, 0x31, 0x32, 0x33, 0x34,	/* 28 */
	0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x61, 0xdd,	/* 30 */
	0x64, 0x40, 0x62, 0x50, 0x51, 0x52, 0x53, 0x54,	/* 38 */
	0x55, 0x56, 0x57, 0x58, 0x59, 0x6c, 0x6d, 0xbd,	/* 40 */
	0xbe, 0xbf, 0xca, 0xad, 0xae, 0xaf, 0xde, 0x9d,	/* 48 */
	0x9e, 0x9f, 0x8f, 0x8e, 0x5f, 0x46, 0x6f, 0x6b,	/* 50 */
	0x9c, 0xbc, 0xdc, 0x2b, 0x2c, 0x3b, 0xc3, 0x4c,	/* 58 */
	0x4f, 0x4e, 0x4d				/* 60 */
};

#define FIRST(l)	((l)->lh_Head->ln_Succ != NULL ? \
			    (struct IORequest *)(l)->lh_Head : \
			    (struct IORequest *)NULL)
#define STD(ior)	((struct IOStdReq *)(ior))

/* Set and clear bits of the write-only system latch at 0x0205. */
void
ksetlatch(clr, set)
int clr, set;
{
	int s;

	s = intoff();
	latch = (latch & ~clr) | set;
	outb(P_LATCH, latch);
	intrest(s);
}

/* PC3 low then high; PC0-PC2 protected and written as read.  VI off. */
static void
strobe()
{
	register int pc;

	pc = inb(P_PCDATA) & PC_KEEP;
	outb(P_PCDATA, PC_WPCLR | pc);
	outb(P_PCDATA, PC_WPCLR | PC_CLR | pc);
}

static UWORD
qualbit(code)
int code;
{
	switch (code) {
	case RAWKEY_LSHIFT:
		return (IEQUALIFIER_LSHIFT);
	case RAWKEY_RSHIFT:
		return (IEQUALIFIER_RSHIFT);
	case RAWKEY_CONTROL:
		return (IEQUALIFIER_CONTROL);
	case RAWKEY_LALT:
		return (IEQUALIFIER_LALT);
	}
	return (0);
}

/* Take the waiting byte: acknowledge it, then record it.  VI off. */
static void
take()
{
	register int code, up;
	register struct KEvent *e;
	int bit, m, rep, x;
	UWORD q;

	up = inb(P_PCDATA) & PC_UP;
	code = inb(P_PADATA) & PA_CODE;
	outb(P_PACS, PACS_CLIPIUS);
	strobe();
	if (code == 0 || code >= NXLATE)
		return;
	x = kxlate[code];
	code = x & ~KX_KEYPAD;
	bit = 1 << (code & 7);
	m = code >> 3;
	rep = 0;
	q = qualbit(code);
	if (up) {
		kmatrix[m] &= ~bit;
		kqual &= ~q;
		/* The keyboard toggles its Caps Lock lamp on release. */
		if (code == RAWKEY_CAPSLOCK)
			kqual ^= IEQUALIFIER_CAPSLOCK;
	} else {
		rep = (kmatrix[m] & bit) != 0;
		kmatrix[m] |= bit;
		kqual |= q;
	}
	q = kqual;
	if (x & KX_KEYPAD)
		q |= IEQUALIFIER_NUMERICPAD;
	if (rep)
		q |= IEQUALIFIER_REPEAT;
	if (kecount == NEVENT)
		KbdLost++;
	else {
		e = &kevent[(kehead + kecount) % NEVENT];
		e->ke_Code = code | (up ? IECODE_UP_PREFIX : 0);
		e->ke_Qual = q;
		e->ke_Tick = TickCount;
		kecount++;
	}
	if (up)
		return;
	if (kccount == NCON) {
		KbdLost++;
		return;
	}
	m = (kchead + kccount) % NCON;
	kccode[m] = code;
	kcqual[m] = q;
	kccount++;
}

/* Take up to TAKEMAX waiting bytes; returns how many.  VI off. */
static int
drain()
{
	register int n;

	for (n = 0; n < TAKEMAX && (inb(P_PADATA) & PA_DAV) != 0; n++)
		take();
	return (n);
}

/* Move events into a KBD_READEVENT request; TRUE when it took any.  VI
 * off. */
static int
fill(ior)
struct IORequest *ior;
{
	register struct IOStdReq *r;
	register struct InputEvent *ie;
	register struct KEvent *e;
	ULONG n, i;

	r = STD(ior);
	n = r->io_Length / sizeof(struct InputEvent);
	if (n > kecount)
		n = kecount;
	ie = (struct InputEvent *)r->io_Data;
	for (i = 0; i < n; i++, ie++) {
		e = &kevent[kehead];
		if (++kehead == NEVENT)
			kehead = 0;
		kecount--;
		ie->ie_NextEvent = i + 1 < n ? ie + 1 : NULL;
		ie->ie_Class = IECLASS_RAWKEY;
		ie->ie_SubClass = 0;
		ie->ie_Code = e->ke_Code;
		ie->ie_Qualifier = e->ke_Qual;
		ie->ie_EventAddress = NULL;
		ie->ie_TimeStamp.tv_secs = e->ke_Tick / TICKS_PER_SEC;
		ie->ie_TimeStamp.tv_micro =
		    (e->ke_Tick % TICKS_PER_SEC) * (1000000L / TICKS_PER_SEC);
	}
	r->io_Actual = n * sizeof(struct InputEvent);
	return (n != 0);
}

/* Complete the reads the ring can satisfy.  VI off. */
static void
serve()
{
	register struct IORequest *ior;

	while (kecount != 0 && (ior = FIRST(&kbdbase.kb_Reads)) != NULL) {
		xRemove(&ior->io_Message.mn_Node);
		fill(ior);
		ior->io_Error = 0;
		xTermIO(ior);
	}
}

static int
kbdint(data, n)
APTR data;
UWORD n;
{
	KbdIntrs += drain();
	serve();
	return (0);
}

/* Bytes an interrupt did not take (polled mode, or a lost interrupt), and
 * reads kbd_getraw's polls left pending. */
static int
kbdtick(data, n)
APTR data;
UWORD n;
{
	if (inb(P_PADATA) & PA_DAV)
		KbdPolled += drain();
	if (kecount != 0)
		serve();
	return (0);
}

/* Program port A for the keyboard, with its interrupt off, and enable the
 * keyboard.  A byte already waiting is taken, else the interface is
 * cleared. */
static void
hwinit()
{
	int s;

	s = intoff();
	outb(P_MCCR, inb(P_MCCR) & ~MCCR_PAE);
	outb(P_PAMS, PAMS_MATCH);
	outb(P_PAHS, 0);
	outb(P_PADPP, 0);
	outb(P_PADD, 0xff);
	outb(P_PASIO, 0);
	outb(P_PAPP, PA_DAV);
	outb(P_PAPT, 0);
	outb(P_PAPM, PA_DAV);
	outb(P_PAIV, INTV_KBD);
	outb(P_PACS, PACS_CLRIE);
	outb(P_PACS, PACS_CLIPIUS);
	outb(P_PCDD, (inb(P_PCDD) & ~PC_CLR) | PC_UP);
	outb(P_PCDPP, inb(P_PCDPP) & ~(PC_UP | PC_CLR));
	outb(P_PCSIO, inb(P_PCSIO) & ~(PC_UP | PC_CLR));
	outb(P_MCCR, inb(P_MCCR) | MCCR_PAE);
	khw = 1;
	if (inb(P_PADATA) & PA_DAV)
		KbdPolled += drain();
	else
		strobe();
	intrest(s);
	ksetlatch(0, LATCH_KBD);
}

/*
 * The console's next key press: the Amiga raw key code in the low byte,
 * the low byte of its qualifiers in the high byte; -1 when none.  Never
 * waits.  Callable with VI off, from interrupt code and before the device
 * or the interrupts are set up; it polls the interface itself.
 */
int
kbd_getraw()
{
	register int c;
	int s;

	if (!khw)
		hwinit();
	s = intoff();
	if (inb(P_PADATA) & PA_DAV)
		KbdPolled += drain();
	c = -1;
	if (kccount != 0) {
		c = kccode[kchead] | (kcqual[kchead] & 0x7f) << 8;
		if (++kchead == NCON)
			kchead = 0;
		kccount--;
	}
	intrest(s);
	return (c);
}

static void
kbegin(dev, ior)
struct Device *dev;
register struct IORequest *ior;
{
	register struct IOStdReq *r;
	register int i;
	int s, err;

	IOBEGIN(ior);
	r = STD(ior);
	err = 0;
	switch (ior->io_Command) {
	case KBD_READEVENT:
		r->io_Actual = 0;
		if (r->io_Length < sizeof(struct InputEvent)) {
			err = IOERR_BADLENGTH;
			break;
		}
		s = intoff();
		if (IsListEmpty(&kbdbase.kb_Reads) && fill(ior)) {
			intrest(s);
			break;
		}
		ior->io_Flags &= ~IOF_QUICK;
		xAddTail(&kbdbase.kb_Reads, &ior->io_Message.mn_Node);
		intrest(s);
		return;
	case KBD_READMATRIX:
		s = intoff();
		for (i = 0; i < KBD_MATRIXSIZE && i < r->io_Length; i++)
			r->io_Data[i] = kmatrix[i];
		intrest(s);
		r->io_Actual = i;
		break;
	case CMD_CLEAR:
		s = intoff();
		kehead = kecount = 0;
		kchead = kccount = 0;
		intrest(s);
		break;
	default:
		err = IOERR_NOCMD;
	}
	ior->io_Error = err;
	xTermIO(ior);
}

static LONG
kabort(dev, ior)
struct Device *dev;
register struct IORequest *ior;
{
	register struct Node *n;
	LONG r;
	int s;

	r = -1;
	s = intoff();
	for (n = kbdbase.kb_Reads.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (n == &ior->io_Message.mn_Node) {
			xRemove(n);
			ior->io_Error = IOERR_ABORTED;
			xTermIO(ior);
			r = 0;
			break;
		}
	intrest(s);
	return (r);
}

static void
kopen(dev, ior, unit, flags)
struct Device *dev;
struct IORequest *ior;
ULONG unit, flags;
{
	if (unit != 0 || ior->io_Message.mn_ReplyPort == NULL) {
		ior->io_Error = IOERR_OPENFAIL;
		return;
	}
	if (ior->io_Message.mn_Length < sizeof(struct IOStdReq)) {
		ior->io_Error = IOERR_BADLENGTH;
		return;
	}
	kbdbase.kb_Unit.unit_OpenCnt++;
	ior->io_Unit = &kbdbase.kb_Unit;
	dev->dd_Library.lib_OpenCnt++;
}

/* Closing with a read of this binding still queued is misuse, as in
 * serial.device. */
static APTR
kclose(dev, ior)
struct Device *dev;
struct IORequest *ior;
{
	register struct Node *n;
	struct MsgPort *port;
	int s;

	port = ior->io_Message.mn_ReplyPort;
	s = intoff();
	for (n = kbdbase.kb_Reads.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (((struct Message *)n)->mn_ReplyPort == port)
			kpanic("keyboard.device: CloseDevice with reads pending");
	intrest(s);
	kbdbase.kb_Unit.unit_OpenCnt--;
	dev->dd_Library.lib_OpenCnt--;
	return (NULL);
}

static APTR kbdbasefuncs[] = {
	(APTR)kopen, (APTR)kclose, (APTR)knull, (APTR)knull,
	(APTR)kbegin, (APTR)kabort, (APTR)-1L
};

void kkbdinit();

struct Resident kbdtag = {
	RTC_MATCHWORD, &kbdtag, (APTR)(&kbdtag + 1), RTF_COLDSTART, 1,
	NT_DEVICE, 0, kbdname, NULL, (APTR)kkbdinit
};

/* Boot: program the port if kbd_getraw has not, take the interrupt, and
 * add the device. */
void
kkbdinit()
{
	register struct KbdBase *kb;
	int s;

	kb = &kbdbase;
	xNewList(&kb->kb_Reads);
	if (!khw)
		hwinit();
	kb->kb_Int.is_Node.ln_Name = kbdname;
	kb->kb_Int.is_Data = (APTR)kb;
	kb->kb_Int.is_Code = kbdint;
	xSetIntVector((UWORD)INTV_KBD, &kb->kb_Int);
	kb->kb_Tick.is_Node.ln_Pri = KBDTICKPRI;
	kb->kb_Tick.is_Node.ln_Name = kbdname;
	kb->kb_Tick.is_Data = (APTR)kb;
	kb->kb_Tick.is_Code = kbdtick;
	xAddIntServer((UWORD)INTV_TICK, &kb->kb_Tick);
	s = intoff();
	outb(P_PACS, PACS_CLIPIUS);
	outb(P_PACS, PACS_SETIE);
	intrest(s);
	kinitlib(&kb->kb_Device.dd_Library, kbdname, kbdbasefuncs);
	xAddDevice(&kb->kb_Device);
}
