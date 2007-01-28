/* $Id$ */
/** @file
 * InnoTek Portable Runtime - Initialization & Termination, R0 Driver, Common.
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
#include <iprt/initterm.h>
#include <iprt/assert.h>
#include <iprt/err.h>

#include "internal/initterm.h"
#include "internal/thread.h"


/**
 * Initalizes the ring-0 driver runtime library.
 *
 * @returns iprt status code.
 * @param   fReserved       Flags reserved for the future.
 */
RTR0DECL(int) RTR0Init(unsigned fReserved)
{
    int rc;
    Assert(fReserved == 0);
    rc = rtR0InitNative();
    if (RT_SUCCESS(rc))
    {
#if !defined(__LINUX__) && !defined(__WIN__)
        rc = rtThreadInit();
#endif
        if (RT_SUCCESS(rc))
            return rc;

        rtR0TermNative();
    }
    return rc;
}


/**
 * Terminates the ring-0 driver runtime library.
 */
RTR0DECL(void) RTR0Term(void)
{
#if !defined(__LINUX__) && !defined(__WIN__)
    rtThreadTerm();
#endif
    rtR0TermNative();
}

