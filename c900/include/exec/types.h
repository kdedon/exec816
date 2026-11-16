/* SPDX-License-Identifier: MIT */
/*
 * Basic types, as Amiga <exec/types.h> (V40), with CONST_STRPTR and
 * CONST_APTR from later includes.  TASKPROC is Exec900's.
 */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H

#define INCLUDE_VERSION	40

#define GLOBAL		extern
#define IMPORT		extern
#define STATIC		static
#define REGISTER	register

#ifndef VOID
#define VOID		void
#endif

#ifndef CONST
#define CONST		const
#endif

/* MWC Z8001: int 16 bits, long 32, pointers 32-bit segmented far. */
typedef void *APTR;
typedef CONST void *CONST_APTR;
typedef long LONG;
typedef unsigned long ULONG;
typedef unsigned long LONGBITS;
typedef short WORD;
typedef unsigned short UWORD;
typedef unsigned short WORDBITS;
typedef signed char BYTE;
typedef unsigned char UBYTE;
typedef unsigned char BYTEBITS;
typedef unsigned short RPTR;
typedef unsigned char *STRPTR;
typedef CONST unsigned char *CONST_STRPTR;

/* Obsolete Amiga names. */
typedef short SHORT;
typedef unsigned short USHORT;
typedef short COUNT;
typedef unsigned short UCOUNT;
typedef ULONG CPTR;

typedef float FLOAT;
typedef double DOUBLE;
typedef short BOOL;
typedef unsigned char TEXT;

typedef void (*TASKPROC)();

#ifndef TRUE
#define TRUE		1
#endif
#ifndef FALSE
#define FALSE		0
#endif
#ifndef NULL
#define NULL		((void *)0)
#endif

#define BYTEMASK	0xFF
#define LIBRARY_MINIMUM	33

#endif
