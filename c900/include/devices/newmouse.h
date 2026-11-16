/* SPDX-License-Identifier: MIT */
/*
 * NewMouse wheel and fourth-button codes, the ie_Code of AROS's
 * IECLASS_NEWMOUSE events.  The values are <devices/rawkeycodes.h>'s
 * RAWKEY_NM_*.  An extension: no Exec900 driver sends them.
 */
#ifndef DEVICES_NEWMOUSE_H
#define DEVICES_NEWMOUSE_H

#define NM_WHEEL_UP		0x7A
#define NM_WHEEL_DOWN		0x7B
#define NM_WHEEL_LEFT		0x7C
#define NM_WHEEL_RIGHT		0x7D
#define NM_BUTTON_FOURTH	0x7E

#ifndef IECLASS_NEWMOUSE
#define IECLASS_NEWMOUSE	0x16
#endif

#endif
