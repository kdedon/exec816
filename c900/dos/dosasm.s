/ SPDX-License-Identifier: GPL-3.0-only
/ dos.library's call into a program, Exit's return from one, and a held
/ Task's way out of trap state.
/
/ LONG dcall(entry, args, len, slot): saves r6-r13, stores the stack
/ pointer in the long at slot (pr_ReturnAddr), and calls entry(args,
/ len); entry's LONG result in rr0 is dcall's.  dexitto(slot, rc)
/ returns from that dcall with rc, from any depth inside entry.
/
/ dheld: where dtrap (doscrash.c) resumes a Task it holds, with the trap
/ number in rr0, the trapping PC's segment word and offset in r2 and r3,
/ the stack offset to run on in r4 and the trapping FCW in r5.  It calls
/ dcrash(n, pcseg, pc, fcw), which never returns.

	.globl	dcall_, dexitto_, dheld_

	.shri

dcall_:
	sub	r15, $16
	ldm	(rr14), r6, $8
	ldl	rr2, rr14(32)
	ldl	(rr2), rr14
	ldl	rr4, rr14(20)
	ldl	rr2, rr14(28)
	pushl	(rr14), rr2
	ldl	rr2, rr14(28)
	pushl	(rr14), rr2
	call	(rr4)
	inc	r15, $8
dret:
	ldm	r6, (rr14), $8
	add	r15, $16
	ret

dexitto_:
	ldl	rr2, rr14(4)
	ldl	rr0, rr14(8)
	ld	r15, rr2(2)
	jr	dret

dheld_:
	ld	r15, r4
	push	(rr14), r5
	push	(rr14), r3
	push	(rr14), r2
	pushl	(rr14), rr0
	call	dcrash_
