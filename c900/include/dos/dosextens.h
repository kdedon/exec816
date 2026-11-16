/* SPDX-License-Identifier: MIT */
/*
 * AmigaDOS records (dos/dosextens.h, V36+) for Exec900: Amiga's shapes
 * and names, with native pointers where Amiga has BPTRs and C strings
 * where it has BSTRs (docs/platform.md, DOS).
 */
#ifndef DOS_DOSEXTENS_H
#define DOS_DOSEXTENS_H

#include <exec/exec.h>
#include <dos/dos.h>

struct Process {
	struct Task pr_Task;
	struct MsgPort pr_MsgPort;	/* packets; signal SIGB_DOS */
	WORD pr_Pad;
	BPTR pr_SegList;		/* the seglist it runs, or NULL */
	LONG pr_StackSize;		/* bytes */
	APTR pr_GlobVec;
	LONG pr_TaskNum;		/* CLI number, or 0 */
	BPTR pr_StackBase;
	LONG pr_Result2;		/* IoErr */
	BPTR pr_CurrentDir;
	BPTR pr_CIS;
	BPTR pr_COS;
	APTR pr_ConsoleTask;		/* CON: handler port */
	APTR pr_FileSystemTask;		/* default filesystem port */
	BPTR pr_CLI;			/* struct CommandLineInterface * */
	APTR pr_ReturnAddr;
	APTR pr_PktWait;
	APTR pr_WindowPtr;
	BPTR pr_HomeDir;
	LONG pr_Flags;			/* PRF_ */
	void (*pr_ExitCode)();
	LONG pr_ExitData;
	UBYTE *pr_Arguments;
	struct MinList pr_LocalVars;
	ULONG pr_ShellPrivate;
	BPTR pr_CES;
};

#define PRB_FREESEGLIST		0
#define PRF_FREESEGLIST		1L
#define PRB_FREECURRDIR		1
#define PRF_FREECURRDIR		2L
#define PRB_FREECLI		2
#define PRF_FREECLI		4L
#define PRB_CLOSEINPUT		3
#define PRF_CLOSEINPUT		8L
#define PRB_CLOSEOUTPUT		4
#define PRF_CLOSEOUTPUT		16L
#define PRB_FREEARGS		5
#define PRF_FREEARGS		32L

/* A file handle.  fh_Port is nonzero for an interactive stream; fh_Type
 * is the handler's port; fh_Args is the handler's own value, passed back
 * as dp_Arg1 of every packet on the handle. */
struct FileHandle {
	struct Message *fh_Link;
	struct MsgPort *fh_Port;
	struct MsgPort *fh_Type;
	LONG fh_Buf;
	LONG fh_Pos;
	LONG fh_End;
	LONG fh_Funcs;
	LONG fh_Func2;
	LONG fh_Func3;
	LONG fh_Args;
	LONG fh_Arg2;
};
#define fh_Func1	fh_Funcs
#define fh_Arg1		fh_Args

struct DosPacket {
	struct Message *dp_Link;
	struct MsgPort *dp_Port;	/* reply port; the handler's on reply */
	LONG dp_Type;			/* ACTION_ */
	LONG dp_Res1;
	LONG dp_Res2;
	LONG dp_Arg1;
	LONG dp_Arg2;
	LONG dp_Arg3;
	LONG dp_Arg4;
	LONG dp_Arg5;
	LONG dp_Arg6;
	LONG dp_Arg7;
};
#define dp_Action	dp_Type
#define dp_Status	dp_Res1
#define dp_Status2	dp_Res2
#define dp_BufAddr	dp_Arg1

/* The message's ln_Name points at the packet, as on the Amiga. */
struct StandardPacket {
	struct Message sp_Msg;
	struct DosPacket sp_Pkt;
};

#define ACTION_NIL		0L
#define ACTION_STARTUP		0L
#define ACTION_GET_BLOCK	2L
#define ACTION_SET_MAP		4L
#define ACTION_DIE		5L
#define ACTION_EVENT		6L
#define ACTION_CURRENT_VOLUME	7L
#define ACTION_LOCATE_OBJECT	8L
#define ACTION_RENAME_DISK	9L
#define ACTION_WRITE		87L
#define ACTION_READ		82L
#define ACTION_FREE_LOCK	15L
#define ACTION_DELETE_OBJECT	16L
#define ACTION_RENAME_OBJECT	17L
#define ACTION_MORE_CACHE	18L
#define ACTION_COPY_DIR		19L
#define ACTION_WAIT_CHAR	20L
#define ACTION_SET_PROTECT	21L
#define ACTION_CREATE_DIR	22L
#define ACTION_EXAMINE_OBJECT	23L
#define ACTION_EXAMINE_NEXT	24L
#define ACTION_DISK_INFO	25L
#define ACTION_INFO		26L
#define ACTION_FLUSH		27L
#define ACTION_SET_COMMENT	28L
#define ACTION_PARENT		29L
#define ACTION_TIMER		30L
#define ACTION_INHIBIT		31L
#define ACTION_DISK_TYPE	32L
#define ACTION_DISK_CHANGE	33L
#define ACTION_SET_DATE		34L
#define ACTION_SAME_LOCK	40L
#define ACTION_SCREEN_MODE	994L
#define ACTION_CHANGE_SIGNAL	995L
#define ACTION_READ_RETURN	1001L
#define ACTION_WRITE_RETURN	1002L
#define ACTION_SEEK		1008L
#define ACTION_FINDUPDATE	1004L
#define ACTION_FINDINPUT	1005L
#define ACTION_FINDOUTPUT	1006L
#define ACTION_END		1007L
#define ACTION_FORMAT		1020L
#define ACTION_MAKE_LINK	1021L
#define ACTION_SET_FILE_SIZE	1022L
#define ACTION_WRITE_PROTECT	1023L
#define ACTION_READ_LINK	1024L
#define ACTION_FH_FROM_LOCK	1026L
#define ACTION_IS_FILESYSTEM	1027L
#define ACTION_CHANGE_MODE	1028L
#define ACTION_COPY_DIR_FH	1030L
#define ACTION_PARENT_FH	1031L
#define ACTION_EXAMINE_ALL	1033L
#define ACTION_EXAMINE_FH	1034L

/* A lock, allocated by its handler; fl_Key is the handler's. */
struct FileLock {
	BPTR fl_Link;
	LONG fl_Key;
	LONG fl_Access;
	struct MsgPort *fl_Task;
	BPTR fl_Volume;
};

struct CommandLineInterface {
	LONG cli_Result2;
	BSTR cli_SetName;		/* current directory's name */
	BPTR cli_CommandDir;		/* struct PathList *, NULL: C: only */
	LONG cli_ReturnCode;
	BSTR cli_CommandName;
	LONG cli_FailLevel;
	BSTR cli_Prompt;
	BPTR cli_StandardInput;
	BPTR cli_CurrentInput;
	BSTR cli_CommandFile;
	LONG cli_Interactive;
	LONG cli_Background;
	BPTR cli_CurrentOutput;
	LONG cli_DefaultStack;		/* longwords, as on the Amiga */
	BPTR cli_StandardOutput;
	BPTR cli_Module;
};
/* An entry of cli_CommandDir, the command path (AllocVec'd). */
struct PathNode {
	BPTR pn_Next;			/* struct PathNode * */
	BPTR pn_Lock;
};

/* A resident segment (AddSegment); di_ResList is the list. */
struct Segment {
	BPTR seg_Next;
	LONG seg_UC;			/* users, or CMD_ */
	BPTR seg_Seg;
	UBYTE seg_Name[4];		/* NUL-terminated, as long as needed */
};
#define CMD_SYSTEM	(-1L)
#define CMD_INTERNAL	(-2L)
#define CMD_DISABLED	(-999L)

/* Sizes of the strings AllocDosObject(DOS_CLI) provides. */
#define CLI_SETNAME	80
#define CLI_COMMANDNAME	64
#define CLI_PROMPT	32
#define CLI_COMMANDFILE	64

struct DosLibrary {
	struct Library dl_lib;
	struct RootNode *dl_Root;
	APTR dl_GV;
	LONG dl_A2;
	LONG dl_A5;
	LONG dl_A6;
	APTR dl_Errors;
	APTR dl_TimeReq;
	struct Library *dl_UtilityBase;
	struct Library *dl_IntuitionBase;
};

struct RootNode {
	BPTR rn_TaskArray;
	BPTR rn_ConsoleSegment;
	struct DateStamp rn_Time;
	LONG rn_RestartSeg;
	BPTR rn_Info;			/* struct DosInfo * */
	BPTR rn_FileHandlerSegment;
	struct MinList rn_CliList;
	struct MsgPort *rn_BootProc;
	BPTR rn_ShellSegment;
	LONG rn_Flags;			/* RNF_ */
};
#define RNB_WILDSTAR	24	/* "*" is a wildcard, as "#?" */
#define RNF_WILDSTAR	(1L << RNB_WILDSTAR)

struct DosInfo {
	BPTR di_McName;			/* di_ResList: struct Segment * */
	BPTR di_DevInfo;		/* the DosList */
	BPTR di_Devices;
	BPTR di_Handlers;
	APTR di_NetHand;
	struct SignalSemaphore di_DevLock;
	struct SignalSemaphore di_EntryLock;
	struct SignalSemaphore di_DeleteLock;
};

#define di_ResList	di_McName

struct AssignList {
	struct AssignList *al_Next;
	BPTR al_Lock;
};

/* A device, volume or assign. */
struct DosList {
	BPTR dol_Next;
	LONG dol_Type;			/* DLT_ */
	struct MsgPort *dol_Task;
	BPTR dol_Lock;
	union {
		struct {
			BSTR dol_Handler;
			LONG dol_StackSize;	/* bytes */
			LONG dol_Priority;
			ULONG dol_Startup;
			BPTR dol_SegList;
			BPTR dol_GlobVec;
		} dol_handler;
		struct {
			struct DateStamp dol_VolumeDate;
			BPTR dol_LockList;
			LONG dol_DiskType;
		} dol_volume;
		struct {
			UBYTE *dol_AssignName;
			struct AssignList *dol_List;
		} dol_assign;
	} dol_misc;
	BSTR dol_Name;			/* without the colon */
};

#define DLT_DEVICE	0
#define DLT_DIRECTORY	1
#define DLT_VOLUME	2
#define DLT_LATE	3
#define DLT_NONBINDING	4
#define DLT_PRIVATE	(-1)

struct DevProc {
	struct MsgPort *dvp_Port;
	BPTR dvp_Lock;
	ULONG dvp_Flags;
	struct DosList *dvp_DevNode;
};
#define DVPB_UNLOCK	0
#define DVPF_UNLOCK	1L
#define DVPB_ASSIGN	1
#define DVPF_ASSIGN	2L

/* Directory entry types, as in fib_DirEntryType. */
#define ST_ROOT		1
#define ST_USERDIR	2
#define ST_SOFTLINK	3	/* looks like a directory, may name a file */
#define ST_LINKDIR	4	/* hard link to a directory */
#define ST_FILE		(-3)	/* negative for files */
#define ST_LINKFILE	(-4)	/* hard link to a file */
#define ST_PIPEFILE	(-5)	/* pipe that supports ExamineFH */

#define LDB_READ	0
#define LDB_WRITE	1
#define LDB_DEVICES	2
#define LDB_VOLUMES	3
#define LDB_ASSIGNS	4
#define LDB_ENTRY	5
#define LDB_DELETE	6
#define LDF_READ	(1L << LDB_READ)
#define LDF_WRITE	(1L << LDB_WRITE)
#define LDF_DEVICES	(1L << LDB_DEVICES)
#define LDF_VOLUMES	(1L << LDB_VOLUMES)
#define LDF_ASSIGNS	(1L << LDB_ASSIGNS)
#define LDF_ENTRY	(1L << LDB_ENTRY)
#define LDF_DELETE	(1L << LDB_DELETE)
#define LDF_ALL		(LDF_DEVICES | LDF_VOLUMES | LDF_ASSIGNS)

#endif
