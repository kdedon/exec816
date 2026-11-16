/ SPDX-License-Identifier: GPL-3.0-only
/ Z8010 descriptor reads and limit writes.  Full descriptor writes go
/ through mapseg_ (crt.s).
/ Loading the segment address register resets the descriptor byte counter,
/ so two base reads return the high byte, then the low byte.  Callers keep
/ VI off: an interrupt that programs the MMU between the select and the
/ reads would move the address register.

	.globl	kmmubase_, kmmulim_

	.shri

/ UWORD kmmubase(seg): descriptor seg's base page (phys >> 8).
kmmubase_:
	ld	r0, rr14(4)
	soutb	0x01fc, rl0
	sinb	rh1, 0x08fc
	sinb	rl1, 0x08fc
	ret

/ kmmulim(seg, limit): set descriptor seg's limit field (last 256-byte
/ block of an ascending segment).
kmmulim_:
	ld	r0, rr14(4)
	ld	r1, rr14(6)
	soutb	0x01fc, rl0
	soutb	0x09fc, rl1
	ret
