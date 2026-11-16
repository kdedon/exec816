/* SPDX-License-Identifier: MIT */
/*
 * The CON: handler's cooked input from scripted serial input: Backspace,
 * Ctrl-X, Ctrl-\ ending input after a partial line and on an empty one,
 * Ctrl-C signalling the reading process, a line read in pieces, an
 * escape sequence (cursor right) dropped and CR LF counted once; then
 * "*", Write and a failed Seek.  The input is held until the mark is
 * printed.
 */
/* EMUFLAGS: --input-mark=CONHAND-GO --input=\ia\ib\ic\i\^H\id\i\r\ix\iy\i\^X\iz\iz\i\r\iq\i\^\\i\^\\i\^C\il\io\in\ig\i\r\i1\i2\i3\i4\i5\i6\i7\i8\i9\i0\i\r\i\^[\i[\iC\io\ik\i\r\il\if\i\r\i\n\ix\i\r */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

static char *why;

#define CHECK(c, w)	if (!(c)) { why = w; return (1); }

static int
same(a, b, n)
register char *a, *b;
register int n;
{
	while (n-- > 0)
		if (*a++ != *b++)
			return (0);
	return (*b == '\0');
}

static int
ctest()
{
	BPTR fh, out;
	char buf[16];
	LONG n;

	CHECK((fh = Open("CON:0/0/640/200/Test", MODE_OLDFILE)) != NULL,
	    "Open CON:");
	CHECK(IsInteractive(fh) == DOSTRUE, "not interactive");
	CHECK(Write(fh, "CONHAND-GO\n", 11L) == 11, "Write");
	CHECK(Read(fh, buf, 0L) == 0, "empty Read");
	n = Read(fh, buf, 16L);
	CHECK(same(buf, "abd\n", (int)n), "Backspace");
	n = Read(fh, buf, 16L);
	CHECK(same(buf, "zz\n", (int)n), "Ctrl-X");
	n = Read(fh, buf, 16L);
	CHECK(same(buf, "q", (int)n), "Ctrl-\\ after a partial line");
	CHECK(Read(fh, buf, 16L) == 0, "end of input after it");
	CHECK(Read(fh, buf, 16L) == 0, "Ctrl-\\ on an empty line");
	n = Read(fh, buf, 16L);
	CHECK(same(buf, "long\n", (int)n), "line after Ctrl-C");
	CHECK(CheckSignal(SIGBREAKF_CTRL_C) == SIGBREAKF_CTRL_C, "Ctrl-C");
	n = Read(fh, buf, 4L);
	CHECK(same(buf, "1234", (int)n), "first piece");
	n = Read(fh, buf, 4L);
	CHECK(same(buf, "5678", (int)n), "second piece");
	n = Read(fh, buf, 16L);
	CHECK(same(buf, "90\n", (int)n), "the rest of the line");
	n = Read(fh, buf, 16L);
	CHECK(same(buf, "ok\n", (int)n), "escape sequence");
	n = Read(fh, buf, 16L);
	CHECK(same(buf, "lf\n", (int)n), "CR LF");
	n = Read(fh, buf, 16L);
	CHECK(same(buf, "x\n", (int)n), "line after CR LF");
	CHECK(Seek(fh, 0L, OFFSET_BEGINNING) == -1, "Seek");
	CHECK((out = Open("*", MODE_NEWFILE)) != NULL, "Open *");
	CHECK(FPuts(out, "conhand: done\n") == 0, "FPuts to *");
	Close(out);
	Close(fh);
	return (0);
}

static LONG
pmain(args, len)
char *args;
LONG len;
{
	return ((LONG)ctest());
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
		KPutStr("FAIL conhand: no dos.library\n");
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
		KPutStr("FAIL conhand: CreateNewProc\n");
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	if (s.s_RC != 0) {
		xKPrintf("FAIL conhand: %s\n", why);
		return (1);
	}
	KPutStr("PASS conhand\n");
	return (0);
}
