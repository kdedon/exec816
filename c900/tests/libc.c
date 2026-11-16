/* SPDX-License-Identifier: MIT */
/*
 * C programs on the C library, run by the shell: C:CTest's checks with
 * quoted arguments, its output (redirected to a file, flushed at exit
 * after the atexit functions) and its exit code; a script whose second
 * command finds the directory the first command's chdir left restored
 * and that stops at a C program's exit(10); C:CHello on the console.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

static char *why;
static char out[256];

#define CHECK(c, w)	if (!(c)) { why = w; return (1); }
#define WANT	"stderr ok\nctest ok\natexit 2\natexit 1\n"

static LONG
sys(cmd)
char *cmd;
{
	struct TagItem t[1];

	t[0].ti_Tag = TAG_DONE;
	return (SystemTagList(cmd, t));
}

static int
same(a, b)
register char *a, *b;
{
	while (*a != '\0' && *a == *b)
		a++, b++;
	return (*a == *b);
}

static int
ltest()
{
	struct TagItem t[2];
	BPTR fh, l;
	LONG n, rc;

	rc = sys("CTest >SYS:ctest.out <S:CScript run a \"two words\" \"q*\"uo**te\" \"\" end");
	CHECK((fh = Open("SYS:ctest.out", MODE_OLDFILE)) != NULL,
	    "no output file");
	n = Read(fh, out, (LONG)sizeof(out) - 1);
	Close(fh);
	out[n < 0 ? 0 : (int)n] = '\0';
	if (rc != 3 || !same(out, WANT)) {
		xKPrintf("CTest result %ld, output:\n%s", rc, out);
		why = "CTest";
		return (1);
	}
	CHECK(DeleteFile("SYS:ctest.out") == DOSTRUE, "DeleteFile");
	CHECK(sys("Execute S:CScript") == 10, "the script's result");
	l = Lock("SYS:notreached", SHARED_LOCK);
	CHECK(l == NULL, "the script went on after exit(10)");
	CHECK((fh = Open("CON:", MODE_NEWFILE)) != NULL, "Open CON:");
	t[0].ti_Tag = SYS_Output;
	t[0].ti_Data = (ULONG)fh;
	t[1].ti_Tag = TAG_DONE;
	rc = SystemTagList("CHello", t);
	Close(fh);
	CHECK(rc == 0, "CHello");
	return (0);
}

static LONG
pmain(args, len)
char *args;
LONG len;
{
	return ((LONG)ltest());
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
		KPutStr("FAIL libc: no dos.library\n");
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
		KPutStr("FAIL libc: CreateNewProc\n");
		return (1);
	}
	Wait(SIGBREAKF_CTRL_F);
	if (s.s_RC != 0) {
		xKPrintf("FAIL libc: %s\n", why);
		return (1);
	}
	KPutStr("PASS libc\n");
	return (0);
}
