/ SPDX-License-Identifier: MIT
/ The compiler's double add, subtract, multiply and divide on the Z8070
/ EPU when one is fitted, else the C library's software routines (built
/ with an fs prefix).  dr* take both doubles by value, dl* the second by
/ address (at rr14(12)); the result is in rq0 and F0 is scratch.  Template
/ words as in mathepu.s: 0F44/0F24 w2 is F0 op (rr4)/(rr2), double.

	.globl	dradd, dladd, drsub, dlsub, drmul, dlmul, drdiv, dldiv
	.globl	fsdradd, fsdladd, fsdrsub, fsdlsub, fsdrmul, fsdlmul
	.globl	fsdrdiv, fsdldiv, SysBase_

	.shri

dradd:	calr	haveepu
	jp	z, fsdradd
	ldl	rr2, rr14
	add	r3, $12
	jr	1f
dladd:	calr	haveepu
	jp	z, fsdladd
	ldl	rr2, rr14(12)
1:	calr	lda
	.word	0x0F24, 0x7083		/ FADD F0, b
	jr	res

drsub:	calr	haveepu
	jp	z, fsdrsub
	ldl	rr2, rr14
	add	r3, $12
	jr	1f
dlsub:	calr	haveepu
	jp	z, fsdlsub
	ldl	rr2, rr14(12)
1:	calr	lda
	.word	0x0F24, 0x7093		/ FSUB F0, b
	jr	res

drmul:	calr	haveepu
	jp	z, fsdrmul
	ldl	rr2, rr14
	add	r3, $12
	jr	1f
dlmul:	calr	haveepu
	jp	z, fsdlmul
	ldl	rr2, rr14(12)
1:	calr	lda
	.word	0x0F24, 0x70B3		/ FMUL F0, b
	jr	res

drdiv:	calr	haveepu
	jp	z, fsdrdiv
	ldl	rr2, rr14
	add	r3, $12
	jr	1f
dldiv:	calr	haveepu
	jp	z, fsdldiv
	ldl	rr2, rr14(12)
1:	calr	lda
	.word	0x0F24, 0x70A3		/ FDIV F0, b
	jr	res

/ Z when no EPU is fitted.
haveepu:
	ldl	rr4, SysBase_
	ld	r1, rr4(34)		/ ExecBase AttnFlags
	and	r1, $0x10		/ AFF_68881
	ret

/ F0 = a, from the caller's frame below this call.
lda:
	ldl	rr4, rr14
	add	r5, $8
	.word	0x0F44, 0xB003		/ FLD F0, a
	ret

res:
	.word	0x8F00, 0xB003		/ FLD RQ0, F0
	ret
