/* SPDX-License-Identifier: GPL-3.0-only */
/* The default root program: report the running system and stop. */
#include <exec/exec.h>

int
main()
{
	struct Task *me;

	me = FindTask(NULL);
	xKPrintf("hello from %s, Exec %x, %lu bytes free, %lu in stacks\n",
	    me->tc_Node.ln_Name, ExecVersion(), AvailMem(MEMF_ANY),
	    AvailMem(MEMF_STACK));
	KPutStr("PASS hello\n");
	return (0);
}
