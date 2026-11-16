/* SPDX-License-Identifier: MIT */
/*
 * Signals and Wait (port of examples/signals.act), plus the allocation and
 * SetSignal rules from docs/reference/signals.md.
 */
#include <exec/exec.h>

#define NWORKERS	6
#define STACKSIZE	1024
#define READY_BIT	16
#define DONE_BIT	17
#define READY_MASK	(1L << READY_BIT)
#define DONE_MASK	(1L << DONE_BIT)

static struct Task *parent;
static struct Task *workers[NWORKERS];
static volatile int ready, finished, badbit;
static int admitted;
static char wname[] = "signal worker";

static void
worker()
{
	int bit;

	bit = AllocSignal(READY_BIT);
	if (bit != READY_BIT)
		badbit++;
	Forbid();
	ready++;
	Signal(parent, READY_MASK);
	Permit();

	Wait(READY_MASK);
	FreeSignal(bit);

	Forbid();
	finished++;
	Signal(parent, DONE_MASK);
	RemTask(NULL);
}

static char *
rules()
{
	struct MsgPort *mp;
	int a, b;
	ULONG m;

	if (AllocSignal(5) != -1)
		return ("reserved bit allocated");
	if (AllocSignal(32) != -1)
		return ("bit 32 allocated");
	a = AllocSignal(-1);
	if (a != 31)
		return ("AllocSignal(-1) not highest free");
	b = AllocSignal(-1);
	if (b != 30)
		return ("second AllocSignal(-1)");
	if (AllocSignal(31) != -1)
		return ("allocated bit reallocated");

	/* SetSignal replaces selected bits and returns the old mask. */
	SetSignal(0L, (1L << a) | (1L << b));
	SetSignal(1L << a, 1L << a);
	m = SetSignal(0L, 0L);
	if ((m & (1L << a)) == 0 || (m & (1L << b)) != 0)
		return ("SetSignal");

	/* Wait returns only matching bits and leaves the others pending. */
	SetSignal(1L << b, 1L << b);
	m = Wait(1L << a);
	if (m != (1L << a))
		return ("Wait result");
	m = SetSignal(0L, 0L);
	if ((m & (1L << a)) != 0 || (m & (1L << b)) == 0)
		return ("Wait consumed unrelated bits");
	m = Wait((1L << a) | (1L << b));
	if (m != (1L << b))
		return ("second Wait result");

	/* Allocation clears stale delivery. */
	FreeSignal(a);
	Signal(FindTask(NULL), 1L << a);
	if (AllocSignal(a) != a)
		return ("reallocate");
	if (SetSignal(0L, 0L) & (1L << a))
		return ("allocation kept stale bit");
	FreeSignal(a);
	FreeSignal(b);
	FreeSignal(-1);

	/* As on Amiga Exec, a port whose bit was given away (mp_SigBit -1,
	 * as amiga.lib's TimeDelay does) deletes cleanly, and FreeSignal
	 * ignores the low byte 0xFF and an unallocated bit. */
	if ((mp = CreateMsgPort()) == NULL)
		return ("CreateMsgPort");
	a = mp->mp_SigBit;
	FreeSignal(a);
	mp->mp_SigBit = -1;
	DeleteMsgPort(mp);
	FreeSignal(255);
	FreeSignal(a);
	if (AllocSignal(a) != a)
		return ("bit after DeleteMsgPort");
	FreeSignal(a);
	return (NULL);
}

static char *
run()
{
	struct Task *t;
	int i, rb, db;
	char *why;

	parent = FindTask(NULL);
	SetTaskPri(parent, 0);
	if ((why = rules()) != NULL)
		return (why);

	rb = AllocSignal(READY_BIT);
	db = AllocSignal(DONE_BIT);
	if (rb != READY_BIT || db != DONE_BIT)
		return ("parent AllocSignal");

	Forbid();
	for (i = 0; i < NWORKERS; i++) {
		t = CreateTask(wname, 0, worker, STACKSIZE);
		if (t == NULL)
			break;
		workers[i] = t;
		admitted++;
	}
	Permit();

	while (ready != admitted)
		Wait(READY_MASK);
	for (i = 0; i < admitted; i++)
		Signal(workers[i], READY_MASK);
	while (finished != admitted)
		Wait(DONE_MASK);

	FreeSignal(rb);
	FreeSignal(db);
	if (admitted != NWORKERS)
		return ("CreateTask failed");
	if (badbit != 0)
		return ("worker AllocSignal");
	if (FindTask(wname) != NULL)
		return ("worker still live");
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL) {
		xKPrintf("FAIL signals: %s\n", why);
		return (1);
	}
	KPutStr("PASS signals\n");
	return (0);
}
