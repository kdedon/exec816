/* SPDX-License-Identifier: MIT */
/*
 * Message ports, as Amiga <exec/ports.h>.  struct MsgPort, struct
 * Message, PF_ACTION and the PA_ actions are generated from the upstream
 * ABI.
 */
#ifndef EXEC_PORTS_H
#define EXEC_PORTS_H

#include <exec/nodes.h>
#include <exec/lists.h>
#include <exec/tasks.h>
#include <exec/abi/ports.h>

#define mp_SoftInt	mp_SigTask	/* PA_SOFTINT: the Interrupt */

#endif
