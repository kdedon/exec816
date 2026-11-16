/* SPDX-License-Identifier: MIT */
/*
 * End to end: DOSBoot with tests/sys/Startup-Sequence, which runs Echo,
 * CD, C:Dir, C:Type, Stack and C:DosPass, which prints the PASS line,
 * then EndCLI, so the run ends with "exec900: root exit 0".
 */
#include "../dos/dosboot.c"
