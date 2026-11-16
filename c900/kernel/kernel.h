/* SPDX-License-Identifier: GPL-3.0-only */
/* Exec900 kernel-private declarations. */
#ifndef KERNEL_H
#define KERNEL_H

#include <exec/exec.h>
#include "c900.h"

/* Scheduler state. */
extern struct Task *ThisTask;
extern struct List SemaphoreList;
extern struct List DeviceList;
extern struct Task IdleTask;
extern struct Task RootTask;
extern UWORD SysFlags;
extern UWORD IntDepth;
extern WORD Quantum;
extern UWORD Sleepers;

#define SF_SAR		0x0001		/* a switch is wanted */
#define SF_CONOUT	0x0002		/* console text held for Permit */
#define QUANTUM		4		/* ticks per slice */
#define TICKS_PER_SEC	100

/* Private signal bit Sleep waits on. */
#define SIGB_SLEEP	2
#define SIGF_SLEEP	(1L << SIGB_SLEEP)

/* Sleep deadline, kept in the private pointer field. */
#define TC_WAKE(t)	(*(ULONG *)&(t)->tc_ExecPrivate)

/* Pointer pieces.  Pointer arithmetic changes only the offset word. */
#define POFF(p)		((ULONG)(p) & 0xFFFFL)
#define PSEG(p)		((ULONG)(p) & 0x7F000000L)
#define SEGBASE(s)	((ULONG)(s) << 24)
#define MKPTR(b, o)	((APTR)((ULONG)(b) | (ULONG)(o)))

/* Machine layer (platform/). */
int inb();
void outb();
void outw();
int inw();
void kwmove();
void kbmove();
void kwfill();
int intoff();
void intrest();
void haltcpu();
void kresched();
void emustop();
void mapseg();
void con_putc();
int con_getc();

/* The console line's transmit queue (platform/machine.c), set up by
 * serial.device: kcontx loads queued bytes the transmitter takes, VI
 * off; kcondrain sends them all, polled. */
#define KTXQ		256
extern UBYTE *ktxq;
extern UWORD ktxcnt;
int kcontx();
void kcondrain();

/* Keyboard (devices/keyboard.c): the console's next key press or -1,
 * never waits; the write-only system latch at 0x0205.  The console's
 * keymap (devices/conkeymap.c): the next character or -1. */
int kbd_getraw();
void ksetlatch();
int kbd_getc();

void tick_init();
UWORD mach_tickconst();
UWORD stk_frame();
void mach_mem();
void con_init();
UWORD mach_serial();
int mach_cpu6();

/* Console bell (devices/beep.c): a short tone; safe in any context. */
void con_bell();

/* Display console (devices/console.c): con_init finds and maps the card;
 * con_putc and con_getc reach it in any context; kbootmods loads the
 * modules kvidmod names: the one that draws (0), the card's other (1). */
void kvidinit();
void kvidputc();
void kvidflush();
int kvidgetc();
void kvidstop();
char *kvidmod();
int kserbusy();

/* Logical address space (platform/machine.c; docs/platform.md). */
extern UWORD kmmutab[];
extern UWORD HeapPages;
extern UWORD HeapTail;
extern UWORD kstklow;
void kstkzero();

/* Boot stack configuration, which tools patch in the image, found by
 * sc_magic and sc_rmagic (tools/mkdisk.py --stack, --rootstack): the
 * stack segment's size in KB, STKKBBOOT to STKKBMAX, or 0 for STKKBMAX;
 * and the root stack's low
 * offset, 0x10000 less its size. */
struct stkcfg {
	char sc_magic[8];
	UWORD sc_kb;
	char sc_rmagic[4];
	UWORD sc_root;
};
extern struct stkcfg kstkcfg;
extern UWORD PhysPages;
extern UWORD PhysFree;
UWORD kmmubase();
void kmmulim();
void kmapseg();
ULONG kphys();
int bivalid();
int kallocphys();
void kfreephys();
APTR kwinmap();
void kwinunmap();

/* Kernel internals. */
UWORD kswitch();
void ktick();
UWORD kintr();
extern UWORD kintstk;
extern UWORD IntFrame;
void kintinit();
void kintreport();
void kready();
void kmaybe();
void ksignal();
void kpanic();
UWORD kfault();
void ktrapdef();
APTR kalloc();
void kmhinit();
void kmemclr();
void kdeallocate();
struct MemHeader *kaddmem();
void kfinal();
void kidle();
void kinitidle();
void kinitroot();

/* The Z8070 EPU (kernel/epu.c). */
extern struct Task *EpuOwner;
extern APTR kepurec;
void kepuinit();
int kepuswitch();

/* Libraries (kernel/libraries.c).  A vector is the Z8001 "jp addr" with a
 * long segmented address: 0x5E08, then the pointer with bit 31 set. */
struct JumpVec {
	UWORD jv_Op;
	APTR jv_Fn;
};

#define JP_OP		0x5E08
#define LONGSEG		0x80000000L
/* Library vector lvo of lib. */
#define LIBVEC(lib, lvo)	((APTR (*)())((char *)(lib) + (lvo)))

/* The start of a driver's BeginIO, as on the Amiga: ior is pending. */
#define IOBEGIN(ior)	((ior)->io_Error = 0, \
			    (ior)->io_Message.mn_Node.ln_Type = NT_MESSAGE)

extern struct ExecBase ExecLib;		/* *SysBase, after its vectors */

/* ExecBase fields the kernel keeps under their own names.  MemList is
 * written ExecLib.MemList: struct MemList shares its name. */
#ifdef EXEC_KERNEL
#define TaskTrapCode	ExecLib.TaskTrapCode
#define PortList	ExecLib.PortList
#define TaskReady	ExecLib.TaskReady
#define TaskWait	ExecLib.TaskWait
#define LibList		ExecLib.LibList
#define DeviceList	ExecLib.DeviceList
#define ResourceList	ExecLib.ResourceList
#define LastAlert	ExecLib.LastAlert
#define TickCount	ExecLib.ex_TickCount
#endif
extern struct Resident *ResModules[];
void kaddlib();

/* Hunk loading and boot modules (kernel/loadseg.c, kernel/bootmods.c).
 * The boot volume is kboot's partition slot BOOTSLOT on drive 0. */
#define BOOTSLOT	14

/* An open file of the boot volume. */
struct BootFile {
	struct IOStdReq bf_Req;
	ULONG bf_Size;
	ULONG bf_Pos;
	ULONG bf_Addr[13];		/* inode block addresses */
	ULONG bf_Blk[2];		/* block in each buffer; 0 = none */
	int bf_Err;
	UBYTE bf_Buf[2][512];		/* data, indirect */
	UBYTE bf_List[512];		/* the Modules file */
};

struct BootFile *kbfinit();
void kbfdone();
int kbfopen();
LONG kbfread();
extern APTR BootFuncs[];
APTR kbootmods();
int krun();
void kinitlib();
void kexecinit();
APTR knull();
APTR xRemLibrary();

#endif
