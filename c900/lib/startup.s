/ SPDX-License-Identifier: MIT
/ Program startup, linked first so that start opens the code hunk, where
/ the loader's caller enters (docs/platform.md, Program loading).  The
/ loader has already relocated the hunks and cleared bss, and SysBase is
/ an absolute symbol (progabs.s), so nothing is left to set up: start
/ enters main on the caller's stack, and main's int result in r1 is the
/ program's return code.

	.globl	start, main_

	.shri

start:
	jp	main_
