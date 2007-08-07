/* $Id$ */
/** @file
 * innotek Portable Runtime - Threads (Part 2), Ring-0 Driver, Solaris.
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
#include "the-solaris-kernel.h"

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/thread.h>

#include "internal/thread.h"

int rtThreadNativeInit(void)
{
    return VINF_SUCCESS;
}


RTDECL(RTTHREAD) RTThreadSelf(void)
{
    return rtThreadGetByNative(RTThreadNativeSelf());
}


/** @todo Solaris native threading, when more info. on priority etc. (namely default prio value) is available */
int rtThreadNativeSetPriority(PRTTHREADINT pThread, RTTHREADTYPE enmType)
{
    int iPriority;    
    switch (enmType)
    {
        case RTTHREADTYPE_INFREQUENT_POLLER:    iPriority = 1;              break;
        case RTTHREADTYPE_EMULATION:            iPriority = 25;             break;
        case RTTHREADTYPE_DEFAULT:              iPriority = 53;             break;
        case RTTHREADTYPE_MSG_PUMP:             iPriority = 75;             break;
        case RTTHREADTYPE_IO:                   iPriority = 100;            break;
        case RTTHREADTYPE_TIMER:                iPriority = 127;            break;
        default:
            AssertMsgFailed(("enmType=%d\n", enmType));
            return VERR_INVALID_PARAMETER;
    }

    THREAD_CHANGE_PRI(curthread, iPriority);
    return VINF_SUCCESS;
}


int rtThreadNativeAdopt(PRTTHREADINT pThread)
{
    NOREF(pThread);
    /* There is nothing special that needs doing here, but the
       user really better know what he's cooking. */
    return VINF_SUCCESS;
}


/**
 * Native thread main function.
 *
 * @param   pvThreadInt     The thread structure.
 */
static void rtThreadNativeMain(void *pvThreadInt)
{
    PRTTHREADINT pThreadInt = (PRTTHREADINT)pvThreadInt;
    int rc;

    rc = rtThreadMain(pThreadInt, (RTNATIVETHREAD)curthread, &pThreadInt->szName[0]);
    thread_exit();
}


int rtThreadNativeCreate(PRTTHREADINT pThreadInt, PRTNATIVETHREAD pNativeThread)
{
    int rc;
    /** @todo passing hardcoded priority: 52. Find what default priority to pass */
    /* We know its from 0 to 127 priority, but what's the default?? */
    kthread_t* pKernThread = thread_create(NULL, NULL, rtThreadNativeMain, pThreadInt, 0,
                                           curproc, LMS_USER, 52);
    if (rc == 0)
    {
        *pNativeThread = (RTNATIVETHREAD)pKernThread;
        return VINF_SUCCESS;
    }

    return RTErrConvertFromErrno(rc);
}

