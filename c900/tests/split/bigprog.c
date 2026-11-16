/* SPDX-License-Identifier: MIT */
/*
 * The multi-hunk program for tests/bighunk.c: linked first, so start
 * opens hunk 0; the generated parts (genbig.py) fill the other hunks
 * and the C library's atol a hunk of its own.
 */
typedef unsigned long UL;

extern UL bchain0();
extern long atol();

UL
start(v)
UL v;
{
	return (bchain0(v) ^ (UL)atol("4660"));
}
