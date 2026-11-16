/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The console's terminal emulator: one screen of cells on a struct
 * Display, driven by an ECMA-48 subset (docs/internals.md, console).
 */
#ifndef VT_H
#define VT_H

#include "display.h"

#define VT_NPAR		4

struct VT {
	struct Display *vt_Disp;
	void (*vt_Reply)();	/* (vt, s): report text, or NULL */
	void (*vt_Bell)();	/* (): BEL, or NULL */
	UWORD vt_Rows;
	UWORD vt_Cols;
	UWORD vt_Row;
	UWORD vt_Col;
	UWORD vt_Par[VT_NPAR];
	UBYTE vt_NPar;
	UBYTE vt_State;
	UBYTE vt_Attr;		/* DA_ bits for new cells */
	UBYTE vt_Flags;
	UBYTE vt_Inter;		/* intermediate byte, or 0 */
	UBYTE vt_Priv;		/* '?' private parameter marker, or 0 */
	UWORD vt_SRow;		/* saved cursor */
	UWORD vt_SCol;
};

/* vt_Flags. */
#define VTF_WRAP	0x01	/* the cursor sits past the last column */
#define VTF_AUTOWRAP	0x02	/* wrap at the right margin (default) */
#define VTF_LNM		0x04	/* LF also returns (default, as Amiga) */
#define VTF_CURSOR	0x08	/* cursor visible (default) */
#define VTF_SHOWN	0x10	/* the backend is showing it now */

void vt_init();
void vt_write();
void vt_putc();

#endif
