/ SPDX-License-Identifier: GPL-3.0-only
/ Uncontended signal semaphore calls and Forbid/Permit, hand-written.
/ kernel/semaphores.c keeps the protocol and the queueing paths.
/
/ A fast path is take() or the release count update of semaphores.c done
/ with VI and NVI off instead of under Forbid: no Task runs between the
/ count test and its update, and every queueing path runs under Forbid,
/ so it never sees a half-made change.  Whatever the fast path cannot
/ finish (a busy semaphore, a last release with requests queued, a bad
/ release) it leaves untouched and hands to the C call, which decides
/ again under Forbid.  The caller's FCW is restored, so a call made
/ under Disable keeps VI off.
/
/ RR2 points at ss_Owner; ss_NestCount is at -26, ss_QueueCount at +4.
/ R0 is the count, R1 the caller's FCW, RR4 ThisTask.

	.globl	xObtainSem_, xObtainSemShr_, xAttemptSem_, xAttemptSemShr_
	.globl	xReleaseSem_, xForbid_, xPermit_
	.globl	ksemobtx_, ksemobts_, ksemrel_, knestbad_, kmaybe_
	.globl	ThisTask_, SysFlags_

	.shri

/ xObtainSem(ss)
xObtainSem_:
	ldl	rr2, rr14(4)
	add	r3, $40
	ldl	rr4, ThisTask_
	ldctl	r1, FCW
	di	VI, NVI
	ld	r0, rr2(4)
	inc	r0, $1
	jr	z, mine
	cpl	rr4, (rr2)
	jr	eq, mine
	ldctl	FCW, r1
	jp	ksemobtx_

/ xObtainSemShr(ss)
xObtainSemShr_:
	ldl	rr2, rr14(4)
	add	r3, $40
	ldl	rr4, ThisTask_
	ldctl	r1, FCW
	di	VI, NVI
	ld	r0, rr2(4)
	inc	r0, $1
	jr	z, held
	cpl	rr4, (rr2)
	jr	eq, held
	testl	(rr2)
	jr	z, held
	ldctl	FCW, r1
	jp	ksemobts_

/ ULONG xAttemptSem(ss)
xAttemptSem_:
	ldl	rr2, rr14(4)
	add	r3, $40
	ldl	rr4, ThisTask_
	ldctl	r1, FCW
	di	VI, NVI
	ld	r0, rr2(4)
	inc	r0, $1
	jr	z, mine
	cpl	rr4, (rr2)
	jr	eq, mine
	jr	busy

/ ULONG xAttemptSemShr(ss)
xAttemptSemShr_:
	ldl	rr2, rr14(4)
	add	r3, $40
	ldl	rr4, ThisTask_
	ldctl	r1, FCW
	di	VI, NVI
	ld	r0, rr2(4)
	inc	r0, $1
	jr	z, held
	cpl	rr4, (rr2)
	jr	eq, held
	testl	(rr2)
	jr	z, held
busy:	ldctl	FCW, r1
	subl	rr0, rr0
	ret

/ Count the hold; an exclusive one owns the semaphore.  Returns 1.
mine:	ldl	(rr2), rr4
held:	ld	rr2(4), r0
	ld	r0, rr2(-26)
	inc	r0, $1
	ld	rr2(-26), r0
	ldctl	FCW, r1
	ldk	r1, $1
	clr	r0
	ret

/ xReleaseSem(ss)
xReleaseSem_:
	ldl	rr2, rr14(4)
	add	r3, $40
	ldl	rr4, ThisTask_
	ldctl	r1, FCW
	di	VI, NVI
	testl	(rr2)
	jr	z, 1f
	cpl	rr4, (rr2)
	jr	ne, 3f
1:	ld	r0, rr2(-26)
	dec	r0, $1
	jr	lt, 3f
	jr	nz, 2f
	ld	r0, rr2(4)
	dec	r0, $1
	jr	ge, 3f
	ld	rr2(4), r0
	inc	r0, $1
	ld	rr2(-26), r0
	subl	rr4, rr4
	ldl	(rr2), rr4
	ldctl	FCW, r1
	ret
2:	ld	rr2(-26), r0
	ld	r0, rr2(4)
	dec	r0, $1
	ld	rr2(4), r0
	ldctl	FCW, r1
	ret
3:	ldctl	FCW, r1
	jp	ksemrel_

/ Only the running Task changes its own tc_TDNestCnt (offset 17).
/ xForbid()
xForbid_:
	ldl	rr2, ThisTask_
	add	r3, $17
	cpb	(rr2), $127
	jr	eq, 1f
	incb	(rr2), $1
	ret
1:	ldk	r0, $0
	jr	nestbad

/ xPermit(): a pending switch is taken when the count drops below 0.
xPermit_:
	ldl	rr2, ThisTask_
	add	r3, $17
	decb	(rr2), $1
	ret	ge
	cpb	(rr2), $-1
	jr	ne, 1f
	test	SysFlags_
	ret	z
	jp	kmaybe_
1:	ldk	r0, $1
nestbad:
	push	(rr14), r0
	call	knestbad_
