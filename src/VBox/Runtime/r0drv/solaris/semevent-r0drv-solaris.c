/* $Id$ */
/** @file
 * innotek Portable Runtime - Semaphores, Ring-0 Driver, Solaris.
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
#include <time.h>

#include <iprt/semaphore.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/err.h>

#include "internal/magics.h"


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Solaris event semaphore.
 */
typedef struct RTSEMEVENTINTERNAL
{
    /** Magic value (RTSEMEVENT_MAGIC). */
    uint32_t volatile   u32Magic;
    /** The number of waiting threads. */
    uint32_t volatile   cWaiters;
    /** Set if the event object is signaled. */
    uint8_t volatile    fSignaled;
    /** The number of threads in the process of waking up. */
    uint32_t volatile   cWaking;
    /** The Solaris mutex protecting this structure and pairing up the with the cv. */
    struct mutex        Mtx;
    /** The Solaris condition variable. */
    kcondvar_t          Cnd;
} RTSEMEVENTINTERNAL, *PRTSEMEVENTINTERNAL;


RTDECL(int)  RTSemEventCreate(PRTSEMEVENT pEventSem)
{
    Assert(sizeof(RTSEMEVENTINTERNAL) > sizeof(void *));
    AssertPtrReturn(pEventSem, VERR_INVALID_POINTER);

    PRTSEMEVENTINTERNAL pEventInt = (PRTSEMEVENTINTERNAL)RTMemAlloc(sizeof(*pEventInt));
    if (pEventInt)
    {
        pEventInt->u32Magic = RTSEMEVENT_MAGIC;
        pEventInt->cWaiters = 0;
        pEventInt->cWaking = 0;
        pEventInt->fSignaled = 0;
        mutex_init(&pEventInt->Mtx, "IPRT Event Semaphore", MUTEX_DRIVER, NULL);
        cv_init(&pEventInt->Cnd, "IPRT CV", CV_DRIVER, NULL);
        *pEventSem = pEventInt;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


RTDECL(int)  RTSemEventDestroy(RTSEMEVENT EventSem)
{
    if (EventSem == NIL_RTSEMEVENT)
        return VERR_INVALID_HANDLE;
    PRTSEMEVENTINTERNAL pEventInt = (PRTSEMEVENTINTERNAL)EventSem;
    AssertPtrReturn(pEventInt, VERR_INVALID_HANDLE);
    AssertMsgReturn(pEventInt->u32Magic == RTSEMEVENT_MAGIC,
                    ("pEventInt=%p u32Magic=%#x\n", pEventInt, pEventInt->u32Magic),
                    VERR_INVALID_HANDLE);

    mutex_enter(&pEventInt->Mtx);
    ASMAtomicIncU32(&pEventInt->u32Magic); /* make the handle invalid */
    if (pEventInt->cWaiters > 0)
    {
        /* abort waiting thread, last man cleans up. */
        ASMAtomicXchgU32(&pEventInt->cWaking, pEventInt->cWaking + pEventInt->cWaiters);
        cv_signal(&pEventInt->Cnd);
        mutex_exit(&pEventInt->Mtx);
    }
    else if (pEventInt->cWaking)
    {
        /* the last waking thread is gonna do the cleanup */
        mutex_exit(&pEventInt->Mtx);
    }
    else
    {
        mutex_exit(&pEventInt->Mtx);
        mutex_destroy(&pEventInt->Mtx);
        cv_destroy(&pEventInt->Cnd);
        RTMemFree(pEventInt);
    }

    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventSignal(RTSEMEVENT EventSem)
{
    PRTSEMEVENTINTERNAL pEventInt = (PRTSEMEVENTINTERNAL)EventSem;
    AssertPtrReturn(pEventInt, VERR_INVALID_HANDLE);
    AssertMsgReturn(pEventInt->u32Magic == RTSEMEVENT_MAGIC,
                    ("pEventInt=%p u32Magic=%#x\n", pEventInt, pEventInt->u32Magic),
                    VERR_INVALID_HANDLE);

    mutex_enter(&pEventInt->Mtx);

    if (pEventInt->cWaiters > 0)
    {
        ASMAtomicDecU32(&pEventInt->cWaiters);
        ASMAtomicIncU32(&pEventInt->cWaking);
        cv_signal(&pEventInt->Cnd);
    }
    else
        ASMAtomicXchgU8(&pEventInt->fSignaled, true);

    mutex_exit(&pEventInt->Mtx);
    return VINF_SUCCESS;
}

RTDECL(int)  RTSemEventWait(RTSEMEVENT EventSem, unsigned cMillies)
{
    int rc;
    PRTSEMEVENTINTERNAL pEventInt = (PRTSEMEVENTINTERNAL)EventSem;
    AssertPtrReturn(pEventInt, VERR_INVALID_HANDLE);
    AssertMsgReturn(pEventInt->u32Magic == RTSEMEVENT_MAGIC,
                    ("pEventInt=%p u32Magic=%#x\n", pEventInt, pEventInt->u32Magic),
                    VERR_INVALID_HANDLE);

    mutex_enter(&pEventInt->Mtx);

    if (pEventInt->fSignaled)
    {
        Assert(!pEventInt->cWaiters);
        ASMAtomicXchgU8(&pEventInt->fSignaled, false);
        rc = VINF_SUCCESS;
    }
    else
    {
        unsigned long timeout;
        drv_getparm(LBOLT, &timeout);
        
        /*
         * Translate milliseconds into ticks and go to sleep.
         */
        int cTicks;
        if (cMillies != RT_INDEFINITE_WAIT)
            cTicks = drv_usectohz((clock_t)(cMillies * 1000L));
        else
            cTicks = 0;
        cTicks += timeout;
        
        ASMAtomicIncU32(&pEventInt->cWaiters);

        /** @todo r=bird: Is this interruptible or non-interruptible? */
        rc = cv_timedwait_sig(&pEventInt->Cnd, &pEventInt->Mtx, timeout);
        if (rc > 0)
        {
            /* Retured due to call to cv_signal() or cv_broadcast() */
            if (pEventInt->u32Magic != RTSEMEVENT_MAGIC)
            {
                rc = VERR_SEM_DESTROYED;
                /** @todo r=bird: better make sure you're the last guy out before doing the cleanup.... */
                mutex_exit(&pEventInt->Mtx);
                cv_destroy(&pEventInt->Cnd);
                mutex_destroy(&pEventInt->Mtx);
                RTMemFree(pEventInt);
                return rc;
            }

            ASMAtomicDecU32(&pEventInt->cWaking);
            rc = VINF_SUCCESS;
        }
        else if (rc == -1)
        {
            /* Returned due to timeout being reached */
            if (pEventInt->cWaiters > 0)
                ASMAtomicDecU32(&pEventInt->cWaiters);
            rc = VERR_TIMEOUT;
        }
        else
        {
            /* Returned due to pending signal */
            if (pEventInt->cWaiters > 0)
                ASMAtomicDecU32(&pEventInt->cWaiters);
            rc = VERR_INTERRUPTED;
        }
    }

    mutex_exit(&pEventInt->Mtx);
    return rc;
}

/** @todo Implement RTSemEventWaitNoResume (interruptible variant of the uninterruptible RTSemEventWait()). */

