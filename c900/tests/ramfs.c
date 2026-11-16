/* SPDX-License-Identifier: MIT */
/*
 * RAM: through DOS: the volume "Ram Disk", files growing across chunks
 * in pieces of 1 to 1,000 bytes, Seek, SetFileSize down and up, shared
 * and exclusive use, directories, Examine/ExNext, Rename (also across
 * directories), SetProtection (enforced), SetComment, SetFileDate,
 * Info, ParentDir, SameLock, a multi-directory assign, and every byte
 * of memory back after the files are deleted.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

#define BIG	30000L

static char *why;
static struct FileInfoBlock fib;
static struct InfoData id;
static UBYTE buf[1000];

#define CHECK(c, w)	if (!(c)) { why = w; return (1); }

static int
pat(pos)
LONG pos;
{
	return ((int)((pos * 13 + (pos >> 9)) & 0xFF));
}

static int
same(a, b)
register char *a, *b;
{
	while (*a == *b && *a != '\0')
		a++, b++;
	return (*a == *b);
}

static int
exam(name)
char *name;
{
	BPTR lk;
	int ok;

	if ((lk = Lock(name, SHARED_LOCK)) == NULL)
		return (0);
	ok = Examine(lk, &fib) == DOSTRUE;
	UnLock(lk);
	return (ok);
}

static int
writebig(fh)
BPTR fh;
{
	register int i, k;
	LONG pos;

	for (pos = 0, k = 1; pos < BIG; pos += k, k = k * 7 % 1000 + 1) {
		if (k > BIG - pos)
			k = BIG - pos;
		for (i = 0; i < k; i++)
			buf[i] = pat(pos + i);
		if (Write(fh, buf, (LONG)k) != k)
			return (0);
	}
	return (1);
}

static int
checkbig(fh, size)
BPTR fh;
LONG size;
{
	register int i;
	LONG pos, n;

	Seek(fh, 0L, OFFSET_BEGINNING);
	for (pos = 0; (n = Read(fh, buf, 777L)) > 0; pos += n)
		for (i = 0; i < (int)n; i++)
			if (buf[i] != (pos + i < BIG ? pat(pos + i) : 0))
				return (0);
	return (pos == size);
}

/* Names in directory name, in order, as one string with '.' after each. */
static int
names(name, want)
char *name, *want;
{
	BPTR lk;
	char all[80], *p, *s;
	int ok;

	if ((lk = Lock(name, SHARED_LOCK)) == NULL || Examine(lk, &fib) ==
	    DOSFALSE)
		return (0);
	for (p = all; ExNext(lk, &fib) == DOSTRUE; *p++ = '.')
		for (s = fib.fib_FileName; *s != '\0'; )
			*p++ = *s++;
	*p = '\0';
	ok = IoErr() == ERROR_NO_MORE_ENTRIES && same(all, want);
	UnLock(lk);
	return (ok);
}

static int
rtest()
{
	BPTR fh, fh2, lk, lk2;
	struct DateStamp ds;
	ULONG before;

	CHECK((lk = Lock("RAM:", SHARED_LOCK)) != NULL &&
	    Examine(lk, &fib) == DOSTRUE && fib.fib_DirEntryType == 1 &&
	    same(fib.fib_FileName, "Ram Disk"), "RAM: is the volume Ram Disk");
	CHECK(Info(lk, &id) == DOSTRUE && id.id_DiskState == ID_VALIDATED &&
	    id.id_BytesPerBlock == 512 && id.id_NumBlocks == id.id_NumBlocksUsed,
	    "Info");
	UnLock(lk);
	CHECK(exam("Ram Disk:") && fib.fib_DirEntryType == 1, "the volume name");
	before = AvailMem(MEMF_ANY);

	CHECK((fh = Open("RAM:Big", MODE_NEWFILE)) != NULL, "MODE_NEWFILE");
	CHECK(writebig(fh), "writing 30,000 bytes");
	CHECK(checkbig(fh, BIG), "reading them back");
	CHECK(Open("RAM:big", MODE_OLDFILE) == NULL &&
	    IoErr() == ERROR_OBJECT_IN_USE, "MODE_NEWFILE is exclusive");
	CHECK(DeleteFile("RAM:Big") == DOSFALSE &&
	    IoErr() == ERROR_OBJECT_IN_USE, "delete an open file");
	Close(fh);
	CHECK(exam("ram:BIG") && fib.fib_Size == BIG &&
	    same(fib.fib_FileName, "Big") && fib.fib_DirEntryType == -3,
	    "Examine the file");
	CHECK((fh = Open("RAM:Big", MODE_READWRITE)) != NULL &&
	    (fh2 = Open("RAM:Big", MODE_OLDFILE)) != NULL, "shared opens");
	CHECK(Seek(fh, 20000L, OFFSET_BEGINNING) == 0 &&
	    Write(fh, "ZZZZ", 4L) == 4, "overwrite");
	CHECK(Seek(fh2, 19998L, OFFSET_BEGINNING) == 0 &&
	    Read(fh2, buf, 8L) == 8 && buf[2] == 'Z' && buf[5] == 'Z' &&
	    buf[6] == pat(20004L), "the reader sees it");
	CHECK(Seek(fh2, BIG + 1, OFFSET_BEGINNING) == -1 &&
	    IoErr() == ERROR_SEEK_ERROR, "Seek past the end");
	CHECK(Seek(fh2, 0L, OFFSET_END) == 20006L && Read(fh2, buf, 8L) == 0,
	    "Seek to the end");
	Close(fh2);
	CHECK(SetFileSize(fh, 1000L, OFFSET_BEGINNING) == 1000 &&
	    Seek(fh, 0L, OFFSET_CURRENT) == 1000, "SetFileSize down");
	CHECK(SetFileSize(fh, 500L, OFFSET_CURRENT) == 1500 &&
	    Seek(fh, 0L, OFFSET_BEGINNING) == 1000, "SetFileSize up");
	CHECK(Read(fh, buf, 1000L) == 1000 && buf[0] == pat(0L) &&
	    buf[999] == pat(999L), "the bytes kept");
	CHECK(Read(fh, buf, 1000L) == 500 && buf[0] == 0 && buf[499] == 0,
	    "zero filled");
	Close(fh);

	CHECK((lk = CreateDir("RAM:Dir")) != NULL &&
	    Lock("RAM:Dir", SHARED_LOCK) == NULL, "CreateDir's lock is exclusive");
	UnLock(lk);
	CHECK(CreateDir("RAM:dir") == NULL && IoErr() == ERROR_OBJECT_EXISTS,
	    "CreateDir of an existing name");
	CHECK((lk = CreateDir("RAM:Dir/Sub")) != NULL, "a subdirectory");
	UnLock(lk);
	CHECK((fh = Open("RAM:Dir/a", MODE_NEWFILE)) != NULL &&
	    Write(fh, "abc", 3L) == 3, "a file in it");
	Close(fh);
	CHECK((fh = Open("RAM:Dir/b", MODE_READWRITE)) != NULL, "MODE_READWRITE");
	Close(fh);
	CHECK(names("RAM:Dir", "Sub.a.b."), "ExNext");
	CHECK(names("RAM:", "Big.Dir."), "ExNext of the root");
	CHECK(Open("RAM:Dir", MODE_OLDFILE) == NULL &&
	    IoErr() == ERROR_OBJECT_WRONG_TYPE, "Open a directory");
	CHECK(Lock("RAM:none/x", SHARED_LOCK) == NULL &&
	    IoErr() == ERROR_DIR_NOT_FOUND, "missing directory");
	CHECK(Open("RAM:Dir/a/x", MODE_NEWFILE) == NULL &&
	    IoErr() == ERROR_OBJECT_WRONG_TYPE, "a file as a directory");
	CHECK(Open("RAM:Dir/1234567890123456789012345678901", MODE_NEWFILE) ==
	    NULL && IoErr() == ERROR_INVALID_COMPONENT_NAME, "a long name");
	CHECK(DeleteFile("RAM:Dir") == DOSFALSE &&
	    IoErr() == ERROR_DIRECTORY_NOT_EMPTY, "a non-empty directory");

	CHECK((lk = Lock("RAM:Dir/Sub", SHARED_LOCK)) != NULL &&
	    (lk2 = ParentDir(lk)) != NULL && Examine(lk2, &fib) == DOSTRUE &&
	    same(fib.fib_FileName, "Dir"), "ParentDir");
	UnLock(lk);
	CHECK((lk = Lock("RAM:dir", SHARED_LOCK)) != NULL &&
	    SameLock(lk, lk2) == LOCK_SAME, "SameLock");
	UnLock(lk2);
	CHECK((lk2 = Lock("RAM:Big", SHARED_LOCK)) != NULL &&
	    SameLock(lk, lk2) == LOCK_SAME_VOLUME, "SameLock, another object");
	UnLock(lk2);
	CHECK((lk2 = Lock("RAM:", SHARED_LOCK)) != NULL &&
	    ParentDir(lk2) == NULL && IoErr() == 0, "the root has no parent");
	UnLock(lk2);
	CHECK((lk2 = Lock("RAM:Dir/Sub//a", SHARED_LOCK)) != NULL, "'/' is up");
	UnLock(lk2);

	/* relative to a current directory */
	lk2 = CurrentDir(lk);
	CHECK(exam("a") && fib.fib_Size == 3 && exam("/Big"), "relative names");
	CurrentDir(lk2);
	UnLock(lk);

	CHECK(Rename("RAM:Dir/a", "RAM:Dir/Sub/A2") == DOSTRUE &&
	    exam("RAM:Dir/Sub/a2") && fib.fib_Size == 3, "Rename across");
	CHECK(Rename("RAM:Dir/b", "RAM:Dir/B") == DOSTRUE &&
	    exam("RAM:Dir/b") && same(fib.fib_FileName, "B"), "a new spelling");
	CHECK(Rename("RAM:Dir/B", "RAM:Big") == DOSFALSE &&
	    IoErr() == ERROR_OBJECT_EXISTS, "Rename over a file");
	CHECK(Rename("RAM:Dir", "RAM:Dir/Sub/In") == DOSFALSE &&
	    IoErr() == ERROR_OBJECT_IN_USE, "a directory into itself");
	CHECK(Rename("RAM:Dir/Sub", "RAM:Sub") == DOSTRUE &&
	    names("RAM:", "Big.Dir.Sub.") && names("RAM:Sub", "A2."),
	    "Rename a directory");

	CHECK(SetProtection("RAM:Big", FIBF_DELETE | FIBF_WRITE | FIBF_PURE) ==
	    DOSTRUE && exam("RAM:Big") &&
	    fib.fib_Protection == (FIBF_DELETE | FIBF_WRITE | FIBF_PURE),
	    "SetProtection");
	CHECK(DeleteFile("RAM:Big") == DOSFALSE &&
	    IoErr() == ERROR_DELETE_PROTECTED, "delete-protected");
	CHECK(Open("RAM:Big", MODE_NEWFILE) == NULL &&
	    IoErr() == ERROR_WRITE_PROTECTED, "write-protected");
	CHECK((fh = Open("RAM:Big", MODE_READWRITE)) != NULL &&
	    Write(fh, "x", 1L) == -1 && IoErr() == ERROR_WRITE_PROTECTED,
	    "Write refused");
	Close(fh);
	CHECK(SetProtection("RAM:Big", FIBF_READ) == DOSTRUE &&
	    (fh = Open("RAM:Big", MODE_OLDFILE)) != NULL &&
	    Read(fh, buf, 1L) == -1 && IoErr() == ERROR_READ_PROTECTED,
	    "read-protected");
	Close(fh);
	CHECK(SetProtection("RAM:Big", 0L) == DOSTRUE, "protection back");
	CHECK(SetComment("RAM:Big", "a comment") == DOSTRUE && exam("RAM:Big") &&
	    same(fib.fib_Comment, "a comment"), "SetComment");
	CHECK(SetComment("RAM:Big", "") == DOSTRUE && exam("RAM:Big") &&
	    fib.fib_Comment[0] == '\0', "an empty comment");
	ds.ds_Days = 1234;
	ds.ds_Minute = 567;
	ds.ds_Tick = 89;
	CHECK(SetFileDate("RAM:Big", &ds) == DOSTRUE && exam("RAM:Big") &&
	    fib.fib_Date.ds_Days == 1234 && fib.fib_Date.ds_Minute == 567 &&
	    fib.fib_Date.ds_Tick == 89, "SetFileDate");
	CHECK(SetProtection("RAM:none", 0L) == DOSFALSE &&
	    IoErr() == ERROR_OBJECT_NOT_FOUND, "SetProtection of nothing");

	/* a multi-directory assign: RAM:Dir, then RAM:Sub */
	CHECK(AssignLock("RTEST", Lock("RAM:Dir", SHARED_LOCK)) == DOSTRUE &&
	    AssignAdd("RTEST", Lock("RAM:Sub", SHARED_LOCK)) == DOSTRUE,
	    "AssignAdd");
	CHECK(exam("RTEST:A2") && exam("RTEST:B"), "both directories searched");
	CHECK((fh = Open("RTEST:A2", MODE_OLDFILE)) != NULL, "Open through it");
	Close(fh);
	CHECK(Lock("RTEST:none", SHARED_LOCK) == NULL &&
	    IoErr() == ERROR_OBJECT_NOT_FOUND, "not found in either");
	CHECK((lk = Lock("RAM:Dir", SHARED_LOCK)) != NULL &&
	    RemAssignList("RTEST", lk) == DOSTRUE && !exam("RTEST:B") &&
	    exam("RTEST:A2"), "RemAssignList of the first");
	CHECK(RemAssignList("RTEST", lk) == DOSFALSE, "not in the list");
	UnLock(lk);
	CHECK(AssignLock("RTEST", (BPTR)NULL) == DOSTRUE && !exam("RTEST:A2"),
	    "remove the assign");
	CHECK(AssignAdd("RTEST", (BPTR)NULL) == DOSFALSE, "AssignAdd needs one");

	CHECK(DeleteFile("RAM:Sub/A2") == DOSTRUE &&
	    DeleteFile("RAM:Sub") == DOSTRUE, "deleting Sub");
	CHECK(DeleteFile("RAM:Dir/B") == DOSTRUE &&
	    DeleteFile("RAM:Dir") == DOSTRUE &&
	    DeleteFile("RAM:Big") == DOSTRUE, "deleting the rest");
	CHECK(names("RAM:", ""), "an empty root");
	CHECK(AvailMem(MEMF_ANY) == before, "memory back");
	return (0);
}

static LONG
pmain(args, len)
char *args;
LONG len;
{
	return ((LONG)rtest());
}

struct dsync {
	struct Task *s_Task;
	LONG s_RC;
};

static void
pdone(rc, s)
LONG rc;
struct dsync *s;
{
	s->s_RC = rc;
	Signal(s->s_Task, SIGBREAKF_CTRL_F);
}

int
main()
{
	struct TagItem t[4];
	struct dsync s;

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 36L))
	    == NULL) {
		KPutStr("FAIL ramfs: no dos.library\n");
		return (1);
	}
	s.s_Task = FindTask(NULL);
	s.s_RC = -1;
	SetSignal(0L, SIGBREAKF_CTRL_F);
	t[0].ti_Tag = NP_Entry;
	t[0].ti_Data = (ULONG)pmain;
	t[1].ti_Tag = NP_ExitCode;
	t[1].ti_Data = (ULONG)pdone;
	t[2].ti_Tag = NP_ExitData;
	t[2].ti_Data = (ULONG)&s;
	t[3].ti_Tag = TAG_DONE;
	if (CreateNewProc(t) == NULL) {
		KPutStr("FAIL ramfs: CreateNewProc\n");
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	if (s.s_RC != 0) {
		xKPrintf("FAIL ramfs: %s\n", why);
		return (1);
	}
	KPutStr("PASS ramfs\n");
	return (0);
}
