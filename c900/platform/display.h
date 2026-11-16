/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The display console's two halves.  textcon.device, in the kernel,
 * probes and maps the card kboot names, keeps the boot text and owns
 * console input; a display module loaded from the boot volume draws.
 * The module asks textcon.device for the card (CD_ASKDISPLAY), fills in
 * its entry and attaches (CD_SETDISPLAY); textcon.device then replays
 * the boot text through it and sends it every later console byte.
 *
 * Inside the module the terminal emulator owns the cursor, attributes
 * and escape parsing; a backend (struct Display) only draws cells.  Every
 * backend entry is polled and safe with VI off, so a panic still draws.
 * The emulator hides the cursor (dp_Cursor on = 0) before it changes a
 * cell or the position, and shows it at the new position when its call
 * ends.
 */
#ifndef DISPLAY_H
#define DISPLAY_H

/* Cell attributes the console passes; a backend maps what it can. */
#define DA_NORMAL	0x00
#define DA_INVERSE	0x01
#define DA_BOLD		0x02
#define DA_UNDERLINE	0x04
#define DA_BLINK	0x08

struct Display {
	char *dp_Name;			/* "lr", "hr", "lc" */
	UWORD dp_Cols;
	UWORD dp_Rows;
	UBYTE *dp_Font;			/* glyphs from 0x20, 8x16 (lc at 640x200:
					 * 8x8), or NULL */
	void (*dp_Init)();		/* (): program the card, clear, home */
	void (*dp_Put)();		/* (row, col, c, attr) */
	void (*dp_Scroll)();		/* (top, bot, attr): rows top..bot up one */
	void (*dp_Erase)();		/* (row, col, n, attr): n cells blank */
	void (*dp_Cursor)();		/* (row, col, on) */
	void (*dp_Text)();		/* (row, col, s, n, attr): n cells of
					 * s in the row; NULL: dp_Put each */
};

/* cd_Card: the card textcon.device found and mapped. */
#define CDC_NONE	0
#define CDC_LR		1
#define CDC_HR		2
#define CDC_LC		3

/* cd_Mode for CDC_HR: the JP3 mode. */
#define CDM_HR800	1		/* 1024x800 mono */
#define CDM_HR1024	2		/* 1024x1024 mono */
#define CDM_HRCOLOR	3		/* 640x400 RGBI */

/* cd_Out's how. */
#define CDO_QUEUE	0	/* interrupt level or VI off: queue it */
#define CDO_DRAW	1	/* a Task with VI on: draw under Forbid */
#define CDO_STOP	2	/* the system has stopped: draw at once */

/*
 * textcon.device's, its address from CD_ASKDISPLAY.  textcon.device sets
 * the first four fields at boot; the module sets cd_Out, cd_Disp and
 * cd_Reset before CD_SETDISPLAY.
 */
struct ConDisplay {
	UWORD cd_Card;			/* CDC_ */
	UWORD cd_Mode;			/* CDM_ for HR */
	void (*cd_Reply)();		/* (vt, s): text as console input */
	void (*cd_Bell)();		/* (): a short tone, any context */
	void (*cd_Out)();		/* (p, n, how): n bytes of p, any
					 * context; CDO_ says how */
	struct Display *cd_Disp;	/* the module's backend */
	void (*cd_Reset)();		/* (): reprogram and clear the card,
					 * reset the emulator, empty the
					 * queue; polled, VI-off safe */
};

#endif
