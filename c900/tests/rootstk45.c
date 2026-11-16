/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * A 45 KB stack segment with a 1 KB root stack on a 1 MB machine
 * (mkdisk.py --stack=45 --rootstack=1): the segment's RAM from offset
 * 0x4C00 to 0x4F00 up, the heap after it, the pool to the idle stack
 * under the 1 KB root stack (rootstk.h), and a Task on a 32 KB stack
 * whose frame reaches below the 32 KB segment's low end.
 */
#include "test.h"
#include "rootstk.h"

#define BIGSTK	32768

static volatile int ok;
static UBYTE *low;

static void
body()
{
	UBYTE f[BIGSTK - 1024];
	register UWORD i;

	for (i = 0; i < sizeof f; i++)
		f[i] = (UBYTE)i;
	for (i = 0; i < sizeof f && f[i] == (UBYTE)i; i++)
		;
	low = f;
	ok = i == sizeof f ? 1 : -1;
}

static char *
run()
{
	char *why;

	stkexpect = 0x4c00;
	if ((why = rootcheck("rootstk45")) != NULL)
		return (why);
	if (CreateTask("bigstk", 1L, (TASKPROC)body, (ULONG)BIGSTK) == NULL)
		return ("CreateTask with a 32 KB stack");
	while (ok == 0)
		;
	if (ok < 0)
		return ("the big frame changed");
	if (POFF(low) >= STKSEGLOW)
		return ("the frame is not below the 32 KB segment's low end");
	return (NULL);
}

int
main()
{
	char *why;

	if ((why = run()) != NULL)
		xKPrintf("FAIL rootstk45: %s\n", why);
	else
		KPutStr("PASS rootstk45\n");
	return (0);
}
