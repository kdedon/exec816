/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The Z8070 EPU and mathieeedoubbas.library.  Every target runs it: with
 * no EPU, an extended instruction must reach tc_TrapCode as trap 11 and
 * the library runs its software path; with one, the instruction runs, two
 * Tasks keep F2 live across preemption, the root's F3 survives them and
 * the library uses the EPU.  Library results are checked against IEEE
 * round-to-nearest values; with no EPU they must also equal the C
 * runtime's.  The timings are in ticks.
 */
#include <exec/exec.h>
#include <exec/execbase.h>
#include <lvo/exec_lib.h>
#include <lvo/mathieeedoubbas_lib.h>
#include <lvo/mathieeedoubtrans_lib.h>

struct Library *MathIeeeDoubBasBase;
struct Library *MathIeeeDoubTransBase;

int epuraw();
double fsdladd(), fsdlsub(), fsdlmul(), fsdldiv();
void epures(), epuld2(), epust2(), epuadd2(), epuld3(), epust3();

static int fails;
static ULONG traps, trapnum;

static void
fail(what, n)
char *what;
int n;
{
	xKPrintf("FAIL epu: %s %d\n", what, n);
	fails++;
}

/* Resume past the extended instruction, whatever PC the CPU saved. */
static void
trapper(num, fp)
ULONG num;
struct TrapFrame *fp;
{
	traps++;
	trapnum = num;
	fp->tf_PCSeg = 0x8000 | (UWORD)((ULONG)epures >> 16);
	fp->tf_PC = (UWORD)(ULONG)epures;
}

typedef union {
	double d;
	UWORD w[4];
} DW;

static struct {
	char op;
	UWORD a[4], b[4], r[4];
} cases[] = {
	{ '+', { 0x3ff8, 0x0000, 0x0000, 0x0000 }, { 0x4004, 0x0000, 0x0000, 0x0000 },
	    { 0x4010, 0x0000, 0x0000, 0x0000 } },
	{ '+', { 0x3fb9, 0x9999, 0x9999, 0x999a }, { 0x3fc9, 0x9999, 0x9999, 0x999a },
	    { 0x3fd3, 0x3333, 0x3333, 0x3334 } },
	{ '+', { 0x3ff0, 0x0000, 0x0000, 0x0000 }, { 0x3c67, 0x0ef5, 0x4646, 0xd497 },
	    { 0x3ff0, 0x0000, 0x0000, 0x0000 } },
	{ '+', { 0xc00a, 0x0000, 0x0000, 0x0000 }, { 0x3ff2, 0x0000, 0x0000, 0x0000 },
	    { 0xc001, 0x0000, 0x0000, 0x0000 } },
	{ '+', { 0x3fd5, 0x5555, 0x5555, 0x5555 }, { 0x3fe5, 0x5555, 0x5555, 0x5555 },
	    { 0x3ff0, 0x0000, 0x0000, 0x0000 } },
	{ '-', { 0x3ff0, 0x0000, 0x0000, 0x0000 }, { 0x3fec, 0xcccc, 0xcccc, 0xcccd },
	    { 0x3fb9, 0x9999, 0x9999, 0x9998 } },
	{ '-', { 0x4014, 0x0000, 0x0000, 0x0000 }, { 0x401e, 0x0000, 0x0000, 0x0000 },
	    { 0xc004, 0x0000, 0x0000, 0x0000 } },
	{ '-', { 0x4341, 0xc379, 0x37e0, 0x8000 }, { 0x3ff0, 0x0000, 0x0000, 0x0000 },
	    { 0x4341, 0xc379, 0x37e0, 0x8000 } },
	{ '*', { 0x3fb9, 0x9999, 0x9999, 0x999a }, { 0x4008, 0x0000, 0x0000, 0x0000 },
	    { 0x3fd3, 0x3333, 0x3333, 0x3334 } },
	{ '*', { 0x3ff8, 0x0000, 0x0000, 0x0000 }, { 0xc004, 0x0000, 0x0000, 0x0000 },
	    { 0xc00e, 0x0000, 0x0000, 0x0000 } },
	{ '*', { 0x6974, 0xe718, 0xd7d7, 0x625a }, { 0x6974, 0xe718, 0xd7d7, 0x625a },
	    { 0x7ff0, 0x0000, 0x0000, 0x0000 } },
	{ '*', { 0x3fd5, 0x5555, 0x5555, 0x5555 }, { 0x4008, 0x0000, 0x0000, 0x0000 },
	    { 0x3ff0, 0x0000, 0x0000, 0x0000 } },
	{ '*', { 0x3ff1, 0x9999, 0x9999, 0x999a }, { 0x3ff1, 0x9999, 0x9999, 0x999a },
	    { 0x3ff3, 0x5c28, 0xf5c2, 0x8f5d } },
	{ '/', { 0x3ff0, 0x0000, 0x0000, 0x0000 }, { 0x4008, 0x0000, 0x0000, 0x0000 },
	    { 0x3fd5, 0x5555, 0x5555, 0x5555 } },
	{ '/', { 0x4000, 0x0000, 0x0000, 0x0000 }, { 0x401c, 0x0000, 0x0000, 0x0000 },
	    { 0x3fd2, 0x4924, 0x9249, 0x2492 } },
	{ '/', { 0xc022, 0x0000, 0x0000, 0x0000 }, { 0x4008, 0x0000, 0x0000, 0x0000 },
	    { 0xc008, 0x0000, 0x0000, 0x0000 } },
	{ '/', { 0x3ff0, 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000, 0x0000 },
	    { 0x7ff0, 0x0000, 0x0000, 0x0000 } },
	{ '/', { 0x4024, 0x0000, 0x0000, 0x0000 }, { 0x3fb9, 0x9999, 0x9999, 0x999a },
	    { 0x4059, 0x0000, 0x0000, 0x0000 } },
	{ 0 }
};

static int
same(x, w)
DW *x;
UWORD *w;
{
	register int i;

	for (i = 0; i < 4; i++)
		if (x->w[i] != w[i])
			return (0);
	return (1);
}

static void
show(what, i, x)
char *what;
int i;
DW *x;
{
	xKPrintf("epu: %s %d = %04x %04x %04x %04x\n", what, i,
	    x->w[0], x->w[1], x->w[2], x->w[3]);
}

static void
arith(epu)
int epu;
{
	register int i;
	DW a, b, lib, cop, soft;

	for (i = 0; cases[i].op != 0; i++) {
		kcopy(a.w, cases[i].a);
		kcopy(b.w, cases[i].b);
		switch (cases[i].op) {
		case '+':
			lib.d = IEEEDPAdd(a.d, b.d);
			cop.d = a.d + b.d;
			soft.d = fsdladd(a.d, &b.d);
			break;
		case '-':
			lib.d = IEEEDPSub(a.d, b.d);
			cop.d = a.d - b.d;
			soft.d = fsdlsub(a.d, &b.d);
			break;
		case '*':
			lib.d = IEEEDPMul(a.d, b.d);
			cop.d = a.d * b.d;
			soft.d = fsdlmul(a.d, &b.d);
			break;
		default:
			lib.d = IEEEDPDiv(a.d, b.d);
			cop.d = a.d / b.d;
			soft.d = fsdldiv(a.d, &b.d);
			break;
		}
		if (!same(&soft, cases[i].r))
			show("soft differs from IEEE: case", i, &soft);
		if (epu ? !same(&lib, cases[i].r) : !same(&lib, soft.w)) {
			show("lib", i, &lib);
			fail("arith case", i);
		}
		if (epu ? !same(&cop, cases[i].r) : !same(&cop, soft.w)) {
			show("C", i, &cop);
			fail("C runtime case", i);
		}
	}
}

kcopy(d, s)
register UWORD *d, *s;
{
	d[0] = s[0];
	d[1] = s[1];
	d[2] = s[2];
	d[3] = s[3];
}

static void
others()
{
	double x;

	if (IEEEDPFix(2.75) != 2 || IEEEDPFix(-2.75) != -2 ||
	    IEEEDPFix(1e9) != 1000000000L || IEEEDPFix(1e12) != 0x80000000L)
		fail("fix", 0);
	if (IEEEDPFlt(-7L) != -7.0 || IEEEDPFlt(1000000000L) != 1e9)
		fail("flt", 0);
	if (IEEEDPCmp(1.0, 2.0) != -1 || IEEEDPCmp(2.0, 1.0) != 1 ||
	    IEEEDPCmp(-0.0, 0.0) != 0 || IEEEDPTst(-3.0) != -1 ||
	    IEEEDPTst(0.0) != 0 || IEEEDPTst(5.0) != 1)
		fail("cmp/tst", 0);
	if (IEEEDPAbs(-2.5) != 2.5 || IEEEDPNeg(2.5) != -2.5 ||
	    IEEEDPNeg(-2.5) != 2.5)
		fail("abs/neg", 0);
	if (IEEEDPFloor(2.5) != 2.0 || IEEEDPFloor(-2.5) != -3.0 ||
	    IEEEDPFloor(-7.0) != -7.0 || IEEEDPCeil(2.5) != 3.0 ||
	    IEEEDPCeil(-2.5) != -2.0 || IEEEDPTst(IEEEDPCeil(-0.5)) != 0)
		fail("floor/ceil", 0);
	x = IEEEDPFloor(0.5);
	if (x != 0.0)
		fail("floor 0.5", 0);
}

static int
near(x, y)
double x, y;
{
	double d;

	d = x - y;
	if (d < 0.0)
		d = -d;
	return (d < 1e-9);
}

static void
trans()
{
	static UWORD root2[4] = { 0x3ff6, 0xa09e, 0x667f, 0x3bcd };
	DW x;
	double c;

	x.d = IEEEDPSqrt(2.0);
	if (!near(x.d, 1.4142135623730951))
		fail("sqrt", 0);
	if (!same(&x, root2))
		show("sqrt(2) is not the rounded root:", 0, &x);
	if (!near(IEEEDPSin(0.5), 0.479425538604203) ||
	    !near(IEEEDPCos(0.5), 0.8775825618903728) ||
	    !near(IEEEDPAtan(1.0), 0.7853981633974483) ||
	    !near(IEEEDPExp(1.0), 2.718281828459045) ||
	    !near(IEEEDPLog(10.0), 2.302585092994046) ||
	    !near(IEEEDPLog10(1000.0), 3.0) ||
	    !near(IEEEDPPow(10.0, 2.0), 1024.0))
		fail("trans", 0);
	if (!near(IEEEDPSincos(&c, 0.5), 0.479425538604203) ||
	    !near(c, 0.8775825618903728))
		fail("sincos", 0);
	if (IEEEDPTieee(1.5) != 0x3FC00000L || IEEEDPFieee(0xC0200000L) != -2.5)
		fail("tieee/fieee", 0);
}

/* Two Tasks with F2 live across preemption. */
static struct Task *root;
static int rootsig;
static UWORD progress[2];
static int interleaved[2];
static DW sums[2];

static void
accum(k)
int k;
{
	static double one = 1.0, three = 3.0, zero = 0.0;
	register int i;
	UWORD seen;

	epuld2(&zero);
	seen = progress[1 - k];
	for (i = 0; i < 30; i++) {
		epuadd2(1000, k ? &three : &one);
		progress[k]++;
		if (progress[1 - k] != seen) {
			seen = progress[1 - k];
			interleaved[k]++;
		}
	}
	epust2(&sums[k].d);
	Signal(root, 1L << rootsig);
}

static void accum0() { accum(0); }
static void accum1() { accum(1); }

static void
preempt()
{
	static double f3 = 12345.5;
	DW back;

	epuld3(&f3);
	CreateTask("epuA", 0, (APTR)accum0, 1024L);
	CreateTask("epuB", 0, (APTR)accum1, 1024L);
	Wait(1L << rootsig);
	Wait(1L << rootsig);
	if (sums[0].d != 30000.0 || sums[1].d != 90000.0) {
		show("sum", 0, &sums[0]);
		show("sum", 1, &sums[1]);
		fail("sums", 0);
	}
	if (interleaved[0] == 0 || interleaved[1] == 0)
		fail("no interleaving", interleaved[0]);
	epust3(&back.d);
	if (back.d != f3)
		fail("root F3", 0);
	xKPrintf("epu: interleaved %d %d\n", interleaved[0], interleaved[1]);
}

/* Ping-pong: N round trips of two switches, each side optionally
 * running an extended instruction, which moves the EPU every turn. */
static struct Task *ping;
static int pingsig, pingepu;
static volatile int pingdone;

static void
pingfn()
{
	pingsig = AllocSignal(-1L);
	Signal(root, 1L << rootsig);
	for (;;) {
		Wait(1L << pingsig);
		if (pingdone)
			break;
		if (pingepu)
			epuraw();
		Signal(root, 1L << rootsig);
	}
	Signal(root, 1L << rootsig);
}

static ULONG
pingpong(n, withepu)
int n, withepu;
{
	register int i;
	ULONG t;

	pingepu = withepu;
	pingdone = 0;
	ping = CreateTask("epuping", 0, (APTR)pingfn, 1024L);
	Wait(1L << rootsig);
	t = SysBase->ex_TickCount;
	for (i = 0; i < n; i++) {
		Signal(ping, 1L << pingsig);
		Wait(1L << rootsig);
		if (withepu)
			epuraw();
	}
	t = SysBase->ex_TickCount - t;
	pingdone = 1;
	Signal(ping, 1L << pingsig);
	Wait(1L << rootsig);
	return (t);
}

/* N calls of one operation: the library, the C operator (the runtime,
 * on the EPU when fitted) and the software routine. */
#define N	300
#define TIME(what, expr) TIMEN(what, expr, N)
#define TIMEN(what, expr, n) { \
	t = SysBase->ex_TickCount; \
	for (i = 0; i < n; i++) \
		expr; \
	what = SysBase->ex_TickCount - t; }

static void
timing(epu)
int epu;
{
	register int i;
	ULONG t, lib, cop, soft;
	double a, b, r;

	a = 1.1;
	b = 3.3;
	xKPrintf("epu: switch %ld ticks/200 round trips\n", pingpong(200, 0));
	if (epu)
		xKPrintf("epu: switch+EPU %ld ticks/200\n", pingpong(200, 1));
	TIME(lib, r = IEEEDPAdd(a, b));
	TIME(cop, r = a + b);
	TIME(soft, r = fsdladd(a, &b));
	xKPrintf("epu: add lib %ld C %ld soft %ld ticks/%d\n", lib, cop, soft, N);
	TIME(lib, r = IEEEDPMul(a, b));
	TIME(cop, r = a * b);
	TIME(soft, r = fsdlmul(a, &b));
	xKPrintf("epu: mul lib %ld C %ld soft %ld ticks/%d\n", lib, cop, soft, N);
	TIME(lib, r = IEEEDPDiv(a, b));
	TIME(cop, r = a / b);
	TIME(soft, r = fsdldiv(a, &b));
	xKPrintf("epu: div lib %ld C %ld soft %ld ticks/%d\n", lib, cop, soft, N);
	if (MathIeeeDoubTransBase != NULL) {
		TIMEN(lib, r = IEEEDPSqrt(b), 50);
		TIMEN(cop, r = IEEEDPSin(b), 50);
		xKPrintf("epu: sqrt %ld sin %ld ticks/50\n", lib, cop);
	}
}

int
main()
{
	struct Task *me;
	APTR old;
	int epu, v;

	me = FindTask(NULL);
	root = me;
	rootsig = AllocSignal(-1L);
	epu = (SysBase->AttnFlags & AFF_68881) != 0;
	xKPrintf("epu: %s\n", epu ? "Z8070" : "none");

	old = me->tc_TrapCode;
	me->tc_TrapCode = (APTR)trapper;
	v = epuraw();
	me->tc_TrapCode = old;
	if (epu ? traps != 0 || v != 0 : traps != 1 || trapnum != TRAP_EPA)
		fail("trap", (int)trapnum);

	MathIeeeDoubBasBase = OpenLibrary("mathieeedoubbas.library", 0L);
	if (MathIeeeDoubBasBase == NULL) {
		fail("no mathieeedoubbas.library", 0);
		return (20);
	}
	arith(epu);
	others();
	MathIeeeDoubTransBase = OpenLibrary("mathieeedoubtrans.library", 0L);
	if (MathIeeeDoubTransBase == NULL)
		fail("no mathieeedoubtrans.library", 0);
	else
		trans();
	if (epu)
		preempt();
	timing(epu);
	if (fails == 0)
		xKPrintf("PASS epu\n");
	return (0);
}
