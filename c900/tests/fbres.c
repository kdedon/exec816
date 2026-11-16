/* SPDX-License-Identifier: MIT */
/*
 * framebuf.resource on every card: absent without a bitmap card (LR,
 * none); with one, GetFBInfo describes textcon.device's card and mode,
 * and AllocFB lends the bitmap to one owner at a time: NULL for the
 * first, the owner's name to the next, NULL again after FreeFB.
 */
#include "test.h"
#include "fbtest.h"

static char first[] = "fbres";
static char second[] = "fbres two";

static char *
owners()
{
	char *o;

	FreeFB();			/* the console's: no effect */
	if (AllocFB(first) != NULL)
		return ("first AllocFB refused");
	if ((o = AllocFB(second)) != first)
		return ("second AllocFB not refused with the owner's name");
	if ((o = AllocFB(first)) != first)
		return ("owner's own AllocFB not refused");
	FreeFB();
	if (AllocFB(second) != NULL)
		return ("AllocFB after FreeFB refused");
	if (AllocFB(first) != second)
		return ("refusal names the old owner");
	FreeFB();
	return (NULL);
}

int
main()
{
	char *err;
	int card;

	if ((err = fbopen(&card)) == NULL && fbm != NULL)
		err = owners();
	if (err != NULL) {
		xKPrintf("FAIL fbres: %s\n", err);
		return (1);
	}
	xKPrintf("fbres: card %u, resource %s\nPASS fbres\n", card,
	    fbm != NULL ? "checked" : "absent");
	return (0);
}
