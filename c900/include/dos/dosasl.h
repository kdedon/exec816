/* SPDX-License-Identifier: MIT */
/* AmigaDOS pattern matching (dos/dosasl.h, V36+): MatchFirst/MatchNext. */
#ifndef DOS_DOSASL_H
#define DOS_DOSASL_H

#include <exec/types.h>
#include <dos/dos.h>

struct AnchorPath {
	struct AChain *ap_Base;
	struct AChain *ap_Last;
	LONG ap_BreakBits;	/* signals that stop MatchNext */
	LONG ap_FoundBreak;	/* the ones that did (ERROR_BREAK) */
	BYTE ap_Flags;		/* APF_ */
	BYTE ap_Reserved;
	WORD ap_Strlen;		/* size of ap_Buf, 0 for none */
	struct FileInfoBlock ap_Info;
	UBYTE ap_Buf[1];	/* the match's path, ap_Strlen bytes */
};
#define ap_First	ap_Base
#define ap_Current	ap_Last
#define ap_Length	ap_Flags

#define APB_DOWILD	0
#define APF_DOWILD	1
#define APB_ITSWILD	0	/* the pattern has wildcards */
#define APF_ITSWILD	1
#define APB_DODIR	2	/* enter the directory just returned */
#define APF_DODIR	4
#define APB_DIDDIR	3	/* the directory again, after its contents */
#define APF_DIDDIR	8
#define APB_NOMEMERR	4
#define APF_NOMEMERR	16
#define APB_DODOT	5
#define APF_DODOT	32
#define APB_DirChanged	6	/* ap_Current->an_Lock changed */
#define APF_DirChanged	64
#define APB_FollowHLinks 7
#define APF_FollowHLinks 128

/* One level of the path: an_Lock is the directory it searches. */
struct AChain {
	struct AChain *an_Child;
	struct AChain *an_Parent;
	BPTR an_Lock;
	struct FileInfoBlock an_Info;
	BYTE an_Flags;		/* DDF_ */
	UBYTE an_String[1];	/* the level's name or parsed pattern */
};

#define DDB_PatternBit	0
#define DDF_PatternBit	1
#define DDB_ExaminedBit	1
#define DDF_ExaminedBit	2
#define DDB_Completed	2
#define DDF_Completed	4
#define DDB_AllBit	3
#define DDF_AllBit	8
#define DDB_Single	4
#define DDF_Single	16

/* ParsePattern's tokens (the parsed form is private) */
#define P_ANY		0x80
#define P_SINGLE	0x81
#define P_ORSTART	0x82
#define P_ORNEXT	0x83
#define P_OREND		0x84
#define P_NOT		0x85
#define P_NOTEND	0x86
#define P_NOTCLASS	0x87
#define P_CLASS		0x88
#define P_REPBEG	0x89
#define P_REPEND	0x8A
#define P_STOP		0x8B

#define COMPLEX_BIT	1
#define EXAMINE_BIT	2

#endif
