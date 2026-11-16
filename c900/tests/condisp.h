/* SPDX-License-Identifier: MIT */
/*
 * Shared by the display tests: the display console's facts from
 * textcon.device (<display.h>).
 */
#ifndef CONDISP_H
#define CONDISP_H

#include <exec/exec.h>
#include <devices/textcon.h>
#include "display.h"

/* textcon.device's struct ConDisplay, or NULL.  With detach, its display
 * no longer gets console output, so a test can draw on it alone. */
static struct ConDisplay *
conask(detach)
int detach;
{
	register struct IOStdReq *ior;
	struct MsgPort *mp;
	struct ConDisplay *cd;

	cd = NULL;
	if ((mp = CreateMsgPort()) == NULL)
		return (NULL);
	ior = (struct IOStdReq *)CreateIORequest(mp, sizeof(*ior));
	if (ior != NULL && OpenDevice(TEXTCONNAME, CONU_STANDARD, ior, 0) == 0) {
		ior->io_Command = CD_ASKDISPLAY;
		ior->io_Data = (APTR)&cd;
		ior->io_Length = sizeof(cd);
		if (DoIO(ior) != 0)
			cd = NULL;
		if (detach) {
			ior->io_Command = CD_SETDISPLAY;
			ior->io_Length = 0;
			DoIO(ior);
		}
		CloseDevice(ior);
	}
	DeleteIORequest(ior);
	DeleteMsgPort(mp);
	return (cd);
}

#endif
