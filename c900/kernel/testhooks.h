/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Test-only: kernel internals the white-box tests read and call, as the
 * table exec.library's TestHooks vector returns.  Not an API; fields
 * change with the kernel.  tests/test.h maps each to its kernel name.
 */
#ifndef TESTHOOKS_H
#define TESTHOOKS_H

struct BootFile;

struct TestHooks {
	/* Logical address space and memory (platform/machine.c). */
	UWORD *th_kmmutab;
	UWORD *th_HeapPages;
	UWORD *th_HeapTail;
	UWORD *th_PhysPages;
	UWORD *th_PhysFree;
	UWORD *th_kstklow;
	char *th_end;			/* the kernel's end of bss */
	APTR (*th_kalloc)();
	void (*th_kdeallocate)();
	int (*th_kallocphys)();
	void (*th_kfreephys)();
	APTR (*th_kwinmap)();
	void (*th_kwinunmap)();
	UWORD *th_kintstk;
	struct stkcfg *th_kstkcfg;

	/* Driver counters and entry points. */
	ULONG *th_WdStray;
	ULONG *th_BattRetries;
	ULONG *th_KbdIntrs;
	ULONG *th_KbdPolled;
	ULONG *th_KbdLost;
	int (*th_kbd_getc)();
	void (*th_con_bell)();

	/* The boot-volume reader (kernel/bootmods.c). */
	struct BootFile *(*th_kbfinit)();
	int (*th_kbfopen)();
	LONG (*th_kbfread)();
	void (*th_kbfdone)();
	APTR *th_BootFuncs;
};

#endif
