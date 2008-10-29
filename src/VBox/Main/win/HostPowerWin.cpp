/** @file
 *
 * VirtualBox interface to host's power notification service
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
#include <windows.h>

#include <VBox/com/ptr.h>
#include "HostPower.h"
#include "Logging.h"

static char gachWindowClassName[] = "VBoxPowerNotifyClass";

HostPowerServiceWin::HostPowerServiceWin(VirtualBox *aVirtualBox) : HostPowerService(aVirtualBox)
{
    mHwnd = 0;

#if 0
    int rc = RTThreadCreate (&mThread, HostPowerServiceWin::NotificationThread, this, 65536,
                             RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "MainPower");

    if (RT_FAILURE (rc))
    {
        Log(("HostPowerServiceWin::HostPowerServiceWin: RTThreadCreate failed with %Rrc\n", rc));
        return;
    }
#endif
}

HostPowerServiceWin::~HostPowerServiceWin()
{
    if (mHwnd)
    {
        Log(("HostPowerServiceWin::!HostPowerServiceWin: destroy window %x\n", mHwnd));

        /* Is this allowed from another thread? */
        SetWindowLongPtr(mHwnd, 0, 0);
        PostMessage(mHwnd, WM_CLOSE, 0, 0);
    }
}



DECLCALLBACK(int) HostPowerServiceWin::NotificationThread (RTTHREAD ThreadSelf, void *pInstance)
{
    HostPowerServiceWin *pPowerObj = (HostPowerServiceWin *)pInstance;
    HWND                 hwnd = 0;

    /* Create a window and make it a power event notification handler. */
    int rc = VINF_SUCCESS;

    HINSTANCE hInstance = (HINSTANCE)GetModuleHandle (NULL);

    /* Register the Window Class. */
    WNDCLASSA wc;

    wc.style         = CS_NOCLOSE;
    wc.lpfnWndProc   = HostPowerServiceWin::WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = sizeof(void *);
    wc.hInstance     = hInstance;
    wc.hIcon         = NULL;
    wc.hCursor       = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = gachWindowClassName;

    ATOM atomWindowClass = RegisterClassA(&wc);

    if (atomWindowClass == 0)
    {
        rc = VERR_NOT_SUPPORTED;
        Log(("HostPowerServiceWin::NotificationThread: RegisterClassA failed with %x\n", GetLastError()));
    }
    else
    {
        /* Create the window. */
        hwnd = pPowerObj->mHwnd = CreateWindowExA (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
                                                  gachWindowClassName, gachWindowClassName,
                                                  WS_POPUPWINDOW,
                                                 -200, -200, 100, 100, NULL, NULL, hInstance, NULL);

        if (hwnd == NULL)
        {
            Log(("HostPowerServiceWin::NotificationThread: CreateWindowExA failed with %x\n", GetLastError()));
            rc = VERR_NOT_SUPPORTED;
        }
        else
        {
            SetWindowLongPtr(hwnd, 0, (LONG_PTR)pPowerObj);
            SetWindowPos(hwnd, HWND_TOPMOST, -200, -200, 0, 0,
                         SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_NOSIZE);

            MSG msg;
            while (GetMessage(&msg, NULL, 0, 0))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    Log(("HostPowerServiceWin::NotificationThread: exit thread\n"));
    if (hwnd)
    {
        DestroyWindow (hwnd);
    }

    if (atomWindowClass != 0)
    {
        UnregisterClassA (gachWindowClassName, hInstance);
        atomWindowClass = 0;
    }

    return 0;
}

LRESULT CALLBACK HostPowerServiceWin::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_POWERBROADCAST:
        {
            HostPowerServiceWin *pPowerObj;

            pPowerObj = (HostPowerServiceWin *)GetWindowLongPtr(hwnd, 0);
            switch(wParam)
            {
            case PBT_APMSUSPEND:
                pPowerObj->notify(HostPowerEvent_Suspend);
                break;

            case PBT_APMRESUMEAUTOMATIC:
                pPowerObj->notify(HostPowerEvent_Resume);
                break;

            case PBT_APMPOWERSTATUSCHANGE:
            {
                SYSTEM_POWER_STATUS SystemPowerStatus;

                if (GetSystemPowerStatus(&SystemPowerStatus) == TRUE)
                {
                    if (    SystemPowerStatus.BatteryFlag != 255 /* unknown */
                        && (SystemPowerStatus.BatteryFlag & 4 /* critical battery status; less than 5% */))
                    {
                        pPowerObj->notify(HostPowerEvent_BatteryLow);
                    }
                }
                break;
            }

            }
            return DefWindowProc (hwnd, msg, wParam, lParam);
        }

        default:
            return DefWindowProc (hwnd, msg, wParam, lParam);
    }
}
