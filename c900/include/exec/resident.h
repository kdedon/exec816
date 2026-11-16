/* SPDX-License-Identifier: MIT */
/*
 * Resident modules, as Amiga <exec/resident.h>.  The kernel's residents
 * are a NULL-terminated array of pointers, ResModules; nothing scans
 * memory for RTC_MATCHWORD.
 */
#ifndef EXEC_RESIDENT_H
#define EXEC_RESIDENT_H

#include <exec/types.h>

struct Resident {
	UWORD rt_MatchWord;		/* RTC_MATCHWORD */
	struct Resident *rt_MatchTag;	/* this structure */
	APTR rt_EndSkip;
	UBYTE rt_Flags;
	UBYTE rt_Version;
	UBYTE rt_Type;			/* NT_LIBRARY, NT_DEVICE, NT_RESOURCE */
	BYTE rt_Pri;
	char *rt_Name;
	char *rt_IdString;
	APTR rt_Init;	/* RTF_AUTOINIT: ULONG dataSize, APTR vectors,
			 * APTR structure, APTR initFunction */
};

#define RTC_MATCHWORD	0x4AFC

#define RTF_AUTOINIT	(1 << 7)
#define RTF_AFTERDOS	(1 << 2)
#define RTF_SINGLETASK	(1 << 1)
#define RTF_COLDSTART	(1 << 0)

#define RTW_NEVER	0
#define RTW_COLDSTART	1

#endif
