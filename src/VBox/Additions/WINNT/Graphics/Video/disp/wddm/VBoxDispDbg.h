/* $Id$ */

/** @file
 * VBoxVideo Display D3D User mode dll
 */

/*
 * Copyright (C) 2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___VBoxDispDbg_h__
#define ___VBoxDispDbg_h__

#ifdef DEBUG
/* debugging configuration flags */

/* generic debugging facilities & extra data checks */
# define VBOXWDDMDISP_DEBUG
# if defined(DEBUG_misha) || defined(DEBUG_leo)
/* for some reason when debugging with VirtualKD, user-mode DbgPrint's are discarded
 * the workaround so far is to pass the log info to the kernel driver and DbgPrint'ed from there,
 * which is enabled by this define */
#  define VBOXWDDMDISP_DEBUG_PRINTDRV
/* use OutputDebugString */
#  define VBOXWDDMDISP_DEBUG_PRINT
/* adds vectored exception handler to be able to catch non-debug UM exceptions in kernel debugger */
#  define VBOXWDDMDISP_DEBUG_VEHANDLER
# endif
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

#ifdef VBOXWDDMDISP_DEBUG_VEHANDLER
void vboxVDbgVEHandlerRegister();
void vboxVDbgVEHandlerUnregister();
#endif

#ifdef VBOXWDDMDISP_DEBUG_PRINTDRV
# define DbgPrintDrv(_m) do { vboxDispLogDrvF _m; } while (0)
# define DbgPrintDrvRel(_m) do { vboxDispLogDrvF _m; } while (0)
# define DbgPrintDrvFlow(_m) do { } while (0)
#else
# define DbgPrintDrv(_m) do { } while (0)
# define DbgPrintDrvRel(_m) do { } while (0)
# define DbgPrintDrvFlow(_m) do { } while (0)
#endif

#ifdef VBOXWDDMDISP_DEBUG_PRINT
# define DbgPrintUsr(_m) do { vboxDispLogDbgPrintF _m; } while (0)
# define DbgPrintUsrRel(_m) do { vboxDispLogDbgPrintF _m; } while (0)
# define DbgPrintUsrFlow(_m) do { } while (0)
#else
# define DbgPrintUsr(_m) do { } while (0)
# define DbgPrintUsrRel(_m) do { } while (0)
# define DbgPrintUsrFlow(_m) do { } while (0)
#endif
#define vboxVDbgPrint(_m) do { \
        Log(_m); \
        DbgPrintUsr(_m); \
        DbgPrintDrv(_m); \
    } while (0)
#define vboxVDbgPrintF(_m)  do { \
        LogFlow(_m); \
        DbgPrintUsrFlow(_m); \
        DbgPrintDrvFlow(_m); \
    } while (0)
#define vboxVDbgPrintR(_m)  do { \
        LogRel(_m); \
        DbgPrintUsrRel(_m); \
        DbgPrintDrvRel(_m); \
    } while (0)

#ifdef VBOXWDDMDISP_DEBUG
extern bool g_bVBoxVDbgFDumpSetTexture;
extern bool g_bVBoxVDbgFDumpDrawPrim;
extern bool g_bVBoxVDbgFDumpTexBlt;
extern bool g_bVBoxVDbgFDumpBlt;

void vboxDispLogDrvF(char * szString, ...);
void vboxDispLogDrv(char * szString);
void vboxDispLogDbgPrintF(char * szString, ...);

typedef struct VBOXWDDMDISP_ALLOCATION *PVBOXWDDMDISP_ALLOCATION;
typedef struct VBOXWDDMDISP_RESOURCE *PVBOXWDDMDISP_RESOURCE;

VOID vboxVDbgDoDumpSurfRectByAlloc(const char * pPrefix, PVBOXWDDMDISP_ALLOCATION pAlloc, const RECT *pRect, const char* pSuffix);
VOID vboxVDbgDoDumpAllocRect(const char * pPrefix, PVBOXWDDMDISP_ALLOCATION pAlloc, const RECT *pRect, const char* pSuffix);
VOID vboxVDbgDoDumpSurfRectByRc(const char * pPrefix, const PVBOXWDDMDISP_RESOURCE pRc, uint32_t iAlloc, const RECT *pRect, const char* pSuffix);
VOID vboxVDbgDoDumpSurfRect(const char * pPrefix, IDirect3DSurface9 *pSurf, const RECT *pRect, const char * pSuffix, bool bBreak);
VOID vboxVDbgDoDumpSurf(const char * pPrefix, IDirect3DSurface9 *pSurf, const char * pSuffix);
VOID vboxVDbgDoDumpRcRect(const char * pPrefix, IDirect3DResource9 *pRc, const RECT *pRect, const char * pSuffix);
VOID vboxVDbgDoDumpRcRectByRc(const char * pPrefix, const PVBOXWDDMDISP_RESOURCE pRc, const RECT *pRect, const char* pSuffix);
VOID vboxVDbgDoDumpTex(const char * pPrefix, IDirect3DBaseTexture9 *pTexBase, const char * pSuffix);
VOID vboxVDbgDoDumpRt(const char * pPrefix, IDirect3DDevice9 *pDevice, const char * pSuffix);

void vboxVDbgDoPrintRect(const char * pPrefix, const RECT *pRect, const char * pSuffix);
void vboxVDbgDoPrintAlloc(const char * pPrefix, const PVBOXWDDMDISP_RESOURCE pRc, uint32_t iAlloc, const char * pSuffix);
VOID vboxVDbgDoDumpRtData(char * pPrefix, IDirect3DDevice9 *pDevice, char * pSuffix);

#define VBOXVDBG_DUMP_DRAWPRIM_ENTER(_pDevice) do { \
        if (g_bVBoxVDbgFDumpDrawPrim) \
        { \
            vboxVDbgDoDumpRt("==>"__FUNCTION__": RenderTarget Dump\n", (_pDevice), "\n"); \
        }\
    } while (0)

#define VBOXVDBG_DUMP_DRAWPRIM_LEAVE(_pDevice) do { \
        if (g_bVBoxVDbgFDumpDrawPrim) \
        { \
            vboxVDbgDoDumpRt("<=="__FUNCTION__": RenderTarget Dump\n", (_pDevice), "\n"); \
        }\
    } while (0)

#define VBOXVDBG_DUMP_SETTEXTURE(_pRc) do { \
        if (g_bVBoxVDbgFDumpSetTexture) \
        { \
            vboxVDbgDoDumpRcRectByRc("== "__FUNCTION__": Texture Dump\n", _pRc, NULL, "\n"); \
        } \
    } while (0)

#define VBOXVDBG_DUMP_TEXBLT_ENTER(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { \
        if (g_bVBoxVDbgFDumpTexBlt) \
        { \
            RECT _DstRect; \
            vboxWddmRectMoved(&_DstRect, (_pSrcRect), (_pDstPoint)->x, (_pDstPoint)->y); \
            vboxVDbgDoDumpRcRectByRc("==>"__FUNCTION__" Src:\n", (_pSrcRc), (_pSrcRect), "\n"); \
            vboxVDbgDoDumpRcRectByRc("==>"__FUNCTION__" Dst:\n", (_pDstRc), &_DstRect, "\n"); \
        } \
    } while (0)

#define VBOXVDBG_DUMP_TEXBLT_LEAVE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { \
        if (g_bVBoxVDbgFDumpTexBlt) \
        { \
            RECT _DstRect; \
            vboxWddmRectMoved(&_DstRect, (_pSrcRect), (_pDstPoint)->x, (_pDstPoint)->y); \
            vboxVDbgDoDumpRcRectByRc("<=="__FUNCTION__" Src:\n", (_pSrcRc), (_pSrcRect), "\n"); \
            vboxVDbgDoDumpRcRectByRc("<=="__FUNCTION__" Dst:\n", (_pDstRc), &_DstRect, "\n"); \
        } \
    } while (0)

#define VBOXVDBG_DUMP_BLT_ENTER(_pSrcSurf, _pSrcRect, _pDstSurf, _pDstRect) do { \
        if (g_bVBoxVDbgFDumpBlt) \
        { \
            vboxVDbgDoDumpSurfRect("==>"__FUNCTION__" Src:\n", (_pSrcSurf), (_pSrcRect), "\n", true); \
            vboxVDbgDoDumpSurfRect("==>"__FUNCTION__" Dst:\n", (_pDstSurf), (_pDstRect), "\n", true); \
        } \
    } while (0)

#define VBOXVDBG_DUMP_BLT_LEAVE(_pSrcSurf, _pSrcRect, _pDstSurf, _pDstRect) do { \
        if (g_bVBoxVDbgFDumpBlt) \
        { \
            vboxVDbgDoDumpSurfRect("<=="__FUNCTION__" Src:\n", (_pSrcSurf), (_pSrcRect), "\n", true); \
            vboxVDbgDoDumpSurfRect("<=="__FUNCTION__" Dst:\n", (_pDstSurf), (_pDstRect), "\n", true); \
        } \
    } while (0)
#else
#define VBOXVDBG_DUMP_DRAWPRIM_ENTER(_pDevice) do { } while (0)
#define VBOXVDBG_DUMP_DRAWPRIM_LEAVE(_pDevice) do { } while (0)
#define VBOXVDBG_DUMP_SETTEXTURE(_pRc) do { } while (0)
#define VBOXVDBG_DUMP_TEXBLT_ENTER(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { } while (0)
#define VBOXVDBG_DUMP_TEXBLT_LEAVE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { } while (0)
#define VBOXVDBG_DUMP_BLT_ENTER(_pSrcSurf, _pSrcRect, _pDstSurf, _pDstRect) do { } while (0)
#define VBOXVDBG_DUMP_BLT_LEAVE(_pSrcSurf, _pSrcRect, _pDstSurf, _pDstRect) do { } while (0)
#endif


#endif /* #ifndef ___VBoxDispDbg_h__ */
