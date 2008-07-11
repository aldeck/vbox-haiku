/* $Id$ */
/** @file
 * IPRT - System, POSIX.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/system.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/mem.h>

#include <unistd.h>
#include <stdio.h>
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

