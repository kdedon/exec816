/* SPDX-License-Identifier: MIT */
/* Lists, as Amiga <exec/lists.h>. */
#ifndef EXEC_LISTS_H
#define EXEC_LISTS_H

#include <exec/nodes.h>

struct List {
	struct Node *lh_Head;
	struct Node *lh_Tail;
	struct Node *lh_TailPred;
	UBYTE lh_Type;
	UBYTE lh_Pad;
};

struct MinList {
	struct MinNode *mlh_Head;
	struct MinNode *mlh_Tail;
	struct MinNode *mlh_TailPred;
};

#define IsListEmpty(l) ((l)->lh_TailPred == (struct Node *)(l))
#define IsMinListEmpty(l) ((l)->mlh_TailPred == (struct MinNode *)(l))
#define IsMsgPortEmpty(p) \
	((p)->mp_MsgList.lh_TailPred == (struct Node *)&(p)->mp_MsgList)

void xNewList();

#define NewList(l) xNewList((struct List *)(l))
#define NewMinList(l) xNewList((struct List *)(l))

#endif
