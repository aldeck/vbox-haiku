/* $Id$ */
/** @file
 * innotek Portable Runtime - System, POSIX.
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
#include <iprt/system.h>
#include <iprt/assert.h>

#include <unistd.h>
#if !defined(RT_OS_SOLARIS)
# include <sys/sysctl.h>
#endif


/**
 * Gets the number of logical (not physical) processors in the system.
 *
 * @returns Number of logical processors in the system.
 */
RTR3DECL(unsigned) RTSystemProcessorGetCount(void)
{
    int cCpus; NOREF(cCpus);

    /*
     * The sysconf way (linux and others).
     */
#ifdef _SC_NPROCESSORS_ONLN
    cCpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cCpus >= 1)
        return cCpus;
#endif

    /*
     * The BSD 4.4 way.
     */
#if defined(CTL_HW) && defined(HW_NCPU)
    int aiMib[2];
    aiMib[0] = CTL_HW;
    aiMib[1] = HW_NCPU;
    cCpus = -1;
    size_t cb = sizeof(cCpus);
    int rc = sysctl(aiMib, ELEMENTS(aiMib), &cCpus, &cb, NULL, 0);
    if (rc != -1 && cCpus >= 1)
        return cCpus;
#endif
    return 1;
}


/**
 * Gets the active logical processor mask.
 *
 * @returns Active logical processor mask. (bit 0 == logical cpu 0)
 */
RTR3DECL(uint64_t) RTSystemProcessorGetActiveMask(void)
{
    int cCpus = RTSystemProcessorGetCount();
    return ((uint64_t)1 << cCpus) - 1;
}

