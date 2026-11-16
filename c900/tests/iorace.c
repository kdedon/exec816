/* SPDX-License-Identifier: MIT */
/*
 * Concurrent device I/O without Exec's lock.  Three Tasks of equal
 * priority, preempted by the tick, submit and abort requests at random
 * on shared opens of timer.device, serial unit 0 (writes long enough to
 * finish from the Tx interrupt, reads that wait, CMD_FLUSH of every
 * Task's requests) and parallel.device with a short timeout, so the tick
 * server, the SCC interrupt and the parallel driver Task complete them.
 * Every SendIO must be replied exactly once, with IOERR_ABORTED when
 * AbortIO said it aborted, and nothing may arrive afterwards.
 */
#include "test.h"
#include <exec/exec.h>
#include <devices/timer.h>
#include <devices/serial.h>
#include <devices/parallel.h>

#define STACKSIZE	2048
#define DONE_BIT	16
#define DONE_MASK	(1L << DONE_BIT)
#define NTASK		3
#define NTIMER		4	/* requests 0-3: timer.device */
#define NSER		6	/* 4-5: serial.device; 6-7: parallel.device */
#define NREQ		8
#define RUNTICKS	500
#define DRAINTICKS	300

struct Hammer {
	ULONG h_Seed;
	struct MsgPort *h_Port;
	struct IORequest *h_Req[NREQ];
	UBYTE h_Out[NREQ];
	UBYTE h_Abort[NREQ];
	UBYTE h_Buf[NREQ][4];
	ULONG h_Sent;
	ULONG h_Got;
	ULONG h_Aborted;
	char *h_Why;
};

static struct Task *parent;
static struct Hammer ham[NTASK];
static volatile int done;
static char dashes[] = "----------------------------------------";

static int
rnd(h)
register struct Hammer *h;
{
	h->h_Seed = h->h_Seed * 1103515245L + 12345L;
	return ((int)(h->h_Seed >> 16) & 0x7fff);
}

/* Take every reply on the port; NULL or why it is wrong. */
static char *
collect(h)
register struct Hammer *h;
{
	register struct Message *m;
	register int i;

	while ((m = GetMsg(h->h_Port)) != NULL) {
		for (i = 0; i < NREQ; i++)
			if ((struct Message *)h->h_Req[i] == m)
				break;
		if (i == NREQ)
			return ("unknown message");
		if (!h->h_Out[i])
			return ("request replied twice");
		if (m->mn_Node.ln_Type != NT_REPLYMSG)
			return ("reply not NT_REPLYMSG");
		if (h->h_Abort[i] && h->h_Req[i]->io_Error != IOERR_ABORTED)
			return ("aborted request without IOERR_ABORTED");
		h->h_Out[i] = 0;
		h->h_Abort[i] = 0;
		h->h_Got++;
	}
	return (NULL);
}

static void
submit(h, i)
register struct Hammer *h;
int i;
{
	register struct IORequest *ior;
	register struct IOStdReq *r;
	int k;

	ior = h->h_Req[i];
	r = (struct IOStdReq *)ior;
	if (i < NTIMER) {
		ior->io_Command = TR_ADDREQUEST;
		((struct timerequest *)ior)->tr_time.tv_secs = 0;
		((struct timerequest *)ior)->tr_time.tv_micro =
		    (ULONG)(rnd(h) & 1) * 10000L;
	} else if (i >= NSER) {
		ior->io_Command = CMD_WRITE;
		r->io_Data = (UBYTE *)dashes;
		r->io_Length = 1 + (rnd(h) & 1);
	} else {
		k = rnd(h) & 15;
		if (k == 0)
			ior->io_Command = CMD_FLUSH;
		else if (k < 6) {
			ior->io_Command = CMD_READ;
			r->io_Data = h->h_Buf[i];
			r->io_Length = 4;
		} else {
			ior->io_Command = CMD_WRITE;
			r->io_Data = (UBYTE *)dashes;
			r->io_Length = 33 + (rnd(h) & 7);
		}
	}
	h->h_Out[i] = 1;
	h->h_Sent++;
	SendIO(ior);
}

static char *
hammer(h)
register struct Hammer *h;
{
	register int i;
	struct IORequest *ior;
	ULONG size, t0;
	char *why;

	if ((h->h_Port = CreateMsgPort()) == NULL)
		return ("CreateMsgPort");
	for (i = 0; i < NREQ; i++) {
		size = i < NTIMER ? sizeof(struct timerequest) :
		    i >= NSER ? sizeof(struct IOExtPar) :
		    sizeof(struct IOExtSer);
		if ((ior = CreateIORequest(h->h_Port, size)) == NULL)
			return ("CreateIORequest");
		h->h_Req[i] = ior;
		if (i < NTIMER) {
			if (OpenDevice(TIMERNAME, UNIT_VBLANK, ior, 0) != 0)
				return ("OpenDevice timer");
		} else if (i >= NSER) {
			((struct IOExtPar *)ior)->io_ParFlags = PARF_SHARED;
			if (OpenDevice(PARALLELNAME, 0, ior, 0) != 0)
				return ("OpenDevice parallel");
		} else {
			((struct IOExtSer *)ior)->io_SerFlags = SERF_SHARED;
			if (OpenDevice(SERIALNAME, 0, ior, 0) != 0)
				return ("OpenDevice serial");
		}
	}

	t0 = TickCount;
	while (TickCount - t0 < RUNTICKS) {
		i = rnd(h) % NREQ;
		if (!h->h_Out[i])
			submit(h, i);
		else if ((i < NTIMER || (rnd(h) & 1) == 0) &&
		    AbortIO(h->h_Req[i]) == 0) {
			h->h_Abort[i] = 1;
			h->h_Aborted++;
		}
		if ((why = collect(h)) != NULL)
			return (why);
		if ((rnd(h) & 63) == 0)
			Sleep(1);
	}

	/* Abort what is left; every request must come back once. */
	for (i = 0; i < NREQ; i++)
		if (h->h_Out[i] && AbortIO(h->h_Req[i]) == 0)
			h->h_Abort[i] = 1;
	t0 = TickCount;
	for (;;) {
		if ((why = collect(h)) != NULL)
			return (why);
		for (i = 0; i < NREQ && !h->h_Out[i]; i++)
			;
		if (i == NREQ)
			break;
		if (TickCount - t0 > DRAINTICKS)
			return ("request lost");
		Sleep(1);
	}
	Sleep(5);
	if ((why = collect(h)) != NULL)
		return (why);
	if (h->h_Sent != h->h_Got)
		return ("replies do not match submissions");
	for (i = 0; i < NREQ; i++) {
		CloseDevice(h->h_Req[i]);
		DeleteIORequest(h->h_Req[i]);
	}
	DeleteMsgPort(h->h_Port);
	return (NULL);
}

static void
worker(h)
struct Hammer *h;
{
	h->h_Why = hammer(h);
	Forbid();
	done++;
	Signal(parent, DONE_MASK);
}

static void
work0()
{
	worker(&ham[0]);
}

static void
work1()
{
	worker(&ham[1]);
}

static void
work2()
{
	worker(&ham[2]);
}

/* A byte the absent printer never takes times out in 3 ticks; the
 * setting outlasts the open. */
static char *
partimeout()
{
	struct MsgPort *port;
	struct IOExtPar *x;
	int err;

	if ((port = CreateMsgPort()) == NULL)
		return ("CreateMsgPort");
	x = (struct IOExtPar *)CreateIORequest(port, sizeof(struct IOExtPar));
	if (x == NULL)
		return ("CreateIORequest");
	if (OpenDevice(PARALLELNAME, 0, x, 0) != 0)
		return ("OpenDevice parallel");
	x->IOPar.io_Command = PDCMD_SETPARAMS;
	x->io_Timeout = 20000L;
	err = DoIO(x);
	CloseDevice(x);
	DeleteIORequest(x);
	DeleteMsgPort(port);
	return (err != 0 ? "PDCMD_SETPARAMS" : NULL);
}

static char *
run()
{
	char *why;
	int i;

	parent = FindTask(NULL);
	if (AllocSignal(DONE_BIT) != DONE_BIT)
		return ("AllocSignal");
	if ((why = partimeout()) != NULL)
		return (why);
	for (i = 0; i < NTASK; i++)
		ham[i].h_Seed = 12345L + 7919L * i;
	if (CreateTask("iorace0", 0, work0, STACKSIZE) == NULL ||
	    CreateTask("iorace1", 0, work1, STACKSIZE) == NULL ||
	    CreateTask("iorace2", 0, work2, STACKSIZE) == NULL)
		return ("CreateTask");
	while (done != NTASK)
		Wait(DONE_MASK);
	FreeSignal(DONE_BIT);
	for (i = 0; i < NTASK; i++)
		if (ham[i].h_Why != NULL)
			return (ham[i].h_Why);
	for (i = 0; i < NTASK; i++) {
		xKPrintf("\niorace%d: %ld sent, %ld aborted", i,
		    ham[i].h_Sent, ham[i].h_Aborted);
		if (ham[i].h_Sent < 50 || ham[i].h_Aborted == 0)
			return ("too little traffic");
	}
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("\nFAIL iorace: %s\n", why);
		return (1);
	}
	KPutStr("\nPASS iorace\n");
	return (0);
}
