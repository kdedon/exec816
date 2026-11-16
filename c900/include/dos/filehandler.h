/* SPDX-License-Identifier: MIT */
/*
 * A filesystem handler's startup: AmigaDOS's FileSysStartupMsg, which a
 * device's dol_Startup points to, and its DosEnvec.  Pointers are native
 * (fssm_Device is a C string, fssm_Environ a plain pointer).
 */
#ifndef DOS_FILEHANDLER_H
#define DOS_FILEHANDLER_H

#include <exec/types.h>

struct DosEnvec {
	ULONG de_TableSize;	/* longs after this one that are valid */
	ULONG de_SizeBlock;	/* block size in longs */
	ULONG de_SecOrg;
	ULONG de_Surfaces;
	ULONG de_SectorPerBlock;
	ULONG de_BlocksPerTrack;
	ULONG de_Reserved;
	ULONG de_PreAlloc;
	ULONG de_Interleave;
	ULONG de_LowCyl;
	ULONG de_HighCyl;
	ULONG de_NumBuffers;	/* cache buffers */
	ULONG de_BufMemType;	/* their AllocMem requirements */
	ULONG de_MaxTransfer;
	ULONG de_Mask;
	LONG de_BootPri;
	ULONG de_DosType;
};

/* de_TableSize values: the last valid field. */
#define DE_TABLESIZE	0
#define DE_SIZEBLOCK	1
#define DE_NUMBUFFERS	11
#define DE_BUFMEMTYPE	12
#define DE_MAXTRANSFER	13
#define DE_MASK		14
#define DE_BOOTPRI	15
#define DE_DOSTYPE	16

struct FileSysStartupMsg {
	ULONG fssm_Unit;	/* OpenDevice unit */
	char *fssm_Device;	/* OpenDevice name */
	struct DosEnvec *fssm_Environ;
	ULONG fssm_Flags;	/* OpenDevice flags */
};

#endif
