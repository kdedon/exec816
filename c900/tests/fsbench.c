/* SPDX-License-Identifier: MIT */
/*
 * Sequential file speed on the boot volume: a 65,536-byte file written
 * in 1,000-byte pieces and closed, read back and checked in 1,000-byte
 * pieces, then deleted.  The check is sparse, so the time is the
 * filesystem's.  Each phase is bracketed by perf marks (I/O port
 * 0x0F00 + 2N, tools/perf.py) and its 100 Hz ticks are printed, so a
 * board run reports the times directly.
 */
#include "test.h"
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

#define SIZE	65536L
#define PIECE	1000

static char *why;
static struct InfoData id;
static UBYTE buf[PIECE];
static ULONG t0;

#define CHECK(c, w)	if (!(c)) { why = w; return (1); }

static void
mark(n, what)
int n;
char *what;
{
	ULONG t;

	outb(0x0F00 + 2 * n, 0);
	t = TickCount;
	if (what != NULL)
		xKPrintf("fsbench: %s %ld ticks\n", what, (LONG)(t - t0));
	t0 = t;
}

/* Every 16th byte of the file and the one after it hold its offset / 16,
 * so a misplaced block shows; the rest is left as it is, so that the
 * test spends little time of its own. */
static void
fill(pos, k)
LONG pos;
int k;
{
	register UWORD q;
	register int i;

	for (i = (int)(-pos & 15); i < k - 1; i += 16) {
		q = (UWORD)((pos + i) >> 4);
		buf[i] = (UBYTE)q;
		buf[i + 1] = (UBYTE)(q >> 8);
	}
}

static int
match(pos, k)
LONG pos;
int k;
{
	register UWORD q;
	register int i;

	for (i = (int)(-pos & 15); i < k - 1; i += 16) {
		q = (UWORD)((pos + i) >> 4);
		if (buf[i] != (UBYTE)q || buf[i + 1] != (UBYTE)(q >> 8))
			return (0);
	}
	return (1);
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
bench()
{
	BPTR fh;
	LONG before, pos, n;
	int k;

	DeleteFile("SYS:seqfile");
	before = used();
	KPutStr("fsbench: start\n");
	mark(1, (char *)NULL);
	CHECK((fh = Open("SYS:seqfile", MODE_NEWFILE)) != NULL, "Open");
	for (pos = 0; pos < SIZE; pos += k) {
		k = SIZE - pos < PIECE ? (int)(SIZE - pos) : PIECE;
		fill(pos, k);
		CHECK(Write(fh, buf, (LONG)k) == k, "Write");
	}
	Close(fh);
	mark(2, "write");
	CHECK((fh = Open("SYS:seqfile", MODE_OLDFILE)) != NULL, "reopen");
	for (pos = 0; (n = Read(fh, buf, (LONG)PIECE)) > 0; pos += n)
		CHECK(match(pos, (int)n), "data read back");
	Close(fh);
	CHECK(pos == SIZE, "size read back");
	mark(3, "read");
	CHECK(DeleteFile("SYS:seqfile") == DOSTRUE, "DeleteFile");
	mark(4, "delete");
	CHECK(used() == before, "blocks freed");
	return (0);
}

static LONG
pmain(args, len)
char *args;
LONG len;
{
	return ((LONG)bench());
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
		KPutStr("FAIL fsbench: no dos.library\n");
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
		KPutStr("FAIL fsbench: CreateNewProc\n");
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	if (s.s_RC != 0) {
		xKPrintf("FAIL fsbench: %s\n", why);
		return (1);
	}
	KPutStr("PASS fsbench\n");
	return (0);
}
