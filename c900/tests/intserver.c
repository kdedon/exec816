/* SPDX-License-Identifier: MIT */
/*
 * Interrupt servers and handlers: a server on the tick vector runs once
 * per tick until removed, servers run in priority order and a claiming
 * server ends the chain, a server's Signal switches to the woken Task on
 * the same interrupt's exit, and a handler set with SetIntVector replaces
 * the chain until cleared.
 */
#include "test.h"
#include <exec/exec.h>

#define STACKSIZE	1024
#define WAKE_BIT	16
#define WAKE_MASK	(1L << WAKE_BIT)

static volatile UWORD ncount, nhi, nclaim, nlo, nhandler;
static volatile int seq[3], nseq;
static struct Task *waker;
static volatile ULONG wakeat, ranat;
static volatile int woke;

static int
countsrv(data, n)
APTR data;
UWORD n;
{
	ncount++;
	return (0);
}

static int
hisrv(data, n)
APTR data;
UWORD n;
{
	nhi++;
	if (nseq < 3)
		seq[nseq++] = 1;
	return (0);
}

static int
claimsrv(data, n)
APTR data;
UWORD n;
{
	nclaim++;
	if (nseq < 3)
		seq[nseq++] = 2;
	return (1);
}

static int
losrv(data, n)
APTR data;
UWORD n;
{
	nlo++;
	return (0);
}

/* Wake the high-priority Task on the tick after wakeat is armed. */
static int
wakesrv(data, n)
APTR data;
UWORD n;
{
	if (wakeat == 0 || woke)
		return (0);
	wakeat = TickCount;
	woke = 1;
	Signal((struct Task *)data, WAKE_MASK);
	return (0);
}

/* A handler owns the vector: it must acknowledge the CIO itself. */
static int
tickhandler(data, n)
APTR data;
UWORD n;
{
	outb(0x0019, 0x24);
	nhandler++;
	return (0);
}

static void
wakee()
{
	AllocSignal(WAKE_BIT);
	Wait(WAKE_MASK);
	ranat = TickCount;
}

static struct Interrupt countint, hiint, claimint, loint, wakeint, hint;

static void
mkint(is, pri, code, data)
struct Interrupt *is;
int pri;
int (*code)();
APTR data;
{
	is->is_Node.ln_Pri = pri;
	is->is_Node.ln_Name = "test";
	is->is_Code = code;
	is->is_Data = data;
}

static char *
run()
{
	ULONG t0, dt;
	UWORD c;
	long spin;
	struct Interrupt *old;

	SetTaskPri(FindTask(NULL), 0);
	xKPrintf("stray before: %lu total, %lu on disk vector\n",
	    IntStray(INTV_ALL), IntStray(INTV_DISK));
	if (IntStray(INTV_TICK) != 0)
		return ("stray ticks");

	/* One call per tick while installed, none after removal. */
	mkint(&countint, 0, countsrv, NULL);
	Sleep(1);
	Disable();
	t0 = TickCount;
	AddIntServer(INTV_TICK, &countint);
	Enable();
	Sleep(20);
	Disable();
	RemIntServer(INTV_TICK, &countint);
	dt = TickCount - t0;
	c = ncount;
	Enable();
	if (c != dt || c < 20)
		return ("server calls do not match ticks");
	Sleep(5);
	if (ncount != c)
		return ("server ran after removal");

	/* Priority order; a claiming server ends the chain. */
	mkint(&loint, -10, losrv, NULL);
	mkint(&claimint, -5, claimsrv, NULL);
	mkint(&hiint, 10, hisrv, NULL);
	Forbid();
	AddIntServer(INTV_TICK, &loint);
	AddIntServer(INTV_TICK, &claimint);
	AddIntServer(INTV_TICK, &hiint);
	Permit();
	Sleep(5);
	Forbid();
	RemIntServer(INTV_TICK, &hiint);
	RemIntServer(INTV_TICK, &claimint);
	RemIntServer(INTV_TICK, &loint);
	Permit();
	if (nhi < 5 || nclaim != nhi || nlo != 0)
		return ("chain order or claim");
	if (seq[0] != 1 || seq[1] != 2 || seq[2] != 1)
		return ("server sequence");

	/* A server's Signal readies a higher-priority Task; the same
	 * interrupt's exit switches to it while this Task spins. */
	waker = CreateTask("wakee", 5, wakee, STACKSIZE);
	if (waker == NULL)
		return ("CreateTask");
	mkint(&wakeint, 0, wakesrv, (APTR)waker);
	AddIntServer(INTV_TICK, &wakeint);
	wakeat = 1;
	for (spin = 0; ranat == 0 && spin < 5000000L; spin++)
		;
	RemIntServer(INTV_TICK, &wakeint);
	if (ranat == 0)
		return ("woken Task never ran");
	if (ranat != wakeat)
		return ("woken Task ran a tick late");

	/* A handler replaces the chain: no ticks are counted meanwhile. */
	mkint(&hint, 0, tickhandler, NULL);
	t0 = TickCount;
	old = SetIntVector(INTV_TICK, &hint);
	if (old != NULL)
		return ("SetIntVector: old handler");
	for (spin = 0; nhandler < 5 && spin < 5000000L; spin++)
		;
	old = SetIntVector(INTV_TICK, NULL);
	if (old != &hint)
		return ("SetIntVector: returned handler");
	if (nhandler < 5)
		return ("handler did not run");
	if (TickCount != t0)
		return ("chain ran under a handler");
	Sleep(2);
	if (TickCount == t0)
		return ("chain did not resume");
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL intserver: %s\n", why);
		return (1);
	}
	KPutStr("PASS intserver\n");
	return (0);
}
