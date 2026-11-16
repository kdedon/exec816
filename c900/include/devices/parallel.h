/* SPDX-License-Identifier: MIT */
/*
 * parallel.device: the Centronics printer port, in the classic Amiga
 * parallel.device model.  One unit, 0, output only: data on CIO U70 port
 * B, BUSY on U70 PC0, /STROBE on CIO U66 PC3 and /ACK on U66 PB1.
 * docs/internals.md gives the semantics and the departures from Amiga.
 */
#ifndef DEVICES_PARALLEL_H
#define DEVICES_PARALLEL_H

#include <exec/io.h>

#define PARALLELNAME	"parallel.device"

/* Commands besides CMD_RESET, CMD_WRITE, CMD_CLEAR, CMD_FLUSH. */
#define PDCMD_QUERY	(CMD_NONSTD + 0)
#define PDCMD_SETPARAMS	(CMD_NONSTD + 1)

/* End-of-file characters for PARF_EOFMODE: a write ends after sending
 * any of the eight bytes.  Fill unused bytes with a repeat of one used. */
struct IOPArray {
	ULONG PTermArray0;
	ULONG PTermArray1;
};

struct IOExtPar {
	struct IOStdReq IOPar;
	ULONG io_PExtFlags;	/* none is defined; must be 0 */
	UBYTE io_Status;	/* PDCMD_QUERY: IOPTF_* */
	UBYTE io_ParFlags;	/* PARF_* */
	struct IOPArray io_PTermArray;
	ULONG io_Timeout;	/* C900: longest wait for one byte, microseconds */
};

/* io_ParFlags. */
#define PARF_SHARED	0x20	/* OpenDevice: allow other opens */
#define PARF_SLOWMODE	0x10	/* ignored */
#define PARF_FASTMODE	0x08	/* ignored */
#define PARF_ACKMODE	0x04	/* ignored: /ACK is always used */
#define PARF_EOFMODE	0x02	/* writes end at an io_PTermArray byte */

/* io_Status after PDCMD_QUERY.  Only BUSY is wired. */
#define IOPTF_RWDIR	0x08	/* always 0: write */
#define IOPTF_PARSEL	0x04	/* never set: SELECT is not wired */
#define IOPTF_PAPEROUT	0x02	/* never set: PE is not wired */
#define IOPTF_PARBUSY	0x01	/* BUSY is high (also: no printer) */

/* io_Error values beyond IOERR_*, as Amiga. */
#define ParErr_DevBusy		1
#define ParErr_BufTooBig	2
#define ParErr_InvParam		3
#define ParErr_LineErr		4	/* the printer did not take a byte in io_Timeout */
#define ParErr_NotOpen		5
#define ParErr_PortReset	6
#define ParErr_InitErr		7

#define PAR_DEFTIMEOUT	10000000L	/* 10 s */
#define PAR_MAXTIMEOUT	600000000L	/* 10 min */

#endif
