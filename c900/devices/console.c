/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * textcon.device and the kernel half of the display console.  Upstream
 * has no console device, so the model is classic Amiga console.device on
 * one fixed screen; docs/internals.md gives the contract and the
 * departures.
 *
 * kvidinit probes and maps the card bi_console names at cold start,
 * before the memory map.  Drawing is a display module's (<display.h>),
 * loaded from the boot volume after the tick starts.  Until it attaches,
 * the console bytes a card would have shown are kept in bootlog, and the
 * module draws them first.  After that every console byte, the kernel's
 * (con_putc) and CMD_WRITE's, goes through the module's cd_Out, which is
 * told whether it may draw now, must queue (interrupt level, VI off) or
 * must draw at once (the system has stopped).  Input is polled: con_getc's
 * serial line and, with a card, the keyboard, drained by a tick server
 * into a ring while the device is open.
 *
 * framebuf.resource describes an HR or LC card's bitmap and lends it to
 * one owner at a time (AllocFB, FreeFB).  While it is lent the console
 * is detached as before its module attached: bytes go to the serial line
 * and into bootlog, emptied at the loan.  The return resets the module's
 * card and emulator (cd_Reset) and draws the ring; kvidstop takes the
 * card back the same way.  A Task tests conon under Forbid and holds it
 * while it draws, and interrupt level only queues, so no draw is in
 * progress or starts once AllocFB has cleared conon.
 */
#include "kernel.h"
#include "display.h"
#include <bootinfo.h>
#include <devices/textcon.h>
#include <resources/framebuf.h>

extern struct bootinfo bootinfo;

#define CONRING		64	/* input bytes buffered */
#define POLLMAX		8	/* input bytes taken per tick */
#define WRBURST		32	/* output bytes per piece */
#define BOOTLOG		1024	/* console bytes kept for the display */
#define FCW_VIE		0x1000
#define CONPRI		80	/* tick server, after timer.device */

struct ConBase {
	struct Device cb_Device;
	struct List cb_Reads;
	struct Interrupt cb_Tick;
	UWORD cb_Head;
	UWORD cb_Count;
	UBYTE cb_Ring[CONRING];
};

static struct {
	struct JumpVec lv_Vec[6];
	struct ConBase lv_Base;
} conbaselib;
#define conbase conbaselib.lv_Base
static char conname[] = TEXTCONNAME;

static void conreply();

/* The card and, once attached, the module's entry. */
static struct ConDisplay condisp = {
	CDC_NONE, 0, conreply, con_bell, NULL, NULL, NULL
};
static UWORD conon;		/* the display gets console output */
static UWORD conatt;		/* the module is attached */
static char *fbowner;		/* the bitmap's borrower, or NULL */
static UWORD constop;		/* the system has stopped */
static UWORD logdone;		/* bootlog replayed: keep no more */
static UWORD loghead;		/* next byte of bootlog */
static UWORD logwrap;		/* bootlog has wrapped */
static UWORD kvn;		/* bytes bootlog holds for kvidflush */
static UBYTE bootlog[BOOTLOG];

/* The modules that draw on each card, and the card's other modules
 * (gameport.device: V7 names hold 14 bytes). */
static char *conmods[] = { NULL, "lr.display", "hr.display", "lc.display" };
static char *cardmods[] = { NULL, NULL, "gameport.dev", NULL };

#define STD(ior)	((struct IOStdReq *)(ior))
#define FIRST(l)	((l)->lh_Head->ln_Succ != NULL ? \
			    (struct IORequest *)(l)->lh_Head : \
			    (struct IORequest *)NULL)

/* LR: the CRTC's cursor address registers hold what is written, read
 * back last after another register was selected, so an echoing bus
 * cannot pass (R14 is 6 bits); and two complementary patterns in the
 * first two text cells, restored.  A failure leaves LRSEG inhibited. */
#define CRTC_IDX	0x0410
#define CRTC_DATA	0x0412
#define CRTC_RDATA	0x0413
#define R_CURPOS	14

static void
crtc(r, v)
int r, v;
{
	outb(CRTC_IDX, r);
	outb(CRTC_DATA, v);
}

static int
crtcrd(r)
int r;
{
	outb(CRTC_IDX, r);
	return (inb(CRTC_RDATA) & 0xff);
}

/* Two complementary pairs at w, written before either is read; restored. */
static int
patok(w)
register UWORD *w;
{
	UWORD s0, s1;
	register int i, ok;

	s0 = w[0];
	s1 = w[1];
	ok = 1;
	for (i = 0; i < 2 && ok; i++) {
		w[0] = i ? 0xaa55 : 0x55aa;
		w[1] = i ? 0x55aa : 0xaa55;
		ok = w[0] == (i ? 0xaa55 : 0x55aa) &&
		    w[1] == (i ? 0x55aa : 0xaa55);
	}
	w[0] = s0;
	w[1] = s1;
	return (ok);
}

static int
lrprobe()
{
	register int i, ok;
	int h, l;

	kmapseg(LRSEG, (UWORD)(LRPHYS << 8), MMU_SYS);
	ok = patok((UWORD *)SEGBASE(LRSEG));
	h = crtcrd(R_CURPOS);
	l = crtcrd(R_CURPOS + 1);
	for (i = 0; i < 2 && ok; i++) {
		crtc(R_CURPOS, i ? 0x2a : 0x15);
		crtc(R_CURPOS + 1, i ? 0xa5 : 0x5a);
		ok = crtcrd(R_CURPOS + 1) == (i ? 0xa5 : 0x5a) &&
		    crtcrd(R_CURPOS) == (i ? 0x2a : 0x15);
	}
	crtc(R_CURPOS, h);
	crtc(R_CURPOS + 1, l);
	if (!ok)
		kmapseg(LRSEG, 0, MMU_INHIBIT);
	return (ok);
}

/* HR: not RAM in kboot's map at physical 0x3E; bits 12:10 of the X
 * port read as 0 at 0x0400 and 0x0404; the bitmap's first words hold
 * patterns.  Only then is HRSEG1 mapped.  A 0x0404 read is the X port
 * with the JP3 M/C strap in bits 1:0, which hrstrap maps to a mode (0:
 * no card).  The strap does not tell 800 from 1024 mono lines; kboot's
 * BF_HR1024 does.  3 is the 1024x1024 code of the card's documentation,
 * which the core does not produce.  A nonzero khrcfg.hc_mode, patched
 * into the image (tools/mkdisk.py --hrmode), names the jumper and wins
 * over both.  CDM_ mode, or 0. */
#define HRXPORT		0x0400
#define HRSTRAP		0x0404
#define HRMBZ		0x1c00

static UBYTE hrstrap[4] = { 0, CDM_HRCOLOR, CDM_HR800, CDM_HR1024 };

struct hrcfg {
	char hc_magic[8];
	UWORD hc_mode;			/* CDM_, or 0: the strap */
};

struct hrcfg khrcfg = {
	{ 'E', '9', 'H', 'R', 'M', 'O', 'D', 'E' },
	0
};

static int
hrprobe()
{
	register int i;
	register struct biram *r;
	register UWORD id;
	register int m;

	for (i = 0; i < bootinfo.bi_nram; i++) {
		r = &bootinfo.bi_ram[i];
		if (r->br_count != 0 && r->br_first <= HRPHYS0 &&
		    HRPHYS0 < r->br_first + r->br_count)
			return (0);
	}
	id = inw(HRSTRAP);
	m = hrstrap[id & 3];
	if (m == CDM_HR800 && (bootinfo.bi_flags & BF_HR1024))
		m = CDM_HR1024;
	if (khrcfg.hc_mode != 0)
		m = khrcfg.hc_mode;
	if ((id & HRMBZ) != 0 || m == 0 || (inw(HRXPORT) & HRMBZ) != 0)
		return (0);
	kmapseg(HRSEG0, (UWORD)(HRPHYS0 << 8), MMU_SYS);
	if (!patok((UWORD *)SEGBASE(HRSEG0))) {
		kmapseg(HRSEG0, 0, MMU_INHIBIT);
		return (0);
	}
	kmapseg(HRSEG1, (UWORD)(HRPHYS1 << 8), MMU_SYS);
	return (m);
}

/* LC: write 0x3E to the ID port, so an empty bus cannot echo, and read
 * 0xC1; status bits 0x18 read 0; R12 holds 0x55 and 0xAA, left 0.  The
 * status read at the end releases the 8568's INTR.  VI off. */
#define LC_ADDR		0x0181
#define LC_DATA		0x0183
#define LC_ID		0x01ff
#define LC_ID64		0xc1
#define LC_ZERO		0x18
#define LC_R12		12

static int
lcprobe()
{
	register int ok;

	outb(LC_ID, ~LC_ID64 & 0xff);
	ok = (inb(LC_ID) & 0xff) == LC_ID64 && (inb(LC_ADDR) & LC_ZERO) == 0;
	if (ok) {
		outb(LC_ADDR, LC_R12);
		outb(LC_DATA, 0x55);
		ok = (inb(LC_DATA) & 0xff) == 0x55;
		outb(LC_DATA, 0xaa);
		ok = ok && (inb(LC_DATA) & 0xff) == 0xaa;
		outb(LC_DATA, 0);
		inb(LC_ADDR);
	}
	return (ok);
}

/*
 * Cold start, VI off, before the memory map: find the card a valid
 * bi_console names (BI_CON_VID and BI_CON_ANY, from older loaders: HR,
 * then LR) and map it; mach_mem keeps the mapping.  SER or a card that
 * fails its probe leaves the console on serial.
 */
void
kvidinit()
{
	register int con, m;

	if (!bivalid())
		return;
	con = bootinfo.bi_console;
	if (con == BI_CON_HR || con == BI_CON_ANY || con == BI_CON_VID) {
		if ((m = hrprobe()) != 0) {
			condisp.cd_Card = CDC_HR;
			condisp.cd_Mode = m;
			return;
		}
		if (con == BI_CON_HR)
			return;
	}
	if ((con == BI_CON_LR || con == BI_CON_ANY || con == BI_CON_VID) &&
	    lrprobe())
		condisp.cd_Card = CDC_LR;
	else if (con == BI_CON_LC && lcprobe())
		condisp.cd_Card = CDC_LC;
}

/* The boot-volume file of the module that draws on the card (0) or of
 * the card's other module (1), or NULL. */
char *
kvidmod(i)
int i;
{
	return ((i ? cardmods : conmods)[condisp.cd_Card]);
}

static void fbback();

/* kpanic, dead-end alerts and traps, VI off: nothing runs after the
 * message.  A lent bitmap comes back without asking. */
void
kvidstop()
{
	constop = 1;
	if (fbowner != NULL)
		fbback();
}

/* How the display may take a byte now. */
static int
conhow()
{
	register int s;

	if (constop)
		return (CDO_STOP);
	s = intoff();
	intrest(s);
	return (IntDepth != 0 || (s & FCW_VIE) == 0 ? CDO_QUEUE : CDO_DRAW);
}

/* Draw what kvidputc held, unless at interrupt level or with VI off. */
void
kvidflush()
{
	register UWORD n;
	int how;

	if ((how = conhow()) == CDO_QUEUE)
		return;
	if (how == CDO_DRAW)
		xForbid();
	n = kvn;
	kvn = 0;
	SysFlags &= ~SF_CONOUT;
	if (n != 0 && conon)
		(*condisp.cd_Out)(bootlog, n, how);
	if (how == CDO_DRAW)
		xPermit();
}

/* con_putc's copy for the display; any context.  While attached, bootlog
 * is free: a Task holding Forbid collects a line there, drawn at the
 * newline or its Permit. */
void
kvidputc(c)
int c;
{
	UBYTE b;
	int s, how;

	how = conhow();
	if (conon && how == CDO_DRAW && ThisTask->tc_TDNestCnt >= 0) {
		bootlog[kvn++] = c;
		SysFlags |= SF_CONOUT;
		if (c == '\n' || kvn == BOOTLOG)
			kvidflush();
		return;
	}
	if (how == CDO_DRAW)
		xForbid();
	if (conon) {
		if (how != CDO_QUEUE)
			kvidflush();
		b = c;
		(*condisp.cd_Out)(&b, 1, how);
	} else if (condisp.cd_Card != CDC_NONE &&
	    (!logdone || fbowner != NULL)) {
		s = intoff();
		bootlog[loghead] = c;
		if (++loghead == BOOTLOG) {
			loghead = 0;
			logwrap = 1;
		}
		intrest(s);
	}
	if (how == CDO_DRAW)
		xPermit();
}

/* One character the display console's keyboard has, or -1. */
int
kvidgetc()
{
	return (condisp.cd_Card != CDC_NONE ? kbd_getc() : -1);
}

/* The boot text, oldest first; after a wrap from the first whole line. */
static void
replay()
{
	register UWORD i;

	if (logwrap) {
		for (i = loghead; i < BOOTLOG && bootlog[i++] != '\n'; )
			;
		if (i < BOOTLOG)
			(*condisp.cd_Out)(&bootlog[i], BOOTLOG - i, conhow());
	}
	(*condisp.cd_Out)(bootlog, loghead, conhow());
}

/* VI off. */
static void
ringput(c)
int c;
{
	register struct ConBase *cb;
	register int i;

	cb = &conbase;
	i = cb->cb_Head + cb->cb_Count;
	if (i >= CONRING)
		i -= CONRING;
	if (cb->cb_Count == CONRING) {
		if (++cb->cb_Head == CONRING)
			cb->cb_Head = 0;
		cb->cb_Count--;
	}
	cb->cb_Ring[i] = c;
	cb->cb_Count++;
}

/* The emulator's reports (cursor position, window bounds) are input. */
static void
conreply(v, s)
APTR v;
register char *s;
{
	int st;

	st = intoff();
	while (*s != '\0')
		ringput(*s++ & 0xff);
	intrest(st);
}

static void
complete(ior, err)
register struct IORequest *ior;
int err;
{
	ior->io_Error = err;
	xTermIO(ior);
}

/* VI off.  Complete the first read with what the ring holds. */
static int
rxfill(ior)
register struct IORequest *ior;
{
	register struct ConBase *cb;
	register struct IOStdReq *r;
	register ULONG n;

	cb = &conbase;
	r = STD(ior);
	if (cb->cb_Count == 0 && r->io_Length != 0)
		return (0);
	for (n = 0; n < r->io_Length && cb->cb_Count != 0; n++) {
		r->io_Data[(unsigned)n] = cb->cb_Ring[cb->cb_Head];
		if (++cb->cb_Head == CONRING)
			cb->cb_Head = 0;
		cb->cb_Count--;
	}
	r->io_Actual = n;
	return (1);
}

static int
contick(cb, n)
register struct ConBase *cb;
UWORD n;
{
	register struct IORequest *ior;
	register int i, c;

	if (cb->cb_Device.dd_Library.lib_OpenCnt == 0)
		return (0);
	for (i = 0; i < POLLMAX && cb->cb_Count < CONRING; i++) {
		if ((c = con_getc()) < 0)
			break;
		ringput(c);
	}
	while (cb->cb_Count != 0 && (ior = FIRST(&cb->cb_Reads)) != NULL) {
		xRemove(&ior->io_Message.mn_Node);
		rxfill(ior);
		complete(ior, 0);
	}
	return (0);
}

/* n bytes of p, or up to the NUL when n is -1, to the display, else
 * to the serial line (con_putc, LF as CR LF). */
static ULONG
conwrite(p, n)
register UBYTE *p;
long n;
{
	register unsigned k, lim;
	ULONG done;

	done = 0;
	if (conon)
		kvidflush();
	while (n != 0) {
		lim = n > 0 && n < WRBURST ? (unsigned)n : WRBURST;
		if (n < 0)
			for (k = 0; k < lim && p[k] != '\0'; k++)
				;
		else
			k = lim;
		if (k == 0)
			break;
		if (conon)
			(*condisp.cd_Out)(p, k, conhow());
		else
			for (lim = 0; lim < k; lim++)
				con_putc(p[lim]);
		p += k;
		done += k;
		if (n > 0)
			n -= k;
	}
	return (done);
}

/* Attach (on) or detach the module's display.  The first attach draws
 * the boot text; bytes from interrupts meanwhile queue behind it. */
static void
consetdisp(on)
int on;
{
	register int s, first;

	xForbid();
	s = intoff();
	first = 0;
	if (!on)
		conon = conatt = kvn = 0;
	else if (condisp.cd_Out != NULL && !conatt) {
		conatt = 1;
		if (fbowner == NULL) {
			conon = 1;
			first = !logdone;
			logdone = 1;
		}
	}
	intrest(s);
	if (first)
		replay();
	xPermit();
}

static void
cbegin(dev, ior)
struct Device *dev;
register struct IORequest *ior;
{
	register struct ConBase *cb;
	int err, s;

	IOBEGIN(ior);
	cb = &conbase;
	err = 0;
	switch (ior->io_Command) {
	case CMD_WRITE:
		xForbid();
		STD(ior)->io_Actual = conwrite(STD(ior)->io_Data,
		    (long)STD(ior)->io_Length);
		xPermit();
		break;
	case CMD_READ:
		STD(ior)->io_Actual = 0;
		s = intoff();
		if (IsListEmpty(&cb->cb_Reads) && rxfill(ior)) {
			intrest(s);
			break;
		}
		ior->io_Flags &= ~IOF_QUICK;
		xAddTail(&cb->cb_Reads, &ior->io_Message.mn_Node);
		intrest(s);
		return;
	case CD_ASKDISPLAY:
		if (STD(ior)->io_Length < sizeof(APTR)) {
			err = IOERR_BADLENGTH;
			break;
		}
		*(struct ConDisplay **)STD(ior)->io_Data = &condisp;
		STD(ior)->io_Actual = sizeof(APTR);
		break;
	case CD_SETDISPLAY:
		consetdisp(STD(ior)->io_Length != 0);
		break;
	case CMD_CLEAR:
		s = intoff();
		cb->cb_Head = cb->cb_Count = 0;
		intrest(s);
		break;
	default:
		err = IOERR_NOCMD;
	}
	complete(ior, err);
}

static LONG
cabort(dev, ior)
struct Device *dev;
register struct IORequest *ior;
{
	register struct Node *n;
	LONG r;
	int s;

	r = -1;
	s = intoff();
	for (n = conbase.cb_Reads.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (n == &ior->io_Message.mn_Node) {
			xRemove(n);
			complete(ior, IOERR_ABORTED);
			r = 0;
			break;
		}
	intrest(s);
	return (r);
}

static void
copen(dev, ior, unit, flags)
struct Device *dev;
register struct IORequest *ior;
ULONG unit, flags;
{
	if (unit != CONU_STANDARD || flags > CONFLAG_NODRAW_ON_NEWSIZE) {
		ior->io_Error = IOERR_OPENFAIL;
		return;
	}
	if (ior->io_Message.mn_Length < sizeof(struct IOStdReq)) {
		ior->io_Error = IOERR_BADLENGTH;
		return;
	}
	dev->dd_Library.lib_OpenCnt++;
}

/* Closing with a read of this binding still queued is misuse, as in
 * serial.device. */
static APTR
cclose(dev, ior)
struct Device *dev;
struct IORequest *ior;
{
	register struct Node *n;
	struct MsgPort *port;
	int s;

	port = ior->io_Message.mn_ReplyPort;
	s = intoff();
	for (n = conbase.cb_Reads.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ)
		if (((struct Message *)n)->mn_ReplyPort == port)
			kpanic("textcon.device: CloseDevice with a read pending");
	intrest(s);
	dev->dd_Library.lib_OpenCnt--;
	return (NULL);
}

/*
 * framebuf.resource.  fbmodes holds the cards' modes: HR in CDM_ order,
 * then the 8-byte tag E9LCMODE in an entry of its own, then LC (8568
 * bitmap, not CPU-mapped), whose height mkdisk.py --lcmode sets on the
 * disk's copy: 200, or 400 interlaced.  lc.display draws in the same mode.
 */
#define HRHALF0		((APTR)((ULONG)HRSEG0 << 24))
#define HRHALF1		((APTR)((ULONG)HRSEG1 << 24))
#define FBM_LC		4

static struct FBMode fbmodes[] = {
	{ 1024, 800, 1, 128, FBF_MONO, 0xFFFF, 0, { HRHALF0, HRHALF1 },
	    0x20000L },
	{ 1024, 1024, 1, 128, FBF_MONO, 0xFFFF, 0, { HRHALF0, HRHALF1 },
	    0x20000L },
	{ 640, 400, 4, 320, FBF_IRGB, 204, 256, { HRHALF0, HRHALF1 },
	    0x20000L },
	{ 0x4539, 0x4c43, 0x4d4f, 0x4445 },
	{ 640, 200, 1, 80, FBF_MONO, 0xFFFF, 0, { NULL, NULL }, 0x10000L }
};

static struct FBInfo fbinfo;
static char fbname[] = FRAMEBUFNAME;

static struct {
	struct JumpVec lv_Vec[3];
	struct Library lv_Base;
} fblib;

static struct FBInfo *
fbget(base)
APTR base;
{
	return (&fbinfo);
}

static char *
fballoc(base, name)
APTR base;
char *name;
{
	register char *o;
	int s;

	xForbid();
	if ((o = fbowner) == NULL) {
		s = intoff();
		fbowner = name;
		conon = kvn = 0;
		if (logdone)
			loghead = logwrap = 0;
		intrest(s);
	}
	xPermit();
	return (o);
}

/* Forbid or VI off, lent: the card back to the console, cleared, and
 * the text printed meanwhile drawn. */
static void
fbback()
{
	int s;

	if (!conatt || condisp.cd_Reset == NULL) {
		fbowner = NULL;
		return;
	}
	(*condisp.cd_Reset)();
	s = intoff();
	fbowner = NULL;
	conon = 1;
	logdone = 1;
	intrest(s);
	replay();
}

static void
fbfree(base)
APTR base;
{
	xForbid();
	if (fbowner != NULL)
		fbback();
	xPermit();
}

static APTR fbfuncs[] = {
	(APTR)fbget, (APTR)fballoc, (APTR)fbfree, (APTR)-1L
};

/* The resource, for a card with a bitmap. */
static void
fbinit()
{
	register int m;

	if (condisp.cd_Card == CDC_HR)
		m = condisp.cd_Mode - CDM_HR800;
	else if (condisp.cd_Card == CDC_LC)
		m = FBM_LC;
	else
		return;
	fbinfo.fbi_Card = condisp.cd_Card;
	fbinfo.fbi_NumModes = 1;
	fbinfo.fbi_Modes = &fbmodes[m];
	kinitlib(&fblib.lv_Base, fbname, fbfuncs);
	fblib.lv_Base.lib_Node.ln_Type = NT_RESOURCE;
	xAddResource((APTR)&fblib.lv_Base);
}

static APTR conbasefuncs[] = {
	(APTR)copen, (APTR)cclose, (APTR)knull, (APTR)knull,
	(APTR)cbegin, (APTR)cabort, (APTR)-1L
};

void kconinit();

struct Resident contag = {
	RTC_MATCHWORD, &contag, (APTR)(&contag + 1), RTF_COLDSTART, 1,
	NT_DEVICE, 0, conname, NULL, (APTR)kconinit
};

void
kconinit()
{
	register struct ConBase *cb;

	cb = &conbase;
	xNewList(&cb->cb_Reads);
	cb->cb_Tick.is_Node.ln_Pri = CONPRI;
	cb->cb_Tick.is_Node.ln_Name = conname;
	cb->cb_Tick.is_Data = (APTR)cb;
	cb->cb_Tick.is_Code = contick;
	xAddIntServer((UWORD)INTV_TICK, &cb->cb_Tick);
	kinitlib(&cb->cb_Device.dd_Library, conname, conbasefuncs);
	xAddDevice(&cb->cb_Device);
	fbinit();
}
