/* SPDX-License-Identifier: MIT */
/*
 * Signal semaphores: record layout, nesting, Attempt, priority contention,
 * shared holders and handoff order, the public list, Procure/Vacate,
 * ObtainSemaphoreList and the AN_SemCorrupt release checks.
 */
#include <exec/exec.h>

#define STACKSIZE	1024
#define GO_BIT		20
#define DONE_BIT	21
#define GO_MASK		(1L << GO_BIT)
#define DONE_MASK	(1L << DONE_BIT)

static struct Task *root;
static struct SignalSemaphore sem, sem2, sem3;
static char log[32];
static int nlog;
static int badown;
static int ndone;

static char tname[] = "sem worker";
static char semname[] = "test.sem";
static char lowname[] = "low.sem";

static void
note(c)
int c;
{
	log[nlog++] = c;
	log[nlog] = 0;
}

static int
same(s)
register char *s;
{
	register char *l;

	for (l = log; *l == *s; l++, s++)
		if (*l == 0)
			return (1);
	return (0);
}

static void
reset()
{
	nlog = 0;
	log[0] = 0;
}

static struct Task *
spawn(pri, entry)
int pri;
void (*entry)();
{
	return (CreateTask(tname, pri, entry, STACKSIZE));
}

static int
isfree(s)
register struct SignalSemaphore *s;
{
	return (s->ss_QueueCount == -1 && s->ss_NestCount == 0 &&
	    s->ss_Owner == NULL && IsMinListEmpty(&s->ss_WaitQueue));
}

/* The worker's go signal, allocated once per worker. */
static void
waitgo()
{
	AllocSignal(GO_BIT);
	Wait(GO_MASK);
}

/* Report to the root; signals merge, so ndone counts them. */
static void
tell()
{
	Forbid();
	ndone++;
	Signal(root, DONE_MASK);
	Permit();
}

static void
finish()
{
	Forbid();
	ndone++;
	Signal(root, DONE_MASK);
	RemTask(NULL);
}

static void
await(n)
int n;
{
	while (ndone < n)
		Wait(DONE_MASK);
	Forbid();
	ndone -= n;
	Permit();
}

static char *
layout()
{
	struct SignalSemaphore *s;
	struct SemaphoreMessage *m;

	s = &sem;
	m = NULL;
	if (sizeof(struct SemaphoreRequest) != 12 ||
	    sizeof(struct SignalSemaphore) != 46 ||
	    sizeof(struct SemaphoreMessage) != 24)
		return ("record sizes");
	if ((char *)&s->ss_NestCount - (char *)s != 14 ||
	    (char *)&s->ss_WaitQueue - (char *)s != 16 ||
	    (char *)&s->ss_MultipleLink - (char *)s != 28 ||
	    (char *)&s->ss_Owner - (char *)s != 40 ||
	    (char *)&s->ss_QueueCount - (char *)s != 44)
		return ("SignalSemaphore offsets");
	m = (struct SemaphoreMessage *)s;
	if ((char *)&m->ssm_Semaphore - (char *)m != 20)
		return ("SemaphoreMessage offset");
	return (NULL);
}

static char *
nesting()
{
	InitSemaphore(&sem);
	if (sem.ss_Link.ln_Type != NT_SIGNALSEM || !isfree(&sem))
		return ("InitSemaphore");
	ObtainSemaphore(&sem);
	ObtainSemaphore(&sem);
	ObtainSemaphoreShared(&sem);
	if (!AttemptSemaphore(&sem) || !AttemptSemaphoreShared(&sem))
		return ("owner Attempt");
	if (sem.ss_Owner != root || sem.ss_NestCount != 5 ||
	    sem.ss_QueueCount != 4)
		return ("nested counts");
	ReleaseSemaphore(&sem);
	ReleaseSemaphore(&sem);
	ReleaseSemaphore(&sem);
	ReleaseSemaphore(&sem);
	if (sem.ss_Owner != root || sem.ss_NestCount != 1)
		return ("partial release");
	ReleaseSemaphore(&sem);
	if (!isfree(&sem))
		return ("not free after release");

	/* Releasing a free semaphore is AN_SemCorrupt and changes nothing. */
	SysBase->LastAlert[0] = 0;
	ReleaseSemaphore(&sem);
	if (!isfree(&sem))
		return ("free release changed state");
	if (SysBase->LastAlert[0] != AN_SemCorrupt)
		return ("free release raised no alert");

	ObtainSemaphoreShared(&sem);
	if (sem.ss_Owner != NULL || sem.ss_NestCount != 1)
		return ("shared obtain");
	ReleaseSemaphore(&sem);
	if (!isfree(&sem))
		return ("shared release");
	return (NULL);
}

/* Attempt: hold sem until told to go. */
static void
holder()
{
	ObtainSemaphore(&sem);
	tell();
	waitgo();
	ReleaseSemaphore(&sem);
	finish();
}

static char *
attempts()
{
	struct Task *t;

	if ((t = spawn(1, holder)) == NULL)
		return ("CreateTask");
	await(1);
	if (sem.ss_Owner != t)
		return ("holder not owner");
	if (AttemptSemaphore(&sem) || AttemptSemaphoreShared(&sem))
		return ("Attempt on held semaphore");
	if (sem.ss_QueueCount != 0 || sem.ss_NestCount != 1)
		return ("failed Attempt changed counts");

	/* Releasing another Task's semaphore is AN_SemCorrupt. */
	SysBase->LastAlert[0] = 0;
	ReleaseSemaphore(&sem);
	if (sem.ss_Owner != t || sem.ss_NestCount != 1)
		return ("foreign release changed state");
	if (SysBase->LastAlert[0] != AN_SemCorrupt)
		return ("foreign release raised no alert");

	Signal(t, GO_MASK);
	await(1);
	if (!AttemptSemaphore(&sem) || sem.ss_Owner != root)
		return ("Attempt on free semaphore");
	ReleaseSemaphore(&sem);
	if (!isfree(&sem))
		return ("Attempt release");
	return (NULL);
}

/* Contention: high and low priority Tasks queue behind the root. */
static void
contender(c)
int c;
{
	ObtainSemaphore(&sem);
	if (sem.ss_Owner != FindTask(NULL))
		badown++;
	note(c);
	ReleaseSemaphore(&sem);
}

static void
high()
{
	contender('h');
	finish();
}

static void
low()
{
	contender('l');
	finish();
}

static char *
contention()
{
	reset();
	ObtainSemaphore(&sem);
	if (spawn(5, high) == NULL || spawn(-5, low) == NULL)
		return ("CreateTask");
	/* The high Task has queued; Sleep lets the low one queue too. */
	Sleep(2);
	if (sem.ss_QueueCount != 2 || nlog != 0)
		return ("waiters not queued");
	note('r');
	ReleaseSemaphore(&sem);
	note('R');
	await(2);
	if (!same("rhRl"))
		return ("contention order");
	if (badown)
		return ("waiter woke without ownership");
	if (!isfree(&sem))
		return ("contention left semaphore held");
	return (NULL);
}

/* Shared holders keep sem until told to go. */
static void
sharer(c)
int c;
{
	ObtainSemaphoreShared(&sem);
	if (sem.ss_Owner != NULL)
		badown++;
	note(c);
	waitgo();
	note(c - '1' + 'a');
	ReleaseSemaphore(&sem);
}

static void
s1()
{
	sharer('1');
	finish();
}

static void
s2()
{
	sharer('2');
	finish();
}

static void
s3()
{
	ObtainSemaphoreShared(&sem);
	note('3');
	ReleaseSemaphore(&sem);
	finish();
}

static void
excl()
{
	ObtainSemaphore(&sem);
	if (sem.ss_Owner != FindTask(NULL))
		badown++;
	note('X');
	ReleaseSemaphore(&sem);
	finish();
}

static char *
shared()
{
	struct Task *t1, *t2;

	reset();
	badown = 0;
	ObtainSemaphore(&sem);
	t1 = spawn(5, s1);
	t2 = spawn(5, s2);
	if (t1 == NULL || t2 == NULL || spawn(5, excl) == NULL ||
	    spawn(5, s3) == NULL)
		return ("CreateTask");
	if (sem.ss_QueueCount != 4 || nlog != 0)
		return ("requests not queued");

	/* The two leading shared requests are granted together; the
	 * exclusive one and the shared one behind it keep waiting. */
	ReleaseSemaphore(&sem);
	if (!same("12") || sem.ss_Owner != NULL || sem.ss_NestCount != 2 ||
	    sem.ss_QueueCount != 3)
		return ("leading shared grant");

	/* A further shared hold joins the holders. */
	if (!AttemptSemaphoreShared(&sem) || sem.ss_NestCount != 3)
		return ("shared Attempt while shared");
	if (AttemptSemaphore(&sem))
		return ("exclusive Attempt while shared");
	ReleaseSemaphore(&sem);

	Signal(t1, GO_MASK);
	await(1);
	if (!same("12a") || sem.ss_NestCount != 1)
		return ("first shared release");
	Signal(t2, GO_MASK);
	await(3);
	if (!same("12abX3"))
		return ("shared handoff order");
	if (badown)
		return ("shared ownership");
	if (!isfree(&sem))
		return ("shared left semaphore held");
	return (NULL);
}

static char *
registry()
{
	sem2.ss_Link.ln_Name = semname;
	sem2.ss_Link.ln_Pri = 0;
	sem3.ss_Link.ln_Name = lowname;
	sem3.ss_Link.ln_Pri = -10;
	AddSemaphore(&sem3);
	AddSemaphore(&sem2);
	if (sem2.ss_Link.ln_Type != NT_SIGNALSEM || !isfree(&sem2))
		return ("AddSemaphore did not initialise");
	if (sem2.ss_Link.ln_Succ != (struct Node *)&sem3)
		return ("AddSemaphore order");
	if (FindSemaphore(semname) != &sem2 || FindSemaphore(lowname) != &sem3)
		return ("FindSemaphore");
	RemSemaphore(&sem2);
	if (FindSemaphore(semname) != NULL || FindSemaphore(lowname) != &sem3)
		return ("RemSemaphore");
	RemSemaphore(&sem3);
	if (FindSemaphore(lowname) != NULL)
		return ("second RemSemaphore");
	return (NULL);
}

static char *
procure()
{
	struct MsgPort *port;
	struct SemaphoreMessage bid, bid2;
	struct Task *t;
	char *why;

	if ((port = CreateMsgPort()) == NULL)
		return ("CreateMsgPort");
	why = NULL;

	/* Free: granted at once. */
	bid.ssm_Message.mn_Node.ln_Name = (STRPTR)SM_EXCLUSIVE;
	bid.ssm_Message.mn_ReplyPort = port;
	bid.ssm_Semaphore = NULL;
	Procure(&sem, &bid);
	if (GetMsg(port) != &bid.ssm_Message || bid.ssm_Semaphore != &sem ||
	    sem.ss_Owner != root || sem.ss_NestCount != 1) {
		why = "immediate exclusive bid";
		goto out;
	}
	Vacate(&sem, &bid);
	if (!isfree(&sem) || GetMsg(port) != NULL) {
		why = "Vacate of a granted bid";
		goto out;
	}

	bid.ssm_Message.mn_Node.ln_Name = (STRPTR)SM_SHARED;
	Procure(&sem, &bid);
	if (GetMsg(port) != &bid.ssm_Message || sem.ss_Owner != NULL ||
	    sem.ss_NestCount != 1) {
		why = "immediate shared bid";
		goto out;
	}
	Vacate(&sem, &bid);
	if (!isfree(&sem)) {
		why = "shared Vacate";
		goto out;
	}

	/* Held: bids queue; one is withdrawn, the other granted later. */
	if ((t = spawn(1, holder)) == NULL) {
		why = "CreateTask";
		goto out;
	}
	await(1);
	bid.ssm_Message.mn_Node.ln_Name = (STRPTR)SM_EXCLUSIVE;
	bid.ssm_Semaphore = NULL;
	bid2.ssm_Message.mn_Node.ln_Name = (STRPTR)SM_SHARED;
	bid2.ssm_Message.mn_ReplyPort = port;
	bid2.ssm_Semaphore = &sem;
	Procure(&sem, &bid);
	Procure(&sem, &bid2);
	if (GetMsg(port) != NULL || sem.ss_QueueCount != 2) {
		why = "bids on held semaphore";
		goto out;
	}
	Vacate(&sem, &bid2);
	if (GetMsg(port) != &bid2.ssm_Message || bid2.ssm_Semaphore != NULL ||
	    sem.ss_QueueCount != 1 || sem.ss_Owner != t) {
		why = "withdrawn bid";
		goto out;
	}
	Signal(t, GO_MASK);
	await(1);
	if (WaitPort(port) != &bid.ssm_Message || GetMsg(port) != &bid.ssm_Message ||
	    bid.ssm_Semaphore != &sem || sem.ss_Owner != root ||
	    sem.ss_NestCount != 1 || sem.ss_QueueCount != 0) {
		why = "queued bid grant";
		goto out;
	}
	Vacate(&sem, &bid);
	if (!isfree(&sem))
		why = "queued bid Vacate";
out:
	DeleteMsgPort(port);
	return (why);
}

static void
listholder()
{
	ObtainSemaphore(&sem2);
	tell();
	Sleep(3);
	note('w');
	ReleaseSemaphore(&sem2);
	finish();
}

static char *
semlist()
{
	struct List l;

	reset();
	InitSemaphore(&sem);
	InitSemaphore(&sem2);
	InitSemaphore(&sem3);
	NewList(&l);
	AddTail(&l, &sem);
	AddTail(&l, &sem2);
	AddTail(&l, &sem3);
	if (spawn(1, listholder) == NULL)
		return ("CreateTask");
	await(1);
	ObtainSemaphoreList(&l);
	note('r');
	if (!same("wr"))
		return ("ObtainSemaphoreList did not wait");
	if (sem.ss_Owner != root || sem2.ss_Owner != root ||
	    sem3.ss_Owner != root || sem2.ss_NestCount != 1 ||
	    sem2.ss_QueueCount != 0)
		return ("ObtainSemaphoreList ownership");
	await(1);
	ReleaseSemaphoreList(&l);
	if (!isfree(&sem) || !isfree(&sem2) || !isfree(&sem3))
		return ("ReleaseSemaphoreList");
	return (NULL);
}

int
main()
{
	char *why;

	root = FindTask(NULL);
	if (AllocSignal(DONE_BIT) != DONE_BIT) {
		why = "AllocSignal";
		goto fail;
	}
	if ((why = layout()) != NULL || (why = nesting()) != NULL ||
	    (why = attempts()) != NULL || (why = contention()) != NULL ||
	    (why = shared()) != NULL || (why = registry()) != NULL ||
	    (why = procure()) != NULL || (why = semlist()) != NULL)
		goto fail;
	KPutStr("PASS sem\n");
	return (0);
fail:
	xKPrintf("FAIL sem: %s\n", why);
	return (1);
}
