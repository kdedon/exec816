/* SPDX-License-Identifier: MIT */
/*
 * Exec900 public interface.
 *
 * MWC is a K&R compiler: there are no prototypes, so an int literal passed
 * where a callee expects a long or a pointer is pushed as two bytes, not
 * four.  Every public call is therefore a macro that casts each argument to
 * its declared width and calls through exec.library's jump table.  Call the
 * macro names; the x names are the kernel's implementations.
 */
#ifndef EXEC_EXEC_H
#define EXEC_EXEC_H

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/lists.h>
#include <exec/tasks.h>
#include <exec/ports.h>
#include <exec/interrupts.h>
#include <exec/io.h>
#include <exec/errors.h>
#include <exec/devices.h>
#include <exec/memory.h>
#include <exec/alerts.h>
#include <exec/semaphores.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <exec/execbase.h>

/* MEMF_BANK0 names the memory a CPU stack must occupy.  On the 65816 that
 * is bank zero; on the Z8001 it is the single stack segment every compiled
 * frame address is relocated against (see docs/platform.md). */
#define MEMF_STACK MEMF_BANK0

/* The calls: LVO macros generated from fd/exec_lib.fd (tools/genlvo.py).
 * Kernel sources, built with EXEC_KERNEL, call the x functions directly. */
#include <lvo/exec_lib.h>

/* Diagnostic console (polled serial).  Not part of the classic API. */
void xKPrintf();

#endif
