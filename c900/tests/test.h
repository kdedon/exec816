/* SPDX-License-Identifier: MIT */
/*
 * Tests are hunk programs and cannot link kernel symbols.  kernel.h gives
 * them the kernel's types and macros; this header maps the kernel names
 * they use onto ExecBase, CachePreDMA and the test-only TestHooks table
 * (kernel/testhooks.h).  A kernel name used without a mapping here fails
 * the link (tools/mkhunk.py: unresolved external).
 */
#ifndef TEST_H
#define TEST_H

#include "kernel.h"
#include "display.h"
#include "vt.h"
#include "testhooks.h"

#define TickCount	(SysBase->ex_TickCount)

/* The physical address of p: CachePreDMA, 0xFFFFFFFF when unmapped. */
static ULONG
tkphys(p)
APTR p;
{
	ULONG n;

	n = 1;
	return ((ULONG)CachePreDMA(p, &n, 0L));
}

#define kphys(p)	tkphys((APTR)(p))

#define KT		((struct TestHooks *)TestHooks())

#define kmmutab		(KT->th_kmmutab)
#define HeapPages	(*KT->th_HeapPages)
#define HeapTail	(*KT->th_HeapTail)
#define PhysPages	(*KT->th_PhysPages)
#define PhysFree	(*KT->th_PhysFree)
#define kstklow		(*KT->th_kstklow)
#define kend		(KT->th_end)
#define kalloc		(*KT->th_kalloc)
#define kdeallocate	(*KT->th_kdeallocate)
#define kallocphys	(*KT->th_kallocphys)
#define kfreephys	(*KT->th_kfreephys)
#define kwinmap		(*KT->th_kwinmap)
#define kwinunmap	(*KT->th_kwinunmap)
#define kintstk		(*KT->th_kintstk)
#define kstkcfg		(*KT->th_kstkcfg)
#define STKPOOLTOP	(kstkcfg.sc_root - IDLESTKSIZE)

#define WdStray		(*KT->th_WdStray)
#define BattRetries	(*KT->th_BattRetries)
#define KbdIntrs	(*KT->th_KbdIntrs)
#define KbdPolled	(*KT->th_KbdPolled)
#define KbdLost		(*KT->th_KbdLost)
#define kbd_getc	(*KT->th_kbd_getc)
#define con_bell	(*KT->th_con_bell)

#define kbfinit		(*KT->th_kbfinit)
#define kbfopen		(*KT->th_kbfopen)
#define kbfread		(*KT->th_kbfread)
#define kbfdone		(*KT->th_kbfdone)
#define BootFuncs	(KT->th_BootFuncs)

#endif
