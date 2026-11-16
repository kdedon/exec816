/* SPDX-License-Identifier: MIT */
/*
 * framebuf.resource: the console card's bitmap, described and lent to
 * one owner at a time.  textcon.device owns it by default.  Task context
 * only; no call waits.  Set FrameBufBase = OpenResource(FRAMEBUFNAME)
 * first; NULL means no card with a bitmap.
 *
 * AllocFB(name): NULL when the caller now owns the bitmap and the card's
 * control port, else the current owner's name.  name must stay valid
 * while owned.  FreeFB(): the owner gives it back; textcon.device clears
 * the screen and redraws the console text printed meanwhile.
 */
#ifndef RESOURCES_FRAMEBUF_H
#define RESOURCES_FRAMEBUF_H

#include <exec/types.h>

#define FRAMEBUFNAME	"framebuf.resource"

#define FBC_HR		2	/* fbi_Card */
#define FBC_LC		3

#define FBF_MONO	1	/* 1 bit a pixel, MSB leftmost, 1 = lit */
#define FBF_IRGB	2	/* 4 bits a pixel, high nibble leftmost,
				 * nibble bits 3-0 = I R G B */

struct FBMode {
	UWORD fbm_Width, fbm_Height;	/* HR: 1024x800, 1024x1024, 640x400 */
	UWORD fbm_Depth;		/* 1, 1, 4 */
	UWORD fbm_BytesPerRow;		/* 128, 128, 320 */
	UWORD fbm_Format;		/* FBF_ */
	UWORD fbm_SplitRow;		/* row crossing the 64 KB boundary,
					 * 0xFFFF: none (HR mono) */
	UWORD fbm_SplitByte;		/* bytes of that row in half 0 */
	APTR fbm_Half[2];		/* logical bases of bytes 0-0xFFFF and
					 * 0x10000-0x1FFFF; NULL: not CPU-mapped */
	ULONG fbm_Size;			/* bitmap memory: 0x20000 on HR */
};

struct FBInfo {			/* read-only, static for the boot */
	UWORD fbi_Card;			/* FBC_ */
	UWORD fbi_NumModes;		/* entries of fbi_Modes */
	struct FBMode *fbi_Modes;
	UWORD fbi_Mode;			/* current entry */
};

/* byte offset o of the bitmap -> address */
#define FB_ADDR(m, o)	((UBYTE *)(m)->fbm_Half[(UWORD)((o) >> 16)] + \
			    (UWORD)(o))
/* bytes of row y before the boundary: fbm_BytesPerRow unless y crosses */
#define FB_ROWSPLIT(m, y) ((y) == (m)->fbm_SplitRow ? \
			    (m)->fbm_SplitByte : (m)->fbm_BytesPerRow)

/*
 * HR control port, the owner's while it owns the bitmap: a byte write
 * (outb) of FBHR_BLANK, FBHR_DIM or FBHR_FULL, which keep bits 1:0 set
 * as textcon.device writes them.  Bit 2 enables mono video, bit 3 is
 * full (1) or 80 % (0) mono intensity; neither affects colour.  Any I/O
 * write to an even port of the card's block loads the same latch, so
 * never write the mouse ports 0x0400 and 0x0402.
 */
#define FBHR_CTL	0x0404
#define FBHR_BLANK	0x03
#define FBHR_DIM	0x07
#define FBHR_FULL	0x0F

extern APTR FrameBufBase;

/* GetFBInfo(), AllocFB(name), FreeFB() */
#include <lvo/framebuf_lib.h>

#endif
