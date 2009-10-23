#ifdef VBOX_WITH_DDRAW

/******************************Module*Header**********************************\
*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
*/
/*
* Based in part on Microsoft DDK sample code
*
*                           **************************
*                           * DirectDraw SAMPLE CODE *
*                           **************************
*
* Module Name: ddenable.c
*
* Content:
*
* Copyright (c) 1994-1998 3Dlabs Inc. Ltd. All rights reserved.
* Copyright (c) 1995-1999 Microsoft Corporation.  All rights reserved.
\*****************************************************************************/

#include "driver.h"
#include "dd.h"
#undef CO_E_NOTINITIALIZED
#include <winerror.h>

#if 0
static DWORD APIENTRY DdCreateSurface(PDD_CREATESURFACEDATA  lpCreateSurface);
#endif

#ifdef VBOX_WITH_VIDEOHWACCEL
#include <iprt/asm.h>

#define VBOXVHWA_CAP(_pdev, _cap) ((_pdev)->vhwaInfo.caps & (_cap))
static bool getDDHALInfo(PPDEV pDev, DD_HALINFO* pHALInfo);
static DECLCALLBACK(void) vboxVHWASurfBltCompletion(PPDEV ppdev, VBOXVHWACMD * pCmd, void * pContext);
static DECLCALLBACK(void) vboxVHWASurfFlipCompletion(PPDEV ppdev, VBOXVHWACMD * pCmd, void * pContext);

//#define DBG_DDSTUBS 1
#endif

/**
 * DrvGetDirectDrawInfo
 *
 * The DrvGetDirectDrawInfo function returns the capabilities of the graphics hardware.
 *
 * Parameters:
 *
 * dhpdev
 *     Handle to the PDEV returned by the driver�s DrvEnablePDEV routine.
 * pHalInfo
 *     Points to a DD_HALINFO structure in which the driver should return the hardware capabilities that it supports.
 * pdwNumHeaps
 *     Points to the location in which the driver should return the number of VIDEOMEMORY structures pointed to by pvmList.
 * pvmList
 *     Points to an array of VIDEOMEMORY structures in which the driver should return information about each display memory chunk that it controls. The driver should ignore this parameter when it is NULL.
 * pdwNumFourCCCodes
 *     Points to the location in which the driver should return the number of DWORDs pointed to by pdwFourCC.
 * pdwFourCC
 *     Points to an array of DWORDs in which the driver should return information about each FOURCC that it supports. The driver should ignore this parameter when it is NULL.
 *
 * Return Value:
 *
 * DrvGetDirectDrawInfo returns TRUE if it succeeds; otherwise, it returns FALSE.
 *
 */

DD_HALINFO g_h2;

BOOL APIENTRY DrvGetDirectDrawInfo(
    DHPDEV        dhpdev,
    DD_HALINFO   *pHalInfo,
    DWORD        *pdwNumHeaps,
    VIDEOMEMORY  *pvmList,
    DWORD        *pdwNumFourCCCodes,
    DWORD        *pdwFourCC
    )
{
    PPDEV pDev = (PPDEV)dhpdev;
    BOOL bDefineDDrawHeap = FALSE;
    DWORD cHeaps = 0;
    VIDEOMEMORY *pVm = NULL;

    DISPDBG((0, "%s: %p, %p, %p, %p, %p. %p\n", __FUNCTION__, dhpdev, pHalInfo, pdwNumHeaps, pvmList, pdwNumFourCCCodes, pdwFourCC));

    *pdwNumFourCCCodes = 0;
    *pdwNumHeaps       = 0;

    /* Setup the HAL driver caps. */
    pHalInfo->dwSize    = sizeof(DD_HALINFO);
#ifndef VBOX_WITH_VIDEOHWACCEL
    pHalInfo->dwFlags   = 0;
#endif

    if (!(pvmList && pdwFourCC))
    {
#ifdef VBOX_WITH_VIDEOHWACCEL
        vboxVHWAInit();

        memset(pHalInfo, 0, sizeof(DD_HALINFO));
        pHalInfo->dwSize    = sizeof(DD_HALINFO);

        vboxVHWAInitHostInfo1(pDev);
#else
        memset(&pHalInfo->ddCaps, 0, sizeof(DDNTCORECAPS));
#endif

        pHalInfo->ddCaps.dwSize         = sizeof(DDNTCORECAPS);
        pHalInfo->ddCaps.dwVidMemTotal  = pDev->layout.cbDDRAWHeap;
        pHalInfo->ddCaps.dwVidMemFree   = pHalInfo->ddCaps.dwVidMemTotal;

        pHalInfo->ddCaps.dwCaps         = 0;
        pHalInfo->ddCaps.dwCaps2        = 0;

        /* Declare we can handle textures wider than the primary */
        pHalInfo->ddCaps.dwCaps2 |= DDCAPS2_WIDESURFACES;

        pHalInfo->ddCaps.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

        /* Create primary surface attributes */
        pHalInfo->vmiData.pvPrimary                 = pDev->pjScreen;
        pHalInfo->vmiData.fpPrimary                 = 0;
        pHalInfo->vmiData.dwDisplayWidth            = pDev->cxScreen;
        pHalInfo->vmiData.dwDisplayHeight           = pDev->cyScreen;
        pHalInfo->vmiData.lDisplayPitch             = pDev->lDeltaScreen;

        pHalInfo->vmiData.ddpfDisplay.dwSize        = sizeof(DDPIXELFORMAT);
        pHalInfo->vmiData.ddpfDisplay.dwFlags       = DDPF_RGB;
        pHalInfo->vmiData.ddpfDisplay.dwRGBBitCount = pDev->ulBitCount;
        DISPDBG((0, "pvPrimary                      %x\n", pHalInfo->vmiData.pvPrimary));
        DISPDBG((0, "fpPrimary                      %x\n", pHalInfo->vmiData.fpPrimary));
        DISPDBG((0, "dwDisplayWidth                 %d\n", pHalInfo->vmiData.dwDisplayWidth));
        DISPDBG((0, "dwDisplayHeight                %d\n", pHalInfo->vmiData.dwDisplayHeight));
        DISPDBG((0, "lDisplayPitch                  %d\n", pHalInfo->vmiData.lDisplayPitch));
        DISPDBG((0, "dwRGBBitCount                  %d\n", pHalInfo->vmiData.ddpfDisplay.dwRGBBitCount));

        if (pDev->ulBitmapType == BMF_8BPP)
        {
            pHalInfo->vmiData.ddpfDisplay.dwFlags |= DDPF_PALETTEINDEXED8;
            DISPDBG((0, "DDPF_PALETTEINDEXED8\n"));
        }

        pHalInfo->vmiData.ddpfDisplay.dwRBitMask    = pDev->flRed;
        pHalInfo->vmiData.ddpfDisplay.dwGBitMask    = pDev->flGreen;
        pHalInfo->vmiData.ddpfDisplay.dwBBitMask    = pDev->flBlue;

        pHalInfo->vmiData.dwOffscreenAlign          = 4;
        pHalInfo->vmiData.dwZBufferAlign            = 4;
        pHalInfo->vmiData.dwTextureAlign            = 4;


#ifdef VBOX_WITH_VIDEOHWACCEL
        if(pDev->vhwaInfo.bVHWAEnabled)
        {
            pHalInfo->vmiData.dwOverlayAlign = 4;

            pDev->vhwaInfo.bVHWAEnabled = getDDHALInfo(pDev, pHalInfo);
        }
#endif
    }

    cHeaps = 0;

    /* Do we have sufficient videomemory to create an off-screen heap for DDraw? */
    if (pDev->layout.cbDDRAWHeap > 0)
    {
        bDefineDDrawHeap = TRUE;
        cHeaps++;
    }

    pDev->cHeaps = cHeaps;
    *pdwNumHeaps  = cHeaps;

    // If pvmList is not NULL then we can go ahead and fill out the VIDEOMEMORY
    // structures which define our requested heaps.

    if(pvmList) {

        pVm=pvmList;

        //
        // Snag a pointer to the video-memory list so that we can use it to
        // call back to DirectDraw to allocate video memory:
        //
        pDev->pvmList = pVm;

        //
        // Define the heap for DirectDraw
        //
        if ( bDefineDDrawHeap )
        {
            pVm->dwFlags        = VIDMEM_ISLINEAR ;
            pVm->fpStart        = pDev->layout.offDDRAWHeap;
            pVm->fpEnd          = pDev->layout.offDDRAWHeap + pDev->layout.cbDDRAWHeap - 1; /* inclusive */
#ifndef VBOX_WITH_VIDEOHWACCEL
            pVm->ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
#endif
            DISPDBG((0, "fpStart %x fpEnd %x\n", pVm->fpStart, pVm->fpEnd));

            pVm++;
        }
    }

#ifdef VBOX_WITH_VIDEOHWACCEL
    if(pDev->vhwaInfo.bVHWAEnabled)
    {
//        // TODO: filter out hw-unsupported fourccs
//#define FOURCC_YUV422  (MAKEFOURCC('Y','U','Y','2'))
//#define FOURCC_YUV411  (MAKEFOURCC('Y','4','1','1'))
//
//        static DWORD fourCC[] =  { FOURCC_YUV422, FOURCC_YUV411 };  // The FourCC's we support

        *pdwNumFourCCCodes = pDev->vhwaInfo.numFourCC;

        if (pdwFourCC && pDev->vhwaInfo.numFourCC)
        {
            int rc = vboxVHWAInitHostInfo2(pDev, pdwFourCC);
            if(RT_FAILURE(rc))
            {
                *pdwNumFourCCCodes = 0;
                pDev->vhwaInfo.numFourCC = 0;
            }
        }
    }
#endif

#ifdef VBOX_WITH_VIDEOHWACCEL
    if(pDev->vhwaInfo.bVHWAEnabled)
    {
        /* we need it to set DDHAL_PRIVATECAP_NOTIFYPRIMARYCREATION to make ddraw call us for primary surface creation */
        /* DX5 and up */
        pHalInfo->GetDriverInfo = DdGetDriverInfo;
        pHalInfo->dwFlags |= DDHALINFO_GETDRIVERINFOSET;
    }
#endif

#if 0
    /* No 3D capabilities */
    if (pHalInfo->lpD3DGlobalDriverData)
    {
        LPD3DHAL_GLOBALDRIVERDATA lpD3DGlobalDriverData = (LPD3DHAL_GLOBALDRIVERDATA)pHalInfo->lpD3DGlobalDriverData;
        lpD3DGlobalDriverData->dwSize = sizeof(D3DHAL_GLOBALDRIVERDATA);
    }
#endif
    return TRUE;
}

/**
 * DrvEnableDirectDraw
 *
 * The DrvEnableDirectDraw function enables hardware for DirectDraw use.
 *
 * Parameters
 *
 * dhpdev
 *     Handle to the PDEV returned by the driver�s DrvEnablePDEV routine.
 * pCallBacks
 *     Points to the DD_CALLBACKS structure to be initialized by the driver.
 * pSurfaceCallBacks
 *     Points to the DD_SURFACECALLBACKS structure to be initialized by the driver.
 * pPaletteCallBacks
 *     Points to the DD_PALETTECALLBACKS structure to be initialized by the driver.
 *
 * Return Value
 *
 * DrvEnableDirectDraw returns TRUE if it succeeds; otherwise, it returns FALSE.
 *
 */
BOOL APIENTRY DrvEnableDirectDraw(
    DHPDEV                  dhpdev,
    DD_CALLBACKS           *pCallBacks,
    DD_SURFACECALLBACKS    *pSurfaceCallBacks,
    DD_PALETTECALLBACKS    *pPaletteCallBacks
    )
{
#ifdef VBOX_WITH_VIDEOHWACCEL
    PPDEV pDev = (PPDEV)dhpdev;

#endif

    DISPDBG((0, "%s: %p, %p, %p, %p\n", __FUNCTION__, dhpdev, pCallBacks, pSurfaceCallBacks, pPaletteCallBacks));

    /* Fill in the HAL Callback pointers */
    pCallBacks->dwSize                = sizeof(DD_CALLBACKS);
    pCallBacks->dwFlags               = 0;

    pCallBacks->dwFlags               = DDHAL_CB32_CREATESURFACE | DDHAL_CB32_CANCREATESURFACE | DDHAL_CB32_MAPMEMORY;
    pCallBacks->CreateSurface         = DdCreateSurface;
    pCallBacks->CanCreateSurface      = DdCanCreateSurface;
    pCallBacks->MapMemory             = DdMapMemory;
    // pCallBacks->WaitForVerticalBlank  = DdWaitForVerticalBlank;
    // pCallBacks->GetScanLine           = DdGetScanLine;
    // DDHAL_CB32_WAITFORVERTICALBLANK | DDHAL_CB32_GETSCANLINE
    /* Note: pCallBacks->SetMode & pCallBacks->DestroyDriver are unused in Windows 2000 and up */

    /* Fill in the Surface Callback pointers */
    pSurfaceCallBacks->dwSize           = sizeof(DD_SURFACECALLBACKS);
    pSurfaceCallBacks->dwFlags          = DDHAL_SURFCB32_LOCK | DDHAL_SURFCB32_UNLOCK;
    pSurfaceCallBacks->Lock             = DdLock;
    pSurfaceCallBacks->Unlock           = DdUnlock;

    /*
    pSurfaceCallBacks->dwFlags          = DDHAL_SURFCB32_DESTROYSURFACE | DDHAL_SURFCB32_LOCK; // DDHAL_SURFCB32_UNLOCK;
    pSurfaceCallBacks->DestroySurface   = DdDestroySurface;
    pSurfaceCallBacks->Flip             = DdFlip;
    pSurfaceCallBacks->GetBltStatus     = DdGetBltStatus;
    pSurfaceCallBacks->GetFlipStatus    = DdGetFlipStatus;
    pSurfaceCallBacks->Blt              = DdBlt;
    DDHAL_SURFCB32_FLIP | DDHAL_SURFCB32_BLT | DDHAL_SURFCB32_GETBLTSTATUS | DDHAL_SURFCB32_GETFLIPSTATUS;
    */

//    pSurfaceCallBacks.SetColorKey = DdSetColorKey;
//    pSurfaceCallBacks.dwFlags |= DDHAL_SURFCB32_SETCOLORKEY;

    /* Fill in the Palette Callback pointers */
    pPaletteCallBacks->dwSize           = sizeof(DD_PALETTECALLBACKS);
    pPaletteCallBacks->dwFlags          = 0;

#ifdef VBOX_WITH_VIDEOHWACCEL
    if(pDev->vhwaInfo.bVHWAEnabled)
    {
        //TODO: filter out those we do not need in case not supported by hw
        pSurfaceCallBacks->DestroySurface = DdDestroySurface;
//        pSurfaceCallBacks->Lock = DdLock;
//        pSurfaceCallBacks->Unlock = DdUnlock;
        pSurfaceCallBacks->GetBltStatus = DdGetBltStatus;
        pSurfaceCallBacks->GetFlipStatus = DdGetFlipStatus;
        pSurfaceCallBacks->SetColorKey = DdSetColorKey;
        pSurfaceCallBacks->Flip = DdFlip;
        pSurfaceCallBacks->Blt = DdBlt;

        pSurfaceCallBacks->dwFlags |= DDHAL_SURFCB32_DESTROYSURFACE     |
                         DDHAL_SURFCB32_FLIP
//                         | DDHAL_SURFCB32_LOCK
                         | DDHAL_SURFCB32_BLT                |
                         DDHAL_SURFCB32_GETBLTSTATUS       |
                         DDHAL_SURFCB32_GETFLIPSTATUS      |
                         DDHAL_SURFCB32_SETCOLORKEY
//                         | DDHAL_SURFCB32_UNLOCK
                         ;

        if(pDev->vhwaInfo.caps & VBOXVHWA_CAPS_OVERLAY)
        {
            pSurfaceCallBacks->UpdateOverlay = DdUpdateOverlay;   // Now supporting overlays.
            pSurfaceCallBacks->SetOverlayPosition = DdSetOverlayPosition;
            pSurfaceCallBacks->dwFlags |=
                             DDHAL_SURFCB32_UPDATEOVERLAY      | // Now supporting
                             DDHAL_SURFCB32_SETOVERLAYPOSITION ; // overlays.
        }
    }
#endif
    return TRUE;
}

/**
 * DrvDisableDirectDraw
 *
 * The DrvDisableDirectDraw function disables hardware for DirectDraw use.
 *
 * Parameters
 *
 * dhpdev
 *     Handle to the PDEV returned by the driver�s DrvEnablePDEV routine.
 *
 */
VOID APIENTRY DrvDisableDirectDraw( DHPDEV dhpdev)
{
    DISPDBG((0, "%s: %p\n", __FUNCTION__, dhpdev));
}

/**
 * DdGetDriverInfo
 *
 * The DdGetDriverInfo function queries the driver for additional DirectDraw and Direct3D functionality that the driver supports.
 *
 * Parameters
 * lpGetDriverInfo
 *     Points to a DD_GETDRIVERINFODATA structure that contains the information required to perform the query.
 *
 * Return Value
 *
 * DdGetDriverInfo must return DDHAL_DRIVER_HANDLED.
 *
 */
DWORD CALLBACK DdGetDriverInfo(DD_GETDRIVERINFODATA *lpData)
{
    PPDEV pDev = (PPDEV)lpData->dhpdev;
    DWORD dwSize;

    DISPDBG((0, "%s: %p\n", __FUNCTION__, lpData->dhpdev));

    /* Default to 'not supported' */
    lpData->ddRVal = DDERR_CURRENTLYNOTAVAIL;

    /* Fill in supported stuff */
    if (IsEqualIID(&lpData->guidInfo, &GUID_D3DCallbacks3))
    {
        DISPDBG((0, " -> GUID_D3DCallbacks3\n"));
    }
    else
    if (IsEqualIID(&lpData->guidInfo, &GUID_D3DExtendedCaps))
    {
        DISPDBG((0, " -> GUID_D3DExtendedCaps\n"));
    }
    else
    if (IsEqualIID(&lpData->guidInfo, &GUID_ZPixelFormats))
    {
        DISPDBG((0, " -> GUID_ZPixelFormats\n"));
    }
    else
    if (IsEqualIID(&(lpData->guidInfo), &GUID_D3DParseUnknownCommandCallback))
    {
        DISPDBG((0, " -> GUID_D3DParseUnknownCommandCallback\n"));
    }
    else
    if (IsEqualIID(&(lpData->guidInfo), &GUID_Miscellaneous2Callbacks))
    {
        DISPDBG((0, " -> GUID_Miscellaneous2Callbacks\n"));
    }
    else
    if (IsEqualIID(&(lpData->guidInfo), &GUID_UpdateNonLocalHeap))
    {
        DISPDBG((0, " -> GUID_UpdateNonLocalHeap\n"));
    }
    else
    if (IsEqualIID(&(lpData->guidInfo), &GUID_GetHeapAlignment))
    {
        DISPDBG((0, " -> GUID_GetHeapAlignment\n"));
    }
    else
    if (IsEqualIID(&(lpData->guidInfo), &GUID_NTPrivateDriverCaps))
    {
        DD_NTPRIVATEDRIVERCAPS DDPrivateDriverCaps;

        DISPDBG((0, " -> GUID_NTPrivateDriverCaps\n"));

        memset(&DDPrivateDriverCaps, 0, sizeof(DDPrivateDriverCaps));
        DDPrivateDriverCaps.dwSize=sizeof(DDPrivateDriverCaps);
#ifndef VBOX_WITH_VIDEOHWACCEL
        DDPrivateDriverCaps.dwPrivateCaps = 0; /* DDHAL_PRIVATECAP_NOTIFYPRIMARYCREATION -> call CreateSurface for the primary surface */
#else
        DDPrivateDriverCaps.dwPrivateCaps = DDHAL_PRIVATECAP_NOTIFYPRIMARYCREATION; /* -> call CreateSurface for the primary surface */
#endif

        lpData->dwActualSize =sizeof(DDPrivateDriverCaps);

        dwSize = min(sizeof(DDPrivateDriverCaps),lpData->dwExpectedSize);
        memcpy(lpData->lpvData, &DDPrivateDriverCaps, dwSize);
        lpData->ddRVal = DD_OK;
    }
    else
    if (IsEqualIID(&(lpData->guidInfo), &GUID_DDMoreSurfaceCaps))
    {
#ifndef VBOX_WITH_VIDEOHWACCEL
        DD_MORESURFACECAPS DDMoreSurfaceCaps;
        DDSCAPSEX   ddsCapsEx, ddsCapsExAlt;

        DISPDBG((0, " -> GUID_DDMoreSurfaceCaps\n"));

        // fill in everything until expectedsize...
        memset(&DDMoreSurfaceCaps, 0, sizeof(DDMoreSurfaceCaps));

        // Caps for heaps 2..n
        memset(&ddsCapsEx, 0, sizeof(ddsCapsEx));
        memset(&ddsCapsExAlt, 0, sizeof(ddsCapsEx));

        DDMoreSurfaceCaps.dwSize=lpData->dwExpectedSize;

        lpData->dwActualSize = lpData->dwExpectedSize;

        dwSize = min(sizeof(DDMoreSurfaceCaps),lpData->dwExpectedSize);
        memcpy(lpData->lpvData, &DDMoreSurfaceCaps, dwSize);

        // now fill in other heaps...
        while (dwSize < lpData->dwExpectedSize)
        {
            memcpy( (PBYTE)lpData->lpvData+dwSize,
                    &ddsCapsEx,
                    sizeof(DDSCAPSEX));
            dwSize += sizeof(DDSCAPSEX);
            memcpy( (PBYTE)lpData->lpvData+dwSize,
                    &ddsCapsExAlt,
                    sizeof(DDSCAPSEX));
            dwSize += sizeof(DDSCAPSEX);
        }

        lpData->ddRVal = DD_OK;
#else
        DISPDBG((0, " -> GUID_DDMoreSurfaceCaps\n"));
#endif
    }
    else
    if (IsEqualIID(&(lpData->guidInfo), &GUID_DDStereoMode))
    {
        DISPDBG((0, " -> GUID_DDStereoMode\n"));
    }
    else
    if (IsEqualIID(&(lpData->guidInfo), &GUID_NonLocalVidMemCaps))
    {
        DISPDBG((0, " -> GUID_NonLocalVidMemCaps\n"));
    }
    else
    if (IsEqualIID(&lpData->guidInfo, &GUID_NTCallbacks))
    {
#ifndef VBOX_WITH_VIDEOHWACCEL
        DD_NTCALLBACKS NtCallbacks;

        DISPDBG((0, " -> GUID_NTCallbacks\n"));
        memset(&NtCallbacks, 0, sizeof(NtCallbacks));

        dwSize = min(lpData->dwExpectedSize, sizeof(DD_NTCALLBACKS));

        NtCallbacks.dwSize           = dwSize;
        NtCallbacks.dwFlags          =   DDHAL_NTCB32_FREEDRIVERMEMORY
                                       | DDHAL_NTCB32_SETEXCLUSIVEMODE
                                       | DDHAL_NTCB32_FLIPTOGDISURFACE
                                       ;
        NtCallbacks.FreeDriverMemory = DdFreeDriverMemory;
        NtCallbacks.SetExclusiveMode = DdSetExclusiveMode;
        NtCallbacks.FlipToGDISurface = DdFlipToGDISurface;

        memcpy(lpData->lpvData, &NtCallbacks, dwSize);

        lpData->ddRVal = DD_OK;
#else
        DISPDBG((0, " -> GUID_NTCallbacks\n"));
#endif
    }
    else
    if (IsEqualIID(&lpData->guidInfo, &GUID_KernelCaps))
    {
        DISPDBG((0, " -> GUID_KernelCaps\n"));
    }
    else
    if (IsEqualIID(&lpData->guidInfo, &GUID_KernelCallbacks))
    {
        DISPDBG((0, " -> GUID_KernelCallbacks\n"));
    }
    else
    if (IsEqualIID(&lpData->guidInfo, &GUID_MotionCompCallbacks))
    {
        DISPDBG((0, " -> GUID_MotionCompCallbacks\n"));
    }
    else
    if (IsEqualIID(&lpData->guidInfo, &GUID_VideoPortCallbacks))
    {
        DISPDBG((0, " -> GUID_VideoPortCallbacks\n"));
    }
    else
    if (IsEqualIID(&lpData->guidInfo, &GUID_ColorControlCallbacks))
    {
        DISPDBG((0, " -> GUID_ColorControlCallbacks\n"));
    }
    else
    if (IsEqualIID(&lpData->guidInfo, &GUID_VideoPortCaps))
    {
        DISPDBG((0, " -> GUID_VideoPortCaps\n"));
    }
    else
    if (IsEqualIID(&lpData->guidInfo, &GUID_D3DCallbacks2))
    {
        DISPDBG((0, " -> GUID_D3DCallbacks2\n"));
    }
    else
    if (IsEqualIID(&lpData->guidInfo, &GUID_D3DCallbacks3))
    {
        DISPDBG((0, " -> GUID_D3DCallbacks3\n"));
    }

    /* Always return this */
    return DDHAL_DRIVER_HANDLED;
}

/**
 * DdCreateSurface
 *
 * The DdCreateSurface callback function creates a DirectDraw surface.
 *
 * lpCreateSurface
 *     Points to a DD_CREATESURFACEDATA structure that contains the information required to create a surface.
 *
 * Return Value
 *
 * DdCreateSurface returns one of the following callback codes:
 * DDHAL_DRIVER_HANDLED
 * DDHAL_DRIVER_NOTHANDLED
 *
 */
DWORD APIENTRY DdCreateSurface(PDD_CREATESURFACEDATA  lpCreateSurface)
{
    PPDEV pDev = (PPDEV)lpCreateSurface->lpDD->dhpdev;
    DD_SURFACE_LOCAL*   lpSurfaceLocal;
    DD_SURFACE_GLOBAL*  lpSurfaceGlobal;
    LPDDSURFACEDESC     lpSurfaceDesc;
    LONG                lPitch, lBpp;

    DISPDBG((0, "%s: %p\n", __FUNCTION__, pDev));

    lpSurfaceLocal                  = lpCreateSurface->lplpSList[0];
    lpSurfaceGlobal                 = lpSurfaceLocal->lpGbl;
    lpSurfaceDesc                   = lpCreateSurface->lpDDSurfaceDesc;

#ifdef VBOX_WITH_VIDEOHWACCEL
    if(pDev->vhwaInfo.bVHWAEnabled)
    {
        VBOXVHWACMD* pCmd;
        DDPIXELFORMAT * pFormat = &lpSurfaceGlobal->ddpfSurface;

        //
        // Modify surface descriptions as appropriate and let Direct
        // Draw perform the allocation if the surface was not the primary
        //
        if (lpSurfaceLocal->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
        {
            DISPDBG((0, "-> primary surface\n"));
            lpSurfaceGlobal->fpVidMem       = 0;
        }
        else
        {
            DISPDBG((0, "-> secondary surface\n"));
            lpSurfaceGlobal->fpVidMem       = DDHAL_PLEASEALLOC_BLOCKSIZE;
        }

        pCmd = vboxVHWACommandCreate (pDev, VBOXVHWACMD_TYPE_SURF_CREATE, sizeof(VBOXVHWACMD_SURF_CREATE));
        if(pCmd)
        {
            VBOXVHWACMD_SURF_CREATE * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_CREATE);
            PVBOXVHWASURFDESC pDesc;
            int rc;

            memset(pBody, 0, sizeof(VBOXVHWACMD_SURF_CREATE));

            rc = vboxVHWAFromDDSURFACEDESC(&pBody->SurfInfo, lpSurfaceDesc);

            pBody->SurfInfo.surfCaps = vboxVHWAFromDDSCAPS(lpSurfaceLocal->ddsCaps.dwCaps);
            pBody->SurfInfo.flags |= DDSD_CAPS;

            pBody->SurfInfo.height = lpSurfaceGlobal->wHeight;
            pBody->SurfInfo.width = lpSurfaceGlobal->wWidth;
            pBody->SurfInfo.flags |= DDSD_HEIGHT | DDSD_WIDTH;

            vboxVHWAFromDDPIXELFORMAT(&pBody->SurfInfo.PixelFormat, pFormat);
            pBody->SurfInfo.flags |= VBOXVHWA_SD_PIXELFORMAT;

            if (lpSurfaceLocal->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
            {
                pBody->SurfInfo.offSurface = 0;
            }
            else
            {
                pBody->SurfInfo.offSurface = VBOXVHWA_OFFSET64_VOID;
            }


            pDesc = vboxVHWASurfDescAlloc();
            if(pDesc)
            {
                vboxVHWACommandSubmit(pDev, pCmd);
                Assert(pCmd->rc == VINF_SUCCESS);
                if(pCmd->rc == VINF_SUCCESS)
                {
                	uint32_t surfSizeX = pBody->SurfInfo.sizeX;
                	uint32_t surfSizeY = pBody->SurfInfo.sizeY;
                    pDesc->hHostHandle = pBody->SurfInfo.hSurf;
                    if(!!(lpSurfaceLocal->ddsCaps.dwCaps & DDSCAPS_OVERLAY)
                    		&& !!(lpSurfaceLocal->ddsCaps.dwCaps & DDSCAPS_VISIBLE))
                    {
                    	pDesc->bVisible = true;
                    }
                    lpSurfaceGlobal->dwReserved1 = (ULONG_PTR)pDesc;
                    lPitch = pBody->SurfInfo.pitch;
//                    lBpp = pBody->SurfInfo.bitsPerPixel;
//                    pDesc->cBitsPerPixel = lBpp;
#if 0
                    lpSurfaceGlobal->dwBlockSizeX   = lPitch;
                    lpSurfaceGlobal->dwBlockSizeY   = lpSurfaceGlobal->wHeight;
                    lpSurfaceGlobal->lPitch         = lPitch;
#else
                    lpSurfaceGlobal->dwBlockSizeX   = surfSizeX;
                    lpSurfaceGlobal->dwBlockSizeY   = surfSizeY;
                    lpSurfaceGlobal->lPitch         = lPitch;
#endif
#if 1
                    lpSurfaceDesc->lPitch   = lpSurfaceGlobal->lPitch;
                    lpSurfaceDesc->dwFlags |= DDSD_PITCH;
#endif

                    lpCreateSurface->ddRVal = DD_OK;
                }
                else
                {
                    vboxVHWASurfDescFree(pDesc);
                    lpCreateSurface->ddRVal = DDERR_GENERIC;
                }
            }
            vbvaVHWACommandRelease(pDev, pCmd);
        }
        return DDHAL_DRIVER_NOTHANDLED;
    }
#endif
    lpSurfaceGlobal->dwReserved1    = 0;

    if (lpSurfaceDesc->ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED4)
    {
        lBpp = 4;
        lPitch = lpSurfaceGlobal->wWidth/2;
        lPitch = (lPitch + 31) & ~31;
    }
    else
    if (lpSurfaceDesc->ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8)
    {
        lBpp = 8;
        lPitch = lpSurfaceGlobal->wWidth;
        lPitch = (lPitch + 31) & ~31;
    }
    else
    {
        lBpp   = lpSurfaceDesc->ddpfPixelFormat.dwRGBBitCount;
        lPitch = lpSurfaceGlobal->wWidth*(lBpp/8);
    }
    DISPDBG((0, "New surface (%d,%d)\n", lpSurfaceGlobal->wWidth, lpSurfaceGlobal->wHeight));
    DISPDBG((0, "BPP %d lPitch=%d\n", lBpp, lPitch));

    lpSurfaceGlobal->dwBlockSizeX   = lPitch;
    lpSurfaceGlobal->dwBlockSizeY   = lpSurfaceGlobal->wHeight;
    lpSurfaceGlobal->lPitch         = lPitch;

    lpSurfaceDesc->lPitch   = lpSurfaceGlobal->lPitch;
    lpSurfaceDesc->dwFlags |= DDSD_PITCH;

    //
    // Modify surface descriptions as appropriate and let Direct
    // Draw perform the allocation if the surface was not the primary
    //
    if (lpSurfaceLocal->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
    {
        DISPDBG((0, "-> primary surface\n"));
        lpSurfaceGlobal->fpVidMem       = 0;
    }
    else
    {
        DISPDBG((0, "-> secondary surface\n"));
        lpSurfaceGlobal->fpVidMem       = DDHAL_PLEASEALLOC_BLOCKSIZE;
    }

    return DDHAL_DRIVER_NOTHANDLED;
}

/**
 * DdCanCreateSurface
 *
 * The DdCanCreateSurface callback function indicates whether the driver can create a surface of the specified surface description.
 *
 *
 * Parameters
 * lpCanCreateSurface
 *     Points to the DD_CANCREATESURFACEDATA structure containing the information required for the driver to determine whether a surface can be created.
 *
 * Return Value
 *
 * DdCanCreateSurface returns one of the following callback codes:
 *
 * DDHAL_DRIVER_HANDLED
 * DDHAL_DRIVER_NOTHANDLED
 *
 */
DWORD APIENTRY DdCanCreateSurface(PDD_CANCREATESURFACEDATA lpCanCreateSurface)
{
    PPDEV pDev = (PPDEV)lpCanCreateSurface->lpDD->dhpdev;

    PDD_SURFACEDESC lpDDS = lpCanCreateSurface->lpDDSurfaceDesc;

    DISPDBG((0, "%s: %p\n", __FUNCTION__, pDev));

#ifdef VBOX_WITH_VIDEOHWACCEL
    if(pDev->vhwaInfo.bVHWAEnabled)
    {
        VBOXVHWACMD* pCmd;
        uint32_t unsupportedSCaps = vboxVHWAUnsupportedDDSCAPS(lpDDS->ddsCaps.dwCaps);
        Assert(!unsupportedSCaps);
        if(unsupportedSCaps)
        {
            VHWADBG(("vboxVHWASurfCanCreate: unsupported ddscaps: 0x%x", unsupportedSCaps));
            lpCanCreateSurface->ddRVal = DDERR_INVALIDCAPS;
            return DDHAL_DRIVER_HANDLED;
        }

        unsupportedSCaps = vboxVHWAUnsupportedDDPFS(lpDDS->ddpfPixelFormat.dwFlags);
        Assert(!unsupportedSCaps);
        if(unsupportedSCaps)
        {
            VHWADBG(("vboxVHWASurfCanCreate: unsupported pixel format: 0x%x", unsupportedSCaps));
            lpCanCreateSurface->ddRVal = DDERR_INVALIDPIXELFORMAT;
            return DDHAL_DRIVER_HANDLED;
        }

        pCmd = vboxVHWACommandCreate (pDev, VBOXVHWACMD_TYPE_SURF_CANCREATE, sizeof(VBOXVHWACMD_SURF_CANCREATE));
        if(pCmd)
        {
            int rc;
            VBOXVHWACMD_SURF_CANCREATE * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_CANCREATE);
            memset(pBody, 0, sizeof(VBOXVHWACMD_SURF_CANCREATE));

            rc = vboxVHWAFromDDSURFACEDESC(&pBody->SurfInfo, lpDDS);
            pBody->u.in.bIsDifferentPixelFormat = lpCanCreateSurface->bIsDifferentPixelFormat;

            vboxVHWACommandSubmit(pDev, pCmd);
            Assert(pCmd->rc == VINF_SUCCESS);
            if(pCmd->rc == VINF_SUCCESS)
            {
#ifdef DEBUGVHWASTRICT
                Assert(!pBody->u.out.ErrInfo);
#endif
                if(pBody->u.out.ErrInfo)
                {
                    lpCanCreateSurface->ddRVal = DDERR_GENERIC;
                }
                else
                {
                    lpCanCreateSurface->ddRVal = DD_OK;
                }
            }
            else
            {
                lpCanCreateSurface->ddRVal = DDERR_GENERIC;
            }
            vbvaVHWACommandRelease(pDev, pCmd);
        }
        else
        {
            lpCanCreateSurface->ddRVal = DDERR_GENERIC;
        }
        return DDHAL_DRIVER_HANDLED;
    }
#endif

    if (lpDDS->ddsCaps.dwCaps & DDSCAPS_ZBUFFER)
    {
        DISPDBG((0, "No Z-Bufer support\n"));
        lpCanCreateSurface->ddRVal = DDERR_INVALIDPIXELFORMAT;
        return DDHAL_DRIVER_HANDLED;
    }
    if (lpDDS->ddsCaps.dwCaps & DDSCAPS_TEXTURE)
    {
        DISPDBG((0, "No texture support\n"));
        lpCanCreateSurface->ddRVal = DDERR_INVALIDPIXELFORMAT;
        return DDHAL_DRIVER_HANDLED;
    }
    if (lpCanCreateSurface->bIsDifferentPixelFormat && (lpDDS->ddpfPixelFormat.dwFlags & DDPF_FOURCC))
    {
        DISPDBG((0, "FOURCC not supported\n"));
        lpCanCreateSurface->ddRVal = DDERR_INVALIDPIXELFORMAT;
        return DDHAL_DRIVER_HANDLED;
    }

    lpCanCreateSurface->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

// ***************************WIN NT ONLY**********************************
//
// DdMapMemory
//
// Maps application-modifiable portions of the frame buffer into the
// user-mode address space of the specified process, or unmaps memory.
//
// DdMapMemory is called to perform memory mapping before the first call to
// DdLock. The handle returned by the driver in fpProcess will be passed to
// every DdLock call made on the driver.
//
// DdMapMemory is also called to unmap memory after the last DdUnLock call is
// made.
//
// To prevent driver crashes, the driver must not map any portion of the frame
// buffer that must not be modified by an application.
//
// Parameters
//      lpMapMemory
//          Points to a DD_MAPMEMORYDATA structure that contains details for
//          the memory mapping or unmapping operation.
//
//          .lpDD
//              Points to a DD_DIRECTDRAW_GLOBAL structure that represents
//              the driver.
//          .bMap
//              Specifies the memory operation that the driver should perform.
//              A value of TRUE indicates that the driver should map memory;
//              FALSE means that the driver should unmap memory.
//          .hProcess
//              Specifies a handle to the process whose address space is
//              affected.
//          .fpProcess
//              Specifies the location in which the driver should return the
//              base address of the process's memory mapped space when bMap
//              is TRUE. When bMap is FALSE, fpProcess contains the base
//              address of the memory to be unmapped by the driver.
//          .ddRVal
//              Specifies the location in which the driver writes the return
//              value of the DdMapMemory callback. A return code of DD_OK
//              indicates success.
//
//-----------------------------------------------------------------------------

DWORD CALLBACK DdMapMemory(PDD_MAPMEMORYDATA lpMapMemory)
{
    PPDEV pDev = (PPDEV)lpMapMemory->lpDD->dhpdev;

    VIDEO_SHARE_MEMORY              ShareMemory;
    VIDEO_SHARE_MEMORY_INFORMATION  ShareMemoryInformation;
    DWORD                           ReturnedDataLength;

    DISPDBG((0, "%s: %p bMap %d\n", __FUNCTION__, pDev, lpMapMemory->bMap));

    if (lpMapMemory->bMap)
    {
        ShareMemory.ProcessHandle = lpMapMemory->hProcess;

        // 'RequestedVirtualAddress' isn't actually used for the SHARE IOCTL:

        ShareMemory.RequestedVirtualAddress = 0;

        // We map in starting at the top of the frame buffer:

        ShareMemory.ViewOffset = 0;

        // We map down to the end of the frame buffer, including the offscreen heap.
        ShareMemory.ViewSize   = pDev->layout.offVBVABuffer;

        DISPDBG((0, "ViewSize = %x\n", ShareMemory.ViewSize));

        if (EngDeviceIoControl(pDev->hDriver,
                       IOCTL_VIDEO_SHARE_VIDEO_MEMORY,
                       &ShareMemory,
                       sizeof(VIDEO_SHARE_MEMORY),
                       &ShareMemoryInformation,
                       sizeof(VIDEO_SHARE_MEMORY_INFORMATION),
                       &ReturnedDataLength))
        {
            DISPDBG((0, "Failed IOCTL_VIDEO_SHARE_MEMORY\n"));

            lpMapMemory->ddRVal = DDERR_GENERIC;

            DISPDBG((0, "DdMapMemory: Exit GEN, DDHAL_DRIVER_HANDLED\n"));

            return(DDHAL_DRIVER_HANDLED);
        }

        lpMapMemory->fpProcess =
                            (FLATPTR) ShareMemoryInformation.VirtualAddress;
    }
    else
    {
        ShareMemory.ProcessHandle           = lpMapMemory->hProcess;
        ShareMemory.ViewOffset              = 0;
        ShareMemory.ViewSize                = 0;
        ShareMemory.RequestedVirtualAddress = (VOID*) lpMapMemory->fpProcess;

        if (EngDeviceIoControl(pDev->hDriver,
                       IOCTL_VIDEO_UNSHARE_VIDEO_MEMORY,
                       &ShareMemory,
                       sizeof(VIDEO_SHARE_MEMORY),
                       NULL,
                       0,
                       &ReturnedDataLength))
        {
            DISPDBG((0, "Failed IOCTL_VIDEO_UNSHARE_MEMORY\n"));
        }
    }

    lpMapMemory->ddRVal = DD_OK;

    return(DDHAL_DRIVER_HANDLED);
}

/**
 * DdLock
 *
 * The DdLock callback function locks a specified area of surface memory and provides a valid pointer to a block of memory associated with a surface.
 *
 * Parameters
 * lpLock
 *     Points to a DD_LOCKDATA structure that contains the information required to perform the lockdown.
 *
 * Return Value
 *
 * DdLock returns one of the following callback codes:
 *
 * DDHAL_DRIVER_HANDLED
 * DDHAL_DRIVER_NOTHANDLED
 *
 */
DWORD APIENTRY DdLock(PDD_LOCKDATA lpLock)
{
    PPDEV pDev = (PPDEV)lpLock->lpDD->dhpdev;
    DD_SURFACE_LOCAL*   lpSurfaceLocal = lpLock->lpDDSurface;

    DISPDBG((0, "%s: %p bHasRect = %d fpProcess = %p\n", __FUNCTION__, pDev, lpLock->bHasRect, lpLock->fpProcess));

#ifdef VBOX_WITH_VIDEOHWACCEL
    if(pDev->vhwaInfo.bVHWAEnabled)
    {
#ifndef DBG_DDSTUBS
        DD_SURFACE_GLOBAL*  lpSurfaceGlobal = lpSurfaceLocal->lpGbl;
        PVBOXVHWASURFDESC pDesc = (PVBOXVHWASURFDESC)lpSurfaceGlobal->dwReserved1;
        RECTL tmpRect, *pRect;

        if(ASMAtomicUoReadU32(&pDesc->cPendingBltsSrc)
                || ASMAtomicUoReadU32(&pDesc->cPendingFlipsCurr)
                || ASMAtomicUoReadU32(&pDesc->cPendingBltsDst)
                || ASMAtomicUoReadU32(&pDesc->cPendingFlipsTarg))
        {
            /* ensure we have host cmds processed to update pending blits and flips */
            vboxVHWACommandCheckHostCmds(pDev);
            if(ASMAtomicUoReadU32(&pDesc->cPendingBltsSrc)
                        || ASMAtomicUoReadU32(&pDesc->cPendingFlipsCurr)
                        || ASMAtomicUoReadU32(&pDesc->cPendingBltsDst)
                        || ASMAtomicUoReadU32(&pDesc->cPendingFlipsTarg))
            {
                lpLock->ddRVal = DDERR_WASSTILLDRAWING;
                return DDHAL_DRIVER_HANDLED;
            }
        }

//        if(VBOXDD_CHECKFLAG(lpLock->dwFlags, DDLOCK_SURFACEMEMORYPTR))
//        {
//            lpLock->lpSurfData = (LPVOID)(lpSurfaceGlobal->fpVidMem + lpSurfaceGlobal->lPitch * lpLock->rArea.top
//                + lpLock->rArea.left * pDesc->cBitsPerPixel/8);
//        }

        if (lpLock->bHasRect)
        {
            pRect = &lpLock->rArea;
        }
        else
        {
            tmpRect.left=0;
            tmpRect.top=0;
            tmpRect.right=lpSurfaceGlobal->wWidth-1;
            tmpRect.bottom=lpSurfaceGlobal->wHeight-1;
            pRect = &tmpRect;
        }

        if(VBOXDD_CHECKFLAG(lpLock->dwFlags, DDLOCK_DISCARDCONTENTS))
        {
//            pBody->u.in.flags |= VBOXVHWA_LOCK_DISCARDCONTENTS;

            vboxVHWARegionTrySubstitute(&pDesc->NonupdatedMemRegion, pRect);
//                /* we're not interested in completion, just send the command */
//                vboxVHWACommandSubmitAsynch(pDev, pCmd, vboxVHWAFreeCmdCompletion, NULL);
            vboxVHWARegionAdd(&pDesc->UpdatedMemRegion, pRect);
            lpLock->ddRVal = DD_OK;
        }
        else if(!vboxVHWARegionIntersects(&pDesc->NonupdatedMemRegion, pRect))
        {
//                vboxVHWACommandSubmitAsynch(pDev, pCmd, vboxVHWAFreeCmdCompletion, NULL);
            vboxVHWARegionAdd(&pDesc->UpdatedMemRegion, pRect);
            lpLock->ddRVal = DD_OK;
        }
        else
        {
            VBOXVHWACMD* pCmd = vboxVHWACommandCreate (pDev, VBOXVHWACMD_TYPE_SURF_LOCK, sizeof(VBOXVHWACMD_SURF_LOCK));
            if(pCmd)
            {
                VBOXVHWACMD_SURF_LOCK * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_LOCK);
                memset(pBody, 0, sizeof(VBOXVHWACMD_SURF_LOCK));

                pBody->u.in.offSurface = (uint64_t)lpSurfaceGlobal->fpVidMem;

//            if (lpLock->bHasRect)
//            {
//                DISPDBG((0, "%d,%d %dx%d\n", lpLock->rArea.left, lpLock->rArea.top, lpLock->rArea.right - lpLock->rArea.left, lpLock->rArea.bottom - lpLock->rArea.top));
//                vboxVHWAFromRECTL(&pBody->u.in.rect, &lpLock->rArea);
//                pBody->u.in.rectValid = 1;
//
//            }
//            else
//            {
//                pBody->u.in.rectValid = 0;
//            }
                Assert(pDesc->NonupdatedMemRegion.bValid);
                vboxVHWAFromRECTL(&pBody->u.in.rect, &pDesc->NonupdatedMemRegion.Rect);
                pBody->u.in.rectValid = 1;

                pBody->u.in.hSurf = pDesc->hHostHandle;

                /* wait for the surface to be locked and memory buffer updated */
                vboxVHWACommandSubmit(pDev, pCmd);
                vbvaVHWACommandRelease(pDev, pCmd);
                vboxVHWARegionClear(&pDesc->NonupdatedMemRegion);
                lpLock->ddRVal = DD_OK;
            }
            else
            {
                lpLock->ddRVal = DDERR_GENERIC;
            }
        }
#else
        lpLock->ddRVal = DD_OK;
#endif
        return DDHAL_DRIVER_NOTHANDLED;
    }
#endif
    if (lpSurfaceLocal->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
    {
        /* The updated rectangle must be reported only for the primary surface. */
        pDev->ddLock.bLocked = TRUE;

        if (lpLock->bHasRect)
        {
            DISPDBG((0, "%d,%d %dx%d\n", lpLock->rArea.left, lpLock->rArea.top, lpLock->rArea.right - lpLock->rArea.left, lpLock->rArea.bottom - lpLock->rArea.top));
            pDev->ddLock.rArea = lpLock->rArea;
        }
        else
        {
            pDev->ddLock.rArea.left   = 0;
            pDev->ddLock.rArea.top    = 0;
            pDev->ddLock.rArea.right  = pDev->cxScreen;
            pDev->ddLock.rArea.bottom = pDev->cyScreen;
        }
    }
    else
    {
        DISPDBG((0, "%s: secondary surface.\n", __FUNCTION__));
    }

    // Because we correctly set 'fpVidMem' to be the offset into our frame
    // buffer when we created the surface, DirectDraw will automatically take
    // care of adding in the user-mode frame buffer address if we return
    // DDHAL_DRIVER_NOTHANDLED:
    lpLock->ddRVal = DD_OK;
    return DDHAL_DRIVER_NOTHANDLED;
}

/**
 * DdUnlock
 *
 * The DdUnLock callback function releases the lock held on the specified surface.
 *
 * Parameters
 * lpUnlock
 *     Points to a DD_UNLOCKDATA structure that contains the information required to perform the lock release. *
 *
 * Return Value
 *
 * DdLock returns one of the following callback codes:
 *
 * DDHAL_DRIVER_HANDLED
 * DDHAL_DRIVER_NOTHANDLED
 *
 */
DWORD APIENTRY DdUnlock(PDD_UNLOCKDATA lpUnlock)
{
    PPDEV pDev = (PPDEV)lpUnlock->lpDD->dhpdev;
    DISPDBG((0, "%s: %p\n", __FUNCTION__, pDev));
#ifdef VBOX_WITH_VIDEOHWACCEL
    if (pDev->vhwaInfo.bVHWAEnabled)
    {
#ifndef DBG_DDSTUBS
        DD_SURFACE_LOCAL*   lpSurfaceLocal = lpUnlock->lpDDSurface;
        DD_SURFACE_GLOBAL*  lpSurfaceGlobal = lpSurfaceLocal->lpGbl;
        PVBOXVHWASURFDESC pDesc = (PVBOXVHWASURFDESC)lpSurfaceGlobal->dwReserved1;

        //TODO: hadle vrdp properly
        if (  pDev->pVBVA->u32HostEvents
            & VBOX_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET)
        {
            vrdpReset (pDev);

            pDev->pVBVA->u32HostEvents &=
                      ~VBOX_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET;
        }

//        /* ensure we have host cmds processed to update pending blits and flips */
//        vboxVHWACommandCheckHostCmds(pDev);

        if(lpSurfaceLocal->ddsCaps.dwCaps & DDSCAPS_VISIBLE
                || (
                        lpSurfaceLocal->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE
                        || (
                                !!(lpSurfaceLocal->ddsCaps.dwCaps & DDSCAPS_OVERLAY)
                                && pDesc->bVisible
                           )
                   )
          )
        {
            VBOXVHWACMD* pCmd = vboxVHWACommandCreate (pDev, VBOXVHWACMD_TYPE_SURF_UNLOCK, sizeof(VBOXVHWACMD_SURF_UNLOCK));
        //    int rc = VERR_GENERAL_FAILURE;
            if(pCmd)
            {
                VBOXVHWACMD_SURF_UNLOCK * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_UNLOCK);
                memset(pBody, 0, sizeof(VBOXVHWACMD_SURF_UNLOCK));

                pBody->u.in.hSurf = pDesc->hHostHandle;
                if(pDesc->UpdatedMemRegion.bValid)
                {
                    pBody->u.in.xUpdatedMemValid = 1;
                    vboxVHWAFromRECTL(&pBody->u.in.xUpdatedMemRect, &pDesc->UpdatedMemRegion.Rect);
                    vboxVHWARegionClear(&pDesc->UpdatedMemRegion);
                }

                vboxVHWACommandSubmitAsynchAndComplete(pDev, pCmd);

                lpUnlock->ddRVal = DD_OK;
            }
            else
            {
                lpUnlock->ddRVal = DDERR_GENERIC;
            }
        }
#else
        lpUnlock->ddRVal = DD_OK;
#endif

        return DDHAL_DRIVER_NOTHANDLED;
    }
#endif
    if (pDev->ddLock.bLocked)
    {
        DISPDBG((0, "%d,%d %dx%d\n", pDev->ddLock.rArea.left, pDev->ddLock.rArea.top, pDev->ddLock.rArea.right - pDev->ddLock.rArea.left, pDev->ddLock.rArea.bottom - pDev->ddLock.rArea.top));

#ifndef VBOX_WITH_HGSMI
        if (pDev->pInfo && vboxHwBufferBeginUpdate (pDev))
        {
            vbvaReportDirtyRect (pDev, &pDev->ddLock.rArea);

            if (  pDev->pInfo->hostEvents.fu32Events
                & VBOX_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET)
            {
                vrdpReset (pDev);

                pDev->pInfo->hostEvents.fu32Events &=
                          ~VBOX_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET;
            }

            if (pDev->vbva.pVbvaMemory->fu32ModeFlags
                & VBVA_F_MODE_VRDP)
            {
                vrdpReportDirtyRect (pDev, &pDev->ddLock.rArea);
            }

            vboxHwBufferEndUpdate (pDev);
        }
#else
        if (pDev->bHGSMISupported && vboxHwBufferBeginUpdate (pDev))
        {
            vbvaReportDirtyRect (pDev, &pDev->ddLock.rArea);

            if (  pDev->pVBVA->u32HostEvents
                & VBOX_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET)
            {
                vrdpReset (pDev);

                pDev->pVBVA->u32HostEvents &=
                          ~VBOX_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET;
            }

            if (pDev->pVBVA->u32HostEvents
                & VBVA_F_MODE_VRDP)
            {
                vrdpReportDirtyRect (pDev, &pDev->ddLock.rArea);
            }

            vboxHwBufferEndUpdate (pDev);
        }
#endif /* VBOX_WITH_HGSMI */

        pDev->ddLock.bLocked = FALSE;
    }

    lpUnlock->ddRVal = DD_OK;
    return DDHAL_DRIVER_NOTHANDLED;
}

/**
 * DdDestroySurface
 *
 * The DdDestroySurface callback function destroys a DirectDraw surface.
 *
 * Parameters
 * lpDestroySurface
 *     Points to a DD_DESTROYSURFACEDATA structure that contains the information needed to destroy a surface.
 *
 * Return Value
 *
 * DdDestroySurface returns one of the following callback codes:
 *
 * DDHAL_DRIVER_HANDLED
 * DDHAL_DRIVER_NOTHANDLED
 *
 */
DWORD APIENTRY DdDestroySurface(PDD_DESTROYSURFACEDATA lpDestroySurface)
{
#ifdef VBOX_WITH_VIDEOHWACCEL
    PPDEV pDev = (PPDEV)lpDestroySurface->lpDD->dhpdev;
    if(pDev->vhwaInfo.bVHWAEnabled)
    {
        DD_SURFACE_LOCAL*   lpSurfaceLocal = lpDestroySurface->lpDDSurface;
        DD_SURFACE_GLOBAL*  lpSurfaceGlobal = lpSurfaceLocal->lpGbl;
        VBOXVHWACMD* pCmd;

        DISPDBG((0, "%s: %p\n", __FUNCTION__, pDev));

        pCmd = vboxVHWACommandCreate (pDev, VBOXVHWACMD_TYPE_SURF_DESTROY, sizeof(VBOXVHWACMD_SURF_DESTROY));
    //    int rc = VERR_GENERAL_FAILURE;
        if(pCmd)
        {
            VBOXVHWACMD_SURF_DESTROY * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_DESTROY);
            PVBOXVHWASURFDESC pDesc = (PVBOXVHWASURFDESC)lpSurfaceGlobal->dwReserved1;

            memset(pBody, 0, sizeof(VBOXVHWACMD_SURF_DESTROY));

            pBody->u.in.hSurf = pDesc->hHostHandle;

            /* we're not interested in completion, just send the command */
            vboxVHWACommandSubmitAsynchAndComplete(pDev, pCmd);

            vboxVHWASurfDescFree(pDesc);

            lpDestroySurface->ddRVal = DD_OK;
        }
        else
        {
            lpDestroySurface->ddRVal = DDERR_GENERIC;
        }
    }
    else
#endif
        lpDestroySurface->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}


//-----------------------------------------------------------------------------
//
// DdSetExclusiveMode
//
// This function is called by DirectDraw when we switch from the GDI surface,
// to DirectDraw exclusive mode, e.g. to run a game in fullcreen mode.
// You only need to implement this function when you are using the
// 'HeapVidMemAllocAligned' function and allocate memory for Device Bitmaps
// and DirectDraw surfaces from the same heap.
//
// We use this call to disable GDI DeviceBitMaps when we are running in
// DirectDraw exclusive mode. Otherwise a DD app gets confused if both GDI and
// DirectDraw allocate memory from the same heap.
//
// See also DdFlipToGDISurface.
//
//-----------------------------------------------------------------------------


DWORD APIENTRY DdSetExclusiveMode(PDD_SETEXCLUSIVEMODEDATA lpSetExclusiveMode)
{
    PPDEV pDev = (PPDEV)lpSetExclusiveMode->lpDD->dhpdev;
    DISPDBG((0, "%s: %p\n", __FUNCTION__, pDev));

    // remember setting of exclusive mode in pDev,
    // so GDI can stop to promote DeviceBitmaps into
    // video memory

    pDev->bDdExclusiveMode = lpSetExclusiveMode->dwEnterExcl;

    lpSetExclusiveMode->ddRVal = DD_OK;

    return DDHAL_DRIVER_HANDLED;
}

//-----------------------------------------------------------------------------
//
// DWORD DdFlipToGDISurface
//
// This function is called by DirectDraw when it flips to the surface on which
// GDI can write to.
//
//-----------------------------------------------------------------------------

DWORD APIENTRY DdFlipToGDISurface(PDD_FLIPTOGDISURFACEDATA lpFlipToGDISurface)
{
    PPDEV pDev = (PPDEV)lpFlipToGDISurface->lpDD->dhpdev;
    DISPDBG((0, "%s: %p\n", __FUNCTION__, pDev));

    pDev->dwNewDDSurfaceOffset = 0xffffffff;

    lpFlipToGDISurface->ddRVal = DD_OK;

    //
    //  we return NOTHANDLED, then the ddraw runtime takes
    //  care that we flip back to the primary...
    //
    return DDHAL_DRIVER_NOTHANDLED;
}
//-----------------------------------------------------------------------------
//
// DWORD DdFreeDriverMemory
//
// This function called by DirectDraw when it's running low on memory in
// our heap.  You only need to implement this function if you use the
// DirectDraw 'HeapVidMemAllocAligned' function in your driver, and you
// can boot those allocations out of memory to make room for DirectDraw.
//
//-----------------------------------------------------------------------------

DWORD APIENTRY DdFreeDriverMemory(PDD_FREEDRIVERMEMORYDATA lpFreeDriverMemory)
{
    PPDEV pDev = (PPDEV)lpFreeDriverMemory->lpDD->dhpdev;
    DISPDBG((0, "%s: %p\n", __FUNCTION__, pDev));

    lpFreeDriverMemory->ddRVal = DDERR_OUTOFMEMORY;
    return DDHAL_DRIVER_HANDLED;
}

#ifdef VBOX_WITH_VIDEOHWACCEL
#ifndef DBG_DDSTUBS
DWORD APIENTRY DdSetColorKey(PDD_SETCOLORKEYDATA  lpSetColorKey)
{
    PPDEV pDev = (PPDEV)lpSetColorKey->lpDD->dhpdev;
    DD_SURFACE_LOCAL*   lpSurfaceLocal = lpSetColorKey->lpDDSurface;
    DD_SURFACE_GLOBAL*  lpSurfaceGlobal = lpSurfaceLocal->lpGbl;
    VBOXVHWACMD* pCmd;

    DISPDBG((0, "%s\n", __FUNCTION__));

    pCmd = vboxVHWACommandCreate (pDev, VBOXVHWACMD_TYPE_SURF_COLORKEY_SET, sizeof(VBOXVHWACMD_SURF_COLORKEY_SET));
    //    int rc = VERR_GENERAL_FAILURE;
    if(pCmd)
    {
        VBOXVHWACMD_SURF_COLORKEY_SET * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_COLORKEY_SET);
        PVBOXVHWASURFDESC pDesc = (PVBOXVHWASURFDESC)lpSurfaceGlobal->dwReserved1;
        memset(pBody, 0, sizeof(VBOXVHWACMD_SURF_COLORKEY_SET));

        pBody->u.in.offSurface = (uint64_t)lpSurfaceGlobal->fpVidMem;
        pBody->u.in.hSurf = pDesc->hHostHandle;
        pBody->u.in.flags = vboxVHWAFromDDCKEYs(lpSetColorKey->dwFlags);
        vboxVHWAFromDDCOLORKEY(&pBody->u.in.CKey, &lpSetColorKey->ckNew);

        vboxVHWACommandSubmitAsynchAndComplete(pDev, pCmd);
        lpSetColorKey->ddRVal = DD_OK;
    }
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY DdAddAttachedSurface(PDD_ADDATTACHEDSURFACEDATA  lpAddAttachedSurface)
{
    DISPDBG((0, "%s\n", __FUNCTION__));
    lpAddAttachedSurface->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY DdBlt(PDD_BLTDATA  lpBlt)
{
    PPDEV pDev = (PPDEV)lpBlt->lpDD->dhpdev;
    DISPDBG((0, "%s\n", __FUNCTION__));
#if DX9_DDI
    if(VBOXDD_CHECKFLAG(lpBlt->dwFlags, DDBLT_EXTENDED_PRESENTATION_STRETCHFACTOR))
    {
        lpBlt->ddRVal = DD_OK;
    }
    else
#endif
    {
        DD_SURFACE_LOCAL*   lpDestSurfaceLocal = lpBlt->lpDDDestSurface;
        DD_SURFACE_GLOBAL*  lpDestSurfaceGlobal = lpDestSurfaceLocal->lpGbl;
        DD_SURFACE_LOCAL*   lpSrcSurfaceLocal = lpBlt->lpDDSrcSurface;
        DD_SURFACE_GLOBAL*  lpSrcSurfaceGlobal = lpSrcSurfaceLocal->lpGbl;
        VBOXVHWACMD* pCmd = vboxVHWACommandCreate (pDev, VBOXVHWACMD_TYPE_SURF_BLT, sizeof(VBOXVHWACMD_SURF_BLT));
    //    int rc = VERR_GENERAL_FAILURE;
        if(pCmd)
        {
            VBOXVHWACMD_SURF_BLT * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_BLT);
            PVBOXVHWASURFDESC pSrcDesc = (PVBOXVHWASURFDESC)lpSrcSurfaceGlobal->dwReserved1;
            PVBOXVHWASURFDESC pDestDesc = (PVBOXVHWASURFDESC)lpDestSurfaceGlobal->dwReserved1;
            memset(pBody, 0, sizeof(VBOXVHWACMD_SURF_BLT));

            pBody->u.in.offSrcSurface = (uint64_t)lpSrcSurfaceGlobal->fpVidMem;
            pBody->u.in.offDstSurface = (uint64_t)lpDestSurfaceGlobal->fpVidMem;

            pBody->u.in.hDstSurf = pDestDesc->hHostHandle;
            vboxVHWAFromRECTL(&pBody->u.in.dstRect, &lpBlt->rDest);
            pBody->u.in.hSrcSurf = pSrcDesc->hHostHandle;
            vboxVHWAFromRECTL(&pBody->u.in.srcRect, &lpBlt->rSrc);
            pBody->DstGuestSurfInfo = (uint64_t)pDestDesc;
            pBody->SrcGuestSurfInfo = (uint64_t)pSrcDesc;

            pBody->u.in.flags = vboxVHWAFromDDBLTs(lpBlt->dwFlags);
            vboxVHWAFromDDBLTFX(&pBody->u.in.desc, &lpBlt->bltFX);

            ASMAtomicIncU32(&pSrcDesc->cPendingBltsSrc);
            ASMAtomicIncU32(&pDestDesc->cPendingBltsDst);

            vboxVHWARegionAdd(&pDestDesc->NonupdatedMemRegion, &lpBlt->rDest);
            vboxVHWARegionTrySubstitute(&pDestDesc->UpdatedMemRegion, &lpBlt->rDest);

            if(pSrcDesc->UpdatedMemRegion.bValid)
            {
                pBody->u.in.xUpdatedSrcMemValid = 1;
                vboxVHWAFromRECTL(&pBody->u.in.xUpdatedSrcMemRect, &pSrcDesc->UpdatedMemRegion.Rect);
                vboxVHWARegionClear(&pSrcDesc->UpdatedMemRegion);
            }

            vboxVHWACommandSubmitAsynch(pDev, pCmd, vboxVHWASurfBltCompletion, NULL);

            lpBlt->ddRVal = DD_OK;
        }
        else
        {
            lpBlt->ddRVal = DDERR_GENERIC;
        }
    }

    return DDHAL_DRIVER_HANDLED;
}

//DWORD APIENTRY DdDestroySurface(PDD_DESTROYSURFACEDATA  lpDestroySurface)
//{
//    DISPDBG((0, "%s\n", __FUNCTION__));
//    lpDestroySurface->ddRVal = DD_OK;
//    return DDHAL_DRIVER_HANDLED;
//}

DWORD APIENTRY DdFlip(PDD_FLIPDATA  lpFlip)
{
    PPDEV pDev = (PPDEV)lpFlip->lpDD->dhpdev;
    DD_SURFACE_LOCAL*   lpTargSurfaceLocal = lpFlip->lpSurfTarg;
    DD_SURFACE_GLOBAL*  lpTargSurfaceGlobal = lpTargSurfaceLocal->lpGbl;
    DD_SURFACE_LOCAL*   lpCurrSurfaceLocal = lpFlip->lpSurfCurr;
    DD_SURFACE_GLOBAL*  lpCurrSurfaceGlobal = lpCurrSurfaceLocal->lpGbl;
    PVBOXVHWASURFDESC pCurrDesc = (PVBOXVHWASURFDESC)lpCurrSurfaceGlobal->dwReserved1;
    PVBOXVHWASURFDESC pTargDesc = (PVBOXVHWASURFDESC)lpTargSurfaceGlobal->dwReserved1;
    VBOXVHWACMD* pCmd;

    DISPDBG((0, "%s\n", __FUNCTION__));

    if(
//                ASMAtomicUoReadU32(&pDesc->cPendingBltsSrc)
//                || ASMAtomicUoReadU32(&pDesc->cPendingBltsDst)
//                ||
            ASMAtomicUoReadU32(&pCurrDesc->cPendingFlipsTarg)
            || ASMAtomicUoReadU32(&pCurrDesc->cPendingFlipsCurr)
            || ASMAtomicUoReadU32(&pTargDesc->cPendingFlipsTarg)
            || ASMAtomicUoReadU32(&pTargDesc->cPendingFlipsCurr))
    {
        /* ensure we have host cmds processed to update pending blits and flips */
        vboxVHWACommandCheckHostCmds(pDev);
        if(
    //                ASMAtomicUoReadU32(&pDesc->cPendingBltsSrc)
    //                || ASMAtomicUoReadU32(&pDesc->cPendingBltsDst)
    //                ||
                    ASMAtomicUoReadU32(&pCurrDesc->cPendingFlipsTarg)
                    || ASMAtomicUoReadU32(&pCurrDesc->cPendingFlipsCurr)
                    || ASMAtomicUoReadU32(&pTargDesc->cPendingFlipsTarg)
                    || ASMAtomicUoReadU32(&pTargDesc->cPendingFlipsCurr))
        {
            lpFlip->ddRVal = DDERR_WASSTILLDRAWING;
            return DDHAL_DRIVER_HANDLED;
        }
    }


    pCmd = vboxVHWACommandCreate (pDev, VBOXVHWACMD_TYPE_SURF_FLIP, sizeof(VBOXVHWACMD_SURF_FLIP));
    //    int rc = VERR_GENERAL_FAILURE;
    if(pCmd)
    {
        VBOXVHWACMD_SURF_FLIP * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_FLIP);

        memset(pBody, 0, sizeof(VBOXVHWACMD_SURF_FLIP));

        pBody->u.in.offCurrSurface = (uint64_t)lpCurrSurfaceGlobal->fpVidMem;
        pBody->u.in.offTargSurface = (uint64_t)lpTargSurfaceGlobal->fpVidMem;

        pBody->u.in.hTargSurf = pTargDesc->hHostHandle;
        pBody->u.in.hCurrSurf = pCurrDesc->hHostHandle;
        pBody->TargGuestSurfInfo = (uint64_t)pTargDesc;
        pBody->CurrGuestSurfInfo = (uint64_t)pCurrDesc;

//        pBody->u.in.flags = vboxVHWAFromDDFLIPs(lpFlip->dwFlags);

        ASMAtomicIncU32(&pCurrDesc->cPendingFlipsCurr);
        ASMAtomicIncU32(&pTargDesc->cPendingFlipsTarg);
#ifdef DEBUG
        ASMAtomicIncU32(&pCurrDesc->cFlipsCurr);
        ASMAtomicIncU32(&pTargDesc->cFlipsTarg);
#endif

        if(pTargDesc->UpdatedMemRegion.bValid)
        {
            pBody->u.in.xUpdatedTargMemValid = 1;
            vboxVHWAFromRECTL(&pBody->u.in.xUpdatedTargMemRect, &pTargDesc->UpdatedMemRegion.Rect);
            vboxVHWARegionClear(&pTargDesc->UpdatedMemRegion);
        }

        vboxVHWACommandSubmitAsynch(pDev, pCmd, vboxVHWASurfFlipCompletion, NULL);

        lpFlip->ddRVal = DD_OK;
    }
    else
    {
        lpFlip->ddRVal = DDERR_GENERIC;
    }
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY DdGetBltStatus(PDD_GETBLTSTATUSDATA  lpGetBltStatus)
{
    PPDEV pDev = (PPDEV)lpGetBltStatus->lpDD->dhpdev;

    DISPDBG((0, "%s\n", __FUNCTION__));

    if(lpGetBltStatus->dwFlags == DDGBS_CANBLT)
    {
        lpGetBltStatus->ddRVal = DD_OK;
    }
    else /* DDGBS_ISBLTDONE */
    {
        DD_SURFACE_LOCAL*   lpSurfaceLocal = lpGetBltStatus->lpDDSurface;
        DD_SURFACE_GLOBAL*  lpSurfaceGlobal = lpSurfaceLocal->lpGbl;
        PVBOXVHWASURFDESC pDesc = (PVBOXVHWASURFDESC)lpSurfaceGlobal->dwReserved1;

        if(
                    ASMAtomicUoReadU32(&pDesc->cPendingBltsSrc)
                    || ASMAtomicUoReadU32(&pDesc->cPendingBltsDst)
    //                || ASMAtomicUoReadU32(&pDesc->cPendingFlipsTarg)
    //                || ASMAtomicUoReadU32(&pDesc->cPendingFlipsCurr)
                    )
        {
            /* ensure we have host cmds processed to update pending blits and flips */
            vboxVHWACommandCheckHostCmds(pDev);

            if(
                        ASMAtomicUoReadU32(&pDesc->cPendingBltsSrc)
                        || ASMAtomicUoReadU32(&pDesc->cPendingBltsDst)
        //                || ASMAtomicUoReadU32(&pDesc->cPendingFlipsTarg)
        //                || ASMAtomicUoReadU32(&pDesc->cPendingFlipsCurr)
                        )
            {
                lpGetBltStatus->ddRVal = DDERR_WASSTILLDRAWING;
            }
            else
            {
                lpGetBltStatus->ddRVal = DD_OK;
            }
        }
        else
        {
            lpGetBltStatus->ddRVal = DD_OK;
        }
    }

    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY DdGetFlipStatus(PDD_GETFLIPSTATUSDATA  lpGetFlipStatus)
{
    PPDEV pDev = (PPDEV)lpGetFlipStatus->lpDD->dhpdev;
    DD_SURFACE_LOCAL*   lpSurfaceLocal = lpGetFlipStatus->lpDDSurface;
    DD_SURFACE_GLOBAL*  lpSurfaceGlobal = lpSurfaceLocal->lpGbl;
    PVBOXVHWASURFDESC pDesc = (PVBOXVHWASURFDESC)lpSurfaceGlobal->dwReserved1;

    DISPDBG((0, "%s\n", __FUNCTION__));

    if(
//                ASMAtomicUoReadU32(&pDesc->cPendingBltsSrc)
//                || ASMAtomicUoReadU32(&pDesc->cPendingBltsDst)
//                ||
                ASMAtomicUoReadU32(&pDesc->cPendingFlipsTarg)
                || ASMAtomicUoReadU32(&pDesc->cPendingFlipsCurr)
                )
    {
        /* ensure we have host cmds processed to update pending blits and flips */
        vboxVHWACommandCheckHostCmds(pDev);

        if(
    //                ASMAtomicUoReadU32(&pDesc->cPendingBltsSrc)
    //                || ASMAtomicUoReadU32(&pDesc->cPendingBltsDst)
    //                ||
                    ASMAtomicUoReadU32(&pDesc->cPendingFlipsTarg)
                    || ASMAtomicUoReadU32(&pDesc->cPendingFlipsCurr)
                    )
        {
            lpGetFlipStatus->ddRVal = DDERR_WASSTILLDRAWING;
        }
        else
        {
            lpGetFlipStatus->ddRVal = DD_OK;
        }
    }
    else
    {
        lpGetFlipStatus->ddRVal = DD_OK;
    }

//    if(lpGetFlipStatus->dwFlags == DDGFS_CANFLIP)
//    {
//        lpGetFlipStatus->ddRVal = DD_OK;
//    }
//    else
//    {
//        lpGetFlipStatus->ddRVal = DD_OK;
//    }

    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY DdSetOverlayPosition(PDD_SETOVERLAYPOSITIONDATA  lpSetOverlayPosition)
{
    PPDEV pDev = (PPDEV)lpSetOverlayPosition->lpDD->dhpdev;
    DD_SURFACE_LOCAL*   lpDestSurfaceLocal = lpSetOverlayPosition->lpDDDestSurface;
    DD_SURFACE_GLOBAL*  lpDestSurfaceGlobal = lpDestSurfaceLocal->lpGbl;
    DD_SURFACE_LOCAL*   lpSrcSurfaceLocal = lpSetOverlayPosition->lpDDSrcSurface;
    DD_SURFACE_GLOBAL*  lpSrcSurfaceGlobal = lpSrcSurfaceLocal->lpGbl;
    VBOXVHWACMD* pCmd;
    PVBOXVHWASURFDESC pSrcDesc = (PVBOXVHWASURFDESC)lpSrcSurfaceGlobal->dwReserved1;
    PVBOXVHWASURFDESC pDestDesc = (PVBOXVHWASURFDESC)lpDestSurfaceGlobal->dwReserved1;

    DISPDBG((0, "%s\n", __FUNCTION__));

    if(!pSrcDesc->bVisible)
    {
        lpSetOverlayPosition->ddRVal = DDERR_GENERIC;
        return DDHAL_DRIVER_HANDLED;
    }

    pCmd = vboxVHWACommandCreate (pDev, VBOXVHWACMD_TYPE_SURF_OVERLAY_SETPOSITION, sizeof(VBOXVHWACMD_SURF_OVERLAY_SETPOSITION));
    //    int rc = VERR_GENERAL_FAILURE;
    if(pCmd)
    {
        VBOXVHWACMD_SURF_OVERLAY_SETPOSITION * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_OVERLAY_SETPOSITION);

        memset(pBody, 0, sizeof(VBOXVHWACMD_SURF_OVERLAY_SETPOSITION));

        pBody->u.in.offSrcSurface = (uint64_t)lpSrcSurfaceGlobal->fpVidMem;
        pBody->u.in.offDstSurface = (uint64_t)lpDestSurfaceGlobal->fpVidMem;

        pBody->u.in.hDstSurf = pDestDesc->hHostHandle;
        pBody->u.in.hSrcSurf = pSrcDesc->hHostHandle;

        pBody->u.in.xPos = lpSetOverlayPosition->lXPos;
        pBody->u.in.yPos = lpSetOverlayPosition->lYPos;

        vboxVHWACommandSubmitAsynchAndComplete(pDev, pCmd);
        lpSetOverlayPosition->ddRVal = DD_OK;
    }

    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY DdUpdateOverlay(PDD_UPDATEOVERLAYDATA  lpUpdateOverlay)
{
    PPDEV pDev = (PPDEV)lpUpdateOverlay->lpDD->dhpdev;
    DD_SURFACE_LOCAL*   lpDestSurfaceLocal = lpUpdateOverlay->lpDDDestSurface;
    DD_SURFACE_LOCAL*   lpSrcSurfaceLocal = lpUpdateOverlay->lpDDSrcSurface;
    DD_SURFACE_GLOBAL*  lpSrcSurfaceGlobal = lpSrcSurfaceLocal->lpGbl;
    VBOXVHWACMD* pCmd;
    PVBOXVHWASURFDESC pSrcDesc = (PVBOXVHWASURFDESC)lpSrcSurfaceGlobal->dwReserved1;

    DISPDBG((0, "%s\n", __FUNCTION__));

//    if(!pSrcDesc->bVisible)
//    {
//        lpUpdateOverlay->ddRVal = DDERR_GENERIC;
//        return DDHAL_DRIVER_HANDLED;
//    }

    pCmd = vboxVHWACommandCreate (pDev, VBOXVHWACMD_TYPE_SURF_OVERLAY_UPDATE, sizeof(VBOXVHWACMD_SURF_OVERLAY_UPDATE));
    //    int rc = VERR_GENERAL_FAILURE;
    if(pCmd)
    {
        VBOXVHWACMD_SURF_OVERLAY_UPDATE * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_OVERLAY_UPDATE);

        memset(pBody, 0, sizeof(VBOXVHWACMD_SURF_OVERLAY_UPDATE));

        pBody->u.in.offSrcSurface = (uint64_t)lpSrcSurfaceGlobal->fpVidMem;

        pBody->u.in.hSrcSurf = pSrcDesc->hHostHandle;

        vboxVHWAFromRECTL(&pBody->u.in.dstRect, &lpUpdateOverlay->rDest);
        vboxVHWAFromRECTL(&pBody->u.in.srcRect, &lpUpdateOverlay->rSrc);

        pBody->u.in.flags = vboxVHWAFromDDOVERs(lpUpdateOverlay->dwFlags);
        vboxVHWAFromDDOVERLAYFX(&pBody->u.in.desc, &lpUpdateOverlay->overlayFX);

        if(lpUpdateOverlay->dwFlags & DDOVER_HIDE)
        {
            pSrcDesc->bVisible = false;
        }
        else if(lpUpdateOverlay->dwFlags & DDOVER_SHOW)
        {
            pSrcDesc->bVisible = true;
            if(pSrcDesc->UpdatedMemRegion.bValid)
            {
                pBody->u.in.xUpdatedSrcMemValid = 1;
                vboxVHWAFromRECTL(&pBody->u.in.xUpdatedSrcMemRect, &pSrcDesc->UpdatedMemRegion.Rect);
                vboxVHWARegionClear(&pSrcDesc->UpdatedMemRegion);
            }
        }

        if(lpDestSurfaceLocal)
        {
            DD_SURFACE_GLOBAL* lpDestSurfaceGlobal = lpDestSurfaceLocal->lpGbl;
            PVBOXVHWASURFDESC pDestDesc = (PVBOXVHWASURFDESC)lpDestSurfaceGlobal->dwReserved1;
            pBody->u.in.hDstSurf = pDestDesc->hHostHandle;
            pBody->u.in.offDstSurface = (uint64_t)lpDestSurfaceGlobal->fpVidMem;
        }

        vboxVHWACommandSubmitAsynchAndComplete(pDev, pCmd);
        lpUpdateOverlay->ddRVal = DD_OK;
    }

    return DDHAL_DRIVER_HANDLED;
}
#else
DWORD APIENTRY DdSetColorKey(PDD_SETCOLORKEYDATA  lpSetColorKey)
{
    DISPDBG((0, "%s\n", __FUNCTION__));
    lpSetColorKey->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY DdAddAttachedSurface(PDD_ADDATTACHEDSURFACEDATA  lpAddAttachedSurface)
{
    DISPDBG((0, "%s\n", __FUNCTION__));
    lpAddAttachedSurface->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY DdBlt(PDD_BLTDATA  lpBlt)
{
    DISPDBG((0, "%s\n", __FUNCTION__));
    lpBlt->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

//DWORD APIENTRY DdDestroySurface(PDD_DESTROYSURFACEDATA  lpDestroySurface)
//{
//    DISPDBG((0, "%s\n", __FUNCTION__));
//    lpDestroySurface->ddRVal = DD_OK;
//    return DDHAL_DRIVER_HANDLED;
//}

DWORD APIENTRY DdFlip(PDD_FLIPDATA  lpFlip)
{
    DISPDBG((0, "%s\n", __FUNCTION__));
    lpFlip->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY DdGetBltStatus(PDD_GETBLTSTATUSDATA  lpGetBltStatus)
{
    DISPDBG((0, "%s\n", __FUNCTION__));

    if(lpGetBltStatus->dwFlags == DDGBS_CANBLT)
    {
        lpGetBltStatus->ddRVal = DD_OK;
    }
    else
    {
        lpGetBltStatus->ddRVal = DD_OK;
    }

    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY DdGetFlipStatus(PDD_GETFLIPSTATUSDATA  lpGetFlipStatus)
{
    DISPDBG((0, "%s\n", __FUNCTION__));
    if(lpGetFlipStatus->dwFlags == DDGFS_CANFLIP)
    {
        lpGetFlipStatus->ddRVal = DD_OK;
    }
    else
    {
        lpGetFlipStatus->ddRVal = DD_OK;
    }

    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY DdSetOverlayPosition(PDD_SETOVERLAYPOSITIONDATA  lpSetOverlayPosition)
{
    DISPDBG((0, "%s\n", __FUNCTION__));

    lpSetOverlayPosition->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY DdUpdateOverlay(PDD_UPDATEOVERLAYDATA  lpUpdateOverlay)
{
    DISPDBG((0, "%s\n", __FUNCTION__));

    lpUpdateOverlay->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

#endif

//-----------------------------------------------------------------------------
// setupRops
//
// Build array for supported ROPS
//-----------------------------------------------------------------------------
static void
setupRops(
    LPBYTE proplist,
    LPDWORD proptable,
    int cnt )
{
    int         i;
    DWORD       idx;
    DWORD       bit;
    DWORD       rop;

    for(i=0; i<cnt; i++)
    {
        rop = proplist[i];
        idx = rop / 32;
        bit = 1L << ((DWORD)(rop % 32));
        proptable[idx] |= bit;
    }

} // setupRops

//-----------------------------------------------------------------------------
//
// Function: __GetDDHALInfo
//
// Returns: void
//
// Description:
//
// Takes a pointer to a partially or fully filled in pThisDisplay and a pointer
// to an empty DDHALINFO and fills in the DDHALINFO.  This eases porting to NT
// and means that caps changes are done in only one place.  The pThisDisplay
// may not be fully constructed here, so you should only:
// a) Query the registry
// b) DISPDBG
// If you need to add anything to pThisDisplay for NT, you should fill it in
// during the DrvGetDirectDraw call.
//
// The problem here is when the code is run on NT.  If there was any other way...
//
// The following caps have been found to cause NT to bail....
// DDCAPS_GDI, DDFXCAPS_BLTMIRRORUPDOWN, DDFXCAPS_BLTMIRRORLEFTRIGHT
//
//
//-----------------------------------------------------------------------------

//
// use bits to indicate which ROPs you support.
//
// DWORD 0, bit 0 == ROP 0
// DWORD 8, bit 31 == ROP 255
//

//static DWORD ropsAGP[DD_ROP_SPACE] = { 0 };
static BYTE ropListNT[] =
{
    SRCCOPY >> 16
//    WHITENESS >> 16,
//        BLACKNESS >> 16
};

static DWORD rops[DD_ROP_SPACE] = { 0 };

#if 1
static bool
getDDHALInfo(
    PPDEV pDev,
    DD_HALINFO* pHALInfo)
{
    int i;
    if(!VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_BLT) && !VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_OVERLAY))
        return false;

    pHALInfo->ddCaps.dwCaps |= vboxVHWAToDDCAPS(pDev->vhwaInfo.caps);

    if(VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_BLT))
    {
        // Setup the ROPS we do.
        //TODO: hardcoded for now
        setupRops( ropListNT,
                     rops,
                     sizeof(ropListNT)/sizeof(ropListNT[0]));

        //
        // ROPS supported
        //
        for( i=0;i<DD_ROP_SPACE;i++ )
        {
            pHALInfo->ddCaps.dwRops[i] = rops[i];
        }
    }

//                                | DDCAPS_READSCANLINE


    pHALInfo->ddCaps.ddsCaps.dwCaps |= vboxVHWAToDDSCAPS(pDev->vhwaInfo.surfaceCaps);

//disabled
//    pHALInfo->ddCaps.dwCaps |= DDCAPS_3D           |
//                               DDCAPS_BLTDEPTHFILL;
//
//    pHALInfo->ddCaps.ddsCaps.dwCaps |= DDSCAPS_3DDEVICE |
//                                       DDSCAPS_ZBUFFER |
//                                       DDSCAPS_ALPHA;
    pHALInfo->ddCaps.dwCaps2 = vboxVHWAToDDCAPS2(pDev->vhwaInfo.caps2);



//#if DX7_TEXMANAGEMENT
    // We need to set this bit up in order to be able to do
    // out own texture management
//    pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_CANMANAGETEXTURE;
//#if DX8_DDI
//    pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_CANMANAGERESOURCE;
//#endif
//#endif

//#if DX8_DDI
    // We need to flag we can run in windowed mode, otherwise we
    // might get restricted by apps to run in fullscreen only
//    pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_CANRENDERWINDOWED;

    // Also permit surfaces wider than the display buffer.
//    pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_WIDESURFACES;
//#endif

//#if DX8_DDI
    // We need to flag we support dynamic textures. That is , apps can
        // lock with high frequency video memory textures without paying a
        // penalty for it. Since on this sample driver we only support
        // linear memory formats for textures we don't need to do anything
        // else for this support. Otherwise we would have to keep two surfaces
        // for textures created with the DDSCAPS2_HINTDYNAMIC hint in order
        // to efficiently do the linear<->swizzled transformation or keep the
        // texture permanantly in an unswizzled state.
//        pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_DYNAMICTEXTURES;
    #if DX9_DDI
        // Notice that dynamic textures MUST be supported in order to instantiate a DX9 device
    #endif // DX9_DDI
//#endif

//    pHALInfo->ddCaps.dwFXCaps = 0;

    // P3RX can do:
    // 1. Stretching/Shrinking
    // 2. YUV->RGB conversion
    // 3. Mirroring in X and Y
    // 4. ColorKeying from a source color and a source color space
    if(VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_BLT)
            && VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_BLTSTRETCH))
    {
        // Special effects caps
        //TODO: filter them out
        pHALInfo->ddCaps.dwFXCaps |= DDFXCAPS_BLTSTRETCHY  |
                                    DDFXCAPS_BLTSTRETCHX  |
                                    DDFXCAPS_BLTSTRETCHYN |
                                    DDFXCAPS_BLTSTRETCHXN |
                                    DDFXCAPS_BLTSHRINKY   |
                                    DDFXCAPS_BLTSHRINKX   |
                                    DDFXCAPS_BLTSHRINKYN  |
                                    DDFXCAPS_BLTSHRINKXN;

        //        DDFXCAPS_BLTARITHSTRETCHY
        //        DDFXCAPS_BLTARITHSTRETCHYN
        //        DDFXCAPS_BLTMIRRORLEFTRIGHT
        //        DDFXCAPS_BLTMIRRORUPDOWN
        //        DDFXCAPS_BLTROTATION90
    }

    if(VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_OVERLAY)
            && VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_OVERLAYSTRETCH))
    {
        // Special effects caps
        //TODO: filter them out
        pHALInfo->ddCaps.dwFXCaps |= DDFXCAPS_OVERLAYSTRETCHY  |
                                    DDFXCAPS_OVERLAYSTRETCHX  |
                                    DDFXCAPS_OVERLAYSTRETCHYN |
                                    DDFXCAPS_OVERLAYSTRETCHXN |
                                    DDFXCAPS_OVERLAYSHRINKY   |
                                    DDFXCAPS_OVERLAYSHRINKX   |
                                    DDFXCAPS_OVERLAYSHRINKYN  |
                                    DDFXCAPS_OVERLAYSHRINKXN;

        //        DDFXCAPS_OVERLAYARITHSTRETCHY
        //        DDFXCAPS_OVERLAYARITHSTRETCHYN
        //        DDFXCAPS_OVERLAYMIRRORLEFTRIGHT
        //        DDFXCAPS_OVERLAYMIRRORUPDOWN

    }

//    if(VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_BLT)
//            && VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_BLTFOURCC))
//    {
//        pHALInfo->ddCaps.dwCaps |= DDCAPS_BLTFOURCC;
//
//        // Enable copy blts between Four CC formats for DShow acceleration
//        pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_COPYFOURCC;
//    }

//    if((VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_BLT) || VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_OVERLAY))
//            && VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_COLORKEY))
    {
        pHALInfo->ddCaps.dwCKeyCaps = vboxVHWAToDDCKEYCAPS(pDev->vhwaInfo.colorKeyCaps);
    }

//    pHALInfo->ddCaps.dwSVBCaps = DDCAPS_BLT;

//    // We can do a texture from sysmem to video mem.
//    pHALInfo->ddCaps.dwSVBCKeyCaps |= DDCKEYCAPS_DESTBLT         |
//                                     DDCKEYCAPS_DESTBLTCLRSPACE;
    pHALInfo->ddCaps.dwSVBFXCaps = 0;

//    // Fill in the sysmem->vidmem rops (only can copy);
//    for( i=0;i<DD_ROP_SPACE;i++ )
//    {
//        pHALInfo->ddCaps.dwSVBRops[i] = rops[i];
//    }



//disabled
//    pHALInfo->ddCaps.ddsCaps.dwCaps |= DDSCAPS_TEXTURE;

//#if DX7_STEREO
//    // Report the stereo capability back to runtime
//    pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_STEREO;
//    pHALInfo->ddCaps.dwSVCaps = DDSVCAPS_STEREOSEQUENTIAL;
//#endif

    // Z Buffer is only 16 Bits
//    pHALInfo->ddCaps.dwZBufferBitDepths = DDBD_16;
//    pHALInfo->ddCaps.ddsCaps.dwCaps |= DDSCAPS_MIPMAP;

    {
//#ifdef SUPPORT_VIDEOPORT
//        // We support 1 video port.  Must set CurrVideoPorts to 0
//        // We can't do interleaved bobbing yet - maybe in the future.
//        pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_VIDEOPORT            |
//                                    DDCAPS2_CANBOBNONINTERLEAVED;
//
//        pHALInfo->ddCaps.dwMaxVideoPorts = 1;
//        pHALInfo->ddCaps.dwCurrVideoPorts = 0;
//
//
//#endif // SUPPORT_VIDEOPORT

        if(VBOXVHWA_CAP(pDev, VBOXVHWA_CAPS_OVERLAY)) /* no overlay support for now */
        {
            // Overlay is free to use.
            pHALInfo->ddCaps.dwMaxVisibleOverlays = pDev->vhwaInfo.numOverlays;
            pHALInfo->ddCaps.dwCurrVisibleOverlays = 0;

            // Indicates that Perm3 has no stretch ratio limitation
            pHALInfo->ddCaps.dwMinOverlayStretch = 1;
            pHALInfo->ddCaps.dwMaxOverlayStretch = 32000;
        }
    }

//#ifdef W95_DDRAW
//#ifdef USE_DD_CONTROL_COLOR
//    // Enable colour control asc brightness, contrast, gamma.
//    pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_COLORCONTROLPRIMARY;
//#endif
//#endif


    // Won't do Video-Sys mem Blits.
    pHALInfo->ddCaps.dwVSBCaps = 0;
    pHALInfo->ddCaps.dwVSBCKeyCaps = 0;
    pHALInfo->ddCaps.dwVSBFXCaps = 0;
    for( i=0;i<DD_ROP_SPACE;i++ )
    {
        pHALInfo->ddCaps.dwVSBRops[i] = 0;
    }

    // Won't do Sys-Sys mem Blits
    pHALInfo->ddCaps.dwSSBCaps = 0;
    pHALInfo->ddCaps.dwSSBCKeyCaps = 0;
    pHALInfo->ddCaps.dwSSBFXCaps = 0;
    for( i=0;i<DD_ROP_SPACE;i++ )
    {
        pHALInfo->ddCaps.dwSSBRops[i] = 0;
    }

    //
    // bit depths supported for alpha and Z
    //
//    pHALInfo->ddCaps.dwAlphaBltConstBitDepths = DDBD_2 |
//                                                DDBD_4 |
//                                                DDBD_8;
//
//    pHALInfo->ddCaps.dwAlphaBltPixelBitDepths = DDBD_1 |
//                                                DDBD_8;
//    pHALInfo->ddCaps.dwAlphaBltSurfaceBitDepths = DDBD_1 |
//                                                  DDBD_2 |
//                                                  DDBD_4 |
//                                                  DDBD_8;

//disabled
//    // No alpha blending for overlays, so I'm not sure what these should be.
//    // Because we support 32bpp overlays, it's just that you can't use the
//    // alpha bits for blending. Pass.
//    pHALInfo->ddCaps.dwAlphaBltConstBitDepths = DDBD_2 |
//                                                DDBD_4 |
//                                                DDBD_8;
//
//    pHALInfo->ddCaps.dwAlphaBltPixelBitDepths = DDBD_1 |
//                                                DDBD_8;
//
//    pHALInfo->ddCaps.dwAlphaBltSurfaceBitDepths = DDBD_1 |
//                                                  DDBD_2 |
//                                                  DDBD_4 |
//                                                  DDBD_8;


//Reenable:    // For DX5 and beyond we support this new informational callback.
//    pHALInfo->GetDriverInfo = DdGetDriverInfo;
//    pHALInfo->dwFlags |= DDHALINFO_GETDRIVERINFOSET;
//
//#if DX8_DDI
//    // Flag our support for a new class of GUIDs that may come through
//    // GetDriverInfo for DX8 drivers. (This support will be compulsory)
//    pHALInfo->dwFlags |= DDHALINFO_GETDRIVERINFO2;
//#endif DX8_DDI

    return true;
} // getDDHALInfo
#else
bool
getDDHALInfo2(
    PPDEV pDev,
    DD_HALINFO* pHALInfo)
{
    int i;

#if 0
    /* TODO: only enable features supported by the host backend & host hw
     * for now this just combines all caps we might use */

    // Setup the ROPS we do.
    setupRops( ropListNT,
                 rops,
                 sizeof(ropListNT)/sizeof(ropListNT[0]));

    // The most basic DirectDraw functionality
    pHALInfo->ddCaps.dwCaps |=   DDCAPS_BLT
                                 | DDCAPS_BLTQUEUE
                                 | DDCAPS_BLTCOLORFILL
//                                | DDCAPS_READSCANLINE
                                ;
#endif
    pHALInfo->ddCaps.ddsCaps.dwCaps |=   DDSCAPS_OFFSCREENPLAIN
                                         | DDSCAPS_PRIMARYSURFACE
                                         | DDSCAPS_FLIP;
#if 0
    pHALInfo->ddCaps.dwCaps |= DDCAPS_3D           |
                               DDCAPS_BLTDEPTHFILL;

    pHALInfo->ddCaps.ddsCaps.dwCaps |= DDSCAPS_3DDEVICE |
                                       DDSCAPS_ZBUFFER |
                                       DDSCAPS_ALPHA;
#endif
    pHALInfo->ddCaps.dwCaps2 = 0;

//#if DX7_TEXMANAGEMENT
    // We need to set this bit up in order to be able to do
    // out own texture management
//    pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_CANMANAGETEXTURE;
//#if DX8_DDI
//    pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_CANMANAGERESOURCE;
//#endif
//#endif

//#if DX8_DDI
    // We need to flag we can run in windowed mode, otherwise we
    // might get restricted by apps to run in fullscreen only
    pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_CANRENDERWINDOWED;
//#endif

//#if DX8_DDI
    // We need to flag we support dynamic textures. That is , apps can
        // lock with high frequency video memory textures without paying a
        // penalty for it. Since on this sample driver we only support
        // linear memory formats for textures we don't need to do anything
        // else for this support. Otherwise we would have to keep two surfaces
        // for textures created with the DDSCAPS2_HINTDYNAMIC hint in order
        // to efficiently do the linear<->swizzled transformation or keep the
        // texture permanantly in an unswizzled state.
//        pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_DYNAMICTEXTURES;
    #if DX9_DDI
        // Notice that dynamic textures MUST be supported in order to instantiate a DX9 device
    #endif // DX9_DDI
//#endif

//    pHALInfo->ddCaps.dwFXCaps = 0;

    // P3RX can do:
    // 1. Stretching/Shrinking
    // 2. YUV->RGB conversion
    // 3. Mirroring in X and Y
    // 4. ColorKeying from a source color and a source color space
    pHALInfo->ddCaps.dwCaps |=
#if 0
        DDCAPS_BLTSTRETCH
                               | DDCAPS_BLTFOURCC
                               |
#endif
                               DDCAPS_COLORKEY
//                               | DDCAPS_CANBLTSYSMEM
                               ;
#if 0
    // Special effects caps
    pHALInfo->ddCaps.dwFXCaps |= DDFXCAPS_BLTSTRETCHY  |
                                DDFXCAPS_BLTSTRETCHX  |
                                DDFXCAPS_BLTSTRETCHYN |
                                DDFXCAPS_BLTSTRETCHXN |
                                DDFXCAPS_BLTSHRINKY   |
                                DDFXCAPS_BLTSHRINKX   |
                                DDFXCAPS_BLTSHRINKYN  |
                                DDFXCAPS_BLTSHRINKXN;

    // ColorKey caps
    pHALInfo->ddCaps.dwCKeyCaps |= DDCKEYCAPS_SRCBLT         |
                                  DDCKEYCAPS_SRCBLTCLRSPACE |
                                  DDCKEYCAPS_DESTBLT        |
                                  DDCKEYCAPS_DESTBLTCLRSPACE;
#endif
//    pHALInfo->ddCaps.dwSVBCaps = DDCAPS_BLT;

//    // We can do a texture from sysmem to video mem.
//    pHALInfo->ddCaps.dwSVBCKeyCaps |= DDCKEYCAPS_DESTBLT         |
//                                     DDCKEYCAPS_DESTBLTCLRSPACE;
    pHALInfo->ddCaps.dwSVBFXCaps = 0;

//    // Fill in the sysmem->vidmem rops (only can copy);
//    for( i=0;i<DD_ROP_SPACE;i++ )
//    {
//        pHALInfo->ddCaps.dwSVBRops[i] = rops[i];
//    }
#if 0
    //mirroring with blitting
    pHALInfo->ddCaps.dwFXCaps |= DDFXCAPS_BLTMIRRORUPDOWN  |
                                DDFXCAPS_BLTMIRRORLEFTRIGHT;

    pHALInfo->ddCaps.dwCKeyCaps |=  DDCKEYCAPS_SRCBLTCLRSPACEYUV;

    pHALInfo->ddCaps.ddsCaps.dwCaps |= DDSCAPS_TEXTURE;
#endif
//#if DX7_STEREO
//    // Report the stereo capability back to runtime
//    pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_STEREO;
//    pHALInfo->ddCaps.dwSVCaps = DDSVCAPS_STEREOSEQUENTIAL;
//#endif

    // Z Buffer is only 16 Bits
//    pHALInfo->ddCaps.dwZBufferBitDepths = DDBD_16;
//    pHALInfo->ddCaps.ddsCaps.dwCaps |= DDSCAPS_MIPMAP;

        pHALInfo->ddCaps.ddsCaps.dwCaps |= DDSCAPS_LOCALVIDMEM;

    {
//#ifdef SUPPORT_VIDEOPORT
//        // We support 1 video port.  Must set CurrVideoPorts to 0
//        // We can't do interleaved bobbing yet - maybe in the future.
//        pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_VIDEOPORT            |
//                                    DDCAPS2_CANBOBNONINTERLEAVED;
//
//        pHALInfo->ddCaps.dwMaxVideoPorts = 1;
//        pHALInfo->ddCaps.dwCurrVideoPorts = 0;
//
//
//#endif // SUPPORT_VIDEOPORT


        {
            // Overlay is free to use.
            pHALInfo->ddCaps.dwMaxVisibleOverlays = 1;
            pHALInfo->ddCaps.dwCurrVisibleOverlays = 0;

            pHALInfo->ddCaps.dwCaps |=  DDCAPS_OVERLAY          |
                                        DDCAPS_OVERLAYFOURCC    |
                                        DDCAPS_OVERLAYSTRETCH   |
                                        DDCAPS_COLORKEYHWASSIST |
                                        DDCAPS_OVERLAYCANTCLIP;

            pHALInfo->ddCaps.dwCKeyCaps |= DDCKEYCAPS_SRCOVERLAY           |
                                           DDCKEYCAPS_SRCOVERLAYONEACTIVE  |
                                           DDCKEYCAPS_SRCOVERLAYYUV        |
                                           DDCKEYCAPS_DESTOVERLAY          |
                                           DDCKEYCAPS_DESTOVERLAYONEACTIVE |
                                           DDCKEYCAPS_DESTOVERLAYYUV;

            pHALInfo->ddCaps.ddsCaps.dwCaps |= DDSCAPS_OVERLAY;

            pHALInfo->ddCaps.dwFXCaps |= DDFXCAPS_OVERLAYSHRINKX   |
                                         DDFXCAPS_OVERLAYSHRINKXN  |
                                         DDFXCAPS_OVERLAYSHRINKY   |
                                         DDFXCAPS_OVERLAYSHRINKYN  |
                                         DDFXCAPS_OVERLAYSTRETCHX  |
                                         DDFXCAPS_OVERLAYSTRETCHXN |
                                         DDFXCAPS_OVERLAYSTRETCHY  |
                                         DDFXCAPS_OVERLAYSTRETCHYN;

            // Indicates that Perm3 has no stretch ratio limitation
            pHALInfo->ddCaps.dwMinOverlayStretch = 1;
            pHALInfo->ddCaps.dwMaxOverlayStretch = 32000;
        }
    }

//#ifdef W95_DDRAW
//#ifdef USE_DD_CONTROL_COLOR
//    // Enable colour control asc brightness, contrast, gamma.
//    pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_COLORCONTROLPRIMARY;
//#endif
//#endif

    // Also permit surfaces wider than the display buffer.
    pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_WIDESURFACES;
#if 0
    // Enable copy blts betweemn Four CC formats for DShow acceleration
    pHALInfo->ddCaps.dwCaps2 |= DDCAPS2_COPYFOURCC;
#endif
    // Won't do Video-Sys mem Blits.
    pHALInfo->ddCaps.dwVSBCaps = 0;
    pHALInfo->ddCaps.dwVSBCKeyCaps = 0;
    pHALInfo->ddCaps.dwVSBFXCaps = 0;
    for( i=0;i<DD_ROP_SPACE;i++ )
    {
        pHALInfo->ddCaps.dwVSBRops[i] = 0;
    }

    // Won't do Sys-Sys mem Blits
    pHALInfo->ddCaps.dwSSBCaps = 0;
    pHALInfo->ddCaps.dwSSBCKeyCaps = 0;
    pHALInfo->ddCaps.dwSSBFXCaps = 0;
    for( i=0;i<DD_ROP_SPACE;i++ )
    {
        pHALInfo->ddCaps.dwSSBRops[i] = 0;
    }

    //
    // bit depths supported for alpha and Z
    //
//    pHALInfo->ddCaps.dwAlphaBltConstBitDepths = DDBD_2 |
//                                                DDBD_4 |
//                                                DDBD_8;
//
//    pHALInfo->ddCaps.dwAlphaBltPixelBitDepths = DDBD_1 |
//                                                DDBD_8;
//    pHALInfo->ddCaps.dwAlphaBltSurfaceBitDepths = DDBD_1 |
//                                                  DDBD_2 |
//                                                  DDBD_4 |
//                                                  DDBD_8;
#if 0
    // No alpha blending for overlays, so I'm not sure what these should be.
    // Because we support 32bpp overlays, it's just that you can't use the
    // alpha bits for blending. Pass.
    pHALInfo->ddCaps.dwAlphaBltConstBitDepths = DDBD_2 |
                                                DDBD_4 |
                                                DDBD_8;

    pHALInfo->ddCaps.dwAlphaBltPixelBitDepths = DDBD_1 |
                                                DDBD_8;

    pHALInfo->ddCaps.dwAlphaBltSurfaceBitDepths = DDBD_1 |
                                                  DDBD_2 |
                                                  DDBD_4 |
                                                  DDBD_8;

    //
    // ROPS supported
    //
    for( i=0;i<DD_ROP_SPACE;i++ )
    {
        pHALInfo->ddCaps.dwRops[i] = rops[i];
    }
#endif
//Reenable:    // For DX5 and beyond we support this new informational callback.
//    pHALInfo->GetDriverInfo = DdGetDriverInfo;
//    pHALInfo->dwFlags |= DDHALINFO_GETDRIVERINFOSET;
//
//#if DX8_DDI
//    // Flag our support for a new class of GUIDs that may come through
//    // GetDriverInfo for DX8 drivers. (This support will be compulsory)
//    pHALInfo->dwFlags |= DDHALINFO_GETDRIVERINFO2;
//#endif DX8_DDI

    return true;
} // getDDHALInfo

#endif

static DECLCALLBACK(void) vboxVHWASurfBltCompletion(PPDEV ppdev, VBOXVHWACMD * pCmd, void * pContext)
{
    VBOXVHWACMD_SURF_BLT * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_BLT);
    PVBOXVHWASURFDESC pSrcDesc = (PVBOXVHWASURFDESC)pBody->SrcGuestSurfInfo;
    PVBOXVHWASURFDESC pDestDesc = (PVBOXVHWASURFDESC)pBody->DstGuestSurfInfo;

    ASMAtomicDecU32(&pSrcDesc->cPendingBltsSrc);
    ASMAtomicDecU32(&pDestDesc->cPendingBltsDst);

    vbvaVHWACommandRelease(ppdev, pCmd);
}

static DECLCALLBACK(void) vboxVHWASurfFlipCompletion(PPDEV ppdev, VBOXVHWACMD * pCmd, void * pContext)
{
    VBOXVHWACMD_SURF_FLIP * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_FLIP);
    PVBOXVHWASURFDESC pCurrDesc = (PVBOXVHWASURFDESC)pBody->CurrGuestSurfInfo;
    PVBOXVHWASURFDESC pTargDesc = (PVBOXVHWASURFDESC)pBody->TargGuestSurfInfo;

    ASMAtomicDecU32(&pCurrDesc->cPendingFlipsCurr);
    ASMAtomicDecU32(&pTargDesc->cPendingFlipsTarg);

    vbvaVHWACommandRelease(ppdev, pCmd);
}

#endif



#endif /* VBOX_WITH_DDRAW */
