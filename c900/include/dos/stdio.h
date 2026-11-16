/* SPDX-License-Identifier: MIT */
/* AmigaDOS buffered I/O (dos/stdio.h, V36+): SetVBuf modes and macros. */
#ifndef DOS_STDIO_H
#define DOS_STDIO_H

#define ReadChar()		FGetC(Input())
#define WriteChar(c)		FPutC(Output(), (c))
#define UnReadChar(c)		UnGetC(Input(), (c))
#define ReadChars(buf, num)	FRead(Input(), (buf), 1L, (num))
#define ReadLn(buf, len)	FGets(Input(), (buf), (len))
#define WriteStr(s)		FPuts(Output(), (s))
#define VWritef(format, argv)	VFWritef(Output(), (format), (argv))

#define BUF_LINE	0L	/* flush at each newline */
#define BUF_FULL	1L	/* flush when full */
#define BUF_NONE	2L	/* no buffering */

#define ENDSTREAMCH	(-1L)

#endif
