/** @file
 * VBoxService - Guest Additions Service
 */

/*
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

#include "VBoxService.h"
#include "VBoxSeamless.h"
#include "VBoxClipboard.h"
#include "VBoxDisplay.h"
#include "VBoxRestore.h"
#include <VBoxHook.h>
#include "resource.h"
#include <malloc.h>
#include <VBoxGuestInternal.h>

#include "helpers.h"

/* global variables */
HANDLE                gVBoxDriver;
HANDLE                gStopSem;
HANDLE                ghSeamlessNotifyEvent = 0;
SERVICE_STATUS        gVBoxServiceStatus;
SERVICE_STATUS_HANDLE gVBoxServiceStatusHandle;
HINSTANCE             gInstance;
HWND                  gToolWindow;


/* prototypes */
VOID DisplayChangeThread(void *dummy);
LRESULT CALLBACK VBoxToolWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);


#ifdef DEBUG
/**
 * Helper function to send a message to WinDbg
 *
 * @param String message string
 */
void WriteLog(char *String, ...)
{
    DWORD cbReturned;
    CHAR Buffer[1024];
    VMMDevReqLogString *pReq = (VMMDevReqLogString *)Buffer;

    va_list va;

    va_start(va, String);

    vmmdevInitRequest(&pReq->header, VMMDevReq_LogString);
    vsprintf(pReq->szString, String, va);
    OutputDebugStringA(pReq->szString);
    pReq->header.size += strlen(pReq->szString);

    DeviceIoControl(gVBoxDriver, IOCTL_VBOXGUEST_VMMREQUEST, pReq, pReq->header.size,
                    pReq, pReq->header.size, &cbReturned, NULL);

    va_end (va);
    return;
}
#endif



/* The service table. */
static VBOXSERVICEINFO vboxServiceTable[] =
{
    {
        "Display",
        VBoxDisplayInit,
        VBoxDisplayThread,
        VBoxDisplayDestroy,
    },
    {
        "Shared Clipboard",
        VBoxClipboardInit,
        VBoxClipboardThread,
        VBoxClipboardDestroy
    },
    {
        "Seamless Windows",
        VBoxSeamlessInit,
        VBoxSeamlessThread,
        VBoxSeamlessDestroy
    },
#ifdef VBOX_WITH_VRDP_SESSION_HANDLING
    {
        "Restore",
        VBoxRestoreInit,
        VBoxRestoreThread,
        VBoxRestoreDestroy,
    },
#endif
    {
        NULL
    }
};

static int vboxStartServices (VBOXSERVICEENV *pEnv, VBOXSERVICEINFO *pTable)
{
    pEnv->hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (!pEnv->hStopEvent)
    {
        /* Could not create event. */
        return VERR_NOT_SUPPORTED;
    }

    while (pTable->pszName)
    {
        dprintf(("Starting %s...\n", pTable->pszName));

        int rc = VINF_SUCCESS;

        bool fStartThread = false;

        pTable->hThread = (HANDLE)0;
        pTable->pInstance = NULL;
        pTable->fStarted = false;

        if (pTable->pfnInit)
        {
            rc = pTable->pfnInit (pEnv, &pTable->pInstance, &fStartThread);
        }

        if (VBOX_FAILURE (rc))
        {
            dprintf(("Failed to initialize rc = %Vrc.\n", rc));
        }
        else
        {
            if (pTable->pfnThread && fStartThread)
            {
                unsigned threadid;

                pTable->hThread = (HANDLE)_beginthreadex (NULL,  /* security */
                                                          0,     /* stacksize */
                                                          pTable->pfnThread,
                                                          pTable->pInstance,
                                                          0,     /* initflag */
                                                          &threadid);

                if (pTable->hThread == (HANDLE)(0))
                {
                    rc = VERR_NOT_SUPPORTED;
                }
            }

            if (VBOX_FAILURE (rc))
            {
                dprintf(("Failed to start the thread.\n"));

                if (pTable->pfnDestroy)
                {
                    pTable->pfnDestroy (pEnv, pTable->pInstance);
                }
            }
            else
            {
                pTable->fStarted = true;
            }
        }

        /* Advance to next table element. */
        pTable++;
    }

    return VINF_SUCCESS;
}

static void vboxStopServices (VBOXSERVICEENV *pEnv, VBOXSERVICEINFO *pTable)
{
    if (!pEnv->hStopEvent)
    {
        return;
    }

    /* Signal to all threads. */
    SetEvent(pEnv->hStopEvent);

    while (pTable->pszName)
    {
        if (pTable->fStarted)
        {
            if (pTable->pfnThread)
            {
                /* There is a thread, wait for termination. */
                WaitForSingleObject(pTable->hThread, INFINITE);

                CloseHandle (pTable->hThread);
                pTable->hThread = 0;
            }

            if (pTable->pfnDestroy)
            {
                pTable->pfnDestroy (pEnv, pTable->pInstance);
            }

            pTable->fStarted = false;
        }

        /* Advance to next table element. */
        pTable++;
    }

    CloseHandle (pEnv->hStopEvent);
}


void WINAPI VBoxServiceStart(void)
{
    dprintf(("VBoxService: Start\n"));

    VBOXSERVICEENV svcEnv;

    DWORD status = NO_ERROR;

    /* open VBox guest driver */
    gVBoxDriver = CreateFile(VBOXGUEST_DEVICE_NAME,
                             GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                             NULL);
    if (gVBoxDriver == INVALID_HANDLE_VALUE)
    {
        dprintf(("VBoxService: could not open VBox Guest Additions driver! rc = %d\n", GetLastError()));
        status = ERROR_GEN_FAILURE;
    }

    dprintf(("VBoxService: Driver h %p, st %p\n", gVBoxDriver, status));

    if (status == NO_ERROR)
    {
        /* create a custom window class */
        WNDCLASS windowClass = {0};
        windowClass.style         = CS_NOCLOSE;
        windowClass.lpfnWndProc   = (WNDPROC)VBoxToolWndProc;
        windowClass.hInstance     = gInstance;
        windowClass.hCursor       = LoadCursor(NULL, IDC_ARROW);
        windowClass.lpszClassName = "VirtualBoxTool";
        if (!RegisterClass(&windowClass))
            status = GetLastError();
    }

    dprintf(("VBoxService: Class st %p\n", status));

    if (status == NO_ERROR)
    {
        /* create our window */
        gToolWindow = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
                                     "VirtualBoxTool", "VirtualBoxTool",
                                     WS_POPUPWINDOW,
                                     -200, -200, 100, 100, NULL, NULL, gInstance, NULL);
        if (!gToolWindow)
            status = GetLastError();
        else
        {
            /* move the window beneach the mouse pointer so that we get access to it */
            POINT mousePos;
            GetCursorPos(&mousePos);
            SetWindowPos(gToolWindow, HWND_TOPMOST, mousePos.x - 10, mousePos.y - 10, 0, 0,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_NOSIZE);
            /* change the mouse pointer so that we can go for a hardware shape */
            SetCursor(LoadCursor(NULL, IDC_APPSTARTING));
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            /* move back our tool window */
            SetWindowPos(gToolWindow, HWND_TOPMOST, -200, -200, 0, 0,
                         SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_NOSIZE);
        }
    }

    dprintf(("VBoxService: Window h %p, st %p\n", gToolWindow, status));

    if (status == NO_ERROR)
    {
        gStopSem = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (gStopSem == NULL)
        {
            dprintf(("VBoxService: CreateEvent failed: rc = %d\n", GetLastError()));
            return;
        }
        ghSeamlessNotifyEvent = CreateEvent(NULL, FALSE, FALSE, VBOXHOOK_GLOBAL_EVENT_NAME);
        if (ghSeamlessNotifyEvent == NULL)
        {
            dprintf(("VBoxService: CreateEvent failed: rc = %d\n", GetLastError()));
            return;
        }
    }

    /*
     * Start services listed in the vboxServiceTable.
     */
    svcEnv.hInstance  = gInstance;
    svcEnv.hDriver    = gVBoxDriver;

    if (status == NO_ERROR)
    {
        int rc = vboxStartServices (&svcEnv, vboxServiceTable);

        if (VBOX_FAILURE (rc))
        {
            status = ERROR_GEN_FAILURE;
        }
    }

    /* terminate service if something went wrong */
    if (status != NO_ERROR)
    {
        vboxStopServices (&svcEnv, vboxServiceTable);
        return;
    }

    BOOL fTrayIconCreated = false;

    /* prepare the system tray icon */
    NOTIFYICONDATA ndata;
    memset (&ndata, 0, sizeof (ndata));
    ndata.cbSize           = NOTIFYICONDATA_V1_SIZE; // sizeof(NOTIFYICONDATA);
    ndata.hWnd             = gToolWindow;
    ndata.uID              = 2000;
    ndata.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    ndata.uCallbackMessage = WM_USER;
    ndata.hIcon            = LoadIcon(gInstance, MAKEINTRESOURCE(IDI_VIRTUALBOX));
    sprintf(ndata.szTip, "innotek VirtualBox Guest Additions %d.%d.%d", VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR, VBOX_VERSION_BUILD);

    dprintf(("VBoxService: ndata.hWnd %08X, ndata.hIcon = %p\n", ndata.hWnd, ndata.hIcon));

    /*
     * Main execution loop
     * Wait for the stop semaphore to be posted or a window event to arrive
     */
    HANDLE hWaitEvent[2] = {gStopSem, ghSeamlessNotifyEvent};
    while(true)
    {
        DWORD waitResult = MsgWaitForMultipleObjectsEx(2, hWaitEvent, 500, QS_ALLINPUT, 0);
        if (waitResult == WAIT_OBJECT_0)
        {
            dprintf(("VBoxService: exit\n"));
            /* exit */
            break;
        }
        else
        if (waitResult == WAIT_OBJECT_0+1)
        {
            /* seamless window notification */
            VBoxSeamlessCheckWindows();
        }
        else
        {
            /* timeout or a window message, handle it */
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                dprintf(("VBoxService: msg %p\n", msg.message));
                if (msg.message == WM_QUIT)
                {
                    dprintf(("VBoxService: WM_QUIT!\n"));
                    SetEvent(gStopSem);
                    continue;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            /* we might have to repeat this operation because the shell might not be loaded yet */
            if (!fTrayIconCreated)
            {
                fTrayIconCreated = Shell_NotifyIcon(NIM_ADD, &ndata);
                dprintf(("VBoxService: fTrayIconCreated = %d, err %08X\n", fTrayIconCreated, GetLastError ()));
            }
        }
    }

    dprintf(("VBoxService: returned from main loop, exiting...\n"));

    /* remove the system tray icon */
    Shell_NotifyIcon(NIM_DELETE, &ndata);

    dprintf(("VBoxService: waiting for display change thread...\n"));

    vboxStopServices (&svcEnv, vboxServiceTable);

    dprintf(("VBoxService: destroying tool window...\n"));

    /* destroy the tool window */
    DestroyWindow(gToolWindow);

    UnregisterClass("VirtualBoxTool", gInstance);

    CloseHandle(gVBoxDriver);
    CloseHandle(gStopSem);

    dprintf(("VBoxService: leaving service main function\n"));

    return;
}


/**
 * Main function
 */
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    dprintf(("VBoxService: WinMain\n"));
    gInstance = hInstance;
    VBoxServiceStart ();
    return 0;
}

/**
 * Window procedure for our tool window
 */
LRESULT CALLBACK VBoxToolWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CLOSE:
            break;

        case WM_DESTROY:
            break;

        case WM_VBOX_INSTALL_SEAMLESS_HOOK:
            VBoxSeamlessInstallHook();
            break;

        case WM_VBOX_REMOVE_SEAMLESS_HOOK:
            VBoxSeamlessRemoveHook();
            break;

        case WM_VBOX_SEAMLESS_UPDATE:
            VBoxSeamlessCheckWindows();
            break;

        case WM_VBOX_RESTORED:
            VBoxRestoreSession();
            break;

        case WM_VBOX_CHECK_VRDP:
            VBoxRestoreCheckVRDP();
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

