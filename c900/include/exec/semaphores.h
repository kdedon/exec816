/* SPDX-License-Identifier: MIT */
/*
 * Signal semaphores, as in Amiga Exec V39.  The records keep the Amiga
 * field order; docs/internals.md describes the protocol.
 */
#ifndef EXEC_SEMAPHORES_H
#define EXEC_SEMAPHORES_H

#include <exec/nodes.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <exec/tasks.h>

/* A queued request.  Bit 0 of sr_Waiter marks a shared request; a NULL
 * waiter marks a SemaphoreMessage from Procure. */
struct SemaphoreRequest {
	struct MinNode sr_Link;
	struct Task *sr_Waiter;
};

struct SignalSemaphore {
	struct Node ss_Link;
	WORD ss_NestCount;
	struct MinList ss_WaitQueue;
	struct SemaphoreRequest ss_MultipleLink;
	struct Task *ss_Owner;		/* NULL when free or shared */
	WORD ss_QueueCount;		/* -1 when free */
};

/* A Procure bid: mn_Node.ln_Name is SM_SHARED or SM_EXCLUSIVE.  The reply
 * carries the semaphore when granted and NULL when Vacate withdrew it. */
struct SemaphoreMessage {
	struct Message ssm_Message;
	struct SignalSemaphore *ssm_Semaphore;
};

#define SM_SHARED	1L
#define SM_EXCLUSIVE	0L

#endif
