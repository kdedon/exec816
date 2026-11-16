/* SPDX-License-Identifier: MIT */
/* AmigaDOS argument parsing (dos/rdargs.h, V36+): ReadArgs and ReadItem. */
#ifndef DOS_RDARGS_H
#define DOS_RDARGS_H

#include <exec/types.h>

/* A string to parse instead of the command line. */
struct CSource {
	UBYTE *CS_Buffer;
	LONG CS_Length;
	LONG CS_CurChr;
};

struct RDArgs {
	struct CSource RDA_Source;	/* CS_Buffer NULL: the command line */
	LONG RDA_DAList;		/* private: ReadArgs' allocations */
	UBYTE *RDA_Buffer;		/* optional parsing space */
	LONG RDA_BufSiz;
	UBYTE *RDA_ExtHelp;		/* printed for a second "?" */
	LONG RDA_Flags;			/* RDAF_ */
};

#define RDAB_STDIN	0	/* the line comes from Input() */
#define RDAF_STDIN	1L
#define RDAB_NOALLOC	1	/* parse in RDA_Buffer only */
#define RDAF_NOALLOC	2L
#define RDAB_NOPROMPT	2	/* "?" does not prompt */
#define RDAF_NOPROMPT	4L

#define MAX_TEMPLATE_ITEMS	100
#define MAX_MULTIARGS		128

/* ReadItem results */
#define ITEM_EQUAL	(-2L)	/* "=" */
#define ITEM_ERROR	(-1L)	/* overflow or an unmatched quote */
#define ITEM_NOTHING	0L	/* the end of the line */
#define ITEM_UNQUOTED	1L
#define ITEM_QUOTED	2L

#endif
