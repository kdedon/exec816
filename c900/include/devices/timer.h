/* SPDX-License-Identifier: MIT */
/*
 * timer.device, as AmigaOS 3.x <devices/timer.h>.  The base functions
 * (AddTime, SubTime, CmpTime, ReadEClock, GetSysTime) come from
 * <lvo/timer_lib.h>; the program defines TimerBase, set from io_Device
 * after OpenDevice.  System time counts seconds since 1978-01-01.
 */
#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H

#include <exec/types.h>
#include <exec/io.h>

#define UNIT_MICROHZ	0
#define UNIT_VBLANK	1
#define UNIT_ECLOCK	2
#define UNIT_WAITUNTIL	3
#define UNIT_WAITECLOCK	4

#define TIMERNAME	"timer.device"

struct timeval {
	ULONG tv_secs;
	ULONG tv_micro;
};

struct EClockVal {
	ULONG ev_hi;
	ULONG ev_lo;
};

struct timerequest {
	struct IORequest tr_node;
	struct timeval tr_time;
};

#define TR_ADDREQUEST	CMD_NONSTD
#define TR_GETSYSTIME	(CMD_NONSTD + 1)
#define TR_SETSYSTIME	(CMD_NONSTD + 2)

extern struct Device *TimerBase;

#include <lvo/timer_lib.h>

#endif
