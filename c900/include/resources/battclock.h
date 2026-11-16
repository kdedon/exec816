/* SPDX-License-Identifier: MIT */
/*
 * battclock.resource: the battery clock as seconds since 1978-01-01
 * 00:00:00.  Years 1978-2077 are representable.  Task context only.
 * Set BattClockBase = OpenResource(BATTCLOCKNAME) before the calls.
 */
#ifndef RESOURCES_BATTCLOCK_H
#define RESOURCES_BATTCLOCK_H

#include <exec/types.h>

#define BATTCLOCKNAME	"battclock.resource"

#define BC_MAXTIME	((ULONG)0xBC19137FL)	/* 2077-12-31 23:59:59 */

#define BCERR_NOCLOCK	(-1)	/* no clock answered, or it refused the set */
#define BCERR_RANGE	(-2)	/* time past BC_MAXTIME */

extern APTR BattClockBase;

/* ReadBattClock(): 0 when no valid clock answers.  WriteBattClock(t) and
 * ResetBattClock() (WriteBattClock(0)): 0, BCERR_NOCLOCK or BCERR_RANGE. */
#include <lvo/battclock_lib.h>

#endif
