/* SPDX-License-Identifier: MIT */
/*
 * The linked functions programs use beside exec.library's vectors, as
 * amiga.lib and debug.lib provide them on the Amiga: NewList and
 * KPrintf, under the names <exec/lists.h> and <exec/exec.h> call.
 */
#include <exec/exec.h>

/* exec.library's private RawPutChar, which debug.lib's KPrintf uses. */
#define LVORawPutChar	(-516)

void
xNewList(l)
register struct List *l;
{
	l->lh_Head = (struct Node *)&l->lh_Tail;
	l->lh_Tail = NULL;
	l->lh_TailPred = (struct Node *)&l->lh_Head;
}

/* Formatted console output; Forbid keeps one call's output together. */
void
xKPrintf(fmt, args)
char *fmt;
int args;
{
	Forbid();
	RawDoFmt(fmt, (APTR)&args, (APTR)((char *)SysBase + LVORawPutChar),
	    NULL);
	Permit();
}
