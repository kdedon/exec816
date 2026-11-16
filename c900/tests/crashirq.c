/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * A trap in an interrupt server goes to the interrupted Task's
 * tc_TrapCode.  Its frame is on the interrupt stack, so dos.library's
 * default does not hold that Task but gives the kernel's dead end.
 */
/* EXPECT: exec900: alert 80000002 task spinner */
#include "test.h"
#include "crashtest.h"

static struct Interrupt crashint;
static struct Task *spinner;

static int
crashsrv(data, n)
APTR data;
UWORD n;
{
	if (spinner != NULL && FindTask(NULL) == spinner)
		readnull();
	return (0);
}

static void
spin()
{
	for (;;)
		ran++;
}

int
main()
{
	if (FindTask(NULL)->tc_TrapCode == SysBase->TaskTrapCode) {
		KPutStr("FAIL crashirq: TaskTrapCode not dos.library's\n");
		return (1);
	}
	spinner = CreateTask("spinner", -1L, spin, CSTACK);
	crashint.is_Node.ln_Name = "crashirq";
	crashint.is_Code = crashsrv;
	AddIntServer(INTV_TICK, &crashint);
	Sleep(100L);
	RemIntServer(INTV_TICK, &crashint);
	KPutStr("FAIL crashirq: the trap did not stop the system\n");
	return (1);
}
