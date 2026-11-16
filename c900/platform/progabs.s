/ SPDX-License-Identifier: MIT
/ The absolute symbols a separately linked program or module needs, linked
/ into its `ld -r' output so that tools/mkhunk.py finds them resolved.
/ cc2 relocates every compiled frame address against SS (crt.s), and
/ exec.library's call macros read SysBase, the first long of the kernel's
/ data segment.

	.globl	SS
	.globl	SysBase_

SS		=	0x3f3f
SysBase_	=	0x31000000
