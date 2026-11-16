/ SPDX-License-Identifier: GPL-3.0-only
/ mathieeedoubbas.library on the Z8070 EPU.  Arguments follow the base:
/ the first double at rr14(8), the second at rr14(16); a double comes back
/ in rq0, a long in rr0.  F0 is scratch.  Template words (EPU ID 0):
/   0F24 w2  F0 op (rr2), double source      8F00 w2  CPU <- F0
/   8F08 w2  F0 <- CPU                        8F50/8F58  R <- USER, USER <- R
/ w2 = op[5:4] << 14 | rnd << 12 | reg << 8 | op[3:0] << 4 | n-1, with
/ rnd 3 (double).  Ops: FLD 20, FADD 18, FSUB 19, FMUL 1B, FDIV 1A,
/ FINT 2D, FLDIL 22, FLDTL 24, FSQR 2C.

	.globl	mbefix_, mbeflt_, mbeadd_, mbesub_, mbemul_, mbediv_
	.globl	mbefloor_, mbeceil_, mtesqrt_

	.shri

mbeadd_:
	ldl	rr2, rr14
	add	r3, $8
	.word	0x0F24, 0xB003		/ FLD F0, a
	add	r3, $8
	.word	0x0F24, 0x7083		/ FADD F0, b
	.word	0x8F00, 0xB003		/ FLD RQ0, F0
	ret
mbesub_:
	ldl	rr2, rr14
	add	r3, $8
	.word	0x0F24, 0xB003
	add	r3, $8
	.word	0x0F24, 0x7093		/ FSUB
	.word	0x8F00, 0xB003		/ FLD RQ0, F0
	ret
mbemul_:
	ldl	rr2, rr14
	add	r3, $8
	.word	0x0F24, 0xB003
	add	r3, $8
	.word	0x0F24, 0x70B3		/ FMUL
	.word	0x8F00, 0xB003		/ FLD RQ0, F0
	ret
mbediv_:
	ldl	rr2, rr14
	add	r3, $8
	.word	0x0F24, 0xB003
	add	r3, $8
	.word	0x0F24, 0x70A3		/ FDIV
	.word	0x8F00, 0xB003		/ FLD RQ0, F0
	ret

/ FLDTL truncates; out of range gives 0x80000000.
mbefix_:
	ldl	rr2, rr14
	add	r3, $8
	.word	0x0F24, 0xB003
	.word	0x8F00, 0x8041		/ FLDTL RR0, F0
	ret

mbeflt_:
	ldl	rr4, rr14(8)
	.word	0x8F08, 0xB421		/ FLDIL F0, RR4
	.word	0x8F00, 0xB003		/ FLD RQ0, F0
	ret

/ FINT rounds by USER's mode: set it, then put the caller's back.
mbefloor_:
	ld	r1, $3			/ toward -infinity
	jr	1f
mbeceil_:
	ld	r1, $2			/ toward +infinity
1:	.word	0x8F50, 0xC400		/ FLDCTL R4, USER
	ld	r5, r4
	and	r5, $0xFFFC
	or	r5, r1
	.word	0x8F58, 0xC500		/ FLDCTL USER, R5
	ldl	rr2, rr14
	add	r3, $8
	.word	0x0F24, 0xB0D3		/ FINT F0, x
	.word	0x8F58, 0xC400		/ FLDCTL USER, R4
	.word	0x8F00, 0xB003		/ FLD RQ0, F0
	ret

/ mathieeedoubtrans.library's IEEEDPSqrt.
mtesqrt_:
	ldl	rr2, rr14
	add	r3, $8
	.word	0x0F24, 0xB0C3		/ FSQR F0, x
	.word	0x8F00, 0xB003		/ FLD RQ0, F0
	ret
