/* SPDX-License-Identifier: MIT */
/*
 * console.device, the Amiga window console.  Exec900 does not provide
 * it: Workbench's console.device, loaded from DEVS:, draws in intuition
 * windows, and without Workbench OpenDevice(CONSOLENAME) fails.  The
 * kernel's text console is textcon.device (<devices/textcon.h>).
 */
#ifndef DEVICES_CONSOLE_H
#define DEVICES_CONSOLE_H

#include <exec/io.h>

#define CONSOLENAME	"console.device"

#define CD_ASKKEYMAP		(CMD_NONSTD + 0)
#define CD_SETKEYMAP		(CMD_NONSTD + 1)
#define CD_ASKDEFAULTKEYMAP	(CMD_NONSTD + 2)
#define CD_SETDEFAULTKEYMAP	(CMD_NONSTD + 3)

#endif
