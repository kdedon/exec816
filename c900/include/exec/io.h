/* SPDX-License-Identifier: MIT */
/*
 * Device I/O requests, as Amiga <exec/io.h>.  struct IORequest and
 * struct IOStdReq, the CMD_ commands and IOF_QUICK are generated from
 * upstream abi/io.json; the IOERR_ errors are in <exec/errors.h>.  The
 * calls are in <exec/exec.h>; docs/internals.md describes the request
 * life cycle.
 */
#ifndef EXEC_IO_H
#define EXEC_IO_H

#include <exec/ports.h>
#include <exec/abi/io.h>

#define IOB_QUICK	0

#define DEV_BEGINIO	(-30)
#define DEV_ABORTIO	(-36)

#endif
