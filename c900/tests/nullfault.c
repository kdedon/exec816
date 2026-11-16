/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Segment 0 is CPU-inhibit: reading through NULL takes the segment trap,
 * which the default tc_TrapCode reports as a dead-end alert.
 */
/* EXPECT: exec900: alert 80000002 task root */
#include "test.h"

int
main()
{
	register UWORD v;

	KPutStr("nullfault: reading NULL\n");
	v = *(volatile UWORD *)NULL;
	xKPrintf("FAIL nullfault: read %x through NULL without a trap\n", v);
	return (0);
}
