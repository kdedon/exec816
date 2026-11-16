/* SPDX-License-Identifier: MIT */
/*
 * DateToStr and StrToDate (dos/datetime.h, V36+) for Exec900.  There is
 * no locale.library, so FORMAT_DEF is FORMAT_DOS and the strings are
 * English.
 */
#ifndef DOS_DATETIME_H
#define DOS_DATETIME_H

#include <dos/dos.h>

struct DateTime {
	struct DateStamp dat_Stamp;
	UBYTE dat_Format;	/* FORMAT_ */
	UBYTE dat_Flags;	/* DTF_ */
	char *dat_StrDay;	/* LEN_DATSTRING bytes, or NULL */
	char *dat_StrDate;
	char *dat_StrTime;
};

#define LEN_DATSTRING	16

#define DTB_SUBST	0	/* Today, Tomorrow, a weekday name */
#define DTB_FUTURE	1	/* weekday names are in the coming week */
#define DTF_SUBST	(1 << DTB_SUBST)
#define DTF_FUTURE	(1 << DTB_FUTURE)

#define FORMAT_DOS	0	/* dd-mmm-yy */
#define FORMAT_INT	1	/* yy-mm-dd */
#define FORMAT_USA	2	/* mm-dd-yy */
#define FORMAT_CDN	3	/* dd-mm-yy */
#define FORMAT_DEF	4	/* the locale's; here FORMAT_DOS */
#define FORMAT_MAX	FORMAT_CDN

#endif
