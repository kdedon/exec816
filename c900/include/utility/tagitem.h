/* SPDX-License-Identifier: MIT */
/*
 * Tag items, as Amiga utility/tagitem.h.  There is no utility.library:
 * dos.library walks its own tag lists (TAG_DONE, TAG_IGNORE, TAG_MORE,
 * TAG_SKIP).
 */
#ifndef UTILITY_TAGITEM_H
#define UTILITY_TAGITEM_H

#include <exec/types.h>

typedef ULONG Tag;

struct TagItem {
	Tag ti_Tag;
	ULONG ti_Data;
};

#define TAG_DONE	0L
#define TAG_END		0L
#define TAG_IGNORE	1L
#define TAG_MORE	2L
#define TAG_SKIP	3L
#define TAG_USER	((ULONG)(1L<<31))

/* FilterTagItems logic. */
#define TAGFILTER_AND	0
#define TAGFILTER_NOT	1

/* MapTags policy. */
#define MAP_REMOVE_NOT_FOUND	0
#define MAP_KEEP_NOT_FOUND	1

#endif
