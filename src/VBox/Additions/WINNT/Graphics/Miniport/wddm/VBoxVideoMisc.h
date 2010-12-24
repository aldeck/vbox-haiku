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

#ifndef ___VBoxVideoMisc_h__
#define ___VBoxVideoMisc_h__

DECLINLINE(void) vboxVideoLeDetach(LIST_ENTRY *pList, LIST_ENTRY *pDstList)
{
    if (IsListEmpty(pList))
    {
        InitializeListHead(pDstList);
    }
    else
    {
        *pDstList = *pList;
        Assert(pDstList->Flink->Blink == pList);
        Assert(pDstList->Blink->Flink == pList);
        /* pDstList->Flink & pDstList->Blink point to the "real| entries, never to pList
         * since we've checked IsListEmpty(pList) above */
        pDstList->Flink->Blink = pDstList;
        pDstList->Blink->Flink = pDstList;
        InitializeListHead(pList);
    }
}

typedef struct _DEVICE_EXTENSION *PDEVICE_EXTENSION;
typedef struct VBOXWDDM_SWAPCHAIN *PVBOXWDDM_SWAPCHAIN;
typedef struct VBOXWDDM_CONTEXT *PVBOXWDDM_CONTEXT;
typedef struct VBOXWDDM_ALLOCATION *PVBOXWDDM_ALLOCATION;

typedef uint32_t VBOXWDDM_HANDLE;
#define VBOXWDDM_HANDLE_INVALID 0UL

typedef struct VBOXWDDM_HTABLE
{
    uint32_t cData;
    uint32_t iNext2Search;
    uint32_t cSize;
    PVOID *paData;
} VBOXWDDM_HTABLE, *PVBOXWDDM_HTABLE;

typedef struct VBOXWDDM_HTABLE_ITERATOR
{
    PVBOXWDDM_HTABLE pTbl;
    uint32_t iCur;
    uint32_t cLeft;
} VBOXWDDM_HTABLE_ITERATOR, *PVBOXWDDM_HTABLE_ITERATOR;

VOID vboxWddmHTableIterInit(PVBOXWDDM_HTABLE pTbl, PVBOXWDDM_HTABLE_ITERATOR pIter);
PVOID vboxWddmHTableIterNext(PVBOXWDDM_HTABLE_ITERATOR pIter, VBOXWDDM_HANDLE *phHandle);
BOOL vboxWddmHTableIterHasNext(PVBOXWDDM_HTABLE_ITERATOR pIter);
PVOID vboxWddmHTableIterRemoveCur(PVBOXWDDM_HTABLE_ITERATOR pIter);
NTSTATUS vboxWddmHTableCreate(PVBOXWDDM_HTABLE pTbl, uint32_t cSize);
VOID vboxWddmHTableDestroy(PVBOXWDDM_HTABLE pTbl);
NTSTATUS vboxWddmHTableRealloc(PVBOXWDDM_HTABLE pTbl, uint32_t cNewSize);
VBOXWDDM_HANDLE vboxWddmHTablePut(PVBOXWDDM_HTABLE pTbl, PVOID pvData);
PVOID vboxWddmHTableRemove(PVBOXWDDM_HTABLE pTbl, VBOXWDDM_HANDLE hHandle);
PVOID vboxWddmHTableGet(PVBOXWDDM_HTABLE pTbl, VBOXWDDM_HANDLE hHandle);


PVBOXWDDM_SWAPCHAIN vboxWddmSwapchainCreate();
DECLINLINE(BOOLEAN) vboxWddmSwapchainRetain(PDEVICE_EXTENSION pDevExt, PVBOXWDDM_SWAPCHAIN pSwapchain);
DECLINLINE(VOID) vboxWddmSwapchainRelease(PVBOXWDDM_SWAPCHAIN pSwapchain);
PVBOXWDDM_SWAPCHAIN vboxWddmSwapchainRetainByAlloc(PDEVICE_EXTENSION pDevExt, PVBOXWDDM_ALLOCATION pAlloc);
VOID vboxWddmSwapchainAllocRemove(PDEVICE_EXTENSION pDevExt, PVBOXWDDM_SWAPCHAIN pSwapchain, PVBOXWDDM_ALLOCATION pAlloc);
BOOLEAN vboxWddmSwapchainAllocAdd(PDEVICE_EXTENSION pDevExt, PVBOXWDDM_SWAPCHAIN pSwapchain, PVBOXWDDM_ALLOCATION pAlloc);
VOID vboxWddmSwapchainAllocRemoveAll(PDEVICE_EXTENSION pDevExt, PVBOXWDDM_SWAPCHAIN pSwapchain);
VOID vboxWddmSwapchainDestroy(PDEVICE_EXTENSION pDevExt, PVBOXWDDM_SWAPCHAIN pSwapchain);
VOID vboxWddmSwapchainCtxDestroyAll(PDEVICE_EXTENSION pDevExt, PVBOXWDDM_CONTEXT pContext);
NTSTATUS vboxWddmSwapchainCtxEscape(PDEVICE_EXTENSION pDevExt, PVBOXWDDM_CONTEXT pContext, PVBOXDISPIFESCAPE_SWAPCHAININFO pSwapchainInfo, UINT cbSize);


NTSTATUS vboxWddmRegQueryDisplaySettingsKeyName(PDEVICE_EXTENSION pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
        ULONG cbBuf, PWCHAR pBuf, PULONG pcbResult);
NTSTATUS vboxWddmRegOpenDisplaySettingsKey(IN PDEVICE_EXTENSION pDeviceExtension, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, OUT PHANDLE phKey);
NTSTATUS vboxWddmRegDisplaySettingsQueryRelX(HANDLE hKey, int * pResult);
NTSTATUS vboxWddmRegDisplaySettingsQueryRelY(HANDLE hKey, int * pResult);
NTSTATUS vboxWddmDisplaySettingsQueryPos(IN PDEVICE_EXTENSION pDeviceExtension, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, POINT * pPos);
NTSTATUS vboxWddmRegQueryVideoGuidString(ULONG cbBuf, PWCHAR pBuf, PULONG pcbResult);

NTSTATUS vboxWddmRegQueryDrvKeyName(PDEVICE_EXTENSION pDevExt, ULONG cbBuf, PWCHAR pBuf, PULONG pcbResult);

NTSTATUS vboxWddmRegOpenKey(OUT PHANDLE phKey, IN PWCHAR pName, IN ACCESS_MASK fAccess);
NTSTATUS vboxWddmRegQueryValueDword(IN HANDLE hKey, IN PWCHAR pName, OUT PDWORD pDword);
NTSTATUS vboxWddmRegSetValueDword(IN HANDLE hKey, IN PWCHAR pName, OUT DWORD val);

UNICODE_STRING* vboxWddmVGuidGet(PDEVICE_EXTENSION pDevExt);
VOID vboxWddmVGuidFree(PDEVICE_EXTENSION pDevExt);

#define VBOXWDDM_MM_VOID 0xffffffffUL

typedef struct VBOXWDDM_MM
{
    RTL_BITMAP BitMap;
    UINT cPages;
    UINT cAllocs;
    PULONG pBuffer;
} VBOXWDDM_MM, *PVBOXWDDM_MM;

NTSTATUS vboxMmInit(PVBOXWDDM_MM pMm, UINT cPages);
ULONG vboxMmAlloc(PVBOXWDDM_MM pMm, UINT cPages);
VOID vboxMmFree(PVBOXWDDM_MM pMm, UINT iPage, UINT cPages);
NTSTATUS vboxMmTerm(PVBOXWDDM_MM pMm);

typedef struct VBOXVIDEOCM_ALLOC_MGR
{
    /* synch lock */
    FAST_MUTEX Mutex;
    VBOXWDDM_HTABLE AllocTable;
    VBOXWDDM_MM Mm;
//    PHYSICAL_ADDRESS PhData;
    uint8_t *pvData;
    uint32_t offData;
    uint32_t cbData;
} VBOXVIDEOCM_ALLOC_MGR, *PVBOXVIDEOCM_ALLOC_MGR;

typedef struct VBOXVIDEOCM_ALLOC_CONTEXT
{
    PVBOXVIDEOCM_ALLOC_MGR pMgr;
    /* synch lock */
    FAST_MUTEX Mutex;
    VBOXWDDM_HTABLE AllocTable;
} VBOXVIDEOCM_ALLOC_CONTEXT, *PVBOXVIDEOCM_ALLOC_CONTEXT;

NTSTATUS vboxVideoAMgrCreate(PDEVICE_EXTENSION pDevExt, PVBOXVIDEOCM_ALLOC_MGR pMgr, uint32_t offData, uint32_t cbData);
NTSTATUS vboxVideoAMgrDestroy(PDEVICE_EXTENSION pDevExt, PVBOXVIDEOCM_ALLOC_MGR pMgr);

NTSTATUS vboxVideoAMgrCtxCreate(PVBOXVIDEOCM_ALLOC_MGR pMgr, PVBOXVIDEOCM_ALLOC_CONTEXT pCtx);
NTSTATUS vboxVideoAMgrCtxDestroy(PVBOXVIDEOCM_ALLOC_CONTEXT pCtx);

NTSTATUS vboxVideoAMgrCtxAllocCreate(PVBOXVIDEOCM_ALLOC_CONTEXT pContext, PVBOXVIDEOCM_UM_ALLOC pUmAlloc);
NTSTATUS vboxVideoAMgrCtxAllocDestroy(PVBOXVIDEOCM_ALLOC_CONTEXT pContext, VBOXDISP_KMHANDLE hSesionHandle);

#ifdef VBOX_WITH_CRHGSMI
NTSTATUS vboxVideoAMgrCtxAllocSubmit(PDEVICE_EXTENSION pDevExt, PVBOXVIDEOCM_ALLOC_CONTEXT pContext, UINT cBuffers, VBOXWDDM_UHGSMI_BUFFER_UI_INFO_ESCAPE *paBuffers);
#endif

#endif /* #ifndef ___VBoxVideoMisc_h__ */
