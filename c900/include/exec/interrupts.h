/* SPDX-License-Identifier: MIT */
/*
 * Interrupt handlers and server chains.  The model is classic Amiga Exec,
 * adapted to the Z8001: an interrupt number is the even vector a chip
 * supplies (0x00-0xFE), or INTV_NVI for the non-vectored interrupt.  Each
 * number has one handler (SetIntVector) or a priority-ordered server chain
 * (AddIntServer); docs/internals.md describes dispatch.
 */
#ifndef EXEC_INTERRUPTS_H
#define EXEC_INTERRUPTS_H

#include <exec/nodes.h>
#include <exec/lists.h>

/*
 * is_Code is called as code(is_Data, intnum) with VI and NVI off, on the
 * interrupted Task's stack.  A server returns nonzero when the interrupt
 * was its own, which ends the chain; zero passes it on.  A handler's result
 * is ignored.
 */
struct Interrupt {
	struct Node is_Node;
	APTR is_Data;
	int (*is_Code)();
};

/* Interrupt numbers: the vector the chip supplies (docs/hardware.md). */
#define INTV_TICK	0x00	/* CIO U70 C/T 3, 100 Hz; a server chain */
#define INTV_KBD	0x08	/* CIO U70 port A */
#define INTV_SCC	0x10	/* SCC U74, status in vector: 0x10-0x1E */
#define INTV_PRINTER	0x70	/* CIO U66 port B */
#define INTV_DISK	0x80	/* PDMAC completion */
#define INTV_LANCE	0x88
#define INTV_NVI	0x100	/* single-step and the LC card's 8568 */
#define INTV_COUNT	129	/* 128 even vectors and the NVI */

/* Amiga source compatibility: AddIntServer(INTB_VERTB, is) joins the
 * 100 Hz tick chain.  There is no vertical-blank interrupt and no
 * INTENA, so no INTF_ masks. */
#define INTB_VERTB	INTV_TICK

/* IntStray(INTV_ALL) totals every number. */
#define INTV_ALL	(-1)

#endif
