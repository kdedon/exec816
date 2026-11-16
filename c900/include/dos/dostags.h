/* SPDX-License-Identifier: MIT */
/* AmigaDOS tags (dos/dostags.h, V36+): CreateNewProc and SystemTagList. */
#ifndef DOS_DOSTAGS_H
#define DOS_DOSTAGS_H

#include <utility/tagitem.h>

#define SYS_Dummy	(TAG_USER + 32)
#define SYS_Input	(SYS_Dummy + 1)
#define SYS_Output	(SYS_Dummy + 2)
#define SYS_Asynch	(SYS_Dummy + 3)
#define SYS_UserShell	(SYS_Dummy + 4)
#define SYS_CustomShell	(SYS_Dummy + 5)

#define NP_Dummy	(TAG_USER + 1000)
#define NP_Seglist	(NP_Dummy + 1)
#define NP_FreeSeglist	(NP_Dummy + 2)
#define NP_Entry	(NP_Dummy + 3)
#define NP_Input	(NP_Dummy + 4)
#define NP_Output	(NP_Dummy + 5)
#define NP_CloseInput	(NP_Dummy + 6)
#define NP_CloseOutput	(NP_Dummy + 7)
#define NP_Error	(NP_Dummy + 8)
#define NP_CloseError	(NP_Dummy + 9)
#define NP_CurrentDir	(NP_Dummy + 10)
#define NP_StackSize	(NP_Dummy + 11)
#define NP_Name		(NP_Dummy + 12)
#define NP_Priority	(NP_Dummy + 13)
#define NP_ConsoleTask	(NP_Dummy + 14)
#define NP_WindowPtr	(NP_Dummy + 15)
#define NP_HomeDir	(NP_Dummy + 16)
#define NP_CopyVars	(NP_Dummy + 17)
#define NP_Cli		(NP_Dummy + 18)
#define NP_Path		(NP_Dummy + 19)
#define NP_CommandName	(NP_Dummy + 20)
#define NP_Arguments	(NP_Dummy + 21)
#define NP_NotifyOnDeath (NP_Dummy + 22)
#define NP_Synchronous	(NP_Dummy + 23)
#define NP_ExitCode	(NP_Dummy + 24)
#define NP_ExitData	(NP_Dummy + 25)

#endif
