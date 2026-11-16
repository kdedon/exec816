/* SPDX-License-Identifier: MIT */
/*
 * Shared by the framebuf.resource tests: the resource opened and its
 * description checked against the card textcon.device found.
 */
#include <resources/framebuf.h>
#include "condisp.h"

APTR FrameBufBase;
static struct FBMode *fbm;

/* What the mode must say for textcon.device's card and mode. */
struct fbwant {
	UWORD w_Width, w_Height, w_Depth, w_Bpr, w_Format, w_Row, w_Byte;
	ULONG w_Size;
};

static struct fbwant fbwants[] = {
	{ 1024, 800, 1, 128, FBF_MONO, 0xFFFF, 0, 0x20000L },	/* CDM_HR800 */
	{ 1024, 1024, 1, 128, FBF_MONO, 0xFFFF, 0, 0x20000L },	/* CDM_HR1024 */
	{ 640, 400, 4, 320, FBF_IRGB, 204, 256, 0x20000L },	/* CDM_HRCOLOR */
	{ 640, 200, 1, 80, FBF_MONO, 0xFFFF, 0, 0x10000L }	/* LC */
};

/*
 * Open the resource and check it: NULL when all is right, else what is
 * wrong.  *card gets textcon.device's card; fbm the current mode, or
 * NULL when the resource is (rightly) absent.
 */
static char *
fbopen(card)
int *card;
{
	register struct FBInfo *fi;
	register struct fbwant *w;
	struct ConDisplay *cd;
	int c;

	fbm = NULL;
	cd = conask(0);
	c = cd != NULL ? cd->cd_Card : CDC_NONE;
	*card = c;
	FrameBufBase = OpenResource(FRAMEBUFNAME);
	if (c != CDC_HR && c != CDC_LC)
		return (FrameBufBase != NULL ? "resource without a bitmap" : NULL);
	if (FrameBufBase == NULL)
		return ("no resource with a bitmap card");
	fi = GetFBInfo();
	if (fi == NULL || fi->fbi_Card != c || fi->fbi_NumModes != 1 ||
	    fi->fbi_Mode != 0 || fi->fbi_Modes == NULL)
		return ("FBInfo");
	fbm = &fi->fbi_Modes[fi->fbi_Mode];
	w = &fbwants[c == CDC_LC ? 3 : cd->cd_Mode - CDM_HR800];
	xKPrintf("fb: card %u %ux%u depth %u bpr %u format %u split %u/%u\n",
	    fi->fbi_Card, fbm->fbm_Width, fbm->fbm_Height, fbm->fbm_Depth,
	    fbm->fbm_BytesPerRow, fbm->fbm_Format, fbm->fbm_SplitRow,
	    fbm->fbm_SplitByte);
	if (fbm->fbm_Width != w->w_Width || fbm->fbm_Height != w->w_Height ||
	    fbm->fbm_Depth != w->w_Depth ||
	    fbm->fbm_BytesPerRow != w->w_Bpr ||
	    fbm->fbm_Format != w->w_Format ||
	    fbm->fbm_SplitRow != w->w_Row ||
	    fbm->fbm_SplitByte != w->w_Byte || fbm->fbm_Size != w->w_Size)
		return ("mode fields");
	if (c == CDC_LC)
		return (fbm->fbm_Half[0] != NULL || fbm->fbm_Half[1] != NULL ?
		    "LC bitmap mapped" : NULL);
	if (fbm->fbm_Half[0] != (APTR)((ULONG)HRSEG0 << 24) ||
	    fbm->fbm_Half[1] != (APTR)((ULONG)HRSEG1 << 24))
		return ("HR halves");
	if (FB_ADDR(fbm, 0x10000L) != (UBYTE *)fbm->fbm_Half[1] ||
	    FB_ADDR(fbm, 0xFFFFL) != (UBYTE *)fbm->fbm_Half[0] + 0xFFFF)
		return ("FB_ADDR");
	/* The split row is the one that crosses 64 KB, or none does. */
	if (fbm->fbm_SplitRow == 0xFFFF ?
	    0x10000L % fbm->fbm_BytesPerRow != 0 :
	    (ULONG)fbm->fbm_SplitRow * fbm->fbm_BytesPerRow +
	    fbm->fbm_SplitByte != 0x10000L)
		return ("split row");
	return (NULL);
}
