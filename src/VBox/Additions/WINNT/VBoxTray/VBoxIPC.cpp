/* $Id$ */
/** @file
 * VboxIPC - IPC thread.
 */

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
#include <windows.h>
#include "VBoxTray.h"
#include "VBoxTrayMsg.h"
#include "VBoxHelpers.h"
#include "VBoxIPC.h"

#include <iprt/assert.h>
#include <iprt/err.h>
#include <VBoxGuestInternal.h>

typedef struct _VBOXIPCCONTEXT
{
    const VBOXSERVICEENV *pEnv;
    HANDLE hPipe;

} VBOXIPCCONTEXT, *PVBOXIPCCONTEXT;

#define VBOXTRAY_PIPE_BUFSIZE 512

static VBOXIPCCONTEXT gCtx = {0};

int VBoxIPCReadMessage(PVBOXIPCCONTEXT pCtx, BYTE *pMessage, DWORD cbMessage)
{
    int rc = VINF_SUCCESS;
    while (RT_SUCCESS(rc))
    {
        DWORD dwRead;
        if (!ReadFile(pCtx->hPipe, pMessage, cbMessage, &dwRead, 0))
            rc = RTErrConvertFromWin32(GetLastError());
        if (rc == VERR_MORE_DATA)
        {
            rc = VINF_SUCCESS;
            pMessage += dwRead;
        }
    }
    return rc;
}

int VBoxIPCWriteMessage(PVBOXIPCCONTEXT pCtx, BYTE *pMessage, DWORD cbMessage)
{
    int rc = VINF_SUCCESS;
    while (RT_SUCCESS(rc))
    {
        DWORD cbWritten;
        if (!WriteFile(pCtx->hPipe, pMessage, cbMessage, &cbWritten, 0))
            rc = RTErrConvertFromWin32(GetLastError());
        pMessage += cbWritten;
    }
    return rc;
}

int VBoxIPCPostQuitMessage(PVBOXIPCCONTEXT pCtx)
{
    VBOXTRAYIPCHEADER hdr;
    hdr.uMsg = VBOXTRAYIPCMSGTYPE_QUIT;
    hdr.uVer = 0;
    return VBoxIPCWriteMessage(pCtx, (BYTE*)&hdr, sizeof(hdr));
}

/**
 * Shows a balloon tooltip message in VBoxTray's
 * message area in the Windows main taskbar.
 *
 * @return  IPRT status code.
 * @return  int
 * @param   pCtx
 * @param   uVersion
 */
int VBoxIPCMsgShowBalloonMsg(PVBOXIPCCONTEXT pCtx, UINT uVersion)
{
    VBOXTRAYIPCMSG_SHOWBALLOONMSG msg;
    int rc = VBoxIPCReadMessage(pCtx,(BYTE*)&msg, sizeof(msg));
    if (RT_SUCCESS(rc))
    {
        hlpShowBalloonTip(gInstance, gToolWindow, ID_TRAYICON,
                          msg.szBody, msg.szTitle,
                          msg.uShowMS, msg.uType);
    }
    return rc;
}

int VBoxIPCInit(const VBOXSERVICEENV *pEnv, void **ppInstance, bool *pfStartThread)
{
    Log(("VBoxTray: VBoxIPCInit\n"));

    *pfStartThread = false;
    gCtx.pEnv = pEnv;

    SECURITY_ATTRIBUTES sa;
    sa.lpSecurityDescriptor = (PSECURITY_DESCRIPTOR)malloc(SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!InitializeSecurityDescriptor(sa.lpSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION))
    {
        DWORD er = ::GetLastError();
    }
    if (!SetSecurityDescriptorDacl(sa.lpSecurityDescriptor, TRUE, (PACL)0, FALSE))
    {
        DWORD er = ::GetLastError();
    }
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;

    int rc = VINF_SUCCESS;
    gCtx.hPipe = ::CreateNamedPipe((LPSTR)VBOXTRAY_PIPE_IPC,
                                   PIPE_ACCESS_DUPLEX,
                                   PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                   PIPE_UNLIMITED_INSTANCES,
                                   VBOXTRAY_PIPE_BUFSIZE, // output buffer size
                                   VBOXTRAY_PIPE_BUFSIZE, // input buffer size
                                   NMPWAIT_USE_DEFAULT_WAIT,
                                   &sa);
    if (gCtx.hPipe == INVALID_HANDLE_VALUE)
    {
        DWORD dwError = ::GetLastError();
    }
    else
    {
        *pfStartThread = true;
        *ppInstance = &gCtx;
    }
    return rc;
}


void VBoxIPCDestroy(const VBOXSERVICEENV *pEnv, void *pInstance)
{
    Log(("VBoxTray: VBoxIPCDestroy\n"));

    PVBOXIPCCONTEXT pCtx = (PVBOXIPCCONTEXT)pInstance;
    AssertPtr(pCtx);

    if (pCtx->hPipe)
    {
        VBoxIPCPostQuitMessage(pCtx);
        CloseHandle(pCtx->hPipe);
    }
    return;
}

/**
 * Thread function to wait for and process seamless mode change
 * requests
 */
unsigned __stdcall VBoxIPCThread(void *pInstance)
{
    Log(("VBoxTray: VBoxIPCThread\n"));

    PVBOXIPCCONTEXT pCtx = (PVBOXIPCCONTEXT)pInstance;
    AssertPtr(pCtx);

    bool fTerminate = false;
    int rc = VINF_SUCCESS;

    do
    {
        DWORD dwErr = ERROR_SUCCESS;
        BOOL fConnected =   ConnectNamedPipe(pCtx->hPipe, NULL)
                          ? TRUE
                          : (GetLastError() == ERROR_PIPE_CONNECTED);

        /* Are we supposed to stop? */
        if (WaitForSingleObject(pCtx->pEnv->hStopEvent, 0) == WAIT_OBJECT_0)
            break;

        if (fConnected)
        {
            VBOXTRAYIPCHEADER hdr;
            DWORD read = 0;

            if (!ReadFile(pCtx->hPipe, &hdr, sizeof(hdr), &read, 0))
                dwErr = GetLastError();

            /** @todo We might want to spawn a thread per connected client
             *        in order to perform longer tasks. */

            if (SUCCEEDED(dwErr))
            {
                switch (hdr.uMsg)
                {
                    case VBOXTRAYIPCMSGTYPE_SHOWBALLOONMSG:
                        rc = VBoxIPCMsgShowBalloonMsg(pCtx, hdr.uVer);
                        break;

                    /* Someone asked us to quit ... */
                    case VBOXTRAYIPCMSGTYPE_QUIT:
                        fTerminate = true;
                        break;

                    default:
                        break;
                }
            }

            /* Disconnect the client from the pipe. */
            DisconnectNamedPipe(pCtx->hPipe);
        }
        else
            CloseHandle(pCtx->hPipe);

        /* Sleep a bit to not eat too much CPU in case the above call always fails. */
        if (WaitForSingleObject(pCtx->pEnv->hStopEvent, 10) == WAIT_OBJECT_0)
        {
            fTerminate = true;
            break;
        }
    } while (!fTerminate);

    Log(("VBoxTray: VBoxIPCThread exited\n"));
    return 0;
}
