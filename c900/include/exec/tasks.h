/* SPDX-License-Identifier: MIT */
/*
 * Tasks, as Amiga <exec/tasks.h>.  struct Task, with tc_TrapData and
 * tc_TrapCode, and the TS_ states are generated from the upstream ABI.
 *
 * tc_TrapCode is called as code(ULONG trapnum, struct TrapFrame *frame) on
 * the trapping stack, below the frame, with VI and NVI off.  It may change
 * the frame and return, which resumes the frame, or not return.  The
 * Z8001 saves the PC of the instruction after the one that trapped.
 */
#ifndef EXEC_TASKS_H
#define EXEC_TASKS_H

#include <exec/nodes.h>
#include <exec/lists.h>
#include <exec/abi/tasks.h>

/* Signals 0..15 belong to the system. */
#define SYS_SIGALLOC	0xFFFF

#define SIGB_ABORT	0
#define SIGB_CHILD	1
#define SIGB_BLIT	4
#define SIGB_SINGLE	4
#define SIGB_INTUITION	5
#define SIGB_NET	7
#define SIGB_DOS	8

#define SIGF_ABORT	(1L << SIGB_ABORT)
#define SIGF_CHILD	(1L << SIGB_CHILD)
#define SIGF_BLIT	(1L << SIGB_BLIT)
#define SIGF_SINGLE	(1L << SIGB_SINGLE)
#define SIGF_INTUITION	(1L << SIGB_INTUITION)
#define SIGF_NET	(1L << SIGB_NET)
#define SIGF_DOS	(1L << SIGB_DOS)

/* The frame every trap and interrupt entry saves (36 bytes). */
struct TrapFrame {
	UWORD tf_Regs[14];	/* r0..r13; r14/r15 is the frame's end */
	UWORD tf_Id;		/* the identifier the CPU pushed */
	UWORD tf_FCW;
	UWORD tf_PCSeg;		/* segment word: 0x8000 | seg << 8 */
	UWORD tf_PC;		/* offset */
};

/* StackSwap's argument, as on the Amiga.  stk_Pointer must lie in the
 * stack segment (MEMF_STACK), as every stack must. */
struct StackSwapStruct {
	APTR stk_Lower;
	ULONG stk_Upper;
	APTR stk_Pointer;
};

/* Trap numbers, the 68000 vector of the nearest Amiga counterpart. */
#define TRAP_SEGMENT	2	/* Z8010 segment trap: access fault */
#define TRAP_PRIV	8	/* privileged instruction */
#define TRAP_EPA	11	/* extended instruction (line F) */
#define TRAP_NMI	31	/* non-maskable interrupt: system only */
#define TRAP_SC		32	/* SC n, n != 0: TRAP_SC + n */

#endif
