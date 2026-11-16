/* SPDX-License-Identifier: MIT */
/*
 * exec.library's base.  SysBase is the first long of the kernel's data
 * segment (docs/platform.md).  The fields after LibNode are the Amiga
 * ExecBase fields programs read, in Amiga order but not at Amiga offsets;
 * the rest of Amiga's scheduler state is kernel variables.  The library,
 * device and resource lists are read under Forbid, as on the Amiga.  ex_TickCount
 * is an Exec900 extension.  AttnFlags keeps Amiga's bits; only AFF_68881
 * is ever set, and it means a Z8070 EPU is fitted (docs/platform.md).
 */
#ifndef EXEC_EXECBASE_H
#define EXEC_EXECBASE_H

#include <exec/lists.h>
#include <exec/interrupts.h>
#include <exec/libraries.h>
#include <exec/tasks.h>

struct ExecBase {
	struct Library LibNode;
	UWORD AttnFlags;		/* AFF_ bits, set at boot */
	APTR TaskTrapCode;		/* AddTask's default tc_TrapCode */
	struct List MemList;		/* MemHeaders, by priority */
	struct List ResourceList;	/* AddResource */
	struct List DeviceList;		/* AddDevice */
	struct List LibList;		/* AddLibrary */
	struct List PortList;		/* public message ports */
	struct List TaskReady;		/* ready Tasks, by priority; read */
	struct List TaskWait;		/* waiting Tasks; both under Disable */
	ULONG LastAlert[4];		/* last Alert: number, Task */
	ULONG ex_TickCount;		/* 100 Hz ticks since the tick began */
};

/* AttnFlags.  The CPU bits are never set on the Z8001. */
#define AFB_68010	0
#define AFB_68020	1
#define AFB_68030	2
#define AFB_68040	3
#define AFB_68881	4		/* a floating-point unit: the Z8070 */
#define AFB_68882	5
#define AFB_FPU40	6
#define AFB_68060	7
#define AFF_68010	(1 << AFB_68010)
#define AFF_68020	(1 << AFB_68020)
#define AFF_68030	(1 << AFB_68030)
#define AFF_68040	(1 << AFB_68040)
#define AFF_68881	(1 << AFB_68881)
#define AFF_68882	(1 << AFB_68882)
#define AFF_FPU40	(1 << AFB_FPU40)
#define AFF_68060	(1 << AFB_68060)

extern struct ExecBase *SysBase;

#endif
