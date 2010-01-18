/** @file
 *
 * VBox stream I/O devices:
 * Generic char driver
 */

/*
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



/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_CHAR
#include <VBox/pdmdrv.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/stream.h>
#include <iprt/semaphore.h>

#include "Builtins.h"


/** Size of the send fifo queue (in bytes) */
#define CHAR_MAX_SEND_QUEUE             0x80
#define CHAR_MAX_SEND_QUEUE_MASK        0x7f

/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/

/**
 * Char driver instance data.
 */
typedef struct DRVCHAR
{
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Pointer to the char port interface of the driver/device above us. */
    PPDMICHARPORT               pDrvCharPort;
    /** Pointer to the stream interface of the driver below us. */
    PPDMISTREAM                 pDrvStream;
    /** Our char interface. */
    PDMICHAR                    IChar;
    /** Flag to notify the receive thread it should terminate. */
    volatile bool               fShutdown;
    /** Receive thread ID. */
    RTTHREAD                    ReceiveThread;
    /** Send thread ID. */
    RTTHREAD                    SendThread;
    /** Send event semephore */
    RTSEMEVENT                  SendSem;

    /** Internal send FIFO queue */
    uint8_t                     aSendQueue[CHAR_MAX_SEND_QUEUE];
    uint32_t                    iSendQueueHead;
    uint32_t                    iSendQueueTail;

    uintptr_t                   AlignmentPadding;
    /** Read/write statistics */
    STAMCOUNTER                 StatBytesRead;
    STAMCOUNTER                 StatBytesWritten;
} DRVCHAR, *PDRVCHAR;
AssertCompileMemberAlignment(DRVCHAR, StatBytesRead, 8);


/** Converts a pointer to DRVCHAR::IChar to a PDRVCHAR. */
#define PDMICHAR_2_DRVCHAR(pInterface) ( (PDRVCHAR)((uintptr_t)pInterface - RT_OFFSETOF(DRVCHAR, IChar)) )


/* -=-=-=-=- IBase -=-=-=-=- */

/**
 * Queries an interface to the driver.
 *
 * @returns Pointer to interface.
 * @returns NULL if the interface was not supported by the driver.
 * @param   pInterface          Pointer to this interface structure.
 * @param   enmInterface        The requested interface identification.
 */
static DECLCALLBACK(void *) drvCharQueryInterface(PPDMIBASE pInterface, PDMINTERFACE enmInterface)
{
    PPDMDRVINS  pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVCHAR    pThis = PDMINS_2_DATA(pDrvIns, PDRVCHAR);
    switch (enmInterface)
    {
        case PDMINTERFACE_BASE:
            return &pDrvIns->IBase;
        case PDMINTERFACE_CHAR:
            return &pThis->IChar;
        default:
            return NULL;
    }
}


/* -=-=-=-=- IChar -=-=-=-=- */

/** @copydoc PDMICHAR::pfnWrite */
static DECLCALLBACK(int) drvCharWrite(PPDMICHAR pInterface, const void *pvBuf, size_t cbWrite)
{
    PDRVCHAR pThis = PDMICHAR_2_DRVCHAR(pInterface);
    const char *pBuffer = (const char *)pvBuf;

    LogFlow(("%s: pvBuf=%#p cbWrite=%d\n", __FUNCTION__, pvBuf, cbWrite));

    for (uint32_t i=0;i<cbWrite;i++)
    {
        uint32_t idx = pThis->iSendQueueHead;

        pThis->aSendQueue[idx] = pBuffer[i];
        idx = (idx + 1) & CHAR_MAX_SEND_QUEUE_MASK;

        STAM_COUNTER_INC(&pThis->StatBytesWritten);
        ASMAtomicXchgU32(&pThis->iSendQueueHead, idx);
    }
    RTSemEventSignal(pThis->SendSem);
    return VINF_SUCCESS;
}

/** @copydoc PDMICHAR::pfnSetParameters */
static DECLCALLBACK(int) drvCharSetParameters(PPDMICHAR pInterface, unsigned Bps, char chParity, unsigned cDataBits, unsigned cStopBits)
{
    /*PDRVCHAR pThis = PDMICHAR_2_DRVCHAR(pInterface); - unused*/

    LogFlow(("%s: Bps=%u chParity=%c cDataBits=%u cStopBits=%u\n", __FUNCTION__, Bps, chParity, cDataBits, cStopBits));
    return VINF_SUCCESS;
}


/* -=-=-=-=- receive thread -=-=-=-=- */

/**
 * Send thread loop.
 *
 * @returns 0 on success.
 * @param   ThreadSelf  Thread handle to this thread.
 * @param   pvUser      User argument.
 */
static DECLCALLBACK(int) drvCharSendLoop(RTTHREAD ThreadSelf, void *pvUser)
{
    PDRVCHAR pThis = (PDRVCHAR)pvUser;

    for(;;)
    {
        int rc = RTSemEventWait(pThis->SendSem, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
            break;

        /*
         * Write the character to the attached stream (if present).
         */
        if (    !pThis->fShutdown
            &&  pThis->pDrvStream)
        {
            while (pThis->iSendQueueTail != pThis->iSendQueueHead)
            {
                size_t cbProcessed = 1;

                rc = pThis->pDrvStream->pfnWrite(pThis->pDrvStream, &pThis->aSendQueue[pThis->iSendQueueTail], &cbProcessed);
                if (RT_SUCCESS(rc))
                {
                    Assert(cbProcessed);
                    pThis->iSendQueueTail++;
                    pThis->iSendQueueTail &= CHAR_MAX_SEND_QUEUE_MASK;
                }
                else if (rc == VERR_TIMEOUT)
                {
                    /* Normal case, just means that the stream didn't accept a new
                     * character before the timeout elapsed. Just retry. */
                    rc = VINF_SUCCESS;
                }
                else
                {
                    LogFlow(("Write failed with %Rrc; skipping\n", rc));
                    break;
                }
            }
        }
        else
            break;
    }

    pThis->SendThread = NIL_RTTHREAD;

    return VINF_SUCCESS;
}

/* -=-=-=-=- receive thread -=-=-=-=- */

/**
 * Receive thread loop.
 *
 * @returns 0 on success.
 * @param   ThreadSelf  Thread handle to this thread.
 * @param   pvUser      User argument.
 */
static DECLCALLBACK(int) drvCharReceiveLoop(RTTHREAD ThreadSelf, void *pvUser)
{
    PDRVCHAR pThis = (PDRVCHAR)pvUser;
    char aBuffer[256], *pBuffer;
    size_t cbRemaining, cbProcessed;
    int rc;

    cbRemaining = 0;
    pBuffer = aBuffer;
    while (!pThis->fShutdown)
    {
        if (!cbRemaining)
        {
            /* Get block of data from stream driver. */
            if (pThis->pDrvStream)
            {
                cbRemaining = sizeof(aBuffer);
                rc = pThis->pDrvStream->pfnRead(pThis->pDrvStream, aBuffer, &cbRemaining);
                if (RT_FAILURE(rc))
                {
                    LogFlow(("Read failed with %Rrc\n", rc));
                    break;
                }
            }
            else
            {
                cbRemaining = 0;
                RTThreadSleep(100);
            }
            pBuffer = aBuffer;
        }
        else
        {
            /* Send data to guest. */
            cbProcessed = cbRemaining;
            rc = pThis->pDrvCharPort->pfnNotifyRead(pThis->pDrvCharPort, pBuffer, &cbProcessed);
            if (RT_SUCCESS(rc))
            {
                Assert(cbProcessed);
                pBuffer += cbProcessed;
                cbRemaining -= cbProcessed;
                STAM_COUNTER_ADD(&pThis->StatBytesRead, cbProcessed);
            }
            else if (rc == VERR_TIMEOUT)
            {
                /* Normal case, just means that the guest didn't accept a new
                 * character before the timeout elapsed. Just retry. */
                rc = VINF_SUCCESS;
            }
            else
            {
                LogFlow(("NotifyRead failed with %Rrc\n", rc));
                break;
            }
        }
    }

    pThis->ReceiveThread = NIL_RTTHREAD;

    return VINF_SUCCESS;
}

/**
 * Set the modem lines.
 *
 * @returns VBox status code
 * @param pInterface        Pointer to the interface structure.
 * @param RequestToSend     Set to true if this control line should be made active.
 * @param DataTerminalReady Set to true if this control line should be made active.
 */
static DECLCALLBACK(int) drvCharSetModemLines(PPDMICHAR pInterface, bool RequestToSend, bool DataTerminalReady)
{
    /* Nothing to do here. */
    return VINF_SUCCESS;
}

/**
 * Sets the TD line into break condition.
 *
 * @returns VBox status code.
 * @param   pInterface  Pointer to the interface structure containing the called function pointer.
 * @param   fBreak      Set to true to let the device send a break false to put into normal operation.
 * @thread  Any thread.
 */
static DECLCALLBACK(int) drvCharSetBreak(PPDMICHAR pInterface, bool fBreak)
{
    /* Nothing to do here. */
    return VINF_SUCCESS;
}

/* -=-=-=-=- driver interface -=-=-=-=- */

/**
 * Construct a char driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvCharConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfgHandle, uint32_t fFlags)
{
    PDRVCHAR pThis = PDMINS_2_DATA(pDrvIns, PDRVCHAR);
    LogFlow(("%s: iInstance=%d\n", __FUNCTION__, pDrvIns->iInstance));

    /*
     * Init basic data members and interfaces.
     */
    pThis->ReceiveThread                    = NIL_RTTHREAD;
    pThis->fShutdown                        = false;
    /* IBase. */
    pDrvIns->IBase.pfnQueryInterface        = drvCharQueryInterface;
    /* IChar. */
    pThis->IChar.pfnWrite                   = drvCharWrite;
    pThis->IChar.pfnSetParameters           = drvCharSetParameters;
    pThis->IChar.pfnSetModemLines           = drvCharSetModemLines;
    pThis->IChar.pfnSetBreak                = drvCharSetBreak;

    /*
     * Get the ICharPort interface of the above driver/device.
     */
    pThis->pDrvCharPort = (PPDMICHARPORT)pDrvIns->pUpBase->pfnQueryInterface(pDrvIns->pUpBase, PDMINTERFACE_CHAR_PORT);
    if (!pThis->pDrvCharPort)
        return PDMDrvHlpVMSetError(pDrvIns, VERR_PDM_MISSING_INTERFACE_ABOVE, RT_SRC_POS, N_("Char#%d has no char port interface above"), pDrvIns->iInstance);

    /*
     * Attach driver below and query its stream interface.
     */
    PPDMIBASE pBase;
    int rc = PDMDrvHlpAttach(pDrvIns, fFlags, &pBase);
    if (RT_FAILURE(rc))
        return rc; /* Don't call PDMDrvHlpVMSetError here as we assume that the driver already set an appropriate error */
    pThis->pDrvStream = (PPDMISTREAM)pBase->pfnQueryInterface(pBase, PDMINTERFACE_STREAM);
    if (!pThis->pDrvStream)
        return PDMDrvHlpVMSetError(pDrvIns, VERR_PDM_MISSING_INTERFACE_BELOW, RT_SRC_POS, N_("Char#%d has no stream interface below"), pDrvIns->iInstance);

    /*
     * Don't start the receive thread if the driver doesn't support reading
     */
    if (pThis->pDrvStream->pfnRead)
    {
        rc = RTThreadCreate(&pThis->ReceiveThread, drvCharReceiveLoop, (void *)pThis, 0, RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "CharRecv");
        if (RT_FAILURE(rc))
            return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, N_("Char#%d cannot create receive thread"), pDrvIns->iInstance);
    }

    rc = RTSemEventCreate(&pThis->SendSem);
    AssertRC(rc);

    rc = RTThreadCreate(&pThis->SendThread, drvCharSendLoop, (void *)pThis, 0, RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "CharSend");
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, N_("Char#%d cannot create send thread"), pDrvIns->iInstance);


    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatBytesWritten,    STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_BYTES, "Nr of bytes written",         "/Devices/Char%d/Written", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatBytesRead,       STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_BYTES, "Nr of bytes read",            "/Devices/Char%d/Read", pDrvIns->iInstance);

    return VINF_SUCCESS;
}


/**
 * Destruct a char driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that
 * any non-VM resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvCharDestruct(PPDMDRVINS pDrvIns)
{
    PDRVCHAR     pThis = PDMINS_2_DATA(pDrvIns, PDRVCHAR);

    LogFlow(("%s: iInstance=%d\n", __FUNCTION__, pDrvIns->iInstance));

    pThis->fShutdown = true;
    if (pThis->ReceiveThread)
    {
        RTThreadWait(pThis->ReceiveThread, 1000, NULL);
        if (pThis->ReceiveThread != NIL_RTTHREAD)
            LogRel(("Char%d: receive thread did not terminate\n", pDrvIns->iInstance));
    }

    /* Empty the send queue */
    pThis->iSendQueueTail = pThis->iSendQueueHead = 0;

    RTSemEventSignal(pThis->SendSem);
    RTSemEventDestroy(pThis->SendSem);
    pThis->SendSem = NIL_RTSEMEVENT;

    if (pThis->SendThread)
    {
        RTThreadWait(pThis->SendThread, 1000, NULL);
        if (pThis->SendThread != NIL_RTTHREAD)
            LogRel(("Char%d: send thread did not terminate\n", pDrvIns->iInstance));
    }
}

/**
 * Char driver registration record.
 */
const PDMDRVREG g_DrvChar =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szDriverName */
    "Char",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Generic char driver.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_CHAR,
    /* cMaxInstances */
    ~0,
    /* cbInstance */
    sizeof(DRVCHAR),
    /* pfnConstruct */
    drvCharConstruct,
    /* pfnDestruct */
    drvCharDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

