/* SPDX-License-Identifier: MIT */
/* system: one command line in a new shell, through SystemTagList. */
#include "libcint.h"
#include <utility/tagitem.h>

int
system(cmd)
char *cmd;
{
	struct TagItem t[1];

	if (cmd == NULL)
		return (1);
	t[0].ti_Tag = TAG_DONE;
	return ((int)SystemTagList(cmd, t));
}
