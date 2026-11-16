/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The display half of textcon.device, common to every display module:
 * the terminal emulator's screen, a queue for bytes that cannot be drawn
 * now, and the romtag whose init attaches the card's backend
 * (vidsetup) to textcon.device (<display.h>).
 *
 * One drawer holds the screen at a time (conbusy).  A Task draws under
 * Forbid with its interrupts on, so a scroll never holds VI off.  A byte
 * textcon.device says to queue (interrupt level, VI off) or that finds
 * the screen held goes to the queue, which the next drawer empties
 * first.  Once the system has stopped, bytes are drawn at once unless the
 * screen was held.  textcon.device calls cd_Reset when framebuf.resource
 * returns the card, or a crash takes it back; nothing draws then.
 */
#include <exec/exec.h>
#include <devices/textcon.h>
#include "c900.h"
#include "display.h"
#include "vt.h"

#define CONQ		256	/* bytes waiting for a drawer */

int intoff();
void intrest();
struct Display *vidsetup();
extern char vidname[];

static struct VT convt;
static UWORD conbusy;		/* a drawer holds the screen */
static UWORD cqhead;
static UWORD cqcount;
static UBYTE *conq;
static ULONG cqdrop;		/* bytes lost to a full queue */

/* VI off.  Queue a byte for the next drawer; drop it when full. */
static void
qput(c)
int c;
{
	register int i;

	if (cqcount == CONQ) {
		cqdrop++;
		return;
	}
	i = cqhead + cqcount;
	if (i >= CONQ)
		i -= CONQ;
	conq[i] = c;
	cqcount++;
}

/* Draw what is queued, then give the screen back.  A byte queued after
 * the last check waits for the next drawer. */
static void
release(how)
int how;
{
	register int c;
	int s;

	for (;;) {
		s = intoff();
		if (cqcount == 0) {
			conbusy = 0;
			intrest(s);
			break;
		}
		c = conq[cqhead];
		if (++cqhead == CONQ)
			cqhead = 0;
		cqcount--;
		intrest(s);
		vt_putc(&convt, c);
	}
	if (how == CDO_DRAW)
		Permit();
}

/* textcon.device's output: n bytes of p, any context. */
static void
cdout(p, n, how)
register UBYTE *p;
register unsigned n;
int how;
{
	int s;

	s = intoff();
	if (how == CDO_QUEUE || conbusy) {
		while (n-- != 0)
			qput(*p++);
		intrest(s);
		return;
	}
	conbusy = 1;
	intrest(s);
	if (how == CDO_DRAW)
		Forbid();
	vt_write(&convt, p, (long)n);
	release(how);
}

/* The card back from framebuf.resource's owner: as at attach, with
 * the queue empty and the screen free. */
static void
cdreset()
{
	register struct Display *dp;
	void (*reply)(), (*bell)();

	dp = convt.vt_Disp;
	reply = convt.vt_Reply;
	bell = convt.vt_Bell;
	cqhead = cqcount = 0;
	conbusy = 0;
	(*dp->dp_Init)();
	vt_init(&convt, dp);
	convt.vt_Reply = reply;
	convt.vt_Bell = bell;
}

/* Ask textcon.device for its card, bring the backend up and attach.
 * Called as (0, seglist, SysBase); NULL when the display stays off. */
static APTR
cdinit(zero, seg, sysbase)
APTR zero, seg, sysbase;
{
	register struct IOStdReq *ior;
	register struct Display *dp;
	struct MsgPort *mp;
	struct ConDisplay *cd;
	APTR r;

	r = NULL;
	if ((mp = CreateMsgPort()) == NULL)
		return (NULL);
	ior = (struct IOStdReq *)CreateIORequest(mp, sizeof(*ior));
	if (ior != NULL && OpenDevice(TEXTCONNAME, CONU_STANDARD, ior, 0) == 0) {
		ior->io_Command = CD_ASKDISPLAY;
		ior->io_Data = (APTR)&cd;
		ior->io_Length = sizeof(cd);
		if (DoIO(ior) == 0 && (dp = vidsetup(cd)) != NULL &&
		    (conq = (UBYTE *)AllocMem(CONQ, MEMF_PUBLIC)) != NULL) {
			(*dp->dp_Init)();
			vt_init(&convt, dp);
			convt.vt_Reply = cd->cd_Reply;
			convt.vt_Bell = cd->cd_Bell;
			cd->cd_Disp = dp;
			cd->cd_Out = cdout;
			cd->cd_Reset = cdreset;
			ior->io_Command = CD_SETDISPLAY;
			ior->io_Length = 1;
			DoIO(ior);
			KPutStr("console: ");
			KPutStr(dp->dp_Name);
			KPutStr("\n");
			r = (APTR)cd;
		}
		CloseDevice(ior);
	}
	DeleteIORequest(ior);
	DeleteMsgPort(mp);
	if (r == NULL) {
		KPutStr(vidname);
		KPutStr(": no display\n");
	}
	return (r);
}

struct Resident cdtag = {
	RTC_MATCHWORD, &cdtag, (APTR)(&cdtag + 1), RTF_COLDSTART, 1,
	NT_UNKNOWN, 0, vidname, NULL, (APTR)cdinit
};
