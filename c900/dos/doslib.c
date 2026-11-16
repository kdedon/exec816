/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * dos.library: the library base, its romtag and initialisation.  A boot
 * module (docs/platform.md, Boot modules): InitResident makes the base
 * from the generated vector table (DosFuncs, tools/genlvo.py from
 * fd/dos_lib.fd) and adds it.  Init builds the RootNode and DosInfo and
 * the boot assigns SYS:, C:, S:, L:, LIBS:, DEVS: and FONTS:, as late-binding
 * assigns: the boot volume's handler is a later module, and no process
 * exists yet to lock it.  Init also installs ramlib (ramlib.c), which
 * loads libraries and devices from LIBS: and DEVS:, and the Task crash
 * requester (doscrash.c).
 */
#include "dosint.h"
#include <devices/timer.h>

struct DosLibrary *DOSBase;

static char dosname[] = DOSNAME;
static char dosid[] = "dos 39.1 (Exec900)";

static struct RootNode dosroot;
static struct DosInfo dosinfo;
static struct timerequest dostime;	/* binding for GetSysTime */

extern APTR DosFuncs[];
LONG xAssignLate();

struct Library *
dlinit(lib, seg, sysbase)
struct Library *lib;
APTR seg, sysbase;
{
	static char *assigns[] = {
		"SYS", "DH0:", "C", "SYS:C", "S", "SYS:S", "L", "SYS:L",
		"LIBS", "SYS:Libs", "DEVS", "SYS:Devs", "FONTS", "SYS:Fonts", NULL
	};
	register char **a;

	lib->lib_Node.ln_Name = dosname;
	lib->lib_IdString = (APTR)dosid;
	lib->lib_Version = 39;
	lib->lib_Revision = 1;
	DOSBase = (struct DosLibrary *)lib;
	DOSBase->dl_Root = &dosroot;
	dosroot.rn_Info = (BPTR)&dosinfo;
	NewList((struct List *)&dosroot.rn_CliList);
	InitSemaphore(&dosinfo.di_DevLock);
	InitSemaphore(&dosinfo.di_EntryLock);
	InitSemaphore(&dosinfo.di_DeleteLock);
	dvinit();
	if (OpenDevice(TIMERNAME, (ULONG)UNIT_VBLANK, &dostime, 0L) == 0) {
		DOSBase->dl_TimeReq = (APTR)&dostime;
		TimerBase = dostime.tr_node.io_Device;
	}
	rlsetup();
	dcsetup();
	for (a = assigns; *a != NULL; a += 2)
		xAssignLate(DOSBase, a[0], a[1]);
	return (lib);
}

struct Library *
dlopen(lib, ver)
struct Library *lib;
ULONG ver;
{
	lib->lib_OpenCnt++;
	lib->lib_Flags &= ~LIBF_DELEXP;
	return (lib);
}

/* dos.library stays loaded: Expunge never frees it. */
APTR
dlclose(lib)
struct Library *lib;
{
	lib->lib_OpenCnt--;
	return (NULL);
}

APTR
dlexpunge(lib)
struct Library *lib;
{
	return (NULL);
}

LONG
dlnull()
{
	return (0);
}

/* A vector fd/dos_lib.fd leaves unimplemented. */
LONG
dlunimpl()
{
	return (dfail(ERROR_NOT_IMPLEMENTED));
}

static struct {
	ULONG it_Size;
	APTR it_Funcs, it_Struct, it_Init;
} dlinittab = {
	sizeof(struct DosLibrary), (APTR)DosFuncs, NULL, (APTR)dlinit
};

struct Resident dltag = {
	RTC_MATCHWORD, &dltag, (APTR)(&dltag + 1), RTF_AUTOINIT, 39,
	NT_LIBRARY, 0, dosname, dosid, (APTR)&dlinittab
};
