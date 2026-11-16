/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Linked into tests/tailpad.c's kernel image: 44 KB of bss, which moves
 * the stack segment and the heap after it (tests/tailtest.h).  Two arrays:
 * an int-sized array bound stays under 32 KB.
 */
char kpad[0x5800];
char kpad2[0x5800];
