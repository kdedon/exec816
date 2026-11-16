/* SPDX-License-Identifier: MIT */
/*
 * Libraries, as Amiga <exec/libraries.h>.  A library base is a struct
 * Library preceded by its jump table: vector n (n = 1, 2, ...) is the
 * 6-byte Z8001 instruction "jp addr" (0x5E08 and a long segmented
 * address) at base - 6n, the size of a 68000 LVO.  Calls pass the base as
 * a hidden first argument, except exec.library's (docs/platform.md).
 */
#ifndef EXEC_LIBRARIES_H
#define EXEC_LIBRARIES_H

#include <exec/nodes.h>

#define LIB_VECTSIZE	6
#define LIB_RESERVED	4
#define LIB_BASE	(-LIB_VECTSIZE)
#define LIB_USERDEF	(LIB_BASE - (LIB_RESERVED * LIB_VECTSIZE))
#define LIB_NONSTD	(LIB_USERDEF)

#define LIB_OPEN	(-6)
#define LIB_CLOSE	(-12)
#define LIB_EXPUNGE	(-18)
#define LIB_EXTFUNC	(-24)

struct Library {
	struct Node lib_Node;
	UBYTE lib_Flags;
	UBYTE lib_pad;
	UWORD lib_NegSize;	/* bytes before the base */
	UWORD lib_PosSize;	/* bytes after the base */
	UWORD lib_Version;
	UWORD lib_Revision;
	APTR lib_IdString;
	ULONG lib_Sum;
	UWORD lib_OpenCnt;
};

#define LIBF_SUMMING	(1 << 0)
#define LIBF_CHANGED	(1 << 1)
#define LIBF_SUMUSED	(1 << 2)
#define LIBF_DELEXP	(1 << 3)

#endif
