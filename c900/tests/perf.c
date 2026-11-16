/* SPDX-License-Identifier: MIT */
/*
 * Kernel and driver hot paths, one phase at a time between marks, for
 * tools/perf.py: Forbid/Permit, Disable/Enable, Signal, a Signal/Wait and
 * a PutMsg/ReplyMsg round trip with another Task, AllocMem/FreeMem,
 * AllocPooled/FreePooled, uncontended semaphores, DoIO, disk reads, idle ticks, the terminal
 * emulator on a null display and, with a display card, textcon.device
 * writes and scrolls.  perfmark(n)
 * writes I/O port 0x0F00 + 2n, which nothing decodes; the runner times
 * the run between marks.  The test itself checks only that each phase
 * completes.
 */
#include "test.h"
#include "wdtest.h"
#include <devices/timer.h>
#include <devices/textcon.h>
#include "vt.h"
#include "condisp.h"

#define N		64
#define STACKSIZE	1024
#define NLINE		60

static struct Task *root, *peer;
static struct MsgPort *peerport;
static ULONG pingmask, pongmask;
static int peermode;
static char line[] =
    "perf: the quick brown fox jumps over the lazy dog 0123456789 ABCDEFGHIJ\n";

void
perfmark(n)
int n;
{
	outb(0x0F00 + 2 * n, 0);
}

/* Mode 1: answer each ping with a pong.  Mode 2: reply each message. */
static void
peertask()
{
	register struct Message *m;
	register int i;

	if ((peerport = CreateMsgPort()) == NULL) {
		Signal(root, pongmask);
		Wait(0L);
	}
	Signal(root, pongmask);
	for (;;) {
		if (peermode == 1) {
			for (i = 0; i < N; i++) {
				Wait(pingmask);
				Signal(root, pongmask);
			}
			peermode = 0;
		} else if (peermode == 2) {
			for (i = 0; i < N; i++) {
				WaitPort(peerport);
				m = GetMsg(peerport);
				ReplyMsg(m);
			}
			peermode = 0;
		} else
			Wait(pingmask);
	}
}

static void nulldraw() { }
static struct Display nulldisp = {
	"null", 80, 25, NULL, nulldraw, nulldraw, nulldraw, nulldraw,
	nulldraw
};

static char *
kernel()
{
	struct MsgPort *port;
	struct Message msg[N];
	struct timerequest *clk;
	struct VT vt;
	APTR p[N], pool, hold;
	register int i;
	int bit;

	root = FindTask(NULL);
	if ((bit = AllocSignal(-1)) < 0)
		return ("AllocSignal");
	pingmask = 1L << bit;
	if ((bit = AllocSignal(-1)) < 0)
		return ("AllocSignal");
	pongmask = 1L << bit;
	if ((port = CreateMsgPort()) == NULL)
		return ("CreateMsgPort");
	for (i = 0; i < N; i++) {
		msg[i].mn_ReplyPort = port;
		msg[i].mn_Length = sizeof(struct Message);
	}
	/* Higher priority than the root: every Signal and PutMsg switches. */
	peer = CreateTask("perfpeer", 1, peertask, STACKSIZE);
	if (peer == NULL)
		return ("CreateTask");
	Wait(pongmask);
	if (peerport == NULL)
		return ("peer port");

	perfmark(1);
	for (i = 0; i < N; i++) {
		Forbid();
		Permit();
	}
	perfmark(2);
	for (i = 0; i < N; i++) {
		Disable();
		Enable();
	}
	perfmark(3);
	for (i = 0; i < N; i++) {
		Signal(root, pingmask);
		SetSignal(0L, pingmask);
	}
	perfmark(4);
	peermode = 1;
	Signal(peer, pingmask);
	for (i = 0; i < N; i++) {
		Signal(peer, pingmask);
		Wait(pongmask);
	}
	perfmark(5);
	for (i = 0; i < N; i++) {
		PutMsg(port, &msg[i]);
		if (GetMsg(port) != &msg[i])
			return ("GetMsg");
	}
	perfmark(6);
	peermode = 2;
	Signal(peer, pingmask);
	for (i = 0; i < N; i++) {
		PutMsg(peerport, &msg[i]);
		WaitPort(port);
		if (GetMsg(port) != &msg[i])
			return ("reply");
	}
	perfmark(7);
	for (i = 0; i < N; i++)
		p[i] = AllocMem(32, MEMF_PUBLIC);
	for (i = N - 1; i >= 0; i--)
		FreeMem(p[i], 32);
	perfmark(8);
	for (i = 0; i < N; i++) {
		p[0] = AllocMem(200, MEMF_PUBLIC | MEMF_CLEAR);
		FreeMem(p[0], 200);
	}
	perfmark(14);
	/* One block held keeps the puddle across the phases. */
	if ((pool = CreatePool(MEMF_PUBLIC, 4096L, 1024L)) == NULL ||
	    (hold = AllocPooled(pool, 32)) == NULL)
		return ("CreatePool");
	perfmark(15);
	for (i = 0; i < N; i++)
		p[i] = AllocPooled(pool, 32);
	perfmark(16);
	for (i = N - 1; i >= 0; i--)
		FreePooled(pool, p[i], 32);
	perfmark(17);
	FreePooled(pool, hold, 32);
	DeletePool(pool);
	perfmark(9);
	clk = (struct timerequest *)CreateIORequest(port,
	    sizeof(struct timerequest));
	if (clk == NULL || OpenDevice(TIMERNAME, UNIT_VBLANK, clk, 0) != 0)
		return ("timer.device");
	perfmark(10);
	for (i = 0; i < N; i++) {
		clk->tr_node.io_Command = TR_GETSYSTIME;
		DoIO(clk);
	}
	perfmark(11);
	Sleep(10);
	perfmark(12);
	vt_init(&vt, &nulldisp);
	for (i = 0; i < 20; i++)
		vt_write(&vt, (UBYTE *)line, -1L);
	perfmark(13);
	CloseDevice(clk);
	DeleteIORequest(clk);
	return (NULL);
}

static char *
sems()
{
	struct SignalSemaphore ss;
	register int i;

	InitSemaphore(&ss);
	perfmark(40);
	for (i = 0; i < N; i++) {
		ObtainSemaphore(&ss);
		ReleaseSemaphore(&ss);
	}
	perfmark(41);
	ObtainSemaphore(&ss);
	for (i = 0; i < N; i++) {
		ObtainSemaphore(&ss);
		ReleaseSemaphore(&ss);
	}
	ReleaseSemaphore(&ss);
	perfmark(42);
	for (i = 0; i < N; i++) {
		ObtainSemaphoreShared(&ss);
		ReleaseSemaphore(&ss);
	}
	perfmark(43);
	for (i = 0; i < N; i++) {
		if (!AttemptSemaphore(&ss))
			return ("AttemptSemaphore");
		ReleaseSemaphore(&ss);
	}
	perfmark(44);
	for (i = 0; i < N; i++) {
		if (!AttemptSemaphoreShared(&ss))
			return ("AttemptSemaphoreShared");
		ReleaseSemaphore(&ss);
	}
	perfmark(45);
	if (ss.ss_QueueCount != -1 || ss.ss_NestCount != 0)
		return ("semaphore counts");
	return (NULL);
}

static char *
disk()
{
	struct IOStdReq *r;
	UBYTE *buf;
	register int i;

	if ((buf = dmabuf(8, 0)) == NULL)
		return ("no buffer");
	if ((r = wdopen(WD_SLOT(0, 0))) == NULL)
		return ("open pattern slot");
	perfmark(20);
	for (i = 0; i < 8; i++)
		if (wdio(r, CMD_READ, 0L, 1, buf) != 0)
			return ("read");
	perfmark(21);
	for (i = 0; i < 8; i++)
		if (wdio(r, CMD_READ, 8L * i, 8, buf) != 0)
			return ("read 8");
	perfmark(22);
	wdclose(r);
	return (NULL);
}

static char *
console()
{
	struct MsgPort *port;
	struct IOStdReq *r;
	struct ConDisplay *cd;
	register int i;

	if ((cd = conask(0)) == NULL || cd->cd_Disp == NULL)
		return (NULL);
	if ((port = CreateMsgPort()) == NULL)
		return ("CreateMsgPort");
	r = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
	if (r == NULL || OpenDevice(TEXTCONNAME, CONU_STANDARD, r, 0) != 0)
		return ("textcon.device");
	r->io_Command = CMD_WRITE;
	r->io_Data = (UBYTE *)"\033[H\033[J";
	r->io_Length = -1;
	DoIO(r);
	perfmark(30);
	/* A screen of lines from the top, no scrolling. */
	for (i = 0; i < cd->cd_Disp->dp_Rows - 1; i++) {
		r->io_Data = (UBYTE *)line;
		r->io_Length = -1;
		DoIO(r);
	}
	perfmark(31);
	/* Every further line scrolls the whole screen. */
	for (i = 0; i < 8; i++) {
		r->io_Data = (UBYTE *)line;
		r->io_Length = -1;
		DoIO(r);
	}
	perfmark(32);
	CloseDevice(r);
	DeleteIORequest(r);
	DeleteMsgPort(port);
	return (NULL);
}

int
main()
{
	char *err;

	if ((err = kernel()) == NULL && (err = sems()) == NULL &&
	    (err = disk()) == NULL)
		err = console();
	perfmark(99);
	if (err != NULL) {
		xKPrintf("FAIL perf: %s\n", err);
		return (1);
	}
	xKPrintf("PASS perf\n");
	return (0);
}
