/ SPDX-License-Identifier: MIT
/ Port I/O and the interrupt mask for C: linked into the kernel, into
/ programs (lib/) and into display modules.

	.globl	inb_, outb_, outw_, intoff_, intrest_

	.shri

/ int inb(port)
inb_:
	ld	r2, rr14(4)
	inb	rl1, @r2
	clrb	rh1
	ret

/ outb(port, value)
outb_:
	ld	r2, rr14(4)
	ld	r3, rr14(6)
	outb	@r2, rl3
	ret

/ outw(port, value): word output, as the disk doorbell takes
outw_:
	ld	r2, rr14(4)
	ld	r3, rr14(6)
	out	@r2, r3
	ret

/ int intoff(): disable vectored and non-vectored interrupts, return the
/ previous FCW.
intoff_:
	ldctl	r1, FCW
	di	VI, NVI
	ret

/ intrest(fcw): re-enable VI and NVI where fcw had them enabled.
intrest_:
	ld	r1, rr14(4)
	bit	r1, $12
	jr	z, 1f
	ei	VI
1:	bit	r1, $11
	jr	z, 2f
	ei	NVI
2:	ret
