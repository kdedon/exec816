/* SPDX-License-Identifier: MIT */
/* Shared Task crash test code: Tasks that trap, and waiting for them. */

#define CSTACK		1024L

/* Run from the data segment.  epacode is an extended instruction
 * (FLDCTL R1, USER), which traps without an EPU; sccode is sc 1, sc2code
 * sc 2.  A privileged instruction cannot trap alone: only normal mode
 * traps one, and every segment is system-only. */
static UWORD epacode[] = { 0x8F50, 0xC100, 0x9E08 };
static UWORD sccode[] = { 0x7F01, 0x9E08 };
static UWORD sc2code[] = { 0x7F02, 0x9E08 };

static volatile int ran;	/* a crashed Task ran past its trap */

static UWORD
readnull()
{
	return (*(volatile UWORD *)NULL);
}

static void
crashseg()
{
	readnull();
	ran++;
}

static void
crashepa()
{
	(*(void (*)())(APTR)epacode)();
	ran++;
}

static void
crashsc()
{
	(*(void (*)())(APTR)sccode)();
	ran++;
}

static void
crashsc2()
{
	(*(void (*)())(APTR)sc2code)();
	ran++;
}

/* Until (*p)(a) is nonzero, or ticks ticks: whether it is. */
static int
until(p, a, ticks)
int (*p)();
APTR a;
ULONG ticks;
{
	ULONG t0;

	for (t0 = SysBase->ex_TickCount; !(*p)(a) &&
	    SysBase->ex_TickCount - t0 < ticks; )
		Sleep(1L);
	return ((*p)(a));
}

/* Task t is held: waiting on no signal. */
static int
held(t)
struct Task *t;
{
	return (t->tc_State == TS_WAIT && t->tc_SigWait == 0);
}

/* A Task named name is gone. */
static int
gone(name)
char *name;
{
	return (FindTask(name) == NULL);
}
