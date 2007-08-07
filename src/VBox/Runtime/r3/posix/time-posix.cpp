/* $Id$ */
/** @file
 * innotek Portable Runtime - Time, POSIX.
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP RTLOGGROUP_TIME
#define RTTIME_INCL_TIMEVAL
#include <sys/time.h>
#include <time.h>

#include <iprt/time.h>
#include "internal/time.h"


DECLINLINE(uint64_t) rtTimeGetSystemNanoTS(void)
{
#if defined(CLOCK_MONOTONIC) && !defined(RT_OS_L4) && !defined(RT_OS_OS2)
    /* check monotonic clock first. */
    static bool s_fMonoClock = true;
    if (s_fMonoClock)
    {
        struct timespec ts;
        if (!clock_gettime(CLOCK_MONOTONIC, &ts))
            return (uint64_t)ts.tv_sec * (uint64_t)(1000 * 1000 * 1000)
                 + ts.tv_nsec;
        s_fMonoClock = false;
    }
#endif

    /* fallback to gettimeofday(). */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec  * (uint64_t)(1000 * 1000 * 1000)
         + (uint64_t)(tv.tv_usec * 1000);
}


/**
 * Gets the current nanosecond timestamp.
 *
 * This differs from RTTimeNanoTS in that it will use system APIs and not do any
 * resolution or performance optimizations.
 *
 * @returns nanosecond timestamp.
 */
RTDECL(uint64_t) RTTimeSystemNanoTS(void)
{
    return rtTimeGetSystemNanoTS();
}


/**
 * Gets the current millisecond timestamp.
 *
 * This differs from RTTimeNanoTS in that it will use system APIs and not do any
 * resolution or performance optimizations.
 *
 * @returns millisecond timestamp.
 */
RTDECL(uint64_t) RTTimeSystemMilliTS(void)
{
    return rtTimeGetSystemNanoTS() / 1000000;
}

