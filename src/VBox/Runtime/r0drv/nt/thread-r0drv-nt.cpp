/* $Id$ */
/** @file
 * innotek Portable Runtime - Threads, Ring-0 Driver, NT.
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
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
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "the-nt-kernel.h"

#include <iprt/thread.h>
#include <iprt/err.h>

__BEGIN_DECLS
NTSTATUS NTAPI ZwYieldExecution(void);
__END_DECLS


RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    return (RTNATIVETHREAD)PsGetCurrentThread();
}


RTDECL(int)   RTThreadSleep(unsigned cMillies)
{
    LARGE_INTEGER Interval;
    Interval.QuadPart = -(int64_t)cMillies * 10000;
    NTSTATUS rcNt = KeDelayExecutionThread(KernelMode, TRUE, &Interval);
    switch (rcNt)
    {
        case STATUS_SUCCESS:
            return VINF_SUCCESS;
        case STATUS_ALERTED:
        case STATUS_USER_APC:
            return VERR_INTERRUPTED;
        default:
            return RTErrConvertFromNtStatus(rcNt);
    }
}


RTDECL(bool) RTThreadYield(void)
{
    return ZwYieldExecution() != STATUS_NO_YIELD_PERFORMED;
}


