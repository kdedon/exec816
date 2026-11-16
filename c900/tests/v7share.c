/* SPDX-License-Identifier: MIT */
/*
 * One v7fs-handler for two volumes: on a --lnames disk DH0: and DH1:
 * run one handler seglist in two processes.  The test prints the free
 * heap and largest block before either handler starts, with DH1: (SYS:)
 * alone and with both, and allocates asl.library's three hunks at once.
 * The disk gives DH1: 8 buffers (mkdisk.py --sysbuffers=8): its
 * DosEnvec says so and its handler takes 8 blocks less than DH0:'s.
 * Then two processes write files to DH0: and DH1: at the same time and
 * each volume keeps its own state: DH0: cuts names to 14 bytes, DH1:
 * keeps 30, and every file reads back whole.  The runner checks both
 * filesystems afterwards.
 */
/* FSCK: 136 1160 */
#define TNAME	"v7share"
#include "dostest.h"
#include <dos/filehandler.h>

#define NFILE	4
#define FSIZE	3000
#define PIECE	100

static struct FileInfoBlock fib;
static char *wwhy[2];
static int wdone[2];
static struct Task *parent;

#define SYSBUFS	8

/* The free heap, printed. */
static LONG
heap(what)
char *what;
{
	LONG n;

	n = AvailMem(MEMF_ANY);
	xKPrintf("v7share: %s: %ld free, largest %ld\n", what, n,
	    (LONG)AvailMem(MEMF_ANY | MEMF_LARGEST));
	return (n);
}

/* The handler seglist, port and buffer count of device name. */
static void
devof(name, seg, task, nbuf)
char *name;
BPTR *seg;
struct MsgPort **task;
LONG *nbuf;
{
	register struct DosList *dl;
	struct FileSysStartupMsg *fm;

	*seg = NULL;
	*task = NULL;
	*nbuf = 0;
	dl = LockDosList(LDF_DEVICES | LDF_READ);
	if ((dl = FindDosEntry(dl, name, LDF_DEVICES)) != NULL) {
		*seg = dl->dol_misc.dol_handler.dol_SegList;
		*task = dl->dol_Task;
		fm = (struct FileSysStartupMsg *)
		    dl->dol_misc.dol_handler.dol_Startup;
		*nbuf = fm->fssm_Environ->de_NumBuffers;
	}
	UnLockDosList(LDF_DEVICES | LDF_READ);
}

/* The name of file n on volume v: its first 14 bytes differ. */
static char *
fname(buf, v, n)
register char *buf;
int v, n;
{
	register char *s;
	char *b0;

	b0 = buf;
	for (s = v ? "DH1:" : "DH0:"; *s != '\0'; )
		*buf++ = *s++;
	*buf++ = 'A' + n;
	for (s = "-shared-handler-file"; *s != '\0'; )
		*buf++ = *s++;
	*buf = '\0';
	return (b0);
}

static UBYTE
pat(v, n, i)
int v, n, i;
{
	return ((UBYTE)(i * (3 + v) + n * 7 + v * 101));
}

/* Writer v: its files written a piece at a time, then read back. */
static int
writer(v)
int v;
{
	char name[40];
	UBYTE b[PIECE];
	register int i, k;
	int n;
	BPTR fh[NFILE];

	for (n = 0; n < NFILE; n++)
		if ((fh[n] = Open(fname(name, v, n), MODE_NEWFILE)) == NULL) {
			wwhy[v] = "Open to write";
			return (1);
		}
	for (k = 0; k < FSIZE; k += PIECE)
		for (n = 0; n < NFILE; n++) {
			for (i = 0; i < PIECE; i++)
				b[i] = pat(v, n, k + i);
			if (Write(fh[n], b, (LONG)PIECE) != PIECE) {
				wwhy[v] = "Write";
				return (1);
			}
		}
	for (n = 0; n < NFILE; n++)
		Close(fh[n]);
	for (n = 0; n < NFILE; n++) {
		if ((fh[0] = Open(fname(name, v, n), MODE_OLDFILE)) == NULL) {
			wwhy[v] = "Open to read";
			return (1);
		}
		for (k = 0; k < FSIZE; k += PIECE) {
			if (Read(fh[0], b, (LONG)PIECE) != PIECE) {
				Close(fh[0]);
				wwhy[v] = "Read";
				return (1);
			}
			for (i = 0; i < PIECE; i++)
				if (b[i] != pat(v, n, k + i)) {
					Close(fh[0]);
					wwhy[v] = "the data read back";
					return (1);
				}
		}
		Close(fh[0]);
	}
	return (0);
}

static LONG
w0(args, len)
char *args;
LONG len;
{
	return ((LONG)writer(0));
}

static LONG
w1(args, len)
char *args;
LONG len;
{
	return ((LONG)writer(1));
}

static void
wexit(rc, v)
LONG rc, v;
{
	if (rc != 0 && wwhy[(int)v] == NULL)
		wwhy[(int)v] = "failed";
	Forbid();
	wdone[(int)v] = 1;
	Signal(parent, SIGBREAKF_CTRL_E);
	Permit();
}

static int
start(v)
int v;
{
	struct TagItem t[6];

	t[0].ti_Tag = NP_Entry;
	t[0].ti_Data = (ULONG)(v ? w1 : w0);
	t[1].ti_Tag = NP_ExitCode;
	t[1].ti_Data = (ULONG)wexit;
	t[2].ti_Tag = NP_ExitData;
	t[2].ti_Data = (ULONG)v;
	t[3].ti_Tag = NP_StackSize;
	t[3].ti_Data = 2000;
	t[4].ti_Tag = NP_Name;
	t[4].ti_Data = (ULONG)(v ? "v7share DH1" : "v7share DH0");
	t[5].ti_Tag = TAG_DONE;
	return (CreateNewProc(t) != NULL);
}

/* The directory's entries named like fname's, each of length len. */
static int
listed(v, len)
int v, len;
{
	BPTR lk;
	register int n, k;

	if ((lk = Lock(v ? "DH1:" : "DH0:", SHARED_LOCK)) == NULL)
		return (-1);
	n = 0;
	if (Examine(lk, &fib) == DOSTRUE)
		while (ExNext(lk, &fib) == DOSTRUE) {
			for (k = 0; fib.fib_FileName[k] != '\0'; k++)
				;
			if (fib.fib_FileName[1] == '-' && fib.fib_FileName[2] ==
			    's' && k == len)
				n++;
		}
	UnLock(lk);
	return (n);
}

static int
dtest()
{
	BPTR s0, s1, lk;
	struct MsgPort *p0, *p1;
	LONG m0, m1, m2, n0, n1;
	APTR h1, h2, h3;
	char name[40];
	register int v, n;

	m0 = heap("no handler");
	CHECK((lk = Lock("DH1:", SHARED_LOCK)) != NULL, "Lock DH1:");
	UnLock(lk);
	m1 = heap("DH1:");
	CHECK((lk = Lock("DH0:", SHARED_LOCK)) != NULL, "Lock DH0:");
	UnLock(lk);
	m2 = heap("DH0: and DH1:");
	xKPrintf("v7share: DH1: takes %ld, DH0: %ld\n", m0 - m1, m1 - m2);
	h1 = AllocMem(60662L, MEMF_ANY);
	h2 = AllocMem(52964L, MEMF_ANY);
	h3 = AllocMem(13174L, MEMF_ANY);
	xKPrintf("v7share: asl.library's hunks %s\n",
	    h1 != NULL && h2 != NULL && h3 != NULL ? "fit" : "do not fit");
	if (h1 != NULL)
		FreeMem(h1, 60662L);
	if (h2 != NULL)
		FreeMem(h2, 52964L);
	if (h3 != NULL)
		FreeMem(h3, 13174L);

	devof("DH0", &s0, &p0, &n0);
	devof("DH1", &s1, &p1, &n1);
	CHECK(s0 != NULL && s0 == s1, "one handler seglist for DH0: and DH1:");
	CHECK(p0 != NULL && p1 != NULL && p0 != p1, "a process for each");
	CHECK(n0 == 16 && n1 == SYSBUFS, "de_NumBuffers 16 and --sysbuffers");
	CHECK((m1 - m2) - (m0 - m1) >= (16 - SYSBUFS) * 512L &&
	    (m1 - m2) - (m0 - m1) < (16 - SYSBUFS) * 512L + 256,
	    "DH1:'s cache has SYSBUFS blocks");

	parent = FindTask(NULL);
	SetSignal(0L, SIGBREAKF_CTRL_E);
	CHECK(start(0) && start(1), "CreateNewProc");
	for (;;) {
		Forbid();
		v = wdone[0] && wdone[1];
		Permit();
		if (v)
			break;
		Wait(SIGBREAKF_CTRL_E);
	}
	CHECK(wwhy[0] == NULL, wwhy[0]);
	CHECK(wwhy[1] == NULL, wwhy[1]);
	CHECK(listed(0, 14) == NFILE, "DH0: keeps 14 bytes of a name");
	CHECK(listed(1, 21) == NFILE, "DH1: keeps the whole name");
	for (v = 0; v < 2; v++)
		for (n = 0; n < NFILE; n++)
			CHECK(DeleteFile(fname(name, v, n)) == DOSTRUE, "delete");
	CHECK(DoPkt(p0, ACTION_FLUSH, 0L, 0L, 0L, 0L, 0L) == DOSTRUE &&
	    DoPkt(p1, ACTION_FLUSH, 0L, 0L, 0L, 0L, 0L) == DOSTRUE, "flush");
	return (0);
}
