/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * exec.library's test-only TestHooks vector: the kernel internals the
 * white-box tests use (kernel/testhooks.h).
 */
#include "kernel.h"
#include "testhooks.h"

extern char end[];
extern ULONG WdStray;
extern ULONG BattRetries;
extern ULONG KbdIntrs;
extern ULONG KbdPolled;
extern ULONG KbdLost;

static struct TestHooks khooks = {
	kmmutab, &HeapPages, &HeapTail, &PhysPages, &PhysFree, &kstklow,
	end, kalloc, kdeallocate, kallocphys, kfreephys, kwinmap, kwinunmap,
	&kintstk, &kstkcfg,
	&WdStray, &BattRetries, &KbdIntrs, &KbdPolled, &KbdLost, kbd_getc,
	con_bell,
	kbfinit, kbfopen, kbfread, kbfdone, BootFuncs
};

APTR
xTestHooks()
{
	return ((APTR)&khooks);
}
