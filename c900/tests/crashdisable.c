/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * A Task's trap under Disable is not held: dos.library's default
 * tc_TrapCode gives it the kernel's dead end.
 */
/* EXPECT: exec900: alert 80000021 task crasher */
#include "test.h"
#include "crashtest.h"

static void
crasher()
{
	Disable();
	(*(void (*)())(APTR)sccode)();
	ran++;
}

int
main()
{
	if (FindTask(NULL)->tc_TrapCode == SysBase->TaskTrapCode) {
		KPutStr("FAIL crashdisable: TaskTrapCode not dos.library's\n");
		return (1);
	}
	CreateTask("crasher", 1L, crasher, CSTACK);
	Sleep(100L);
	KPutStr("FAIL crashdisable: the trap did not stop the system\n");
	return (1);
}
