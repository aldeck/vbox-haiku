/** @file
 *
 * VBox stream devices:
 * Named pipe stream
 */

/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_NAMEDPIPE
#include <VBox/pdm.h>
#include <VBox/cfgm.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <VBox/mm.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/stream.h>
#include <iprt/alloc.h>
#include <iprt/string.h>

#include "Builtins.h"

#ifdef __WIN__
#include <windows.h>
#else /* !__WIN__ */
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif /* !__WIN__ */

/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/

/** Converts a pointer to DRVNAMEDPIPE::IMedia to a PDRVNAMEDPIPE. */
#define PDMISTREAM_2_DRVNAMEDPIPE(pInterface) ( (PDRVNAMEDPIPE)((uintptr_t)pInterface - RT_OFFSETOF(DRVNAMEDPIPE, IStream)) )

/** Converts a pointer to PDMDRVINS::IBase to a PPDMDRVINS. */
#define PDMIBASE_2_DRVINS(pInterface)   ( (PPDMDRVINS)((uintptr_t)pInterface - RT_OFFSETOF(PDMDRVINS, IBase)) )

/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Named pipe driver instance data.
 */
typedef struct DRVNAMEDPIPE
{
    /** The stream interface. */
    PDMISTREAM          IStream;
    /** Pointer to the driver instance. */
    PPDMDRVINS          pDrvIns;
    /** Pointer to the named pipe file name. (Freed by MM) */
    char                *pszLocation;
    /** Flag whether VirtualBox represents the server or client side. */
    bool                fIsServer;
#ifdef __WIN__
    /* File handle of the named pipe. */
    RTFILE              NamedPipe;
    /* Overlapped structure for writes. */
    OVERLAPPED          OverlappedWrite;
    /* Overlapped structure for reads. */
    OVERLAPPED          OverlappedRead;
#else /* !__WIN__ */
    /** Socket handle of the local socket for server. */
    RTSOCKET            LocalSocketServer;
    /** Socket handle of the local socket. */
    RTSOCKET            LocalSocket;
#endif /* !__WIN__ */
    /** Thread for listening for new connections. */
    RTTHREAD            ListenThread;
    /** Flag to signal listening thread to shut down. */
    bool                fShutdown;
} DRVNAMEDPIPE, *PDRVNAMEDPIPE;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/


/** @copydoc PDMISTREAM::pfnRead */
static DECLCALLBACK(int) drvNamedPipeRead(PPDMISTREAM pInterface, void *pvBuf, size_t *cbRead)
{
    int rc = VINF_SUCCESS;
    PDRVNAMEDPIPE pData = PDMISTREAM_2_DRVNAMEDPIPE(pInterface);
    LogFlow(("%s: pvBuf=%p cbRead=%#x (%s)\n", __FUNCTION__, pvBuf, cbRead, pData->pszLocation));

    Assert(pvBuf);
    /** @todo implement non-blocking I/O */
#ifdef __WIN__
    if (pData->NamedPipe != NIL_RTFILE)
    {
        unsigned cbReallyRead;
        pData->OverlappedRead.Offset     = 0;
        pData->OverlappedRead.OffsetHigh = 0;
        if (!ReadFile((HANDLE)pData->NamedPipe, pvBuf, *cbRead, NULL, &pData->OverlappedRead))
        {
            DWORD uError = GetLastError();

            if (uError == ERROR_PIPE_LISTENING)
            {
                /* nobody connected yet */
                cbReallyRead = 0;

                /* wait a bit or else we'll be called right back. */
                RTThreadSleep(100);
            }
            else
            {
                if (uError == ERROR_IO_PENDING)
                {
                    uError = 0;

                    /* Wait for incoming bytes. */
                    if (GetOverlappedResult((HANDLE)pData->NamedPipe, &pData->OverlappedRead, (DWORD *)&cbReallyRead, TRUE) == FALSE)
                        uError = GetLastError();
                }

                rc = RTErrConvertFromWin32(uError);
                Log(("drvNamedPipeRead: ReadFile returned %d (%Vrc)\n", uError, rc));
            }
        }
        else
            cbReallyRead = *cbRead;

        if (VBOX_FAILURE(rc))
        {
            Log(("drvNamedPipeRead: RTFileRead returned Vrc\n", rc));
            if (rc == VERR_EOF)
            {
                RTFILE tmp = pData->NamedPipe;
                FlushFileBuffers((HANDLE)tmp);
                if (!pData->fIsServer)
                {
                    pData->NamedPipe = NIL_RTFILE;
                    DisconnectNamedPipe((HANDLE)tmp);
                    RTFileClose(tmp);
                }
            }
            cbReallyRead = 0;
        }
        *cbRead = cbReallyRead;
    }
#else /* !__WIN__ */
    if (pData->LocalSocket != NIL_RTSOCKET)
    {
        ssize_t cbReallyRead;
        cbReallyRead = recv(pData->LocalSocket, pvBuf, *cbRead, 0);
        if (cbReallyRead == 0)
        {
            RTSOCKET tmp = pData->LocalSocket;
            pData->LocalSocket = NIL_RTSOCKET;
            close(tmp);
        }
        else if (cbReallyRead == -1)
        {
            cbReallyRead = 0;
            rc = RTErrConvertFromErrno(errno);
        }
        *cbRead = cbReallyRead;
    }
#endif /* !__WIN__ */
    else
    {
        RTThreadSleep(100);
        *cbRead = 0;
    }

    LogFlow(("%s: cbRead=%d returns %Vrc\n", __FUNCTION__, *cbRead, rc));
    return rc;
}


/** @copydoc PDMISTREAM::pfnWrite */
static DECLCALLBACK(int) drvNamedPipeWrite(PPDMISTREAM pInterface, const void *pvBuf, size_t *cbWrite)
{
    int rc = VINF_SUCCESS;
    PDRVNAMEDPIPE pData = PDMISTREAM_2_DRVNAMEDPIPE(pInterface);
    LogFlow(("%s: pvBuf=%p cbWrite=%#x (%s)\n", __FUNCTION__, pvBuf, cbWrite, pData->pszLocation));

    Assert(pvBuf);
    /** @todo implement non-blocking I/O */
#ifdef __WIN__
    if (pData->NamedPipe != NIL_RTFILE)
    {
        unsigned cbWritten;
        pData->OverlappedWrite.Offset     = 0;
        pData->OverlappedWrite.OffsetHigh = 0;
        if (!WriteFile((HANDLE)pData->NamedPipe, pvBuf, *cbWrite, NULL, &pData->OverlappedWrite))
        {
            DWORD uError = GetLastError();

            if (uError == ERROR_PIPE_LISTENING)
            {
                /* No connection yet; just discard the write. */
                cbWritten = *cbWrite;
            }
            else
            if (uError != ERROR_IO_PENDING)
            {
                rc = RTErrConvertFromWin32(uError);
                Log(("drvNamedPipeWrite: WriteFile returned %d (%Vrc)\n", uError, rc));
            }
            else
            {
                /* Wait for the write to complete. */
                if (GetOverlappedResult((HANDLE)pData->NamedPipe, &pData->OverlappedWrite, (DWORD *)&cbWritten, TRUE) == FALSE)
                    uError = GetLastError();
            }
        }
        else
            cbWritten = *cbWrite; 

        if (VBOX_FAILURE(rc))
        {
            if (rc == VERR_EOF)
            {
                RTFILE tmp = pData->NamedPipe;
                FlushFileBuffers((HANDLE)tmp);
                if (!pData->fIsServer)
                {
                    pData->NamedPipe = NIL_RTFILE;
                    DisconnectNamedPipe((HANDLE)tmp);
                    RTFileClose(tmp);
                }
            }
            cbWritten = 0;
        }
        *cbWrite = cbWritten;
    }
#else /* !__WIN__ */
    if (pData->LocalSocket != NIL_RTSOCKET)
    {
        ssize_t cbWritten;
        cbWritten = send(pData->LocalSocket, pvBuf, *cbWrite, 0);
        if (cbWritten == 0)
        {
            RTSOCKET tmp = pData->LocalSocket;
            pData->LocalSocket = NIL_RTSOCKET;
            close(tmp);
        }
        else if (cbWritten == -1)
        {
            cbWritten = 0;
            rc = RTErrConvertFromErrno(errno);
        }
        *cbWrite = cbWritten;
    }
#endif /* !__WIN__ */

    LogFlow(("%s: returns %Vrc\n", __FUNCTION__, rc));
    return rc;
}


/**
 * Queries an interface to the driver.
 *
 * @returns Pointer to interface.
 * @returns NULL if the interface was not supported by the driver.
 * @param   pInterface          Pointer to this interface structure.
 * @param   enmInterface        The requested interface identification.
 * @thread  Any thread.
 */
static DECLCALLBACK(void *) drvNamedPipeQueryInterface(PPDMIBASE pInterface, PDMINTERFACE enmInterface)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_DRVINS(pInterface);
    PDRVNAMEDPIPE pDrv = PDMINS2DATA(pDrvIns, PDRVNAMEDPIPE);
    switch (enmInterface)
    {
        case PDMINTERFACE_BASE:
            return &pDrvIns->IBase;
        case PDMINTERFACE_STREAM:
            return &pDrv->IStream;
        default:
            return NULL;
    }
}


/* -=-=-=-=- listen thread -=-=-=-=- */

/**
 * Receive thread loop.
 *
 * @returns 0 on success.
 * @param   ThreadSelf  Thread handle to this thread.
 * @param   pvUser      User argument.
 */
static DECLCALLBACK(int) drvNamedPipeListenLoop(RTTHREAD ThreadSelf, void *pvUser)
{
    PDRVNAMEDPIPE   pData = (PDRVNAMEDPIPE)pvUser;
    int             rc = VINF_SUCCESS;
    RTFILE          NamedPipe = pData->NamedPipe;

    while (RT_LIKELY(!pData->fShutdown))
    {
#ifdef __WIN__
        BOOL fConnected = ConnectNamedPipe((HANDLE)NamedPipe, NULL);
        if (!fConnected)
        {
            int hrc = GetLastError();
            if (hrc != ERROR_PIPE_CONNECTED)
            {
                rc = RTErrConvertFromWin32(hrc);
                LogRel(("NamedPipe%d: ConnectNamedPipe failed, rc=%Vrc\n", pData->pDrvIns->iInstance, rc));
                break;
            }
        }
#else /* !__WIN__ */
        if (listen(pData->LocalSocketServer, 0) == -1)
        {
            rc = RTErrConvertFromErrno(errno);
            LogRel(("NamedPipe%d: listen failed, rc=%Vrc\n", pData->pDrvIns->iInstance, rc));
            break;
        }
        int s = accept(pData->LocalSocketServer, NULL, NULL);
        if (s == -1)
        {
            rc = RTErrConvertFromErrno(errno);
            LogRel(("NamedPipe%d: accept failed, rc=%Vrc\n", pData->pDrvIns->iInstance, rc));
            break;
        }
        else
        {
            if (pData->LocalSocket != NIL_RTSOCKET)
            {
                LogRel(("NamedPipe%d: only single connection supported\n", pData->pDrvIns->iInstance));
                close(s);
            }
            else
                pData->LocalSocket = s;
        }
#endif /* !__WIN__ */
    }

    return VINF_SUCCESS;
}


/**
 * Construct a named pipe stream driver instance.
 *
 * @returns VBox status.
 * @param   pDrvIns     The driver instance data.
 *                      If the registration structure is needed, pDrvIns->pDrvReg points to it.
 * @param   pCfgHandle  Configuration node handle for the driver. Use this to obtain the configuration
 *                      of the driver instance. It's also found in pDrvIns->pCfgHandle, but like
 *                      iInstance it's expected to be used a bit in this function.
 */
static DECLCALLBACK(int) drvNamedPipeConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfgHandle)
{
    int rc;
    PDRVNAMEDPIPE pData = PDMINS2DATA(pDrvIns, PDRVNAMEDPIPE);

    /*
     * Init the static parts.
     */
    pData->pDrvIns                      = pDrvIns;
    pData->pszLocation                  = NULL;
    pData->fIsServer                    = false;
#ifdef __WIN__
    pData->NamedPipe                    = NIL_RTFILE;
#else /* !__WIN__ */
    pData->LocalSocketServer            = NIL_RTSOCKET;
    pData->LocalSocket                  = NIL_RTSOCKET;
#endif /* !__WIN__ */
    pData->ListenThread                 = NIL_RTTHREAD;
    pData->fShutdown                    = false;
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface    = drvNamedPipeQueryInterface;
    /* IStream */
    pData->IStream.pfnRead              = drvNamedPipeRead;
    pData->IStream.pfnWrite             = drvNamedPipeWrite;

    /*
     * Read the configuration.
     */
    if (!CFGMR3AreValuesValid(pCfgHandle, "Location\0IsServer\0"))
        return VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES;

    char *pszLocation;
    rc = CFGMR3QueryStringAlloc(pCfgHandle, "Location", &pszLocation);
    if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: query \"Location\" resulted in %Vrc.\n", rc));
        return rc;
    }
    pData->pszLocation = pszLocation;

    bool fIsServer;
    rc = CFGMR3QueryBool(pCfgHandle, "IsServer", &fIsServer);
    if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: query \"IsServer\" resulted in %Vrc.\n", rc));
        goto out;
    }
    pData->fIsServer = fIsServer;

#ifdef __WIN__
    if (fIsServer)
    {
        HANDLE hPipe = CreateNamedPipe(pData->pszLocation, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 32, 32, 10000, NULL);
        if (hPipe == INVALID_HANDLE_VALUE)
        {
            rc = RTErrConvertFromWin32(GetLastError());
            LogRel(("NamedPipe%d: CreateNamedPipe failed rc=%Vrc\n", pData->pDrvIns->iInstance));
            return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, N_("NamedPipe#%d failed to create named pipe %s"), pDrvIns->iInstance, pszLocation);
        }
        pData->NamedPipe = (HFILE)hPipe;

        rc = RTThreadCreate(&pData->ListenThread, drvNamedPipeListenLoop, (void *)pData, 0, RTTHREADTYPE_IO, 0, "NamedPipe");
        if VBOX_FAILURE(rc)
            return PDMDrvHlpVMSetError(pDrvIns, rc,  RT_SRC_POS, N_("NamedPipe#%d failed to create listening thread\n"), pDrvIns->iInstance);
    }
    else
    {
        /* Connect to the named pipe. */
        rc = RTFileOpen(&pData->NamedPipe, pszLocation, RTFILE_O_READWRITE);
        if (VBOX_FAILURE(rc))
            return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, N_("NamedPipe#%d failed to connect to named pipe %s"), pDrvIns->iInstance, pszLocation);
    }

    memset(&pData->OverlappedWrite, 0, sizeof(pData->OverlappedWrite));
    memset(&pData->OverlappedRead, 0, sizeof(pData->OverlappedRead));
    pData->OverlappedWrite.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    pData->OverlappedRead.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

#else /* !__WIN__ */
    int s;
    struct sockaddr_un addr;

    if ((s = socket(PF_UNIX, SOCK_STREAM, 0)) == -1)
        return PDMDrvHlpVMSetError(pDrvIns, RTErrConvertFromErrno(errno), RT_SRC_POS, N_("NamedPipe#%d failed to create local socket"), pDrvIns->iInstance);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, pszLocation, sizeof(addr.sun_path)-1);

    if (fIsServer)
    {
        /* Bind address to the local socket. */
        RTFileDelete(pszLocation);
        if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1)
            return PDMDrvHlpVMSetError(pDrvIns, RTErrConvertFromErrno(errno), RT_SRC_POS, N_("NamedPipe#%d failed to bind to local socket %s"), pDrvIns->iInstance, pszLocation);
        rc = RTThreadCreate(&pData->ListenThread, drvNamedPipeListenLoop, (void *)pData, 0, RTTHREADTYPE_IO, 0, "NamedPipe");
        if VBOX_FAILURE(rc)
            return PDMDrvHlpVMSetError(pDrvIns, rc,  RT_SRC_POS, N_("NamedPipe#%d failed to create listening thread\n"), pDrvIns->iInstance);
        pData->LocalSocketServer = s;
    }
    else
    {
        /* Connect to the local socket. */
        if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == -1)
            return PDMDrvHlpVMSetError(pDrvIns, RTErrConvertFromErrno(errno), RT_SRC_POS, N_("NamedPipe#%d failed to connect to local socket %s"), pDrvIns->iInstance, pszLocation);
        pData->LocalSocket = s;
    }
#endif /* !__WIN__ */

out:
    if (VBOX_FAILURE(rc))
    {
        if (pszLocation)
            MMR3HeapFree(pszLocation);
    }
    else
    {
        LogFlow(("drvNamedPipeConstruct: location %s isServer %d\n", pszLocation, fIsServer));
        LogRel(("NamedPipe: location %s, %s\n", pszLocation, fIsServer ? "server" : "client"));
    }
    return rc;
}


/**
 * Destruct a named pipe stream driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that
 * any non-VM resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvNamedPipeDestruct(PPDMDRVINS pDrvIns)
{
    PDRVNAMEDPIPE pData = PDMINS2DATA(pDrvIns, PDRVNAMEDPIPE);
    LogFlow(("%s: %s\n", __FUNCTION__, pData->pszLocation));

#ifdef __WIN__
    if (pData->NamedPipe != NIL_RTFILE)
    {
        FlushFileBuffers((HANDLE)pData->NamedPipe);
        if (!pData->fIsServer)
            DisconnectNamedPipe((HANDLE)pData->NamedPipe);

        RTFileClose(pData->NamedPipe);
        CloseHandle(pData->OverlappedRead.hEvent);
        CloseHandle(pData->OverlappedWrite.hEvent);
    }
#else /* !__WIN__ */
    if (pData->fIsServer)
    {
        if (pData->LocalSocketServer != NIL_RTSOCKET)
            close(pData->LocalSocketServer);
        if (pData->pszLocation)
            RTFileDelete(pData->pszLocation);
    }
    else
    {
        if (pData->LocalSocket != NIL_RTSOCKET)
            close(pData->LocalSocket);
    }
#endif /* !__WIN__ */
    if (pData->pszLocation)
        MMR3HeapFree(pData->pszLocation);
}


/**
 * Named pipe driver registration record.
 */
const PDMDRVREG g_DrvNamedPipe =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szDriverName */
    "NamedPipe",
    /* pszDescription */
    "Named Pipe stream driver.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_STREAM,
    /* cMaxInstances */
    ~0,
    /* cbInstance */
    sizeof(DRVNAMEDPIPE),
    /* pfnConstruct */
    drvNamedPipeConstruct,
    /* pfnDestruct */
    drvNamedPipeDestruct,
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
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL
};
