/* SPDX-License-Identifier: MIT */
/*
 * gameport.device: the HR card's J2 mouse or a serial mouse on SCC U74
 * channel A, in the classic Amiga interface.  Unit 0 is the only port.  GPD_READEVENT returns
 * IECLASS_RAWMOUSE (and, on a trigger timeout, IECLASS_TIMER) events from
 * <devices/inputevent.h>; docs/internals.md records the departures.
 *
 * Exec900 extension: GetGPButtons() (<lvo/gameport_lib.h>, LVO -42)
 * returns the buttons held now as IEQUALIFIER_LEFTBUTTON, _MIDBUTTON and
 * _RBUTTON, from any Task, without waiting.  The program defines
 * GamePortBase, set from io_Device after OpenDevice.
 */
#ifndef DEVICES_GAMEPORT_H
#define DEVICES_GAMEPORT_H

#include <exec/io.h>
#include <devices/inputevent.h>

#define GAMEPORTNAME	"gameport.device"

#define GPD_READEVENT	(CMD_NONSTD + 0)
#define GPD_ASKCTYPE	(CMD_NONSTD + 1)
#define GPD_SETCTYPE	(CMD_NONSTD + 2)
#define GPD_ASKTRIGGER	(CMD_NONSTD + 3)
#define GPD_SETTRIGGER	(CMD_NONSTD + 4)

/* Controller types: one byte at io_Data. */
#define GPCT_ALLOCATED		(-1)
#define GPCT_NOCONTROLLER	0
#define GPCT_MOUSE		1
#define GPCT_RELJOYSTICK	2
#define GPCT_ABSJOYSTICK	3

/* Trigger conditions: gpt_Timeout counts 100 Hz ticks, the deltas
 * counter steps. */
struct GamePortTrigger {
	UWORD gpt_Keys;
	UWORD gpt_Timeout;
	UWORD gpt_XDelta;
	UWORD gpt_YDelta;
};

#define GPTF_DOWNKEYS	0x01
#define GPTF_UPKEYS	0x02

#define GPDERR_SETCTYPE	1

extern struct Device *GamePortBase;

#include <lvo/gameport_lib.h>

#endif
