/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * A Task's trap under Forbid is not held: dos.library's default
 * tc_TrapCode gives it the kernel's dead end.
 */
/* EXPECT: exec900: alert 80000002 task crasher */
#include "test.h"
#include "crashtest.h"

static void
crasher()
{
	Forbid();
	readnull();
	ran++;
}

int
main()
{
	if (FindTask(NULL)->tc_TrapCode == SysBase->TaskTrapCode) {
		KPutStr("FAIL crashforbid: TaskTrapCode not dos.library's\n");
		return (1);
	}
	CreateTask("crasher", 1L, crasher, CSTACK);
	Sleep(100L);
	KPutStr("FAIL crashforbid: the trap did not stop the system\n");
	return (1);
}
