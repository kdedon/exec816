/* SPDX-License-Identifier: MIT */
/*
 * A dead-end alert while framebuf.resource has lent the bitmap: the
 * console takes the card back, clears it, draws the text printed while
 * lent and then the alert (with an HR card, the screen capture shows
 * them).  Without a bitmap card only the alert is checked.
 */
/* EXPECT: exec900: alert 80001234 task root */
#include "test.h"
#include "fbtest.h"

int
main()
{
	register UWORD *p;
	register UWORD i;
	char *err;
	int card;

	if ((err = fbopen(&card)) != NULL) {
		xKPrintf("FAIL fbpanic: %s\n", err);
		return (1);
	}
	if (fbm != NULL && fbm->fbm_Half[0] != NULL) {
		if (AllocFB("fbpanic") != NULL) {
			xKPrintf("FAIL fbpanic: AllocFB refused\n");
			return (1);
		}
		p = (UWORD *)fbm->fbm_Half[0];
		for (i = 0; i < 0x4000; i++)
			p[i] = 0xAAAA;
		xKPrintf("fbpanic: lent and drawn\n");
	}
	Alert(AT_DeadEnd | 0x1234L);
	xKPrintf("FAIL fbpanic: dead-end alert returned\n");
	return (1);
}
