/* $Id$ */
/** @file
 * DrvTAP - Universial TAP network transport driver.
 */

/*
 * Copyright (C) 2006-2010 Sun Microsystems, Inc.
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
#define LOG_GROUP LOG_GROUP_DRV_TUN
#include <VBox/log.h>
#include <VBox/pdmdrv.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>
#ifdef RT_OS_SOLARIS
# include <iprt/process.h>
# include <iprt/env.h>
# ifdef VBOX_WITH_CROSSBOW
#  include <iprt/mem.h>
# endif
#endif

#include <sys/ioctl.h>
#include <sys/poll.h>
#ifdef RT_OS_SOLARIS
# include <sys/stat.h>
# include <sys/ethernet.h>
# include <sys/sockio.h>
# include <netinet/in.h>
# include <netinet/in_systm.h>
# include <netinet/ip.h>
# include <netinet/ip_icmp.h>
# include <netinet/udp.h>
# include <netinet/tcp.h>
# include <net/if.h>
# include <stropts.h>
# include <fcntl.h>
# include <stdlib.h>
# include <stdio.h>
# ifdef VBOX_WITH_CROSSBOW
#  include "solaris/vbox-libdlpi.h"
# endif
#else
# include <sys/fcntl.h>
#endif
#include <errno.h>
#include <unistd.h>

#ifdef RT_OS_L4
# include <l4/vboxserver/file.h>
#endif

#include "Builtins.h"


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * TAP driver instance data.
 *
 * @implements PDMINETWORKCONNECTOR
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
    /** The configured TAP device name. */
    char                   *pszDeviceName;
#ifdef RT_OS_SOLARIS
# ifdef VBOX_WITH_CROSSBOW
    /** Crossbow: MAC address of the device. */
    RTMAC                   MacAddress;
    /** Crossbow: Handle of the NIC. */
    dlpi_handle_t           pDeviceHandle;
# else
    /** IP device file handle (/dev/udp). */
    RTFILE                  IPFileDevice;
# endif
    /** Whether device name is obtained from setup application. */
    bool                    fStatic;
#endif
    /** TAP setup application. */
    char                   *pszSetupApplication;
    /** TAP terminate application. */
    char                   *pszTerminateApplication;
    /** The write end of the control pipe. */
    RTFILE                  PipeWrite;
    /** The read end of the control pipe. */
    RTFILE                  PipeRead;
    /** Reader thread. */
    PPDMTHREAD              pThread;

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


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
#ifdef RT_OS_SOLARIS
# ifdef VBOX_WITH_CROSSBOW
static int              SolarisOpenVNIC(PDRVTAP pThis);
static int              SolarisDLPIErr2VBoxErr(int rc);
# else
static int              SolarisTAPAttach(PDRVTAP pThis);
# endif
#endif


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
    PDRVTAP pThis = PDMINETWORKCONNECTOR_2_DRVTAP(pInterface);
    STAM_COUNTER_INC(&pThis->StatPktSent);
    STAM_COUNTER_ADD(&pThis->StatPktSentBytes, cb);
    STAM_PROFILE_START(&pThis->StatTransmit, a);

#ifdef LOG_ENABLED
    uint64_t u64Now = RTTimeProgramNanoTS();
    LogFlow(("drvTAPSend: %-4d bytes at %llu ns  deltas: r=%llu t=%llu\n",
             cb, u64Now, u64Now - pThis->u64LastReceiveTS, u64Now - pThis->u64LastTransferTS));
    pThis->u64LastTransferTS = u64Now;
#endif
    Log2(("drvTAPSend: pvBuf=%p cb=%#x\n"
          "%.*Rhxd\n",
          pvBuf, cb, cb, pvBuf));

    int rc = RTFileWrite(pThis->FileDevice, pvBuf, cb, NULL);

    STAM_PROFILE_STOP(&pThis->StatTransmit, a);
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
 * Asynchronous I/O thread for handling receive.
 *
 * @returns VINF_SUCCESS (ignored).
 * @param   Thread          Thread handle.
 * @param   pvUser          Pointer to a DRVTAP structure.
 */
static DECLCALLBACK(int) drvTAPAsyncIoThread(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVTAP pThis = PDMINS_2_DATA(pDrvIns, PDRVTAP);
    LogFlow(("drvTAPAsyncIoThread: pThis=%p\n", pThis));

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    STAM_PROFILE_ADV_START(&pThis->StatReceive, a);

    /*
     * Polling loop.
     */
    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        /*
         * Wait for something to become available.
         */
        struct pollfd aFDs[2];
        aFDs[0].fd      = pThis->FileDevice;
        aFDs[0].events  = POLLIN | POLLPRI;
        aFDs[0].revents = 0;
        aFDs[1].fd      = pThis->PipeRead;
        aFDs[1].events  = POLLIN | POLLPRI | POLLERR | POLLHUP;
        aFDs[1].revents = 0;
        STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
        errno=0;
        int rc = poll(&aFDs[0], RT_ELEMENTS(aFDs), -1 /* infinite */);

        /* this might have changed in the meantime */
        if (pThread->enmState != PDMTHREADSTATE_RUNNING)
            break;

        STAM_PROFILE_ADV_START(&pThis->StatReceive, a);
        if (    rc > 0
            &&  (aFDs[0].revents & (POLLIN | POLLPRI))
            &&  !aFDs[1].revents)
        {
            /*
             * Read the frame.
             */
            char achBuf[16384];
            size_t cbRead = 0;
#ifdef VBOX_WITH_CROSSBOW
            cbRead = sizeof(achBuf);
            rc = g_pfnLibDlpiRecv(pThis->pDeviceHandle, NULL, NULL, achBuf, &cbRead, -1, NULL);
            rc = RT_LIKELY(rc == DLPI_SUCCESS) ? VINF_SUCCESS : SolarisDLPIErr2VBoxErr(rc);
#else
            /** @note At least on Linux we will never receive more than one network packet
             *        after poll() returned successfully. I don't know why but a second
             *        RTFileRead() operation will return with VERR_TRY_AGAIN in any case. */
            rc = RTFileRead(pThis->FileDevice, achBuf, sizeof(achBuf), &cbRead);
#endif
            if (RT_SUCCESS(rc))
            {
                /*
                 * Wait for the device to have space for this frame.
                 * Most guests use frame-sized receive buffers, hence non-zero cbMax
                 * automatically means there is enough room for entire frame. Some
                 * guests (eg. Solaris) use large chains of small receive buffers
                 * (each 128 or so bytes large). We will still start receiving as soon
                 * as cbMax is non-zero because:
                 *  - it would be quite expensive for pfnCanReceive to accurately
                 *    determine free receive buffer space
                 *  - if we were waiting for enough free buffers, there is a risk
                 *    of deadlocking because the guest could be waiting for a receive
                 *    overflow error to allocate more receive buffers
                 */
                STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
                int rc1 = pThis->pPort->pfnWaitReceiveAvail(pThis->pPort, RT_INDEFINITE_WAIT);
                STAM_PROFILE_ADV_START(&pThis->StatReceive, a);

                /*
                 * A return code != VINF_SUCCESS means that we were woken up during a VM
                 * state transistion. Drop the packet and wait for the next one.
                 */
                if (RT_FAILURE(rc1))
                    continue;

                /*
                 * Pass the data up.
                 */
#ifdef LOG_ENABLED
                uint64_t u64Now = RTTimeProgramNanoTS();
                LogFlow(("drvTAPAsyncIoThread: %-4d bytes at %llu ns  deltas: r=%llu t=%llu\n",
                         cbRead, u64Now, u64Now - pThis->u64LastReceiveTS, u64Now - pThis->u64LastTransferTS));
                pThis->u64LastReceiveTS = u64Now;
#endif
                Log2(("drvTAPAsyncIoThread: cbRead=%#x\n" "%.*Rhxd\n", cbRead, cbRead, achBuf));
                STAM_COUNTER_INC(&pThis->StatPktRecv);
                STAM_COUNTER_ADD(&pThis->StatPktRecvBytes, cbRead);
                rc1 = pThis->pPort->pfnReceive(pThis->pPort, achBuf, cbRead);
                AssertRC(rc1);
            }
            else
            {
                LogFlow(("drvTAPAsyncIoThread: RTFileRead -> %Rrc\n", rc));
                if (rc == VERR_INVALID_HANDLE)
                    break;
                RTThreadYield();
            }
        }
        else if (   rc > 0
                 && aFDs[1].revents)
        {
            LogFlow(("drvTAPAsyncIoThread: Control message: enmState=%d revents=%#x\n", pThread->enmState, aFDs[1].revents));
            if (aFDs[1].revents & (POLLHUP | POLLERR | POLLNVAL))
                break;

            /* drain the pipe */
            char ch;
            size_t cbRead;
            RTFileRead(pThis->PipeRead, &ch, 1, &cbRead);
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


    LogFlow(("drvTAPAsyncIoThread: returns %Rrc\n", VINF_SUCCESS));
    STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
    return VINF_SUCCESS;
}


/**
 * Unblock the send thread so it can respond to a state change.
 *
 * @returns VBox status code.
 * @param   pDevIns     The pcnet device instance.
 * @param   pThread     The send thread.
 */
static DECLCALLBACK(int) drvTapAsyncIoWakeup(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVTAP pThis = PDMINS_2_DATA(pDrvIns, PDRVTAP);

    int rc = RTFileWrite(pThis->PipeWrite, "", 1, NULL);
    AssertRC(rc);

    return VINF_SUCCESS;
}


#if defined(RT_OS_SOLARIS)
/**
 * Calls OS-specific TAP setup application/script.
 *
 * @returns VBox error code.
 * @param   pThis           The instance data.
 */
static int drvTAPSetupApplication(PDRVTAP pThis)
{
    char szCommand[4096];

#ifdef VBOX_WITH_CROSSBOW
    /* Convert MAC address bytes to string (required by Solaris' dladm). */
    char *pszHex = "0123456789abcdef";
    uint8_t *pMacAddr8 = pThis->MacAddress.au8;
    char szMacAddress[3 * sizeof(RTMAC)];
    for (unsigned int i = 0; i < sizeof(RTMAC); i++)
    {
        szMacAddress[3 * i] = pszHex[((*pMacAddr8 >> 4) & 0x0f)];
        szMacAddress[3 * i + 1] = pszHex[(*pMacAddr8 & 0x0f)];
        szMacAddress[3 * i + 2] = ':';
        *pMacAddr8++;
    }
    szMacAddress[sizeof(szMacAddress) - 1] =  0;

    RTStrPrintf(szCommand, sizeof(szCommand), "%s %s %s", pThis->pszSetupApplication,
            szMacAddress, pThis->fStatic ? pThis->pszDeviceName : "");
#else
    RTStrPrintf(szCommand, sizeof(szCommand), "%s %s", pThis->pszSetupApplication,
            pThis->fStatic ? pThis->pszDeviceName : "");
#endif

    /* Pipe open the setup application. */
    Log2(("Starting TAP setup application: %s\n", szCommand));
    FILE* pfSetupHandle = popen(szCommand, "r");
    if (pfSetupHandle == 0)
    {
        LogRel(("TAP#%d: Failed to run TAP setup application: %s\n", pThis->pDrvIns->iInstance,
              pThis->pszSetupApplication, strerror(errno)));
        return VERR_HOSTIF_INIT_FAILED;
    }
    if (!pThis->fStatic)
    {
        /* Obtain device name from setup application. */
        char acBuffer[64];
        size_t cBufSize;
        fgets(acBuffer, sizeof(acBuffer), pfSetupHandle);
        cBufSize = strlen(acBuffer);
        /* The script must return the name of the interface followed by a carriage return as the
          first line of its output.  We need a null-terminated string. */
        if ((cBufSize < 2) || (acBuffer[cBufSize - 1] != '\n'))
        {
            pclose(pfSetupHandle);
            LogRel(("The TAP interface setup script did not return the name of a TAP device.\n"));
            return VERR_HOSTIF_INIT_FAILED;
        }
        /* Overwrite the terminating newline character. */
        acBuffer[cBufSize - 1] = 0;
        RTStrAPrintf(&pThis->pszDeviceName, "%s", acBuffer);
    }
    int rc = pclose(pfSetupHandle);
    if (!WIFEXITED(rc))
    {
        LogRel(("The TAP interface setup script terminated abnormally.\n"));
        return VERR_HOSTIF_INIT_FAILED;
    }
    if (WEXITSTATUS(rc) != 0)
    {
        LogRel(("The TAP interface setup script returned a non-zero exit code.\n"));
        return VERR_HOSTIF_INIT_FAILED;
    }
    return VINF_SUCCESS;
}


/**
 * Calls OS-specific TAP terminate application/script.
 *
 * @returns VBox error code.
 * @param   pThis           The instance data.
 */
static int drvTAPTerminateApplication(PDRVTAP pThis)
{
    char *pszArgs[3];
    pszArgs[0] = pThis->pszTerminateApplication;
    pszArgs[1] = pThis->pszDeviceName;
    pszArgs[2] = NULL;

    Log2(("Starting TAP terminate application: %s %s\n", pThis->pszTerminateApplication, pThis->pszDeviceName));
    RTPROCESS pid = NIL_RTPROCESS;
    int rc = RTProcCreate(pszArgs[0], pszArgs, RTENV_DEFAULT, 0, &pid);
    if (RT_SUCCESS(rc))
    {
        RTPROCSTATUS Status;
        rc = RTProcWait(pid, 0, &Status);
        if (RT_SUCCESS(rc))
        {
            if (    Status.iStatus == 0
                &&  Status.enmReason == RTPROCEXITREASON_NORMAL)
                return VINF_SUCCESS;

            LogRel(("TAP#%d: Error running TAP terminate application: %s\n", pThis->pDrvIns->iInstance, pThis->pszTerminateApplication));
        }
        else
            LogRel(("TAP#%d: RTProcWait failed for: %s\n", pThis->pDrvIns->iInstance, pThis->pszTerminateApplication));
    }
    else
    {
        /* Bad. RTProcCreate() failed! */
        LogRel(("TAP#%d: Failed to fork() process for running TAP terminate application: %s\n", pThis->pDrvIns->iInstance,
              pThis->pszTerminateApplication, strerror(errno)));
    }
    return VERR_HOSTIF_TERM_FAILED;
}

#endif /* RT_OS_SOLARIS */


#ifdef RT_OS_SOLARIS
# ifdef VBOX_WITH_CROSSBOW
/**
 * Crossbow: Open & configure the virtual NIC.
 *
 * @returns VBox error code.
 * @param   pThis           The instance data.
 */
static int SolarisOpenVNIC(PDRVTAP pThis)
{
    /*
     * Open & bind the NIC using the datalink provider routine.
     */
    int rc = g_pfnLibDlpiOpen(pThis->pszDeviceName, &pThis->pDeviceHandle, DLPI_RAW);
    if (rc != DLPI_SUCCESS)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                           N_("Failed to open VNIC \"%s\" in raw mode"), pThis->pszDeviceName);

    dlpi_info_t vnicInfo;
    rc = g_pfnLibDlpiInfo(pThis->pDeviceHandle, &vnicInfo, 0);
    if (rc == DLPI_SUCCESS)
    {
        if (vnicInfo.di_mactype == DL_ETHER)
        {
            rc = g_pfnLibDlpiBind(pThis->pDeviceHandle, DLPI_ANY_SAP, NULL);
            if (rc == DLPI_SUCCESS)
            {
                rc = g_pfnLibDlpiSetPhysAddr(pThis->pDeviceHandle, DL_CURR_PHYS_ADDR, &pThis->MacAddress, ETHERADDRL);
                if (rc == DLPI_SUCCESS)
                {
                    rc = g_pfnLibDlpiPromiscon(pThis->pDeviceHandle, DL_PROMISC_SAP);
                    if (rc == DLPI_SUCCESS)
                    {
                        /* Need to use DL_PROMIS_PHYS (not multicast) as we cannot be sure what the guest needs. */
                        rc = g_pfnLibDlpiPromiscon(pThis->pDeviceHandle, DL_PROMISC_PHYS);
                        if (rc == DLPI_SUCCESS)
                        {
                            pThis->FileDevice = g_pfnLibDlpiFd(pThis->pDeviceHandle);
                            if (pThis->FileDevice >= 0)
                            {
                                Log(("SolarisOpenVNIC: %s -> %d\n", pThis->pszDeviceName, pThis->FileDevice));
                                return VINF_SUCCESS;
                            }

                            rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                                     N_("Failed to obtain file descriptor for VNIC"));
                        }
                        else
                            rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                                     N_("Failed to set appropriate promiscous mode"));
                    }
                    else
                        rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                                 N_("Failed to activate promiscous mode for VNIC"));
                }
                else
                    rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                             N_("Failed to set physical address for VNIC"));
            }
            else
                rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                         N_("Failed to bind VNIC"));
        }
        else
            rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                         N_("VNIC type is not ethernet"));
    }
    else
        rc = PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                         N_("Failed to obtain VNIC info"));
    g_pfnLibDlpiClose(pThis->pDeviceHandle);
    return rc;
}


/**
 * Crossbow: Converts a Solaris DLPI error code to a VBox error code.
 *
 * @returns corresponding VBox error code.
 * @param   rc  DLPI error code (DLPI_* defines).
 */
static int SolarisDLPIErr2VBoxErr(int rc)
{
    switch (rc)
    {
        case DLPI_SUCCESS:          return VINF_SUCCESS;
        case DLPI_EINVAL:           return VERR_INVALID_PARAMETER;
        case DLPI_ELINKNAMEINVAL:   return VERR_INVALID_NAME;
        case DLPI_EINHANDLE:        return VERR_INVALID_HANDLE;
        case DLPI_ETIMEDOUT:        return VERR_TIMEOUT;
        case DLPI_FAILURE:          return VERR_GENERAL_FAILURE;

        case DLPI_EVERNOTSUP:
        case DLPI_EMODENOTSUP:
        case DLPI_ERAWNOTSUP:
        /* case DLPI_ENOTENOTSUP: */
        case DLPI_EUNAVAILSAP:      return VERR_NOT_SUPPORTED;

        /*  Define VBox error codes for these, if really needed. */
        case DLPI_ENOLINK:
        case DLPI_EBADLINK:
        /* case DLPI_ENOTEIDINVAL: */
        case DLPI_EBADMSG:
        case DLPI_ENOTSTYLE2:       return VERR_GENERAL_FAILURE;
    }

    AssertMsgFailed(("SolarisDLPIErr2VBoxErr: Unhandled error %d\n", rc));
    return VERR_UNRESOLVED_ERROR;
}

# else  /* VBOX_WITH_CROSSBOW */

/** From net/if_tun.h, installed by Universal TUN/TAP driver */
# define TUNNEWPPA                   (('T'<<16) | 0x0001)
/** Whether to enable ARP for TAP. */
# define VBOX_SOLARIS_TAP_ARP        1

/**
 * Creates/Attaches TAP device to IP.
 *
 * @returns VBox error code.
 * @param   pThis            The instance data.
 */
static DECLCALLBACK(int) SolarisTAPAttach(PDRVTAP pThis)
{
    LogFlow(("SolarisTapAttach: pThis=%p\n", pThis));


    int IPFileDes = open("/dev/udp", O_RDWR, 0);
    if (IPFileDes < 0)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to open /dev/udp. errno=%d"), errno);

    int TapFileDes = open("/dev/tap", O_RDWR, 0);
    if (TapFileDes < 0)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to open /dev/tap for TAP. errno=%d"), errno);

    /* Use the PPA from the ifname if possible (e.g "tap2", then use 2 as PPA) */
    int iPPA = -1;
    if (pThis->pszDeviceName)
    {
        size_t cch = strlen(pThis->pszDeviceName);
        if (cch > 1 && RT_C_IS_DIGIT(pThis->pszDeviceName[cch - 1]) != 0)
            iPPA = pThis->pszDeviceName[cch - 1] - '0';
    }

    struct strioctl ioIF;
    ioIF.ic_cmd = TUNNEWPPA;
    ioIF.ic_len = sizeof(iPPA);
    ioIF.ic_dp = (char *)(&iPPA);
    ioIF.ic_timout = 0;
    iPPA = ioctl(TapFileDes, I_STR, &ioIF);
    if (iPPA < 0)
    {
        close(TapFileDes);
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_IOCTL, RT_SRC_POS,
                                   N_("Failed to get new interface. errno=%d"), errno);
    }

    int InterfaceFD = open("/dev/tap", O_RDWR, 0);
    if (!InterfaceFD)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to open interface /dev/tap. errno=%d"), errno);

    if (ioctl(InterfaceFD, I_PUSH, "ip") == -1)
    {
        close(InterfaceFD);
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_IOCTL, RT_SRC_POS,
                                   N_("Failed to push IP. errno=%d"), errno);
    }

    struct lifreq ifReq;
    memset(&ifReq, 0, sizeof(ifReq));
    if (ioctl(InterfaceFD, SIOCGLIFFLAGS, &ifReq) == -1)
        LogRel(("TAP#%d: Failed to get interface flags.\n", pThis->pDrvIns->iInstance));

    ifReq.lifr_ppa = iPPA;
    RTStrPrintf (ifReq.lifr_name, sizeof(ifReq.lifr_name), pThis->pszDeviceName);

    if (ioctl(InterfaceFD, SIOCSLIFNAME, &ifReq) == -1)
        LogRel(("TAP#%d: Failed to set PPA. errno=%d\n", pThis->pDrvIns->iInstance, errno));

    if (ioctl(InterfaceFD, SIOCGLIFFLAGS, &ifReq) == -1)
        LogRel(("TAP#%d: Failed to get interface flags after setting PPA. errno=%d\n", pThis->pDrvIns->iInstance, errno));

#ifdef VBOX_SOLARIS_TAP_ARP
    /* Interface */
    if (ioctl(InterfaceFD, I_PUSH, "arp") == -1)
        LogRel(("TAP#%d: Failed to push ARP to Interface FD. errno=%d\n", pThis->pDrvIns->iInstance, errno));

    /* IP */
    if (ioctl(IPFileDes, I_POP, NULL) == -1)
        LogRel(("TAP#%d: Failed I_POP from IP FD. errno=%d\n", pThis->pDrvIns->iInstance, errno));

    if (ioctl(IPFileDes, I_PUSH, "arp") == -1)
        LogRel(("TAP#%d: Failed to push ARP to IP FD. errno=%d\n", pThis->pDrvIns->iInstance, errno));

    /* ARP */
    int ARPFileDes = open("/dev/tap", O_RDWR, 0);
    if (ARPFileDes < 0)
        LogRel(("TAP#%d: Failed to open for /dev/tap for ARP. errno=%d", pThis->pDrvIns->iInstance, errno));

    if (ioctl(ARPFileDes, I_PUSH, "arp") == -1)
        LogRel(("TAP#%d: Failed to push ARP to ARP FD. errno=%d\n", pThis->pDrvIns->iInstance, errno));

    ioIF.ic_cmd = SIOCSLIFNAME;
    ioIF.ic_timout = 0;
    ioIF.ic_len = sizeof(ifReq);
    ioIF.ic_dp = (char *)&ifReq;
    if (ioctl(ARPFileDes, I_STR, &ioIF) == -1)
        LogRel(("TAP#%d: Failed to set interface name to ARP.\n", pThis->pDrvIns->iInstance));
#endif

    /* We must use I_LINK and not I_PLINK as I_PLINK makes the link persistent.
     * Then we would not be able unlink the interface if we reuse it.
     * Even 'unplumb' won't work after that.
     */
    int IPMuxID = ioctl(IPFileDes, I_LINK, InterfaceFD);
    if (IPMuxID == -1)
    {
        close(InterfaceFD);
#ifdef VBOX_SOLARIS_TAP_ARP
        close(ARPFileDes);
#endif
        LogRel(("TAP#%d: Cannot link TAP device to IP.\n", pThis->pDrvIns->iInstance));
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_IOCTL, RT_SRC_POS,
                    N_("Failed to link TAP device to IP. Check TAP interface name. errno=%d"), errno);
    }

#ifdef VBOX_SOLARIS_TAP_ARP
    int ARPMuxID = ioctl(IPFileDes, I_LINK, ARPFileDes);
    if (ARPMuxID == -1)
        LogRel(("TAP#%d: Failed to link TAP device to ARP\n", pThis->pDrvIns->iInstance));

    close(ARPFileDes);
#endif
    close(InterfaceFD);

    /* Reuse ifReq */
    memset(&ifReq, 0, sizeof(ifReq));
    RTStrPrintf (ifReq.lifr_name, sizeof(ifReq.lifr_name), pThis->pszDeviceName);
    ifReq.lifr_ip_muxid  = IPMuxID;
#ifdef VBOX_SOLARIS_TAP_ARP
    ifReq.lifr_arp_muxid = ARPMuxID;
#endif

    if (ioctl(IPFileDes, SIOCSLIFMUXID, &ifReq) == -1)
    {
#ifdef VBOX_SOLARIS_TAP_ARP
        ioctl(IPFileDes, I_PUNLINK, ARPMuxID);
#endif
        ioctl(IPFileDes, I_PUNLINK, IPMuxID);
        close(IPFileDes);
        LogRel(("TAP#%d: Failed to set Mux ID.\n", pThis->pDrvIns->iInstance));
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_IOCTL, RT_SRC_POS,
                                   N_("Failed to set Mux ID. Check TAP interface name. errno=%d"), errno);
    }

    pThis->FileDevice = (RTFILE)TapFileDes;
    pThis->IPFileDevice = (RTFILE)IPFileDes;

    return VINF_SUCCESS;
}

# endif /* VBOX_WITH_CROSSBOW */
#endif  /* RT_OS_SOLARIS */

/* -=-=-=-=- PDMIBASE -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvTAPQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS  pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVTAP     pThis   = PDMINS_2_DATA(pDrvIns, PDRVTAP);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKCONNECTOR, &pThis->INetworkConnector);
    return NULL;
}

/* -=-=-=-=- PDMDRVREG -=-=-=-=- */

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
    PDRVTAP pThis = PDMINS_2_DATA(pDrvIns, PDRVTAP);
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    /*
     * Terminate the control pipe.
     */
    if (pThis->PipeWrite != NIL_RTFILE)
    {
        int rc = RTFileClose(pThis->PipeWrite);
        AssertRC(rc);
        pThis->PipeWrite = NIL_RTFILE;
    }
    if (pThis->PipeRead != NIL_RTFILE)
    {
        int rc = RTFileClose(pThis->PipeRead);
        AssertRC(rc);
        pThis->PipeRead = NIL_RTFILE;
    }

#ifdef RT_OS_SOLARIS
    /** @todo r=bird: This *does* need checking against ConsoleImpl2.cpp if used on non-solaris systems. */
    if (pThis->FileDevice != NIL_RTFILE)
    {
        int rc = RTFileClose(pThis->FileDevice);
        AssertRC(rc);
        pThis->FileDevice = NIL_RTFILE;
    }

# ifndef VBOX_WITH_CROSSBOW
    if (pThis->IPFileDevice != NIL_RTFILE)
    {
        int rc = RTFileClose(pThis->IPFileDevice);
        AssertRC(rc);
        pThis->IPFileDevice = NIL_RTFILE;
    }
# endif

    /*
     * Call TerminateApplication after closing the device otherwise
     * TerminateApplication would not be able to unplumb it.
     */
    if (pThis->pszTerminateApplication)
        drvTAPTerminateApplication(pThis);

#endif  /* RT_OS_SOLARIS */

#ifdef RT_OS_SOLARIS
    if (!pThis->fStatic)
        RTStrFree(pThis->pszDeviceName);    /* allocated by drvTAPSetupApplication */
    else
        MMR3HeapFree(pThis->pszDeviceName);
#else
    MMR3HeapFree(pThis->pszDeviceName);
#endif
    MMR3HeapFree(pThis->pszSetupApplication);
    MMR3HeapFree(pThis->pszTerminateApplication);

#ifdef VBOX_WITH_STATISTICS
    /*
     * Deregister statistics.
     */
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatPktSent);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatPktSentBytes);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatPktRecv);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatPktRecvBytes);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatTransmit);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReceive);
#endif /* VBOX_WITH_STATISTICS */
}


/**
 * Construct a TAP network transport driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvTAPConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfgHandle, uint32_t fFlags)
{
    PDRVTAP pThis = PDMINS_2_DATA(pDrvIns, PDRVTAP);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                      = pDrvIns;
    pThis->FileDevice                   = NIL_RTFILE;
    pThis->pszDeviceName                = NULL;
#ifdef RT_OS_SOLARIS
# ifdef VBOX_WITH_CROSSBOW
    pThis->pDeviceHandle                = NULL;
# else
    pThis->IPFileDevice                 = NIL_RTFILE;
# endif
    pThis->fStatic                      = true;
#endif
    pThis->pszSetupApplication          = NULL;
    pThis->pszTerminateApplication      = NULL;

    /* IBase */
    pDrvIns->IBase.pfnQueryInterface    = drvTAPQueryInterface;
    /* INetwork */
    pThis->INetworkConnector.pfnSend                = drvTAPSend;
    pThis->INetworkConnector.pfnSetPromiscuousMode  = drvTAPSetPromiscuousMode;
    pThis->INetworkConnector.pfnNotifyLinkChanged   = drvTAPNotifyLinkChanged;

    /*
     * Validate the config.
     */
    if (!CFGMR3AreValuesValid(pCfgHandle, "Device\0InitProg\0TermProg\0FileHandle\0TAPSetupApplication\0TAPTerminateApplication\0MAC"))
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES, "");

    /*
     * Check that no-one is attached to us.
     */
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * Query the network port interface.
     */
    pThis->pPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMINETWORKPORT);
    if (!pThis->pPort)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_MISSING_INTERFACE_ABOVE,
                                N_("Configuration error: The above device/driver didn't export the network port interface"));

    /*
     * Read the configuration.
     */
    int rc;
#if defined(RT_OS_SOLARIS)   /** @todo Other platforms' TAP code should be moved here from ConsoleImpl & VBoxBFE. */
    rc = CFGMR3QueryStringAlloc(pCfgHandle, "TAPSetupApplication", &pThis->pszSetupApplication);
    if (RT_SUCCESS(rc))
    {
        if (!RTPathExists(pThis->pszSetupApplication))
            return PDMDrvHlpVMSetError(pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                       N_("Invalid TAP setup program path: %s"), pThis->pszSetupApplication);
    }
    else if (rc != VERR_CFGM_VALUE_NOT_FOUND)
        return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Configuration error: failed to query \"TAPTerminateApplication\""));

    rc = CFGMR3QueryStringAlloc(pCfgHandle, "TAPTerminateApplication", &pThis->pszTerminateApplication);
    if (RT_SUCCESS(rc))
    {
        if (!RTPathExists(pThis->pszTerminateApplication))
            return PDMDrvHlpVMSetError(pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                       N_("Invalid TAP terminate program path: %s"), pThis->pszTerminateApplication);
    }
    else if (rc != VERR_CFGM_VALUE_NOT_FOUND)
        return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Configuration error: failed to query \"TAPTerminateApplication\""));

# ifdef VBOX_WITH_CROSSBOW
    rc = CFGMR3QueryBytes(pCfgHandle, "MAC", &pThis->MacAddress, sizeof(pThis->MacAddress));
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Configuration error: Failed to query \"MAC\""));
# endif

    rc = CFGMR3QueryStringAlloc(pCfgHandle, "Device", &pThis->pszDeviceName);
    if (RT_FAILURE(rc))
        pThis->fStatic = false;

    /* Obtain the device name from the setup application (if none was specified). */
    if (pThis->pszSetupApplication)
    {
        rc = drvTAPSetupApplication(pThis);
        if (RT_FAILURE(rc))
            return PDMDrvHlpVMSetError(pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                       N_("Error running TAP setup application. rc=%d"), rc);
    }

    /*
     * Do the setup.
     */
# ifdef VBOX_WITH_CROSSBOW
    if (!VBoxLibDlpiFound())
    {
        return PDMDrvHlpVMSetError(pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                       N_("Failed to load library %s required for host interface networking."), LIB_DLPI);
    }
    rc = SolarisOpenVNIC(pThis);
# else
    rc = SolarisTAPAttach(pThis);
# endif
    if (RT_FAILURE(rc))
        return rc;

#else /* !RT_OS_SOLARIS */

    int32_t iFile;
    rc = CFGMR3QueryS32(pCfgHandle, "FileHandle", &iFile);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Query for \"FileHandle\" 32-bit signed integer failed"));
    pThis->FileDevice = (RTFILE)iFile;
    if (!RTFileIsValid(pThis->FileDevice))
        return PDMDrvHlpVMSetError(pDrvIns, VERR_INVALID_HANDLE, RT_SRC_POS,
                                   N_("The TAP file handle %RTfile is not valid"), pThis->FileDevice);
#endif /* !RT_OS_SOLARIS */

    /*
     * Make sure the descriptor is non-blocking and valid.
     *
     * We should actually query if it's a TAP device, but I haven't
     * found any way to do that.
     */
    if (fcntl(pThis->FileDevice, F_SETFL, O_NONBLOCK) == -1)
        return PDMDrvHlpVMSetError(pDrvIns, VERR_HOSTIF_IOCTL, RT_SRC_POS,
                                   N_("Configuration error: Failed to configure /dev/net/tun. errno=%d"), errno);
    /** @todo determine device name. This can be done by reading the link /proc/<pid>/fd/<fd> */
    Log(("drvTAPContruct: %d (from fd)\n", pThis->FileDevice));
    rc = VINF_SUCCESS;

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
        rc = RTErrConvertFromErrno(errno);
        AssertRC(rc);
        return rc;
    }
    pThis->PipeRead = fds[0];
    pThis->PipeWrite = fds[1];

    /*
     * Create the async I/O thread.
     */
    rc = PDMDrvHlpPDMThreadCreate(pDrvIns, &pThis->pThread, pThis, drvTAPAsyncIoThread, drvTapAsyncIoWakeup, 128 * _1K, RTTHREADTYPE_IO, "TAP");
    AssertRCReturn(rc, rc);

#ifdef VBOX_WITH_STATISTICS
    /*
     * Statistics.
     */
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktSent,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,        "Number of sent packets.",          "/Drivers/TAP%d/Packets/Sent", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktSentBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,             "Number of sent bytes.",            "/Drivers/TAP%d/Bytes/Sent", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktRecv,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,        "Number of received packets.",      "/Drivers/TAP%d/Packets/Received", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktRecvBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,             "Number of received bytes.",        "/Drivers/TAP%d/Bytes/Received", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatTransmit,      STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling packet transmit runs.",  "/Drivers/TAP%d/Transmit", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReceive,       STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling packet receive runs.",   "/Drivers/TAP%d/Receive", pDrvIns->iInstance);
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
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
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
    /* pfnRelocate */
    NULL,
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

