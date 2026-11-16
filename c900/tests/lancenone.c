/* SPDX-License-Identifier: MIT */
/*
 * lance.device without the card: the module loads from Modules, finds
 * no chip and adds no device, so OpenDevice fails.
 */
#include "lantest.h"

int
main()
{
	struct IOSana2Req *a;

	if ((a = lnreq(0)) == NULL) {
		xKPrintf("FAIL lancenone: CreateIORequest\n");
		return (1);
	}
	if (OpenDevice(LANCENAME, 0L, a, 0L) != IOERR_OPENFAIL) {
		xKPrintf("FAIL lancenone: opened without a card\n");
		return (1);
	}
	xKPrintf("PASS lancenone\n");
	return (0);
}
