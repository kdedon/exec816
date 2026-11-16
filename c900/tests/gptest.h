/* SPDX-License-Identifier: MIT */
/*
 * Shared GetGPButtons test code.  gpholder() starts a Task in the place
 * of input.device: it opens gameport.device unit 0, sets the mouse type
 * and keeps a GPD_READEVENT pending, counting the events, until
 * gprelease().  gpwait() polls GetGPButtons for a state.
 */
#include <exec/exec.h>
#include <devices/gameport.h>

#define GPDONE_BIT	16
#define GPDONE		(1L << GPDONE_BIT)
#define GPSTACK		2000

struct Device *GamePortBase;

static struct Task *gpparent, *gptask;
static volatile int gpstate;		/* 1 holding, 2 stopped, -1 failed */
static volatile int gpstop;		/* the holder's stop signal bit */
static volatile ULONG gpevents;

static void
gphold()
{
	struct MsgPort *port;
	struct IOStdReq *r;
	struct InputEvent ev;
	ULONG got;
	BYTE ct;

	port = CreateMsgPort();
	r = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
	gpstop = AllocSignal(-1);
	if (r == NULL || gpstop < 0 || OpenDevice(GAMEPORTNAME, 0, r, 0) != 0) {
		gpstate = -1;
		Signal(gpparent, GPDONE);
		return;
	}
	ct = GPCT_MOUSE;
	r->io_Command = GPD_SETCTYPE;
	r->io_Data = (UBYTE *)&ct;
	r->io_Length = 1;
	DoIO(r);
	gpstate = 1;
	Signal(gpparent, GPDONE);
	for (;;) {
		r->io_Command = GPD_READEVENT;
		r->io_Data = (UBYTE *)&ev;
		r->io_Length = sizeof ev;
		SendIO(r);
		got = Wait((1L << port->mp_SigBit) | (1L << gpstop));
		if (CheckIO(r) == NULL) {
			AbortIO(r);
			WaitIO(r);
			break;
		}
		WaitIO(r);
		gpevents++;
		if (got & (1L << gpstop))
			break;
	}
	CloseDevice(r);
	DeleteIORequest(r);
	DeleteMsgPort(port);
	gpstate = 2;
	Signal(gpparent, GPDONE);
}

/* Start the holder; NULL once it holds the unit. */
static char *
gpholder()
{
	gpparent = FindTask(NULL);
	if (AllocSignal(GPDONE_BIT) != GPDONE_BIT)
		return ("AllocSignal");
	gptask = CreateTask("gpholder", 0, gphold, GPSTACK);
	if (gptask == NULL)
		return ("CreateTask");
	while (gpstate == 0)
		Wait(GPDONE);
	return (gpstate == 1 ? NULL : "holder did not open the unit");
}

static char *
gprelease()
{
	Signal(gptask, 1L << gpstop);
	while (gpstate == 1)
		Wait(GPDONE);
	return (gpstate == 2 ? NULL : "holder");
}

/* Poll until GetGPButtons() is want, up to ticks ticks. */
static char *
gpwait(name, want, ticks)
char *name;
ULONG want;
int ticks;
{
	ULONG b;

	while ((b = GetGPButtons()) != want) {
		if (ticks-- == 0) {
			xKPrintf("%s: buttons %lx, want %lx\n", name, b, want);
			return ("GetGPButtons");
		}
		Sleep(1);
	}
	xKPrintf("%s: buttons %lx\n", name, b);
	return (NULL);
}
