/** @file
 *
 * VBox network devices:
 * Linux TAP network transport driver
 */

/*
 *
 * Copyright (C) 2006-2007 innotek GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#define ASYNC_NET

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_TUN
#include <VBox/pdmdrv.h>

#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/string.h>
#ifdef ASYNC_NET
#include <iprt/thread.h>
#include <iprt/asm.h>
#include <iprt/semaphore.h>
#endif

#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/fcntl.h>
#include <errno.h>
#ifdef ASYNC_NET
#include <unistd.h>
#endif

#ifdef RT_OS_L4
#include <l4/vboxserver/file.h>
#endif

#include "Builtins.h"


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
typedef enum ASYNCSTATE
{
    //ASYNCSTATE_SUSPENDED = 1,
    ASYNCSTATE_RUNNING,
    ASYNCSTATE_TERMINATE
} ASYNCSTATE;

/**
 * Block driver instance data.
 */
typedef struct DRVTAP
{
    /** The network interface. */
    PDMINETWORKCONNECTOR    INetworkConnector;
    /** The network interface. */
    PPDMINETWORKPORT        pPort;
    /** Pointer to the driver instance. */
    PPDMDRVINS              pDrvIns;
    /** TAP device file handle. */
    RTFILE                  FileDevice;
#ifdef ASYNC_NET
    /** The write end of the control pipe. */
    RTFILE                  PipeWrite;
    /** The read end of the control pipe. */
    RTFILE                  PipeRead;
    /** The thread state. */
    ASYNCSTATE volatile     enmState;
    /** Reader thread. */
    RTTHREAD                Thread;
    /** We are waiting for more receive buffers. */
    uint32_t volatile       fOutOfSpace;
    /** Event semaphore for blocking on receive. */
    RTSEMEVENT              EventOutOfSpace;
#endif

#ifdef VBOX_WITH_STATISTICS
    /** Number of sent packets. */
    STAMCOUNTER             StatPktSent;
    /** Number of sent bytes. */
    STAMCOUNTER             StatPktSentBytes;
    /** Number of received packets. */
    STAMCOUNTER             StatPktRecv;
    /** Number of received bytes. */
    STAMCOUNTER             StatPktRecvBytes;
    /** Profiling packet transmit runs. */
    STAMPROFILE             StatTransmit;
    /** Profiling packet receive runs. */
    STAMPROFILEADV          StatReceive;
#ifdef ASYNC_NET
    STAMPROFILE             StatRecvOverflows;
#endif
#endif /* VBOX_WITH_STATISTICS */

#ifdef LOG_ENABLED
    /** The nano ts of the last transfer. */
    uint64_t                u64LastTransferTS;
    /** The nano ts of the last receive. */
    uint64_t                u64LastReceiveTS;
#endif
} DRVTAP, *PDRVTAP;


/** Converts a pointer to TAP::INetworkConnector to a PRDVTAP. */
#define PDMINETWORKCONNECTOR_2_DRVTAP(pInterface) ( (PDRVTAP)((uintptr_t)pInterface - RT_OFFSETOF(DRVTAP, INetworkConnector)) )


/**
 * Send data to the network.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   pvBuf           Data to send.
 * @param   cb              Number of bytes to send.
 * @thread  EMT
 */
static DECLCALLBACK(int) drvTAPSend(PPDMINETWORKCONNECTOR pInterface, const void *pvBuf, size_t cb)
{
    PDRVTAP pData = PDMINETWORKCONNECTOR_2_DRVTAP(pInterface);
    STAM_COUNTER_INC(&pData->StatPktSent);
    STAM_COUNTER_ADD(&pData->StatPktSentBytes, cb);
    STAM_PROFILE_START(&pData->StatTransmit, a);

#ifdef LOG_ENABLED
    uint64_t u64Now = RTTimeProgramNanoTS();
    LogFlow(("drvTAPSend: %-4d bytes at %llu ns  deltas: r=%llu t=%llu\n",
             cb, u64Now, u64Now - pData->u64LastReceiveTS, u64Now - pData->u64LastTransferTS));
    pData->u64LastTransferTS = u64Now;
#endif
    Log2(("drvTAPSend: pvBuf=%p cb=%#x\n"
          "%.*Vhxd\n",
          pvBuf, cb, cb, pvBuf));

    int rc = RTFileWrite(pData->FileDevice, pvBuf, cb, NULL);

    STAM_PROFILE_STOP(&pData->StatTransmit, a);
    AssertRC(rc);
    return rc;
}


/**
 * Set promiscuous mode.
 *
 * This is called when the promiscuous mode is set. This means that there doesn't have
 * to be a mode change when it's called.
 *
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   fPromiscuous    Set if the adaptor is now in promiscuous mode. Clear if it is not.
 * @thread  EMT
 */
static DECLCALLBACK(void) drvTAPSetPromiscuousMode(PPDMINETWORKCONNECTOR pInterface, bool fPromiscuous)
{
    LogFlow(("drvTAPSetPromiscuousMode: fPromiscuous=%d\n", fPromiscuous));
    /* nothing to do */
}


/**
 * Notification on link status changes.
 *
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   enmLinkState    The new link state.
 * @thread  EMT
 */
static DECLCALLBACK(void) drvTAPNotifyLinkChanged(PPDMINETWORKCONNECTOR pInterface, PDMNETWORKLINKSTATE enmLinkState)
{
    LogFlow(("drvNATNotifyLinkChanged: enmLinkState=%d\n", enmLinkState));
    /** @todo take action on link down and up. Stop the polling and such like. */
}


/**
 * More receive buffer has become available.
 *
 * This is called when the NIC frees up receive buffers.
 *
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @thread  EMT
 */
static DECLCALLBACK(void) drvTAPNotifyCanReceive(PPDMINETWORKCONNECTOR pInterface)
{
    PDRVTAP pData = PDMINETWORKCONNECTOR_2_DRVTAP(pInterface);

    LogFlow(("drvTAPNotifyCanReceive:\n"));
    /* ensure we wake up only once */
    if (ASMAtomicXchgU32(&pData->fOutOfSpace, false))
        RTSemEventSignal(pData->EventOutOfSpace);
}


#ifdef ASYNC_NET
/**
 * Asynchronous I/O thread for handling receive.
 *
 * @returns VINF_SUCCESS (ignored).
 * @param   Thread          Thread handle.
 * @param   pvUser          Pointer to a DRVTAP structure.
 */
static DECLCALLBACK(int) drvTAPAsyncIoThread(RTTHREAD ThreadSelf, void *pvUser)
{
    PDRVTAP pData = (PDRVTAP)pvUser;
    LogFlow(("drvTAPAsyncIoThread: pData=%p\n", pData));
    STAM_PROFILE_ADV_START(&pData->StatReceive, a);

    int rc = RTSemEventCreate(&pData->EventOutOfSpace);
    AssertRC(rc);

    /*
     * Polling loop.
     */
    for (;;)
    {
        /*
         * Wait for something to become available.
         */
        struct pollfd aFDs[2];
        aFDs[0].fd      = pData->FileDevice;
        aFDs[0].events  = POLLIN | POLLPRI;
        aFDs[0].revents = 0;
        aFDs[1].fd      = pData->PipeRead;
        aFDs[1].events  = POLLIN | POLLPRI | POLLERR | POLLHUP;
        aFDs[1].revents = 0;
        STAM_PROFILE_ADV_STOP(&pData->StatReceive, a);
        errno=0;
        rc = poll(&aFDs[0], ELEMENTS(aFDs), -1 /* infinite */);
        STAM_PROFILE_ADV_START(&pData->StatReceive, a);
        if (    rc > 0
            &&  (aFDs[0].revents & (POLLIN | POLLPRI))
            &&  !aFDs[1].revents)
        {
            /*
             * Read the frame.
             */
            char achBuf[4096];
            unsigned cbRead = 0;
            rc = RTFileRead(pData->FileDevice, achBuf, sizeof(achBuf), &cbRead);
            if (VBOX_SUCCESS(rc))
            {
                AssertMsg(cbRead <= 1536, ("cbRead=%d\n", cbRead));

                /*
                 * Wait for the device to have space for this frame.
                 */
                size_t cbMax = pData->pPort->pfnCanReceive(pData->pPort);
                if (cbMax < cbRead)
                {
                    /** @todo receive overflow handling needs serious improving! */
                    STAM_PROFILE_ADV_STOP(&pData->StatReceive, a);
                    STAM_PROFILE_START(&pData->StatRecvOverflows, b);
                    while (   cbMax < cbRead
                           && pData->enmState != ASYNCSTATE_TERMINATE)
                    {
                        LogFlow(("drvTAPAsyncIoThread: cbMax=%d cbRead=%d waiting...\n", cbMax, cbRead));
#if 1
                        /* We get signalled by the network driver. 50ms is just for sanity */
                        ASMAtomicXchgU32(&pData->fOutOfSpace, true);
                        RTSemEventWait(pData->EventOutOfSpace, 50);
#else
                        RTThreadSleep(1);
#endif
                        cbMax = pData->pPort->pfnCanReceive(pData->pPort);
                    }
                    ASMAtomicXchgU32(&pData->fOutOfSpace, false);
                    STAM_PROFILE_STOP(&pData->StatRecvOverflows, b);
                    STAM_PROFILE_ADV_START(&pData->StatReceive, a);
                    if (pData->enmState == ASYNCSTATE_TERMINATE)
                        break;
                }

                /*
                 * Pass the data up.
                 */
#ifdef LOG_ENABLED
                uint64_t u64Now = RTTimeProgramNanoTS();
                LogFlow(("drvTAPAsyncIoThread: %-4d bytes at %llu ns  deltas: r=%llu t=%llu\n",
                         cbRead, u64Now, u64Now - pData->u64LastReceiveTS, u64Now - pData->u64LastTransferTS));
                pData->u64LastReceiveTS = u64Now;
#endif
                Log2(("drvTAPAsyncIoThread: cbRead=%#x\n"
                      "%.*Vhxd\n",
                      cbRead, cbRead, achBuf));
                STAM_COUNTER_INC(&pData->StatPktRecv);
                STAM_COUNTER_ADD(&pData->StatPktRecvBytes, cbRead);
                rc = pData->pPort->pfnReceive(pData->pPort, achBuf, cbRead);
                AssertRC(rc);
            }
            else
            {
                LogFlow(("drvTAPAsyncIoThread: RTFileRead -> %Vrc\n", rc));
                if (rc == VERR_INVALID_HANDLE)
                    break;
                RTThreadYield();
            }
        }
        else if (   rc > 0
                 && aFDs[1].revents)
        {
            LogFlow(("drvTAPAsyncIoThread: Control message: enmState=%d revents=%#x\n", pData->enmState, aFDs[1].revents));
            if (pData->enmState == ASYNCSTATE_TERMINATE)
                break;
            if (aFDs[1].revents & (POLLHUP | POLLERR | POLLNVAL))
                break;

            /* drain the pipe */
            char ch;
            unsigned cbRead;
            RTFileRead(pData->PipeRead, &ch, 1, &cbRead);
        }
        else
        {
            /*
             * poll() failed for some reason. Yield to avoid eating too much CPU.
             *
             * EINTR errors have been seen frequently. They should be harmless, even
             * if they are not supposed to occur in our setup.
             */
            if (errno == EINTR)
                Log(("rc=%d revents=%#x,%#x errno=%p %s\n", rc, aFDs[0].revents, aFDs[1].revents, errno, strerror(errno)));
            else
                AssertMsgFailed(("rc=%d revents=%#x,%#x errno=%p %s\n", rc, aFDs[0].revents, aFDs[1].revents, errno, strerror(errno)));
            RTThreadYield();
        }
    }

    rc = RTSemEventDestroy(pData->EventOutOfSpace);
    AssertRC(rc);

    LogFlow(("drvTAPAsyncIoThread: returns %Vrc\n", VINF_SUCCESS));
    STAM_PROFILE_ADV_STOP(&pData->StatReceive, a);
    return VINF_SUCCESS;
}

#else
/**
 * Poller callback.
 */
static DECLCALLBACK(void) drvTAPPoller(PPDMDRVINS pDrvIns)
{
    /* check how much the device/driver can receive now. */
    PDRVTAP pData = PDMINS2DATA(pDrvIns, PDRVTAP);
    STAM_PROFILE_ADV_START(&pData->StatReceive, a);

    size_t  cbMax = pData->pPort->pfnCanReceive(pData->pPort);
    while (cbMax > 0)
    {
        /* check for data to read */
        struct pollfd aFDs[1];
        aFDs[0].fd      = pData->FileDevice;
        aFDs[0].events  = POLLIN | POLLPRI;
        aFDs[0].revents = 0;
        if (poll(&aFDs[0], 1, 0) > 0)
        {
            if (aFDs[0].revents & (POLLIN | POLLPRI))
            {
                /* data waiting, read it. */
                char        achBuf[4096];
                unsigned    cbRead = 0;
                int rc = RTFileRead(pData->FileDevice, achBuf, RT_MIN(sizeof(achBuf), cbMax), &cbRead);
                if (VBOX_SUCCESS(rc))
                {
                    STAM_COUNTER_INC(&pData->StatPktRecv);
                    STAM_COUNTER_ADD(&pData->StatPktRecvBytes, cbRead);

                    /* push it up to guy over us. */
                    Log2(("drvTAPPoller: cbRead=%#x\n"
                          "%.*Vhxd\n",
                          cbRead, cbRead, achBuf));
                    rc = pData->pPort->pfnReceive(pData->pPort, achBuf, cbRead);
                    AssertRC(rc);
                }
                else
                    AssertRC(rc);
                if (VBOX_FAILURE(rc) || !cbRead)
                    break;
            }
            else
                break;
        }
        else
            break;

        cbMax = pData->pPort->pfnCanReceive(pData->pPort);
    }

    STAM_PROFILE_ADV_STOP(&pData->StatReceive, a);
}
#endif


/**
 * Queries an interface to the driver.
 *
 * @returns Pointer to interface.
 * @returns NULL if the interface was not supported by the driver.
 * @param   pInterface          Pointer to this interface structure.
 * @param   enmInterface        The requested interface identification.
 * @thread  Any thread.
 */
static DECLCALLBACK(void *) drvTAPQueryInterface(PPDMIBASE pInterface, PDMINTERFACE enmInterface)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVTAP pData = PDMINS2DATA(pDrvIns, PDRVTAP);
    switch (enmInterface)
    {
        case PDMINTERFACE_BASE:
            return &pDrvIns->IBase;
        case PDMINTERFACE_NETWORK_CONNECTOR:
            return &pData->INetworkConnector;
        default:
            return NULL;
    }
}


/**
 * Destruct a driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvTAPDestruct(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvTAPDestruct\n"));
#ifdef ASYNC_NET
    PDRVTAP pData = PDMINS2DATA(pDrvIns, PDRVTAP);

    /*
     * Terminate the Async I/O Thread.
     */
    ASMAtomicXchgSize(&pData->enmState, ASYNCSTATE_TERMINATE);
    if (pData->Thread != NIL_RTTHREAD)
    {
        /* Ensure that it does not spin in the CanReceive loop */
        if (ASMAtomicXchgU32(&pData->fOutOfSpace, false))
            RTSemEventSignal(pData->EventOutOfSpace);

        int rc = RTFileWrite(pData->PipeWrite, "", 1, NULL);
        AssertRC(rc);
        rc = RTThreadWait(pData->Thread, 5000, NULL);
        AssertRC(rc);
        pData->Thread = NIL_RTTHREAD;
    }

    /*
     * Terminate the control pipe.
     */
    if (pData->PipeWrite != NIL_RTFILE)
    {
        int rc = RTFileClose(pData->PipeWrite);
        AssertRC(rc);
        pData->PipeWrite = NIL_RTFILE;
    }
    if (pData->PipeRead != NIL_RTFILE)
    {
        int rc = RTFileClose(pData->PipeRead);
        AssertRC(rc);
        pData->PipeRead = NIL_RTFILE;
    }
#endif
}


/**
 * Construct a TAP network transport driver instance.
 *
 * @returns VBox status.
 * @param   pDrvIns     The driver instance data.
 *                      If the registration structure is needed, pDrvIns->pDrvReg points to it.
 * @param   pCfgHandle  Configuration node handle for the driver. Use this to obtain the configuration
 *                      of the driver instance. It's also found in pDrvIns->pCfgHandle, but like
 *                      iInstance it's expected to be used a bit in this function.
 */
static DECLCALLBACK(int) drvTAPConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfgHandle)
{
    PDRVTAP pData = PDMINS2DATA(pDrvIns, PDRVTAP);

    /*
     * Init the static parts.
     */
    pData->pDrvIns                      = pDrvIns;
    pData->FileDevice                   = NIL_RTFILE;
#ifdef ASYNC_NET
    pData->Thread                       = NIL_RTTHREAD;
    pData->enmState                     = ASYNCSTATE_RUNNING;
#endif
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface    = drvTAPQueryInterface;
    /* INetwork */
    pData->INetworkConnector.pfnSend                = drvTAPSend;
    pData->INetworkConnector.pfnSetPromiscuousMode  = drvTAPSetPromiscuousMode;
    pData->INetworkConnector.pfnNotifyLinkChanged   = drvTAPNotifyLinkChanged;
    pData->INetworkConnector.pfnNotifyCanReceive    = drvTAPNotifyCanReceive;

    /*
     * Validate the config.
     */
    if (!CFGMR3AreValuesValid(pCfgHandle, "Device\0InitProg\0TermProg\0FileHandle\0"))
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES, "");

    /*
     * Check that no-one is attached to us.
     */
    int rc = pDrvIns->pDrvHlp->pfnAttach(pDrvIns, NULL);
    if (rc != VERR_PDM_NO_ATTACHED_DRIVER)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_DRVINS_NO_ATTACH,
                                N_("Configuration error: Cannot attach drivers to the TAP driver!"));

    /*
     * Query the network port interface.
     */
    pData->pPort = (PPDMINETWORKPORT)pDrvIns->pUpBase->pfnQueryInterface(pDrvIns->pUpBase, PDMINTERFACE_NETWORK_PORT);
    if (!pData->pPort)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_MISSING_INTERFACE_ABOVE,
                                N_("Configuration error: The above device/driver didn't export the network port interface!"));

    /*
     * Read the configuration.
     */
    int32_t iFile;
    rc = CFGMR3QueryS32(pCfgHandle, "FileHandle", &iFile);
    if (VBOX_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Query for \"FileHandle\" 32-bit signed integer failed!"));
    pData->FileDevice = (RTFILE)iFile;
    if (!RTFileIsValid(pData->FileDevice))
        return PDMDrvHlpVMSetError(pDrvIns, VERR_INVALID_HANDLE, RT_SRC_POS,
                                   N_("The TAP file handle %RTfile is not valid!"), pData->FileDevice);

    /*
     * Make sure the descriptor is non-blocking and valid.
     *
     * We should actually query if it's a TAP device, but I haven't
     * found any way to do that.
     */
    if (fcntl(pData->FileDevice, F_SETFL, O_NONBLOCK) == -1)
        return PDMDrvHlpVMSetError(pDrvIns, VERR_HOSTIF_IOCTL, RT_SRC_POS,
                                   N_("Configuration error: Failed to configure /dev/net/tun. errno=%d"), errno);
    /** @todo determine device name. This can be done by reading the link /proc/<pid>/fd/<fd> */
    Log(("drvTAPContruct: %d (from fd)\n", pData->FileDevice));
    rc = VINF_SUCCESS;

#ifdef ASYNC_NET
    /*
     * Create the control pipe.
     */
    int fds[2];
#ifdef RT_OS_L4
    /* XXX We need to tell the library which interface we are using */
    fds[0] = vboxrtLinuxFd2VBoxFd(VBOXRT_FT_TAP, 0);
#endif
    if (pipe(&fds[0]) != 0) /** @todo RTPipeCreate() or something... */
    {
        int rc = RTErrConvertFromErrno(errno);
        AssertRC(rc);
        return rc;
    }
    pData->PipeRead = fds[0];
    pData->PipeWrite = fds[1];

    /*
     * Create the async I/O thread.
     */
    rc = RTThreadCreate(&pData->Thread, drvTAPAsyncIoThread, pData, 128*_1K, RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "TAP");
    AssertRCReturn(rc, rc);
#else
    /*
     * Register poller
     */
    rc = pDrvIns->pDrvHlp->pfnPDMPollerRegister(pDrvIns, drvTAPPoller);
    AssertRCReturn(rc, rc);
#endif

#ifdef VBOX_WITH_STATISTICS
    /*
     * Statistics.
     */
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pData->StatPktSent,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,        "Number of sent packets.",          "/Drivers/TAP%d/Packets/Sent", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pData->StatPktSentBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,             "Number of sent bytes.",            "/Drivers/TAP%d/Bytes/Sent", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pData->StatPktRecv,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,        "Number of received packets.",      "/Drivers/TAP%d/Packets/Received", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pData->StatPktRecvBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,             "Number of received bytes.",        "/Drivers/TAP%d/Bytes/Received", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pData->StatTransmit,      STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling packet transmit runs.",  "/Drivers/TAP%d/Transmit", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pData->StatReceive,       STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling packet receive runs.",   "/Drivers/TAP%d/Receive", pDrvIns->iInstance);
# ifdef ASYNC_NET
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pData->StatRecvOverflows, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_OCCURENCE, "Profiling packet receive overflows.", "/Drivers/TAP%d/RecvOverflows", pDrvIns->iInstance);
# endif
#endif /* VBOX_WITH_STATISTICS */

    return rc;
}


/**
 * TAP network transport driver registration record.
 */
const PDMDRVREG g_DrvHostInterface =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szDriverName */
    "HostInterface",
    /* pszDescription */
    "TAP Network Transport Driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_NETWORK,
    /* cMaxInstances */
    ~0,
    /* cbInstance */
    sizeof(DRVTAP),
    /* pfnConstruct */
    drvTAPConstruct,
    /* pfnDestruct */
    drvTAPDestruct,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL, /** @todo Do power on, suspend and resume handlers! */
    /* pfnResume */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL
};
