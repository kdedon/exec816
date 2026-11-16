/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * dos.library internals.  Every public function takes DOSBase as its
 * hidden first argument (the fd file's convention) and ignores it: the
 * module's own DOSBase global is the base.  Calls between dos.library
 * functions are direct C calls with that base, so the x names keep the
 * library's argument order.
 */
#ifndef DOS_DOSINT_H
#define DOS_DOSINT_H

#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <dos/stdio.h>
#include <dos/rdargs.h>

#define DB	struct DosLibrary *

extern struct DosLibrary *DOSBase;

/* A process as CreateNewProc allocates it: the public record, then
 * dos.library's own fields.  The record and its stack are one MemList on
 * tc_MemEntry, which RemTask frees. */
#define PNAMELEN	32
struct DProc {
	struct Process dp_Proc;
	LONG (*dp_Entry)();
	struct DFH *dp_ConFH;	/* the open that set pr_ConsoleTask */
	char dp_Name[PNAMELEN];
};

/* A file handle as AllocDosObject makes it: the public record, then the
 * buffer's state.  fh_Buf is the buffer, fh_Pos and fh_End index it: in
 * read state fh_Pos < fh_End is read-ahead; with DFF_WRITE, fh_Pos bytes
 * wait to be written.  fh_Pos == fh_End is an empty buffer. */
struct DFH {
	struct FileHandle f_FH;
	LONG f_Size;		/* buffer size, 0 for the default */
	UWORD f_Flags;		/* DFF_ */
	UWORD f_Mode;		/* BUF_LINE, BUF_FULL or BUF_NONE */
	struct DProc *f_Con;	/* the process whose console this open set */
};
#define DFF_WRITE	1	/* the buffer holds output */
#define DFF_OWNBUF	2	/* dos.library allocated fh_Buf */
#define DFF_EOF		4	/* the last FGetC reached the end */
#define DFF_UNEOF	8	/* UnGetC(-1) after the end */
#define DFBUFSIZE	512L	/* a file's buffer: a V7 block */
#define DFCONSIZE	256L	/* an interactive one: a CON: line */

/* Stack sizes, bytes. */
#define DEFSTACK	1536L	/* CreateNewProc without NP_StackSize */
#define MINSTACK	512L	/* RunCommand's least */
#define SHELLSTACK	2048L	/* a shell process; commands run on their own */
#define CLISTACK	1000L	/* cli_DefaultStack, longwords: 4000 bytes, as AmigaOS */

/* The shell's file, loaded once (rn_ShellSegment). */
#define SHELLFILE	"L:Shell-Seg"

/* dosmisc.c */
struct Process *dme();
LONG dstrlen();
int dnamecmp();
char *dcolon();
LONG dseterr();
LONG dfail();
struct DosInfo *ddi();
void dnumput();

/* dosio.c */
void dconlink();

/* dosproc.c */
LONG dcall();
void dexitto();

/* dosio.c, dosbuf.c */
LONG dfhio();
LONG dflush();
LONG dbwrite();
struct MsgPort *dlport();
BPTR dlockin();

/* ramlib.c */
void rlsetup();
extern struct Library *(*rloldlib)();	/* exec's OpenLibrary */
extern struct SignalSemaphore rlalsem;	/* the alert display's turn */

/* doscrash.c */
void dcsetup();

/* dosvar.c */
void dvfree();
void dvcopy();
void dvinit();

/* The x functions other files call. */
LONG xDoPkt();
LONG xSetIoErr();
BPTR xOpen();
LONG xClose();
LONG xRead();
LONG xWrite();
BPTR xLock();
void xUnLock();
BPTR xDupLock();
BPTR xInput();
BPTR xOutput();
BPTR xLoadSeg();
void xUnLoadSeg();
APTR xAllocDosObj();
void xFreeDosObj();
struct Process *xCreateNewProc();
struct DevProc *xGetDevProc();
void xFreeDevProc();
struct DosList *xLockDosList();
void xUnLockDosList();
struct DosList *xFindDosEntry();
struct DosList *xMakeDosEntry();
LONG xAddDosEntry();
void xFreeDosEntry();
LONG xVFPrintf();
LONG xFPuts();
LONG xIsInteractive();
LONG xRunCommand();
struct CommandLineInterface *xCli();
LONG xStrToLong();
LONG xIoErr();
LONG xSeek();
LONG xExamine();
LONG xExNext();
LONG xDeleteFile();
LONG xFGetC();
LONG xUnGetC();
char *xFGets();
LONG xFlush();
char *xGetArgStr();
LONG xReadItem();
LONG xFindArg();
LONG xDeleteVar();
void xMatchEnd();
char *xFilePart();

#endif
