/* SPDX-License-Identifier: MIT */
/*
 * Console units and OpenDevice flags, as in Amiga <devices/conunit.h>.
 * textcon.device has unit CONU_STANDARD only and ignores the flags.
 */
#ifndef DEVICES_CONUNIT_H
#define DEVICES_CONUNIT_H

#define CONU_LIBRARY	-1
#define CONU_STANDARD	0
#define CONU_CHARMAP	1
#define CONU_SNIPMAP	3

#define CONFLAG_DEFAULT			0
#define CONFLAG_NODRAW_ON_NEWSIZE	1

#endif
