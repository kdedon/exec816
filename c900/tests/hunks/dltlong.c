/* SPDX-License-Identifier: MIT */
/*
 * dltlongname.library, for tests/ramlib.c: dlt.library under a name
 * longer than the 14 bytes a V7 directory entry keeps.
 */
#define DTNAME	"dltlongname.library"
#include "dlt.c"
