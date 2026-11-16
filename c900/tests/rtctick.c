/* SPDX-License-Identifier: MIT */
/*
 * battclock beside the tick: a Sleep(1) Task keeps waking while the clock
 * is read and set, Sleep keeps time afterwards, and the CIO's tick, port A
 * and port C setup survive, with Port B's data and direction restored.
 */
/* EMUFLAGS: --rtc=2026-11-12T10:20:30 */
#include "test.h"
#include <exec/exec.h>
#include <resources/battclock.h>

APTR BattClockBase;

#define DONE_BIT	16
#define DONE_MASK	(1L << DONE_BIT)
#define P_PBDATA	0x001d
#define P_PCDATA	0x001f
#define P_PBDD		0x0057

/* CIO U70 ports that must not change: MICR, MCCR, port A vector, CTIV,
 * port A command/status, CT3 time constant and mode, port A mode,
 * handshake, polarity, direction and special I/O, port C polarity,
 * direction and special I/O. */
static int ports[] = {
	0x01, 0x03, 0x05, 0x09, 0x11, 0x35, 0x37, 0x3d,
	0x41, 0x43, 0x45, 0x47, 0x49, 0x0b, 0x0d, 0x0f
};
#define NPORTS	(sizeof(ports) / sizeof(ports[0]))

static struct Task *parent;
static volatile int stop;
static volatile UWORD wakes;

static void
sleeper()
{
	while (!stop) {
		Sleep(1);
		wakes++;
	}
	Signal(parent, DONE_MASK);
}

static char *
run()
{
	int before[NPORTS];
	ULONG t0, dt, t;
	int i, n, pc;

	parent = FindTask(NULL);
	SetTaskPri(parent, 0);
	for (i = 0; i < NPORTS; i++)
		before[i] = inb(ports[i]);
	pc = inb(P_PCDATA) & 0x0d;
	outb(P_PBDATA, 0x05);

	if (AllocSignal(DONE_BIT) != DONE_BIT)
		return ("AllocSignal");
	if (CreateTask("sleeper", 5, sleeper, 1024) == NULL)
		return ("CreateTask");
	Sleep(1);
	t0 = TickCount;
	wakes = 0;
	for (n = 0; TickCount - t0 < 100; n++) {
		if ((t = ReadBattClock()) == 0)
			return ("ReadBattClock");
		if ((n & 15) == 0 && WriteBattClock(t) != 0)
			return ("WriteBattClock");
	}
	dt = TickCount - t0;
	stop = 1;
	Wait(DONE_MASK);
	FreeSignal(DONE_BIT);
	xKPrintf("rtctick: %d accesses, %u wakes in %lu ticks\n", n, wakes, dt);
	if (n < 10 || wakes < dt * 3 / 4)
		return ("tick starved during clock access");

	t0 = TickCount;
	Sleep(10);
	dt = TickCount - t0;
	if (dt < 10 || dt > 12)
		return ("Sleep after clock access");

	for (i = 0; i < NPORTS; i++)
		if (inb(ports[i]) != before[i]) {
			xKPrintf("rtctick: port %x %x -> %x\n", ports[i],
			    before[i], inb(ports[i]));
			return ("CIO register changed");
		}
	if ((inb(P_PCDATA) & 0x0f) != (pc | 0x02))
		return ("port C bits");
	if (inb(P_PBDATA) != 0x05 || inb(P_PBDD) != 0)
		return ("port B not restored");
	outb(P_PBDATA, 0);
	return (NULL);
}

int
main()
{
	char *why;

	BattClockBase = OpenResource(BATTCLOCKNAME);
	if ((why = run()) != NULL) {
		xKPrintf("FAIL rtctick: %s\n", why);
		return (1);
	}
	KPutStr("PASS rtctick\n");
	return (0);
}
