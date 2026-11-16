/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Boot-chain smoke test.  Linked without the Task, memory and port code:
 * it replaces cmain and stubs the kernel entry points the machine layer
 * and trap stubs call, then checks the console, KPrintf, the memory map,
 * segment mapping, the SC gate and the tick.
 */
#include "kernel.h"

struct Task *ThisTask = NULL;
UWORD IntDepth;

static int fails;
static int nadd;
static UWORD nsc;
static UWORD sclast;
static volatile UWORD nticks;
static volatile UWORD nother;
static volatile UWORD lastid;

void xForbid() {}
void xPermit() {}
void kvidinit() {}
void kvidputc(c) int c; {}
int kvidgetc() { return (-1); }
int kserbusy() { return (0); }
void kvidstop() {}

static void
fail(why)
char *why;
{
	xKPrintf("FAIL smoke: %s\n", why);
	fails++;
}

/* Write and read back a word at p. */
static int
probe(p)
register UWORD *p;
{
	UWORD old;

	old = *p;
	*p = 0x5AC3;
	if (*p != 0x5AC3)
		return (0);
	*p = ~0x5AC3;
	if (*p != (UWORD)~0x5AC3)
		return (0);
	*p = old;
	return (1);
}

struct MemHeader *
kaddmem(base, lower, upper, attrs, pri, name)
APTR base;
ULONG lower, upper;
UWORD attrs;
int pri;
char *name;
{
	xKPrintf("kaddmem %p %lx-%lx attr %x pri %d %s\n", base, lower,
	    upper, attrs, pri, name);
	if (!probe((UWORD *)MKPTR(base, lower)) ||
	    !probe((UWORD *)MKPTR(base, upper - 2L)))
		fail("kaddmem range is not RAM");
	nadd++;
	return (NULL);
}

UWORD
kswitch(frameoff)
UWORD frameoff;
{
	nsc++;
	sclast = frameoff;
	return (frameoff);
}

/* trap.s runs interrupt C code on the stack below this offset; the root
 * stack has room for it. */
UWORD kintstk = ROOTSTKLOW + 0x400;

/* The interrupt entry: acknowledge and count the tick on vector 0; any
 * other source (the boot loader's latched disk completion) is counted,
 * acknowledged where the kernel acknowledges it, and dismissed.  A storm
 * stops the test, since it starves the wait below. */
UWORD
kintr(frameoff, id)
UWORD frameoff, id;
{
	if (id == 0) {
		outb(0x0019, 0x24);
		nticks++;
		return (frameoff);
	}
	if (id == 0x100)
		inb(0x0181);
	else if (id == 0x80)
		outw(0x0500, 0);
	lastid = id;
	if (++nother >= 1000) {
		xKPrintf("FAIL smoke: interrupt storm, id %x, %u ticks\n",
		    id, nticks);
		emustop();
		for (;;)
			;
	}
	return (frameoff);
}

UWORD
kfault(frameoff, code)
UWORD frameoff;
int code;
{
	register UWORD *f;

	f = (UWORD *)MKPTR(SEGBASE(STACKSEG), frameoff);
	xKPrintf("FAIL smoke: trap %d id %x fcw %x pc %x:%x\n", code, f[14],
	    f[15], f[16], f[17]);
	emustop();
	for (;;)
		haltcpu();
}

static void
fmtcheck()
{
	xKPrintf("fmt: [%d] [%u] [%x] [%04x] [%5d] [%-3s] [%c] [%%]\n",
	    -42, (UWORD)65535, 0xbeef, 0x2a, 7, "ab", 'z');
	xKPrintf("fmt: [%ld] [%lu] [%lx] [%p] [%s]\n", -100000L, 4000000000L,
	    0xdeadbeefL, (APTR)0x3f00fff0L, "end");
}

/* A heap page through a low segment number, the way mach_mem maps one. */
static void
segcheck()
{
	mapseg(0x02, 0x0a00, 2);
	if (probe((UWORD *)0x02000100L))
		xKPrintf("seg 02 -> phys 0a0000: ram\n");
	else
		fail("segment 02 mapped to page 0a is not RAM");
}

void
cmain()
{
	register int s;
	register long spin;

	xKPutStr("Exec900 smoke\n");
	fmtcheck();
	mach_mem();
	if (nadd == 0)
		fail("mach_mem added no memory");
	segcheck();

	s = intoff();
	kresched();
	intrest(s);
	if (nsc != 1 || sclast < ROOTSTKLOW)
		fail("SC gate did not reach kswitch");
	else
		xKPrintf("sc: kswitch frame %x\n", sclast);

	/* Spin rather than halt, so a tick that never comes still ends the
	 * test: about 5 s at 6 MHz. */
	tick_init();
	for (spin = 0; nticks < 10 && spin < 300000L; spin++)
		;
	s = intoff();
	xKPrintf("tick: %u ticks\n", nticks);
	if (nother != 0)
		xKPrintf("other: %u interrupt(s), last id %x\n", nother, lastid);
	if (nticks < 10) {
		xKPrintf("cio: ct3cs %x micr %x mccr %x ctiv %x\n",
		    inb(0x0019), inb(0x0001), inb(0x0003), inb(0x0009));
		fail("the tick did not run");
	}
	if (fails == 0)
		xKPutStr("PASS smoke\n");
	emustop();
	for (;;)
		haltcpu();
}
