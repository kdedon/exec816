/* SPDX-License-Identifier: MIT */
/*
 * The toolchain's COHERENT <errno.h> plus ENOSYS, which Exec900's C
 * library sets for a call it does not provide (fork, exec, signals,
 * pipes).  System V's number.
 */
#ifndef C900_ERRNO_H
#define C900_ERRNO_H

#include <include/errno.h>

#define ENOSYS	89		/* Function not implemented */

#endif
