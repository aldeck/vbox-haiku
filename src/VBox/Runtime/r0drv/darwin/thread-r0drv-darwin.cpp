/* $Id$ */
/** @file
 * innotek Portable Runtime - Threads, Ring-0 Driver, Darwin.
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
#include "the-darwin-kernel.h"
#include <iprt/thread.h>
#include <iprt/err.h>



RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    return (RTNATIVETHREAD)current_thread();
}


RTDECL(int) RTThreadSleep(unsigned cMillies)
{
    uint64_t u64Deadline;
    clock_interval_to_deadline(cMillies, kMillisecondScale, &u64Deadline);
    clock_delay_until(u64Deadline);
    return VINF_SUCCESS;
}


RTDECL(bool) RTThreadYield(void)
{
    thread_block(THREAD_CONTINUE_NULL);
    return true; /* this is fishy */
}

