/** @file
 *
 * VBoxVideo Display D3D User mode dll
 *
 * Copyright (C) 2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifndef ___VBoxDispD3DCmn_h___
#define ___VBoxDispD3DCmn_h___

#include <windows.h>
#include <d3d9types.h>
//#include <d3dtypes.h>
#include <D3dumddi.h>
#include <d3dhal.h>

#include <iprt/initterm.h>
#include <iprt/log.h>
#include <iprt/mem.h>

#include <VBox/Log.h>

#include <VBox/VBoxGuestLib.h>

#include "VBoxDispD3DIf.h"
#include "../../Miniport/wddm/VBoxVideoIf.h"
#include "VBoxDispCm.h"
#ifdef VBOX_WITH_CRHGSMI
#include "VBoxUhgsmiBase.h"
#include "VBoxUhgsmiDisp.h"
#include "VBoxUhgsmiKmt.h"
#endif
#include "VBoxDispD3D.h"

#ifdef DEBUG
# define VBOXWDDMDISP_DEBUG
# define VBOXWDDMDISP_DEBUG_FLOW
# define VBOXWDDMDISP_DEBUG_DUMPSURFDATA
# ifdef DEBUG_misha
//#  define VBOXWDDMDISP_DEBUG_VEHANDLER
# endif
#endif

#if defined(VBOXWDDMDISP_DEBUG) || defined(VBOX_WDDMDISP_WITH_PROFILE) || defined(VBOXWDDM_TEST_UHGSMI)
# define VBOXWDDMDISP_DEBUG_PRINT
#endif

#if 0
# ifdef Assert
#  undef Assert
#  define Assert(_a) do{}while(0)
# endif
# ifdef AssertBreakpoint
#  undef AssertBreakpoint
#  define AssertBreakpoint() do{}while(0)
# endif
# ifdef AssertFailed
#  undef AssertFailed
#  define AssertFailed() do{}while(0)
# endif
#endif

#ifdef VBOXWDDMDISP_DEBUG_PRINT
VOID vboxVDbgDoMpPrintF(const PVBOXWDDMDISP_DEVICE pDevice, LPCSTR szString, ...);
VOID vboxVDbgDoMpPrint(const PVBOXWDDMDISP_DEVICE pDevice, LPCSTR szString);
VOID vboxVDbgDoPrint(LPCSTR szString, ...);
#endif

#ifdef VBOXWDDMDISP_DEBUG_VEHANDLER
void vboxVDbgVEHandlerRegister();
void vboxVDbgVEHandlerUnregister();
#endif

#define vboxVDbgPrint Log
#define vboxVDbgPrintF LogFlow
#define vboxVDbgPrintR LogRel

#ifdef VBOXWDDMDISP_DEBUG
VOID vboxVDbgDoDumpAllocData(const char * pPrefix, PVBOXWDDMDISP_ALLOCATION pAlloc, const RECT *pRect, const char* pSuffix);
VOID vboxVDbgDoDumpAllocSurfData(const char * pPrefix, PVBOXWDDMDISP_ALLOCATION pAlloc, IDirect3DSurface9 *pSurf, const RECT *pRect, const char* pSuffix);
VOID vboxVDbgDoDumpSurfData(const char * pPrefix, const PVBOXWDDMDISP_RESOURCE pRc, uint32_t iAlloc, const RECT *pRect, IDirect3DSurface9 *pSurf, const char* pSuffix);
void vboxVDbgDoPrintRect(const char * pPrefix, const RECT *pRect, const char * pSuffix);
void vboxVDbgDoPrintAlloc(const char * pPrefix, const PVBOXWDDMDISP_RESOURCE pRc, uint32_t iAlloc, const char * pSuffix);
#endif

#if 0
#ifdef VBOXWDDMDISP_DEBUG
extern bool g_VDbgTstDumpEnable;
extern bool g_VDbgTstDumpOnSys2VidSameSizeEnable;

VOID vboxVDbgDoDumpAllocData(const char * pPrefix, PVBOXWDDMDISP_ALLOCATION pAlloc, const RECT *pRect, const char* pSuffix);
VOID vboxVDbgDoDumpAllocSurfData(const char * pPrefix, PVBOXWDDMDISP_ALLOCATION pAlloc, IDirect3DSurface9 *pSurf, const RECT *pRect, const char* pSuffix);
VOID vboxVDbgDoDumpSurfData(const char * pPrefix, const PVBOXWDDMDISP_RESOURCE pRc, uint32_t iAlloc, const RECT *pRect, IDirect3DSurface9 *pSurf, const char* pSuffix);
void vboxVDbgDoMpPrintRect(const char * pPrefix, const RECT *pRect, const char * pSuffix);
void vboxVDbgDoMpPrintAlloc(const char * pPrefix, const PVBOXWDDMDISP_RESOURCE pRc, uint32_t iAlloc, const char * pSuffix);

#define vboxVDbgBreak() AssertBreakpoint()
#define vboxVDbgPrint(_m) \
    do { \
        vboxVDbgDoPrint _m ; \
    } while (0)
#define vboxVDbgPrintR vboxVDbgPrint
#define vboxVDbgMpPrint(_m) \
    do { \
        vboxVDbgDoMpPrint _m ; \
    } while (0)
#define vboxVDbgMpPrintF(_m) \
    do { \
        vboxVDbgDoMpPrintF _m ; \
    } while (0)
#define vboxVDbgMpPrintRect(_m) \
    do { \
        vboxVDbgDoMpPrintRect _m ; \
    } while (0)
#define vboxVDbgMpPrintAlloc(_m) \
    do { \
        vboxVDbgDoMpPrintAlloc _m ; \
    } while (0)
#ifdef VBOXWDDMDISP_DEBUG_DUMPSURFDATA
#define vboxVDbgDumpSurfData(_m) \
    do { \
        vboxVDbgDoDumpSurfData _m ; \
    } while (0)
#define vboxVDbgDumpAllocSurfData(_m) \
    do { \
        vboxVDbgDoDumpAllocSurfData _m ; \
    } while (0)
#define vboxVDbgDumpAllocData(_m) \
    do { \
        vboxVDbgDoDumpAllocData _m ; \
    } while (0)
#else
#define vboxVDbgDumpSurfData(_m) do {} while (0)
#endif
#ifdef VBOXWDDMDISP_DEBUG_FLOW
# define vboxVDbgPrintF  vboxVDbgPrint
#else
# define vboxVDbgPrintF(_m)  do {} while (0)
#endif
#else
#define vboxVDbgMpPrint(_m) do {} while (0)
#define vboxVDbgMpPrintF(_m) do {} while (0)
#define vboxVDbgMpPrintRect(_m) do {} while (0)
#define vboxVDbgMpPrintAlloc(_m) do {} while (0)
#define vboxVDbgDumpSurfData(_m) do {} while (0)
#define vboxVDbgDumpAllocSurfData(_m) do {} while (0)
#define vboxVDbgDumpAllocData(_m) do {} while (0)
#define vboxVDbgBreak() do {} while (0)
#define vboxVDbgPrint(_m)  do {} while (0)
#define vboxVDbgPrintR vboxVDbgPrint
#define vboxVDbgPrintF vboxVDbgPrint
#endif
#endif

# ifdef VBOXWDDMDISP
#  define VBOXWDDMDISP_DECL(_type) DECLEXPORT(_type)
# else
#  define VBOXWDDMDISP_DECL(_type) DECLIMPORT(_type)
# endif

#endif /* #ifndef ___VBoxDispD3DCmn_h___ */
