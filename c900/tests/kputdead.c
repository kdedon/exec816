/* SPDX-License-Identifier: MIT */
/*
 * A dead-end alert right after queued console text: the alert sends the
 * queue out polled before its own message, so both reach the line, in
 * order, before the system stops.
 */
/* EXPECT: kputdead: tail exec900: alert 80001234 task root */
#include "test.h"

int
main()
{
	register int i;

	for (i = 0; i < 4; i++)
		KPutStr("kputdead: 0123456789012345678901234567890123456789\n");
	KPutStr("kputdead: tail ");
	Alert(AT_DeadEnd | 0x1234L);
	return (1);
}
