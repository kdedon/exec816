/ SPDX-License-Identifier: GPL-3.0-only
/ Word and byte block moves, word fills and word input, for the kernel
/ and for display modules, which link their own copy.
/ A block never crosses a 64 KB segment: the callers split at boundaries.
/ LDIR is interruptible, so a long move does not hold off interrupts.

	.globl	kwmove_, kbmove_, kwfill_, inw_

	.shri

/ kwmove(dst, src, nwords): copy upwards; dst below src may overlap.
kwmove_:
	ldl	rr4, rr14(4)
	ldl	rr2, rr14(8)
	ld	r1, rr14(12)
	test	r1
	jr	z, 1f
	ldir	(rr4), (rr2), r1
1:	ret

/ kbmove(dst, src, nbytes): the same, a byte at a time.
kbmove_:
	ldl	rr4, rr14(4)
	ldl	rr2, rr14(8)
	ld	r1, rr14(12)
	test	r1
	jr	z, 1f
	ldirb	(rr4), (rr2), r1
1:	ret

/ kwfill(dst, value, nwords): store value, then let LDIR copy each word
/ onto the next.
kwfill_:
	ldl	rr4, rr14(4)
	ld	r0, rr14(8)
	ld	r1, rr14(10)
	test	r1
	jr	z, 1f
	ld	(rr4), r0
	dec	r1, $1
	jr	z, 1f
	ldl	rr2, rr4
	inc	r5, $2
	ldir	(rr4), (rr2), r1
1:	ret

/ int inw(port): one word input cycle, for registers read as a word.
inw_:
	ld	r2, rr14(4)
	in	r1, @r2
	ret
