/* SPDX-License-Identifier: MIT */
/*
 * AmigaDOS (dos/dos.h, V36+) for Exec900.  DOS uses native pointers:
 * BPTR is an ordinary pointer and BSTR a NUL-terminated C string, so
 * BADDR and MKBADDR do nothing (docs/platform.md, DOS).
 */
#ifndef DOS_DOS_H
#define DOS_DOS_H

#include <exec/types.h>

typedef APTR BPTR;
typedef char *BSTR;

#define BADDR(x)	((APTR)(x))
#define MKBADDR(x)	((BPTR)(x))

#define DOSNAME		"dos.library"

/* The base the <lvo/dos_lib.h> macros call through; lib/dosstart.c
 * defines and opens it for DOS programs. */
extern struct DosLibrary *DOSBase;

#define DOSTRUE		(-1L)
#define DOSFALSE	(0L)

/* Open modes */
#define MODE_OLDFILE	1005L
#define MODE_NEWFILE	1006L
#define MODE_READWRITE	1004L

/* Seek origins */
#define OFFSET_BEGINNING	(-1L)
#define OFFSET_CURRENT		(0L)
#define OFFSET_END		(1L)
#define OFFSET_BEGINING		OFFSET_BEGINNING

#define BITSPERBYTE	8
#define BYTESPERLONG	4
#define BITSPERLONG	32
#define MAXINT		0x7FFFFFFFL
#define MININT		0x80000000L

/* SameLock */
#define LOCK_DIFFERENT		(-1L)
#define LOCK_SAME		(0L)
#define LOCK_SAME_VOLUME	(1L)
#define LOCK_SAME_HANDLER	LOCK_SAME_VOLUME

/* Lock modes */
#define SHARED_LOCK	(-2L)
#define ACCESS_READ	(-2L)
#define EXCLUSIVE_LOCK	(-1L)
#define ACCESS_WRITE	(-1L)

struct DateStamp {
	LONG ds_Days;		/* since 1978-01-01 */
	LONG ds_Minute;		/* since midnight */
	LONG ds_Tick;		/* 1/50 s in the minute */
};
#define TICKS_PER_SECOND	50

/* Name lengths, NUL included, as AROS's */
#define MAXFILENAMELENGTH	108
#define MAXCOMMENTLENGTH	80

struct FileInfoBlock {
	LONG fib_DiskKey;
	LONG fib_DirEntryType;	/* > 0 directory, < 0 file */
	char fib_FileName[108];	/* NUL-terminated */
	LONG fib_Protection;
	LONG fib_EntryType;
	LONG fib_Size;
	LONG fib_NumBlocks;
	struct DateStamp fib_Date;
	char fib_Comment[80];
	UWORD fib_OwnerUID;
	UWORD fib_OwnerGID;
	char fib_Reserved[32];
};

/* fib_Protection: set bits forbid (RWED), as on the Amiga */
#define FIBB_READ	3
#define FIBB_WRITE	2
#define FIBB_EXECUTE	1
#define FIBB_DELETE	0
#define FIBF_READ	(1L << FIBB_READ)
#define FIBF_WRITE	(1L << FIBB_WRITE)
#define FIBF_EXECUTE	(1L << FIBB_EXECUTE)
#define FIBF_DELETE	(1L << FIBB_DELETE)
/* set bits allow */
#define FIBB_HOLD	7
#define FIBB_SCRIPT	6
#define FIBB_PURE	5
#define FIBB_ARCHIVE	4
#define FIBF_HOLD	(1L << FIBB_HOLD)
#define FIBF_SCRIPT	(1L << FIBB_SCRIPT)
#define FIBF_PURE	(1L << FIBB_PURE)
#define FIBF_ARCHIVE	(1L << FIBB_ARCHIVE)
/* group and other bits; set bits allow, except DELETE */
#define FIBB_OTR_READ		15
#define FIBB_OTR_WRITE		14
#define FIBB_OTR_EXECUTE	13
#define FIBB_OTR_DELETE		12
#define FIBB_GRP_READ		11
#define FIBB_GRP_WRITE		10
#define FIBB_GRP_EXECUTE	9
#define FIBB_GRP_DELETE		8
#define FIBF_OTR_READ		(1L << FIBB_OTR_READ)
#define FIBF_OTR_WRITE		(1L << FIBB_OTR_WRITE)
#define FIBF_OTR_EXECUTE	(1L << FIBB_OTR_EXECUTE)
#define FIBF_OTR_DELETE		(1L << FIBB_OTR_DELETE)
#define FIBF_GRP_READ		(1L << FIBB_GRP_READ)
#define FIBF_GRP_WRITE		(1L << FIBB_GRP_WRITE)
#define FIBF_GRP_EXECUTE	(1L << FIBB_GRP_EXECUTE)
#define FIBF_GRP_DELETE		(1L << FIBB_GRP_DELETE)

/* Fault buffer size */
#define FAULT_MAX	82

struct InfoData {
	LONG id_NumSoftErrors;
	LONG id_UnitNumber;
	LONG id_DiskState;
	LONG id_NumBlocks;
	LONG id_NumBlocksUsed;
	LONG id_BytesPerBlock;
	LONG id_DiskType;
	BPTR id_VolumeNode;
	LONG id_InUse;
};

#define ID_WRITE_PROTECTED	80
#define ID_VALIDATING		81
#define ID_VALIDATED		82

#define ID_NO_DISK_PRESENT	(-1L)
#define ID_UNREADABLE_DISK	0x42414400L	/* BAD\0 */
#define ID_DOS_DISK		0x444F5300L	/* DOS\0 */
#define ID_FFS_DISK		0x444F5301L	/* DOS\1 */
#define ID_INTER_DOS_DISK	0x444F5302L	/* DOS\2 */
#define ID_INTER_FFS_DISK	0x444F5303L	/* DOS\3 */
#define ID_FASTDIR_DOS_DISK	0x444F5304L	/* DOS\4 */
#define ID_FASTDIR_FFS_DISK	0x444F5305L	/* DOS\5 */
#define ID_LONG_DOS_DISK	0x444F5306L	/* DOS\6 */
#define ID_LONG_FFS_DISK	0x444F5307L	/* DOS\7 */
#define ID_COMPLONG_FFS_DISK	0x444F5308L	/* DOS\8 */
#define ID_KICKSTART_DISK	0x4B49434BL	/* KICK */
#define ID_MSDOS_DISK		0x4D534400L	/* MSD\0 */
#define ID_NOT_REALLY_DOS	0x4E444F53L	/* NDOS */
#define ID_V7_DISK		0x56374653L	/* V7FS: COHERENT V7 (Exec900) */
#define ID_V7L_DISK		0x5637464CL	/* V7FL: V7, 30-byte names (Exec900) */

/* Errors (IoErr); long, so they pass as LONG arguments */
#define ERROR_NO_FREE_STORE		103L
#define ERROR_TASK_TABLE_FULL		105L
#define ERROR_BAD_TEMPLATE		114L
#define ERROR_BAD_NUMBER		115L
#define ERROR_REQUIRED_ARG_MISSING	116L
#define ERROR_KEY_NEEDS_ARG		117L
#define ERROR_TOO_MANY_ARGS		118L
#define ERROR_UNMATCHED_QUOTES		119L
#define ERROR_LINE_TOO_LONG		120L
#define ERROR_FILE_NOT_OBJECT		121L
#define ERROR_INVALID_RESIDENT_LIBRARY	122L
#define ERROR_NO_DEFAULT_DIR		201L
#define ERROR_OBJECT_IN_USE		202L
#define ERROR_OBJECT_EXISTS		203L
#define ERROR_DIR_NOT_FOUND		204L
#define ERROR_OBJECT_NOT_FOUND		205L
#define ERROR_BAD_STREAM_NAME		206L
#define ERROR_OBJECT_TOO_LARGE		207L
#define ERROR_ACTION_NOT_KNOWN		209L
#define ERROR_INVALID_COMPONENT_NAME	210L
#define ERROR_INVALID_LOCK		211L
#define ERROR_OBJECT_WRONG_TYPE		212L
#define ERROR_DISK_NOT_VALIDATED	213L
#define ERROR_DISK_WRITE_PROTECTED	214L
#define ERROR_RENAME_ACROSS_DEVICES	215L
#define ERROR_DIRECTORY_NOT_EMPTY	216L
#define ERROR_TOO_MANY_LEVELS		217L
#define ERROR_DEVICE_NOT_MOUNTED	218L
#define ERROR_SEEK_ERROR		219L
#define ERROR_COMMENT_TOO_BIG		220L
#define ERROR_DISK_FULL			221L
#define ERROR_DELETE_PROTECTED		222L
#define ERROR_WRITE_PROTECTED		223L
#define ERROR_READ_PROTECTED		224L
#define ERROR_NOT_A_DOS_DISK		225L
#define ERROR_NO_DISK			226L
#define ERROR_NO_MORE_ENTRIES		232L
#define ERROR_IS_SOFT_LINK		233L
#define ERROR_OBJECT_LINKED		234L
#define ERROR_BAD_HUNK			235L
#define ERROR_NOT_IMPLEMENTED		236L
#define ERROR_RECORD_NOT_LOCKED		240L
#define ERROR_LOCK_COLLISION		241L
#define ERROR_LOCK_TIMEOUT		242L
#define ERROR_UNLOCK_ERROR		243L
#define ERROR_BUFFER_OVERFLOW		303L
#define ERROR_BREAK			304L
#define ERROR_NOT_EXECUTABLE		305L

/* Return codes */
#define RETURN_OK	0
#define RETURN_WARN	5
#define RETURN_ERROR	10
#define RETURN_FAIL	20

/* Break signals: system signal bits 12-15, as on the Amiga */
#define SIGBREAKB_CTRL_C	12
#define SIGBREAKB_CTRL_D	13
#define SIGBREAKB_CTRL_E	14
#define SIGBREAKB_CTRL_F	15
#define SIGBREAKF_CTRL_C	(1L << SIGBREAKB_CTRL_C)
#define SIGBREAKF_CTRL_D	(1L << SIGBREAKB_CTRL_D)
#define SIGBREAKF_CTRL_E	(1L << SIGBREAKB_CTRL_E)
#define SIGBREAKF_CTRL_F	(1L << SIGBREAKB_CTRL_F)

/* ChangeMode types */
#define CHANGE_LOCK	0
#define CHANGE_FH	1

/* MakeLink types */
#define LINK_HARD	0
#define LINK_SOFT	1

/* AllocDosObject types */
#define DOS_FILEHANDLE		0
#define DOS_EXALLCONTROL	1
#define DOS_FIB			2
#define DOS_STDPKT		3
#define DOS_CLI			4
#define DOS_RDARGS		5

#endif
