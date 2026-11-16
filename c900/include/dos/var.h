/* SPDX-License-Identifier: MIT */
/* AmigaDOS local and environment variables (dos/var.h, V36+). */
#ifndef DOS_VAR_H
#define DOS_VAR_H

#include <exec/lists.h>

/* A local variable on pr_LocalVars; ln_Type is LV_VAR or LV_ALIAS. */
struct LocalVar {
	struct Node lv_Node;
	UWORD lv_Flags;		/* GVF_BINARY_VAR */
	UBYTE *lv_Value;
	ULONG lv_Len;
};

#define LV_VAR		0
#define LV_ALIAS	1
#define LVB_IGNORE	7	/* in ln_Type: skip this one */
#define LVF_IGNORE	0x80

#define GVB_GLOBAL_ONLY		8
#define GVF_GLOBAL_ONLY		0x100L
#define GVB_LOCAL_ONLY		9
#define GVF_LOCAL_ONLY		0x200L
#define GVB_BINARY_VAR		10	/* do not stop at a newline */
#define GVF_BINARY_VAR		0x400L
#define GVB_DONT_NULL_TERM	11	/* no NUL (binary only) */
#define GVF_DONT_NULL_TERM	0x800L
#define GVB_SAVE_VAR		12	/* also write ENVARC: */
#define GVF_SAVE_VAR		0x1000L

#endif
