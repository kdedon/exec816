/ SPDX-License-Identifier: MIT
/ C program startup, linked first so that start opens the code hunk.
/ dos.library calls the first hunk as LONG start(char *args, LONG len);
/ start enters cstart (libc/cstart.c) with the same frame.

	.globl	start, cstart_

	.shri

start:
	jp	cstart_
