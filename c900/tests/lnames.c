/* SPDX-License-Identifier: MIT */
/*
 * Long names: mkdisk.py --lnames puts SYS: on DH1:, a long-name V7
 * volume in slot 13, beside the standard boot volume DH0:.  Boot 1
 * checks both volumes' Info, LoadSeg of a long-named command, ExNext and
 * an .info file read and written; creates, renames and deletes files and
 * directories with 15- to 30-byte names (and enough of them to grow a
 * directory past a block); refuses names over 30 bytes; and shows DH0:
 * still keeping 14 bytes of a name.  Then it writes DH1:Crash-directory
 * with one file flushed and the rest left in the cache, and stops the
 * machine: a reset.  The runner checks both filesystems after each boot.
 * Boot 2 finds the flushed file whole and the rest consistent, and
 * removes everything.
 */
/* BOOTS: 2 */
/* FSCK: 136 1160 */
#define TNAME	"lnames"
#include "dostest.h"

/* 31 bytes: its prefixes of 15 to 30 bytes are the names. */
static char lname[] = "Commodities-Exchange-Prefs.info";
static char path[80];
static struct FileInfoBlock fib;
static struct InfoData id;
static UBYTE buf[600];

#define CRASH	"DH1:Crash-directory-long-name"

/* dir/ and the first n bytes of lname, in path. */
static char *
pname(dir, n)
register char *dir;
int n;
{
	register char *p;
	register int i;

	for (p = path; *dir != '\0'; )
		*p++ = *dir++;
	*p++ = '/';
	for (i = 0; i < n; i++)
		*p++ = lname[i];
	*p = '\0';
	return (path);
}

/* Write n bytes of the pattern seed to a new file name. */
static int
wfile(name, n, seed)
char *name;
int n, seed;
{
	BPTR fh;
	register int i;
	LONG r;

	if ((fh = Open(name, MODE_NEWFILE)) == NULL)
		return (0);
	for (i = 0; i < n; i++)
		buf[i] = (UBYTE)(i * seed + seed);
	r = Write(fh, buf, (LONG)n);
	Close(fh);
	return (r == n);
}

/* The size of file name, -1 when missing, -2 when not the pattern. */
static LONG
rfile(name, seed)
char *name;
int seed;
{
	BPTR fh;
	register int i;
	LONG n;

	if ((fh = Open(name, MODE_OLDFILE)) == NULL)
		return (-1);
	n = Read(fh, buf, (LONG)sizeof(buf));
	Close(fh);
	for (i = 0; i < (int)n; i++)
		if (buf[i] != (UBYTE)(i * seed + seed))
			return (-2);
	return (n);
}

/* The number of entries in directory name, and whether one is called
 * want. */
static int
count(name, want, found)
char *name, *want;
int *found;
{
	BPTR lk;
	int n;

	*found = 0;
	if ((lk = Lock(name, SHARED_LOCK)) == NULL)
		return (-1);
	n = 0;
	if (Examine(lk, &fib) == DOSTRUE)
		while (ExNext(lk, &fib) == DOSTRUE) {
			n++;
			if (want != NULL && same(fib.fib_FileName, want))
				*found = 1;
		}
	UnLock(lk);
	return (n);
}

static int
volumes()
{
	BPTR lk, seg;

	CHECK((lk = Lock("SYS:", SHARED_LOCK)) != NULL, "Lock SYS:");
	CHECK(Examine(lk, &fib) == DOSTRUE && same(fib.fib_FileName, "System"),
	    "SYS: is System");
	CHECK(Info(lk, &id) == DOSTRUE && id.id_DiskType == ID_V7L_DISK &&
	    id.id_NumBlocks == 1024, "Info SYS:");
	UnLock(lk);
	CHECK((lk = Lock("DH1:", SHARED_LOCK)) != NULL, "Lock DH1:");
	CHECK(Examine(lk, &fib) == DOSTRUE && same(fib.fib_FileName, "System"),
	    "DH1: is System");
	UnLock(lk);
	CHECK((lk = Lock("DH0:", SHARED_LOCK)) != NULL, "Lock DH0:");
	CHECK(Examine(lk, &fib) == DOSTRUE && same(fib.fib_FileName, "Boot"),
	    "DH0: is Boot");
	CHECK(Info(lk, &id) == DOSTRUE && id.id_DiskType == ID_V7_DISK,
	    "Info DH0:");
	UnLock(lk);
	CHECK((seg = LoadSeg("C:Seghunk-with-a-long-name.30")) != NULL,
	    "LoadSeg of a long-named command");
	UnLoadSeg(seg);
	return (0);
}

/* Calculator.info, read, written as Commodities.info, read back. */
static int
icons()
{
	BPTR fh;
	LONG n, m;
	int found;

	CHECK(count("SYS:Prefs", "Calculator.info", &found) == 1 && found,
	    "ExNext finds Calculator.info");
	CHECK((fh = Open("SYS:Prefs/calculator.INFO", MODE_OLDFILE)) != NULL,
	    "Open Calculator.info ignoring case");
	n = Read(fh, buf, (LONG)sizeof(buf));
	Close(fh);
	CHECK(n == 300, "read Calculator.info");
	CHECK((fh = Open("SYS:Prefs/Commodities.info", MODE_NEWFILE)) != NULL &&
	    Write(fh, buf, n) == n, "write Commodities.info");
	Close(fh);
	CHECK((fh = Open("SYS:Prefs/Commodities.info", MODE_OLDFILE)) != NULL,
	    "Open Commodities.info");
	m = Read(fh, buf + 300, 300L);
	Close(fh);
	CHECK(m == n, "Commodities.info's size");
	for (m = 0; m < n; m++)
		CHECK(buf[m] == buf[300 + m], "Commodities.info's bytes");
	CHECK(count("SYS:Prefs", "Commodities.info", &found) == 2 && found,
	    "ExNext finds Commodities.info");
	CHECK(DeleteFile("SYS:Prefs/Commodities.info") == DOSTRUE &&
	    count("SYS:Prefs", (char *)NULL, &found) == 1, "delete the copy");
	return (0);
}

static int
names()
{
	BPTR lk;
	register int n;
	int found;
	char p2[80];
	register char *a, *b;

	CHECK((lk = CreateDir("SYS:T-a-directory-of-25-bytes")) != NULL,
	    "CreateDir of a 25-byte name");
	UnLock(lk);
	for (n = 15; n <= 30; n++)
		CHECK(wfile(pname("SYS:T-a-directory-of-25-bytes", n), 10 + n, n),
		    "create 15 to 30 bytes");
	CHECK(count("SYS:T-a-directory-of-25-bytes", lname, &found) == 16 &&
	    !found, "16 entries");
	CHECK(count("SYS:T-a-directory-of-25-bytes", pname("x", 30) + 2,
	    &found) == 16 && found, "ExNext gives a 30-byte name whole");
	for (n = 15; n <= 30; n++)
		CHECK(rfile(pname("SYS:T-a-directory-of-25-bytes", n), n) ==
		    10 + n, "read 15 to 30 bytes");
	CHECK((lk = Lock(pname("SYS:T-a-directory-of-25-bytes", 30),
	    SHARED_LOCK)) != NULL && Examine(lk, &fib) == DOSTRUE &&
	    same(fib.fib_FileName, pname("x", 30) + 2), "Examine 30 bytes");
	UnLock(lk);

	/* Over 30 bytes. */
	CHECK(Open(pname("SYS:T-a-directory-of-25-bytes", 31), MODE_NEWFILE) ==
	    NULL && IoErr() == ERROR_INVALID_COMPONENT_NAME, "create 31 bytes");
	CHECK(CreateDir(pname("SYS:T-a-directory-of-25-bytes", 31)) == NULL &&
	    IoErr() == ERROR_INVALID_COMPONENT_NAME, "CreateDir 31 bytes");
	CHECK(Lock(pname("SYS:T-a-directory-of-25-bytes", 31), SHARED_LOCK) ==
	    NULL && IoErr() == ERROR_OBJECT_NOT_FOUND, "Lock 31 bytes");
	for (a = pname("SYS:T-a-directory-of-25-bytes", 15), b = p2;
	    (*b = *a) != '\0'; a++, b++)
		;
	CHECK(Rename(p2, pname("SYS:T-a-directory-of-25-bytes", 31)) ==
	    DOSFALSE && IoErr() == ERROR_INVALID_COMPONENT_NAME,
	    "Rename to 31 bytes");

	/* Rename in the directory, respelled, to another and back. */
	CHECK(Rename(p2, "SYS:T-a-directory-of-25-bytes/Renamed-in-the-dir") ==
	    DOSTRUE && count("SYS:T-a-directory-of-25-bytes",
	    "Renamed-in-the-dir", &found) == 16 && found, "Rename");
	CHECK(Rename("SYS:T-a-directory-of-25-bytes/Renamed-in-the-dir",
	    "SYS:T-a-directory-of-25-bytes/RENAMED-IN-THE-DIR") == DOSTRUE &&
	    count("SYS:T-a-directory-of-25-bytes", "RENAMED-IN-THE-DIR",
	    &found) == 16 && found, "Rename's spelling");
	CHECK(Rename("SYS:T-a-directory-of-25-bytes/Renamed-in-the-dir",
	    "SYS:Prefs/Moved-with-a-name-of-30-bytes!") == DOSTRUE &&
	    rfile("SYS:Prefs/moved-with-a-name-of-30-bytes!", 15) == 25 &&
	    count("SYS:T-a-directory-of-25-bytes", (char *)NULL, &found) == 15,
	    "Rename to another directory");
	CHECK(Rename("SYS:T-a-directory-of-25-bytes",
	    "SYS:Prefs/T-a-directory-of-25-bytes") == DOSTRUE &&
	    rfile(pname("SYS:Prefs/T-a-directory-of-25-bytes", 30), 30) == 40,
	    "Rename a directory");
	CHECK(Rename("SYS:Prefs/T-a-directory-of-25-bytes",
	    "SYS:T-a-directory-of-25-bytes") == DOSTRUE, "Rename it back");
	CHECK(DeleteFile("SYS:Prefs/Moved-with-a-name-of-30-bytes!") == DOSTRUE,
	    "delete a 30-byte name");

	/* Delete them all, then the directory. */
	CHECK(DeleteFile("SYS:T-a-directory-of-25-bytes") == DOSFALSE &&
	    IoErr() == ERROR_DIRECTORY_NOT_EMPTY, "delete a full directory");
	for (n = 16; n <= 30; n++)
		CHECK(DeleteFile(pname("SYS:T-a-directory-of-25-bytes", n)) ==
		    DOSTRUE, "delete 16 to 30 bytes");
	CHECK(DeleteFile("SYS:T-a-directory-of-25-bytes") == DOSTRUE &&
	    Lock("SYS:T-a-directory-of-25-bytes", SHARED_LOCK) == NULL,
	    "delete the directory");

	/* The standard volume keeps 14 bytes. */
	CHECK(wfile("DH0:A-name-longer-than-14", 20, 3), "DH0: long create");
	CHECK(count("DH0:", "A-name-longer-", &found) > 0 && found,
	    "DH0: keeps 14 bytes");
	CHECK(rfile("DH0:A-name-longer-than-any", 3) == 20, "DH0: lookup");
	CHECK(DeleteFile("DH0:A-name-longer-") == DOSTRUE, "DH0: delete");
	return (0);
}

static int
boot1()
{
	BPTR lk;
	register int n;
	struct MsgPort *mp;

	if (volumes() || icons() || names())
		return (1);
	CHECK((lk = CreateDir(CRASH)) != NULL, "CreateDir crash");
	UnLock(lk);
	CHECK(wfile(pname(CRASH, 30), 500, 9), "kept");
	mp = DeviceProc("DH1:");
	CHECK(DoPkt(mp, ACTION_FLUSH, 0L, 0L, 0L, 0L, 0L) == DOSTRUE,
	    "ACTION_FLUSH");
	/* 19 more entries: the directory grows a block. */
	for (n = 15; n < 30; n++)
		CHECK(wfile(pname(CRASH, n), 100 + n, n), "unflushed files");
	for (n = 0; n < 4; n++) {
		lname[0] = 'W' + n;
		CHECK((lk = CreateDir(pname(CRASH, 20))) != NULL, "unflushed dir");
		UnLock(lk);
	}
	lname[0] = 'C';
	KPutStr("lnames: reset with changes in the cache\n");
	outw(0x0FFE, 0xC900);
	why = "the machine did not stop";
	return (1);
}

static int
boot2()
{
	register int n;
	LONG r;
	int found;
	BPTR lk;

	CHECK(rfile(pname(CRASH, 30), 9) == 500, "kept after the reset");
	for (n = 15; n < 30; n++) {
		r = rfile(pname(CRASH, n), n);
		CHECK(r == -1 || (r >= 0 && r <= 100 + n), "after the reset");
		DeleteFile(path);
	}
	for (n = 0; n < 4; n++) {
		lname[0] = 'W' + n;
		if ((lk = Lock(pname(CRASH, 20), SHARED_LOCK)) != NULL) {
			CHECK(Examine(lk, &fib) == DOSTRUE &&
			    fib.fib_DirEntryType == 2 && ExNext(lk, &fib) ==
			    DOSFALSE, "a directory after the reset");
			UnLock(lk);
			CHECK(DeleteFile(path) == DOSTRUE, "delete it");
		}
	}
	lname[0] = 'C';
	CHECK(count(CRASH, (char *)NULL, &found) == 1, "one left");
	CHECK(DeleteFile(pname(CRASH, 30)) == DOSTRUE &&
	    DeleteFile(CRASH) == DOSTRUE, "removing crash");
	CHECK(DoPkt(DeviceProc("DH1:"), ACTION_FLUSH, 0L, 0L, 0L, 0L, 0L) ==
	    DOSTRUE, "ACTION_FLUSH");
	return (0);
}

static int
dtest()
{
	BPTR lk;

	if ((lk = Lock(CRASH, SHARED_LOCK)) == NULL)
		return (boot1());
	UnLock(lk);
	return (boot2());
}
