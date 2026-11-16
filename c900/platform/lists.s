/ SPDX-License-Identifier: GPL-3.0-only
/ Classic Exec intrusive lists.  Port of lib/exec/execlists.act; the
/ contract is docs/reference/lists.md.  Caller owns storage, membership
/ and synchronization.  Hand-written because every queue operation of
/ the scheduler, ports and drivers passes through here: without a frame
/ and saved registers a call costs about half the compiled one.
/
/ struct Node: +0 ln_Succ, +4 ln_Pred, +8 ln_Type, +9 ln_Pri, +10 ln_Name.
/ struct List: +0 lh_Head, +4 lh_Tail, +8 lh_TailPred.  A list's head is
/ a node whose successor field is lh_Head and whose predecessor is NULL.
/ Only the offset word of a pointer is stepped.  R0-R5 are scratch and
/ RR0 cannot address memory, so pointers use RR2 and RR4 (RR6 saved where
/ a third is needed); results come back in RR0.

	.globl	xNewList_, xAddHead_, xAddTail_, xInsert_, xRemove_
	.globl	xRemHead_, xRemTail_, xEnqueue_, xFindName_

	.shri

/ xNewList(l)
xNewList_:
	ldl	rr2, rr14(4)
	ldl	rr4, rr2
	inc	r5, $4
	ldl	(rr2), rr4
	subl	rr0, rr0
	ldl	rr2(4), rr0
	ldl	rr2(8), rr2
	ret

/ xAddHead(l, n)
xAddHead_:
	ldl	rr2, rr14(4)
	ldl	rr4, rr14(8)
addhead:
	ldl	rr0, (rr2)
	ldl	(rr4), rr0
	ldl	rr4(4), rr2
	ldl	(rr2), rr4
	ldl	rr2, rr0
	ldl	rr2(4), rr4
	ret

/ xAddTail(l, n)
xAddTail_:
	ldl	rr2, rr14(4)
	ldl	rr4, rr14(8)
addtail:
	ldl	rr0, rr2(8)
	ldl	rr4(4), rr0
	ldl	rr2(8), rr4
	inc	r3, $4
	ldl	(rr4), rr2
	ldl	rr2, rr0
	ldl	(rr2), rr4
	ret

/ xInsert(l, n, pred): after pred; NULL or the head adds at the head,
/ the tail at the tail.
xInsert_:
	ldl	rr2, rr14(4)
	ldl	rr0, rr14(12)
	testl	rr0
	jr	z, 1f
	cpl	rr0, rr2
	jr	eq, 1f
	ldl	rr4, rr2
	inc	r5, $4
	cpl	rr0, rr4
	ldl	rr4, rr14(8)
	jr	eq, addtail
	ldl	rr2, rr0
	ldl	rr0, (rr2)
	ldl	(rr4), rr0
	ldl	rr4(4), rr2
	ldl	(rr2), rr4
	ldl	rr2, rr0
	ldl	rr2(4), rr4
	ret
1:	ldl	rr4, rr14(8)
	jr	addhead

/ xRemove(n)
xRemove_:
	ldl	rr2, rr14(4)
	ldl	rr4, (rr2)
	ldl	rr0, rr2(4)
	ldl	rr4(4), rr0
	ldl	rr2, rr0
	ldl	(rr2), rr4
	ret

/ struct Node *xRemHead(l): NULL when empty.  The old head is read back
/ from its successor's predecessor field before that is rewritten.
xRemHead_:
	ldl	rr2, rr14(4)
	ldl	rr4, (rr2)
	ldl	rr0, (rr4)
	testl	rr0
	jr	z, 1f
	ldl	(rr2), rr0
	ldl	rr4, rr0
	ldl	rr0, rr4(4)
	ldl	rr4(4), rr2
1:	ret

/ struct Node *xRemTail(l): NULL when empty.
xRemTail_:
	ldl	rr2, rr14(4)
	ldl	rr4, rr2(8)
	ldl	rr0, rr4(4)
	testl	rr0
	jr	z, 1f
	ldl	rr2(8), rr0
	inc	r3, $4
	ldl	rr4, rr0
	ldl	rr0, (rr4)
	ldl	(rr4), rr2
1:	ret

/ xEnqueue(l, n): descending signed priority; equal priorities stay FIFO.
/ n goes before the first node of lower priority, else at the tail.
xEnqueue_:
	ldl	rr2, rr14(4)
	ldl	rr4, rr14(8)
	pushl	(rr14), rr6
	ldb	rl1, rr4(9)
	ldl	rr6, (rr2)
1:	testl	(rr6)
	jr	z, 3f
	ldb	rh1, rr6(9)
	cpb	rl1, rh1
	jr	gt, 2f
	ldl	rr6, (rr6)
	jr	1b
2:	ldl	rr2, rr6(4)
	ldl	(rr4), rr6
	ldl	rr4(4), rr2
	ldl	(rr2), rr4
	ldl	rr6(4), rr4
	popl	rr6, (rr14)
	ret
3:	popl	rr6, (rr14)
	jr	addtail

/ struct Node *xFindName(l, name): the first node after l's head (or
/ after l itself, when a node is passed) whose name equals name.
xFindName_:
	ldl	rr2, rr14(4)
	pushl	(rr14), rr6
	ldl	rr4, (rr2)
1:	testl	(rr4)
	jr	z, 4f
	ldl	rr6, rr4(10)
	testl	rr6
	jr	z, 3f
	ldl	rr2, rr14(12)
2:	ldb	rl0, (rr6)
	cpb	rl0, (rr2)
	jr	ne, 3f
	testb	rl0
	jr	z, 5f
	inc	r7, $1
	inc	r3, $1
	jr	2b
3:	ldl	rr4, (rr4)
	jr	1b
4:	subl	rr0, rr0
	popl	rr6, (rr14)
	ret
5:	ldl	rr0, rr4
	popl	rr6, (rr14)
	ret
