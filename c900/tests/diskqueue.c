/* SPDX-License-Identifier: MIT */
/*
 * wd.device queueing: requests submitted with VI off queue behind the
 * issued one and complete in order; AbortIO ends a queued request at once
 * with IOERR_ABORTED and leaves its buffer alone, and has no effect on the
 * issued request or a finished one.  Two Tasks then keep requests of both
 * queued together (each waits with VI off, so the other runs and submits
 * behind it) on the pattern and scratch partitions, and every reply carries
 * the right data to the right Task.
 */
#include "test.h"
#include "wdtest.h"

#define STACKSIZE	1024
#define DONE_BIT	16
#define DONE_MASK	(1L << DONE_BIT)
#define ROUNDS		20

static struct Task *parent;
static char *taskerr[2];
static int ndone;

/* Two reads queued together, waited for with VI off. */
static char *
pair(a, b, blk, bufa, bufb)
struct IOStdReq *a, *b;
ULONG blk;
UBYTE *bufa, *bufb;
{
	wdset(a, CMD_READ, blk, 2, bufa);
	wdset(b, CMD_READ, blk + 2, 1, bufb);
	Disable();
	SendIO(a);
	SendIO(b);
	WaitIO(a);
	WaitIO(b);
	Enable();
	if (a->io_Error != 0 || b->io_Error != 0)
		return ("pair error");
	return (NULL);
}

static void
reader()
{
	struct IOStdReq *a, *b;
	UBYTE *bufa, *bufb;
	ULONG blk;
	char *why;
	int i;

	why = NULL;
	a = wdopen(WD_SLOT(0, 0));
	b = wdopen(WD_SLOT(0, 0));
	bufa = dmabuf(2, 0);
	bufb = dmabuf(1, 1);
	if (a == NULL || b == NULL || bufa == NULL || bufb == NULL)
		why = "reader setup";
	for (i = 0; why == NULL && i < ROUNDS; i++) {
		blk = (ULONG)(i * 3) % (PAT_BLKS - 3);
		if ((why = pair(a, b, blk, bufa, bufb)) == NULL &&
		    (patcheck(bufa, blk, 2) >= 0 || patcheck(bufb, blk + 2, 1) >= 0))
			why = "reader data";
	}
	Forbid();
	taskerr[0] = why;
	ndone++;
	Signal(parent, DONE_MASK);
}

static void
writer()
{
	struct IOStdReq *a, *b;
	UBYTE *bufa, *bufb;
	ULONG blk;
	char *why;
	int i;

	why = NULL;
	a = wdopen(WD_SLOT(0, 1));
	b = wdopen(WD_SLOT(0, 1));
	bufa = dmabuf(2, 0);
	bufb = dmabuf(1, 3);
	if (a == NULL || b == NULL || bufa == NULL || bufb == NULL)
		why = "writer setup";
	for (i = 0; why == NULL && i < ROUNDS; i++) {
		blk = 40 + (ULONG)(i % 4) * 4;
		seedfill(bufa, (UWORD)i, 2);
		seedfill(bufb, (UWORD)i + 100, 1);
		wdset(a, CMD_WRITE, blk, 2, bufa);
		wdset(b, CMD_WRITE, blk + 2, 1, bufb);
		Disable();
		SendIO(a);
		SendIO(b);
		WaitIO(a);
		WaitIO(b);
		Enable();
		if (a->io_Error != 0 || b->io_Error != 0) {
			why = "writer write";
			break;
		}
		seedfill(bufa, 0, 2);
		seedfill(bufb, 0, 1);
		if ((why = pair(a, b, blk, bufa, bufb)) == NULL &&
		    (!seedcheck(bufa, (UWORD)i, 2) ||
		    !seedcheck(bufb, (UWORD)i + 100, 1)))
			why = "writer data";
	}
	Forbid();
	taskerr[1] = why;
	ndone++;
	Signal(parent, DONE_MASK);
}

static char *
run()
{
	struct IOStdReq *r[4];
	UBYTE *buf[4];
	int i;

	parent = FindTask(NULL);
	SetTaskPri(parent, 0);
	for (i = 0; i < 4; i++) {
		if ((r[i] = wdopen(WD_SLOT(0, 0))) == NULL ||
		    (buf[i] = dmabuf(2, 0)) == NULL)
			return ("setup");
		buf[i][0] = 0xee;
		wdset(r[i], CMD_READ, (ULONG)i * 2, 2, buf[i]);
	}

	Disable();
	for (i = 0; i < 4; i++)
		SendIO(r[i]);
	if (CheckIO(r[1]) != NULL || CheckIO(r[2]) != NULL)
		return ("queued request not pending");
	AbortIO(r[2]);
	AbortIO(r[0]);
	if (CheckIO(r[2]) == NULL)
		return ("aborted request still pending");
	if (CheckIO(r[1]) != NULL)
		return ("abort ended another request");
	Enable();
	for (i = 0; i < 4; i++)
		WaitIO(r[i]);
	if (r[2]->io_Error != IOERR_ABORTED || r[2]->io_Actual != 0 ||
	    buf[2][0] != 0xee)
		return ("aborted request");
	for (i = 0; i < 4; i++)
		if (i != 2 && (r[i]->io_Error != 0 ||
		    patcheck(buf[i], (ULONG)i * 2, 2) >= 0))
			return ("queued request result");
	AbortIO(r[1]);
	if (r[1]->io_Error != 0 || CheckIO(r[1]) == NULL)
		return ("abort of a finished request");
	for (i = 0; i < 4; i++)
		wdclose(r[i]);

	if (CreateTask("wdreader", 0, reader, STACKSIZE) == NULL ||
	    CreateTask("wdwriter", 0, writer, STACKSIZE) == NULL)
		return ("CreateTask");
	while (ndone < 2)
		Wait(DONE_MASK);
	if (taskerr[0] != NULL)
		return (taskerr[0]);
	if (taskerr[1] != NULL)
		return (taskerr[1]);
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL)
		xKPrintf("FAIL diskqueue: %s\n", why);
	else
		KPutStr("PASS diskqueue\n");
	return (0);
}
