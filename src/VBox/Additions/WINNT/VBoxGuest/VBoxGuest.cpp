/** @file
 *
 * VBoxGuest -- VirtualBox Win32 guest support driver
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

// enable backdoor logging
//#define LOG_ENABLED

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "VBoxGuest_Internal.h"
#ifdef TARGET_NT4
#include "NTLegacy.h"
#else
#include "VBoxGuestPnP.h"
#endif
#include "Helper.h"
#include <excpt.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <stdio.h>
#include <VBox/VBoxGuestLib.h>
#include <VBoxGuestInternal.h>

#ifdef TARGET_NT4
/*
 * XP DDK #defines ExFreePool to ExFreePoolWithTag. The latter does not exist
 * on NT4, so... The same for ExAllocatePool.
 */
#undef ExAllocatePool
#undef ExFreePool
#endif

/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
extern "C"
{
static NTSTATUS VBoxGuestAddDevice(PDRIVER_OBJECT pDrvObj, PDEVICE_OBJECT pDevObj);
static void     VBoxGuestUnload(PDRIVER_OBJECT pDrvObj);
static NTSTATUS VBoxGuestCreate(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static NTSTATUS VBoxGuestClose(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static NTSTATUS VBoxGuestDeviceControl(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static NTSTATUS VBoxGuestSystemControl(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static NTSTATUS VBoxGuestShutdown(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static NTSTATUS VBoxGuestNotSupportedStub(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static VOID     vboxWorkerThread(PVOID context);
static VOID     reserveHypervisorMemory(PVBOXGUESTDEVEXT pDevExt);
static VOID     vboxIdleThread(PVOID context);
}

#ifdef VBOX_WITH_HGCM
DECLVBGL(int) VBoxHGCMCallback(VMMDevHGCMRequestHeader *pHeader, void *pvData, uint32_t u32Data);
#endif

#ifdef DEBUG
static VOID testVBoxGuest(VOID);
#endif

/*******************************************************************************
*   Exported Functions                                                         *
*******************************************************************************/
RT_C_DECLS_BEGIN
ULONG DriverEntry(PDRIVER_OBJECT pDrvObj, PUNICODE_STRING pRegPath);
RT_C_DECLS_END

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, createThreads)
#pragma alloc_text (PAGE, unreserveHypervisorMemory)
#pragma alloc_text (PAGE, VBoxGuestAddDevice)
#pragma alloc_text (PAGE, VBoxGuestUnload)
#pragma alloc_text (PAGE, VBoxGuestCreate)
#pragma alloc_text (PAGE, VBoxGuestClose)
#pragma alloc_text (PAGE, VBoxGuestDeviceControl)
#pragma alloc_text (PAGE, VBoxGuestShutdown)
#pragma alloc_text (PAGE, VBoxGuestNotSupportedStub)
/* Note: at least the isr handler should be in non-pageable memory! */
/*#pragma alloc_text (PAGE, VBoxGuestDpcHandler)
 #pragma alloc_text (PAGE, VBoxGuestIsrHandler) */
#pragma alloc_text (PAGE, vboxWorkerThread)
#pragma alloc_text (PAGE, reserveHypervisorMemory)
#pragma alloc_text (PAGE, vboxIdleThread)
#endif

winVersion_t winVersion;

/**
 * Driver entry point.
 *
 * @returns appropriate status code.
 * @param   pDrvObj     Pointer to driver object.
 * @param   pRegPath    Registry base path.
 */
ULONG DriverEntry(PDRIVER_OBJECT pDrvObj, PUNICODE_STRING pRegPath)
{
    NTSTATUS rc = STATUS_SUCCESS;

    dprintf(("VBoxGuest::DriverEntry. Driver built: %s %s\n", __DATE__, __TIME__));

    ULONG majorVersion;
    ULONG minorVersion;
    ULONG buildNumber;
    PsGetVersion(&majorVersion, &minorVersion, &buildNumber, NULL);
    dprintf(("VBoxGuest::DriverEntry: Running on Windows NT version %d.%d, build %d\n", majorVersion, minorVersion, buildNumber));
#ifdef DEBUG
    testVBoxGuest();
#endif
    switch (majorVersion)
    {
        case 6: /* Windows Vista or Windows 7 (based on minor ver) */
            switch (minorVersion)
            {
                case 0: /* Note: Also could be Windows 2008 Server! */
                    winVersion = WINVISTA;
                    break;
                case 1: /* Note: Also could be Windows 2008 Server R2! */
                    winVersion = WIN7;
                    break;
                default:
                    dprintf(("VBoxGuest::DriverEntry: Unknown version of Windows, refusing!\n"));
                    return STATUS_DRIVER_UNABLE_TO_LOAD;
            }
            break;
        case 5:
            switch (minorVersion)
            {
                case 2:
                    winVersion = WIN2K3;
                    break;
                case 1:
                    winVersion = WINXP;
                    break;
                case 0:
                    winVersion = WIN2K;
                    break;
                default:
                    dprintf(("VBoxGuest::DriverEntry: Unknown version of Windows, refusing!\n"));
                    return STATUS_DRIVER_UNABLE_TO_LOAD;
            }
            break;
        case 4:
            winVersion = WINNT4;
            break;
        default:
            dprintf(("VBoxGuest::DriverEntry: At least Windows NT4 required!\n"));
            return STATUS_DRIVER_UNABLE_TO_LOAD;
    }

    /*
     * Setup the driver entry points in pDrvObj.
     */
    pDrvObj->DriverUnload                             = VBoxGuestUnload;
    pDrvObj->MajorFunction[IRP_MJ_CREATE]             = VBoxGuestCreate;
    pDrvObj->MajorFunction[IRP_MJ_CLOSE]              = VBoxGuestClose;
    pDrvObj->MajorFunction[IRP_MJ_DEVICE_CONTROL]     = VBoxGuestDeviceControl;
    pDrvObj->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = VBoxGuestDeviceControl;
    pDrvObj->MajorFunction[IRP_MJ_SHUTDOWN]           = VBoxGuestShutdown;
    pDrvObj->MajorFunction[IRP_MJ_READ]               = VBoxGuestNotSupportedStub;
    pDrvObj->MajorFunction[IRP_MJ_WRITE]              = VBoxGuestNotSupportedStub;
#ifdef TARGET_NT4
    rc = ntCreateDevice(pDrvObj, NULL /* pDevObj */, pRegPath);
#else
    pDrvObj->MajorFunction[IRP_MJ_PNP]                = VBoxGuestPnP;
    pDrvObj->MajorFunction[IRP_MJ_POWER]              = VBoxGuestPower;
    pDrvObj->MajorFunction[IRP_MJ_SYSTEM_CONTROL]     = VBoxGuestSystemControl;
    pDrvObj->DriverExtension->AddDevice               = (PDRIVER_ADD_DEVICE)VBoxGuestAddDevice;
#endif

    dprintf(("VBoxGuest::DriverEntry returning %#x\n", rc));
    return rc;
}

#ifndef TARGET_NT4
/**
 * Handle request from the Plug & Play subsystem
 *
 * @returns NT status code
 * @param  pDrvObj   Driver object
 * @param  pDevObj   Device object
 */
static NTSTATUS VBoxGuestAddDevice(PDRIVER_OBJECT pDrvObj, PDEVICE_OBJECT pDevObj)
{
    NTSTATUS rc;
    dprintf(("VBoxGuest::VBoxGuestAddDevice\n"));

    /*
     * Create device.
     */
    PDEVICE_OBJECT deviceObject = NULL;
    UNICODE_STRING  devName;
    RtlInitUnicodeString(&devName, VBOXGUEST_DEVICE_NAME_NT);
    rc = IoCreateDevice(pDrvObj, sizeof(VBOXGUESTDEVEXT), &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObject);
    if (!NT_SUCCESS(rc))
    {
        dprintf(("VBoxGuest::VBoxGuestAddDevice: IoCreateDevice failed with rc=%#x!\n", rc));
        return rc;
    }
    UNICODE_STRING win32Name;
    RtlInitUnicodeString(&win32Name, VBOXGUEST_DEVICE_NAME_DOS);
    rc = IoCreateSymbolicLink(&win32Name, &devName);
    if (!NT_SUCCESS(rc))
    {
        dprintf(("VBoxGuest::VBoxGuestAddDevice: IoCreateSymbolicLink failed with rc=%#x!\n", rc));
        IoDeleteDevice(deviceObject);
        return rc;
    }

    /*
     * Setup the device extension.
     */
    PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)deviceObject->DeviceExtension;
    RtlZeroMemory(pDevExt, sizeof(VBOXGUESTDEVEXT));

    pDevExt->deviceObject = deviceObject;
    pDevExt->devState = STOPPED;

    pDevExt->nextLowerDriver = IoAttachDeviceToDeviceStack(deviceObject, pDevObj);
    if (pDevExt->nextLowerDriver == NULL)
    {
        dprintf(("VBoxGuest::VBoxGuestAddDevice: IoAttachDeviceToDeviceStack did not give a nextLowerDrive\n"));
        IoDeleteSymbolicLink(&win32Name);
        IoDeleteDevice(deviceObject);
        return STATUS_DEVICE_NOT_CONNECTED;
    }

#ifdef VBOX_WITH_HGCM
    int rc2 = RTSpinlockCreate(&pDevExt->SessionSpinlock);
    if (RT_FAILURE(rc2))
    {
        dprintf(("VBoxGuest::VBoxGuestAddDevice: RTSpinlockCreate failed\n"));
        IoDetachDevice(pDevExt->nextLowerDriver);
        IoDeleteSymbolicLink(&win32Name);
        IoDeleteDevice(deviceObject);
        return STATUS_DRIVER_UNABLE_TO_LOAD;
    }
#endif

#ifdef VBOX_WITH_GUEST_BUGCHECK_DETECTION
    hlpRegisterBugCheckCallback(pDevExt); /* ignore failure! */
#endif

    /* Driver is ready now. */
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    dprintf(("VBoxGuest::VBoxGuestAddDevice: returning with rc = 0x%x\n", rc));
    return rc;
}
#endif


/**
 * Unload the driver.
 *
 * @param   pDrvObj     Driver object.
 */
void VBoxGuestUnload(PDRIVER_OBJECT pDrvObj)
{
    dprintf(("VBoxGuest::VBoxGuestUnload\n"));
#ifdef TARGET_NT4
    PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)pDrvObj->DeviceObject->DeviceExtension;
    unreserveHypervisorMemory(pDevExt);
    if (pDevExt->workerThread)
    {
        dprintf(("VBoxGuest::VBoxGuestUnload: waiting for the worker thread to terminate...\n"));
        pDevExt->stopThread = TRUE;
        KeSetEvent(&pDevExt->workerThreadRequest, 0, FALSE);
        KeWaitForSingleObject(pDevExt->workerThread,
                              Executive, KernelMode, FALSE, NULL);
        dprintf(("VBoxGuest::VBoxGuestUnload: returned from KeWaitForSingleObject for worker thread\n"));
    }
    if (pDevExt->idleThread)
    {
        dprintf(("VBoxGuest::VBoxGuestUnload: waiting for the idle thread to terminate...\n"));
        pDevExt->stopThread = TRUE;
        KeWaitForSingleObject(pDevExt->idleThread,
                              Executive, KernelMode, FALSE, NULL);
        dprintf(("VBoxGuest::VBoxGuestUnload: returned from KeWaitForSingleObject for idle thread\n"));
    }

    hlpVBoxUnmapVMMDevMemory (pDevExt);

    VBoxCleanupMemBalloon(pDevExt);

#ifdef VBOX_WITH_GUEST_BUGCHECK_DETECTION
    hlpDeregisterBugCheckCallback(pDevExt); /* ignore failure! */
#endif

    /*
     * I don't think it's possible to unload a driver which processes have
     * opened, at least we'll blindly assume that here.
     */
    UNICODE_STRING win32Name;
    RtlInitUnicodeString(&win32Name, VBOXGUEST_DEVICE_NAME_DOS);
    NTSTATUS rc = IoDeleteSymbolicLink(&win32Name);

#ifdef VBOX_WITH_HGCM
    if (pDevExt->SessionSpinlock != NIL_RTSPINLOCK)
    {
        int rc2 = RTSpinlockDestroy(pDevExt->SessionSpinlock);
        dprintf(("VBoxGuest::VBoxGuestUnload: spinlock destroyed with rc=%Rrc\n", rc2));
    }
#endif
    IoDeleteDevice(pDrvObj->DeviceObject);
#endif

    dprintf(("VBoxGuest::VBoxGuestUnload: returning\n"));
}

/**
 * Create (i.e. Open) file entry point.
 *
 * @param   pDevObj     Device object.
 * @param   pIrp        Request packet.
 */
NTSTATUS VBoxGuestCreate(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    dprintf(("VBoxGuest::VBoxGuestCreate\n"));

    PIO_STACK_LOCATION  pStack = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT        pFileObj = pStack->FileObject;
    PVBOXGUESTDEVEXT    pDevExt = (PVBOXGUESTDEVEXT)pDevObj->DeviceExtension;

    /*
     * We are not remotely similar to a directory...
     * (But this is possible.)
     */
    if (pStack->Parameters.Create.Options & FILE_DIRECTORY_FILE)
    {
        dprintf(("VBoxGuest::VBoxGuestCreate: we're not a directory!\n"));
        pIrp->IoStatus.Status       = STATUS_NOT_A_DIRECTORY;
        pIrp->IoStatus.Information  = 0;
        IoCompleteRequest(pIrp, IO_NO_INCREMENT);
        return STATUS_NOT_A_DIRECTORY;
    }

#ifdef VBOX_WITH_HGCM
    if (pFileObj)
    {
        PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)RTMemAllocZ(sizeof(*pSession));
        if (RT_UNLIKELY(!pSession))
        {
            dprintf(("VBoxGuestCreate: no memory!\n"));
            pIrp->IoStatus.Status       = STATUS_NO_MEMORY;
            pIrp->IoStatus.Information  = 0;
            IoCompleteRequest(pIrp, IO_NO_INCREMENT);
            return STATUS_NO_MEMORY;
        }

        pFileObj->FsContext = pSession;
        dprintf(("VBoxGuestCreate: pDevExt=%p pFileObj=%p pSession=%p\n",
                 pDevExt, pFileObj, pFileObj->FsContext));
    }
#endif

    NTSTATUS    rcNt = pIrp->IoStatus.Status = STATUS_SUCCESS;
    pIrp->IoStatus.Information  = 0;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    dprintf(("VBoxGuest::VBoxGuestCreate: returning 0x%x\n", rcNt));
    return rcNt;
}


/**
 * Close file entry point.
 *
 * @param   pDevObj     Device object.
 * @param   pIrp        Request packet.
 */
NTSTATUS VBoxGuestClose(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    dprintf(("VBoxGuest::VBoxGuestClose\n"));

    PVBOXGUESTDEVEXT   pDevExt  = (PVBOXGUESTDEVEXT)pDevObj->DeviceExtension;
    PIO_STACK_LOCATION pStack   = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT       pFileObj = pStack->FileObject;
    dprintf(("VBoxGuest::VBoxGuestClose: pDevExt=%p pFileObj=%p pSession=%p\n",
             pDevExt, pFileObj, pFileObj->FsContext));

#ifdef VBOX_WITH_HGCM
    if (pFileObj)
    {
        PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)pFileObj->FsContext;
        if (RT_UNLIKELY(!pSession))
        {
            dprintf(("VBoxGuestClose: no FsContext!\n"));
        }
        else
        {
            for (unsigned i = 0; i < RT_ELEMENTS(pSession->aHGCMClientIds); i++)
                if (pSession->aHGCMClientIds[i])
                {
                    VBoxGuestHGCMDisconnectInfo Info;
                    Info.result = 0;
                    Info.u32ClientID = pSession->aHGCMClientIds[i];
                    pSession->aHGCMClientIds[i] = 0;
                    dprintf(("VBoxGuestClose: disconnecting HGCM client id %#RX32\n", Info.u32ClientID));
                    VbglR0HGCMInternalDisconnect(&Info, VBoxHGCMCallback, pDevExt, RT_INDEFINITE_WAIT);
                }
            RTMemFree(pSession);
        }
    }
#endif

    pFileObj->FsContext = NULL;
    pIrp->IoStatus.Information = 0;
    pIrp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

#ifdef VBOX_WITH_HGCM
static void VBoxHGCMCallbackWorker (VMMDevHGCMRequestHeader *pHeader, PVBOXGUESTDEVEXT pDevExt,
                                    uint32_t u32Timeout, bool fInterruptible, KPROCESSOR_MODE ProcessorMode)
{
    /* Possible problem with request completion right between the fu32Flags check and KeWaitForSingleObject
     * call; introduce a timeout to make sure we don't wait indefinitely.
     */

    LARGE_INTEGER timeout;
    if (u32Timeout == RT_INDEFINITE_WAIT)
        timeout.QuadPart = pDevExt->HGCMWaitTimeout.QuadPart;
    else
    {
        timeout.QuadPart = u32Timeout;
        timeout.QuadPart *= -10000; /* relative in 100ns units */
    }
    while ((pHeader->fu32Flags & VBOX_HGCM_REQ_DONE) == 0)
    {
        /* Specifying UserMode so killing the user process will abort the wait. */
        NTSTATUS rc = KeWaitForSingleObject (&pDevExt->keventNotification, Executive,
                                             ProcessorMode,
                                             fInterruptible ? TRUE : FALSE, /* Alertable */
                                             &timeout
                                            );
        dprintf(("VBoxHGCMCallback: Wait returned %d fu32Flags=%x\n", rc, pHeader->fu32Flags));

        if (rc == STATUS_TIMEOUT && u32Timeout == RT_INDEFINITE_WAIT)
            continue;

        if (rc != STATUS_WAIT_0)
        {
            dprintf(("VBoxHGCMCallback: The external event was signalled or the wait timed out or terminated rc = 0x%08X.\n", rc));
            break;
        }

        dprintf(("VBoxHGCMCallback: fu32Flags = %08X\n", pHeader->fu32Flags));
    }
    return;
}

DECLVBGL(int) VBoxHGCMCallback (VMMDevHGCMRequestHeader *pHeader, void *pvData, uint32_t u32Data)
{
    PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)pvData;

    dprintf(("VBoxHGCMCallback\n"));
    VBoxHGCMCallbackWorker (pHeader, pDevExt, u32Data, false, UserMode);
    return VINF_SUCCESS;
}

DECLVBGL(int) VBoxHGCMCallbackKernelMode (VMMDevHGCMRequestHeader *pHeader, void *pvData, uint32_t u32Data)
{
    PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)pvData;

    dprintf(("VBoxHGCMCallback\n"));
    VBoxHGCMCallbackWorker (pHeader, pDevExt, u32Data, false, KernelMode);
    return VINF_SUCCESS;
}

DECLVBGL(int) VBoxHGCMCallbackInterruptible (VMMDevHGCMRequestHeader *pHeader, void *pvData,
                                             uint32_t u32Data)
{
    PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)pvData;

    dprintf(("VBoxHGCMCallbackInterruptible\n"));
    VBoxHGCMCallbackWorker (pHeader, pDevExt, u32Data, true, UserMode);
    return VINF_SUCCESS;
}

NTSTATUS vboxHGCMVerifyIOBuffers (PIO_STACK_LOCATION pStack, unsigned cb)
{
    if (pStack->Parameters.DeviceIoControl.OutputBufferLength < cb)
    {
        dprintf(("VBoxGuest::vboxHGCMVerifyIOBuffers: OutputBufferLength %d < %d\n",
                 pStack->Parameters.DeviceIoControl.OutputBufferLength, cb));
        return STATUS_INVALID_PARAMETER;
    }

    if (pStack->Parameters.DeviceIoControl.InputBufferLength < cb)
    {
        dprintf(("VBoxGuest::vboxHGCMVerifyIOBuffers: InputBufferLength %d < %d\n",
                 pStack->Parameters.DeviceIoControl.InputBufferLength, cb));
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

#endif /* VBOX_WITH_HGCM */

static bool IsPowerOfTwo (uint32_t val)
{
    return (val & (val - 1)) == 0;
}

static bool CtlGuestFilterMask (uint32_t u32OrMask, uint32_t u32NotMask)
{
    bool result = false;
    VMMDevCtlGuestFilterMask *req;
    int rc = VbglGRAlloc ((VMMDevRequestHeader **) &req, sizeof (*req),
                          VMMDevReq_CtlGuestFilterMask);

    if (RT_SUCCESS (rc))
    {
        req->u32OrMask = u32OrMask;
        req->u32NotMask = u32NotMask;

        rc = VbglGRPerform (&req->header);
        if (RT_FAILURE (rc) || RT_FAILURE (req->header.rc))
        {
            dprintf (("VBoxGuest::VBoxGuestDeviceControl: error issuing request to VMMDev! "
                      "rc = %d, VMMDev rc = %Rrc\n", rc, req->header.rc));
        }
        else
        {
            result = true;
        }
        VbglGRFree (&req->header);
    }

    return result;
}

#ifdef VBOX_WITH_MANAGEMENT
static int VBoxGuestSetBalloonSize(PVBOXGUESTDEVEXT pDevExt, uint32_t u32BalloonSize)
{
    VMMDevChangeMemBalloon *req = NULL;
    int rc = VINF_SUCCESS;

    if (u32BalloonSize > pDevExt->MemBalloon.cMaxBalloons)
    {
        AssertMsgFailed(("VBoxGuestSetBalloonSize illegal balloon size %d (max=%d)\n", u32BalloonSize, pDevExt->MemBalloon.cMaxBalloons));
        return VERR_INVALID_PARAMETER;
    }

    if (u32BalloonSize == pDevExt->MemBalloon.cBalloons)
        return VINF_SUCCESS;    /* nothing to do */

    /* Allocate request packet */
    rc = VbglGRAlloc((VMMDevRequestHeader **)&req, RT_OFFSETOF(VMMDevChangeMemBalloon, aPhysPage[VMMDEV_MEMORY_BALLOON_CHUNK_PAGES]), VMMDevReq_ChangeMemBalloon);
    if (RT_FAILURE(rc))
        return rc;

    if (u32BalloonSize > pDevExt->MemBalloon.cBalloons)
    {
        /* inflate */
        for (uint32_t i=pDevExt->MemBalloon.cBalloons;i<u32BalloonSize;i++)
        {
#ifndef TARGET_NT4
            /*
             * Use MmAllocatePagesForMdl to specify the range of physical addresses we wish to use.
             */
            PHYSICAL_ADDRESS Zero;
            PHYSICAL_ADDRESS HighAddr;
            Zero.QuadPart = 0;
            HighAddr.QuadPart = _4G - 1;
            PMDL pMdl = MmAllocatePagesForMdl(Zero, HighAddr, Zero, VMMDEV_MEMORY_BALLOON_CHUNK_SIZE);
            if (pMdl)
            {
                if (MmGetMdlByteCount(pMdl) < VMMDEV_MEMORY_BALLOON_CHUNK_SIZE)
                {
                    MmFreePagesFromMdl(pMdl);
                    ExFreePool(pMdl);
                    rc = VERR_NO_MEMORY;
                    goto end;
                }
            }
#else
            PVOID pvBalloon;
            pvBalloon = ExAllocatePool(PagedPool, VMMDEV_MEMORY_BALLOON_CHUNK_SIZE);
            if (!pvBalloon)
            {
                rc = VERR_NO_MEMORY;
                goto end;
            }

            PMDL pMdl = IoAllocateMdl (pvBalloon, VMMDEV_MEMORY_BALLOON_CHUNK_SIZE, FALSE, FALSE, NULL);
            if (pMdl == NULL)
            {
                rc = VERR_NO_MEMORY;
                ExFreePool(pvBalloon);
                AssertMsgFailed(("IoAllocateMdl %p %x failed!!\n", pvBalloon, VMMDEV_MEMORY_BALLOON_CHUNK_SIZE));
                goto end;
            }
            else
            {
                __try {
                    /* Calls to MmProbeAndLockPages must be enclosed in a try/except block. */
                    MmProbeAndLockPages (pMdl, KernelMode, IoModifyAccess);
                }
                __except(EXCEPTION_EXECUTE_HANDLER)
                {
                    dprintf(("MmProbeAndLockPages failed!\n"));
                    rc = VERR_NO_MEMORY;
                    IoFreeMdl (pMdl);
                    ExFreePool(pvBalloon);
                    goto end;
                }
            }
#endif

            PPFN_NUMBER pPageDesc = MmGetMdlPfnArray(pMdl);

            /* Copy manually as RTGCPHYS is always 64 bits */
            for (uint32_t j=0;j<VMMDEV_MEMORY_BALLOON_CHUNK_PAGES;j++)
                req->aPhysPage[j] = pPageDesc[j] << PAGE_SHIFT; /* PFN_NUMBER is physical page nr, so shift left by 12 to get the physical address */

            req->header.size = RT_OFFSETOF(VMMDevChangeMemBalloon, aPhysPage[VMMDEV_MEMORY_BALLOON_CHUNK_PAGES]);
            req->cPages      = VMMDEV_MEMORY_BALLOON_CHUNK_PAGES;
            req->fInflate    = true;

            rc = VbglGRPerform(&req->header);
            if (RT_FAILURE(rc) || RT_FAILURE(req->header.rc))
            {
                dprintf(("VBoxGuest::VBoxGuestSetBalloonSize: error issuing request to VMMDev!"
                         "rc = %d, VMMDev rc = %Rrc\n", rc, req->header.rc));

#ifndef TARGET_NT4
                MmFreePagesFromMdl(pMdl);
                ExFreePool(pMdl);
#else
                IoFreeMdl (pMdl);
                ExFreePool(pvBalloon);
#endif
                goto end;
            }
            else
            {
#ifndef TARGET_NT4
                dprintf(("VBoxGuest::VBoxGuestSetBalloonSize %d MB added chunk at %x\n", i, pMdl));
#else
                dprintf(("VBoxGuest::VBoxGuestSetBalloonSize %d MB added chunk at %x\n", i, pvBalloon));
#endif
                pDevExt->MemBalloon.paMdlMemBalloon[i] = pMdl;
                pDevExt->MemBalloon.cBalloons++;
            }
        }
    }
    else
    {
        /* deflate */
        for (uint32_t _i = pDevExt->MemBalloon.cBalloons - 1; _i > u32BalloonSize; _i--)
        {
            uint32_t index = _i - 1;
            PMDL  pMdl = pDevExt->MemBalloon.paMdlMemBalloon[index];

            Assert(pMdl);
            if (pMdl)
            {
#ifdef TARGET_NT4
                PVOID pvBalloon = MmGetMdlVirtualAddress(pMdl);
#endif

                PPFN_NUMBER pPageDesc = MmGetMdlPfnArray(pMdl);

                /* Copy manually as RTGCPHYS is always 64 bits */
                for (uint32_t j = 0; j < VMMDEV_MEMORY_BALLOON_CHUNK_PAGES; j++)
                    req->aPhysPage[j] = pPageDesc[j] << PAGE_SHIFT; /* PFN_NUMBER is physical page nr, so shift left by 12 to get the physical address */

                req->header.size = RT_OFFSETOF(VMMDevChangeMemBalloon, aPhysPage[VMMDEV_MEMORY_BALLOON_CHUNK_PAGES]);
                req->cPages      = VMMDEV_MEMORY_BALLOON_CHUNK_PAGES;
                req->fInflate    = false;

                rc = VbglGRPerform(&req->header);
                if (RT_FAILURE(rc) || RT_FAILURE(req->header.rc))
                {
                    AssertMsgFailed(("VBoxGuest::VBoxGuestSetBalloonSize: error issuing request to VMMDev! rc = %d, VMMDev rc = %Rrc\n", rc, req->header.rc));
                    break;
                }

                /* Free the ballooned memory */
#ifndef TARGET_NT4
                dprintf(("VBoxGuest::VBoxGuestSetBalloonSize %d MB free chunk at %x\n", index, pMdl));
                MmFreePagesFromMdl(pMdl);
                ExFreePool(pMdl);
#else
                dprintf(("VBoxGuest::VBoxGuestSetBalloonSize %d MB free chunk at %x\n", index, pvBalloon));
                MmUnlockPages (pMdl);
                IoFreeMdl (pMdl);
                ExFreePool(pvBalloon);
#endif

                pDevExt->MemBalloon.paMdlMemBalloon[index] = NULL;
                pDevExt->MemBalloon.cBalloons--;
            }
        }
    }
    Assert(pDevExt->MemBalloon.cBalloons <= pDevExt->MemBalloon.cMaxBalloons);

end:
    VbglGRFree(&req->header);
    return rc;
}

static int VBoxGuestQueryMemoryBalloon(PVBOXGUESTDEVEXT pDevExt, ULONG *pMemBalloonSize)
{
    /* just perform the request */
    VMMDevGetMemBalloonChangeRequest *req = NULL;

    dprintf(("VBoxGuestQueryMemoryBalloon\n"));

    int rc = VbglGRAlloc((VMMDevRequestHeader **)&req, sizeof(VMMDevGetMemBalloonChangeRequest), VMMDevReq_GetMemBalloonChangeRequest);
    req->eventAck = VMMDEV_EVENT_BALLOON_CHANGE_REQUEST;

    if (RT_SUCCESS(rc))
    {
        rc = VbglGRPerform(&req->header);

        if (RT_FAILURE(rc) || RT_FAILURE(req->header.rc))
        {
            dprintf(("VBoxGuest::VBoxGuestDeviceControl VBOXGUEST_IOCTL_CTL_CHECK_BALLOON: error issuing request to VMMDev!"
                     "rc = %d, VMMDev rc = %Rrc\n", rc, req->header.rc));
        }
        else
        {
            if (!pDevExt->MemBalloon.paMdlMemBalloon)
            {
                pDevExt->MemBalloon.cMaxBalloons = req->u32PhysMemSize;
                pDevExt->MemBalloon.paMdlMemBalloon = (PMDL *)ExAllocatePool(PagedPool, req->u32PhysMemSize * sizeof(PMDL));
                Assert(pDevExt->MemBalloon.paMdlMemBalloon);
                if (!pDevExt->MemBalloon.paMdlMemBalloon)
                    return VERR_NO_MEMORY;
            }
            Assert(pDevExt->MemBalloon.cMaxBalloons == req->u32PhysMemSize);

            rc = VBoxGuestSetBalloonSize(pDevExt, req->u32BalloonSize);
            /* ignore out of memory failures */
            if (rc == VERR_NO_MEMORY)
                rc = VINF_SUCCESS;

            if (pMemBalloonSize)
                *pMemBalloonSize = pDevExt->MemBalloon.cBalloons;
        }

        VbglGRFree(&req->header);
    }
    return rc;
}
#endif

void VBoxInitMemBalloon(PVBOXGUESTDEVEXT pDevExt)
{
#ifdef VBOX_WITH_MANAGEMENT
    ULONG dummy;

    pDevExt->MemBalloon.cBalloons       = 0;
    pDevExt->MemBalloon.cMaxBalloons    = 0;
    pDevExt->MemBalloon.paMdlMemBalloon = NULL;

    VBoxGuestQueryMemoryBalloon(pDevExt, &dummy);
#endif
}

void VBoxCleanupMemBalloon(PVBOXGUESTDEVEXT pDevExt)
{
#ifdef VBOX_WITH_MANAGEMENT
    if (pDevExt->MemBalloon.paMdlMemBalloon)
    {
        /* Clean up the memory balloon leftovers */
        VBoxGuestSetBalloonSize(pDevExt, 0);
        ExFreePool(pDevExt->MemBalloon.paMdlMemBalloon);
        pDevExt->MemBalloon.paMdlMemBalloon = NULL;
    }
    Assert(pDevExt->MemBalloon.cBalloons == 0);
#endif
}

/** A quick implementation of AtomicTestAndClear for uint32_t and multiple
 *  bits.
 */
static uint32_t guestAtomicBitsTestAndClear(void *pu32Bits, uint32_t u32Mask)
{
    AssertPtrReturn(pu32Bits, 0);
    LogFlowFunc(("*pu32Bits=0x%x, u32Mask=0x%x\n", *(long *)pu32Bits,
                 u32Mask));
    uint32_t u32Result = 0;
    uint32_t u32WorkingMask = u32Mask;
    int iBitOffset = ASMBitFirstSetU32 (u32WorkingMask);

    while (iBitOffset > 0)
    {
        bool fSet = ASMAtomicBitTestAndClear(pu32Bits, iBitOffset - 1);
        if (fSet)
            u32Result |= 1 << (iBitOffset - 1);
        u32WorkingMask &= ~(1 << (iBitOffset - 1));
        iBitOffset = ASMBitFirstSetU32 (u32WorkingMask);
    }
    LogFlowFunc(("Returning 0x%x\n", u32Result));
    return u32Result;
}

/**
 * Device I/O Control entry point.
 *
 * @param   pDevObj     Device object.
 * @param   pIrp        Request packet.
 */
NTSTATUS VBoxGuestDeviceControl(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    dprintf(("VBoxGuest::VBoxGuestDeviceControl\n"));

    NTSTATUS Status = STATUS_SUCCESS;

    PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)pDevObj->DeviceExtension;

    PIO_STACK_LOCATION pStack = IoGetCurrentIrpStackLocation(pIrp);

    char *pBuf = (char *)pIrp->AssociatedIrp.SystemBuffer; /* all requests are buffered. */

    unsigned cbOut = 0;

    switch (pStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case VBOXGUEST_IOCTL_GETVMMDEVPORT:
        {
            dprintf(("VBoxGuest::VBoxGuestDeviceControl: VBOXGUEST_IOCTL_GETVMMDEVPORT\n"));

            if (pStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof (VBoxGuestPortInfo))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            VBoxGuestPortInfo *portInfo = (VBoxGuestPortInfo*)pBuf;

            portInfo->portAddress = pDevExt->startPortAddress;
            portInfo->pVMMDevMemory = pDevExt->pVMMDevMemory;

            cbOut = sizeof(VBoxGuestPortInfo);

            break;
        }

        case VBOXGUEST_IOCTL_WAITEVENT:
        {
            /* Need to be extended to support multiple waiters for an event,
             * array of counters for each event, event mask is computed, each
             * time a wait event is arrived.
             */
            dprintf(("VBoxGuest::VBoxGuestDeviceControl: VBOXGUEST_IOCTL_WAITEVENT\n"));

            if (pStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(VBoxGuestWaitEventInfo))
            {
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: OutputBufferLength %d < sizeof(VBoxGuestWaitEventInfo)\n",
                         pStack->Parameters.DeviceIoControl.OutputBufferLength, sizeof(VBoxGuestWaitEventInfo)));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (pStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(VBoxGuestWaitEventInfo)) {
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: InputBufferLength %d < sizeof(VBoxGuestWaitEventInfo)\n",
                         pStack->Parameters.DeviceIoControl.InputBufferLength, sizeof(VBoxGuestWaitEventInfo)));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            VBoxGuestWaitEventInfo *eventInfo = (VBoxGuestWaitEventInfo *)pBuf;

            if (!eventInfo->u32EventMaskIn) {
                dprintf (("VBoxGuest::VBoxGuestDeviceControl: Invalid input mask %#x\n",
                          eventInfo->u32EventMaskIn));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            eventInfo->u32EventFlagsOut = 0;
            bool fTimeout = (eventInfo->u32TimeoutIn != ~0L);

            /* Possible problem with request completion right between the pending event check and KeWaitForSingleObject
             * call; introduce a timeout (if none was specified) to make sure we don't wait indefinitely.
             */
            LARGE_INTEGER timeout;
            timeout.QuadPart = (fTimeout) ? eventInfo->u32TimeoutIn : 250;
            timeout.QuadPart *= -10000;

            NTSTATUS rc = STATUS_SUCCESS;

            for (;;)
            {
                uint32_t u32EventsPending =
                    guestAtomicBitsTestAndClear(&pDevExt->u32Events,
                                                eventInfo->u32EventMaskIn);
                dprintf (("mask = 0x%x, pending = 0x%x\n",
                          eventInfo->u32EventMaskIn, u32EventsPending));

                if (u32EventsPending)
                {
                    eventInfo->u32EventFlagsOut = u32EventsPending;
                    break;
                }

                rc = KeWaitForSingleObject (&pDevExt->keventNotification, Executive /** @todo UserRequest? */,
                                            KernelMode, TRUE, &timeout);
                dprintf(("VBOXGUEST_IOCTL_WAITEVENT: Wait returned %d -> event %x\n", rc, eventInfo->u32EventFlagsOut));

                if (!fTimeout && rc == STATUS_TIMEOUT)
                    continue;

                if (rc != STATUS_SUCCESS)
                {
                    /* There was a timeout or wait was interrupted, etc. */
                    break;
                }
            }

            dprintf (("u32EventFlagsOut = %#x\n", eventInfo->u32EventFlagsOut));
            cbOut = sizeof(VBoxGuestWaitEventInfo);
            break;
        }

        case VBOXGUEST_IOCTL_VMMREQUEST(0): /* (The size isn't relevant on NT.)*/
        {
            dprintf(("VBoxGuest::VBoxGuestDeviceControl: VBOXGUEST_IOCTL_VMMREQUEST\n"));

#define CHECK_SIZE(s) \
            if (pStack->Parameters.DeviceIoControl.OutputBufferLength < s) \
            { \
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: OutputBufferLength %d < %d\n", \
                         pStack->Parameters.DeviceIoControl.OutputBufferLength, s)); \
                Status = STATUS_BUFFER_TOO_SMALL; \
                break; \
            } \
            if (pStack->Parameters.DeviceIoControl.InputBufferLength < s) { \
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: InputBufferLength %d < %d\n", \
                         pStack->Parameters.DeviceIoControl.InputBufferLength, s)); \
                Status = STATUS_BUFFER_TOO_SMALL; \
                break; \
            }

            /* get the request header */
            CHECK_SIZE(sizeof(VMMDevRequestHeader));
            VMMDevRequestHeader *requestHeader = (VMMDevRequestHeader *)pBuf;
            if (!vmmdevGetRequestSize(requestHeader->requestType))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            /* make sure the buffers suit the request */
            CHECK_SIZE(vmmdevGetRequestSize(requestHeader->requestType));

            int rc = VbglGRVerify(requestHeader, requestHeader->size);
            if (RT_FAILURE(rc))
            {
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: VMMREQUEST: invalid header: size %#x, expected >= %#x (hdr); type=%#x; rc %d!!\n",
                     requestHeader->size, vmmdevGetRequestSize(requestHeader->requestType), requestHeader->requestType, rc));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* just perform the request */
            VMMDevRequestHeader *req = NULL;

            rc = VbglGRAlloc((VMMDevRequestHeader **)&req, requestHeader->size, requestHeader->requestType);

            if (RT_SUCCESS(rc))
            {
                /* copy the request information */
                memcpy((void*)req, (void*)pBuf, requestHeader->size);
                rc = VbglGRPerform(req);

                if (RT_FAILURE(rc) || RT_FAILURE(req->rc))
                {
                    dprintf(("VBoxGuest::VBoxGuestDeviceControl VBOXGUEST_IOCTL_VMMREQUEST: Error issuing request to VMMDev! "
                             "rc = %d, VMMDev rc = %Rrc\n", rc, req->rc));
                    Status = STATUS_UNSUCCESSFUL;
                }
                else
                {
                    /* copy result */
                    memcpy((void*)pBuf, (void*)req, requestHeader->size);
                    cbOut = requestHeader->size;
                }

                VbglGRFree(req);
            }
            else
            {
                Status = STATUS_UNSUCCESSFUL;
            }
#undef CHECK_SIZE
            break;
        }

        case VBOXGUEST_IOCTL_CTL_FILTER_MASK:
        {
            VBoxGuestFilterMaskInfo *maskInfo;

            if (pStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(VBoxGuestFilterMaskInfo)) {
                dprintf (("VBoxGuest::VBoxGuestDeviceControl: InputBufferLength %d < %d\n",
                          pStack->Parameters.DeviceIoControl.InputBufferLength,
                          sizeof (VBoxGuestFilterMaskInfo)));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;

            }

            maskInfo = (VBoxGuestFilterMaskInfo *) pBuf;
            if (!CtlGuestFilterMask (maskInfo->u32OrMask, maskInfo->u32NotMask))
            {
                Status = STATUS_UNSUCCESSFUL;
            }
            break;
        }

#ifdef VBOX_WITH_HGCM
        /* HGCM offers blocking IOCTLSs just like waitevent and actually
         * uses the same waiting code.
         */
#ifdef RT_ARCH_AMD64
        case VBOXGUEST_IOCTL_HGCM_CONNECT_32:
#endif /* RT_ARCH_AMD64 */
        case VBOXGUEST_IOCTL_HGCM_CONNECT:
        {
            dprintf(("VBoxGuest::VBoxGuestDeviceControl: VBOXGUEST_IOCTL_HGCM_CONNECT\n"));

            if (pStack->Parameters.DeviceIoControl.OutputBufferLength != sizeof(VBoxGuestHGCMConnectInfo))
            {
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: OutputBufferLength %d != sizeof(VBoxGuestHGCMConnectInfo) %d\n",
                         pStack->Parameters.DeviceIoControl.OutputBufferLength, sizeof(VBoxGuestHGCMConnectInfo)));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (pStack->Parameters.DeviceIoControl.InputBufferLength != sizeof(VBoxGuestHGCMConnectInfo)) {
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: InputBufferLength %d != sizeof(VBoxGuestHGCMConnectInfo) %d\n",
                         pStack->Parameters.DeviceIoControl.InputBufferLength, sizeof(VBoxGuestHGCMConnectInfo)));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            VBoxGuestHGCMConnectInfo *ptr = (VBoxGuestHGCMConnectInfo *)pBuf;

            /* If request will be processed asynchronously, execution will
             * go to VBoxHGCMCallback. There it will wait for the request event, signalled from IRQ.
             * On IRQ arrival, the VBoxHGCMCallback(s) will check the request memory and, if completion
             * flag is set, returns.
             */

            dprintf(("a) ptr->u32ClientID = %d\n", ptr->u32ClientID));

            int rc = VbglR0HGCMInternalConnect (ptr, pIrp->RequestorMode == KernelMode? VBoxHGCMCallbackKernelMode :VBoxHGCMCallback,
                                                pDevExt, RT_INDEFINITE_WAIT);

            dprintf(("b) ptr->u32ClientID = %d\n", ptr->u32ClientID));

            if (RT_FAILURE(rc))
            {
                dprintf(("VBOXGUEST_IOCTL_HGCM_CONNECT: vbox rc = %Rrc\n", rc));
                Status = STATUS_UNSUCCESSFUL;
            }
            else
            {
                cbOut = pStack->Parameters.DeviceIoControl.OutputBufferLength;

                if (RT_SUCCESS(ptr->result) && pStack->FileObject)
                {
                    dprintf(("VBOXGUEST_IOCTL_HGCM_CONNECT: pDevExt=%p pFileObj=%p pSession=%p\n",
                             pDevExt, pStack->FileObject, pStack->FileObject->FsContext));

                    /*
                     * Append the client id to the client id table.
                     * If the table has somehow become filled up, we'll disconnect the session.
                     */
                    unsigned i;
                    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)pStack->FileObject->FsContext;
                    RTSPINLOCKTMP Tmp = RTSPINLOCKTMP_INITIALIZER;

                    RTSpinlockAcquireNoInts(pDevExt->SessionSpinlock, &Tmp);
                    for (i = 0; i < RT_ELEMENTS(pSession->aHGCMClientIds); i++)
                        if (!pSession->aHGCMClientIds[i])
                        {
                            pSession->aHGCMClientIds[i] = ptr->u32ClientID;
                            break;
                        }
                    RTSpinlockReleaseNoInts(pDevExt->SessionSpinlock, &Tmp);

                    if (i >= RT_ELEMENTS(pSession->aHGCMClientIds))
                    {
                        static unsigned s_cErrors = 0;
                        if (s_cErrors++ < 32)
                            dprintf(("VBoxGuestCommonIOCtl: HGCM_CONNECT: too many HGCMConnect calls for one session!\n"));

                        VBoxGuestHGCMDisconnectInfo Info;
                        Info.result = 0;
                        Info.u32ClientID = ptr->u32ClientID;
                        VbglR0HGCMInternalDisconnect(&Info, pIrp->RequestorMode == KernelMode? VBoxHGCMCallbackKernelMode :VBoxHGCMCallback, pDevExt, RT_INDEFINITE_WAIT);
                        Status = STATUS_UNSUCCESSFUL;
                        break;
                    }
                }
                else
                {
                    /* @fixme, r=Leonid. I have no clue what to do in cases where
                     * pStack->FileObject==NULL. Can't populate list of HGCM ID's...
                     * But things worked before, so do nothing for now.
                     */
                    dprintf(("VBOXGUEST_IOCTL_HGCM_CONNECT: pDevExt=%p, pStack->FileObject=%p\n", pDevExt, pStack->FileObject));
                }
            }

        } break;

#ifdef RT_ARCH_AMD64
        case VBOXGUEST_IOCTL_HGCM_DISCONNECT_32:
#endif /* RT_ARCH_AMD64 */
        case VBOXGUEST_IOCTL_HGCM_DISCONNECT:
        {
            dprintf(("VBoxGuest::VBoxGuestDeviceControl: VBOXGUEST_IOCTL_HGCM_DISCONNECT\n"));

            if (pStack->Parameters.DeviceIoControl.OutputBufferLength != sizeof(VBoxGuestHGCMDisconnectInfo))
            {
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: OutputBufferLength %d != sizeof(VBoxGuestHGCMDisconnectInfo) %d\n",
                         pStack->Parameters.DeviceIoControl.OutputBufferLength, sizeof(VBoxGuestHGCMDisconnectInfo)));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (pStack->Parameters.DeviceIoControl.InputBufferLength != sizeof(VBoxGuestHGCMDisconnectInfo)) {
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: InputBufferLength %d != sizeof(VBoxGuestHGCMDisconnectInfo) %d\n",
                         pStack->Parameters.DeviceIoControl.InputBufferLength, sizeof(VBoxGuestHGCMDisconnectInfo)));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            VBoxGuestHGCMDisconnectInfo *ptr = (VBoxGuestHGCMDisconnectInfo *)pBuf;

            uint32_t u32ClientId=0;
            unsigned i=0;
            PVBOXGUESTSESSION pSession=0;
            RTSPINLOCKTMP Tmp = RTSPINLOCKTMP_INITIALIZER;

            /* See comment in VBOXGUEST_IOCTL_HGCM_CONNECT */
            if (pStack->FileObject)
            {
                dprintf(("VBOXGUEST_IOCTL_HGCM_DISCONNECT: pDevExt=%p pFileObj=%p pSession=%p\n",
                         pDevExt, pStack->FileObject, pStack->FileObject->FsContext));

                u32ClientId = ptr->u32ClientID;
                pSession = (PVBOXGUESTSESSION)pStack->FileObject->FsContext;

                RTSpinlockAcquireNoInts(pDevExt->SessionSpinlock, &Tmp);
                for (i = 0; i < RT_ELEMENTS(pSession->aHGCMClientIds); i++)
                    if (pSession->aHGCMClientIds[i] == u32ClientId)
                    {
                        pSession->aHGCMClientIds[i] = UINT32_MAX;
                        break;
                    }
                RTSpinlockReleaseNoInts(pDevExt->SessionSpinlock, &Tmp);
                if (i >= RT_ELEMENTS(pSession->aHGCMClientIds))
                {
                    static unsigned s_cErrors = 0;
                    if (s_cErrors++ > 32)
                        dprintf(("VBoxGuestCommonIOCtl: HGCM_DISCONNECT: u32Client=%RX32\n", u32ClientId));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
            }

            /* If request will be processed asynchronously, execution will
             * go to VBoxHGCMCallback. There it will wait for the request event, signalled from IRQ.
             * On IRQ arrival, the VBoxHGCMCallback(s) will check the request memory and, if completion
             * flag is set, returns.
             */

            int rc = VbglR0HGCMInternalDisconnect (ptr, pIrp->RequestorMode == KernelMode? VBoxHGCMCallbackKernelMode :VBoxHGCMCallback, pDevExt, RT_INDEFINITE_WAIT);

            if (RT_FAILURE(rc))
            {
                dprintf(("VBOXGUEST_IOCTL_HGCM_DISCONNECT: vbox rc = %Rrc\n", rc));
                Status = STATUS_UNSUCCESSFUL;
            }
            else
            {
                cbOut = pStack->Parameters.DeviceIoControl.OutputBufferLength;
            }

            if (pStack->FileObject)
            {
                RTSpinlockAcquireNoInts(pDevExt->SessionSpinlock, &Tmp);
                if (pSession->aHGCMClientIds[i] == UINT32_MAX)
                    pSession->aHGCMClientIds[i] = RT_SUCCESS(rc) && RT_SUCCESS(ptr->result) ? 0 : u32ClientId;
                RTSpinlockReleaseNoInts(pDevExt->SessionSpinlock, &Tmp);
            }
        } break;

#ifdef RT_ARCH_AMD64
        case VBOXGUEST_IOCTL_HGCM_CALL_32(0): /* (The size isn't relevant on NT.) */
        {
            /* A 32 bit application call. */
            int rc;

            dprintf(("VBoxGuest::VBoxGuestDeviceControl: VBOXGUEST_IOCTL_HGCM_CALL_32\n"));

            Status = vboxHGCMVerifyIOBuffers (pStack,
                                              sizeof (VBoxGuestHGCMCallInfo));

            if (Status != STATUS_SUCCESS)
            {
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: invalid parameter. Status: %p\n", Status));
                break;
            }

            /* @todo: Old guest OpenGL driver used the same IOCtl code for both 32 and 64 bit binaries.
             *        This is a protection, and can be removed if there were no 64 bit driver.
             */
            if (!IoIs32bitProcess(pIrp))
            {
                Status = STATUS_UNSUCCESSFUL;
                break;
            }

            VBoxGuestHGCMCallInfo *ptr = (VBoxGuestHGCMCallInfo *)pBuf;
            uint32_t fFlags = pIrp->RequestorMode == KernelMode ? VBGLR0_HGCMCALL_F_KERNEL : VBGLR0_HGCMCALL_F_USER;

            rc = VbglR0HGCMInternalCall32(ptr, pStack->Parameters.DeviceIoControl.InputBufferLength, fFlags,
                                          pIrp->RequestorMode == KernelMode? VBoxHGCMCallbackKernelMode :VBoxHGCMCallback,
                                          pDevExt, RT_INDEFINITE_WAIT);

            if (RT_FAILURE(rc))
            {
                dprintf(("VBOXGUEST_IOCTL_HGCM_CALL_32: vbox rc = %Rrc\n", rc));
                Status = STATUS_UNSUCCESSFUL;
            }
            else
            {
                cbOut = pStack->Parameters.DeviceIoControl.OutputBufferLength;
            }

        } break;
#endif /* RT_ARCH_AMD64 */

        case VBOXGUEST_IOCTL_HGCM_CALL(0): /* (The size isn't relevant on NT.) */
        {
            int rc;

            dprintf(("VBoxGuest::VBoxGuestDeviceControl: VBOXGUEST_IOCTL_HGCM_CALL\n"));

            Status = vboxHGCMVerifyIOBuffers (pStack,
                                              sizeof (VBoxGuestHGCMCallInfo));

            if (Status != STATUS_SUCCESS)
            {
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: invalid parameter. Status: %p\n", Status));
                break;
            }

            VBoxGuestHGCMCallInfo *ptr = (VBoxGuestHGCMCallInfo *)pBuf;
            uint32_t fFlags = pIrp->RequestorMode == KernelMode ? VBGLR0_HGCMCALL_F_KERNEL : VBGLR0_HGCMCALL_F_USER;

            rc = VbglR0HGCMInternalCall (ptr, pStack->Parameters.DeviceIoControl.InputBufferLength, fFlags,
                                         pIrp->RequestorMode == KernelMode? VBoxHGCMCallbackKernelMode :VBoxHGCMCallback,
                                         pDevExt, RT_INDEFINITE_WAIT);

            if (RT_FAILURE(rc))
            {
                dprintf(("VBOXGUEST_IOCTL_HGCM_CALL: vbox rc = %Rrc\n", rc));
                Status = STATUS_UNSUCCESSFUL;
            }
            else
            {
                cbOut = pStack->Parameters.DeviceIoControl.OutputBufferLength;
            }

        } break;

        case VBOXGUEST_IOCTL_HGCM_CALL_TIMED(0): /* (The size isn't relevant on NT.) */
        {
            /* This IOCTL is not used by shared folders, so VBoxHGCMCallbackKernelMode is not used. */
            dprintf(("VBoxGuest::VBoxGuestDeviceControl: VBOXGUEST_IOCTL_HGCM_CALL_TIMED\n"));

            Status = vboxHGCMVerifyIOBuffers (pStack,
                                              sizeof (VBoxGuestHGCMCallInfoTimed));

            if (Status != STATUS_SUCCESS)
            {
                dprintf(("nvalid parameter. Status: %p\n", Status));
                break;
            }

            VBoxGuestHGCMCallInfoTimed *pInfo = (VBoxGuestHGCMCallInfoTimed *)pBuf;
            VBoxGuestHGCMCallInfo *ptr = &pInfo->info;

            int rc;
            uint32_t fFlags = pIrp->RequestorMode == KernelMode ? VBGLR0_HGCMCALL_F_KERNEL : VBGLR0_HGCMCALL_F_USER;
            if (pInfo->fInterruptible)
            {
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: calling VBoxHGCMCall interruptible, timeout %lu ms\n",
                         pInfo->u32Timeout));
                rc = VbglR0HGCMInternalCall (ptr, pStack->Parameters.DeviceIoControl.InputBufferLength, fFlags,
                                             VBoxHGCMCallbackInterruptible, pDevExt, pInfo->u32Timeout);
            }
            else
            {
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: calling VBoxHGCMCall, timeout %lu ms\n",
                         pInfo->u32Timeout));
                rc = VbglR0HGCMInternalCall (ptr, pStack->Parameters.DeviceIoControl.InputBufferLength, fFlags,
                                             VBoxHGCMCallback, pDevExt, pInfo->u32Timeout);
            }

            if (RT_FAILURE(rc))
            {
                dprintf(("VBOXGUEST_IOCTL_HGCM_CALL_TIMED: vbox rc = %Rrc\n", rc));
                Status = STATUS_UNSUCCESSFUL;
            }
            else
            {
                cbOut = pStack->Parameters.DeviceIoControl.OutputBufferLength;
            }

        } break;
#endif /* VBOX_WITH_HGCM */

#ifdef VBOX_WITH_VRDP_SESSION_HANDLING
        case VBOXGUEST_IOCTL_ENABLE_VRDP_SESSION:
        {
            LogRel(("VRDP_SESSION: Enable. Currently: %sabled\n", pDevExt->fVRDPEnabled? "en": "dis"));
            if (!pDevExt->fVRDPEnabled)
            {
                KUSER_SHARED_DATA *pSharedUserData = (KUSER_SHARED_DATA *)KI_USER_SHARED_DATA;

                pDevExt->fVRDPEnabled            = TRUE;
                LogRel(("VRDP_SESSION: Current active console id: 0x%08X\n", pSharedUserData->ActiveConsoleId));
                pDevExt->ulOldActiveConsoleId    = pSharedUserData->ActiveConsoleId;
                pSharedUserData->ActiveConsoleId = 2;
            }
            break;
        }

        case VBOXGUEST_IOCTL_DISABLE_VRDP_SESSION:
        {
            LogRel(("VRDP_SESSION: Disable. Currently: %sabled\n", pDevExt->fVRDPEnabled? "en": "dis"));
            if (pDevExt->fVRDPEnabled)
            {
                KUSER_SHARED_DATA *pSharedUserData = (KUSER_SHARED_DATA *)KI_USER_SHARED_DATA;

                pDevExt->fVRDPEnabled            = FALSE;
                LogRel(("VRDP_SESSION: Current active console id: 0x%08X\n", pSharedUserData->ActiveConsoleId));
                pSharedUserData->ActiveConsoleId = pDevExt->ulOldActiveConsoleId;
                pDevExt->ulOldActiveConsoleId    = 0;
            }
            break;
        }
#endif

#ifdef VBOX_WITH_MANAGEMENT
        case VBOXGUEST_IOCTL_CTL_CHECK_BALLOON_MASK:
        {
            ULONG *pMemBalloonSize = (ULONG *) pBuf;

            if (pStack->Parameters.DeviceIoControl.OutputBufferLength != sizeof(ULONG))
            {
                dprintf(("VBoxGuest::VBoxGuestDeviceControl: OutputBufferLength %d != sizeof(ULONG) %d\n",
                         pStack->Parameters.DeviceIoControl.OutputBufferLength, sizeof(ULONG)));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            int rc = VBoxGuestQueryMemoryBalloon(pDevExt, pMemBalloonSize);
            if (RT_FAILURE(rc))
            {
                dprintf(("VBOXGUEST_IOCTL_CTL_CHECK_BALLOON: vbox rc = %Rrc\n", rc));
                Status = STATUS_UNSUCCESSFUL;
            }
            else
            {
                cbOut = pStack->Parameters.DeviceIoControl.OutputBufferLength;
            }
            break;
        }
#endif

        case VBOXGUEST_IOCTL_LOG(0):    /* The size isn't relevant on NT. */
        {
            /* Enable this only for debugging:
            dprintf(("VBoxGuest::VBoxGuestDeviceControl: VBOXGUEST_IOCTL_LOG %.*s\n", (int)pStack->Parameters.DeviceIoControl.InputBufferLength, pBuf));
             */
            LogRel(("%.*s", (int)pStack->Parameters.DeviceIoControl.InputBufferLength, pBuf));
            cbOut = 0;
            break;
        }

        default:
             Status = STATUS_INVALID_PARAMETER;
             break;
    }

    pIrp->IoStatus.Status = Status;
    pIrp->IoStatus.Information = cbOut;

    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    dprintf(("VBoxGuest::VBoxGuestDeviceControl: returned cbOut=%d rc=%#x\n", cbOut, Status));

    return Status;
}


/**
 * IRP_MJ_SYSTEM_CONTROL handler
 *
 * @returns NT status code
 * @param   pDevObj     Device object.
 * @param   pIrp        IRP.
 */
NTSTATUS VBoxGuestSystemControl(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)pDevObj->DeviceExtension;

    dprintf(("VBoxGuest::VBoxGuestSystemControl\n"));

    /* Always pass it on to the next driver. */
    IoSkipCurrentIrpStackLocation(pIrp);

    return IoCallDriver(pDevExt->nextLowerDriver, pIrp);
}

/**
 * IRP_MJ_SHUTDOWN handler
 *
 * @returns NT status code
 * @param pDevObj    Device object.
 * @param pIrp       IRP.
 */
NTSTATUS VBoxGuestShutdown(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)pDevObj->DeviceExtension;

    dprintf(("VBoxGuest::VBoxGuestShutdown\n"));

    if (pDevExt && pDevExt->powerStateRequest)
    {
        VMMDevPowerStateRequest *req = pDevExt->powerStateRequest;

        req->header.requestType = VMMDevReq_SetPowerStatus;
        req->powerState = VMMDevPowerState_PowerOff;

        int rc = VbglGRPerform (&req->header);

        if (RT_FAILURE(rc) || RT_FAILURE(req->header.rc))
        {
            dprintf(("VBoxGuest::PowerStateRequest: error performing request to VMMDev."
                      "rc = %d, VMMDev rc = %Rrc\n", rc, req->header.rc));
        }
    }

    return STATUS_SUCCESS;
}

/**
 * Stub function for functions we don't implemented.
 *
 * @returns STATUS_NOT_SUPPORTED
 * @param   pDevObj     Device object.
 * @param   pIrp        IRP.
 */
NTSTATUS VBoxGuestNotSupportedStub(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    dprintf(("VBoxGuest::VBoxGuestNotSupportedStub\n"));
    pDevObj = pDevObj;

    pIrp->IoStatus.Information = 0;
    pIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return STATUS_NOT_SUPPORTED;
}

/**
 * DPC handler
 *
 * @param   dpc         DPC descriptor.
 * @param   pDevObj     Device object.
 * @param   irp         Interrupt request packet.
 * @param   context     Context specific pointer.
 */
VOID VBoxGuestDpcHandler(PKDPC dpc, PDEVICE_OBJECT pDevObj,
                         PIRP irp, PVOID context)
{
    /* Unblock handlers waiting for arrived events.
     *
     * Events are very low things, there is one event flag (1 or more bit)
     * for each event. Each event is processed by exactly one handler.
     *
     * Assume that we trust additions and that other drivers will
     * handle its respective events without trying to fetch all events.
     *
     * Anyway design assures that wrong event processing will affect only guest.
     *
     * Event handler calls VMMDev IOCTL for waiting an event.
     * It supplies event mask. IOCTL blocks on EventNotification.
     * Here we just signal an the EventNotification to all waiting
     * threads, the IOCTL handler analyzes events and either
     * return to caller or blocks again.
     *
     * If we do not have too many events this is a simple and good
     * approach. Other way is to have as many Event objects as the callers
     * and wake up only callers waiting for the specific event.
     *
     * Now with the 'wake up all' appoach we probably do not need the DPC
     * handler and can signal event directly from ISR.
     *
     */

    PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)pDevObj->DeviceExtension;

    dprintf(("VBoxGuest::VBoxGuestDpcHandler\n"));

    KePulseEvent(&pDevExt->keventNotification, 0, FALSE);

}

/**
 * ISR handler
 *
 * @return  BOOLEAN        indicates whether the IRQ came from us (TRUE) or not (FALSE)
 * @param   interrupt      Interrupt that was triggered.
 * @param   serviceContext Context specific pointer.
 */
BOOLEAN VBoxGuestIsrHandler(PKINTERRUPT interrupt, PVOID serviceContext)
{
    NTSTATUS rc;
    PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)serviceContext;
    BOOLEAN fIRQTaken = FALSE;

    dprintf(("VBoxGuest::VBoxGuestIsrHandler haveEvents = %d\n",
             pDevExt->pVMMDevMemory->V.V1_04.fHaveEvents));

    /*
     * now we have to find out whether it was our IRQ. Read the event mask
     * from our device to see if there are any pending events
     */
    if (pDevExt->pVMMDevMemory->V.V1_04.fHaveEvents)
    {
        /* Acknowlegde events. */
        VMMDevEvents *req = pDevExt->irqAckEvents;

        rc = VbglGRPerform (&req->header);
        if (RT_SUCCESS(rc) && RT_SUCCESS(req->header.rc))
        {
            dprintf(("VBoxGuest::VBoxGuestIsrHandler: acknowledge events succeeded %#x\n",
                     req->events));

            ASMAtomicOrU32((uint32_t *)&pDevExt->u32Events, req->events);
            IoRequestDpc(pDevExt->deviceObject, pDevExt->currentIrp, NULL);
        }
        else
        {
            /* This can't be actually. This is sign of a serious problem. */
            dprintf(("VBoxGuest::VBoxGuestIsrHandler: "
                     "acknowledge events failed rc = %d, header rc = %d\n",
                     rc, req->header.rc));
        }

        /* Mark IRQ as taken, there were events for us. */
        fIRQTaken = TRUE;
    }

    return fIRQTaken;
}

/**
 * Worker thread to do periodic things such as notify other
 * drivers of events.
 *
 * @param   pDevExt device extension pointer
 */
VOID vboxWorkerThread(PVOID context)
{
    PVBOXGUESTDEVEXT pDevExt;

    pDevExt = (PVBOXGUESTDEVEXT)context;
    dprintf(("VBoxGuest::vboxWorkerThread entered\n"));

    /* perform the hypervisor address space reservation */
    reserveHypervisorMemory(pDevExt);

    do
    {
        /* Nothing to do here yet. */

        /*
         * Go asleep unless we're supposed to terminate
         */
        if (!pDevExt->stopThread)
        {
            ULONG secWait = 60;
            dprintf(("VBoxGuest::vboxWorkerThread: waiting for %u seconds...\n", secWait));
            LARGE_INTEGER dueTime;
            dueTime.QuadPart = -10000 * 1000 * (int)secWait;
            if (KeWaitForSingleObject(&pDevExt->workerThreadRequest, Executive,
                                  KernelMode, FALSE, &dueTime) == STATUS_SUCCESS)
            {
                KeResetEvent(&pDevExt->workerThreadRequest);
            }
        }
    } while (!pDevExt->stopThread);

    dprintf(("VBoxGuest::vboxWorkerThread: we've been asked to terminate!\n"));

    if (pDevExt->workerThread)
    {
        ObDereferenceObject(pDevExt->workerThread);
        pDevExt->workerThread = NULL;
    }
    dprintf(("VBoxGuest::vboxWorkerThread: now really gone!\n"));
}

/**
 * Create driver worker threads
 *
 * @returns NTSTATUS NT status code
 * @param pDevExt    VBoxGuest device extension
 */
NTSTATUS createThreads(PVBOXGUESTDEVEXT pDevExt)
{
    NTSTATUS rc;
    HANDLE threadHandle;
    OBJECT_ATTRIBUTES objAttributes;

    dprintf(("VBoxGuest::createThreads\n"));

    // first setup the request semaphore
    KeInitializeEvent(&pDevExt->workerThreadRequest, SynchronizationEvent, FALSE);

// the API has slightly changed after NT4
#ifdef TARGET_NT4
#ifdef OBJ_KERNEL_HANDLE
#undef OBJ_KERNEL_HANDLE
#endif
#define OBJ_KERNEL_HANDLE 0
#endif

    /*
     * The worker thread
     */
    InitializeObjectAttributes(&objAttributes,
                               NULL,
                               OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    rc = PsCreateSystemThread(&threadHandle,
                              THREAD_ALL_ACCESS,
                              &objAttributes,
                              (HANDLE)0L,
                              NULL,
                              vboxWorkerThread,
                              pDevExt);
    dprintf(("VBoxGuest::createThreads: PsCreateSystemThread for worker thread returned: 0x%x\n", rc));
    rc = ObReferenceObjectByHandle(threadHandle,
                                   THREAD_ALL_ACCESS,
                                   NULL,
                                   KernelMode,
                                   (PVOID*)&pDevExt->workerThread,
                                   NULL);
    ZwClose(threadHandle);

    /*
     * The idle thread
     */
#if 0 /// @todo Windows "sees" that time is lost and reports 100% usage
    rc = PsCreateSystemThread(&threadHandle,
                              THREAD_ALL_ACCESS,
                              &objAttributes,
                              (HANDLE)0L,
                              NULL,
                              vboxIdleThread,
                              pDevExt);
    dprintf(("VBoxGuest::createThreads: PsCreateSystemThread for idle thread returned: 0x%x\n", rc));
    rc = ObReferenceObjectByHandle(threadHandle,
                                   THREAD_ALL_ACCESS,
                                   NULL,
                                   KernelMode,
                                   (PVOID*)&pDevExt->idleThread,
                                   NULL);
    ZwClose(threadHandle);
#endif

    return rc;
}

/**
 * Helper routine to reserve address space for the hypervisor
 * and communicate its position.
 *
 * @param pDevExt     Device extension structure.
 */
VOID reserveHypervisorMemory(PVBOXGUESTDEVEXT pDevExt)
{
    // @todo rc handling
    uint32_t hypervisorSize;

    VMMDevReqHypervisorInfo *req = NULL;

    int rc = VbglGRAlloc ((VMMDevRequestHeader **)&req, sizeof (VMMDevReqHypervisorInfo), VMMDevReq_GetHypervisorInfo);

    if (RT_SUCCESS(rc))
    {
        req->hypervisorStart = 0;
        req->hypervisorSize = 0;

        rc = VbglGRPerform (&req->header);

        if (RT_SUCCESS(rc) && RT_SUCCESS(req->header.rc))
        {
            hypervisorSize = req->hypervisorSize;

            if (!hypervisorSize)
            {
                dprintf(("VBoxGuest::reserveHypervisorMemory: host returned 0, not doing anything\n"));
                return;
            }

            dprintf(("VBoxGuest::reserveHypervisorMemory: host wants %u bytes of hypervisor address space\n", hypervisorSize));

            // Map fictive physical memory into the kernel address space to reserve virtual
            // address space. This API does not perform any checks but just allocate the
            // PTEs (which we don't really need/want but there isn't any other clean method).
            // The hypervisor only likes 4MB aligned virtual addresses, so we have to allocate
            // 4MB more than we are actually supposed to in order to guarantee that. Maybe we
            // can come up with a less lavish algorithm lateron.
            PHYSICAL_ADDRESS physAddr;
            physAddr.QuadPart = VBOXGUEST_HYPERVISOR_PHYSICAL_START;
            pDevExt->hypervisorMappingSize = hypervisorSize + 0x400000;
            pDevExt->hypervisorMapping     = MmMapIoSpace(physAddr,
                                                          pDevExt->hypervisorMappingSize,
                                                          MmNonCached);
            if (!pDevExt->hypervisorMapping)
            {
                dprintf(("VBoxGuest::reserveHypervisorMemory: MmMapIoSpace returned NULL!\n"));
                return;
            }

            dprintf(("VBoxGuest::reserveHypervisorMemory: MmMapIoSpace returned %p\n", pDevExt->hypervisorMapping));
            dprintf(("VBoxGuest::reserveHypervisorMemory: communicating %p to host\n",
                    RT_ALIGN_P(pDevExt->hypervisorMapping, 0x400000)));

            /* align at 4MB */
            req->hypervisorStart = (uintptr_t)RT_ALIGN_P(pDevExt->hypervisorMapping, 0x400000);

            req->header.requestType = VMMDevReq_SetHypervisorInfo;
            req->header.rc          = VERR_GENERAL_FAILURE;

            /* issue request */
            rc = VbglGRPerform (&req->header);

            if (RT_FAILURE(rc) || RT_FAILURE(req->header.rc))
            {
                dprintf(("VBoxGuest::reserveHypervisorMemory: error communicating physical address to VMMDev!"
                         "rc = %d, VMMDev rc = %Rrc\n", rc, req->header.rc));
            }
        }
        else
        {
            dprintf(("VBoxGuest::reserveHypervisorMemory: request failed with rc %d, VMMDev rc = %Rrc\n", rc, req->header.rc));
        }
        VbglGRFree (&req->header);
    }

#ifdef RT_ARCH_X86
    /* Allocate locked executable memory that can be used for patching guest code. */
    {
        VMMDevReqPatchMemory *req = NULL;
        int rc = VbglGRAlloc ((VMMDevRequestHeader **)&req, sizeof (VMMDevReqPatchMemory), VMMDevReq_RegisterPatchMemory);
        if (RT_SUCCESS(rc))
        {
            req->cbPatchMem = VMMDEV_GUEST_DEFAULT_PATCHMEM_SIZE;

            rc = RTR0MemObjAllocPage(&pDevExt->PatchMemObj, req->cbPatchMem, true /* executable. */);
            if (RT_SUCCESS(rc))
            {
                req->pPatchMem = (RTGCPTR)(uintptr_t)RTR0MemObjAddress(pDevExt->PatchMemObj);

                rc = VbglGRPerform (&req->header);
                if (RT_FAILURE(rc) || RT_FAILURE(req->header.rc))
                {
                    dprintf(("VBoxGuest::reserveHypervisorMemory: VMMDevReq_RegisterPatchMemory error!"
                                "rc = %d, VMMDev rc = %Rrc\n", rc, req->header.rc));
                    RTR0MemObjFree(pDevExt->PatchMemObj, true);
                    pDevExt->PatchMemObj = NULL;
                }
            }
            else
            {
                dprintf(("VBoxGuest::reserveHypervisorMemory: RTR0MemObjAllocPage failed with rc %d\n", rc));
            }
            VbglGRFree (&req->header);
        }
    }
#endif
    return;
}

/**
 * Helper function to unregister a virtual address space mapping
 *
 * @param pDevExt     Device extension
 */
VOID unreserveHypervisorMemory(PVBOXGUESTDEVEXT pDevExt)
{
#ifdef RT_ARCH_X86
    /* Remove the locked executable memory range that can be used for patching guest code. */
    if (pDevExt->PatchMemObj)
    {
        VMMDevReqPatchMemory *req = NULL;
        int rc = VbglGRAlloc ((VMMDevRequestHeader **)&req, sizeof (VMMDevReqPatchMemory), VMMDevReq_DeregisterPatchMemory);
        if (RT_SUCCESS(rc))
        {
            req->cbPatchMem = (uint32_t)RTR0MemObjSize(pDevExt->PatchMemObj);
            req->pPatchMem  = (RTGCPTR)(uintptr_t)RTR0MemObjAddress(pDevExt->PatchMemObj);

            rc = VbglGRPerform (&req->header);
            if (RT_FAILURE(rc) || RT_FAILURE(req->header.rc))
            {
                dprintf(("VBoxGuest::reserveHypervisorMemory: VMMDevReq_DeregisterPatchMemory error!"
                            "rc = %d, VMMDev rc = %Rrc\n", rc, req->header.rc));
                /* We intentially leak the memory object here as there still could
                 * be references to it!!!
                 */
            }
            else
            {
                RTR0MemObjFree(pDevExt->PatchMemObj, true);
            }
        }
    }
#endif

    VMMDevReqHypervisorInfo *req = NULL;

    int rc = VbglGRAlloc ((VMMDevRequestHeader **)&req, sizeof (VMMDevReqHypervisorInfo), VMMDevReq_SetHypervisorInfo);

    if (RT_SUCCESS(rc))
    {
        /* tell the hypervisor that the mapping is no longer available */

        req->hypervisorStart = 0;
        req->hypervisorSize = 0;

        rc = VbglGRPerform (&req->header);

        if (RT_FAILURE(rc) || RT_FAILURE(req->header.rc))
        {
            dprintf(("VBoxGuest::unreserveHypervisorMemory: error communicating physical address to VMMDev!"
                     "rc = %d, VMMDev rc = %Rrc\n", rc, req->header.rc));
        }

        VbglGRFree (&req->header);
    }

    if (!pDevExt->hypervisorMapping)
    {
        dprintf(("VBoxGuest::unreserveHypervisorMemory: there is no mapping, returning\n"));
        return;
    }

    // unmap fictive IO space
    MmUnmapIoSpace(pDevExt->hypervisorMapping, pDevExt->hypervisorMappingSize);
    dprintf(("VBoxGuest::unreserveHypervisorMemmory: done\n"));
}

/**
 * Idle thread that runs at the lowest priority possible
 * and whenever scheduled, makes a VMMDev call to give up
 * timeslices. This is so prevent Windows from thinking that
 * nothing is happening on the machine and doing stupid things
 * that would steal time from other VMs it doesn't know of.
 *
 * @param   pDevExt device extension pointer
 */
VOID vboxIdleThread(PVOID context)
{
    PVBOXGUESTDEVEXT pDevExt;

    pDevExt = (PVBOXGUESTDEVEXT)context;
    dprintf(("VBoxGuest::vboxIdleThread entered\n"));

    /* set priority as low as possible */
    KeSetPriorityThread(KeGetCurrentThread(), LOW_PRIORITY);

    /* allocate VMMDev request structure */
    VMMDevReqIdle *req;
    int rc = VbglGRAlloc((VMMDevRequestHeader **)&req, sizeof (VMMDevReqHypervisorInfo), VMMDevReq_Idle);
    if (RT_FAILURE(rc))
    {
        dprintf(("VBoxGuest::vboxIdleThread: error %Rrc allocating request structure!\n"));
        return;
    }

    do
    {
        //dprintf(("VBoxGuest: performing idle request..\n"));
        /* perform idle request */
        VbglGRPerform(&req->header);

    } while (!pDevExt->stopThread);

    VbglGRFree(&req->header);

    dprintf(("VBoxGuest::vboxIdleThread leaving\n"));
}

#ifdef DEBUG
static VOID testAtomicTestAndClearBitsU32(uint32_t u32Mask, uint32_t u32Bits,
                                          uint32_t u32Exp)
{
    ULONG u32Bits2 = u32Bits;
    uint32_t u32Result = guestAtomicBitsTestAndClear(&u32Bits2, u32Mask);
    if (   u32Result != u32Exp
        || (u32Bits2 & u32Mask)
        || (u32Bits2 & u32Result)
        || ((u32Bits2 | u32Result) != u32Bits)
       )
        AssertLogRelMsgFailed(("%s: TEST FAILED: u32Mask=0x%x, u32Bits (before)=0x%x, u32Bits (after)=0x%x, u32Result=0x%x, u32Exp=ox%x\n",
                               __PRETTY_FUNCTION__, u32Mask, u32Bits, u32Bits2,
                               u32Result));
}

static VOID testVBoxGuest(VOID)
{
    testAtomicTestAndClearBitsU32(0x00, 0x23, 0);
    testAtomicTestAndClearBitsU32(0x11, 0, 0);
    testAtomicTestAndClearBitsU32(0x11, 0x22, 0);
    testAtomicTestAndClearBitsU32(0x11, 0x23, 0x1);
    testAtomicTestAndClearBitsU32(0x11, 0x32, 0x10);
    testAtomicTestAndClearBitsU32(0x22, 0x23, 0x22);
}
#endif
