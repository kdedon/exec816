/ SPDX-License-Identifier: GPL-3.0-only
/ EPU helpers for tests/epu.c (template words as in platform/epuasm.s).

	.globl	epuraw_, epures_, epuld2_, epust2_, epuadd2_, epuld3_, epust3_

	.shri

/ int epuraw(): FLDCTL R1, USER.  A trap handler resumes at epures.
epuraw_:
	clr	r1
	.word	0x8F50, 0xC100
epures_:
	ret

/ epuld2(p), epust2(p): F2 from and to the double at p; F3 likewise.
epuld2_:
	ldl	rr2, rr14(4)
	.word	0x0F24, 0xB203
	ret
epust2_:
	ldl	rr2, rr14(4)
	.word	0x0F2C, 0xB203
	ret
epuld3_:
	ldl	rr2, rr14(4)
	.word	0x0F24, 0xB303
	ret
epust3_:
	ldl	rr2, rr14(4)
	.word	0x0F2C, 0xB303
	ret

/ epuadd2(n, p): F2 += the double at p, n times.
epuadd2_:
	ld	r1, rr14(4)
	ldl	rr2, rr14(6)
1:	.word	0x0F24, 0x7283
	djnz	r1, 1b
	ret

/ The C library's software double routines under C names: fsdlop(a, &b).
	.globl	fsdladd_, fsdlsub_, fsdlmul_, fsdldiv_
	.globl	fsdladd, fsdlsub, fsdlmul, fsdldiv
fsdladd_:
	jp	fsdladd
fsdlsub_:
	jp	fsdlsub
fsdlmul_:
	jp	fsdlmul
fsdldiv_:
	jp	fsdldiv
