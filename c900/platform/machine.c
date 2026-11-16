/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C900 machine layer: the polled SCC console, the CIO tick, and the logical
 * address space built from kboot's memory map: identity-mapped heap pages,
 * the physical-only page pool and the remap windows.
 */
#include "kernel.h"
#include <bootinfo.h>

/* Motherboard SCC channel B, the ROM's console.  On a video machine the ROM
 * leaves its transmitter disabled, so con_init programs it. */
#define SCC_STAT	0x0101
#define SCC_DATA	0x0111
#define SCC_TXE		0x04
#define SCC_RXA		0x01
#define SCC_RR1		0x0103
#define SCC_ALLSENT	0x01
#define SCCWAIT		40000
#define FCW_VIE		0x1000

/* Register, value: 9600 8N1 from the BRG at PCLK 6 MHz, Rx and Tx on. */
static UBYTE conregs[] = {
	0x09, 0x4c,	/* WR4: x16 clock, 1 stop bit */
	0x17, 0x56,	/* WR11: Rx and Tx clocks from the BRG */
	0x1d, 0x02,	/* WR14: BRG stopped */
	0x19, 17,	/* WR12, WR13: time constant */
	0x1b, 0,
	0x1d, 0x03,	/* WR14: BRG running */
	0x07, 0xc1,	/* WR3: Rx 8 bits, enabled */
	0x0b, 0xea	/* WR5: DTR, Tx 8 bits, enabled, RTS */
};

static void romcopy();

/* Called with VI off before the first console byte.  Waits for kboot's last
 * character to leave, then finds the display card, whose mapping may
 * cover the ROM's segments, so the ROM is read first. */
void
con_init()
{
	register unsigned n;
	register int i;

	romcopy();
	for (n = SCCWAIT; n != 0 && (inb(SCC_RR1) & SCC_ALLSENT) == 0; n--)
		;
	for (i = 0; i < sizeof conregs; i += 2)
		outb(0x0100 + conregs[i], conregs[i + 1]);
	kvidinit();
}

/* The console line's transmit queue, from serial.device's init on: a Task
 * with VI on queues its bytes and the Tx interrupt sends them. */
UBYTE *ktxq;
UWORD ktxcnt;
static UWORD ktxhead;

/* Load queued bytes while the transmitter takes them; the bytes loaded.
 * VI off. */
int
kcontx()
{
	register int n;

	for (n = 0; ktxcnt != 0 && (inb(SCC_STAT) & SCC_TXE) != 0; n++) {
		outb(SCC_DATA, ktxq[ktxhead]);
		ktxhead = (ktxhead + 1) & (KTXQ - 1);
		ktxcnt--;
	}
	return (n);
}

/* Send what is queued, polled. */
void
kcondrain()
{
	register unsigned n;
	int s;

	s = intoff();
	while (ktxcnt != 0) {
		for (n = SCCWAIT; n != 0 && (inb(SCC_STAT) & SCC_TXE) == 0; n--)
			;
		if (n == 0)
			break;
		kcontx();
	}
	intrest(s);
}

/* serial.device unit 0 loads the same transmitter from interrupts, so the
 * empty test and the load are one step with VI off.  Interrupt level and
 * VI-off callers wait for the queue to empty and send polled, so a polled
 * byte never passes a queued one.  The serial line always gets the byte;
 * a display console gets it too, or keeps it until its module loads. */
void
con_putc(c)
int c;
{
	int s;

	if (c == '\n')
		con_putc('\r');
	for (;;) {
		s = intoff();
		if (ktxq != NULL && IntDepth == 0 && (s & FCW_VIE) != 0) {
			if (ktxcnt < KTXQ) {
				ktxq[(ktxhead + ktxcnt) & (KTXQ - 1)] = c;
				ktxcnt++;
				kcontx();
				break;
			}
		} else if ((ktxcnt == 0 || (kcontx(), ktxcnt == 0)) &&
		    (inb(SCC_STAT) & SCC_TXE) != 0) {
			outb(SCC_DATA, c & 0xff);
			break;
		}
		intrest(s);
	}
	intrest(s);
	kvidputc(c);
}

/* The serial line's next byte, else the display console's keyboard.
 * While serial.device unit 0 is open the line's input is its own. */
int
con_getc()
{
	if (kserbusy() || (inb(SCC_STAT) & SCC_RXA) == 0)
		return (kvidgetc());
	return (inb(SCC_DATA) & 0xff);
}

/* Z8036 CIO #1 counter/timer 3. */
#define P_MICR		0x0001
#define P_CTIV		0x0009
#define P_CT3CS		0x0019
#define P_CT3TCH	0x0035
#define P_CT3TCL	0x0037
#define P_CT3MS		0x003d

#define MICR_MIE	0x80	/* master interrupt enable */
#define CT3MS_TICK	0x80	/* continuous; CT3's pins stay unused */
#define CT3CS_CLIP	0x24	/* clear IP and IUS, gate open */
#define CT3CS_IEGO	0xc6	/* set IE, gate open, trigger */
#define CT3CS_GO	0x06	/* gate open, trigger */

#define TICKVEC		0	/* INTV_TICK: the tick server chain */

#define TC4MHZ		20000	/* 100 Hz at a 4 MHz CPU clock */
#define TC6MHZ		30000	/* 100 Hz at 6 MHz */

/* Segment 1 offset 0 holds a far pointer to the ROM configuration block:
 * words 0 and 1 are the RAM range in 1 KB clicks, byte 14 the CPU type. */
#define ROMCONF_PP	0x01000000L
#define RC_CTYPE	14
#define RC_BRAM		0
#define RC_ERAM		1

/* The ROM's configuration block, or NULL when segment 1 holds no pointer
 * into itself.  Valid only until con_init() may remap segment 1. */
static char *
romconf()
{
	register ULONG pp;

	pp = *(ULONG *)ROMCONF_PP;
	if (((pp >> 24) & 0x7fL) != 1L)
		return (NULL);
	return ((char *)pp);
}

/* The ROM's CPU type, copied before segment 1 goes away: nonzero = 6 MHz;
 * and its RAM range, the fallback memory map. */
static int cpu6 = 1;
static struct biram romrun;

static void
romcopy()
{
	register char *rc;

	rc = romconf();
	cpu6 = rc == NULL || rc[RC_CTYPE] != 0;
	romrun.br_count = 0;
	if (rc != NULL) {
		/* The ROM's range may enclose framebuffers; it is only a
		 * fallback.  Its 1 KB clicks are 64 to a page. */
		romrun.br_first = ((unsigned *)rc)[RC_BRAM] >> 6;
		romrun.br_count = (((unsigned *)rc)[RC_ERAM] >> 6) -
		    romrun.br_first;
	}
}

/* C/T 3's time constant: its input counts per tick. */
UWORD
mach_tickconst()
{
	return (cpu6 ? TC6MHZ : TC4MHZ);
}

void
tick_init()
{
	register unsigned tc;

	tc = mach_tickconst();
	outb(P_CT3MS, CT3MS_TICK);
	outb(P_CTIV, TICKVEC);
	outb(P_CT3TCH, (tc >> 8) & 0xff);
	outb(P_CT3TCL, tc & 0xff);
	outb(P_CT3CS, CT3CS_CLIP);
	outb(P_CT3CS, CT3CS_IEGO);
	outb(P_MICR, MICR_MIE);
	outb(P_CT3CS, CT3CS_GO);
	intrest(FCW_TASK);
}

/* kboot finds this block by its magic in the loaded data and fills it. */
struct bootinfo bootinfo = {
	BI_MAGIC,
	BI_VERSION,
	(unsigned short)BI_LEN5,
	BI_NPART,
	0,
	BI_SRC_KERNEL,
	0, 0L, 0L,
	{ { 0L, 0L }, { 0L, 0L }, { 0L, 0L }, { 0L, 0L },
	  { 0L, 0L }, { 0L, 0L }, { 0L, 0L }, { 0L, 0L },
	  { 0L, 0L }, { 0L, 0L }, { 0L, 0L }, { 0L, 0L },
	  { 0L, 0L }, { 0L, 0L }, { 0L, 0L }, { 0L, 0L } },
	0,
	BI_CON_ANY,
	0,
	0
};

int
bivalid()
{
	if (bootinfo.bi_src != BI_SRC_KBOOT)
		return (0);
	if (bootinfo.bi_version != BI_VERSION
	    || bootinfo.bi_len != (unsigned short)BI_LEN5)
		return (0);
	if (bisum(&bootinfo) != 0)
		return (0);
	return (bootinfo.bi_nram <= BI_NRAM);
}

/* The serial lines kboot proved present (bi_serial); without a valid
 * block, only the console line. */
UWORD
mach_serial()
{
	return (bivalid() ? bootinfo.bi_serial : BI_SER_CON);
}

/* Nonzero on a 6 MHz machine (the ROM's CPU type). */
int
mach_cpu6()
{
	return (cpu6);
}

/* Per logical segment, the base page (phys >> 8) it maps, or MMU_NONE. */
UWORD kmmutab[NMMUSEG];

/* The physical-only pool: one bit per physical segment, set when free. */
static UBYTE physmap[256 / 8];
UWORD PhysPages;
UWORD PhysFree;
UWORD HeapPages;
UWORD HeapTail;
static int nreloc;

/* The first 256-byte page past the kernel image and the stack segment,
 * where the heap starts. */
static UWORD kendpage;

/* The stack-segment offset where its RAM starts (crt.s, then stkgrow). */
UWORD kstklow;
extern char end[];

struct stkcfg kstkcfg = {
	{ 'E', '9', 'S', 'T', 'A', 'C', 'K', 'K' },
	0,
	{ 'R', 'O', 'O', 'T' },
	ROOTSTKLOW
};

void kstkmove();

/*
 * Program descriptor seg with base page (phys >> 8) and attr, and record
 * it.  The descriptor load is several MMU cycles, so VI is off across it.
 */
void
kmapseg(seg, page, attr)
int seg;
UWORD page;
int attr;
{
	register int s;

	s = intoff();
	mapseg(seg, page, attr);
	kmmutab[seg] = (attr & MMU_INHIBIT) ? MMU_NONE : page;
	intrest(s);
}

/* Physical address of a logical pointer, or 0xFFFFFFFF if unmapped. */
ULONG
kphys(p)
APTR p;
{
	register UWORD seg, pg;

	seg = (UWORD)((ULONG)p >> 24) & 0x7f;
	if (seg >= NMMUSEG || (pg = kmmutab[seg]) == MMU_NONE)
		return (0xffffffffL);
	return (((ULONG)pg << 8) + POFF(p));
}

/* Take a free pool page; its physical segment number, or -1. */
int
kallocphys()
{
	register int pg, s;

	s = intoff();
	for (pg = 0; pg < 256; pg++)
		if (physmap[pg >> 3] & (1 << (pg & 7))) {
			physmap[pg >> 3] &= ~(1 << (pg & 7));
			PhysFree--;
			intrest(s);
			return (pg);
		}
	intrest(s);
	return (-1);
}

/* Return a page kallocphys() handed out. */
void
kfreephys(pg)
int pg;
{
	register int s;

	s = intoff();
	physmap[pg >> 3] |= 1 << (pg & 7);
	PhysFree++;
	intrest(s);
}

/*
 * Map window win (0 or 1) at the 1 KB-aligned base below phys, and return
 * the pointer to phys.  The window spans 64 KB from that base.
 */
APTR
kwinmap(win, phys)
int win;
ULONG phys;
{
	register int seg;

	seg = win ? WINSEG1 : WINSEG0;
	kmapseg(seg, (UWORD)(phys >> 8) & 0xfffc, MMU_SYS);
	return (MKPTR(SEGBASE(seg), phys & 0x3ffL));
}

void
kwinunmap(win)
int win;
{
	kmapseg(win ? WINSEG1 : WINSEG0, 0, MMU_INHIBIT);
}

/* Map CMDSEG over the disk command area, writable, limited to CMDLEN. */
static void
cmdmap()
{
	register int s;

	s = intoff();
	kmapseg(CMDSEG, (UWORD)(CMDPHYS >> 8), MMU_SYS);
	kmmulim(CMDSEG, CMDLIM);
	intrest(s);
}

/* The reserved video slots. */
static int
vidseg(seg)
register int seg;
{
	return (seg == LRSEG || seg == HRSEG0 || seg == HRSEG1);
}

/* Logical segments that never take identity heap. */
static int
fixedseg(seg)
register int seg;
{
	return (seg <= CMDSEG || seg == KTEXTSEG || seg == KDATASEG ||
	    seg == LRSEG || seg == STACKSEG);
}

/* The next unmapped relocation slot, or -1.  Segments from the heap's
 * first page up are identity segments. */
static int
relocslot()
{
	register int seg;

	for (seg = RELOCSEG0; seg <= RELOCSEGN; seg++)
		if (seg < (int)(kendpage >> 8) && kmmutab[seg] == MMU_NONE)
			return (seg);
	return (-1);
}

/*
 * Place one RAM page: at its identity segment, else in a relocation slot,
 * else (out of slots) in the physical-only pool.  The page the stack
 * segment ends in is heap from that end up.
 */
static void
rampage(pg)
int pg;
{
	register int seg;
	ULONG low;

	if (pg < (int)(kendpage >> 8) || pg == LRPHYS || pg == HRPHYS1)
		return;
	low = 0;
	if (pg == (int)(kendpage >> 8))
		low = (ULONG)(kendpage & 0xff) << 8;
	seg = pg;
	if (pg >= NMMUSEG || fixedseg(pg)) {
		if ((seg = relocslot()) < 0) {
			kfreephys(pg);
			PhysPages++;
			return;
		}
		nreloc++;
	}
	kmapseg(seg, (UWORD)(pg << 8), MMU_SYS);
	kaddmem((APTR)SEGBASE(seg), low, 0x10000L,
	    (UWORD)(MEMF_PUBLIC | MEMF_UPPER), 0, "ram");
	if (low != 0)
		HeapTail = (UWORD)(0x10000L - low);
	else
		HeapPages++;
}

/* Place every page of a run list of (first page, count). */
static void
ramruns(r, n)
register struct biram *r;
int n;
{
	register int i, pg;

	for (i = 0; i < n; i++) {
		if (r[i].br_count == 0)
			continue;
		xKPrintf("mach: ram %x-%x\n", r[i].br_first,
		    r[i].br_first + r[i].br_count - 1);
		for (pg = r[i].br_first;
		    pg < r[i].br_first + r[i].br_count; pg++)
			rampage(pg);
	}
}

/*
 * Size the stack segment from kstkcfg and the n runs at r.  A larger one
 * moves up over the start of the heap, whose RAM must be in the kernel's
 * run: the live stack is copied to the same offsets in the new RAM and the
 * descriptor reloaded (kstkmove), so its low end drops by the growth.
 */
static void
stkgrow(r, n)
register struct biram *r;
int n;
{
	register int i;
	register UWORD kb, grow, pg;

	if ((kb = kstkcfg.sc_kb) == 0 || kb > STKKBMAX)
		kb = STKKBMAX;
	if (kb <= STKKBBOOT)
		return;
	grow = (kb - STKKBBOOT) << 2;
	pg = kmmutab[STACKSEG] + grow;
	for (i = 0; i < n; i++)
		if (r[i].br_first <= KPHYSPAGE &&
		    ((pg + 0xff) >> 8) < r[i].br_first + r[i].br_count)
			break;
	if (i == n) {
		xKPrintf("mach: no RAM for a %u KB stack segment\n", kb);
		return;
	}
	kstkmove(pg, (kstklow >> 8) - 1 - grow, kwinmap(0, (ULONG)pg << 8));
	kwinunmap(0);
	kmmutab[STACKSEG] = pg;
	kstklow -= grow << 8;
}

/*
 * Build the logical address space from the ROM facts con_init copied:
 * every RAM page is placed and every descriptor without a mapping,
 * segments 0 and 1 included, is made CPU-inhibit.  Kernel text, data and
 * the stack segment keep kboot's and crt.s's mappings, and the display
 * console its card's.  VI is off.
 */
void
mach_mem()
{
	register int seg;
	struct biram *r;
	int n;

	/* A display console mapped its card in con_init; nothing maps page
	 * 0 of a video slot. */
	for (seg = 0; seg < NMMUSEG; seg++)
		if (!vidseg(seg) || kmmutab[seg] == 0)
			kmmutab[seg] = MMU_NONE;
	kmmutab[KTEXTSEG] = kmmubase(KTEXTSEG);
	kmmutab[KDATASEG] = kmmubase(KDATASEG);
	kmmutab[STACKSEG] = kmmubase(STACKSEG);
	kstklow = (UWORD)(((kphys((APTR)end) + 0xffL) & ~0xffL) -
	    ((ULONG)kmmutab[STACKSEG] << 8));
	cmdmap();
	for (seg = 0; seg < sizeof(physmap); seg++)
		physmap[seg] = 0;
	HeapPages = HeapTail = PhysPages = PhysFree = 0;
	nreloc = 0;

	n = 0;
	if (bivalid() && bootinfo.bi_nram != 0) {
		xKPrintf("mach: kboot memory map, %u runs\n", bootinfo.bi_nram);
		r = bootinfo.bi_ram;
		n = (int)bootinfo.bi_nram;
	} else if (romrun.br_count != 0) {
		xKPrintf("mach: no kboot memory map; ROM range %x-%x\n",
		    romrun.br_first, romrun.br_first + romrun.br_count - 1);
		r = &romrun;
		n = 1;
	} else
		xKPrintf("mach: no memory map\n");
	if (n != 0)
		stkgrow(r, n);
	kendpage = kmmutab[STACKSEG] + 0x100;
	xKPrintf("mach: stack segment %u KB from %x\n",
	    (UWORD)((0x10000L - (kstklow & 0xfc00)) >> 10), kstklow);
	if (n != 0)
		ramruns(r, n);
	for (seg = 0; seg < NMMUSEG; seg++)
		if (kmmutab[seg] == MMU_NONE)
			kmapseg(seg, 0, MMU_INHIBIT);
	xKPrintf("mach: %u heap pages (%u relocated) and %u bytes, %u pool\n",
	    HeapPages, nreloc, HeapTail, PhysPages);
	kaddmem((APTR)SEGBASE(STACKSEG), (ULONG)kstklow,
	    (ULONG)(kstkcfg.sc_root - IDLESTKSIZE),
	    (UWORD)(MEMF_PUBLIC | MEMF_BANK0), -10, "stack");
}
