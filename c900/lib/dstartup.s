/ SPDX-License-Identifier: MIT
/ DOS program startup, linked first so that start opens the code hunk.
/ dos.library calls the first hunk as LONG start(char *args, LONG len)
/ (RunCommand, CreateNewProc); start enters dosstart (lib/dosstart.c)
/ with the same frame, which opens dos.library, calls main(args, len)
/ and returns its result.

	.globl	start, dosstart_

	.shri

start:
	jp	dosstart_
