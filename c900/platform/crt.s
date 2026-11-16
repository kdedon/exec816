/ SPDX-License-Identifier: GPL-3.0-only
/ Exec900 entry.  kboot leaves:
/   text  seg 0x30 -> phys 0x080000, read/execute
/   data  seg 0x31 -> phys 0x080000 + roundup(text, 1K), bss zeroed
/   ROM   segs 0 and 1 still mapped; segmented system mode, interrupts off
/ The linker's end_ is the end of bss.
/ The WD controller's command block and sector buffer sit at phys
/ 0x080000..0x0803FF, under the first 0x400 text bytes, so the text starts
/ with a pad.  The PSA follows it, 256-aligned.

#include "c900.h"

	.globl	start
	.globl	SS
	.globl	tsc_, tepa_, tprv_, tseg_, tnmi_, tnvi_, tvi_
	.globl	cmain_
	.globl	mapseg_, kstkmove_, kstkzero_
	.globl	end_, kstkcfg_

/ cc2 relocates every compiled frame address against SS: all stacks live in
/ this one segment.  Both bytes carry the segment number.
SS	=	0x3f3f

	.shri

pad_:
	ldar	rr2, start
	jp	(rr2)
	.blkb	CMDLEN+pad_-.

/ Program Status Area: 4-word fixed entries (reserved, FCW, PC seg, PC off),
/ then one FCW for every vectored interrupt and a PC per even vector.  The
/ PSA FCW 0xC000 keeps VI and NVI off in every handler.  A fitted EPU takes
/ the trap's first PSA read, the FCW, as the word 2 of the instruction that
/ trapped: the flags in 0xC0F0 make it an invalid opcode, which the EPU
/ drops.
psa:
	.word	0, 0, 0, 0		/ +0: reserved
	.long	0xC0F0			/ +8: extended-instruction trap
	.long	tepa_
	.long	0xC000			/ +16: privileged-instruction trap
	.long	tprv_
	.long	0xC000			/ +24: system call: Exec reschedule
	.long	tsc_
	.long	0xC000			/ +32: segment trap
	.long	tseg_
	.long	0xC000			/ +40: non-maskable interrupt
	.long	tnmi_
	.long	0xC000			/ +48: non-vectored interrupt
	.long	tnvi_
	.long	0xC000			/ +56: FCW for every vectored interrupt
/ One PC per even vector, all the common entry: kintr dispatches on the
/ identifier the CPU saved (kernel/interrupts.c).
	.long	tvi_			/ vector 0x00
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_			/ vector 0x80
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_
	.long	tvi_

start:
	ldar	rr0, psa
	ldctl	PSAPSEG, r0
	ldctl	PSAPOFF, r1

	/ The stack segment: every Task stack, the root stack included.  Its
	/ RAM starts at the first 256-byte boundary at or after the end of
	/ bss, in r1 as a page (phys >> 8): the data segment's base page plus
	/ the image's 256-byte blocks.  The base is the 1 KB boundary at or
	/ below it, less STKSEGPG, so the segment ends 1 KB-aligned and its
	/ RAM is 64 KB less STKSEGLOW, less up to 768 bytes.  It grows down,
	/ with the limit on the page below the RAM (c900.h).
	ldb	rl0, $KDATASEG
	soutb	0x01fc, rl0
	sinb	rh1, 0x08fc
	sinb	rl1, 0x08fc
	ldl	rr2, $end_
	dec	r3, $1
	srl	r3, $8
	inc	r3, $1
	add	r1, r3
	ld	r2, r1
	and	r2, $3
	sub	r1, r2
	sub	r1, $STKSEGPG
	add	r2, $[STKSEGPG-1]
	ldb	rl0, $STACKSEG
	soutb	0x01fc, rl0
	soutb	0x0ffc, rh1
	soutb	0x0ffc, rl1
	soutb	0x0ffc, rl2
	ldb	rl0, $[MMU_SYS+MMU_DOWN]
	soutb	0x0ffc, rl0

	ld	r14, $[STACKSEG*256]
	ld	r15, $ROOTSTKTOP
	sub	r13, r13
	call	cmain_
hang:
	halt
	jr	hang

/ kstkmove(page, limit, dst): move the stack segment to base page `page'
/ with `limit'.  dst is a window onto the new base, so its offsets are the
/ segment's.  Every word from SP to 0xFFFE is copied, top first (the new
/ RAM is higher and may overlap the old), then the descriptor is reloaded.
/ Nothing writes the stack between the copy and the reload, so the return
/ address and the callers' frames are at the same offsets in the new RAM.
/ VI is off.
kstkmove_:
	ldl	rr2, rr14(8)
	ld	r3, $0xfffe
	ld	r4, r14
	ld	r5, r3
	ld	r1, r3
	sub	r1, r15
	srl	r1, $1
	inc	r1, $1
	lddr	(rr2), (rr4), r1
	ld	r0, rr14(4)
	ld	r1, rr14(6)
	ldb	rh2, $STACKSEG
	ldb	rl2, $[MMU_SYS+MMU_DOWN]
	soutb	0x01fc, rh2
	soutb	0x0ffc, rh0
	soutb	0x0ffc, rl0
	soutb	0x0ffc, rl1
	soutb	0x0ffc, rl2
	ret

/ kstkzero(): clear the stack segment from the idle stack's low end
/ (kstkcfg.sc_root, at offset 14, less IDLESTKSIZE) up to the caller's
/ return address, so the root and idle stacks start as cleared as a
/ CreateTask stack (C:StackUse counts the untouched bytes).
kstkzero_:
	ld	r2, r14
	ld	r3, kstkcfg_+14
	sub	r3, $IDLESTKSIZE
	ld	r1, r15
	sub	r1, r3
	srl	r1, $1
	test	r1
	jr	z, 1f
	sub	r0, r0
	ld	(rr2), r0
	dec	r1, $1
	jr	z, 1f
	ldl	rr4, rr2
	inc	r5, $2
	ldir	(rr4), (rr2), r1
1:	ret

/ mapseg_(seg, basepage, attr): program one Z8010 descriptor with a 64K
/ limit.  basepage = phys >> 8; attr 0x02 = system read/write.
mapseg_:
	ld	r0, rr14(4)
	ld	r1, rr14(6)
	ld	r2, rr14(8)
	soutb	0x01fc, rl0
	soutb	0x0ffc, rh1
	soutb	0x0ffc, rl1
	ldb	rl3, $0xff
	soutb	0x0ffc, rl3
	soutb	0x0ffc, rl2
	ret
