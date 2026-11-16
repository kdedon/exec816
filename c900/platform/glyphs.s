/ SPDX-License-Identifier: GPL-3.0-only
/ HR glyph runs, block moves and cell inversion, for the HR display module.

	.globl	kglyphs_, kglyph4_, kmove2d_, kfill2d_, kcomb_, kxor4_

	.shri

/ kglyphs(dst, s, n, font): draw n 8x16 cells of 1-bit pixels from the
/ string s, one byte per cell and 128 bytes per raster, starting at dst;
/ font holds 16 bytes per glyph from code 0x20, and codes outside
/ 0x20-0x7E draw as '?'.  The cells must lie in one segment.
kglyphs_:
	ld	r1, rr14(12)
	test	r1
	jr	z, 3f
	pushl	(rr14), rr6
	pushl	(rr14), rr8
	pushl	(rr14), rr10
	ldl	rr4, rr14(16)
	ldl	rr2, rr14(20)
	ldl	rr6, rr14(26)
	ld	r10, $128
1:	ldb	rl0, (rr2)
	inc	r3, $1
	subb	rl0, $0x20
	cpb	rl0, $0x5e
	jr	ule, 2f
	ldb	rl0, $0x1f
2:	clrb	rh0
	sll	r0, $4
	ldl	rr8, rr6
	add	r9, r0
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	inc	r9, $1
	ldb	(rr4), rl0
	add	r5, r10
	ldb	rl0, (rr8)
	ldb	(rr4), rl0
	sub	r5, $1919
	djnz	r1, 1b
	popl	rr10, (rr14)
	popl	rr8, (rr14)
	popl	rr6, (rr14)
3:	ret

/ kglyph4(dst, s, n, font, tab): draw n 8x16 colour cells, 4 bits per
/ pixel and 320 bytes per raster, from the string s; tab holds the 256
/ longs eight pixels of a glyph byte become.  Codes as kglyphs.  The
/ cells must lie in one segment.
kglyph4_:
	ld	r1, rr14(12)
	test	r1
	jr	z, 3f
	pushl	(rr14), rr6
	pushl	(rr14), rr8
	pushl	(rr14), rr10
	pushl	(rr14), rr12
	ldl	rr4, rr14(20)
	ldl	rr2, rr14(24)
	ldl	rr12, rr14(34)
	ld	r11, $320
1:	ldl	rr8, rr14(30)
	ldb	rl0, (rr2)
	inc	r3, $1
	subb	rl0, $0x20
	cpb	rl0, $0x5e
	jr	ule, 2f
	ldb	rl0, $0x1f
2:	clrb	rh0
	sll	r0, $4
	add	r9, r0
	ld	r10, $16
4:	ldb	rl6, (rr8)
	inc	r9, $1
	clrb	rh6
	sll	r6, $2
	ldl	rr6, rr12(r6)
	ldl	(rr4), rr6
	add	r5, r11
	djnz	r10, 4b
	sub	r5, $5116
	djnz	r1, 1b
	popl	rr12, (rr14)
	popl	rr10, (rr14)
	popl	rr8, (rr14)
	popl	rr6, (rr14)
3:	ret

/ kmove2d(dst, src, nwords, nrows, stride): copy nwords words of each of
/ nrows rows, stride bytes apart, upwards; dst below src may overlap.
/ Each side's rows must lie in one segment.
kmove2d_:
	pushl	(rr14), rr6
	ldl	rr4, rr14(8)
	ldl	rr2, rr14(12)
	ld	r6, rr14(16)
	ld	r0, rr14(18)
	ld	r7, rr14(20)
	test	r6
	jr	z, 2f
	test	r0
	jr	z, 2f
	sub	r7, r6
	sub	r7, r6
1:	ld	r1, r6
	ldir	(rr4), (rr2), r1
	add	r5, r7
	add	r3, r7
	djnz	r0, 1b
2:	popl	rr6, (rr14)
	ret

/ kfill2d(dst, value, nwords, nrows, stride): store value in nwords
/ words of each of nrows rows, stride bytes apart, in one segment; LDIR
/ copies each row's first word onto the next.
kfill2d_:
	pushl	(rr14), rr6
	pushl	(rr14), rr8
	ldl	rr4, rr14(12)
	ld	r8, rr14(16)
	ld	r6, rr14(18)
	ld	r0, rr14(20)
	ld	r7, rr14(22)
	test	r6
	jr	z, 2f
	test	r0
	jr	z, 2f
	sub	r7, r6
	sub	r7, r6
1:	ld	(rr4), r8
	ld	r1, r6
	dec	r1, $1
	jr	z, 3f
	ldl	rr2, rr4
	inc	r5, $2
	ldir	(rr4), (rr2), r1
	jr	4f
3:	inc	r5, $2
4:	add	r5, r7
	djnz	r0, 1b
2:	popl	rr8, (rr14)
	popl	rr6, (rr14)
	ret

/ kcomb(dst, n, stride): complement one byte in each of n rows, stride
/ bytes apart, in one segment.
kcomb_:
	ldl	rr4, rr14(4)
	ld	r0, rr14(8)
	ld	r1, rr14(10)
	test	r0
	jr	z, 2f
1:	comb	(rr4)
	add	r5, r1
	djnz	r0, 1b
2:	ret

/ kxor4(dst, value, nrows, stride): XOR value into the two words of a
/ colour cell in each of nrows rows, stride bytes apart, in one segment.
kxor4_:
	ldl	rr4, rr14(4)
	ld	r2, rr14(8)
	ld	r0, rr14(10)
	ld	r1, rr14(12)
	test	r0
	jr	z, 2f
1:	ld	r3, (rr4)
	xor	r3, r2
	ld	(rr4), r3
	ld	r3, rr4(2)
	xor	r3, r2
	ld	rr4(2), r3
	add	r5, r1
	djnz	r0, 1b
2:	ret
