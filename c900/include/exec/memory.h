/* SPDX-License-Identifier: MIT */
/*
 * Memory lists, as Amiga <exec/memory.h>.  struct MemChunk, struct
 * MemHeader and the MEMF_ flags are generated from upstream
 * abi/heap-v1.json.  AllocEntry and FreeEntry are in
 * <exec/exec.h>.
 */
#ifndef EXEC_MEMORY_H
#define EXEC_MEMORY_H

#include <exec/nodes.h>
#include <exec/abi/memory.h>

#define MEM_BLOCKMASK	(MEM_BLOCKSIZE - 1)

struct MemEntry {
	union {
		ULONG meu_Reqs;		/* AllocEntry: MEMF_ requirements */
		APTR meu_Addr;		/* the allocated block */
	} me_Un;
	ULONG me_Length;
};

#define me_un	me_Un
#define me_Reqs	me_Un.meu_Reqs
#define me_Addr	me_Un.meu_Addr

/* ml_NumEntries entries follow; ml_ME[1] is the first of them. */
struct MemList {
	struct Node ml_Node;
	UWORD ml_NumEntries;
	struct MemEntry ml_ME[1];
};

#define ml_me	ml_ME

/* A low-memory handler's argument (AddMemHandler, V39). */
struct MemHandlerData {
	ULONG memh_RequestSize;
	ULONG memh_RequestFlags;
	ULONG memh_Flags;		/* MEMHF_ */
};

#define MEMHF_RECYCLE	(1L << 0)	/* called again after MEM_TRY_AGAIN */

/* A handler's result. */
#define MEM_DID_NOTHING	0L
#define MEM_ALL_DONE	(-1L)
#define MEM_TRY_AGAIN	1L

/* The Amiga name of MEMF_DMA24. */
#define MEMF_24BITDMA	MEMF_DMA24

/* CreatePool: the pool serialises its own calls with a semaphore (from
 * AROS).  AllocMem does not take it. */
#define MEMF_SEM_PROTECTED	(1L << 22)

#endif
