/ SPDX-License-Identifier: GPL-3.0-only
/ Trap and interrupt entry, and the machine helpers C calls.
/
/ Every entry builds the same 36-byte frame on the interrupted stack:
/   +0   r0..r13          saved here
/   +28  identifier       pushed by the CPU
/   +30  FCW
/   +32  PC segment word
/   +34  PC offset
/ All stacks are in segment SS, so a frame is named by its offset alone.  A
/ handler passes that offset to C and resumes whatever frame offset C
/ returns; returning another Task's frame is the context switch.  A trap
/ resumes its own frame once the Task's tc_TrapCode returns (kfault).

	.globl	tsc_, tepa_, tprv_, tseg_, tnmi_, tnvi_, tvi_
	.globl	kswitch_, kintr_, kintstk_, kfault_
	.globl	haltcpu_, kresched_, emustop_

	.shri

/ SC: the reschedule gate.  Callers enter with VI already off when they
/ have queue state to protect; the PSA FCW keeps it off throughout.  Only
/ sc 0 is the gate; any other code is a trap.
tsc_:
	sub	r15, $28
	ldm	(rr14), r0, $14
	ldb	rl2, rr14(29)
	testb	rl2
	jr	nz, tscbad
	ld	r1, r15
	push	(rr14), r1
	call	kswitch_
	ld	r15, r1
	ldm	r0, (rr14), $14
	add	r15, $28
	iret

/ Every vectored interrupt and the NVI: kintr(frameoff, id) runs the
/ installed handler or server chain and returns the frame to resume.  id is
/ the vector, the low byte of the identifier the CPU saved at +28, or
/ INTV_NVI (0x100) for the NVI.  The chips drive only AD7-AD0, so the
/ identifier's high byte floats (0xFF on hardware).
/ The frame stays on the interrupted stack; C runs on the interrupt stack
/ whose top offset is kintstk, so a Task stack carries only the frame.
/ Handlers never nest, so one interrupt stack serves every entry.
tvi_:
	sub	r15, $28
	ldm	(rr14), r0, $14
	ld	r2, rr14(28)
	clrb	rh2
	jr	intr
tnvi_:
	sub	r15, $28
	ldm	(rr14), r0, $14
	ld	r2, $0x100
intr:
	ld	r1, r15
	ld	r15, kintstk_
	push	(rr14), r2
	push	(rr14), r1
	call	kintr_
	ld	r15, r1
	ldm	r0, (rr14), $14
	add	r15, $28
	iret

tepa_:
	sub	r15, $28
	ldm	(rr14), r0, $14
	ld	r2, $1
	jr	fault
tprv_:
	sub	r15, $28
	ldm	(rr14), r0, $14
	ld	r2, $2
	jr	fault
tseg_:
	sub	r15, $28
	ldm	(rr14), r0, $14
	sinb	rl0, 0x02fc		/ read and reset the violation latch
	soutb	0x11fc, rl0
	ld	r2, $3
	jr	fault
tnmi_:
	sub	r15, $28
	ldm	(rr14), r0, $14
	ld	r2, $4
	jr	fault
tscbad:
	ld	r2, $5
fault:
	push	(rr14), r2
	ld	r1, r15
	add	r1, $2
	push	(rr14), r1
	call	kfault_
	ld	r15, r1
	ldm	r0, (rr14), $14
	add	r15, $28
	iret

/ haltcpu(): wait for an interrupt.
haltcpu_:
	halt
	ret

/ kresched(): enter the reschedule gate.
kresched_:
	sc	0
	ret

/ emustop(): end an emulator run; real hardware ignores the unmapped port.
emustop_:
	ld	r1, $0xC900
	out	0x0FFE, r1
	ret

/ StackSwap(sss): Amiga exec's.  sss holds stk_Lower, stk_Upper and
/ stk_Pointer (+0, +4, +8); they are exchanged with ThisTask's
/ tc_SPLower and tc_SPUpper (+46, +50; tests/dosproc.c checks them) and the stack
/ pointer, and the call returns on the new stack, whose low end gets the
/ guard word kswitch checks.  Every stack is in SS, so only the offset
/ word of the pointer changes.  VI and NVI stay off across the exchange,
/ so the stack probe never sees it half done.  The argument's slot is
/ copied to the new stack with the return address, for the caller to
/ pop; the old pointer stored is the one the caller has after its pop.
	.globl	xStackSwap_, ThisTask_
xStackSwap_:
	ldctl	r1, FCW
	di	VI, NVI
	pushl	(rr14), rr6
	pushl	(rr14), rr8
	ldl	rr2, rr14(12)
	ldl	rr4, ThisTask_
	ldl	rr6, (rr2)
	ldl	rr8, rr4(46)
	ldl	(rr2), rr8
	ldl	rr4(46), rr6
	ldl	rr6, rr2(4)
	ldl	rr8, rr4(50)
	ldl	rr2(4), rr8
	ldl	rr4(50), rr6
	ldl	rr6, rr4(46)
	ld	(rr6), $0xC900
	ldl	rr4, rr14(8)
	popl	rr8, (rr14)
	popl	rr6, (rr14)
	ld	r0, rr2(10)
	inc	r15, $8
	ld	rr2(10), r15
	ld	r15, r0
	pushl	(rr14), rr2
	pushl	(rr14), rr4
	ldctl	FCW, r1
	ret
