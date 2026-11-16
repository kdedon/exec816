/* SPDX-License-Identifier: MIT */
/*
 * serial.device: the Z8030 SCC lines, in the classic Amiga serial.device
 * model.  Units are the machine's serial lines in kboot's bi_serial order:
 * 0 = 0x0100 (the ROM and kernel console), 1 = 0x0120 (busy while
 * gameport.device's serial mouse holds it), 2-5 = the LR board at 0x0300,
 * 0x0320, 0x0380 and 0x03A0.  docs/internals.md gives the
 * semantics and the departures from Amiga.
 */
#ifndef DEVICES_SERIAL_H
#define DEVICES_SERIAL_H

#include <exec/io.h>

#define SERIALNAME	"serial.device"
#define SER_UNITS	6

/* Commands besides CMD_RESET, CMD_READ, CMD_WRITE, CMD_CLEAR, CMD_FLUSH. */
#define SDCMD_QUERY	(CMD_NONSTD + 0)
#define SDCMD_BREAK	(CMD_NONSTD + 1)
#define SDCMD_SETPARAMS	(CMD_NONSTD + 2)

/* End-of-file characters for SERF_EOFMODE: any of the eight bytes ends a
 * read, which includes it.  Fill unused bytes with a repeat of one used. */
struct IOTArray {
	ULONG TermArray0;
	ULONG TermArray1;
};

struct IOExtSer {
	struct IOStdReq IOSer;
	ULONG io_CtlChar;	/* stored; no XON/XOFF is done */
	ULONG io_RBufLen;	/* receive ring size, at least 64 */
	ULONG io_ExtFlags;	/* SEXTF_*; none is supported */
	ULONG io_Baud;
	ULONG io_BrkTime;	/* SDCMD_BREAK length, microseconds */
	struct IOTArray io_TermArray;
	UBYTE io_ReadLen;	/* bits per received character, 5-8 */
	UBYTE io_WriteLen;	/* bits per sent character, 5-8 */
	UBYTE io_StopBits;	/* 1 or 2 */
	UBYTE io_SerFlags;	/* SERF_* */
	UWORD io_Status;	/* SDCMD_QUERY: SERST_* */
};

/* io_SerFlags. */
#define SERF_XDISABLED	0x80	/* always reported: no XON/XOFF */
#define SERF_EOFMODE	0x40	/* reads end at an io_TermArray byte */
#define SERF_SHARED	0x20	/* OpenDevice: allow other opens */
#define SERF_RAD_BOOGIE	0x10	/* ignored */
#define SERF_QUEUEDBRK	0x08	/* ignored: breaks are always queued */
#define SERF_7WIRE	0x04	/* unsupported: SerErr_InvParam */
#define SERF_PARTY_ODD	0x02
#define SERF_PARTY_ON	0x01

/* io_ExtFlags: mark/space parity, which the SCC lacks. */
#define SEXTF_MSPON	0x01
#define SEXTF_MARK	0x02

/*
 * io_Status after SDCMD_QUERY.  Amiga's bit positions, but every bit is
 * active high (Amiga's modem bits are active low).  The latched bits stay
 * set until CMD_CLEAR.
 */
#define SERST_CTS	0x0010
#define SERST_CD	0x0020
#define SERST_RTS	0x0040
#define SERST_DTR	0x0080
#define SERST_OVERRUN	0x0100	/* latched: received bytes were lost */
#define SERST_WROTEBRK	0x0200	/* a break is being sent */
#define SERST_READBRK	0x0400	/* latched: a break was received */

/* io_Error values beyond IOERR_*, as Amiga. */
#define SerErr_DevBusy		1
#define SerErr_BaudMismatch	2
#define SerErr_BufErr		4
#define SerErr_InvParam		5
#define SerErr_LineErr		6
#define SerErr_ParityErr	9
#define SerErr_BufOverflow	12

#define SER_DEFBAUD	9600L
#define SER_DEFBUF	512L
#define SER_MINBUF	64L
#define SER_MAXBUF	32768L
#define SER_DEFBRK	250000L

#endif
