/* SPDX-License-Identifier: MIT */
/* The fake intuition.library's base (fakeint.c), which tests/alertint.c
 * and tests/crashint.c read. */
#ifndef FAKEINT_H
#define FAKEINT_H

#define FITEXTMAX	160
#define FITASKS		8
#define FISTRMAX	72

/* intuition's struct EasyStruct. */
struct FIEasy {
	ULONG es_StructSize;
	ULONG es_Flags;
	char *es_Title;
	char *es_TextFormat;
	char *es_GadgetFormat;
};

/* What BuildEasyRequestArgs does. */
#define FIB_WINDOW	0	/* returns a window */
#define FIB_FAIL	1	/* returns NULL */
#define FIB_HANG	2	/* waits for SIGBREAKF_CTRL_C, then a window */

struct FakeIBase {
	struct Library fi_Lib;
	APTR fi_Seg;
	UWORD fi_Calls;		/* DisplayAlert calls */
	UWORD fi_Timed;		/* TimedDisplayAlert calls */
	ULONG fi_Num;		/* the last call's arguments */
	UWORD fi_Height;
	UWORD fi_Len;		/* bytes of fi_Text */
	UBYTE fi_Text[FITEXTMAX];
	struct Task *fi_Task;	/* the caller, its nest counts and our opens */
	BYTE fi_TD, fi_ID;
	UWORD fi_Opens;
	BOOL fi_Answer;		/* what DisplayAlert returns */
	BOOL fi_Refuse;		/* a call is a failure */
	struct Task *fi_Hold;	/* signalled by the next DisplayAlert, which
				 * then waits for SIGBREAKF_CTRL_C */
	UWORD fi_Active;	/* DisplayAlert calls in progress */
	BOOL fi_Overlap;	/* two were in progress at once */
	struct Task *fi_Tasks[FITASKS];	/* each DisplayAlert's caller */
	UWORD fi_Easys;		/* EasyRequestArgs calls */
	UWORD fi_Builds;	/* BuildEasyRequestArgs calls */
	UWORD fi_Handles;	/* SysReqHandler calls */
	UWORD fi_Frees;		/* FreeSysRequest calls */
	UWORD fi_BuildMode;	/* FIB_ */
	UWORD fi_Pending;	/* SysReqHandler -2 answers before fi_Gadget */
	LONG fi_Gadget;		/* what SysReqHandler returns */
	struct Task *fi_BuildTask;	/* the last build's caller */
	ULONG fi_BuildStack;	/* its stack size */
	struct Task *fi_Hung;	/* a build waiting for SIGBREAKF_CTRL_C */
	APTR fi_BuildWin;	/* its window, IDCMP and first argument */
	ULONG fi_BuildIDCMP;
	ULONG fi_BuildArg;
	ULONG fi_ESSize, fi_ESFlags;
	BOOL fi_WrongWin;	/* SysReqHandler or FreeSysRequest got another */
	BOOL fi_NoWait;		/* SysReqHandler without waitInput */
	char fi_Title[FISTRMAX];	/* the EasyStruct's strings */
	char fi_Format[FISTRMAX];
	char fi_Gadgets[FISTRMAX];
	UWORD fi_Win;		/* the window BuildEasyRequestArgs returns */
};

#endif
