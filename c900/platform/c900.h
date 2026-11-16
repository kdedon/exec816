/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C900 memory layout, shared by C and preprocessed assembly.  Defines only.
 * docs/platform.md describes the layout and the reasons for it.
 */
#ifndef C900_H
#define C900_H

/*
 * Logical segments.  The one Z8010 has descriptors 0x00-0x3F; 0x40-0x7F are
 * never translated.  RAM below physical 0x40 is mapped at logical = physical
 * (identity), so a heap pointer's segment is its physical segment.  The
 * segments below are fixed and never identity heap.
 */
#define NULLSEG		0x00		/* CPU-inhibit: NULL traps */
#define HRSEG1		0x01		/* reserved: HR bitmap, second half (phys 0x3F) */
#define WINSEG0		0x02		/* window 0: Task level, under Forbid */
#define WINSEG1		0x03		/* window 1: interrupt level */
#define RELOCSEG0	0x04		/* relocation slots, CMDSEG excepted: */
#define RELOCSEGN	0x09		/* RAM without an identity segment */
#define CMDSEG		0x06		/* disk command area: writable alias */
#define KTEXTSEG	0x30		/* kernel text (kboot) */
#define KDATASEG	0x31		/* kernel data (kboot) */
#define LRSEG		0x37		/* reserved: LR text RAM, identity */
#define HRSEG0		0x3e		/* reserved: HR bitmap, first half, identity */
#define STACKSEG	0x3f		/* SS: every Task stack */
#define NMMUSEG		0x40		/* descriptors */

/* Descriptor attributes. */
#define MMU_RO		0x01		/* read-only */
#define MMU_SYS		0x02		/* system only */
#define MMU_INHIBIT	0x04		/* CPU-inhibit: any access traps */
#define MMU_DOWN	0x20		/* grows down: valid pages limit..0xff */
#define MMU_NONE	0xffff		/* kmmutab: no mapping */

/* Physical pages (phys >> 16).  The kernel image starts at page 0x08; the
 * stack segment is the 64 KB after it, from the next 1 KB boundary (crt.s),
 * and the heap starts where the stack segment ends. */
#define KPHYSPAGE	0x08
#define LRPHYS		0x37		/* LR text RAM */
#define HRPHYS0		0x3e		/* HR bitmap, 128 KB */
#define HRPHYS1		0x3f

/* The disk controllers' command area: fixed by their firmware at the start
 * of the kernel's page, under the text pad (crt.s), which is read-only
 * through KTEXTSEG.  CMDSEG maps only these bytes, writable; the limit
 * traps any access past them. */
#define CMDPHYS		0x080000L
#define CMDLEN		0x400		/* text pad: blocks, then the buffer */
#define CMDLIM		((CMDLEN >> 8) - 1)	/* last 256-byte block */
#define CMD_HD		0x000		/* hard-disk command block */
#define CMD_FD		0x010		/* floppy command block */
#define CMD_BUF		0x200		/* 512-byte aligned bounce/parameter buffer */

/* Stack segment offsets.  The descriptor grows down (MMU_DOWN): it ends at
 * a 1 KB boundary and its RAM starts at the 256-byte boundary after bss,
 * at offset kstklow, a 1 KB step plus 0 to 0x300.  Its limit is the
 * page below, so a write there takes the segment trap (write warning) and
 * any lower access traps.  The heap starts where the segment ends.  The
 * root Task runs on the boot stack, from kstkcfg.sc_root (ROOTSTKLOW by
 * default) to ROOTSTKTOP; the idle Task's IDLESTKSIZE bytes lie below it,
 * and below them the MEMF_STACK pool.  crt.s maps the boot size;
 * mach_mem() may then move the segment up over the heap's start, which
 * lowers kstklow to as little as STKSEGMIN (STKKBMAX KB of segment). */
#define STKSEGPG	0x80		/* STKSEGLOW >> 8, for crt.s (loutsize.py checks) */
#define STKSEGLOW	0x8000
#define STKSEGMIN	0x0400		/* the lowest 1 KB step a moved segment starts at */
#define STKKBBOOT	32		/* (0x10000 - STKSEGLOW) / 1024 */
#define STKKBMAX	63		/* (0x10000 - STKSEGMIN) / 1024 */
#define ROOTSTKTOP	0xfffe
#define ROOTSTKLOW	0xf000		/* the default kstkcfg.sc_root */
#define IDLESTKSIZE	0x200
#define STKPOOLLOW	STKSEGMIN	/* lowest kstklow */

/* Interrupt headroom every Task stack must keep below its lowest frame.
 * Handlers run on the interrupt stack; only the 36-byte trap frame lands on
 * the Task stack. */
#define IRQHEADROOM	256

#define FCW_TASK	0xD800		/* segmented, system, VI and NVI enabled */
#define FCW_EPA		0x2000		/* extended instructions reach the EPU */

#endif
