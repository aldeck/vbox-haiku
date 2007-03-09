/******************************Module*Header*******************************\
*
*                           *******************
*                           * GDI SAMPLE CODE *
*                           *******************
*
* Module Name: enable.c
*
* This module contains the functions that enable and disable the
* driver, the pdev, and the surface.
*
* Copyright (c) 1992-1998 Microsoft Corporation
\**************************************************************************/

#include "driver.h"

// The driver function table with all function index/address pairs

// Hook functions to track dirty rectangles and generate RDP orders.
// NT4 functions
DRVFN gadrvfn_nt4[] = {
    {   INDEX_DrvEnablePDEV,            (PFN) DrvEnablePDEV         },	//  0
    {   INDEX_DrvCompletePDEV,          (PFN) DrvCompletePDEV       },	//  1
    {   INDEX_DrvDisablePDEV,           (PFN) DrvDisablePDEV        },	//  2
    {   INDEX_DrvEnableSurface,         (PFN) DrvEnableSurface      },	//  3
    {   INDEX_DrvDisableSurface,        (PFN) DrvDisableSurface     },	//  4
    {   INDEX_DrvAssertMode,            (PFN) DrvAssertMode         },	//  5
    {   INDEX_DrvOffset,                (PFN) DrvOffset             },  //  6
    {   INDEX_DrvDisableDriver,         (PFN) DrvDisableDriver      },  //  8
    {   INDEX_DrvRealizeBrush,          (PFN) DrvRealizeBrush       },  // 12
    {   INDEX_DrvDitherColor,           (PFN) DrvDitherColor        },	// 13
    {   INDEX_DrvStrokePath,            (PFN) DrvStrokePath         },	// 14
    {   INDEX_DrvFillPath,              (PFN) DrvFillPath           },	// 15
    {   INDEX_DrvPaint,                 (PFN) DrvPaint              },	// 17
    {   INDEX_DrvBitBlt,                (PFN) DrvBitBlt             },	// 18
    {   INDEX_DrvCopyBits,              (PFN) DrvCopyBits           },	// 19
    {   INDEX_DrvStretchBlt,            (PFN) DrvStretchBlt,        },	// 20
    {   INDEX_DrvSetPalette,            (PFN) DrvSetPalette         },	// 22
    {   INDEX_DrvTextOut,               (PFN) DrvTextOut            },	// 23
    {   INDEX_DrvSetPointerShape,       (PFN) DrvSetPointerShape    },	// 29
    {   INDEX_DrvMovePointer,           (PFN) DrvMovePointer        },	// 30
    {   INDEX_DrvLineTo,                (PFN) DrvLineTo             },	// 31
    {   INDEX_DrvSynchronize,           (PFN) DrvSynchronize        },  // 38
    {   INDEX_DrvSaveScreenBits,        (PFN) DrvSaveScreenBits     },  // 40
    {   INDEX_DrvGetModes,              (PFN) DrvGetModes           },	// 41
};
/* Experimental begin */
BOOL APIENTRY DrvResetPDEV(
    DHPDEV dhpdevOld,
    DHPDEV dhpdevNew
    )
{
    DISPDBG((0, "Experimental %s: %p, %p\n", __FUNCTION__, dhpdevOld, dhpdevNew));
    return TRUE;
}

BOOL DrvNineGrid (PVOID x1, PVOID x2, PVOID x3, PVOID x4, PVOID x5, PVOID x6, PVOID x7, PVOID x8, PVOID x9)
{
    DISPDBG((0, "Experimental %s: %p, %p, %p, %p, %p, %p, %p, %p, %p\n", __FUNCTION__, x1, x2, x3, x4, x5, x6, x7, x8, x9));
    return FALSE;
}

VOID APIENTRY DrvDestroyFont(
    FONTOBJ *pfo)
{
    DISPDBG((0, "Experimental %s: %p\n", __FUNCTION__, pfo));
}

ULONG APIENTRY DrvEscape(
    SURFOBJ *pso,
    ULONG    iEsc,
    ULONG    cjIn,
    PVOID    pvIn,
    ULONG    cjOut,
    PVOID    pvOut
    )
{
    DISPDBG((0, "Experimental %s: %p, %p, %p, %p, %p, %p\n", __FUNCTION__, pso, iEsc, cjIn, pvIn, cjOut, pvOut));
    return 0;
}
    
BOOL DrvConnect (PVOID x1, PVOID x2, PVOID x3, PVOID x4)
{
    DISPDBG((0, "Experimental %s: %p, %p, %p, %p\n", __FUNCTION__, x1, x2, x3, x4));
    return TRUE;
}

BOOL DrvDisconnect (PVOID x1, PVOID x2)
{
    DISPDBG((0, "Experimental %s: %p, %p\n", __FUNCTION__, x1, x2));
    return FALSE;
}

BOOL DrvReconnect (PVOID x1, PVOID x2)
{
    DISPDBG((0, "Experimental %s: %p, %p\n", __FUNCTION__, x1, x2));
    return FALSE;
}

BOOL DrvShadowConnect (PVOID x1, PVOID x2)
{
    DISPDBG((0, "Experimental %s: %p, %p\n", __FUNCTION__, x1, x2));
    return FALSE;
}

BOOL DrvShadowDisconnect (PVOID x1, PVOID x2)
{
    DISPDBG((0, "Experimental %s: %p, %p\n", __FUNCTION__, x1, x2));
    return FALSE;
}

BOOL DrvDDInit (PVOID x1)
{
    DISPDBG((0, "Experimental %s: %p\n", __FUNCTION__, x1));
    return FALSE;
}

BOOL APIENTRY DrvGetDirectDrawInfo(
    DHPDEV        dhpdev,
    DD_HALINFO   *pHalInfo,
    DWORD        *pdwNumHeaps,
    VIDEOMEMORY  *pvmList,
    DWORD        *pdwNumFourCCCodes,
    DWORD        *pdwFourCC
    )
{
    DISPDBG((0, "Experimental %s: %p, %p, %p, %p, %p. %p\n", __FUNCTION__, dhpdev, pHalInfo, pdwNumHeaps, pvmList, pdwNumFourCCCodes, pdwFourCC));
    return FALSE;
}

BOOL APIENTRY DrvEnableDirectDraw(
    DHPDEV                  dhpdev,
    DD_CALLBACKS           *pCallBacks,
    DD_SURFACECALLBACKS    *pSurfaceCallBacks,
    DD_PALETTECALLBACKS    *pPaletteCallBacks
    )
{
    DISPDBG((0, "Experimental %s: %p, %p, %p, %p\n", __FUNCTION__, dhpdev, pCallBacks, pSurfaceCallBacks, pPaletteCallBacks));
    return FALSE;
}

/* Experimental end */

// W2K,XP functions
DRVFN gadrvfn_nt5[] = {
    {   INDEX_DrvEnablePDEV,            (PFN) DrvEnablePDEV         },	//  0 0x0
    {   INDEX_DrvCompletePDEV,          (PFN) DrvCompletePDEV       },	//  1 0x1
    {   INDEX_DrvDisablePDEV,           (PFN) DrvDisablePDEV        },	//  2 0x2
    {   INDEX_DrvEnableSurface,         (PFN) DrvEnableSurface      },	//  3 0x3
    {   INDEX_DrvDisableSurface,        (PFN) DrvDisableSurface     },	//  4 0x4
    {   INDEX_DrvAssertMode,            (PFN) DrvAssertMode         },	//  5 0x5
    {   INDEX_DrvDisableDriver,         (PFN) DrvDisableDriver      },  //  8 0x8
    {   INDEX_DrvRealizeBrush,          (PFN) DrvRealizeBrush       },  // 12 0xc
    {   INDEX_DrvDitherColor,           (PFN) DrvDitherColor        },	// 13 0xd
    {   INDEX_DrvStrokePath,            (PFN) DrvStrokePath         },	// 14 0xe
    {   INDEX_DrvFillPath,              (PFN) DrvFillPath           },	// 15 0xf
    {   INDEX_DrvPaint,                 (PFN) DrvPaint              },	// 17 0x11
    {   INDEX_DrvBitBlt,                (PFN) DrvBitBlt             },	// 18 0x12
    {   INDEX_DrvCopyBits,              (PFN) DrvCopyBits           },	// 19 0x13
    {   INDEX_DrvStretchBlt,            (PFN) DrvStretchBlt,        },	// 20 0x14
    {   INDEX_DrvSetPalette,            (PFN) DrvSetPalette         },	// 22 0x16
    {   INDEX_DrvTextOut,               (PFN) DrvTextOut            },	// 23 0x17
    {   INDEX_DrvSetPointerShape,       (PFN) DrvSetPointerShape    },	// 29 0x1d
    {   INDEX_DrvMovePointer,           (PFN) DrvMovePointer        },	// 30 0x1e
    {   INDEX_DrvLineTo,                (PFN) DrvLineTo             },	// 31 0x1f
    {   INDEX_DrvSynchronize,           (PFN) DrvSynchronize        },  // 38 0x26
    {   INDEX_DrvSaveScreenBits,        (PFN) DrvSaveScreenBits     },  // 40 0x28
    {   INDEX_DrvGetModes,              (PFN) DrvGetModes           },	// 41 0x29
//     /* Experimental. */
//     {   0x7,                            (PFN) DrvResetPDEV          },	// 0x7
//     {   0x5b,                           (PFN) DrvNineGrid           },	// 0x5b
//     {   0x2b,                           (PFN) DrvDestroyFont        },	// 0x2b
//     {   0x18,                           (PFN) DrvEscape             },	// 0x18
//     {   0x4d,                           (PFN) DrvConnect            },	// 0x4d
//     {   0x4e,                           (PFN) DrvDisconnect         },	// 0x4e
//     {   0x4f,                           (PFN) DrvReconnect          },	// 0x4f
//     {   0x50,                           (PFN) DrvShadowConnect      },	// 0x50
//     {   0x51,                           (PFN) DrvShadowDisconnect   },	// 0x51
//     {   0x3d,                           (PFN) DrvDDInit             },	// 0x3d
//     {   0x3b,                           (PFN) DrvGetDirectDrawInfo  },	// 0x3b
//     {   0x3c,                           (PFN) DrvEnableDirectDraw   },	// 0x3c
    
};

// Required hook bits will be set up according to DDI version
static ULONG gflHooks = 0;

#define HOOKS_BMF8BPP  gflHooks
#define HOOKS_BMF16BPP gflHooks
#define HOOKS_BMF24BPP gflHooks
#define HOOKS_BMF32BPP gflHooks

/******************************Public*Routine******************************\
* DrvEnableDriver
*
* Enables the driver by retrieving the drivers function table and version.
*
\**************************************************************************/

BOOL DrvEnableDriver(
ULONG iEngineVersion,
ULONG cj,
PDRVENABLEDATA pded)
{
// Engine Version is passed down so future drivers can support previous
// engine versions.  A next generation driver can support both the old
// and new engine conventions if told what version of engine it is
// working with.  For the first version the driver does nothing with it.

    iEngineVersion;

    DISPDBG((0, "VBoxDisp::DrvEnableDriver called. iEngine version = %08X\n", iEngineVersion));

    // Set up hook flags to intercept all functions which can generate VRDP orders
    gflHooks = HOOK_BITBLT | HOOK_TEXTOUT | HOOK_FILLPATH |
               HOOK_COPYBITS | HOOK_STROKEPATH | HOOK_LINETO |
               HOOK_PAINT | HOOK_STRETCHBLT | HOOK_SYNCHRONIZEACCESS;

// Fill in as much as we can.

    if (cj >= sizeof(DRVENABLEDATA))
        pded->pdrvfn = (iEngineVersion >= DDI_DRIVER_VERSION_NT5)?
             gadrvfn_nt5:
             gadrvfn_nt4;


    if (cj >= (sizeof(ULONG) * 2))
        pded->c = (iEngineVersion >= DDI_DRIVER_VERSION_NT5)?
            sizeof(gadrvfn_nt5) / sizeof(DRVFN):
            sizeof(gadrvfn_nt4) / sizeof(DRVFN);

// DDI version this driver was targeted for is passed back to engine.
// Future graphic's engine may break calls down to old driver format.

    if (cj >= sizeof(ULONG))
        pded->iDriverVersion = (iEngineVersion >= DDI_DRIVER_VERSION_NT5)?
            DDI_DRIVER_VERSION_NT5:
            DDI_DRIVER_VERSION_NT4;

    return(TRUE);
}

/******************************Public*Routine******************************\
* DrvDisableDriver
*
* Tells the driver it is being disabled. Release any resources allocated in
* DrvEnableDriver.
*
\**************************************************************************/

VOID DrvDisableDriver(VOID)
{
    DISPDBG((0, "VBoxDisp::DrvDisableDriver called.\n"));
    return;
}

/******************************Public*Routine******************************\
* DrvEnablePDEV
*
* DDI function, Enables the Physical Device.
*
* Return Value: device handle to pdev.
*
\**************************************************************************/

DHPDEV DrvEnablePDEV(
DEVMODEW   *pDevmode,       // Pointer to DEVMODE
PWSTR       pwszLogAddress, // Logical address
ULONG       cPatterns,      // number of patterns
HSURF      *ahsurfPatterns, // return standard patterns
ULONG       cjGdiInfo,      // Length of memory pointed to by pGdiInfo
ULONG      *pGdiInfo,       // Pointer to GdiInfo structure
ULONG       cjDevInfo,      // Length of following PDEVINFO structure
DEVINFO    *pDevInfo,       // physical device information structure
HDEV        hdev,           // HDEV, used for callbacks
PWSTR       pwszDeviceName, // DeviceName - not used
HANDLE      hDriver)        // Handle to base driver
{
    GDIINFO GdiInfo;
    DEVINFO DevInfo;
    PPDEV   ppdev = (PPDEV) NULL;

    DISPDBG((0, "VBoxDisp::DrvEnablePDEV called\n"));
    
    UNREFERENCED_PARAMETER(pwszLogAddress);
    UNREFERENCED_PARAMETER(pwszDeviceName);
    
    RtlZeroMemory(&DevInfo, sizeof (DEVINFO));
    RtlZeroMemory(&GdiInfo, sizeof (GDIINFO));

    // Allocate a physical device structure.

    ppdev = (PPDEV) EngAllocMem(0, sizeof(PDEV), ALLOC_TAG);

    if (ppdev == (PPDEV) NULL)
    {
        DISPDBG((0, "DISP DrvEnablePDEV failed EngAllocMem\n"));
        return((DHPDEV) 0);
    }

    memset(ppdev, 0, sizeof(PDEV));

    // Save the screen handle in the PDEV.

    ppdev->hDriver = hDriver;
    
    // Get the current screen mode information.  Set up device caps and devinfo.

    if (!bInitPDEV(ppdev, pDevmode, &GdiInfo, &DevInfo))
    {
        DISPDBG((0,"DISP DrvEnablePDEV failed\n"));
        goto error_free;
    }

    // Initialize the cursor information.

    if (!bInitPointer(ppdev, &DevInfo))
    {
        // Not a fatal error...
        DISPDBG((0, "DrvEnablePDEV failed bInitPointer\n"));
    }
    
    // Initialize palette information.

    if (!bInitPaletteInfo(ppdev, &DevInfo))
    {
        DISPDBG((0, "DrvEnablePDEV failed bInitPalette\n"));
        goto error_free;
    }
    
//    // Start a thread that will process notifications from VMMDev
//    if (!bInitNotificationThread(ppdev))
//    {
//        DISPDBG((0, "DrvEnablePDEV failed bInitNotificationThread\n"));
//        goto error_free;
//    }
    
    // Copy the devinfo into the engine buffer.
    
    DISPDBG((0, "VBoxDisp::DrvEnablePDEV: sizeof(DEVINFO) = %d, cjDevInfo = %d, alpha = %d\n", sizeof(DEVINFO), cjDevInfo, DevInfo.flGraphicsCaps2 & GCAPS2_ALPHACURSOR));
    
// @todo seems to be not necessary. these bits are initialized in screen.c    DevInfo.flGraphicsCaps |= GCAPS_OPAQUERECT       |
//                              GCAPS_DITHERONREALIZE  |
//                              GCAPS_PALMANAGED       |
//                              GCAPS_ALTERNATEFILL    |
//                              GCAPS_WINDINGFILL      |
//                              GCAPS_MONO_DITHER      |
//                              GCAPS_COLOR_DITHER     |
//                              GCAPS_ASYNCMOVE;
//                              
//    DevInfo.flGraphicsCaps |= GCAPS_DITHERONREALIZE;
    
    DevInfo.flGraphicsCaps2 |= GCAPS2_RESERVED1; /* @todo figure out what is this. */

    memcpy(pDevInfo, &DevInfo, min(sizeof(DEVINFO), cjDevInfo));

    // Set the pdevCaps with GdiInfo we have prepared to the list of caps for this
    // pdev.

    memcpy(pGdiInfo, &GdiInfo, min(cjGdiInfo, sizeof(GDIINFO)));

    DISPDBG((0, "VBoxDisp::DrvEnablePDEV completed\n"));
    
    return((DHPDEV) ppdev);

    // Error case for failure.
error_free:
    EngFreeMem(ppdev);
    return((DHPDEV) 0);
}

/******************************Public*Routine******************************\
* DrvCompletePDEV
*
* Store the HPDEV, the engines handle for this PDEV, in the DHPDEV.
*
\**************************************************************************/

VOID DrvCompletePDEV(
DHPDEV dhpdev,
HDEV  hdev)
{
    DISPDBG((0, "VBoxDisp::DrvCompletePDEV called\n"));
    ((PPDEV) dhpdev)->hdevEng = hdev;
}

/******************************Public*Routine******************************\
* DrvDisablePDEV
*
* Release the resources allocated in DrvEnablePDEV.  If a surface has been
* enabled DrvDisableSurface will have already been called.
*
\**************************************************************************/

VOID DrvDisablePDEV(
DHPDEV dhpdev)
{
    DISPDBG((0, "VBoxDisp::DrvDisablePDEV called\n"));
//    vStopNotificationThread ((PPDEV) dhpdev);
    vDisablePalette((PPDEV) dhpdev);
    
    /* Free the driver's VBVA resources. */
    vboxVbvaDisable ((PPDEV) dhpdev);

    EngFreeMem(dhpdev);
}

/******************************Public*Routine******************************\
* VOID DrvOffset
*
* DescriptionText
*
\**************************************************************************/

BOOL DrvOffset(
SURFOBJ*    pso,
LONG        x,
LONG        y,
FLONG       flReserved)
{
    PDEV*   ppdev = (PDEV*) pso->dhpdev;

    // Add back last offset that we subtracted.  I could combine the next
    // two statements, but I thought this was more clear.  It's not
    // performance critical anyway.

    ppdev->pjScreen += ((ppdev->ptlOrg.y * ppdev->lDeltaScreen) +
                        (ppdev->ptlOrg.x * ((ppdev->ulBitCount+1) >> 3)));

    // Subtract out new offset

    ppdev->pjScreen -= ((y * ppdev->lDeltaScreen) +
                        (x * ((ppdev->ulBitCount+1) >> 3)));

    ppdev->ptlOrg.x = x;
    ppdev->ptlOrg.y = y;

    return(TRUE);
}

/******************************Public*Routine******************************\
* DrvEnableSurface
*
* Enable the surface for the device.  Hook the calls this driver supports.
*
* Return: Handle to the surface if successful, 0 for failure.
*
\**************************************************************************/

HSURF DrvEnableSurface(
DHPDEV dhpdev)
{
    PPDEV ppdev;
    HSURF hsurf;
    SIZEL sizl;
    ULONG ulBitmapType;
    FLONG flHooks;

    DISPDBG((0, "DISP DrvEnableSurface called\n"));
        
    // Create engine bitmap around frame buffer.

    ppdev = (PPDEV) dhpdev;

    ppdev->ptlOrg.x = 0;
    ppdev->ptlOrg.y = 0;

    if (!bInitSURF(ppdev, TRUE))
    {
        DISPDBG((0, "DISP DrvEnableSurface failed bInitSURF\n"));
        return(FALSE);
    }

    DISPDBG((0, "DISP DrvEnableSurface bInitSURF success\n"));
    
    sizl.cx = ppdev->cxScreen;
    sizl.cy = ppdev->cyScreen;

    if (ppdev->ulBitCount == 8)
    {
        if (!bInit256ColorPalette(ppdev)) {
            DISPDBG((0, "DISP DrvEnableSurface failed to init the 8bpp palette\n"));
            return(FALSE);
        }
        ulBitmapType = BMF_8BPP;
        flHooks = HOOKS_BMF8BPP;
    }
    else if (ppdev->ulBitCount == 16)
    {
        ulBitmapType = BMF_16BPP;
        flHooks = HOOKS_BMF16BPP;
    }
    else if (ppdev->ulBitCount == 24)
    {
        ulBitmapType = BMF_24BPP;
        flHooks = HOOKS_BMF24BPP;
    }
    else
    {
        ulBitmapType = BMF_32BPP;
        flHooks = HOOKS_BMF32BPP;
    }

    hsurf = (HSURF) EngCreateBitmap(sizl,
                                    ppdev->lDeltaScreen,
                                    ulBitmapType,
                                    (ppdev->lDeltaScreen > 0) ? BMF_TOPDOWN : 0,
                                    (PVOID) (ppdev->pjScreen));
                                     
    if (hsurf == (HSURF) 0)
    {
        DISPDBG((0, "DISP DrvEnableSurface failed EngCreateBitmap\n"));
        goto l_Failure;
    }
    else
    {
        ppdev->hsurfScreenBitmap = hsurf;
         
        if (!EngAssociateSurface(hsurf, ppdev->hdevEng, 0))
        {
            DISPDBG((0, "DISP DrvEnableSurface failed EngAssociateSurface for ScreenBitmap.\n"));
            goto l_Failure;
        }
        else
        {
            SURFOBJ *pso = EngLockSurface(hsurf);
             
            ppdev->psoScreenBitmap = pso;
   
            hsurf = (HSURF) EngCreateDeviceSurface((DHSURF)pso,
                                                    sizl,
                                                    ulBitmapType);

            if (hsurf == (HSURF) 0)
            {
                DISPDBG((0, "DISP DrvEnableSurface failed EngCreateDeviceSurface\n"));
                goto l_Failure;
            }
            else
            {
                ppdev->hsurfScreen = hsurf;
                
                if (!EngAssociateSurface(hsurf, ppdev->hdevEng, flHooks))
                {
                    DISPDBG((0, "DISP DrvEnableSurface failed EngAssociateSurface for Screen.\n"));
                    goto l_Failure;
                }
                else
                {
                    ppdev->flHooks = flHooks;
                    ppdev->ulBitmapType = ulBitmapType;
                }
            }
        }
    }
    
    return ppdev->hsurfScreen;
     
l_Failure:

    DrvDisableSurface(dhpdev);
    
    return((HSURF)0);
}

/******************************Public*Routine******************************\
* DrvDisableSurface
*
* Free resources allocated by DrvEnableSurface.  Release the surface.
*
\**************************************************************************/

VOID DrvDisableSurface(
DHPDEV dhpdev)
{
    PPDEV ppdev = (PPDEV)dhpdev;
    
    DISPDBG((0, "VBoxDisp::DrvDisableSurface called\n"));
    if (ppdev->psoScreenBitmap)
    {
        EngUnlockSurface (ppdev->psoScreenBitmap);
        ppdev->psoScreenBitmap = NULL;
    }

    if (ppdev->hsurfScreen)
    {
        EngDeleteSurface(ppdev->hsurfScreen);
        ppdev->hsurfScreen = (HSURF)0;
    }
    
    if (ppdev->hsurfScreenBitmap)
    {
        EngDeleteSurface(ppdev->hsurfScreenBitmap);
        ppdev->hsurfScreenBitmap = (HSURF)0;
    }
    
    vDisableSURF(ppdev);
}

/******************************Public*Routine******************************\
* DrvAssertMode
*
* This asks the device to reset itself to the mode of the pdev passed in.
*
\**************************************************************************/

BOOL DrvAssertMode(
DHPDEV dhpdev,
BOOL bEnable)
{
    PPDEV   ppdev = (PPDEV) dhpdev;
    ULONG   ulReturn;
    PBYTE   pjScreen;

    DISPDBG((0, "DISP DrvAssertMode called bEnable = %d\n", bEnable));
    
    if (bEnable)
    {
        pjScreen = ppdev->pjScreen;

        if (!bInitSURF(ppdev, FALSE))
        {
            DISPDBG((0, "DISP DrvAssertMode failed bInitSURF\n"));
            return (FALSE);
        }

        if (pjScreen != ppdev->pjScreen)
        {
            HSURF hsurf;
            SIZEL sizl;
            SURFOBJ *pso;
            
            DISPDBG((0, "DISP DrvAssertMode Screen pointer has changed!!!\n"));
            
            sizl.cx = ppdev->cxScreen;
            sizl.cy = ppdev->cyScreen;
            
            hsurf = (HSURF) EngCreateBitmap(sizl,
                                            ppdev->lDeltaScreen,
                                            ppdev->ulBitmapType,
                                            (ppdev->lDeltaScreen > 0) ? BMF_TOPDOWN : 0,
                                            (PVOID) (ppdev->pjScreen));
                                     
            if (hsurf == (HSURF) 0)
            {
                DISPDBG((0, "DISP DrvAssertMode failed EngCreateBitmap\n"));
                return FALSE;
            }
            
            pso = EngLockSurface(hsurf);
             
            if (ppdev->psoScreenBitmap)
            {
                EngUnlockSurface (ppdev->psoScreenBitmap);
                ppdev->psoScreenBitmap = NULL;
            }

            if (ppdev->hsurfScreenBitmap)
            {
                EngDeleteSurface(ppdev->hsurfScreenBitmap);
                ppdev->hsurfScreenBitmap = (HSURF)0;
            }
    
            ppdev->hsurfScreenBitmap = hsurf;
            ppdev->psoScreenBitmap = pso;
        }

        if (!EngAssociateSurface(ppdev->hsurfScreenBitmap, ppdev->hdevEng, 0))
        {
            DISPDBG((0, "DISP DrvAssertMode failed EngAssociateSurface for ScreenBitmap.\n"));
            return FALSE;
        }
            
        if (!EngAssociateSurface(ppdev->hsurfScreen, ppdev->hdevEng, ppdev->flHooks))
        {
            DISPDBG((0, "DISP DrvAssertMode failed EngAssociateSurface for Screen.\n"));
            return FALSE;
        }
            
        return TRUE;
    }
    else
    {
        //
        // We must give up the display.
        // Call the kernel driver to reset the device to a known state.
        //

        if (EngDeviceIoControl(ppdev->hDriver,
                               IOCTL_VIDEO_RESET_DEVICE,
                               NULL,
                               0,
                               NULL,
                               0,
                               &ulReturn))
        {
            DISPDBG((0, "DISP DrvAssertMode failed IOCTL\n"));
            return FALSE;
        }
        else
        {
            return TRUE;
        }
    }
}

/******************************Public*Routine******************************\
* DrvGetModes
*
* Returns the list of available modes for the device.
*
\**************************************************************************/

ULONG DrvGetModes(
HANDLE hDriver,
ULONG cjSize,
DEVMODEW *pdm)

{

    DWORD cModes;
    DWORD cbOutputSize;
    PVIDEO_MODE_INFORMATION pVideoModeInformation, pVideoTemp;
    DWORD cOutputModes = cjSize / (sizeof(DEVMODEW) + DRIVER_EXTRA_SIZE);
    DWORD cbModeSize;

    DISPDBG((3, "DrvGetModes\n"));

    cModes = getAvailableModes(hDriver,
                               (PVIDEO_MODE_INFORMATION *) &pVideoModeInformation,
                               &cbModeSize);

    if (cModes == 0)
    {
        DISPDBG((0, "DrvGetModes failed to get mode information"));
        return 0;
    }

    if (pdm == NULL)
    {
        cbOutputSize = cModes * (sizeof(DEVMODEW) + DRIVER_EXTRA_SIZE);
    }
    else
    {
        //
        // Now copy the information for the supported modes back into the output
        // buffer
        //

        cbOutputSize = 0;

        pVideoTemp = pVideoModeInformation;

        do
        {
            if (pVideoTemp->Length != 0)
            {
                if (cOutputModes == 0)
                {
                    break;
                }

                //
                // Zero the entire structure to start off with.
                //

                memset(pdm, 0, sizeof(DEVMODEW));

                //
                // Set the name of the device to the name of the DLL.
                //

                memcpy(pdm->dmDeviceName, DLL_NAME, sizeof(DLL_NAME));

                pdm->dmSpecVersion      = DM_SPECVERSION;
                pdm->dmDriverVersion    = DM_SPECVERSION;
                pdm->dmSize             = sizeof(DEVMODEW);
                pdm->dmDriverExtra      = DRIVER_EXTRA_SIZE;

                pdm->dmBitsPerPel       = pVideoTemp->NumberOfPlanes *
                                          pVideoTemp->BitsPerPlane;
                pdm->dmPelsWidth        = pVideoTemp->VisScreenWidth;
                pdm->dmPelsHeight       = pVideoTemp->VisScreenHeight;
                pdm->dmDisplayFrequency = pVideoTemp->Frequency;
                pdm->dmDisplayFlags     = 0;

                pdm->dmFields           = DM_BITSPERPEL       |
                                          DM_PELSWIDTH        |
                                          DM_PELSHEIGHT       |
                                          DM_DISPLAYFREQUENCY |
                                          DM_DISPLAYFLAGS     ;

                //
                // Go to the next DEVMODE entry in the buffer.
                //

                cOutputModes--;

                pdm = (LPDEVMODEW) ( ((ULONG_PTR)pdm) + sizeof(DEVMODEW)
                                                     + DRIVER_EXTRA_SIZE);

                cbOutputSize += (sizeof(DEVMODEW) + DRIVER_EXTRA_SIZE);

            }

            pVideoTemp = (PVIDEO_MODE_INFORMATION)
                (((PUCHAR)pVideoTemp) + cbModeSize);

        } while (--cModes);
    }

    EngFreeMem(pVideoModeInformation);

    return cbOutputSize;

}

VOID DrvSynchronize(
IN DHPDEV dhpdev,
IN RECTL *prcl)
{
}

