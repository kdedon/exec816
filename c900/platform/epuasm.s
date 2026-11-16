/ SPDX-License-Identifier: GPL-3.0-only
/ Z8070 EPU probe and context moves.  The assembler has no EPU mnemonics,
/ so each extended instruction is its two template words (EPU ID 0):
/   0F4C/0F44 w2   store to / load from (rr4)
/   8F00|s<<4 w2   CPU <- control register s;  8F08|s<<4: register <- CPU
/ w2 for F register k, extended: 0x8000 | k << 8 | 4 (FLD, n = 5 words);
/ for control register s: 0xC000 | s << 8 | n-1 (FLDCTL; CPU forms put
/ the CPU register in bits 11:8).  Control registers: SC 0, FFLAGS 4,
/ USER 5 (one word).
/
/ The save area (EPUSAVE bytes): F0-F7 extended, FFLAGS, USER.

	.globl	kepuprobe_, kepusave_, kepuload_, kepurec_

	.shri

/ int kepuprobe(): 1 when an EPU answers.  EPA is set for the probe only.
/ An empty socket reads all ones; a CPU that traps every extended
/ instruction resumes at 8f through kepurec (kfault).
kepuprobe_:
	ldctl	r2, FCW
	ld	r3, r2
	set	r3, $13
	ldar	rr4, 8f
	ldl	kepurec_, rr4
	ldctl	FCW, r3
	ldl	rr4, $-1
	.word	0x8F00, 0xC401		/ FLDCTL RR4, SC
	and	r4, r5
	cp	r4, $-1
	jr	eq, 8f
	ld	r4, $1			/ USER RM bits read back, two patterns
	.word	0x8F58, 0xC400		/ FLDCTL USER, R4
	clr	r5
	.word	0x8F50, 0xC500		/ FLDCTL R5, USER
	and	r5, $0xFF03
	cp	r5, $1
	jr	ne, 8f
	ld	r4, $2
	.word	0x8F58, 0xC400
	clr	r5
	.word	0x8F50, 0xC500
	and	r5, $0xFF03
	cp	r5, $2
	jr	ne, 8f
	clr	r4
	.word	0x8F58, 0xC400		/ USER = 0
	clr	r5
	.word	0x8F48, 0xC401		/ FFLAGS = 0
	ld	r1, $1
	jr	9f
8:	clr	r1
9:	subl	rr4, rr4
	ldl	kepurec_, rr4
	ldctl	FCW, r2
	ret

/ kepusave(area): the EPU state to area.  Trap handlers run with EPA
/ clear, so both moves set it for their own instructions.
kepusave_:
	ldctl	r2, FCW
	ld	r3, r2
	set	r3, $13
	ldctl	FCW, r3
	ldl	rr4, rr14(4)
	.word	0x0F4C, 0x8004
	inc	r5, $10
	.word	0x0F4C, 0x8104
	inc	r5, $10
	.word	0x0F4C, 0x8204
	inc	r5, $10
	.word	0x0F4C, 0x8304
	inc	r5, $10
	.word	0x0F4C, 0x8404
	inc	r5, $10
	.word	0x0F4C, 0x8504
	inc	r5, $10
	.word	0x0F4C, 0x8604
	inc	r5, $10
	.word	0x0F4C, 0x8704
	inc	r5, $10
	.word	0x0F4C, 0xC401		/ FFLAGS
	inc	r5, $4
	.word	0x0F4C, 0xC500		/ USER
	ldctl	FCW, r2
	ret

/ kepuload(area): the EPU state from area.  FFLAGS follows the registers,
/ whose loads can set flags.
kepuload_:
	ldctl	r2, FCW
	ld	r3, r2
	set	r3, $13
	ldctl	FCW, r3
	ldl	rr4, rr14(4)
	.word	0x0F44, 0x8004
	inc	r5, $10
	.word	0x0F44, 0x8104
	inc	r5, $10
	.word	0x0F44, 0x8204
	inc	r5, $10
	.word	0x0F44, 0x8304
	inc	r5, $10
	.word	0x0F44, 0x8404
	inc	r5, $10
	.word	0x0F44, 0x8504
	inc	r5, $10
	.word	0x0F44, 0x8604
	inc	r5, $10
	.word	0x0F44, 0x8704
	inc	r5, $10
	.word	0x0F44, 0xC401
	inc	r5, $4
	.word	0x0F44, 0xC500
	ldctl	FCW, r2
	ret
