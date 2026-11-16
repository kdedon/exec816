/* SPDX-License-Identifier: MIT */
/*
 * A fake intuition.library for tests/alertint.c and tests/crashint.c
 * (LIBS:): DisplayAlert and TimedDisplayAlert at intuition's vectors
 * record each call in the base and return fi_Answer without showing
 * anything.  With fi_Hold set, the next DisplayAlert signals that Task
 * and stays on the "display" until its caller gets SIGBREAKF_CTRL_C.
 * The system requester calls record theirs too: BuildEasyRequestArgs
 * returns a window, NULL, or a window only once its caller gets
 * SIGBREAKF_CTRL_C (fi_BuildMode); SysReqHandler answers fi_Gadget;
 * EasyRequestArgs answers 0.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <lvo/exec_lib.h>
#include "fakeint.h"

static char finame[] = "intuition.library";
static char fiid[] = "intuition 40.0 (alert test fake)";

extern APTR FakeIFuncs[];

struct Library *
fiopen(lib, ver)
struct Library *lib;
ULONG ver;
{
	lib->lib_OpenCnt++;
	lib->lib_Flags &= ~LIBF_DELEXP;
	return (lib);
}

APTR
fiexpunge(lib)
register struct Library *lib;
{
	APTR seg;

	if (lib->lib_OpenCnt != 0) {
		lib->lib_Flags |= LIBF_DELEXP;
		return (NULL);
	}
	seg = ((struct FakeIBase *)lib)->fi_Seg;
	Remove(&lib->lib_Node);
	FreeMem((char *)lib - lib->lib_NegSize,
	    (ULONG)(lib->lib_NegSize + lib->lib_PosSize));
	return (seg);
}

APTR
ficlose(lib)
struct Library *lib;
{
	if (--lib->lib_OpenCnt == 0 && (lib->lib_Flags & LIBF_DELEXP))
		return (fiexpunge(lib));
	return (NULL);
}

APTR
finull()
{
	return (NULL);
}

APTR
fiunimpl()
{
	return (NULL);
}

/* Record a call: the text's lines (x, y, string, continuation) up to
 * the last continuation byte. */
static BOOL
firecord(fi, num, s, h)
register struct FakeIBase *fi;
ULONG num;
register UBYTE *s;
UWORD h;
{
	register UWORD n;
	register struct Task *me;

	if (fi->fi_Refuse)
		KPutStr("FAIL alertint: an alert reached intuition\n");
	me = FindTask(NULL);
	fi->fi_Num = num;
	fi->fi_Height = h;
	fi->fi_Task = me;
	fi->fi_TD = me->tc_TDNestCnt;
	fi->fi_ID = me->tc_IDNestCnt;
	fi->fi_Opens = fi->fi_Lib.lib_OpenCnt;
	n = 0;
	for (;;) {
		n += 3;
		while (n < FITEXTMAX && s[n] != 0)
			n++;
		n += 2;
		if (n > FITEXTMAX || s[n - 1] == 0)
			break;
	}
	fi->fi_Len = n > FITEXTMAX ? FITEXTMAX : n;
	CopyMem(s, fi->fi_Text, (ULONG)fi->fi_Len);
	return (fi->fi_Answer);
}

BOOL
fidisplay(fi, num, s, h)
struct FakeIBase *fi;
ULONG num;
UBYTE *s;
UWORD h;
{
	register struct Task *t;
	BOOL r;

	if (fi->fi_Active++ != 0)
		fi->fi_Overlap = TRUE;
	if (fi->fi_Calls < FITASKS)
		fi->fi_Tasks[fi->fi_Calls] = FindTask(NULL);
	fi->fi_Calls++;
	r = firecord(fi, num, s, h);
	if ((t = fi->fi_Hold) != NULL) {
		fi->fi_Hold = NULL;
		Signal(t, SIGBREAKF_CTRL_F);
		Wait(SIGBREAKF_CTRL_C);
	}
	fi->fi_Active--;
	return (r);
}

BOOL
fitimed(fi, num, s, h, t)
struct FakeIBase *fi;
ULONG num;
UBYTE *s;
UWORD h;
ULONG t;
{
	fi->fi_Timed++;
	return (firecord(fi, num, s, h));
}

/* Copy string s, cut to FISTRMAX - 1 bytes, to d. */
static void
fistr(d, s)
register char *d, *s;
{
	register int n;

	for (n = 0; s != NULL && n < FISTRMAX - 1 && s[n] != 0; n++)
		d[n] = s[n];
	d[n] = 0;
}

LONG
fieasy(fi, w, es, idcmp, args)
struct FakeIBase *fi;
APTR w;
struct FIEasy *es;
ULONG *idcmp;
APTR args;
{
	fi->fi_Easys++;
	return (0L);
}

APTR
fibuild(fi, w, es, idcmp, args)
register struct FakeIBase *fi;
APTR w;
register struct FIEasy *es;
ULONG idcmp;
ULONG *args;
{
	fi->fi_Builds++;
	fi->fi_BuildTask = FindTask(NULL);
	fi->fi_BuildStack = fi->fi_BuildTask->tc_Node.ln_Type == NT_PROCESS ?
	    ((struct Process *)fi->fi_BuildTask)->pr_StackSize : 0L;
	fi->fi_BuildWin = w;
	fi->fi_BuildIDCMP = idcmp;
	fi->fi_BuildArg = args != NULL ? args[0] : 0L;
	fi->fi_ESSize = es->es_StructSize;
	fi->fi_ESFlags = es->es_Flags;
	fistr(fi->fi_Title, es->es_Title);
	fistr(fi->fi_Format, es->es_TextFormat);
	fistr(fi->fi_Gadgets, es->es_GadgetFormat);
	if (fi->fi_BuildMode == FIB_FAIL)
		return (NULL);
	if (fi->fi_BuildMode == FIB_HANG) {
		fi->fi_Hung = fi->fi_BuildTask;
		Wait(SIGBREAKF_CTRL_C);
		fi->fi_Hung = NULL;
	}
	return ((APTR)&fi->fi_Win);
}

LONG
fisysreq(fi, w, idcmp, wait)
register struct FakeIBase *fi;
APTR w;
ULONG *idcmp;
BOOL wait;
{
	fi->fi_Handles++;
	if (w != (APTR)&fi->fi_Win)
		fi->fi_WrongWin = TRUE;
	if (!wait)
		fi->fi_NoWait = TRUE;
	if (fi->fi_Pending != 0) {
		fi->fi_Pending--;
		return (-2L);
	}
	return (fi->fi_Gadget);
}

void
fifreereq(fi, w)
struct FakeIBase *fi;
APTR w;
{
	fi->fi_Frees++;
	if (w != (APTR)&fi->fi_Win)
		fi->fi_WrongWin = TRUE;
}

static struct Library *
fiinit(lib, seg, sysbase)
struct Library *lib;
APTR seg, sysbase;
{
	lib->lib_Node.ln_Name = finame;
	lib->lib_IdString = (APTR)fiid;
	lib->lib_Version = 40;
	((struct FakeIBase *)lib)->fi_Seg = seg;
	((struct FakeIBase *)lib)->fi_Answer = TRUE;
	((struct FakeIBase *)lib)->fi_Gadget = 1;
	return (lib);
}

static struct {
	ULONG it_Size;
	APTR it_Funcs, it_Struct, it_Init;
} fiinittab = {
	sizeof(struct FakeIBase), (APTR)FakeIFuncs, NULL, (APTR)fiinit
};

struct Resident fitag = {
	RTC_MATCHWORD, &fitag, (APTR)(&fitag + 1), RTF_AUTOINIT, 40,
	NT_LIBRARY, 0, finame, fiid, (APTR)&fiinittab
};
