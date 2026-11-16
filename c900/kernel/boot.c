/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Cold start.  crt.s calls cmain on the root stack with VI off: bring up
 * the console, memory, the root and idle Tasks, interrupt dispatch,
 * exec.library and the resident modules, start the tick, load the boot
 * modules and the root program, then run it as the root Task.
 */
#include "kernel.h"

/* Enter the root program.  A function of its own, so that a debugger
 * breakpoint here finds the program's entry (tools/perf.py). */
int
krun(entry)
int (*entry)();
{
	return ((*entry)());
}

void
cmain()
{
	register UWORD v;
	APTR seg;

	NewList(&TaskReady);
	TaskReady.lh_Type = NT_TASK;
	NewList(&TaskWait);
	TaskWait.lh_Type = NT_TASK;
	con_init();
	v = xExecVersion();
	xKPrintf("Exec900 %u.%u\n", v >> 8, v & 0xff);
	NewList(&PortList);
	PortList.lh_Type = NT_MSGPORT;
	NewList(&SemaphoreList);
	NewList(&ExecLib.MemList);
	ExecLib.MemList.lh_Type = NT_MEMORY;
	NewList(&LibList);
	LibList.lh_Type = NT_LIBRARY;
	NewList(&DeviceList);
	DeviceList.lh_Type = NT_DEVICE;
	NewList(&ResourceList);
	ResourceList.lh_Type = NT_RESOURCE;
	TaskTrapCode = (APTR)ktrapdef;
	mach_mem();
	kinitroot();
	kepuinit();
	kinitidle();
	kintinit();
	kexecinit();
	xInitCode((ULONG)RTF_COLDSTART, 0L);
	tick_init();
	if ((seg = kbootmods()) != NULL)
		xKPrintf("exec900: root exit %d\n",
		    krun((int (*)())((ULONG *)seg + 1)));
	kintreport();
	kcondrain();
	emustop();
	for (;;)
		haltcpu();
}
