/* SPDX-License-Identifier: MIT */
/*
 * beep.device: the internal speaker, a square wave from CIO U66
 * counter/timer 2 on PB0.  Amiga has no counterpart; docs/internals.md
 * gives the interface and why it is not an audio.device subset.  One
 * unit, 0, opened shared by any number of Tasks; CMD_WRITE requests play
 * one after another in submission order.
 */
#ifndef DEVICES_BEEP_H
#define DEVICES_BEEP_H

#include <exec/io.h>

#define BEEPNAME	"beep.device"

/* CMD_WRITE plays io_Freq for io_Micros; CMD_FLUSH aborts every queued
 * tone; CMD_RESET also silences the console bell. */
struct IOBeep {
	struct IORequest io_Req;
	ULONG io_Freq;		/* Hz, BEEP_MINFREQ-BEEP_MAXFREQ; 0: a rest */
	ULONG io_Micros;	/* duration, microseconds; 0 completes at once */
};

#define BEEP_MINFREQ	3L
#define BEEP_MAXFREQ	20000L

/* io_Error beyond IOERR_*. */
#define BeepErr_Freq	1	/* io_Freq out of range */

#endif
