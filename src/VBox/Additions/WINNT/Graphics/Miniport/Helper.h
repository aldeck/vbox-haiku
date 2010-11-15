/** @file
 *
 * VBoxGuest -- VirtualBox Win 2000/XP guest video driver
 *
 * Copyright (C) 2006-2007 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __HELPER_h__
#define __HELPER_h__

// Windows version identifier
typedef enum
{
    UNKNOWN_WINVERSION = 0,
    WINNT4    = 1,
    WIN2K     = 2,
    WINXP     = 3,
    WINVISTA  = 4,
    WIN7      = 5
} winVersion_t;


extern "C"
{
bool vboxQueryDisplayRequest(uint32_t *xres, uint32_t *yres, uint32_t *bpp, uint32_t *pDisplayId);
bool vboxLikesVideoMode(uint32_t display, uint32_t width, uint32_t height, uint32_t bpp);
uint32_t vboxGetHeightReduction();
bool vboxQueryPointerPos(uint16_t *pointerXPos, uint16_t *pointerYPos);
bool vboxQueryHostWantsAbsolute();
winVersion_t vboxQueryWinVersion();

// #include "vboxioctl.h"

// int vboxVbvaEnable (ULONG ulEnable, VBVAENABLERESULT *pVbvaResult);
}


/* debug printf */
/** @todo replace this with normal IPRT guest logging */
#ifdef RT_OS_WINDOWS
# define OSDBGPRINT(a) DbgPrint a
#else
# define OSDBGPRINT(a) do { } while(0)
#endif

#ifdef LOG_TO_BACKDOOR
# include <VBox/log.h>
# define dDoPrintf(a) RTLogBackdoorPrintf a
#else
# define dDoPrintf(a) OSDBGPRINT(a)
#endif

/* release log */
# define drprintf dDoPrintf
/* flow log */
# define dfprintf(a) do {} while (0)
/* basic debug log */
#if (defined(DEBUG) && !defined(NO_LOGGING)) || defined(LOG_ENABLED)
# define dprintf dDoPrintf
#else
# define dprintf(a) do {} while (0)
#endif


/* dprintf2 - extended logging. */
#if 0
# define dprintf2 dprintf
#else
# define dprintf2(a) do { } while (0)
#endif

#ifdef DEBUG_misha
/* specifies whether the vboxVDbgBreakF should break in the debugger
 * windbg seems to have some issues when there is a lot ( >~50) of sw breakpoints defined
 * to simplify things we just insert breaks for the case of intensive debugging WDDM driver*/
extern bool g_bVBoxVDbgBreakF;
extern bool g_bVBoxVDbgBreakFv;
#define vboxVDbgBreakF() do { if (g_bVBoxVDbgBreakF) AssertBreakpoint(); } while (0)
#define vboxVDbgBreakFv() do { if (g_bVBoxVDbgBreakFv) AssertBreakpoint(); } while (0)
#else
#define vboxVDbgBreakF() do { } while (0)
#define vboxVDbgBreakFv() do { } while (0)
#endif

#endif // __HELPER_h__
