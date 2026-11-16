/* SPDX-License-Identifier: MIT */
/*
 * Callback hooks, as Amiga utility/hooks.h.  There is no utility.library
 * and no register convention: a hook is called as
 * h_Entry(hook, object, message), the arguments amiga.lib's HookEntry
 * passes on to h_SubEntry.
 */
#ifndef UTILITY_HOOKS_H
#define UTILITY_HOOKS_H

#include <exec/types.h>
#include <exec/nodes.h>

/* The Hook function's type. */
typedef unsigned long (*HOOKFUNC)();

struct Hook {
	struct MinNode h_MinNode;
	ULONG (*h_Entry)();
	ULONG (*h_SubEntry)();
	APTR h_Data;
};

#endif
