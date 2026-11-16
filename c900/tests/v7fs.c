/* SPDX-License-Identifier: MIT */
/*
 * The V7 filesystem handler on the boot volume, through DOS: SYS: bound
 * on first use, Examine, ExNext and Info, case-insensitive paths with
 * parent and root references, CurrentDir, the C: assign, reads and
 * seeks across the direct, indirect and double-indirect blocks of an
 * 80,000-byte pattern file, errors and LoadSeg.  tests/fswrite writes.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

#define BIGSIZE	80000L

static char *why;
static struct FileInfoBlock fib;
static struct InfoData id;

#define CHECK(c, w)	if (!(c)) { why = w; return (1); }

static int
pat(pos)
LONG pos;
{
	return ((int)((pos * 7 + (pos >> 9)) & 0xFF));
}

static int
same(a, b)
register char *a, *b;
{
	while (*a == *b && *a != '\0')
		a++, b++;
	return (*a == *b);
}

/* Read n bytes at pos and check them against the pattern. */
static int
readat(fh, pos, n)
BPTR fh;
LONG pos;
int n;
{
	UBYTE buf[64];
	register int i;

	if (Seek(fh, pos, OFFSET_BEGINNING) < 0 || Read(fh, buf, (LONG)n) != n)
		return (0);
	for (i = 0; i < n; i++)
		if (buf[i] != pat(pos + i))
			return (0);
	return (1);
}

static int
ftest()
{
	BPTR lk, lk2, fh, old;
	UBYTE *big, *p;
	LONG n, i;
	int dirs, files, found;
	BPTR seg;

	CHECK((lk = Lock("SYS:", SHARED_LOCK)) != NULL, "Lock SYS:");
	CHECK(Examine(lk, &fib) == DOSTRUE && fib.fib_DirEntryType == 1 &&
	    same(fib.fib_FileName, "System"), "Examine the root");
	CHECK(Info(lk, &id) == DOSTRUE && id.id_DiskType == ID_V7_DISK &&
	    id.id_BytesPerBlock == 512 && id.id_NumBlocks == 1024 &&
	    id.id_DiskState == ID_VALIDATED, "Info");
	dirs = files = found = 0;
	while (ExNext(lk, &fib) == DOSTRUE) {
		if (fib.fib_DirEntryType > 0)
			dirs++;
		else
			files++;
		if (same(fib.fib_FileName, "exec900.sys") &&
		    fib.fib_DirEntryType < 0 && fib.fib_Size > 40000)
			found |= 1;
		if (same(fib.fib_FileName, "Libs") && fib.fib_DirEntryType == 2)
			found |= 2;
	}
	CHECK(IoErr() == ERROR_NO_MORE_ENTRIES, "ExNext end");
	CHECK(dirs == 5 && files >= 4 && found == 3, "root entries");

	CHECK((lk2 = Lock("sys:DEVS/data/BIG", SHARED_LOCK)) != NULL,
	    "Lock ignoring case");
	CHECK(Examine(lk2, &fib) == DOSTRUE && fib.fib_DirEntryType < 0 &&
	    fib.fib_Size == BIGSIZE && same(fib.fib_FileName, "big") &&
	    fib.fib_NumBlocks == (BIGSIZE + 511) / 512, "Examine a file");
	UnLock(lk2);
	lk2 = Lock("SYS:Devs/Data", SHARED_LOCK);
	old = CurrentDir(lk2);
	CHECK((fh = Lock("big", SHARED_LOCK)) != NULL, "relative Lock");
	UnLock(fh);
	CHECK((fh = Lock("/", SHARED_LOCK)) != NULL &&
	    Examine(fh, &fib) == DOSTRUE && same(fib.fib_FileName, "Devs"),
	    "parent by /");
	UnLock(fh);
	CHECK((fh = Lock("//S", SHARED_LOCK)) != NULL &&
	    Examine(fh, &fib) == DOSTRUE && same(fib.fib_FileName, "S"),
	    "//S");
	UnLock(fh);
	CHECK((fh = Lock(":C", SHARED_LOCK)) != NULL &&
	    Examine(fh, &fib) == DOSTRUE && same(fib.fib_FileName, "C"),
	    "root by :");
	UnLock(fh);
	CHECK((fh = ParentDir(lk2)) != NULL && Examine(fh, &fib) == DOSTRUE &&
	    same(fib.fib_FileName, "Devs"), "ParentDir");
	UnLock(fh);
	CHECK(ParentDir(lk) == NULL && IoErr() == 0, "ParentDir of the root");
	CurrentDir(old);
	UnLock(lk2);
	CHECK((fh = Lock("C:", SHARED_LOCK)) != NULL &&
	    Examine(fh, &fib) == DOSTRUE && same(fib.fib_FileName, "C"),
	    "the C: assign");
	UnLock(fh);
	UnLock(lk);

	CHECK((fh = Open("SYS:Devs/Data/big", MODE_OLDFILE)) != NULL, "Open");
	CHECK(IsInteractive(fh) == DOSFALSE, "a file is not interactive");
	CHECK(readat(fh, 0L, 64), "direct blocks");
	CHECK(Seek(fh, 5100L, OFFSET_BEGINNING) == 64, "Seek's old position");
	CHECK(readat(fh, 5100L, 40), "into the indirect block");
	CHECK(readat(fh, 70600L, 64), "into the double-indirect block");
	CHECK(Seek(fh, -10L, OFFSET_END) == 70664L, "Seek from the end");
	CHECK(Read(fh, (APTR)&fib, 64L) == 10,
	    "short read at the end");
	CHECK(Read(fh, (APTR)&fib, 64L) == 0, "Read at the end");
	CHECK(Seek(fh, 1L, OFFSET_END) == -1 && IoErr() == ERROR_SEEK_ERROR,
	    "Seek past the end");
	CHECK(Seek(fh, -2L, OFFSET_CURRENT) == BIGSIZE, "Seek back");
	CHECK(ExamineFH(fh, &fib) == DOSTRUE && same(fib.fib_FileName, "big"),
	    "ExamineFH");
	CHECK((big = (UBYTE *)AllocMem((ULONG)BIGSIZE, MEMF_PUBLIC)) != NULL,
	    "memory");
	Seek(fh, 0L, OFFSET_BEGINNING);
	n = Read(fh, big, BIGSIZE);
	/* The block is over 64 KB: its second segment is stepped to. */
	for (i = 0; i < 65536L && big[(UWORD)i] == pat(i); i++)
		;
	for (p = (UBYTE *)PtrStep(big, 65536L); i < n && p[(UWORD)i] == pat(i);
	    i++)
		;
	FreeMem(big, (ULONG)BIGSIZE);
	CHECK(n == BIGSIZE && i == n, "whole-file read");
	Close(fh);

	CHECK(Open("SYS:none", MODE_OLDFILE) == NULL &&
	    IoErr() == ERROR_OBJECT_NOT_FOUND, "missing file");
	CHECK(Lock("SYS:none/x", SHARED_LOCK) == NULL &&
	    IoErr() == ERROR_DIR_NOT_FOUND, "missing directory");
	CHECK(Open("SYS:C", MODE_OLDFILE) == NULL &&
	    IoErr() == ERROR_OBJECT_WRONG_TYPE, "Open a directory");
	CHECK(DeleteFile("SYS:none") == DOSFALSE &&
	    IoErr() == ERROR_OBJECT_NOT_FOUND, "DeleteFile of nothing");

	CHECK(LoadSeg("SYS:Devs/Data/big") == NULL &&
	    IoErr() == ERROR_BAD_HUNK, "LoadSeg of data");
	CHECK((seg = LoadSeg("L:seghunk")) != NULL, "LoadSeg");
	UnLoadSeg(seg);
	return (0);
}

static LONG
pmain(args, len)
char *args;
LONG len;
{
	return ((LONG)ftest());
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
	struct TagItem t[5];
	struct dsync s;

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 36L))
	    == NULL) {
		KPutStr("FAIL v7fs: no dos.library\n");
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
	t[3].ti_Tag = NP_StackSize;
	t[3].ti_Data = 1536;
	t[4].ti_Tag = TAG_DONE;
	if (CreateNewProc(t) == NULL) {
		KPutStr("FAIL v7fs: CreateNewProc\n");
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	if (s.s_RC != 0) {
		xKPrintf("FAIL v7fs: %s\n", why);
		return (1);
	}
	KPutStr("PASS v7fs\n");
	return (0);
}
