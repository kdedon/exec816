/* SPDX-License-Identifier: MIT */
/*
 * keyboard.device: the C900 keyboard on CIO U70 port A, in the classic
 * Amiga keyboard.device model.  One unit, 0.  KBD_READEVENT returns
 * IECLASS_RAWKEY InputEvents (<devices/inputevent.h>) whose codes are
 * Amiga raw key codes, with IECODE_UP_PREFIX on release.
 * docs/internals.md gives the semantics and the departures from Amiga.
 */
#ifndef DEVICES_KEYBOARD_H
#define DEVICES_KEYBOARD_H

#include <exec/io.h>
#include <devices/inputevent.h>

#define KEYBOARDNAME	"keyboard.device"

/* Commands besides CMD_CLEAR. */
#define KBD_READEVENT		(CMD_NONSTD + 0)
#define KBD_READMATRIX		(CMD_NONSTD + 1)
#define KBD_ADDRESETHANDLER	(CMD_NONSTD + 2)	/* unsupported */
#define KBD_REMRESETHANDLER	(CMD_NONSTD + 3)	/* unsupported */
#define KBD_RESETHANDLERDONE	(CMD_NONSTD + 4)	/* unsupported */

/* KBD_READMATRIX: one bit per raw code, bit (code & 7) of byte code >> 3. */
#define KBD_MATRIXSIZE	16

/*
 * The C900 keys with no Amiga raw key code take codes of keys the C900
 * lacks, which the usa keymap leaves without characters: 0x0E, 0x1C,
 * 0x2C and 0x3B are on no Amiga keyboard, 0x2B is the international key
 * beside Return, 0x6B-0x6D are AROS's Scroll Lock, Print Screen and Num
 * Lock.  The other keys' codes are in <devices/rawkeycodes.h>.
 */
#define RAWKEY_KP_00	0x0E	/* keypad "00" */
#define RAWKEY_KP_CE	0x1C	/* keypad CE */
#define RAWKEY_W1	0x2B
#define RAWKEY_W2	0x2C
#define RAWKEY_W3	0x3B
#define RAWKEY_F13	0x6B
#define RAWKEY_F14	0x6C
#define RAWKEY_F15	0x6D

#endif
