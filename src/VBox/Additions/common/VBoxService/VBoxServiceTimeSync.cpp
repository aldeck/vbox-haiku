/* $Id$ */
/** @file
 * VBoxService - Guest Additions TimeSync Service.
 */

/*
 * Copyright (C) 2007 Sun Microsystems, Inc.
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


/** @page pg_vboxservice_timesync       The Time Sync Service
 *
 * The time sync service plays along with the Time Manager (TM) in the VMM
 * to keep the guest time accurate using the host machine as reference.
 * TM will try its best to make sure all timer ticks gets delivered so that
 * there isn't normally any need to adjust the guest time.
 *
 * There are three normal (= acceptable) cases:
 *      -# When the service starts up. This is because ticks and such might
 *         be lost during VM and OS startup. (Need to figure out exactly why!)
 *      -# When the TM is unable to deliver all the ticks and swallows a
 *         backlog of ticks. The threshold for this is configurable with
 *         a default of 60 seconds.
 *      -# The time is adjusted on the host. This can be caused manually by
 *         the user or by some time sync daemon (NTP, LAN server, etc.).
 *
 * There are a number of very odd case where adjusting is needed. Here
 * are some of them:
 *      -# Timer device emulation inaccurancies (like rounding).
 *      -# Inaccurancies in time source VirtualBox uses.
 *      -# The Guest and/or Host OS doesn't perform proper time keeping. This
 *         come about as a result of OS and/or hardware issues.
 *
 * The TM is our source for the host time and will make adjustments for
 * current timer delivery lag. The simplistic approach taken by TM is to
 * adjust the host time by the current guest timer delivery lag, meaning that
 * if the guest is behind 1 second with PIT/RTC/++ ticks this should be reflected
 * in the guest wall time as well.
 *
 * Now, there is any amount of trouble we can cause by changing the time.
 * Most applications probably uses the wall time when they need to measure
 * things. A walltime that is being juggled about every so often, even if just
 * a little bit, could occationally upset these measurements by for instance
 * yielding negative results.
 *
 * This bottom line here is that the time sync service isn't really supposed
 * to do anything and will try avoid having to do anything when possible.
 *
 * The implementation uses the latency it takes to query host time as the
 * absolute maximum precision to avoid messing up under timer tick catchup
 * and/or heavy host/guest load. (Rational is that a *lot* of stuff may happen
 * on our way back from ring-3 and TM/VMMDev since we're taking the route
 * thru the inner EM loop with it's force action processing.)
 *
 * But this latency has to be measured from our perspective, which means it
 * could just as easily come out as 0. (OS/2 and Windows guest only updates
 * the current time when the timer ticks for instance.) The good thing is
 * that this isn't really a problem since we won't ever do anything unless
 * the drift is noticable.
 *
 * It now boils down to these three (configuration) factors:
 *  -# g_TimesyncMinAdjust - The minimum drift we will ever bother with.
 *  -# g_TimesyncLatencyFactor - The factor we multiply the latency by to
 *     calculate the dynamic minimum adjust factor.
 *  -# g_TimesyncMaxLatency - When to start discarding the data as utterly
 *     useless and take a rest (someone is too busy to give us good data).
 *  -# g_TimeSyncSetThreshold - The threshold at which we will just set the time
 *     instead of trying to adjust it (milliseconds).
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#ifdef RT_OS_WINDOWS
# include <Windows.h>
# include <winbase.h> /** @todo r=bird: Why is this here? Windows.h should include winbase.h... */
#else
# include <unistd.h>
# include <errno.h>
# include <time.h>
# include <sys/time.h>
#endif

#include <iprt/thread.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/time.h>
#include <iprt/assert.h>
#include <VBox/VBoxGuestLib.h>
#include "VBoxServiceInternal.h"


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** The timesync interval (millseconds). */
uint32_t g_TimeSyncInterval = 0;
/**
 * @see pg_vboxservice_timesync
 *
 * @remark  OS/2: There is either a 1 second resolution on the DosSetDateTime
 *                API or a but in the settimeofday implementation. Thus, don't
 *                bother unless there is at least a 1 second drift.
 */
#ifdef RT_OS_OS2
static uint32_t g_TimeSyncMinAdjust = 1000;
#else
static uint32_t g_TimeSyncMinAdjust = 100;
#endif
/** @see pg_vboxservice_timesync */
static uint32_t g_TimeSyncLatencyFactor = 8;
/** @see pg_vboxservice_timesync */
static uint32_t g_TimeSyncMaxLatency = 250;
/** @see pg_vboxservice_timesync */
static uint32_t g_TimeSyncSetThreshold = 20*60*1000;
/** Whether the next adjustment should just set the time instead of trying to
 * adjust it. This is used to implement --timesync-set-start.  */
static bool volatile g_fTimeSyncSetNext = false;

/** Current error count. Used to knowing when to bitch and when not to. */
static uint32_t g_cTimeSyncErrors = 0;

/** The semaphore we're blocking on. */
static RTSEMEVENTMULTI g_TimeSyncEvent = NIL_RTSEMEVENTMULTI;

#ifdef RT_OS_WINDOWS
/** Process token. */
static HANDLE g_hTokenProcess = NULL;
/** Old token privileges. */
static TOKEN_PRIVILEGES g_TkOldPrivileges;
/** Backup values for time adjustment. */
static DWORD g_dwWinTimeAdjustment;
static DWORD g_dwWinTimeIncrement;
static BOOL g_bWinTimeAdjustmentDisabled;
#endif


/** @copydoc VBOXSERVICE::pfnPreInit */
static DECLCALLBACK(int) VBoxServiceTimeSyncPreInit(void)
{
    return VINF_SUCCESS;
}


/** @copydoc VBOXSERVICE::pfnOption */
static DECLCALLBACK(int) VBoxServiceTimeSyncOption(const char **ppszShort, int argc, char **argv, int *pi)
{
    int rc = -1;
    if (ppszShort)
        /* no short options */;
    else if (!strcmp(argv[*pi], "--timesync-interval"))
        rc = VBoxServiceArgUInt32(argc, argv, "", pi,
                                  &g_TimeSyncInterval, 1, UINT32_MAX - 1);
    else if (!strcmp(argv[*pi], "--timesync-min-adjust"))
        rc = VBoxServiceArgUInt32(argc, argv, "", pi,
                                  &g_TimeSyncMinAdjust, 0, 3600000);
    else if (!strcmp(argv[*pi], "--timesync-latency-factor"))
        rc = VBoxServiceArgUInt32(argc, argv, "", pi,
                                  &g_TimeSyncLatencyFactor, 1, 1024);
    else if (!strcmp(argv[*pi], "--timesync-max-latency"))
        rc = VBoxServiceArgUInt32(argc, argv, "", pi,
                                  &g_TimeSyncMaxLatency, 1, 3600000);
    else if (!strcmp(argv[*pi], "--timesync-set-threshold"))
        rc = VBoxServiceArgUInt32(argc, argv, "", pi,
                                  &g_TimeSyncSetThreshold, 0, 7*24*60*1000); /* a week */
    else if (!strcmp(argv[*pi], "--timesync-set-start"))
    {
        g_fTimeSyncSetNext = true;
        rc = VINF_SUCCESS;
    }

    return rc;
}


/** @copydoc VBOXSERVICE::pfnInit */
static DECLCALLBACK(int) VBoxServiceTimeSyncInit(void)
{
    /*
     * If not specified, find the right interval default.
     * Then create the event sem to block on.
     */
    if (!g_TimeSyncInterval)
        g_TimeSyncInterval = g_DefaultInterval * 1000;
    if (!g_TimeSyncInterval)
        g_TimeSyncInterval = 10 * 1000;

    int rc = RTSemEventMultiCreate(&g_TimeSyncEvent);
    AssertRC(rc);
#ifdef RT_OS_WINDOWS
    if (RT_SUCCESS(rc))
    {
        /*
         * Adjust priviledges of this process so we can make system time adjustments.
         */
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &g_hTokenProcess))
        {
            TOKEN_PRIVILEGES tkPriv;
            RT_ZERO(tkPriv);
            tkPriv.PrivilegeCount = 1;
            tkPriv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            if (LookupPrivilegeValue(NULL, SE_SYSTEMTIME_NAME, &tkPriv.Privileges[0].Luid))
            {
                DWORD cbRet = sizeof(g_TkOldPrivileges);
                if (AdjustTokenPrivileges(g_hTokenProcess, FALSE, &tkPriv, sizeof(TOKEN_PRIVILEGES), &g_TkOldPrivileges, &cbRet))
                    rc = VINF_SUCCESS;
                else
                {
                    DWORD dwErr = GetLastError();
                    rc = RTErrConvertFromWin32(dwErr);
                    VBoxServiceError("Adjusting token privileges (SE_SYSTEMTIME_NAME) failed with status code %u/%Rrc!\n", dwErr, rc);
                }
            }
            else
            {
                DWORD dwErr = GetLastError();
                rc = RTErrConvertFromWin32(dwErr);
                VBoxServiceError("Looking up token privileges (SE_SYSTEMTIME_NAME) failed with status code %u/%Rrc!\n", dwErr, rc);
            }
            if (RT_FAILURE(rc))
            {
                CloseHandle(g_hTokenProcess);
                g_hTokenProcess = NULL;
            }
        }
        else
        {
            DWORD dwErr = GetLastError();
            rc = RTErrConvertFromWin32(dwErr);
            VBoxServiceError("Opening process token (SE_SYSTEMTIME_NAME) failed with status code %u/%Rrc!\n", dwErr, rc);
            g_hTokenProcess = NULL;
        }
    }

    if (GetSystemTimeAdjustment(&g_dwWinTimeAdjustment, &g_dwWinTimeIncrement, &g_bWinTimeAdjustmentDisabled))
        VBoxServiceVerbose(3, "Windows time adjustment: Initially %ld (100ns) units per %ld (100 ns) units interval, disabled=%d\n",
                           g_dwWinTimeAdjustment, g_dwWinTimeIncrement, g_bWinTimeAdjustmentDisabled ? 1 : 0);
    else
    {
        DWORD dwErr = GetLastError();
        rc = RTErrConvertFromWin32(dwErr);
        VBoxServiceError("Could not get time adjustment values! Last error: %ld!\n", dwErr);
    }
#endif /* RT_OS_WINDOWS */

    return rc;
}


/**
 * Try adjust the time using adjtime or similar.
 *
 * @returns true on success, false on failure.
 *
 * @param   pDrift          The time adjustment.
 */
static bool VBoxServiceTimeSyncAdjust(PCRTTIMESPEC pDrift)
{
#ifdef RT_OS_WINDOWS
/** @todo r=bird: NT4 doesn't have GetSystemTimeAdjustment. */
    DWORD dwWinTimeAdjustment, dwWinNewTimeAdjustment, dwWinTimeIncrement;
    BOOL  fWinTimeAdjustmentDisabled;
    if (GetSystemTimeAdjustment(&dwWinTimeAdjustment, &dwWinTimeIncrement, &fWinTimeAdjustmentDisabled))
    {
        DWORD dwDiffMax = g_dwWinTimeAdjustment * 0.50;
        DWORD dwDiffNew =   dwWinTimeAdjustment * 0.10;

        if (RTTimeSpecGetMilli(pDrift) > 0)
        {
            dwWinNewTimeAdjustment = dwWinTimeAdjustment + dwDiffNew;
            if (dwWinNewTimeAdjustment > (g_dwWinTimeAdjustment + dwDiffMax))
            {
                dwWinNewTimeAdjustment = g_dwWinTimeAdjustment + dwDiffMax;
                dwDiffNew = dwDiffMax;
            }
        }
        else
        {
            dwWinNewTimeAdjustment = dwWinTimeAdjustment - dwDiffNew;
            if (dwWinNewTimeAdjustment < (g_dwWinTimeAdjustment - dwDiffMax))
            {
                dwWinNewTimeAdjustment = g_dwWinTimeAdjustment - dwDiffMax;
                dwDiffNew = dwDiffMax;
            }
        }

        VBoxServiceVerbose(3, "Windows time adjustment: Drift=%lldms\n", RTTimeSpecGetMilli(pDrift));
        VBoxServiceVerbose(3, "Windows time adjustment: OrgTA=%ld, CurTA=%ld, NewTA=%ld, DiffNew=%ld, DiffMax=%ld\n",
                           g_dwWinTimeAdjustment, dwWinTimeAdjustment, dwWinNewTimeAdjustment, dwDiffNew, dwDiffMax);
        if (SetSystemTimeAdjustment(dwWinNewTimeAdjustment, FALSE /* Periodic adjustments enabled. */))
        {
            g_cTimeSyncErrors = 0;
            return true;
        }

        if (g_cTimeSyncErrors++ < 10)
             VBoxServiceError("SetSystemTimeAdjustment failed, error=%u\n", GetLastError());
    }
    else if (g_cTimeSyncErrors++ < 10)
        VBoxServiceError("GetSystemTimeAdjustment failed, error=%ld\n", GetLastError());

#elif defined(RT_OS_OS2)
    /* No API for doing gradual time adjustments. */

#else /* PORTME */
    /*
     * Try use adjtime(), most unix-like systems have this.
     */
    struct timeval tv;
    RTTimeSpecGetTimeval(pDrift, &tv);
    if (adjtime(&tv, NULL) == 0)
    {
        if (g_cVerbosity >= 1)
            VBoxServiceVerbose(1, "adjtime by %RDtimespec\n", pDrift);
        g_cTimeSyncErrors = 0;
        return true;
    }
#endif

    /* failed */
    return false;
}


/**
 * Cancels any pending time adjustment.
 *
 * Called when we've caught up and before calls to VBoxServiceTimeSyncSet.
 */
static void VBoxServiceTimeSyncCancelAdjust(void)
{
#ifdef RT_OS_WINDOWS
    if (SetSystemTimeAdjustment(0, TRUE /* Periodic adjustments disabled. */))
        VBoxServiceVerbose(3, "Windows Time Adjustment is now disabled.\n");
    else if (g_cTimeSyncErrors++ < 10)
        VBoxServiceError("SetSystemTimeAdjustment(,disable) failed, error=%u\n", GetLastError());
#endif /* !RT_OS_WINDOWS */
}


/**
 * Try adjust the time using adjtime or similar.
 *
 * @returns true on success, false on failure.
 *
 * @param   pDrift              The time adjustment.
 * @param   pHostNow            The host time at the time of the host query.
 *                              REMOVE THIS ARGUMENT!
 */
static void VBoxServiceTimeSyncSet(PCRTTIMESPEC pDrift, PCRTTIMESPEC pHostNow)
{
    /*
     * Query the current time, add the adjustment, then try it.
     */
#ifdef RT_OS_WINDOWS
/** @todo r=bird: Get current time and add the adjustment, the host time is
 *                stale by now. */
    FILETIME ft;
    RTTimeSpecGetNtFileTime(pHostNow, &ft);
    SYSTEMTIME st;
    if (FileTimeToSystemTime(&ft, &st))
    {
        if (!SetSystemTime(&st))
            VBoxServiceError("SetSystemTime failed, error=%u\n", GetLastError());
    }
    else
        VBoxServiceError("Cannot convert system times, error=%u\n", GetLastError());

#else  /* !RT_OS_WINDOWS */
    struct timeval tv;
    errno = 0;
    if (!gettimeofday(&tv, NULL))
    {
        RTTIMESPEC Tmp;
        RTTimeSpecAdd(RTTimeSpecSetTimeval(&Tmp, &tv), pDrift);
        if (!settimeofday(RTTimeSpecGetTimeval(&Tmp, &tv), NULL))
        {
            char    sz[64];
            RTTIME  Time;
            if (g_cVerbosity >= 1)
                VBoxServiceVerbose(1, "settimeofday to %s\n",
                                   RTTimeToString(RTTimeExplode(&Time, &Tmp), sz, sizeof(sz)));
# ifdef DEBUG
            if (g_cVerbosity >= 3)
                VBoxServiceVerbose(2, "       new time %s\n",
                                   RTTimeToString(RTTimeExplode(&Time, RTTimeNow(&Tmp)), sz, sizeof(sz)));
# endif
            g_cTimeSyncErrors = 0;
        }
        else if (g_cTimeSyncErrors++ < 10)
            VBoxServiceError("settimeofday failed; errno=%d: %s\n", errno, strerror(errno));
    }
    else if (g_cTimeSyncErrors++ < 10)
        VBoxServiceError("gettimeofday failed; errno=%d: %s\n", errno, strerror(errno));
#endif /* !RT_OS_WINDOWS */
}


/** @copydoc VBOXSERVICE::pfnWorker */
DECLCALLBACK(int) VBoxServiceTimeSyncWorker(bool volatile *pfShutdown)
{
    RTTIME Time;
    char sz[64];
    int rc = VINF_SUCCESS;

    /*
     * Tell the control thread that it can continue spawning services.
     */
    RTThreadUserSignal(RTThreadSelf());

    /*
     * The Work Loop.
     */
    for (;;)
    {
        /*
         * Try get a reliable time reading.
         */
        int cTries = 3;
        do
        {
            /* query it. */
            RTTIMESPEC GuestNow0, GuestNow, HostNow;
            RTTimeNow(&GuestNow0);
            int rc2 = VbglR3GetHostTime(&HostNow);
            if (RT_FAILURE(rc2))
            {
                if (g_cTimeSyncErrors++ < 10)
                    VBoxServiceError("VbglR3GetHostTime failed; rc2=%Rrc\n", rc2);
                break;
            }
            RTTimeNow(&GuestNow);

            /* calc latency and check if it's ok. */
            RTTIMESPEC GuestElapsed = GuestNow;
            RTTimeSpecSub(&GuestElapsed, &GuestNow0);
            if ((uint32_t)RTTimeSpecGetMilli(&GuestElapsed) < g_TimeSyncMaxLatency)
            {
                /*
                 * Calculate the adjustment threshold and the current drift.
                 */
                uint32_t MinAdjust = RTTimeSpecGetMilli(&GuestElapsed) * g_TimeSyncLatencyFactor;
                if (MinAdjust < g_TimeSyncMinAdjust)
                    MinAdjust = g_TimeSyncMinAdjust;

                RTTIMESPEC Drift = HostNow;
                RTTimeSpecSub(&Drift, &GuestNow);
                if (RTTimeSpecGetMilli(&Drift) < 0)
                    MinAdjust += g_TimeSyncMinAdjust; /* extra buffer against moving time backwards. */

                RTTIMESPEC AbsDrift = Drift;
                RTTimeSpecAbsolute(&AbsDrift);
                if (g_cVerbosity >= 3)
                {
                    VBoxServiceVerbose(3, "Host:    %s    (MinAdjust: %RU32 ms)\n",
                                       RTTimeToString(RTTimeExplode(&Time, &HostNow), sz, sizeof(sz)), MinAdjust);
                    VBoxServiceVerbose(3, "Guest: - %s => %RDtimespec drift\n",
                                       RTTimeToString(RTTimeExplode(&Time, &GuestNow), sz, sizeof(sz)),
                                       &Drift);
                }

                uint32_t AbsDriftMilli = RTTimeSpecGetMilli(&AbsDrift);
                if (AbsDriftMilli > MinAdjust)
                {
                    /*
                     * Ok, the drift is above the threshold.
                     *
                     * Try a gradual adjustment first, if that fails or the drift is
                     * too big, fall back on just setting the time.
                     */

                    if (    AbsDriftMilli > g_TimeSyncSetThreshold
                        ||  g_fTimeSyncSetNext
                        ||  !VBoxServiceTimeSyncAdjust(&Drift))
                    {
                        VBoxServiceTimeSyncCancelAdjust();
                        VBoxServiceTimeSyncSet(&Drift, &HostNow);
                    }
                }
                else
                    VBoxServiceTimeSyncCancelAdjust();
                break;
            }
            VBoxServiceVerbose(3, "%RDtimespec: latency too high (%RDtimespec) sleeping 1s\n", GuestElapsed);
            RTThreadSleep(1000);
        } while (--cTries > 0);

        /* Clear the set-next/set-start flag. */
        g_fTimeSyncSetNext = false;

        /*
         * Block for a while.
         *
         * The event semaphore takes care of ignoring interruptions and it
         * allows us to implement service wakeup later.
         */
        if (*pfShutdown)
            break;
        int rc2 = RTSemEventMultiWait(g_TimeSyncEvent, g_TimeSyncInterval);
        if (*pfShutdown)
            break;
        if (rc2 != VERR_TIMEOUT && RT_FAILURE(rc2))
        {
            VBoxServiceError("RTSemEventMultiWait failed; rc2=%Rrc\n", rc2);
            rc = rc2;
            break;
        }
    }

    RTSemEventMultiDestroy(g_TimeSyncEvent);
    g_TimeSyncEvent = NIL_RTSEMEVENTMULTI;
    return rc;
}


/** @copydoc VBOXSERVICE::pfnStop */
static DECLCALLBACK(void) VBoxServiceTimeSyncStop(void)
{
    RTSemEventMultiSignal(g_TimeSyncEvent);
}


/** @copydoc VBOXSERVICE::pfnTerm */
static DECLCALLBACK(void) VBoxServiceTimeSyncTerm(void)
{
#ifdef RT_OS_WINDOWS
    /*
     * Restore the SE_SYSTEMTIME_NAME token privileges (if init succeeded).
     */
    if (g_hTokenProcess)
    {
        if (!AdjustTokenPrivileges(g_hTokenProcess, FALSE, &g_TkOldPrivileges, sizeof(TOKEN_PRIVILEGES), NULL, NULL))
        {
            DWORD dwErr = GetLastError();
            VBoxServiceError("Restoring token privileges (SE_SYSTEMTIME_NAME) failed with code %u!\n", dwErr);
        }
        CloseHandle(g_hTokenProcess);
        g_hTokenProcess = NULL;
    }
#endif /* !RT_OS_WINDOWS */

    if (g_TimeSyncEvent != NIL_RTSEMEVENTMULTI)
    {
        RTSemEventMultiDestroy(g_TimeSyncEvent);
        g_TimeSyncEvent = NIL_RTSEMEVENTMULTI;
    }
}


/**
 * The 'timesync' service description.
 */
VBOXSERVICE g_TimeSync =
{
    /* pszName. */
    "timesync",
    /* pszDescription. */
    "Time synchronization",
    /* pszUsage. */
    "[--timesync-interval <ms>] [--timesync-min-adjust <ms>] "
    "[--timesync-latency-factor <x>] [--timesync-max-latency <ms>]"
    "[--timesync-set-threshold <ms>] [--timesync-set-start]"
    ,
    /* pszOptions. */
    "    --timesync-interval Specifies the interval at which to synchronize the\n"
    "                        time with the host. The default is 10000 ms.\n"
    "    --timesync-min-adjust\n"
    "                        The minimum absolute drift value measured in\n"
    "                        milliseconds to make adjustments for.\n"
    "                        The default is 1000 ms on OS/2 and 100 ms elsewhere.\n"
    "    --timesync-latency-factor\n"
    "                        The factor to multiply the time query latency with to\n"
    "                        calculate the dynamic minimum adjust time.\n"
    "                        The default is 8 times.\n"
    "    --timesync-max-latency\n"
    "                        The max host timer query latency to accept.\n"
    "                        The default is 250 ms.\n"
    "    --timesync-set-threshold\n"
    "                        The absolute drift threshold, given as milliseconds,\n"
    "                        where to start setting the time instead of trying to\n"
    "                        adjust it. The default is 20 min.\n"
    "    --timesync-set-start\n"
    "                        Set the time when starting the time sync service.\n"
    ,
    /* methods */
    VBoxServiceTimeSyncPreInit,
    VBoxServiceTimeSyncOption,
    VBoxServiceTimeSyncInit,
    VBoxServiceTimeSyncWorker,
    VBoxServiceTimeSyncStop,
    VBoxServiceTimeSyncTerm
};

