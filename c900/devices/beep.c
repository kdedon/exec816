/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * beep.device and the console bell: the internal speaker.  CIO U66
 * counter/timer 2 runs as a continuous square wave with its output on
 * PB0, which sinks the speaker coil through 7407s while low.  The
 * counters clock at PCLK/2 (375 kHz) and a square wave toggles once per
 * time constant, so TC = 187500 / f.  docs/internals.md gives the
 * interface.
 *
 * kparinit resets and configures U66, so kbeepinit must run after it.
 * While silent, PB0 is an input and floats high, so the coil carries no
 * current whatever CT2 does.  U66 belongs to the speaker and the printer
 * only; its MCCR and PBDD are read-modify-written with VI off.
 *
 * Requests play in order from one queue.  A tick server ends each tone;
 * no code waits for one.  BeginIO and AbortIO run unlocked; the queue and
 * the sounding state are changed with VI off because the server completes
 * requests.  Open and Close run under OpenDevice's Forbid.
 */
#include "kernel.h"
#include <devices/beep.h>

#define P_MCCR		0x0083
#define P_CT2CS		0x0097
#define P_CT2TCH	0x00b1
#define P_CT2TCL	0x00b3
#define P_CT2MS		0x00bb
#define P_PBDD		0x00d7

#define MCCR_CT2E	0x20
#define CT2_TONE	0xc2	/* continuous, external output, square wave */
#define CT2_RUN		0x06	/* gate and trigger */
#define PB_SPK		0x01

#define CTHZ		187500L	/* half the counter clock */
#define TICKUS		10000L
#define BELLFREQ	440L
#define BELLTICKS	13	/* 130 ms */

/* After serial.device's server at 90. */
#define BEEPTICKPRI	80

/* bb_State. */
#define BS_IDLE		0
#define BS_PLAY		1	/* the queue's head is sounding or resting */
#define BS_BELL		2

struct BeepBase {
	struct Device bb_Device;
	struct Unit bb_Unit;
	struct List bb_Queue;
	struct Interrupt bb_Tick;
	ULONG bb_End;		/* tick the current sound ends at */
	UWORD bb_State;
	UWORD bb_Ready;
};

static struct {
	struct JumpVec lv_Vec[6];
	struct BeepBase lv_Base;
} beepbaselib;
#define beepbase beepbaselib.lv_Base
static char beepname[] = BEEPNAME;

#define BREQ(ior)	((struct IOBeep *)(ior))
#define FIRST(l)	((l)->lh_Head->ln_Succ != NULL ? \
			    (struct IORequest *)(l)->lh_Head : \
			    (struct IORequest *)NULL)

static void
complete(ior, err)
register struct IORequest *ior;
int err;
{
	ior->io_Error = err;
	xTermIO(ior);
}

/* Silence: PB0 back to an input first, then stop CT2 and drop its
 * external output.  VI off. */
static void
quiet()
{
	outb(P_PBDD, inb(P_PBDD) | PB_SPK);
	outb(P_CT2CS, 0);
	outb(P_MCCR, inb(P_MCCR) & ~MCCR_CT2E);
	outb(P_CT2MS, 0);
}

/* Sound f Hz, or silence for 0.  The mode register is written only with
 * CT2 disabled.  VI off. */
static void
sound(f)
ULONG f;
{
	register UWORD tc;

	if (f == 0) {
		quiet();
		return;
	}
	tc = (UWORD)(CTHZ / f);
	outb(P_CT2CS, 0);
	outb(P_MCCR, inb(P_MCCR) & ~MCCR_CT2E);
	outb(P_CT2MS, CT2_TONE);
	outb(P_CT2TCH, (tc >> 8) & 0xff);
	outb(P_CT2TCL, tc & 0xff);
	outb(P_MCCR, inb(P_MCCR) | MCCR_CT2E);
	outb(P_CT2CS, CT2_RUN);
	outb(P_PBDD, inb(P_PBDD) & ~PB_SPK);
}

/* Start the queue's head, completing zero-length tones on the way, or
 * go silent.  On a tick boundary (aligned) the first tick is whole;
 * otherwise one more tick keeps the tone at least io_Micros.  VI off. */
static void
startnext(bb, aligned)
register struct BeepBase *bb;
int aligned;
{
	register struct IORequest *ior;
	ULONG n;

	while ((ior = FIRST(&bb->bb_Queue)) != NULL) {
		n = (BREQ(ior)->io_Micros + TICKUS - 1) / TICKUS;
		if (n == 0) {
			xRemove(&ior->io_Message.mn_Node);
			complete(ior, 0);
			continue;
		}
		sound(BREQ(ior)->io_Freq);
		bb->bb_End = TickCount + n + (aligned ? 0 : 1);
		bb->bb_State = BS_PLAY;
		return;
	}
	quiet();
	bb->bb_State = BS_IDLE;
}

static int
beeptick(bb, n)
register struct BeepBase *bb;
UWORD n;
{
	register struct IORequest *ior;

	if (bb->bb_State == BS_IDLE || (long)(TickCount - bb->bb_End) < 0)
		return (0);
	if (bb->bb_State == BS_PLAY && (ior = FIRST(&bb->bb_Queue)) != NULL) {
		xRemove(&ior->io_Message.mn_Node);
		complete(ior, 0);
	}
	startnext(bb, 1);
	return (0);
}

/* The console's BEL: a short tone unless a request is playing.  Safe from
 * any context; a no-op before kbeepinit. */
void
con_bell()
{
	register struct BeepBase *bb;
	int s;

	bb = &beepbase;
	if (!bb->bb_Ready)
		return;
	s = intoff();
	if (bb->bb_State != BS_PLAY) {
		sound(BELLFREQ);
		bb->bb_End = TickCount + BELLTICKS;
		bb->bb_State = BS_BELL;
	}
	intrest(s);
}

/* Abort every queued tone.  VI off. */
static void
flushq(bb)
register struct BeepBase *bb;
{
	register struct IORequest *ior;

	while ((ior = FIRST(&bb->bb_Queue)) != NULL) {
		xRemove(&ior->io_Message.mn_Node);
		complete(ior, IOERR_ABORTED);
	}
	if (bb->bb_State == BS_PLAY) {
		quiet();
		bb->bb_State = BS_IDLE;
	}
}

static void
bbegin(dev, ior)
struct Device *dev;
register struct IORequest *ior;
{
	register struct BeepBase *bb;
	register struct IOBeep *b;
	int err, s;

	IOBEGIN(ior);
	bb = (struct BeepBase *)dev;
	b = BREQ(ior);
	err = 0;
	switch (ior->io_Command) {
	case CMD_WRITE:
		if (b->io_Freq != 0 && (b->io_Freq < BEEP_MINFREQ ||
		    b->io_Freq > BEEP_MAXFREQ)) {
			err = BeepErr_Freq;
			break;
		}
		if (b->io_Micros == 0)
			break;
		s = intoff();
		ior->io_Flags &= ~IOF_QUICK;
		xAddTail(&bb->bb_Queue, &ior->io_Message.mn_Node);
		if (bb->bb_State != BS_PLAY)
			startnext(bb, 0);
		intrest(s);
		return;
	case CMD_FLUSH:
		s = intoff();
		flushq(bb);
		intrest(s);
		break;
	case CMD_RESET:
		s = intoff();
		flushq(bb);
		quiet();
		bb->bb_State = BS_IDLE;
		intrest(s);
		break;
	default:
		err = IOERR_NOCMD;
	}
	complete(ior, err);
}

static LONG
babort(dev, ior)
struct Device *dev;
register struct IORequest *ior;
{
	register struct BeepBase *bb;
	register struct Node *n;
	LONG r;
	int head, s;

	bb = (struct BeepBase *)dev;
	r = -1;
	s = intoff();
	for (n = bb->bb_Queue.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (n == &ior->io_Message.mn_Node)
			break;
	if (n->ln_Succ != NULL) {
		head = n == bb->bb_Queue.lh_Head;
		xRemove(n);
		complete(ior, IOERR_ABORTED);
		if (head && bb->bb_State == BS_PLAY)
			startnext(bb, 0);
		r = 0;
	}
	intrest(s);
	return (r);
}

static void
bopen(dev, ior, unit, flags)
struct Device *dev;
register struct IORequest *ior;
ULONG unit, flags;
{
	register struct BeepBase *bb;

	bb = (struct BeepBase *)dev;
	if (unit != 0 || flags != 0) {
		ior->io_Error = IOERR_OPENFAIL;
		return;
	}
	if (ior->io_Message.mn_Length < sizeof(struct IOBeep)) {
		ior->io_Error = IOERR_BADLENGTH;
		return;
	}
	bb->bb_Unit.unit_OpenCnt++;
	ior->io_Unit = &bb->bb_Unit;
	dev->dd_Library.lib_OpenCnt++;
}

/* Closing with a request of this binding still queued is misuse, as in
 * timer.device. */
static APTR
bclose(dev, ior)
struct Device *dev;
struct IORequest *ior;
{
	register struct BeepBase *bb;
	register struct Node *n;
	struct MsgPort *port;
	int s;

	bb = (struct BeepBase *)dev;
	port = ior->io_Message.mn_ReplyPort;
	s = intoff();
	for (n = bb->bb_Queue.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (((struct Message *)n)->mn_ReplyPort == port)
			kpanic("beep.device: CloseDevice with requests pending");
	bb->bb_Unit.unit_OpenCnt--;
	intrest(s);
	dev->dd_Library.lib_OpenCnt--;
	return (NULL);
}

static APTR beepbasefuncs[] = {
	(APTR)bopen, (APTR)bclose, (APTR)knull, (APTR)knull,
	(APTR)bbegin, (APTR)babort, (APTR)-1L
};

void kbeepinit();

struct Resident beeptag = {
	RTC_MATCHWORD, &beeptag, (APTR)(&beeptag + 1), RTF_COLDSTART, 1,
	NT_DEVICE, 0, beepname, NULL, (APTR)kbeepinit
};

void
kbeepinit()
{
	register struct BeepBase *bb;
	int s;

	bb = &beepbase;
	xNewList(&bb->bb_Queue);
	s = intoff();
	quiet();
	intrest(s);
	bb->bb_State = BS_IDLE;
	bb->bb_Tick.is_Node.ln_Pri = BEEPTICKPRI;
	bb->bb_Tick.is_Node.ln_Name = beepname;
	bb->bb_Tick.is_Data = (APTR)bb;
	bb->bb_Tick.is_Code = beeptick;
	xAddIntServer((UWORD)INTV_TICK, &bb->bb_Tick);
	kinitlib(&bb->bb_Device.dd_Library, beepname, beepbasefuncs);
	xAddDevice(&bb->bb_Device);
	bb->bb_Ready = 1;
}
