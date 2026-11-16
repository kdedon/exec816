/* SPDX-License-Identifier: GPL-3.0-only */
/* Disk DMA into the reclaimed image tail and across the stack segment's
 * physical page boundary (tailtest.h), with the 32 KB segment, whose
 * partial heap page has room for the buffers. */
#define TAILNAME	"tailmem"
#include "test.h"
#include "tailtest.h"
