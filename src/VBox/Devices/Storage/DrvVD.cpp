/* $Id$ */
/** @file
 * DrvVD - Generic VBox disk media driver.
 */

/*
 * Copyright (C) 2006-2008 Sun Microsystems, Inc.
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


/*******************************************************************************
*   Header files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_VD
#include <VBox/VBoxHDD.h>
#include <VBox/pdmdrv.h>
#include <VBox/pdmasynccompletion.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include <iprt/cache.h>
#include <iprt/tcp.h>
#include <iprt/semaphore.h>

#ifdef VBOX_WITH_INIP
/* All lwip header files are not C++ safe. So hack around this. */
RT_C_DECLS_BEGIN
#include <lwip/inet.h>
#include <lwip/tcp.h>
#include <lwip/sockets.h>
RT_C_DECLS_END
#endif /* VBOX_WITH_INIP */

#include "Builtins.h"

#ifdef VBOX_WITH_INIP
/* Small hack to get at lwIP initialized status */
extern bool DevINIPConfigured(void);
#endif /* VBOX_WITH_INIP */


/*******************************************************************************
*   Defined types, constants and macros                                        *
*******************************************************************************/

/** Converts a pointer to VDIDISK::IMedia to a PVBOXDISK. */
#define PDMIMEDIA_2_VBOXDISK(pInterface) \
    ( (PVBOXDISK)((uintptr_t)pInterface - RT_OFFSETOF(VBOXDISK, IMedia)) )

/** Converts a pointer to PDMDRVINS::IBase to a PPDMDRVINS. */
#define PDMIBASE_2_DRVINS(pInterface) \
    ( (PPDMDRVINS)((uintptr_t)pInterface - RT_OFFSETOF(PDMDRVINS, IBase)) )

/** Converts a pointer to PDMDRVINS::IBase to a PVBOXDISK. */
#define PDMIBASE_2_VBOXDISK(pInterface) \
    ( PDMINS_2_DATA(PDMIBASE_2_DRVINS(pInterface), PVBOXDISK) )

/** Converts a pointer to VBOXDISK::IMediaAsync to a PVBOXDISK. */
#define PDMIMEDIAASYNC_2_VBOXDISK(pInterface) \
    ( (PVBOXDISK)((uintptr_t)pInterface - RT_OFFSETOF(VBOXDISK, IMediaAsync)) )

/**
 * VBox disk container, image information, private part.
 */

typedef struct VBOXIMAGE
{
    /** Pointer to next image. */
    struct VBOXIMAGE    *pNext;
    /** Pointer to list of VD interfaces. Per-image. */
    PVDINTERFACE       pVDIfsImage;
    /** Common structure for the configuration information interface. */
    VDINTERFACE        VDIConfig;
} VBOXIMAGE, *PVBOXIMAGE;

/**
 * Storage backend data.
 */
typedef struct DRVVDSTORAGEBACKEND
{
    /** PDM async completion end point. */
    PPDMASYNCCOMPLETIONENDPOINT pEndpoint;
    /** The template. */
    PPDMASYNCCOMPLETIONTEMPLATE pTemplate;
    /** Event semaphore for synchronous operations. */
    RTSEMEVENT                  EventSem;
    /** Flag whether a synchronous operation is currently pending. */
    volatile bool               fSyncIoPending;
    /** Callback routine */
    PFNVDCOMPLETED              pfnCompleted;
} DRVVDSTORAGEBACKEND, *PDRVVDSTORAGEBACKEND;

/**
 * VBox disk container media main structure, private part.
 */
typedef struct VBOXDISK
{
    /** The VBox disk container. */
    PVBOXHDD           pDisk;
    /** The media interface. */
    PDMIMEDIA          IMedia;
    /** Pointer to the driver instance. */
    PPDMDRVINS         pDrvIns;
    /** Flag whether suspend has changed image open mode to read only. */
    bool               fTempReadOnly;
    /** Flag whether to use the runtime (true) or startup error facility. */
    bool               fErrorUseRuntime;
    /** Pointer to list of VD interfaces. Per-disk. */
    PVDINTERFACE       pVDIfsDisk;
    /** Common structure for the supported error interface. */
    VDINTERFACE        VDIError;
    /** Callback table for error interface. */
    VDINTERFACEERROR   VDIErrorCallbacks;
    /** Common structure for the supported TCP network stack interface. */
    VDINTERFACE        VDITcpNet;
    /** Callback table for TCP network stack interface. */
    VDINTERFACETCPNET  VDITcpNetCallbacks;
    /** Common structure for the supported async I/O interface. */
    VDINTERFACE        VDIAsyncIO;
    /** Callback table for async I/O interface. */
    VDINTERFACEASYNCIO VDIAsyncIOCallbacks;
    /** Callback table for the configuration information interface. */
    VDINTERFACECONFIG  VDIConfigCallbacks;
    /** Flag whether opened disk suppports async I/O operations. */
    bool               fAsyncIOSupported;
    /** The async media interface. */
    PDMIMEDIAASYNC           IMediaAsync;
    /** The async media port interface above. */
    PPDMIMEDIAASYNCPORT      pDrvMediaAsyncPort;
    /** Pointer to the list of data we need to keep per image. */
    PVBOXIMAGE               pImages;
} VBOXDISK, *PVBOXDISK;

/*******************************************************************************
*   Error reporting callback                                                   *
*******************************************************************************/

static void drvvdErrorCallback(void *pvUser, int rc, RT_SRC_POS_DECL,
                               const char *pszFormat, va_list va)
{
    PPDMDRVINS pDrvIns = (PPDMDRVINS)pvUser;
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);
    if (pThis->fErrorUseRuntime)
        /* We must not pass VMSETRTERR_FLAGS_FATAL as it could lead to a
         * deadlock: We are probably executed in a thread context != EMT
         * and the EM thread would wait until every thread is suspended
         * but we would wait for the EM thread ... */
        pDrvIns->pDrvHlp->pfnVMSetRuntimeErrorV(pDrvIns, /* fFlags=*/ 0, "DrvVD", pszFormat, va);
    else
        pDrvIns->pDrvHlp->pfnVMSetErrorV(pDrvIns, rc, RT_SRC_POS_ARGS, pszFormat, va);
}


/**
 * Internal: allocate new image descriptor and put it in the list
 */
static PVBOXIMAGE drvvdNewImage(PVBOXDISK pThis)
{
    AssertPtr(pThis);
    PVBOXIMAGE pImage = (PVBOXIMAGE)RTMemAllocZ(sizeof(VBOXIMAGE));
    if (pImage)
    {
        pImage->pVDIfsImage = NULL;
        PVBOXIMAGE *pp = &pThis->pImages;
        while (*pp != NULL)
            pp = &(*pp)->pNext;
        *pp = pImage;
        pImage->pNext = NULL;
    }

    return pImage;
}

/**
 * Internal: free the list of images descriptors.
 */
static void drvvdFreeImages(PVBOXDISK pThis)
{
    while (pThis->pImages != NULL)
    {
        PVBOXIMAGE p = pThis->pImages;
        pThis->pImages = pThis->pImages->pNext;
        RTMemFree(p);
    }
}

/*******************************************************************************
*   VD Async I/O interface implementation                                      *
*******************************************************************************/

#ifdef VBOX_WITH_PDM_ASYNC_COMPLETION

static DECLCALLBACK(void) drvvdAsyncTaskCompleted(PPDMDRVINS pDrvIns, void *pvTemplateUser, void *pvUser)
{
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pvTemplateUser;

    if (pStorageBackend->fSyncIoPending)
    {
        pStorageBackend->fSyncIoPending = false;
        RTSemEventSignal(pStorageBackend->EventSem);
    }
    else
    {
        int rc = VINF_VD_ASYNC_IO_FINISHED;
        void *pvCallerUser = NULL;

        if (pStorageBackend->pfnCompleted)
            rc = pStorageBackend->pfnCompleted(pvUser, &pvCallerUser);
        else
            pvCallerUser = pvUser;

        if (rc == VINF_VD_ASYNC_IO_FINISHED)
        {
            rc = pThis->pDrvMediaAsyncPort->pfnTransferCompleteNotify(pThis->pDrvMediaAsyncPort, pvCallerUser);
            AssertRC(rc);
        }
        else
            AssertMsg(rc == VERR_VD_ASYNC_IO_IN_PROGRESS, ("Invalid return code from disk backend rc=%Rrc\n", rc));
    }
}

static DECLCALLBACK(int) drvvdAsyncIOOpen(void *pvUser, const char *pszLocation, unsigned uOpenFlags,
                                          PFNVDCOMPLETED pfnCompleted, void **ppStorage)
{
    PVBOXDISK pDrvVD = (PVBOXDISK)pvUser;
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)RTMemAllocZ(sizeof(DRVVDSTORAGEBACKEND));
    int rc = VINF_SUCCESS;

    if (pStorageBackend)
    {
        pStorageBackend->fSyncIoPending = false;
        pStorageBackend->pfnCompleted   = pfnCompleted;

        rc = RTSemEventCreate(&pStorageBackend->EventSem);
        if (RT_SUCCESS(rc))
        {
            rc = PDMDrvHlpPDMAsyncCompletionTemplateCreate(pDrvVD->pDrvIns, &pStorageBackend->pTemplate,
                                                           drvvdAsyncTaskCompleted, pStorageBackend, "AsyncTaskCompleted");
            if (RT_SUCCESS(rc))
            {
                rc = PDMR3AsyncCompletionEpCreateForFile(&pStorageBackend->pEndpoint, pszLocation,
                                                         uOpenFlags & VD_INTERFACEASYNCIO_OPEN_FLAGS_READONLY
                                                         ? PDMACEP_FILE_FLAGS_READ_ONLY | PDMACEP_FILE_FLAGS_CACHING
                                                         : PDMACEP_FILE_FLAGS_CACHING,
                                                         pStorageBackend->pTemplate);
                if (RT_SUCCESS(rc))
                {
                    *ppStorage = pStorageBackend;
                    return VINF_SUCCESS;
                }

                PDMR3AsyncCompletionTemplateDestroy(pStorageBackend->pTemplate);
            }
            RTSemEventDestroy(pStorageBackend->EventSem);
        }
        RTMemFree(pStorageBackend);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

static DECLCALLBACK(int) drvvdAsyncIOClose(void *pvUser, void *pStorage)
{
    PVBOXDISK pDrvVD = (PVBOXDISK)pvUser;
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;

    PDMR3AsyncCompletionEpClose(pStorageBackend->pEndpoint);
    PDMR3AsyncCompletionTemplateDestroy(pStorageBackend->pTemplate);
    RTSemEventDestroy(pStorageBackend->EventSem);
    RTMemFree(pStorageBackend);

    return VINF_SUCCESS;;
}

static DECLCALLBACK(int) drvvdAsyncIOGetSize(void *pvUser, void *pStorage, uint64_t *pcbSize)
{
    PVBOXDISK pDrvVD = (PVBOXDISK)pvUser;
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;

    return PDMR3AsyncCompletionEpGetSize(pStorageBackend->pEndpoint, pcbSize);
}

static DECLCALLBACK(int) drvvdAsyncIOSetSize(void *pvUser, void *pStorage, uint64_t cbSize)
{
    PVBOXDISK pDrvVD = (PVBOXDISK)pvUser;
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;

    return VERR_NOT_SUPPORTED;
}

static DECLCALLBACK(int) drvvdAsyncIOReadSync(void *pvUser, void *pStorage, uint64_t uOffset,
                                              size_t cbRead, void *pvBuf, size_t *pcbRead)
{
    PVBOXDISK pDrvVD = (PVBOXDISK)pvUser;
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;
    PDMDATASEG DataSeg;
    PPDMASYNCCOMPLETIONTASK pTask;

    Assert(!pStorageBackend->fSyncIoPending);
    pStorageBackend->fSyncIoPending = true;
    DataSeg.cbSeg = cbRead;
    DataSeg.pvSeg = pvBuf;

    int rc = PDMR3AsyncCompletionEpRead(pStorageBackend->pEndpoint, uOffset, &DataSeg, 1, cbRead, NULL, &pTask);
    if (RT_FAILURE(rc))
        return rc;

    /* Wait */
    rc = RTSemEventWait(pStorageBackend->EventSem, RT_INDEFINITE_WAIT);
    AssertRC(rc);

    if (pcbRead)
        *pcbRead = cbRead;

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) drvvdAsyncIOWriteSync(void *pvUser, void *pStorage, uint64_t uOffset,
                                               size_t cbWrite, const void *pvBuf, size_t *pcbWritten)
{
    PVBOXDISK pDrvVD = (PVBOXDISK)pvUser;
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;
    PDMDATASEG DataSeg;
    PPDMASYNCCOMPLETIONTASK pTask;

    Assert(!pStorageBackend->fSyncIoPending);
    pStorageBackend->fSyncIoPending = true;
    DataSeg.cbSeg = cbWrite;
    DataSeg.pvSeg = (void *)pvBuf;

    int rc = PDMR3AsyncCompletionEpWrite(pStorageBackend->pEndpoint, uOffset, &DataSeg, 1, cbWrite, NULL, &pTask);
    if (RT_FAILURE(rc))
        return rc;

    /* Wait */
    rc = RTSemEventWait(pStorageBackend->EventSem, RT_INDEFINITE_WAIT);
    AssertRC(rc);

    if (pcbWritten)
        *pcbWritten = cbWrite;

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) drvvdAsyncIOFlushSync(void *pvUser, void *pStorage)
{
    PVBOXDISK pDrvVD = (PVBOXDISK)pvUser;
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;
    PPDMASYNCCOMPLETIONTASK pTask;

    Assert(!pStorageBackend->fSyncIoPending);
    pStorageBackend->fSyncIoPending = true;

    int rc = PDMR3AsyncCompletionEpFlush(pStorageBackend->pEndpoint, NULL, &pTask);
    if (RT_FAILURE(rc))
        return rc;

    /* Wait */
    rc = RTSemEventWait(pStorageBackend->EventSem, RT_INDEFINITE_WAIT);
    AssertRC(rc);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) drvvdAsyncIOReadAsync(void *pvUser, void *pStorage, uint64_t uOffset,
                                               PCPDMDATASEG paSegments, size_t cSegments,
                                               size_t cbRead, void *pvCompletion,
                                               void **ppTask)
{
    PVBOXDISK pDrvVD = (PVBOXDISK)pvUser;
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;

    return PDMR3AsyncCompletionEpRead(pStorageBackend->pEndpoint, uOffset, paSegments, cSegments, cbRead,
                                      pvCompletion, (PPPDMASYNCCOMPLETIONTASK)ppTask);
}

static DECLCALLBACK(int) drvvdAsyncIOWriteAsync(void *pvUser, void *pStorage, uint64_t uOffset,
                                                PCPDMDATASEG paSegments, size_t cSegments,
                                                size_t cbWrite, void *pvCompletion,
                                                void **ppTask)
{
    PVBOXDISK pDrvVD = (PVBOXDISK)pvUser;
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;

    return PDMR3AsyncCompletionEpWrite(pStorageBackend->pEndpoint, uOffset, paSegments, cSegments, cbWrite,
                                       pvCompletion, (PPPDMASYNCCOMPLETIONTASK)ppTask);
}

static DECLCALLBACK(int) drvvdAsyncIOFlushAsync(void *pvUser, void *pStorage,
                                                void *pvCompletion, void **ppTask)
{
    PVBOXDISK pDrvVD = (PVBOXDISK)pvUser;
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;

    return PDMR3AsyncCompletionEpFlush(pStorageBackend->pEndpoint, pvCompletion,
                                       (PPPDMASYNCCOMPLETIONTASK)ppTask);
}

#endif /* VBOX_WITH_PDM_ASYNC_COMPLETION */


/*******************************************************************************
*   VD Configuration interface implementation                                  *
*******************************************************************************/

static bool drvvdCfgAreKeysValid(void *pvUser, const char *pszzValid)
{
    return CFGMR3AreValuesValid((PCFGMNODE)pvUser, pszzValid);
}

static int drvvdCfgQuerySize(void *pvUser, const char *pszName, size_t *pcb)
{
    return CFGMR3QuerySize((PCFGMNODE)pvUser, pszName, pcb);
}

static int drvvdCfgQuery(void *pvUser, const char *pszName, char *pszString, size_t cchString)
{
    return CFGMR3QueryString((PCFGMNODE)pvUser, pszName, pszString, cchString);
}


#ifdef VBOX_WITH_INIP
/*******************************************************************************
*   VD TCP network stack interface implementation - INIP case                  *
*******************************************************************************/

/** @copydoc VDINTERFACETCPNET::pfnClientConnect */
static DECLCALLBACK(int) drvvdINIPClientConnect(const char *pszAddress, uint32_t uPort, PRTSOCKET pSock)
{
    int rc = VINF_SUCCESS;
    /* First check whether lwIP is set up in this VM instance. */
    if (!DevINIPConfigured())
    {
        LogRelFunc(("no IP stack\n"));
        return VERR_NET_HOST_UNREACHABLE;
    }
    /* Resolve hostname. As there is no standard resolver for lwIP yet,
     * just accept numeric IP addresses for now. */
    struct in_addr ip;
    if (!lwip_inet_aton(pszAddress, &ip))
    {
        LogRelFunc(("cannot resolve IP %s\n", pszAddress));
        return VERR_NET_HOST_UNREACHABLE;
    }
    /* Create socket and connect. */
    RTSOCKET Sock = lwip_socket(PF_INET, SOCK_STREAM, 0);
    if (Sock != -1)
    {
        struct sockaddr_in InAddr = {0};
        InAddr.sin_family = AF_INET;
        InAddr.sin_port = htons(uPort);
        InAddr.sin_addr = ip;
        if (!lwip_connect(Sock, (struct sockaddr *)&InAddr, sizeof(InAddr)))
        {
            *pSock = Sock;
            return VINF_SUCCESS;
        }
        rc = VERR_NET_CONNECTION_REFUSED; /* @todo real solution needed */
        lwip_close(Sock);
    }
    else
        rc = VERR_NET_CONNECTION_REFUSED; /* @todo real solution needed */
    return rc;
}

/** @copydoc VDINTERFACETCPNET::pfnClientClose */
static DECLCALLBACK(int) drvvdINIPClientClose(RTSOCKET Sock)
{
    lwip_close(Sock);
    return VINF_SUCCESS; /** @todo real solution needed */
}

/** @copydoc VDINTERFACETCPNET::pfnSelectOne */
static DECLCALLBACK(int) drvvdINIPSelectOne(RTSOCKET Sock, unsigned cMillies)
{
    fd_set fdsetR;
    FD_ZERO(&fdsetR);
    FD_SET(Sock, &fdsetR);
    fd_set fdsetE = fdsetR;

    int rc;
    if (cMillies == RT_INDEFINITE_WAIT)
        rc = lwip_select(Sock + 1, &fdsetR, NULL, &fdsetE, NULL);
    else
    {
        struct timeval timeout;
        timeout.tv_sec = cMillies / 1000;
        timeout.tv_usec = (cMillies % 1000) * 1000;
        rc = lwip_select(Sock + 1, &fdsetR, NULL, &fdsetE, &timeout);
    }
    if (rc > 0)
        return VINF_SUCCESS;
    if (rc == 0)
        return VERR_TIMEOUT;
    return VERR_NET_CONNECTION_REFUSED; /** @todo real solution needed */
}

/** @copydoc VDINTERFACETCPNET::pfnRead */
static DECLCALLBACK(int) drvvdINIPRead(RTSOCKET Sock, void *pvBuffer, size_t cbBuffer, size_t *pcbRead)
{
    /* Do params checking */
    if (!pvBuffer || !cbBuffer)
    {
        AssertMsgFailed(("Invalid params\n"));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Read loop.
     * If pcbRead is NULL we have to fill the entire buffer!
     */
    size_t cbRead = 0;
    size_t cbToRead = cbBuffer;
    for (;;)
    {
        /** @todo this clipping here is just in case (the send function
         * needed it, so I added it here, too). Didn't investigate if this
         * really has issues. Better be safe than sorry. */
        ssize_t cbBytesRead = lwip_recv(Sock, (char *)pvBuffer + cbRead,
                                        RT_MIN(cbToRead, 32768), 0);
        if (cbBytesRead < 0)
            return VERR_NET_CONNECTION_REFUSED; /** @todo real solution */
        if (cbBytesRead == 0 && errno)
            return VERR_NET_CONNECTION_REFUSED; /** @todo real solution */
        if (pcbRead)
        {
            /* return partial data */
            *pcbRead = cbBytesRead;
            break;
        }

        /* read more? */
        cbRead += cbBytesRead;
        if (cbRead == cbBuffer)
            break;

        /* next */
        cbToRead = cbBuffer - cbRead;
    }

    return VINF_SUCCESS;
}

/** @copydoc VDINTERFACETCPNET::pfnWrite */
static DECLCALLBACK(int) drvvdINIPWrite(RTSOCKET Sock, const void *pvBuffer, size_t cbBuffer)
{
    do
    {
        /** @todo lwip send only supports up to 65535 bytes in a single
         * send (stupid limitation buried in the code), so make sure we
         * don't get any wraparounds. This should be moved to DevINIP
         * stack interface once that's implemented. */
        ssize_t cbWritten = lwip_send(Sock, (void *)pvBuffer,
                                      RT_MIN(cbBuffer, 32768), 0);
        if (cbWritten < 0)
            return VERR_NET_CONNECTION_REFUSED; /** @todo real solution needed */
        AssertMsg(cbBuffer >= (size_t)cbWritten, ("Wrote more than we requested!!! cbWritten=%d cbBuffer=%d\n",
                                                  cbWritten, cbBuffer));
        cbBuffer -= cbWritten;
        pvBuffer = (const char *)pvBuffer + cbWritten;
    } while (cbBuffer);

    return VINF_SUCCESS;
}

/** @copydoc VDINTERFACETCPNET::pfnFlush */
static DECLCALLBACK(int) drvvdINIPFlush(RTSOCKET Sock)
{
    int fFlag = 1;
    lwip_setsockopt(Sock, IPPROTO_TCP, TCP_NODELAY,
                    (const char *)&fFlag, sizeof(fFlag));
    fFlag = 0;
    lwip_setsockopt(Sock, IPPROTO_TCP, TCP_NODELAY,
                    (const char *)&fFlag, sizeof(fFlag));
    return VINF_SUCCESS;
}
#endif /* VBOX_WITH_INIP */


/*******************************************************************************
*   Media interface methods                                                    *
*******************************************************************************/

/** @copydoc PDMIMEDIA::pfnRead */
static DECLCALLBACK(int) drvvdRead(PPDMIMEDIA pInterface,
                                   uint64_t off, void *pvBuf, size_t cbRead)
{
    LogFlow(("%s: off=%#llx pvBuf=%p cbRead=%d\n", __FUNCTION__,
             off, pvBuf, cbRead));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    int rc = VDRead(pThis->pDisk, off, pvBuf, cbRead);
    if (RT_SUCCESS(rc))
        Log2(("%s: off=%#llx pvBuf=%p cbRead=%d %.*Rhxd\n", __FUNCTION__,
              off, pvBuf, cbRead, cbRead, pvBuf));
    LogFlow(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}

/** @copydoc PDMIMEDIA::pfnWrite */
static DECLCALLBACK(int) drvvdWrite(PPDMIMEDIA pInterface,
                                    uint64_t off, const void *pvBuf,
                                    size_t cbWrite)
{
    LogFlow(("%s: off=%#llx pvBuf=%p cbWrite=%d\n", __FUNCTION__,
             off, pvBuf, cbWrite));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    Log2(("%s: off=%#llx pvBuf=%p cbWrite=%d %.*Rhxd\n", __FUNCTION__,
          off, pvBuf, cbWrite, cbWrite, pvBuf));
    int rc = VDWrite(pThis->pDisk, off, pvBuf, cbWrite);
    LogFlow(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}

/** @copydoc PDMIMEDIA::pfnFlush */
static DECLCALLBACK(int) drvvdFlush(PPDMIMEDIA pInterface)
{
    LogFlow(("%s:\n", __FUNCTION__));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    int rc = VDFlush(pThis->pDisk);
    LogFlow(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}

/** @copydoc PDMIMEDIA::pfnGetSize */
static DECLCALLBACK(uint64_t) drvvdGetSize(PPDMIMEDIA pInterface)
{
    LogFlow(("%s:\n", __FUNCTION__));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    uint64_t cb = VDGetSize(pThis->pDisk, VD_LAST_IMAGE);
    LogFlow(("%s: returns %#llx (%llu)\n", __FUNCTION__, cb, cb));
    return cb;
}

/** @copydoc PDMIMEDIA::pfnIsReadOnly */
static DECLCALLBACK(bool) drvvdIsReadOnly(PPDMIMEDIA pInterface)
{
    LogFlow(("%s:\n", __FUNCTION__));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    bool f = VDIsReadOnly(pThis->pDisk);
    LogFlow(("%s: returns %d\n", __FUNCTION__, f));
    return f;
}

/** @copydoc PDMIMEDIA::pfnBiosGetPCHSGeometry */
static DECLCALLBACK(int) drvvdBiosGetPCHSGeometry(PPDMIMEDIA pInterface,
                                                  PPDMMEDIAGEOMETRY pPCHSGeometry)
{
    LogFlow(("%s:\n", __FUNCTION__));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    int rc = VDGetPCHSGeometry(pThis->pDisk, VD_LAST_IMAGE, pPCHSGeometry);
    if (RT_FAILURE(rc))
    {
        Log(("%s: geometry not available.\n", __FUNCTION__));
        rc = VERR_PDM_GEOMETRY_NOT_SET;
    }
    LogFlow(("%s: returns %Rrc (CHS=%d/%d/%d)\n", __FUNCTION__,
             rc, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    return rc;
}

/** @copydoc PDMIMEDIA::pfnBiosSetPCHSGeometry */
static DECLCALLBACK(int) drvvdBiosSetPCHSGeometry(PPDMIMEDIA pInterface,
                                                  PCPDMMEDIAGEOMETRY pPCHSGeometry)
{
    LogFlow(("%s: CHS=%d/%d/%d\n", __FUNCTION__,
             pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    int rc = VDSetPCHSGeometry(pThis->pDisk, VD_LAST_IMAGE, pPCHSGeometry);
    if (rc == VERR_VD_GEOMETRY_NOT_SET)
        rc = VERR_PDM_GEOMETRY_NOT_SET;
    LogFlow(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}

/** @copydoc PDMIMEDIA::pfnBiosGetLCHSGeometry */
static DECLCALLBACK(int) drvvdBiosGetLCHSGeometry(PPDMIMEDIA pInterface,
                                                  PPDMMEDIAGEOMETRY pLCHSGeometry)
{
    LogFlow(("%s:\n", __FUNCTION__));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    int rc = VDGetLCHSGeometry(pThis->pDisk, VD_LAST_IMAGE, pLCHSGeometry);
    if (RT_FAILURE(rc))
    {
        Log(("%s: geometry not available.\n", __FUNCTION__));
        rc = VERR_PDM_GEOMETRY_NOT_SET;
    }
    LogFlow(("%s: returns %Rrc (CHS=%d/%d/%d)\n", __FUNCTION__,
             rc, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    return rc;
}

/** @copydoc PDMIMEDIA::pfnBiosSetLCHSGeometry */
static DECLCALLBACK(int) drvvdBiosSetLCHSGeometry(PPDMIMEDIA pInterface,
                                                  PCPDMMEDIAGEOMETRY pLCHSGeometry)
{
    LogFlow(("%s: CHS=%d/%d/%d\n", __FUNCTION__,
             pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    int rc = VDSetLCHSGeometry(pThis->pDisk, VD_LAST_IMAGE, pLCHSGeometry);
    if (rc == VERR_VD_GEOMETRY_NOT_SET)
        rc = VERR_PDM_GEOMETRY_NOT_SET;
    LogFlow(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}

/** @copydoc PDMIMEDIA::pfnGetUuid */
static DECLCALLBACK(int) drvvdGetUuid(PPDMIMEDIA pInterface, PRTUUID pUuid)
{
    LogFlow(("%s:\n", __FUNCTION__));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    int rc = VDGetUuid(pThis->pDisk, 0, pUuid);
    LogFlow(("%s: returns %Rrc ({%RTuuid})\n", __FUNCTION__, rc, pUuid));
    return rc;
}

/*******************************************************************************
*   Async Media interface methods                                              *
*******************************************************************************/

static DECLCALLBACK(int) drvvdStartRead(PPDMIMEDIAASYNC pInterface, uint64_t uOffset,
                                        PPDMDATASEG paSeg, unsigned cSeg,
                                        size_t cbRead, void *pvUser)
{
     LogFlow(("%s: uOffset=%#llx paSeg=%#p cSeg=%u cbRead=%d\n pvUser=%#p", __FUNCTION__,
             uOffset, paSeg, cSeg, cbRead, pvUser));
    PVBOXDISK pThis = PDMIMEDIAASYNC_2_VBOXDISK(pInterface);
    int rc = VDAsyncRead(pThis->pDisk, uOffset, cbRead, paSeg, cSeg, pvUser);
    LogFlow(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}

static DECLCALLBACK(int) drvvdStartWrite(PPDMIMEDIAASYNC pInterface, uint64_t uOffset,
                                         PPDMDATASEG paSeg, unsigned cSeg,
                                         size_t cbWrite, void *pvUser)
{
     LogFlow(("%s: uOffset=%#llx paSeg=%#p cSeg=%u cbWrite=%d\n pvUser=%#p", __FUNCTION__,
             uOffset, paSeg, cSeg, cbWrite, pvUser));
    PVBOXDISK pThis = PDMIMEDIAASYNC_2_VBOXDISK(pInterface);
    int rc = VDAsyncWrite(pThis->pDisk, uOffset, cbWrite, paSeg, cSeg, pvUser);
    LogFlow(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}

/*******************************************************************************
*   Async transport port interface methods                                     *
*******************************************************************************/

static DECLCALLBACK(int) drvvdTasksCompleteNotify(PPDMDRVINS pDrvIns, void *pvUser)
{
    return VERR_NOT_IMPLEMENTED;
}


/*******************************************************************************
*   Base interface methods                                                     *
*******************************************************************************/

/** @copydoc PDMIBASE::pfnQueryInterface */
static DECLCALLBACK(void *) drvvdQueryInterface(PPDMIBASE pInterface,
                                                PDMINTERFACE enmInterface)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_DRVINS(pInterface);
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);
    switch (enmInterface)
    {
        case PDMINTERFACE_BASE:
            return &pDrvIns->IBase;
        case PDMINTERFACE_MEDIA:
            return &pThis->IMedia;
        case PDMINTERFACE_MEDIA_ASYNC:
            return pThis->fAsyncIOSupported ? &pThis->IMediaAsync : NULL;
        default:
            return NULL;
    }
}


/*******************************************************************************
*   Driver methods                                                             *
*******************************************************************************/


/**
 * Construct a VBox disk media driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvvdConstruct(PPDMDRVINS pDrvIns,
                                        PCFGMNODE pCfgHandle,
                                        uint32_t fFlags)
{
    LogFlow(("%s:\n", __FUNCTION__));
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);
    int rc = VINF_SUCCESS;
    char *pszName = NULL;   /**< The path of the disk image file. */
    char *pszFormat = NULL; /**< The format backed to use for this image. */
    bool fReadOnly;         /**< True if the media is readonly. */
    bool fHonorZeroWrites;  /**< True if zero blocks should be written. */

    /*
     * Init the static parts.
     */
    pDrvIns->IBase.pfnQueryInterface    = drvvdQueryInterface;
    pThis->pDrvIns                      = pDrvIns;
    pThis->fTempReadOnly                = false;
    pThis->pDisk                        = NULL;
    pThis->fAsyncIOSupported            = false;

    /* IMedia */
    pThis->IMedia.pfnRead               = drvvdRead;
    pThis->IMedia.pfnWrite              = drvvdWrite;
    pThis->IMedia.pfnFlush              = drvvdFlush;
    pThis->IMedia.pfnGetSize            = drvvdGetSize;
    pThis->IMedia.pfnIsReadOnly         = drvvdIsReadOnly;
    pThis->IMedia.pfnBiosGetPCHSGeometry = drvvdBiosGetPCHSGeometry;
    pThis->IMedia.pfnBiosSetPCHSGeometry = drvvdBiosSetPCHSGeometry;
    pThis->IMedia.pfnBiosGetLCHSGeometry = drvvdBiosGetLCHSGeometry;
    pThis->IMedia.pfnBiosSetLCHSGeometry = drvvdBiosSetLCHSGeometry;
    pThis->IMedia.pfnGetUuid            = drvvdGetUuid;

    /* IMediaAsync */
    pThis->IMediaAsync.pfnStartRead       = drvvdStartRead;
    pThis->IMediaAsync.pfnStartWrite      = drvvdStartWrite;

    /* Initialize supported VD interfaces. */
    pThis->pVDIfsDisk = NULL;

    pThis->VDIErrorCallbacks.cbSize       = sizeof(VDINTERFACEERROR);
    pThis->VDIErrorCallbacks.enmInterface = VDINTERFACETYPE_ERROR;
    pThis->VDIErrorCallbacks.pfnError     = drvvdErrorCallback;
    pThis->VDIErrorCallbacks.pfnMessage   = NULL;

    rc = VDInterfaceAdd(&pThis->VDIError, "DrvVD_VDIError", VDINTERFACETYPE_ERROR,
                        &pThis->VDIErrorCallbacks, pDrvIns, &pThis->pVDIfsDisk);
    AssertRC(rc);

#ifdef VBOX_WITH_PDM_ASYNC_COMPLETION
    pThis->VDIAsyncIOCallbacks.cbSize                  = sizeof(VDINTERFACEASYNCIO);
    pThis->VDIAsyncIOCallbacks.enmInterface            = VDINTERFACETYPE_ASYNCIO;
    pThis->VDIAsyncIOCallbacks.pfnOpen                 = drvvdAsyncIOOpen;
    pThis->VDIAsyncIOCallbacks.pfnClose                = drvvdAsyncIOClose;
    pThis->VDIAsyncIOCallbacks.pfnGetSize              = drvvdAsyncIOGetSize;
    pThis->VDIAsyncIOCallbacks.pfnSetSize              = drvvdAsyncIOSetSize;
    pThis->VDIAsyncIOCallbacks.pfnReadSync             = drvvdAsyncIOReadSync;
    pThis->VDIAsyncIOCallbacks.pfnWriteSync            = drvvdAsyncIOWriteSync;
    pThis->VDIAsyncIOCallbacks.pfnFlushSync            = drvvdAsyncIOFlushSync;
    pThis->VDIAsyncIOCallbacks.pfnReadAsync            = drvvdAsyncIOReadAsync;
    pThis->VDIAsyncIOCallbacks.pfnWriteAsync           = drvvdAsyncIOWriteAsync;
    pThis->VDIAsyncIOCallbacks.pfnFlushAsync           = drvvdAsyncIOFlushAsync;

    rc = VDInterfaceAdd(&pThis->VDIAsyncIO, "DrvVD_AsyncIO", VDINTERFACETYPE_ASYNCIO,
                        &pThis->VDIAsyncIOCallbacks, pThis, &pThis->pVDIfsDisk);
    AssertRC(rc);
#endif

    /* This is just prepared here, the actual interface is per-image, so it's
     * added later. No need to have separate callback tables. */
    pThis->VDIConfigCallbacks.cbSize                = sizeof(VDINTERFACECONFIG);
    pThis->VDIConfigCallbacks.enmInterface          = VDINTERFACETYPE_CONFIG;
    pThis->VDIConfigCallbacks.pfnAreKeysValid       = drvvdCfgAreKeysValid;
    pThis->VDIConfigCallbacks.pfnQuerySize          = drvvdCfgQuerySize;
    pThis->VDIConfigCallbacks.pfnQuery              = drvvdCfgQuery;

    /* List of images is empty now. */
    pThis->pImages = NULL;

    /* Try to attach async media port interface above.*/
    pThis->pDrvMediaAsyncPort = (PPDMIMEDIAASYNCPORT)pDrvIns->pUpBase->pfnQueryInterface(pDrvIns->pUpBase, PDMINTERFACE_MEDIA_ASYNC_PORT);

    /*
     * Validate configuration and find all parent images.
     * It's sort of up side down from the image dependency tree.
     */
    bool        fHostIP = false;
    unsigned    iLevel = 0;
    PCFGMNODE   pCurNode = pCfgHandle;

    for (;;)
    {
        bool fValid;

        if (pCurNode == pCfgHandle)
        {
            /* Toplevel configuration additionally contains the global image
             * open flags. Some might be converted to per-image flags later. */
            fValid = CFGMR3AreValuesValid(pCurNode,
                                          "Format\0Path\0"
                                          "ReadOnly\0TempReadOnly\0HonorZeroWrites\0"
                                          "HostIPStack\0");
        }
        else
        {
            /* All other image configurations only contain image name and
             * the format information. */
            fValid = CFGMR3AreValuesValid(pCurNode, "Format\0Path\0");
        }
        if (!fValid)
        {
            rc = PDMDrvHlpVMSetError(pDrvIns, VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES,
                                     RT_SRC_POS, N_("DrvVD: Configuration error: keys incorrect at level %d"), iLevel);
            break;
        }

        if (pCurNode == pCfgHandle)
        {
            rc = CFGMR3QueryBoolDef(pCurNode, "HostIPStack", &fHostIP, true);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"HostIPStack\" as boolean failed"));
                break;
            }

            rc = CFGMR3QueryBoolDef(pCurNode, "HonorZeroWrites", &fHonorZeroWrites, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"HonorZeroWrites\" as boolean failed"));
                break;
            }

            rc = CFGMR3QueryBoolDef(pCurNode, "ReadOnly", &fReadOnly, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"ReadOnly\" as boolean failed"));
                break;
            }

            rc = CFGMR3QueryBoolDef(pCurNode, "TempReadOnly", &pThis->fTempReadOnly, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"TempReadOnly\" as boolean failed"));
                break;
            }
            if (fReadOnly && pThis->fTempReadOnly)
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Both \"ReadOnly\" and \"TempReadOnly\" are set"));
                break;
            }
        }

        PCFGMNODE pParent = CFGMR3GetChild(pCurNode, "Parent");
        if (!pParent)
            break;
        pCurNode = pParent;
        iLevel++;
    }

    /*
     * Open the images.
     */
    if (RT_SUCCESS(rc))
    {
        /* First of all figure out what kind of TCP networking stack interface
         * to use. This is done unconditionally, as backends which don't need
         * it will just ignore it. */
        if (fHostIP)
        {
            pThis->VDITcpNetCallbacks.cbSize = sizeof(VDINTERFACETCPNET);
            pThis->VDITcpNetCallbacks.enmInterface = VDINTERFACETYPE_TCPNET;
            pThis->VDITcpNetCallbacks.pfnClientConnect = RTTcpClientConnect;
            pThis->VDITcpNetCallbacks.pfnClientClose = RTTcpClientClose;
            pThis->VDITcpNetCallbacks.pfnSelectOne = RTTcpSelectOne;
            pThis->VDITcpNetCallbacks.pfnRead = RTTcpRead;
            pThis->VDITcpNetCallbacks.pfnWrite = RTTcpWrite;
            pThis->VDITcpNetCallbacks.pfnFlush = RTTcpFlush;
        }
        else
        {
#ifndef VBOX_WITH_INIP
            rc = PDMDrvHlpVMSetError(pDrvIns, VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES,
                                     RT_SRC_POS, N_("DrvVD: Configuration error: TCP over Internal Networking not compiled in"));
#else /* VBOX_WITH_INIP */
            pThis->VDITcpNetCallbacks.cbSize = sizeof(VDINTERFACETCPNET);
            pThis->VDITcpNetCallbacks.enmInterface = VDINTERFACETYPE_TCPNET;
            pThis->VDITcpNetCallbacks.pfnClientConnect = drvvdINIPClientConnect;
            pThis->VDITcpNetCallbacks.pfnClientClose = drvvdINIPClientClose;
            pThis->VDITcpNetCallbacks.pfnSelectOne = drvvdINIPSelectOne;
            pThis->VDITcpNetCallbacks.pfnRead = drvvdINIPRead;
            pThis->VDITcpNetCallbacks.pfnWrite = drvvdINIPWrite;
            pThis->VDITcpNetCallbacks.pfnFlush = drvvdINIPFlush;
#endif /* VBOX_WITH_INIP */
        }
        if (RT_SUCCESS(rc))
        {
            rc = VDInterfaceAdd(&pThis->VDITcpNet, "DrvVD_INIP",
                                VDINTERFACETYPE_TCPNET,
                                &pThis->VDITcpNetCallbacks, NULL,
                                &pThis->pVDIfsDisk);
        }
        if (RT_SUCCESS(rc))
        {
            rc = VDCreate(pThis->pVDIfsDisk, &pThis->pDisk);
            /* Error message is already set correctly. */
        }
    }

    if (pThis->pDrvMediaAsyncPort)
        pThis->fAsyncIOSupported = true;

    while (pCurNode && RT_SUCCESS(rc))
    {
        /* Allocate per-image data. */
        PVBOXIMAGE pImage = drvvdNewImage(pThis);
        if (!pImage)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        /*
         * Read the image configuration.
         */
        rc = CFGMR3QueryStringAlloc(pCurNode, "Path", &pszName);
        if (RT_FAILURE(rc))
        {
            rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                  N_("DrvVD: Configuration error: Querying \"Path\" as string failed"));
            break;
        }

        rc = CFGMR3QueryStringAlloc(pCurNode, "Format", &pszFormat);
        if (RT_FAILURE(rc))
        {
            rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                  N_("DrvVD: Configuration error: Querying \"Format\" as string failed"));
            break;
        }

        PCFGMNODE pCfg = CFGMR3GetChild(pCurNode, "VDConfig");
        rc = VDInterfaceAdd(&pImage->VDIConfig, "DrvVD_Config", VDINTERFACETYPE_CONFIG,
                            &pThis->VDIConfigCallbacks, pCfg, &pImage->pVDIfsImage);
        AssertRC(rc);

        /*
         * Open the image.
         */
        unsigned uOpenFlags;
        if (fReadOnly || pThis->fTempReadOnly || iLevel != 0)
            uOpenFlags = VD_OPEN_FLAGS_READONLY;
        else
            uOpenFlags = VD_OPEN_FLAGS_NORMAL;
        if (fHonorZeroWrites)
            uOpenFlags |= VD_OPEN_FLAGS_HONOR_ZEROES;
        if (pThis->fAsyncIOSupported)
            uOpenFlags |= VD_OPEN_FLAGS_ASYNC_IO;

        /* Try to open backend in async I/O mode first. */
        rc = VDOpen(pThis->pDisk, pszFormat, pszName, uOpenFlags, pImage->pVDIfsImage);
        if (rc == VERR_NOT_SUPPORTED)
        {
            /* Seems async I/O is not supported by the backend, open in normal mode. */
            pThis->fAsyncIOSupported = false;
            uOpenFlags &= ~VD_OPEN_FLAGS_ASYNC_IO;
            rc = VDOpen(pThis->pDisk, pszFormat, pszName, uOpenFlags, pImage->pVDIfsImage);
        }

        if (RT_SUCCESS(rc))
        {
            Log(("%s: %d - Opened '%s' in %s mode\n", __FUNCTION__,
                 iLevel, pszName,
                 VDIsReadOnly(pThis->pDisk) ? "read-only" : "read-write"));
            if (   VDIsReadOnly(pThis->pDisk)
                && !fReadOnly
                && !pThis->fTempReadOnly
                && iLevel == 0)
            {
                rc = PDMDrvHlpVMSetError(pDrvIns, VERR_VD_IMAGE_READ_ONLY, RT_SRC_POS,
                                         N_("Failed to open image '%s' for writing due to wrong permissions"),
                                         pszName);
                break;
            }
        }
        else
        {
           rc = PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                    N_("Failed to open image '%s' in %s mode rc=%Rrc"), pszName,
                                    (uOpenFlags & VD_OPEN_FLAGS_READONLY) ? "readonly" : "read-write", rc);
           break;
        }


        MMR3HeapFree(pszName);
        pszName = NULL;
        MMR3HeapFree(pszFormat);
        pszFormat = NULL;

        /* next */
        iLevel--;
        pCurNode = CFGMR3GetParent(pCurNode);
    }

    if (RT_FAILURE(rc))
    {
        if (VALID_PTR(pszName))
            MMR3HeapFree(pszName);
        if (VALID_PTR(pszFormat))
            MMR3HeapFree(pszFormat);
        /* drvvdDestruct does the rest. */
    }
    else
    {
        /* Switch to runtime error facility. */
        pThis->fErrorUseRuntime = true;
    }

    /* else: drvvdDestruct cleans up. */

    LogFlow(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}

/**
 * Destruct a driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvvdDestruct(PPDMDRVINS pDrvIns)
{
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);
    LogFlow(("%s:\n", __FUNCTION__));

    if (VALID_PTR(pThis->pDisk))
    {
        VDDestroy(pThis->pDisk);
        pThis->pDisk = NULL;
    }
    drvvdFreeImages(pThis);
}


/**
 * When the VM has been suspended we'll change the image mode to read-only
 * so that main and others can read the VDIs. This is important when
 * saving state and so forth.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvvdSuspend(PPDMDRVINS pDrvIns)
{
    LogFlow(("%s:\n", __FUNCTION__));
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);
    if (!VDIsReadOnly(pThis->pDisk))
    {
        unsigned uOpenFlags;
        int rc = VDGetOpenFlags(pThis->pDisk, VD_LAST_IMAGE, &uOpenFlags);
        AssertRC(rc);
        uOpenFlags |= VD_OPEN_FLAGS_READONLY;
        rc = VDSetOpenFlags(pThis->pDisk, VD_LAST_IMAGE, uOpenFlags);
        AssertRC(rc);
        pThis->fTempReadOnly = true;
    }
}

/**
 * Before the VM resumes we'll have to undo the read-only mode change
 * done in drvvdSuspend.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvvdResumeOrPowerOn(PPDMDRVINS pDrvIns)
{
    LogFlow(("%s:\n", __FUNCTION__));
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);
    if (pThis->fTempReadOnly)
    {
        unsigned uOpenFlags;
        int rc = VDGetOpenFlags(pThis->pDisk, VD_LAST_IMAGE, &uOpenFlags);
        AssertRC(rc);
        uOpenFlags &= ~VD_OPEN_FLAGS_READONLY;
        rc = VDSetOpenFlags(pThis->pDisk, VD_LAST_IMAGE, uOpenFlags);
        AssertRC(rc);
        pThis->fTempReadOnly = false;
    }
}

static DECLCALLBACK(void) drvvdPowerOff(PPDMDRVINS pDrvIns)
{
    LogFlow(("%s:\n", __FUNCTION__));
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);

    /*
     * We must close the disk here to ensure that
     * the backend closes all files before the
     * async transport driver is destructed.
     */
    int rc = VDCloseAll(pThis->pDisk);
    AssertRC(rc);
}


/**
 * VBox disk container media driver registration record.
 */
const PDMDRVREG g_DrvVD =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szDriverName */
    "VD",
    /* pszDescription */
    "Generic VBox disk media driver.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_MEDIA,
    /* cMaxInstances */
    ~0,
    /* cbInstance */
    sizeof(VBOXDISK),
    /* pfnConstruct */
    drvvdConstruct,
    /* pfnDestruct */
    drvvdDestruct,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    drvvdResumeOrPowerOn,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    drvvdSuspend,
    /* pfnResume */
    drvvdResumeOrPowerOn,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    drvvdPowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

