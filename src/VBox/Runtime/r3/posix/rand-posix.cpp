/* $Id$ */
/** @file
 * innotek Portable Runtime - Random Numbers and Byte Streams, POSIX.
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
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <fcntl.h>
#ifdef _MSC_VER
# include <io.h>
# include <stdio.h>
#else
# include <unistd.h>
# include <sys/time.h>
#endif

#include <iprt/rand.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include "internal/rand.h"


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** File handle of /dev/random. */
static int g_fhDevRandom = -1;


void rtRandLazyInitNative(void)
{
    if (g_fhDevRandom != -1)
        return;

    int fh = open("/dev/urandom", O_RDONLY);
    if (fh <= 0)
        fh = open("/dev/random", O_RDONLY | O_NONBLOCK);
    if (fh >= 0)
    {
        fcntl(fh, F_SETFD, FD_CLOEXEC);
        g_fhDevRandom = fh;
    }
}


int rtRandGenBytesNative(void *pv, size_t cb)
{
    int fh = g_fhDevRandom;
    if (fh == -1)
        return VERR_NOT_SUPPORTED;

    ssize_t cbRead = read(fh, pv, cb);
    if ((size_t)cbRead != cb)
    {
        /* 
         * Use the fallback for the remainder if /dev/urandom / /dev/random 
         * is out to lunch. 
         */
        if (cbRead <= 0)
            rtRandGenBytesFallback(pv, cb);
        else 
        {
            AssertRelease((size_t)cbRead < cb);
            rtRandGenBytesFallback((uint8_t *)pv + cbRead, cb - cbRead);
        }
    }
    return VINF_SUCCESS;
}

