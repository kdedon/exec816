/* SPDX-License-Identifier: MIT */
/*
 * Alert numbers, as Amiga <exec/alerts.h> (NDK 3.9): deadend bit, 7-bit
 * subsystem, 8-bit general error and 16-bit specific error.  Alert() is
 * in <exec/exec.h>.  The 68000 ACPU_ codes are left out: the Z8001 traps
 * are reported by the trap handler, not as alerts.
 */
#ifndef EXEC_ALERTS_H
#define EXEC_ALERTS_H

#include <exec/types.h>

/* Alert types. */
#define AT_DeadEnd	0x80000000L	/* the system stops */
#define AT_Recovery	0x00000000L	/* Alert returns */

/* General errors, ORed with a subsystem and an object. */
#define AG_NoMemory	0x00010000L
#define AG_MakeLib	0x00020000L
#define AG_OpenLib	0x00030000L
#define AG_OpenDev	0x00040000L
#define AG_OpenRes	0x00050000L
#define AG_IOError	0x00060000L
#define AG_NoSignal	0x00070000L
#define AG_BadParm	0x00080000L
#define AG_CloseLib	0x00090000L	/* usually too many closes */
#define AG_CloseDev	0x000A0000L	/* or a mismatched close */
#define AG_ProcCreate	0x000B0000L	/* process creation failed */

/* Alert objects: what a general error was about. */
#define AO_ExecLib	0x00008001L
#define AO_GraphicsLib	0x00008002L
#define AO_LayersLib	0x00008003L
#define AO_Intuition	0x00008004L
#define AO_MathLib	0x00008005L
#define AO_DOSLib	0x00008007L
#define AO_RAMLib	0x00008008L
#define AO_IconLib	0x00008009L
#define AO_ExpansionLib	0x0000800AL
#define AO_DiskfontLib	0x0000800BL
#define AO_UtilityLib	0x0000800CL
#define AO_KeyMapLib	0x0000800DL

#define AO_AudioDev	0x00008010L
#define AO_ConsoleDev	0x00008011L
#define AO_GamePortDev	0x00008012L
#define AO_KeyboardDev	0x00008013L
#define AO_TrackDiskDev	0x00008014L
#define AO_TimerDev	0x00008015L

#define AO_CIARsrc	0x00008020L
#define AO_DiskRsrc	0x00008021L
#define AO_MiscRsrc	0x00008022L

#define AO_BootStrap	0x00008030L
#define AO_Workbench	0x00008031L
#define AO_DiskCopy	0x00008032L
#define AO_GadTools	0x00008033L
#define AO_Unknown	0x00008035L

/* exec.library */
#define AN_ExecLib	0x01000000L
#define AN_ExcptVect	0x01000001L	/* exception vector checksum (obs.) */
#define AN_BaseChkSum	0x01000002L	/* ExecBase checksum (obs.) */
#define AN_LibChkSum	0x01000003L	/* a library's vectors changed */
#define AN_MemCorrupt	0x81000005L	/* corrupt memory list in FreeMem */
#define AN_IntrMem	0x81000006L	/* no memory for interrupt servers */
#define AN_InitAPtr	0x01000007L	/* InitStruct() of an APTR (obs.) */
#define AN_SemCorrupt	0x01000008L	/* semaphore released by a non-holder */
#define AN_FreeTwice	0x01000009L	/* freeing memory already freed */
#define AN_BogusExcpt	0x8100000AL	/* illegal exception taken (obs.) */
#define AN_IOUsedTwice	0x0100000BL	/* reuse of an active IORequest */
#define AN_MemoryInsane	0x0100000CL	/* memory list check failed */
#define AN_IOAfterClose	0x0100000DL	/* I/O on a closed IORequest */
#define AN_StackProbe	0x0100000EL	/* a Task's stack is out of bounds */
#define AN_BadFreeAddr	0x0100000FL	/* FreeMem: no memory header */
#define AN_BadSemaphore	0x01000010L	/* old message semaphore used */
#define AN_BadLVO	0x01008000L	/* C900: unimplemented exec.library
					 * vector; low 15 bits: -LVO */

/* graphics.library */
#define AN_GraphicsLib	0x02000000L
#define AN_GfxNoMem	0x82010000L	/* graphics out of memory */
#define AN_GfxNoMemMspc	0x82010001L	/* MonitorSpec alloc, no memory */
#define AN_LongFrame	0x82010006L	/* long frame, no memory */
#define AN_ShortFrame	0x82010007L	/* short frame, no memory */
#define AN_TextTmpRas	0x02010009L	/* text, no memory for TmpRas */
#define AN_BltBitMap	0x8201000AL	/* BltBitMap, no memory */
#define AN_RegionMemory	0x8201000BL	/* regions, memory not available */
#define AN_MakeVPort	0x82010030L	/* MakeVPort, no memory */
#define AN_GfxNewError	0x0200000CL
#define AN_GfxFreeError	0x0200000DL
#define AN_GfxNoLCM	0x82011234L	/* emergency memory not available */
#define AN_ObsoleteFont	0x02000401L	/* unsupported font description */

/* layers.library */
#define AN_LayersLib	0x03000000L
#define AN_LayersNoMem	0x83010000L	/* layers out of memory */

/* intuition.library */
#define AN_Intuition	0x04000000L
#define AN_GadgetType	0x84000001L	/* unknown gadget type */
#define AN_BadGadget	0x04000001L	/* recovery form of AN_GadgetType */
#define AN_CreatePort	0x84010002L	/* create port, no memory */
#define AN_ItemAlloc	0x04010003L	/* item plane alloc, no memory */
#define AN_SubAlloc	0x04010004L	/* sub alloc, no memory */
#define AN_PlaneAlloc	0x84010005L	/* plane alloc, no memory */
#define AN_ItemBoxTop	0x84000006L	/* item box top < RelZero */
#define AN_OpenScreen	0x84010007L	/* open screen, no memory */
#define AN_OpenScrnRast	0x84010008L	/* open screen, raster, no memory */
#define AN_SysScrnType	0x84000009L	/* open sys screen, unknown type */
#define AN_AddSWGadget	0x8401000AL	/* add SW gadgets, no memory */
#define AN_OpenWindow	0x8401000BL	/* open window, no memory */
#define AN_BadState	0x8400000CL	/* bad state entering Intuition */
#define AN_BadMessage	0x8400000DL	/* bad message received by IDCMP */
#define AN_WeirdEcho	0x8400000EL	/* weird echo */
#define AN_NoConsole	0x8400000FL	/* console.device would not open */
#define AN_NoISem	0x04000010L	/* a semaphore was not obtained */
#define AN_ISemOrder	0x04000011L	/* semaphores obtained out of order */

/* math.library */
#define AN_MathLib	0x05000000L

/* dos.library */
#define AN_DOSLib	0x07000000L
#define AN_StartMem	0x07010001L	/* no memory at startup */
#define AN_EndTask	0x07000002L	/* EndTask didn't */
#define AN_QPktFail	0x07000003L	/* QPkt failure */
#define AN_AsyncPkt	0x07000004L	/* unexpected packet received */
#define AN_FreeVec	0x07000005L	/* FreeVec failed */
#define AN_DiskBlkSeq	0x07000006L	/* disk block sequence error */
#define AN_BitMap	0x07000007L	/* bitmap corrupt */
#define AN_KeyFree	0x07000008L	/* key already free */
#define AN_BadChkSum	0x07000009L	/* invalid checksum */
#define AN_DiskError	0x0700000AL	/* disk error */
#define AN_KeyRange	0x0700000BL	/* key out of range */
#define AN_BadOverlay	0x0700000CL	/* bad overlay */
#define AN_BadInitFunc	0x0700000DL	/* invalid shell init packet */
#define AN_FileReclosed	0x0700000EL	/* a file handle closed twice */

/* ramlib */
#define AN_RAMLib	0x08000000L
#define AN_BadSegList	0x08000001L	/* no overlays in library seglists */

/* icon.library */
#define AN_IconLib	0x09000000L

/* expansion.library */
#define AN_ExpansionLib	0x0A000000L
#define AN_BadExpansionFree 0x0A000001L	/* freed free region */

/* diskfont.library */
#define AN_DiskfontLib	0x0B000000L

/* audio.device */
#define AN_AudioDev	0x10000000L

/* console.device */
#define AN_ConsoleDev	0x11000000L
#define AN_NoWindow	0x11000001L	/* initial window would not open */

/* gameport.device */
#define AN_GamePortDev	0x12000000L

/* keyboard.device */
#define AN_KeyboardDev	0x13000000L

/* trackdisk.device */
#define AN_TrackDiskDev	0x14000000L
#define AN_TDCalibSeek	0x14000001L	/* calibrate: seek error */
#define AN_TDDelay	0x14000002L	/* delay: error on timer wait */

/* timer.device */
#define AN_TimerDev	0x15000000L
#define AN_TMBadReq	0x15000001L	/* bad request */
#define AN_TMBadSupply	0x15000002L	/* no 50/60 Hz ticks */

/* cia.resource */
#define AN_CIARsrc	0x20000000L

/* disk.resource */
#define AN_DiskRsrc	0x21000000L
#define AN_DRHasDisk	0x21000001L	/* get unit: already has disk */
#define AN_DRIntNoAct	0x21000002L	/* interrupt: no active unit */

/* misc.resource */
#define AN_MiscRsrc	0x22000000L

/* bootstrap */
#define AN_BootStrap	0x30000000L
#define AN_BootError	0x30000001L	/* boot code returned an error */

/* Workbench */
#define AN_Workbench	0x31000000L
#define AN_NoFonts	0xB1000001L
#define AN_WBBadStartupMsg1 0x31000001L
#define AN_WBBadStartupMsg2 0x31000002L
#define AN_WBBadIOMsg	0x31000003L
#define AN_WBReLayoutToolMenu 0xB1010009L

/* DiskCopy */
#define AN_DiskCopy	0x32000000L

/* gadtools.library */
#define AN_GadTools	0x33000000L

/* utility.library */
#define AN_UtilityLib	0x34000000L

/* For any program that needs it. */
#define AN_Unknown	0x35000000L

#endif
