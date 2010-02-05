/* $Id: $ */
/** @file
 * VBoxStats - Guest statistics notification
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
#define _WIN32_WINNT 0x0500
#include <windows.h>
#include <psapi.h>
#include "VBoxTray.h"
#include <VBoxDisplay.h>
#include <VBox/VMMDev.h>
#include <VBox/VBoxGuest.h>
#include <VBoxGuestInternal.h>
#include <iprt/assert.h>
#include "helpers.h"
#include <winternl.h>

typedef struct _VBOXSTATSCONTEXT
{
    uint32_t              uStatInterval;

    uint64_t              ullLastCpuLoad_Idle;
    uint64_t              ullLastCpuLoad_Kernel;
    uint64_t              ullLastCpuLoad_User;

#ifdef RT_OS_WINDOWS
    NTSTATUS (WINAPI *pfnNtQuerySystemInformation)(SYSTEM_INFORMATION_CLASS SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength);
    void     (WINAPI *pfnGlobalMemoryStatusEx)(LPMEMORYSTATUSEX lpBuffer);
    BOOL     (WINAPI *pfnGetPerformanceInfo)(PPERFORMANCE_INFORMATION pPerformanceInformation, DWORD cb);
#endif
} VBOXSTATSCONTEXT;

/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
static VBOXSTATSCONTEXT gCtx = {0};

/** The semaphore we're blocking on. */
static RTSEMEVENTMULTI  g_VMStatEvent = NIL_RTSEMEVENTMULTI;
/** The vmstats interval (millseconds). */
uint32_t                g_VMStatsInterval = 0;


/** @copydoc VBOXSERVICE::pfnPreInit */
static DECLCALLBACK(int) VBoxServiceVMStatsPreInit(void)
{
    return VINF_SUCCESS;
}


/** @copydoc VBOXSERVICE::pfnOption */
static DECLCALLBACK(int) VBoxServiceVMStatsOption(const char **ppszShort, int argc, char **argv, int *pi)
{
    int rc = -1;
    if (ppszShort)
        /* no short options */;
    else if (!strcmp(argv[*pi], "--vmstats-interval"))
        rc = VBoxServiceArgUInt32(argc, argv, "", pi,
                                  &g_VMStatsInterval, 1000, UINT32_MAX - 1);
    return rc;
}


/** @copydoc VBOXSERVICE::pfnInit */
static DECLCALLBACK(int) VBoxServiceVMStatsInit(void)
{
    VBoxServiceVerbose(3, "VBoxServiceVMStatsInit\n");

    int rc = RTSemEventMultiCreate(&g_VMStatEvent);
    AssertRCReturn(rc, rc);

    gCtx.pEnv                   = pEnv;
    gCtx.uStatInterval          = 0;     /* default */
    gCtx.ullLastCpuLoad_Idle    = 0;
    gCtx.ullLastCpuLoad_Kernel  = 0;
    gCtx.ullLastCpuLoad_User    = 0;

    VMMDevGetStatisticsChangeRequest req;
    vmmdevInitRequest(&req.header, VMMDevReq_GetStatisticsChangeRequest);
    req.eventAck = 0;

    if (DeviceIoControl(gVBoxDriver, VBOXGUEST_IOCTL_VMMREQUEST(req.header.size), &req, req.header.size, &req, req.header.size, &cbReturned, NULL))
    {
        Log(("VBoxStatsInit: new statistics interval %d seconds\n", req.u32StatInterval));
        gCtx.uStatInterval = req.u32StatInterval * 1000;
    }
    else
        Log(("VBoxStatsInit: DeviceIoControl failed with %d\n", GetLastError()));

#ifdef RT_OS_WINDOWS
    /* NtQuerySystemInformation might be dropped in future releases, so load it dynamically as per Microsoft's recommendation */
    HMODULE hMod = LoadLibrary("NTDLL.DLL");
    if (hMod)
    {
        *(uintptr_t *)&gCtx.pfnNtQuerySystemInformation = (uintptr_t)GetProcAddress(hMod, "NtQuerySystemInformation");
        if (gCtx.pfnNtQuerySystemInformation)
            VBoxServiceVerbose(3, "VBoxStatsInit: gCtx.pfnNtQuerySystemInformation = %x\n", gCtx.pfnNtQuerySystemInformation);
        else
        {
            VBoxServiceError("VBoxStatsInit: NTDLL.NtQuerySystemInformation not found!!\n");
            return VERR_NOT_IMPLEMENTED;
        }
    }

    /* GlobalMemoryStatus is win2k and up, so load it dynamically */
    hMod = LoadLibrary("KERNEL32.DLL");
    if (hMod)
    {
        *(uintptr_t *)&gCtx.pfnGlobalMemoryStatusEx = (uintptr_t)GetProcAddress(hMod, "GlobalMemoryStatusEx");
        if (gCtx.pfnGlobalMemoryStatusEx)
            VBoxServiceVerbose(3, "VBoxStatsInit: gCtx.GlobalMemoryStatusEx = %x\n", gCtx.pfnGlobalMemoryStatusEx);
        else
        {
            /** @todo now fails in NT4; do we care? */
            VBoxServiceError("VBoxStatsInit: KERNEL32.GlobalMemoryStatusEx not found!!\n");
            return VERR_NOT_IMPLEMENTED;
        }
    }
    /* GetPerformanceInfo is xp and up, so load it dynamically */
    hMod = LoadLibrary("PSAPI.DLL");
    if (hMod)
    {
        *(uintptr_t *)&gCtx.pfnGetPerformanceInfo = (uintptr_t)GetProcAddress(hMod, "GetPerformanceInfo");
        if (gCtx.pfnGetPerformanceInfo)
            VBoxServiceVerbose(3, "VBoxStatsInit: gCtx.pfnGetPerformanceInfo= %x\n", gCtx.pfnGetPerformanceInfo);
        /* failure is not fatal */
    }
#endif /* RT_OS_WINDOWS */

    return VINF_SUCCESS;
}


static void VBoxServiceVMStatsReport(VBOXSTATSCONTEXT *pCtx)
{
    SYSTEM_INFO systemInfo;
    PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION pProcInfo;
    MEMORYSTATUSEX memStatus;
    VMMDevReportGuestStats req;
    uint32_t cbStruct;
    DWORD    cbReturned;
    HANDLE   gVBoxDriver = pCtx->pEnv->hDriver;

    Assert(gCtx.pfnGlobalMemoryStatusEx && gCtx.pfnNtQuerySystemInformation);
    if (    !gCtx.pfnGlobalMemoryStatusEx
        ||  !gCtx.pfnNtQuerySystemInformation)
        return;

    vmmdevInitRequest(&req.header, VMMDevReq_ReportGuestStats);

    /* Query and report guest statistics */
    GetSystemInfo(&systemInfo);

    memStatus.dwLength = sizeof(memStatus);
    gCtx.pfnGlobalMemoryStatusEx(&memStatus);

    req.guestStats.u32PageSize          = systemInfo.dwPageSize;
    req.guestStats.u32PhysMemTotal      = (uint32_t)(memStatus.ullTotalPhys / systemInfo.dwPageSize);
    req.guestStats.u32PhysMemAvail      = (uint32_t)(memStatus.ullAvailPhys / systemInfo.dwPageSize);
    /* The current size of the committed memory limit, in bytes. This is physical memory plus the size of the page file, minus a small overhead. */
    req.guestStats.u32PageFileSize      = (uint32_t)(memStatus.ullTotalPageFile / systemInfo.dwPageSize) - req.guestStats.u32PhysMemTotal;
    req.guestStats.u32MemoryLoad        = memStatus.dwMemoryLoad;
    req.guestStats.u32PhysMemBalloon    = VBoxMemBalloonQuerySize() * (_1M/systemInfo.dwPageSize);    /* was in megabytes */
    req.guestStats.u32StatCaps          = VBOX_GUEST_STAT_PHYS_MEM_TOTAL | VBOX_GUEST_STAT_PHYS_MEM_AVAIL | VBOX_GUEST_STAT_PAGE_FILE_SIZE | VBOX_GUEST_STAT_MEMORY_LOAD | VBOX_GUEST_STAT_PHYS_MEM_BALLOON;

    if (gCtx.pfnGetPerformanceInfo)
    {
        PERFORMANCE_INFORMATION perfInfo;

        if (gCtx.pfnGetPerformanceInfo(&perfInfo, sizeof(perfInfo)))
        {
            req.guestStats.u32Processes         = perfInfo.ProcessCount;
            req.guestStats.u32Threads           = perfInfo.ThreadCount;
            req.guestStats.u32Handles           = perfInfo.HandleCount;
            req.guestStats.u32MemCommitTotal    = perfInfo.CommitTotal;     /* already in pages */
            req.guestStats.u32MemKernelTotal    = perfInfo.KernelTotal;     /* already in pages */
            req.guestStats.u32MemKernelPaged    = perfInfo.KernelPaged;     /* already in pages */
            req.guestStats.u32MemKernelNonPaged = perfInfo.KernelNonpaged;  /* already in pages */
            req.guestStats.u32MemSystemCache    = perfInfo.SystemCache;     /* already in pages */
            req.guestStats.u32StatCaps |= VBOX_GUEST_STAT_PROCESSES | VBOX_GUEST_STAT_THREADS | VBOX_GUEST_STAT_HANDLES | VBOX_GUEST_STAT_MEM_COMMIT_TOTAL | VBOX_GUEST_STAT_MEM_KERNEL_TOTAL | VBOX_GUEST_STAT_MEM_KERNEL_PAGED | VBOX_GUEST_STAT_MEM_KERNEL_NONPAGED | VBOX_GUEST_STAT_MEM_SYSTEM_CACHE;
        }
        else
            Log(("GetPerformanceInfo failed with %d\n", GetLastError()));
    }

    /* Query CPU load information */
    cbStruct = systemInfo.dwNumberOfProcessors*sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION);
    pProcInfo = (PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)malloc(cbStruct);
    Assert(pProcInfo);
    if (!pProcInfo)
        return;

    /* Unfortunately GetSystemTimes is XP SP1 and up only, so we need to use the semi-undocumented NtQuerySystemInformation */
    NTSTATUS rc = gCtx.pfnNtQuerySystemInformation(SystemProcessorPerformanceInformation, pProcInfo, cbStruct, &cbReturned);
    if (    !rc
        &&  cbReturned == cbStruct)
    {
        if (gCtx.ullLastCpuLoad_Kernel == 0)
        {
            /* first time */
            gCtx.ullLastCpuLoad_Idle    = pProcInfo->IdleTime.QuadPart;
            gCtx.ullLastCpuLoad_Kernel  = pProcInfo->KernelTime.QuadPart;
            gCtx.ullLastCpuLoad_User    = pProcInfo->UserTime.QuadPart;

            Sleep(250);

            rc = gCtx.pfnNtQuerySystemInformation(SystemProcessorPerformanceInformation, pProcInfo, cbStruct, &cbReturned);
            Assert(!rc);
        }

        uint64_t deltaIdle    = (pProcInfo->IdleTime.QuadPart - gCtx.ullLastCpuLoad_Idle);
        uint64_t deltaKernel  = (pProcInfo->KernelTime.QuadPart - gCtx.ullLastCpuLoad_Kernel);
        uint64_t deltaUser    = (pProcInfo->UserTime.QuadPart - gCtx.ullLastCpuLoad_User);
        deltaKernel          -= deltaIdle;  /* idle time is added to kernel time */
        uint64_t ullTotalTime = deltaIdle + deltaKernel + deltaUser;

        req.guestStats.u32CpuLoad_Idle      = (uint32_t)(deltaIdle  * 100 / ullTotalTime);
        req.guestStats.u32CpuLoad_Kernel    = (uint32_t)(deltaKernel* 100 / ullTotalTime);
        req.guestStats.u32CpuLoad_User      = (uint32_t)(deltaUser  * 100 / ullTotalTime);

        req.guestStats.u32StatCaps |= VBOX_GUEST_STAT_CPU_LOAD_IDLE | VBOX_GUEST_STAT_CPU_LOAD_KERNEL | VBOX_GUEST_STAT_CPU_LOAD_USER;

        gCtx.ullLastCpuLoad_Idle    = pProcInfo->IdleTime.QuadPart;
        gCtx.ullLastCpuLoad_Kernel  = pProcInfo->KernelTime.QuadPart;
        gCtx.ullLastCpuLoad_User    = pProcInfo->UserTime.QuadPart;
    }

    for (uint32_t i=0;i<systemInfo.dwNumberOfProcessors;i++)
    {
        req.guestStats.u32CpuId = i;

        if (DeviceIoControl(gVBoxDriver, VBOXGUEST_IOCTL_VMMREQUEST(req.header.size), &req, req.header.size, &req, req.header.size, &cbReturned, NULL))
        {
            Log(("VBoxStatsReportStatistics: new statistics reported successfully!\n"));
        }
        else
            Log(("VBoxStatsReportStatistics: DeviceIoControl (stats report) failed with %d\n", GetLastError()));
    }

    free(pProcInfo);
}

/** @copydoc VBOXSERVICE::pfnWorker */
DECLCALLBACK(int) VBoxServiceVMStatsWorker(bool volatile *pfShutdown)
{
    VBOXSTATSCONTEXT *pCtx = (VBOXSTATSCONTEXT *)pInstance;
    HANDLE gVBoxDriver = pCtx->pEnv->hDriver;
    bool fTerminate = false;
    VBoxGuestFilterMaskInfo maskInfo;
    DWORD cbReturned;

    int rc = VINF_SUCCESS;

    /*
     * Tell the control thread that it can continue
     * spawning services.
     */
    RTThreadUserSignal(RTThreadSelf());

    maskInfo.u32OrMask = VMMDEV_EVENT_STATISTICS_INTERVAL_CHANGE_REQUEST;
    maskInfo.u32NotMask = 0;
    if (DeviceIoControl (gVBoxDriver, VBOXGUEST_IOCTL_CTL_FILTER_MASK, &maskInfo, sizeof (maskInfo), NULL, 0, &cbReturned, NULL))
    {
        Log(("VBoxStatsThread: DeviceIOControl(CtlMask - or) succeeded\n"));
    }
    else
    {
        Log(("VBoxStatsThread: DeviceIOControl(CtlMask) failed, SeamlessChangeThread exited\n"));
        return 0;
    }

    /*
     * Now enter the loop retrieving runtime data continuously.
     */
    for (;;)
    {
        /* Report statistics to the host */
        if (gCtx.pfnNtQuerySystemInformation)
        {
            VBoxServiceVMStatsReport();
        }

        /*
         * Block for a while.
         *
         * The event semaphore takes care of ignoring interruptions and it
         * allows us to implement service wakeup later.
         */
        if (*pfShutdown)
            break;
        int rc2 = RTSemEventMultiWait(g_VMStatEvent, g_VMStatsInterval);
        if (*pfShutdown)
            break;
        if (rc2 != VERR_TIMEOUT && RT_FAILURE(rc2))
        {
            VBoxServiceError("RTSemEventMultiWait failed; rc2=%Rrc\n", rc2);
            rc = rc2;
            break;
        }
    }

    maskInfo.u32OrMask = 0;
    maskInfo.u32NotMask = VMMDEV_EVENT_STATISTICS_INTERVAL_CHANGE_REQUEST;
    if (DeviceIoControl (gVBoxDriver, VBOXGUEST_IOCTL_CTL_FILTER_MASK, &maskInfo, sizeof (maskInfo), NULL, 0, &cbReturned, NULL))
    {
        Log(("VBoxStatsThread: DeviceIOControl(CtlMask - not) succeeded\n"));
    }
    else
    {
        Log(("VBoxStatsThread: DeviceIOControl(CtlMask) failed\n"));
    }

    RTSemEventMultiDestroy(g_VMStatsEvent);
    g_VMStatsEvent = NIL_RTSEMEVENTMULTI;

    Log(("VBoxStatsThread: finished statistics change request thread\n"));
    return 0;
}


/** @copydoc VBOXSERVICE::pfnTerm */
static DECLCALLBACK(void) VBoxServiceVMStatsTerm(void)
{
    VBoxServiceVerbose(3, "VBoxServiceVMStatsTerm\n");
    return;
}


/** @copydoc VBOXSERVICE::pfnStop */
static DECLCALLBACK(void) VBoxServiceVMStatsStop(void)
{
    RTSemEventMultiSignal(g_VMStatsEvent);
}


/**
 * The 'vminfo' service description.
 */
VBOXSERVICE g_VMStatistics =
{
    /* pszName. */
    "vmstats",
    /* pszDescription. */
    "Virtual Machine Statistics",
    /* pszUsage. */
    "[--vmstats-interval <ms>]"
    ,
    /* pszOptions. */
    "    --vmstats-interval   Specifies the interval at which to retrieve the\n"
    "                        VM statistcs. The default is 10000 ms.\n"
    ,
    /* methods */
    VBoxServiceVMStatsPreInit,
    VBoxServiceVMStatsOption,
    VBoxServiceVMStatsInit,
    VBoxServiceVMStatsWorker,
    VBoxServiceVMStatsStop,
    VBoxServiceVMStatsTerm
};
