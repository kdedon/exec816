/* SPDX-License-Identifier: MIT */
/*
 * The boot volume across a reset with changes still in the handler's
 * cache.  Boot 1 writes DH0:crash/kept and flushes it with ACTION_FLUSH
 * while it stays open, then writes 20,000 bytes to crash/lost, creates
 * crash/new and the directory crash/sub without closing or flushing,
 * and stops the machine at once: a reset.  The runner checks the
 * filesystem (FSCK) after each boot.  Boot 2 finds kept whole and
 * whatever reached the disk of the rest consistent (every byte inside a
 * file's size is the data written there), then checks ACTION_INHIBIT and
 * ACTION_DIE, removes everything and flushes.  Names go through DH0:, so
 * no assign holds a lock and the handler may die.
 */
/* BOOTS: 2 */
/* FSCK: 136 */
#include "test.h"
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

#define KEPT	3000
#define LOST	20000L
#define NEW	100

static char *why;
static struct FileInfoBlock fib;
static UBYTE buf[1000];

#define CHECK(c, w)	if (!(c)) { why = w; return (1); }

static UBYTE
pat(pos, seed)
LONG pos;
int seed;
{
	return ((UBYTE)((UWORD)pos * seed + (UWORD)(pos >> 8)));
}

/* Write n pattern bytes at the handle's position (pos). */
static int
put(fh, pos, n, seed)
BPTR fh;
LONG pos, n;
int seed;
{
	register int i, k;

	for (; n > 0; n -= k, pos += k) {
		k = n > 1000 ? 1000 : (int)n;
		for (i = 0; i < k; i++)
			buf[i] = pat(pos + i, seed);
		if (Write(fh, buf, (LONG)k) != k)
			return (0);
	}
	return (1);
}

/* The size of name, or -1 when it is not there; with seed, its bytes
 * must be the pattern. */
static LONG
got(name, seed)
char *name;
int seed;
{
	BPTR fh;
	register int i;
	LONG pos, n;

	if ((fh = Open(name, MODE_OLDFILE)) == NULL)
		return (-1);
	for (pos = 0; (n = Read(fh, buf, 1000L)) > 0; pos += n)
		for (i = 0; i < (int)n; i++)
			if (buf[i] != pat(pos + i, seed)) {
				Close(fh);
				return (-2);
			}
	Close(fh);
	return (pos);
}

static int
boot1()
{
	BPTR fh, lk;
	struct MsgPort *mp;

	CHECK((lk = CreateDir("DH0:crash")) != NULL, "CreateDir crash");
	UnLock(lk);
	CHECK((fh = Open("DH0:crash/kept", MODE_NEWFILE)) != NULL, "kept");
	CHECK(put(fh, 0L, (LONG)KEPT, 3), "writing kept");
	mp = DeviceProc("DH0:");
	CHECK(DoPkt(mp, ACTION_FLUSH, 0L, 0L, 0L, 0L, 0L) == DOSTRUE,
	    "ACTION_FLUSH");
	CHECK((fh = Open("DH0:crash/lost", MODE_NEWFILE)) != NULL, "lost");
	CHECK(put(fh, 0L, LOST, 5), "writing lost");
	CHECK((fh = Open("DH0:crash/new", MODE_NEWFILE)) != NULL, "new");
	CHECK(put(fh, 0L, (LONG)NEW, 7), "writing new");
	CHECK((lk = CreateDir("DH0:crash/sub")) != NULL, "CreateDir sub");
	KPutStr("fscrash: reset with changes in the cache\n");
	outw(0x0FFE, 0xC900);
	why = "the machine did not stop";
	return (1);
}

static int
boot2()
{
	BPTR lk, lk2;
	struct MsgPort *mp;
	LONG n;

	CHECK(got("DH0:crash/kept", 3) == KEPT, "kept after the reset");
	n = got("DH0:crash/lost", 5);
	CHECK(n >= -1 && n <= LOST, "lost after the reset");
	xKPrintf("fscrash: lost has %ld bytes\n", n);
	n = got("DH0:crash/new", 7);
	CHECK(n >= -1 && n <= NEW, "new after the reset");
	xKPrintf("fscrash: new has %ld bytes\n", n);
	if ((lk = Lock("DH0:crash/sub", SHARED_LOCK)) != NULL) {
		CHECK(Examine(lk, &fib) == DOSTRUE && fib.fib_DirEntryType == 2 &&
		    ExNext(lk, &fib) == DOSFALSE, "sub after the reset");
		CHECK((lk2 = ParentDir(lk)) != NULL, "sub's parent");
		UnLock(lk2);
		UnLock(lk);
		KPutStr("fscrash: sub is there\n");
	}

	mp = DeviceProc("DH0:");
	CHECK(DoPkt(mp, ACTION_INHIBIT, DOSTRUE, 0L, 0L, 0L, 0L) == DOSTRUE,
	    "ACTION_INHIBIT");
	CHECK(Lock("DH0:crash", SHARED_LOCK) == NULL &&
	    IoErr() == ERROR_NOT_A_DOS_DISK, "inhibited");
	CHECK(DoPkt(mp, ACTION_INHIBIT, DOSFALSE, 0L, 0L, 0L, 0L) == DOSTRUE,
	    "ACTION_INHIBIT off");
	CHECK(got("DH0:crash/kept", 3) == KEPT, "kept after the inhibit");

	DeleteFile("DH0:crash/lost");
	DeleteFile("DH0:crash/new");
	DeleteFile("DH0:crash/sub");
	CHECK((lk = Lock("DH0:crash", SHARED_LOCK)) != NULL, "crash");
	CHECK(DoPkt(mp, ACTION_DIE, 0L, 0L, 0L, 0L, 0L) == DOSFALSE &&
	    IoErr() == ERROR_OBJECT_IN_USE, "ACTION_DIE with a lock");
	UnLock(lk);
	CHECK(DoPkt(mp, ACTION_DIE, 0L, 0L, 0L, 0L, 0L) == DOSTRUE,
	    "ACTION_DIE");
	CHECK(got("DH0:crash/kept", 3) == KEPT, "kept after ACTION_DIE");
	CHECK(DeleteFile("DH0:crash/kept") == DOSTRUE &&
	    DeleteFile("DH0:crash") == DOSTRUE &&
	    Lock("DH0:crash", SHARED_LOCK) == NULL, "removing crash");
	return (0);
}

static LONG
pmain(args, len)
char *args;
LONG len;
{
	BPTR lk;

	if ((lk = Lock("DH0:crash/kept", SHARED_LOCK)) == NULL)
		return ((LONG)boot1());
	UnLock(lk);
	return ((LONG)boot2());
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
		KPutStr("FAIL fscrash: no dos.library\n");
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
		KPutStr("FAIL fscrash: CreateNewProc\n");
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	if (s.s_RC != 0) {
		xKPrintf("FAIL fscrash: %s\n", why);
		return (1);
	}
	KPutStr("PASS fscrash\n");
	return (0);
}
