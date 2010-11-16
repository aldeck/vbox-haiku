/*
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
#ifndef ___VBoxVideoWddm_h___
#define ___VBoxVideoWddm_h___

#include "../VBoxVideo.h"

//#define VBOXWDDM_DEBUG_VIDPN

/* one page size */
#define VBOXWDDM_C_DMA_BUFFER_SIZE         0x1000
#define VBOXWDDM_C_DMA_PRIVATEDATA_SIZE    0x4000
#define VBOXWDDM_C_ALLOC_LIST_SIZE         0xc00
#define VBOXWDDM_C_PATH_LOCATION_LIST_SIZE 0xc00

#define VBOXWDDM_C_POINTER_MAX_WIDTH  64
#define VBOXWDDM_C_POINTER_MAX_HEIGHT 64

#ifdef VBOX_WITH_VDMA
#define VBOXWDDM_C_VDMA_BUFFER_SIZE   (64*_1K)
#endif

#ifndef VBOXWDDM_RENDER_FROM_SHADOW
# ifndef VBOX_WITH_VDMA
#  error "VBOX_WITH_VDMA must be defined!!!"
# endif
#endif

#ifndef DEBUG_misha
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

PVOID vboxWddmMemAlloc(IN SIZE_T cbSize);
PVOID vboxWddmMemAllocZero(IN SIZE_T cbSize);
VOID vboxWddmMemFree(PVOID pvMem);

NTSTATUS vboxWddmCallIsr(PDEVICE_EXTENSION pDevExt);

/* allocation */
//#define VBOXWDDM_ALLOCATIONINDEX_VOID (~0U)
typedef struct VBOXWDDM_ALLOCATION
{
    LIST_ENTRY SwapchainEntry;
    struct VBOXWDDM_SWAPCHAIN *pSwapchain;
    VBOXWDDM_ALLOC_TYPE enmType;
//    VBOXWDDM_ALLOCUSAGE_TYPE enmCurrentUsage;
    D3DDDI_RESOURCEFLAGS fRcFlags;
    UINT SegmentId;
    VBOXVIDEOOFFSET offVram;
#ifdef VBOX_WITH_VIDEOHWACCEL
    VBOXVHWA_SURFHANDLE hHostHandle;
#endif
    BOOLEAN bVisible;
    BOOLEAN bAssigned;
    VBOXWDDM_SURFACE_DESC SurfDesc;
    struct VBOXWDDM_RESOURCE *pResource;
    /* to return to the Runtime on DxgkDdiCreateAllocation */
    DXGK_ALLOCATIONUSAGEHINT UsageHint;
    uint32_t iIndex;
    VBOXUHGSMI_SYNCHOBJECT_TYPE enmSynchType;
    union
    {
        PKEVENT pSynchEvent;
        PRKSEMAPHORE pSynchSemaphore;
    };
} VBOXWDDM_ALLOCATION, *PVBOXWDDM_ALLOCATION;

typedef struct VBOXWDDM_RESOURCE
{
    uint32_t fFlags;
    VBOXWDDM_RC_DESC RcDesc;
    uint32_t cAllocations;
    VBOXWDDM_ALLOCATION aAllocations[1];
} VBOXWDDM_RESOURCE, *PVBOXWDDM_RESOURCE;

DECLINLINE(PVBOXWDDM_RESOURCE) vboxWddmResourceForAlloc(PVBOXWDDM_ALLOCATION pAlloc)
{
#if 0
    if(pAlloc->iIndex == VBOXWDDM_ALLOCATIONINDEX_VOID)
        return NULL;
    PVBOXWDDM_RESOURCE pRc = (PVBOXWDDM_RESOURCE)(((uint8_t*)pAlloc) - RT_OFFSETOF(VBOXWDDM_RESOURCE, aAllocations[pAlloc->iIndex]));
    return pRc;
#else
    return pAlloc->pResource;
#endif
}

typedef struct VBOXWDDM_OVERLAY
{
    LIST_ENTRY ListEntry;
    PDEVICE_EXTENSION pDevExt;
    PVBOXWDDM_RESOURCE pResource;
    PVBOXWDDM_ALLOCATION pCurentAlloc;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    RECT DstRect;
} VBOXWDDM_OVERLAY, *PVBOXWDDM_OVERLAY;

typedef enum
{
    VBOXWDDM_DEVICE_TYPE_UNDEFINED = 0,
    VBOXWDDM_DEVICE_TYPE_SYSTEM
} VBOXWDDM_DEVICE_TYPE;

typedef struct VBOXWDDM_DEVICE
{
    struct _DEVICE_EXTENSION * pAdapter; /* Adapder info */
    HANDLE hDevice; /* handle passed to CreateDevice */
    VBOXWDDM_DEVICE_TYPE enmType; /* device creation flags passed to DxgkDdiCreateDevice, not sure we need it */
} VBOXWDDM_DEVICE, *PVBOXWDDM_DEVICE;

typedef enum
{
    VBOXWDDM_OBJSTATE_TYPE_UNKNOWN = 0,
    VBOXWDDM_OBJSTATE_TYPE_INITIALIZED,
    VBOXWDDM_OBJSTATE_TYPE_TERMINATED
} VBOXWDDM_OBJSTATE_TYPE;
typedef struct VBOXWDDM_SWAPCHAIN
{
    LIST_ENTRY DevExtListEntry;
    LIST_ENTRY AllocList;
    struct VBOXWDDM_CONTEXT *pContext;
    RECT ViewRect;
    VBOXWDDM_OBJSTATE_TYPE enmState;
    volatile uint32_t cRefs;
    VBOXDISP_UMHANDLE hSwapchainUm;
    VBOXDISP_KMHANDLE hSwapchainKm;
    PVBOXVIDEOCM_CMD_RECTS_INTERNAL pLastReportedRects;
}VBOXWDDM_SWAPCHAIN, *PVBOXWDDM_SWAPCHAIN;

typedef struct VBOXWDDM_CONTEXT
{
//    LIST_ENTRY ListEntry;
    struct VBOXWDDM_DEVICE * pDevice;
    HANDLE hContext;
    VBOXWDDM_CONTEXT_TYPE enmType;
    UINT  NodeOrdinal;
    UINT  EngineAffinity;
//    UINT uLastCompletedCmdFenceId;
    FAST_MUTEX SwapchainMutex;
    VBOXWDDM_HTABLE Swapchains;
    VBOXVIDEOCM_CTX CmContext;
    VBOXVIDEOCM_ALLOC_CONTEXT AllocContext;
} VBOXWDDM_CONTEXT, *PVBOXWDDM_CONTEXT;

#define VBOXWDDMENTRY_2_SWAPCHAIN(_pE) ((PVBOXWDDM_SWAPCHAIN)((uint8_t*)(_pE) - RT_OFFSETOF(VBOXWDDM_SWAPCHAIN, DevExtListEntry)))

typedef struct VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR
{
    VBOXWDDM_DMA_PRIVATEDATA_BASEHDR BaseHdr;
}VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR, *PVBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR;

#ifdef VBOXWDDM_RENDER_FROM_SHADOW

typedef struct VBOXWDDM_DMA_PRIVATEDATA_SHADOW2PRIMARY
{
    VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR Hdr;
    VBOXVDMA_SHADOW2PRIMARY Shadow2Primary;
} VBOXWDDM_DMA_PRIVATEDATA_SHADOW2PRIMARY, *PVBOXWDDM_DMA_PRIVATEDATA_SHADOW2PRIMARY;

#endif

typedef struct VBOXWDDM_DMA_PRIVATEDATA_BLT
{
    VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR Hdr;
    VBOXVDMA_BLT Blt;
} VBOXWDDM_DMA_PRIVATEDATA_BLT, *PVBOXWDDM_DMA_PRIVATEDATA_BLT;

typedef struct VBOXWDDM_DMA_PRIVATEDATA_FLIP
{
    VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR Hdr;
    VBOXVDMA_FLIP Flip;
} VBOXWDDM_DMA_PRIVATEDATA_FLIP, *PVBOXWDDM_DMA_PRIVATEDATA_FLIP;

typedef struct VBOXWDDM_DMA_PRIVATEDATA_CLRFILL
{
    VBOXWDDM_DMA_PRIVATEDATA_PRESENTHDR Hdr;
    VBOXVDMA_CLRFILL ClrFill;
} VBOXWDDM_DMA_PRIVATEDATA_CLRFILL, *PVBOXWDDM_DMA_PRIVATEDATA_CLRFILL;

typedef struct VBOXWDDM_UHGSMI_BUFFER_SUBMIT_INFO
{
    VBOXWDDM_DMA_ALLOCINFO Alloc;
    uint32_t cbData;
    uint32_t bDoNotSignalCompletion;
} VBOXWDDM_UHGSMI_BUFFER_SUBMIT_INFO, *PVBOXWDDM_UHGSMI_BUFFER_SUBMIT_INFO;

typedef struct VBOXWDDM_DMA_PRIVATEDATA_CHROMIUM_CMD
{
    VBOXWDDM_DMA_PRIVATEDATA_BASEHDR Base;
    VBOXWDDM_UHGSMI_BUFFER_SUBMIT_INFO aBufInfos[1];
} VBOXWDDM_DMA_PRIVATEDATA_CHROMIUM_CMD, *PVBOXWDDM_DMA_PRIVATEDATA_CHROMIUM_CMD;

typedef struct VBOXWDDM_OPENALLOCATION
{
    D3DKMT_HANDLE  hAllocation;
} VBOXWDDM_OPENALLOCATION, *PVBOXWDDM_OPENALLOCATION;

#ifdef VBOXWDDM_RENDER_FROM_SHADOW
# define VBOXWDDM_FB_ALLOCATION(_pSrc) ((_pSrc)->pShadowAllocation)
#else
# define VBOXWDDM_FB_ALLOCATION(_pSrc) ((_pSrc)->pPrimaryAllocation)
#endif

#define VBOXWDDM_MAX_VIDEOMODES 128

typedef struct VBOXWDDM_VIDEOMODES_INFO
{
    int32_t iPreferredMode;
    uint32_t cModes;
    uint32_t cPrevModes;
    VIDEO_MODE_INFORMATION aModes[VBOXWDDM_MAX_VIDEOMODES];
    uint32_t cResolutions;
    D3DKMDT_2DREGION aResolutions[VBOXWDDM_MAX_VIDEOMODES];
} VBOXWDDM_VIDEOMODES_INFO, *PVBOXWDDM_VIDEOMODES_INFO;

#endif /* #ifndef ___VBoxVideoWddm_h___ */
