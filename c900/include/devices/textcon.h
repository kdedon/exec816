/* SPDX-License-Identifier: MIT */
/*
 * textcon.device: Exec900's text console, the operator's console.  One
 * unit; CMD_WRITE goes through the terminal emulator to the display kboot
 * chose, or to the serial console without one; CMD_READ returns the
 * characters typed so far.  docs/internals.md gives the escape repertoire
 * and the departures from Amiga console.device.  The name console.device
 * belongs to the window console, as on Amiga (<devices/console.h>).
 */
#ifndef DEVICES_TEXTCON_H
#define DEVICES_TEXTCON_H

#include <exec/io.h>
#include <devices/conunit.h>

#define TEXTCONNAME	"textcon.device"

/* Commands between textcon.device and the display module (<display.h>),
 * past Amiga console.device's keymap commands.  CD_ASKDISPLAY stores the
 * address of textcon.device's struct ConDisplay at io_Data (io_Length at
 * least 4).  CD_SETDISPLAY with io_Length 1 attaches the display that
 * structure names, the first time replaying the boot text; with 0 it
 * detaches it, and output goes to the serial line only. */
#define CD_ASKDISPLAY	(CMD_NONSTD + 16)
#define CD_SETDISPLAY	(CMD_NONSTD + 17)

#endif
