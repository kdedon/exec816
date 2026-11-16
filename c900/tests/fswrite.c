/* SPDX-License-Identifier: MIT */
/*
 * Writing the boot volume through DOS: CreateDir, MODE_NEWFILE with a
 * case-preserved name, truncation, a 75,000-byte file through the
 * double-indirect block, MODE_READWRITE overwrite and extension,
 * exclusive use, SetProtection and SetFileDate (mode bits and mtime),
 * DeleteFile refusals, Rename of a file, of a directory
 * to another parent and of a name's case, and the volume's free blocks
 * back to their count once everything is deleted.  The emulator writes
 * the image in place, so the test first removes what an earlier run
 * left.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

#define BIG	75000L

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

static LONG
used()
{
	BPTR lk;

	lk = Lock("SYS:", SHARED_LOCK);
	Info(lk, &id);
	UnLock(lk);
	return (id.id_NumBlocksUsed);
}

static int
exsize(name, size)
char *name;
LONG size;
{
	BPTR lk;
	int ok;

	if ((lk = Lock(name, SHARED_LOCK)) == NULL)
		return (0);
	ok = Examine(lk, &fib) == DOSTRUE && fib.fib_Size == size;
	UnLock(lk);
	return (ok);
}

/* Write BIG pattern bytes in pieces of 1 to 1000 bytes. */
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
checkbig(fh)
BPTR fh;
{
	register int i;
	LONG pos, n;

	Seek(fh, 0L, OFFSET_BEGINNING);
	for (pos = 0; (n = Read(fh, buf, 1000L)) > 0; pos += n)
		for (i = 0; i < (int)n; i++)
			if (buf[i] != pat(pos + i))
				return (0);
	return (pos == BIG);
}

static void
cleanup()
{
	static char *left[] = {
		"SYS:wsub/HI", "SYS:wsub/Hi", "SYS:wsub", "SYS:wtest/sub/Hi",
		"SYS:wtest/sub", "SYS:wtest/Hello", "SYS:wtest/big",
		"SYS:wtest/x", "SYS:wtest", NULL
	};
	register char **p;

	for (p = left; *p != NULL; p++)
		DeleteFile(*p);
}

static int
wtest()
{
	BPTR fh, fh2, lk, lk2;
	struct DateStamp ds;
	LONG before;

	cleanup();
	before = used();
	CHECK((lk = CreateDir("SYS:wtest")) != NULL, "CreateDir");
	CHECK(Examine(lk, &fib) == DOSTRUE && fib.fib_DirEntryType == 2 &&
	    same(fib.fib_FileName, "wtest"), "Examine the new directory");
	UnLock(lk);
	CHECK(CreateDir("SYS:WTEST") == NULL && IoErr() == ERROR_OBJECT_EXISTS,
	    "CreateDir of an existing name");

	CHECK((fh = Open("SYS:wtest/Hello", MODE_NEWFILE)) != NULL, "MODE_NEWFILE");
	CHECK(Write(fh, "hello world\n", 12L) == 12, "Write");
	CHECK(Seek(fh, 0L, OFFSET_BEGINNING) == 12 && Read(fh, buf, 20L) == 12 &&
	    buf[0] == 'h' && buf[11] == '\n', "read back through the writer");
	Close(fh);
	CHECK(exsize("sys:WTEST/hello", 12L), "size after Close");
	lk = Lock("SYS:wtest/hello", SHARED_LOCK);
	CHECK(Examine(lk, &fib) == DOSTRUE && same(fib.fib_FileName, "Hello"),
	    "the name keeps its case");
	UnLock(lk);

	CHECK((fh = Open("SYS:wtest/x", MODE_NEWFILE)) != NULL, "a second file");
	Write(fh, "xyz", 3L);
	CHECK(Open("SYS:wtest/x", MODE_OLDFILE) == NULL &&
	    IoErr() == ERROR_OBJECT_IN_USE, "MODE_NEWFILE is exclusive");
	CHECK(DeleteFile("SYS:wtest/x") == DOSFALSE &&
	    IoErr() == ERROR_OBJECT_IN_USE, "DeleteFile of an open file");
	Close(fh);
	CHECK((fh = Open("SYS:wtest/x", MODE_NEWFILE)) != NULL &&
	    Write(fh, "q", 1L) == 1, "MODE_NEWFILE again");
	Close(fh);
	CHECK(exsize("SYS:wtest/x", 1L), "truncation");
	ds.ds_Days = 3000;
	ds.ds_Minute = 61;
	ds.ds_Tick = 150;
	CHECK(SetProtection("SYS:wtest/x", FIBF_EXECUTE | FIBF_WRITE) ==
	    DOSTRUE && SetFileDate("SYS:wtest/x", &ds) == DOSTRUE,
	    "SetProtection and SetFileDate");
	lk = Lock("SYS:wtest/x", SHARED_LOCK);
	CHECK(Examine(lk, &fib) == DOSTRUE && fib.fib_Protection ==
	    (FIBF_EXECUTE | FIBF_WRITE | FIBF_DELETE) &&
	    fib.fib_Date.ds_Days == 3000 && fib.fib_Date.ds_Minute == 61 &&
	    fib.fib_Date.ds_Tick == 150, "the mode bits and date");
	UnLock(lk);
	CHECK(SetProtection("SYS:wtest/x", 0L) == DOSTRUE &&
	    SetProtection("SYS:wtest/none", 0L) == DOSFALSE &&
	    IoErr() == ERROR_OBJECT_NOT_FOUND, "SetProtection back");
	lk = Lock("SYS:wtest/x", SHARED_LOCK);
	CHECK(Examine(lk, &fib) == DOSTRUE && fib.fib_Protection == 0 &&
	    fib.fib_Date.ds_Days == 3000, "protection back, date kept");
	UnLock(lk);
	lk = Lock("SYS:wtest/x", SHARED_LOCK);
	CHECK(Lock("SYS:wtest/x", EXCLUSIVE_LOCK) == NULL &&
	    IoErr() == ERROR_OBJECT_IN_USE, "exclusive lock refused");
	UnLock(lk);
	CHECK((lk = Lock("SYS:wtest/x", EXCLUSIVE_LOCK)) != NULL,
	    "exclusive lock");
	CHECK(Lock("SYS:wtest/x", SHARED_LOCK) == NULL, "shared under exclusive");
	UnLock(lk);
	CHECK(DeleteFile("SYS:wtest/x") == DOSTRUE &&
	    Lock("SYS:wtest/x", SHARED_LOCK) == NULL, "DeleteFile");

	CHECK((fh = Open("SYS:wtest/big", MODE_NEWFILE)) != NULL, "big file");
	CHECK(writebig(fh), "writing 75,000 bytes");
	CHECK(checkbig(fh), "reading them back");
	Close(fh);
	CHECK(exsize("SYS:wtest/big", BIG), "big file size");
	CHECK(used() >= before + 1 + 1 + 147 + 3, "blocks taken");
	CHECK((fh = Open("SYS:wtest/big", MODE_READWRITE)) != NULL,
	    "MODE_READWRITE");
	CHECK((fh2 = Open("SYS:wtest/big", MODE_OLDFILE)) != NULL,
	    "a reader beside it");
	Seek(fh, 70000L, OFFSET_BEGINNING);
	CHECK(Write(fh, "ZZZZ", 4L) == 4, "overwrite");
	CHECK(Seek(fh2, 69998L, OFFSET_BEGINNING) == 0 &&
	    Read(fh2, buf, 8L) == 8 && buf[2] == 'Z' && buf[5] == 'Z' &&
	    buf[6] == pat(70004L), "the reader sees it");
	Close(fh2);
	CHECK(Seek(fh, 0L, OFFSET_END) == 70004L && Write(fh, "end", 3L) == 3,
	    "extend");
	Close(fh);
	CHECK(exsize("SYS:wtest/big", BIG + 3), "extended size");

	CHECK(DeleteFile("SYS:wtest") == DOSFALSE &&
	    IoErr() == ERROR_DIRECTORY_NOT_EMPTY, "non-empty directory");
	CHECK((lk = CreateDir("SYS:wtest/sub")) != NULL, "subdirectory");
	UnLock(lk);
	CHECK(Rename("SYS:wtest/Hello", "SYS:wtest/sub/Hi") == DOSTRUE &&
	    Lock("SYS:wtest/Hello", SHARED_LOCK) == NULL &&
	    exsize("SYS:wtest/sub/Hi", 12L), "Rename a file");
	CHECK(Rename("SYS:wtest/sub", "SYS:wtest/sub/in") == DOSFALSE &&
	    IoErr() == ERROR_OBJECT_IN_USE, "a directory into itself");
	CHECK(Rename("SYS:wtest/sub", "SYS:wsub") == DOSTRUE &&
	    exsize("SYS:wsub/Hi", 12L), "Rename a directory");
	CHECK((lk = Lock("SYS:wsub/Hi", SHARED_LOCK)) != NULL &&
	    (lk2 = ParentDir(lk)) != NULL, "lock in the moved directory");
	UnLock(lk);
	CHECK((lk = ParentDir(lk2)) != NULL && Examine(lk, &fib) == DOSTRUE &&
	    fib.fib_DirEntryType == 1, "its \"..\" names the new parent");
	UnLock(lk);
	UnLock(lk2);
	CHECK(Rename("SYS:wsub/Hi", "SYS:wsub/HI") == DOSTRUE &&
	    (lk = Lock("SYS:wsub/hi", SHARED_LOCK)) != NULL &&
	    Examine(lk, &fib) == DOSTRUE && same(fib.fib_FileName, "HI"),
	    "a new spelling");
	UnLock(lk);
	CHECK(Rename("SYS:wsub/HI", "SYS:wtest/big") == DOSFALSE &&
	    IoErr() == ERROR_OBJECT_EXISTS, "Rename over a file");

	CHECK(DeleteFile("SYS:wsub/HI") == DOSTRUE &&
	    DeleteFile("SYS:wsub") == DOSTRUE &&
	    DeleteFile("SYS:wtest/big") == DOSTRUE &&
	    DeleteFile("SYS:wtest") == DOSTRUE, "deleting everything");
	CHECK(used() == before, "every block freed");
	CHECK((fh = Open("SYS:Devs/Data/big", MODE_OLDFILE)) != NULL,
	    "the old file");
	Seek(fh, 70600L, OFFSET_BEGINNING);
	CHECK(Read(fh, buf, 4L) == 4 &&
	    buf[0] == (UBYTE)((70600L * 7 + (70600L >> 9)) & 0xFF),
	    "the old file intact");
	Close(fh);
	return (0);
}

static LONG
pmain(args, len)
char *args;
LONG len;
{
	return ((LONG)wtest());
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
		KPutStr("FAIL fswrite: no dos.library\n");
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
		KPutStr("FAIL fswrite: CreateNewProc\n");
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	if (s.s_RC != 0) {
		xKPrintf("FAIL fswrite: %s\n", why);
		return (1);
	}
	KPutStr("PASS fswrite\n");
	return (0);
}
