/******************************Module*Header*******************************\
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
/*
* Based in part on Microsoft DDK sample code
*
*                           *******************
*                           * GDI SAMPLE CODE *
*                           *******************
*
* Module Name: driver.h
*
* contains prototypes for the frame buffer driver.
*
* Copyright (c) 1992-1998 Microsoft Corporation
\**************************************************************************/

#include "stddef.h"
#include <stdarg.h>
#include "windef.h"
#include "wingdi.h"
#include "winddi.h"
#include "devioctl.h"
#include "ntddvdeo.h"
#include "debug.h"

#include "../Miniport/vboxioctl.h"

#include <VBox/VBoxVideoGuest.h>
#include <VBox/VBoxVideo.h>
#ifdef VBOX_WITH_VIDEOHWACCEL
#include <iprt/asm.h>
#endif

/* Forward declaration. */
struct _PDEV;
typedef struct _PDEV PDEV;
typedef PDEV *PPDEV;

typedef struct _VBOXDISPLAYINFO
{
    VBOXVIDEOINFOHDR         hdrLink;
    VBOXVIDEOINFOLINK        link;
    VBOXVIDEOINFOHDR         hdrScreen;
    VBOXVIDEOINFOSCREEN      screen;
    VBOXVIDEOINFOHDR         hdrHostEvents;
    VBOXVIDEOINFOHOSTEVENTS  hostEvents;
    VBOXVIDEOINFOHDR         hdrEnd;
} VBOXDISPLAYINFO;

#include "vbvavrdp.h"
#include "vrdpbmp.h"

/* Saved screen bits information. */
typedef struct _SSB
{
    ULONG ident;   /* 1 based index in the stack = the handle returned by DrvSaveScreenBits (SS_SAVE) */
    BYTE *pBuffer; /* Buffer where screen bits are saved. */
} SSB;

/* VRAM
 *  |           |          |           |           |
 * 0+framebuffer+ddraw heap+VBVA buffer+displayinfo=cScreenSize
 */
typedef struct _VRAMLAYOUT
{
    ULONG cbVRAM;

    ULONG offFrameBuffer;
    ULONG cbFrameBuffer;

    ULONG offDDRAWHeap; //@todo
    ULONG cbDDRAWHeap;

    ULONG offVBVABuffer;
    ULONG cbVBVABuffer;

    ULONG offDisplayInformation;
    ULONG cbDisplayInformation;
} VRAMLAYOUT;

typedef struct
{
    PPDEV   ppdev;
} VBOXSURF, *PVBOXSURF;

#ifdef VBOX_WITH_VIDEOHWACCEL
typedef struct _VBOXVHWAREGION
{
    RECTL Rect;
    bool bValid;
}VBOXVHWAREGION, *PVBOXVHWAREGION;

typedef struct _VBOXVHWASURFDESC
{
    VBOXVHWA_SURFHANDLE hHostHandle;
    volatile uint32_t cPendingBltsSrc;
    volatile uint32_t cPendingBltsDst;
    volatile uint32_t cPendingFlipsCurr;
    volatile uint32_t cPendingFlipsTarg;
#ifdef DEBUG
    volatile uint32_t cFlipsCurr;
    volatile uint32_t cFlipsTarg;
#endif
//    uint32_t cBitsPerPixel;
    bool bVisible;
    VBOXVHWAREGION UpdatedMemRegion;
    VBOXVHWAREGION NonupdatedMemRegion;
}VBOXVHWASURFDESC, *PVBOXVHWASURFDESC;

typedef struct _VBOXVHWAINFO
{
    uint32_t caps;
    uint32_t caps2;
    uint32_t colorKeyCaps;
    uint32_t stretchCaps;
    uint32_t surfaceCaps;
    uint32_t numOverlays;
    uint32_t numFourCC;
    HGSMIOFFSET FourCC;
    ULONG_PTR offVramBase;
    BOOLEAN bVHWAEnabled;
    BOOLEAN bVHWAInited;
} VBOXVHWAINFO;
#endif

struct  _PDEV
{
    HANDLE  hDriver;                    // Handle to \Device\Screen
    HDEV    hdevEng;                    // Engine's handle to PDEV
    HSURF   hsurfScreenBitmap;          // Engine's handle to VRAM screen bitmap surface
    SURFOBJ *psoScreenBitmap;           // VRAM screen bitmap surface
    HSURF   hsurfScreen;                // Engine's handle to VRAM screen device surface
    ULONG   ulBitmapType;
    HPALETTE hpalDefault;               // Handle to the default palette for device.
    PBYTE   pjScreen;                   // This is pointer to base screen address
    ULONG   cxScreen;                   // Visible screen width
    ULONG   cyScreen;                   // Visible screen height
    POINTL  ptlOrg;                     // Where this display is anchored in
                                        //   the virtual desktop.
    POINTL  ptlDevOrg;                  // Device origin for DualView (0,0 for primary view).
    ULONG   ulMode;                     // Mode the mini-port driver is in.
    LONG    lDeltaScreen;               // Distance from one scan to the next.

    PVOID   pOffscreenList;             // linked list of DCI offscreen surfaces.
    FLONG   flRed;                      // For bitfields device, Red Mask
    FLONG   flGreen;                    // For bitfields device, Green Mask
    FLONG   flBlue;                     // For bitfields device, Blue Mask
    ULONG   cPaletteShift;              // number of bits the 8-8-8 palette must
                                        // be shifted by to fit in the hardware
                                        // palette.
    ULONG   ulBitCount;                 // # of bits per pel 8,16,24,32 are only supported.
    POINTL  ptlHotSpot;                 // adjustment for pointer hot spot
    VIDEO_POINTER_CAPABILITIES PointerCapabilities; // HW pointer abilities
    PVIDEO_POINTER_ATTRIBUTES pPointerAttributes; // hardware pointer attributes
    DWORD   cjPointerAttributes;        // Size of buffer allocated
    BOOL    fHwCursorActive;            // Are we currently using the hw cursor
    PALETTEENTRY *pPal;                 // If this is pal managed, this is the pal
    BOOL    bSupportDCI;                // Does the miniport support DCI?
    FLONG   flHooks;

    VRDPBC           cache;

    ULONG cSSB;                 // Number of active saved screen bits records in the following array.
    SSB aSSB[4];                // LIFO type stack for saved screen areas.

    ULONG iDevice;
    VRAMLAYOUT layout;

    PVBOXSURF   pdsurfScreen;

#ifdef VBOX_WITH_DDRAW
    BOOL             bDdExclusiveMode;
    DWORD            dwNewDDSurfaceOffset;
    DWORD            cHeaps;
    VIDEOMEMORY*     pvmList;
    struct {
        DWORD bLocked;
        RECTL rArea;
    } ddLock;
#endif /* VBOX_WITH_DDRAW */

    BOOLEAN bHGSMISupported;
    HGSMIGUESTCOMMANDCONTEXT guestCtx;
    VBVABUFFERCONTEXT vbvaCtx;

    HVBOXVIDEOHGSMI hMpHGSMI; /* context handler passed to miniport HGSMI callbacks */
    PFNVBOXVIDEOHGSMICOMPLETION pfnHGSMICommandComplete; /* called to complete the command we receive from the miniport */
    PFNVBOXVIDEOHGSMICOMMANDS   pfnHGSMIRequestCommands; /* called to requests the commands posted to us from the host */

    PVOID pVideoPortContext;
    VBOXVIDEOPORTPROCS VideoPortProcs;

#ifdef VBOX_WITH_VIDEOHWACCEL
    VBOXVHWAINFO vhwaInfo;
#endif
};

#ifdef VBOX_WITH_OPENGL
typedef struct
{
    DWORD dwVersion;
    DWORD dwDriverVersion;
    WCHAR szDriverName[256];
} OPENGL_INFO, *POPENGL_INFO;
#endif



extern BOOL  g_bOnNT40;

DWORD getAvailableModes(HANDLE, PVIDEO_MODE_INFORMATION *, DWORD *);
BOOL bInitPDEV(PPDEV, PDEVMODEW, GDIINFO *, DEVINFO *);
BOOL bInitSURF(PPDEV, BOOL);
BOOL bInitPaletteInfo(PPDEV, DEVINFO *);
BOOL bInitPointer(PPDEV, DEVINFO *);
BOOL bInit256ColorPalette(PPDEV);
BOOL bInitNotificationThread(PPDEV);
VOID vStopNotificationThread (PPDEV);
VOID vDisablePalette(PPDEV);
VOID vDisableSURF(PPDEV);

#define MAX_CLUT_SIZE (sizeof(VIDEO_CLUT) + (sizeof(ULONG) * 256))

//
// Determines the size of the DriverExtra information in the DEVMODE
// structure passed to and from the display driver.
//

#define DRIVER_EXTRA_SIZE 0

#define DLL_NAME                L"VBoxDisp"   // Name of the DLL in UNICODE
#define STANDARD_DEBUG_PREFIX   "VBOXDISP: "  // All debug output is prefixed
#define ALLOC_TAG               'bvDD'        // Four byte tag (characters in
                                              // reverse order) used for memory
                                              // allocations

// VBOX
typedef struct _CLIPRECTS {
    ULONG  c;
    RECTL  arcl[64];
} CLIPRECTS;

typedef struct _VRDPCLIPRECTS
{
    RECTL rclDstOrig; /* Original bounding rectangle. */
    RECTL rclDst;     /* Bounding rectangle of all rects. */
    CLIPRECTS rects;  /* Rectangles to update. */
} VRDPCLIPRECTS;


void VBoxProcessDisplayInfo(PPDEV ppdev);
void VBoxUpdateDisplayInfo (PPDEV ppdev);

void drvLoadEng (void);

void vboxVBVAHostCommandComplete(PPDEV ppdev, VBVAHOSTCMD * pCmd);

#ifdef VBOX_WITH_VIDEOHWACCEL

DECLINLINE(uint64_t) vboxVHWAVramOffsetFromPDEV(PPDEV pDev, ULONG_PTR offPdev)
{
    return (uint64_t)(pDev->vhwaInfo.offVramBase + offPdev);
}

#define VBOXDD_CHECKFLAG(_v, _f) ((_v) & (_f)) == (_f)

typedef DECLCALLBACK(void) FNVBOXVHWACMDCOMPLETION(PPDEV ppdev, VBOXVHWACMD * pCmd, void * pContext);
typedef FNVBOXVHWACMDCOMPLETION *PFNVBOXVHWACMDCOMPLETION;

void vboxVHWARectUnited(RECTL * pDst, RECTL * pRect1, RECTL * pRect2);
bool vboxVHWARectIsEmpty(RECTL * pRect);
bool vboxVHWARectIntersect(RECTL * pRect1, RECTL * pRect2);
bool vboxVHWARectInclude(RECTL * pRect1, RECTL * pRect2);
bool vboxVHWARegionIntersects(PVBOXVHWAREGION pReg, RECTL * pRect);
bool vboxVHWARegionIncludes(PVBOXVHWAREGION pReg, RECTL * pRect);
bool vboxVHWARegionIncluded(PVBOXVHWAREGION pReg, RECTL * pRect);
void vboxVHWARegionSet(PVBOXVHWAREGION pReg, RECTL * pRect);
void vboxVHWARegionAdd(PVBOXVHWAREGION pReg, RECTL * pRect);
void vboxVHWARegionInit(PVBOXVHWAREGION pReg);
void vboxVHWARegionClear(PVBOXVHWAREGION pReg);
bool vboxVHWARegionValid(PVBOXVHWAREGION pReg);
void vboxVHWARegionTrySubstitute(PVBOXVHWAREGION pReg, const RECTL *pRect);

VBOXVHWACMD* vboxVHWACommandCreate (PPDEV ppdev, VBOXVHWACMD_TYPE enmCmd, VBOXVHWACMD_LENGTH cbCmd);
void vboxVHWACommandFree (PPDEV ppdev, VBOXVHWACMD* pCmd);
DECLINLINE(void) vbvaVHWACommandRelease (PPDEV ppdev, VBOXVHWACMD* pCmd)
{
    uint32_t cRefs = ASMAtomicDecU32(&pCmd->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if(!cRefs)
    {
        vboxVHWACommandFree(ppdev, pCmd);
    }
}

DECLINLINE(void) vbvaVHWACommandRetain (PPDEV ppdev, VBOXVHWACMD* pCmd)
{
    ASMAtomicIncU32(&pCmd->cRefs);
}

BOOL vboxVHWACommandSubmit (PPDEV ppdev, VBOXVHWACMD* pCmd);
void vboxVHWACommandSubmitAsynch (PPDEV ppdev, VBOXVHWACMD* pCmd, PFNVBOXVHWACMDCOMPLETION pfnCompletion, void * pContext);
void vboxVHWACommandSubmitAsynchByEvent (PPDEV ppdev, VBOXVHWACMD* pCmd, VBOXPEVENT pEvent);
void vboxVHWACommandCheckHostCmds(PPDEV ppdev);
void vboxVHWACommandSubmitAsynchAndComplete (PPDEV ppdev, VBOXVHWACMD* pCmd);

int vboxVHWAInitHostInfo1(PPDEV ppdev);
int vboxVHWAInitHostInfo2(PPDEV ppdev, DWORD *pFourCC);

VBOXVHWACMD_QUERYINFO1* vboxVHWAQueryHostInfo1(PPDEV ppdev);
void vboxVHWAFreeHostInfo1(PPDEV ppdev, VBOXVHWACMD_QUERYINFO1* pInfo);
VBOXVHWACMD_QUERYINFO2* vboxVHWAQueryHostInfo2(PPDEV ppdev, uint32_t numFourCC);
void vboxVHWAFreeHostInfo2(PPDEV ppdev, VBOXVHWACMD_QUERYINFO2* pInfo);

void vboxVHWAInit();
void vboxVHWATerm();
uint32_t vboxVHWAUnsupportedDDCAPS(uint32_t caps);
uint32_t vboxVHWAUnsupportedDDSCAPS(uint32_t caps);
uint32_t vboxVHWAUnsupportedDDPFS(uint32_t caps);
uint32_t vboxVHWASupportedDDCAPS(uint32_t caps);
uint32_t vboxVHWASupportedDDSCAPS(uint32_t caps);
uint32_t vboxVHWASupportedDDPFS(uint32_t caps);

uint32_t vboxVHWAUnsupportedDDCEYCAPS(uint32_t caps);
uint32_t vboxVHWASupportedDDCEYCAPS(uint32_t caps);

uint32_t vboxVHWAToDDBLTs(uint32_t caps);
uint32_t vboxVHWAFromDDBLTs(uint32_t caps);

uint32_t vboxVHWAFromDDCAPS2(uint32_t caps);
uint32_t vboxVHWAToDDCAPS2(uint32_t caps);

void vboxVHWAFromDDBLTFX(VBOXVHWA_BLTFX *pVHWABlt, DDBLTFX *pDdBlt);

void vboxVHWAFromDDCOLORKEY(VBOXVHWA_COLORKEY *pVHWACKey, DDCOLORKEY  *pDdCKey);

uint32_t vboxVHWAFromDDOVERs(uint32_t caps);
uint32_t vboxVHWAToDDOVERs(uint32_t caps);
uint32_t vboxVHWAFromDDCKEYs(uint32_t caps);
uint32_t vboxVHWAToDDCKEYs(uint32_t caps);

void vboxVHWAFromDDOVERLAYFX(VBOXVHWA_OVERLAYFX *pVHWAOverlay, DDOVERLAYFX *pDdOverlay);

uint32_t vboxVHWAFromDDCAPS(uint32_t caps);
uint32_t vboxVHWAToDDCAPS(uint32_t caps);
uint32_t vboxVHWAFromDDSCAPS(uint32_t caps);
uint32_t vboxVHWAToDDSCAPS(uint32_t caps);
uint32_t vboxVHWAFromDDPFS(uint32_t caps);
uint32_t vboxVHWAToDDPFS(uint32_t caps);
uint32_t vboxVHWAFromDDCKEYCAPS(uint32_t caps);
uint32_t vboxVHWAToDDCKEYCAPS(uint32_t caps);
int vboxVHWAFromDDPIXELFORMAT(VBOXVHWA_PIXELFORMAT *pVHWAFormat, DDPIXELFORMAT *pDdFormat);
int vboxVHWAFromDDSURFACEDESC(VBOXVHWA_SURFACEDESC *pVHWADesc, DDSURFACEDESC *pDdDesc);
void vboxVHWAFromRECTL(VBOXVHWA_RECTL *pDst, RECTL *pSrc);
PVBOXVHWASURFDESC vboxVHWASurfDescAlloc();
void vboxVHWASurfDescFree(PVBOXVHWASURFDESC pDesc);

int vboxVHWAEnable(PPDEV ppdev);
int vboxVHWADisable(PPDEV ppdev);

#endif

BOOL bIsScreenSurface (SURFOBJ *pso);

__inline SURFOBJ *getSurfObj (SURFOBJ *pso)
{
    if (pso)
    {
        PPDEV ppdev = (PPDEV)pso->dhpdev;

        if (ppdev)
        {
            if (ppdev->psoScreenBitmap && pso->hsurf == ppdev->hsurfScreen)
            {
                /* Convert the device PSO to the bitmap PSO which can be passed to Eng*. */
                pso = ppdev->psoScreenBitmap;
            }
        }
    }

    return pso;
}

#define CONV_SURF(_pso) getSurfObj (_pso)

__inline int format2BytesPerPixel(const SURFOBJ *pso)
{
    switch (pso->iBitmapFormat)
    {
        case BMF_16BPP: return 2;
        case BMF_24BPP: return 3;
        case BMF_32BPP: return 4;
    }

    return 0;
}

#ifdef VBOX_VBVA_ADJUST_RECT
void vrdpAdjustRect (SURFOBJ *pso, RECTL *prcl);
BOOL vbvaFindChangedRect (SURFOBJ *psoDest, SURFOBJ *psoSrc, RECTL *prclDest, POINTL *pptlSrc);
#endif /* VBOX_VBVA_ADJUST_RECT */

void vrdpReportDirtyRect (PPDEV ppdev, RECTL *prcl);
void vbvaReportDirtyRect (PPDEV ppdev, RECTL *prcl);

#define VRDP_TEXT_MAX_GLYPH_SIZE 0x100
#define VRDP_TEXT_MAX_GLYPHS     0xfe

BOOL vboxReportText (PPDEV ppdev,
                     VRDPCLIPRECTS *pClipRects,
                     STROBJ   *pstro,
                     FONTOBJ  *pfo,
                     RECTL    *prclOpaque,
                     ULONG    ulForeRGB,
                     ULONG    ulBackRGB
                    );

BOOL vrdpReportOrderGeneric (PPDEV ppdev,
                             const VRDPCLIPRECTS *pClipRects,
                             const void *pvOrder,
                             unsigned cbOrder,
                             unsigned code);


#include <iprt/assert.h>

#ifdef DEBUG
#define VBVA_ASSERT(expr) \
    do { \
        if (!(expr)) \
        { \
            RTAssertMsg1Weak(#expr, __LINE__, __FILE__, __PRETTY_FUNCTION__); \
            RTAssertMsg2Weak("!!!\n"); \
        } \
    } while (0)
#else
#define VBVA_ASSERT(expr) do {} while (0)
#endif

#ifdef STAT_sunlover
extern ULONG gStatCopyBitsOffscreenToScreen;
extern ULONG gStatCopyBitsScreenToScreen;
extern ULONG gStatBitBltOffscreenToScreen;
extern ULONG gStatBitBltScreenToScreen;
extern ULONG gStatUnchangedOffscreenToScreen;
extern ULONG gStatUnchangedOffscreenToScreenCRC;
extern ULONG gStatNonTransientEngineBitmaps;
extern ULONG gStatTransientEngineBitmaps;
extern ULONG gStatUnchangedBitmapsCRC;
extern ULONG gStatUnchangedBitmapsDeviceCRC;
extern ULONG gStatBitmapsCRC;
extern ULONG gStatBitBltScreenPattern;
extern ULONG gStatBitBltScreenSquare;
extern ULONG gStatBitBltScreenPatternReported;
extern ULONG gStatBitBltScreenSquareReported;
extern ULONG gStatCopyBitsScreenSquare;

extern ULONG gStatEnablePDEV;
extern ULONG gStatCompletePDEV;
extern ULONG gStatDisablePDEV;
extern ULONG gStatEnableSurface;
extern ULONG gStatDisableSurface;
extern ULONG gStatAssertMode;
extern ULONG gStatDisableDriver;
extern ULONG gStatCreateDeviceBitmap;
extern ULONG gStatDeleteDeviceBitmap;
extern ULONG gStatDitherColor;
extern ULONG gStatStrokePath;
extern ULONG gStatFillPath;
extern ULONG gStatStrokeAndFillPath;
extern ULONG gStatPaint;
extern ULONG gStatBitBlt;
extern ULONG gStatCopyBits;
extern ULONG gStatStretchBlt;
extern ULONG gStatSetPalette;
extern ULONG gStatTextOut;
extern ULONG gStatSetPointerShape;
extern ULONG gStatMovePointer;
extern ULONG gStatLineTo;
extern ULONG gStatSynchronize;
extern ULONG gStatGetModes;
extern ULONG gStatGradientFill;
extern ULONG gStatStretchBltROP;
extern ULONG gStatPlgBlt;
extern ULONG gStatAlphaBlend;
extern ULONG gStatTransparentBlt;

void statPrint (void);

#define STATDRVENTRY(a, b) do { if (bIsScreenSurface (b)) gStat##a++; } while (0)
#define STATPRINT do { statPrint (); } while (0)
#else
#define STATDRVENTRY(a, b)
#define STATPRINT
#endif /* STAT_sunlover */
