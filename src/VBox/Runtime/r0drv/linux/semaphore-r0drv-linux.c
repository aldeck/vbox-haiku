/* $Id$ */
/** @file
 * innotek Portable Runtime - Semaphores, Ring-0 Driver, Linux.
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
#include "the-linux-kernel.h"
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
 * Linux event semaphore.
 */
typedef struct RTSEMEVENTINTERNAL
{
    /** Magic value (RTSEMEVENT_MAGIC). */
    uint32_t volatile   u32Magic;
    /** The object status - !0 when signaled and 0 when reset. */
    uint32_t volatile   fState;
    /** The wait queue. */
    wait_queue_head_t   Head;
} RTSEMEVENTINTERNAL, *PRTSEMEVENTINTERNAL;


/**
 * Linux mutex semaphore.
 */
typedef struct RTSEMMUTEXINTERNAL
{
    /** Magic value (RTSEMMUTEX_MAGIC). */
    uint32_t volatile   u32Magic;
    /** Number of recursive locks - 0 if not owned by anyone, > 0 if owned. */
    uint32_t volatile   cRecursion;
    /** The wait queue. */
    wait_queue_head_t   Head;
    /** The current owner. */
    void * volatile     pOwner;
} RTSEMMUTEXINTERNAL, *PRTSEMMUTEXINTERNAL;


/**
 * Wrapper for the linux semaphore structure.
 */
typedef struct RTSEMFASTMUTEXINTERNAL
{
    /** Magic value (RTSEMFASTMUTEX_MAGIC). */
    uint32_t            u32Magic;
    /** the linux semaphore. */
    struct semaphore    Semaphore;
} RTSEMFASTMUTEXINTERNAL, *PRTSEMFASTMUTEXINTERNAL;




RTDECL(int)  RTSemEventCreate(PRTSEMEVENT pEventSem)
{
    PRTSEMEVENTINTERNAL pEventInt = (PRTSEMEVENTINTERNAL)RTMemAlloc(sizeof(*pEventInt));
    if (pEventInt)
    {
        pEventInt->u32Magic = RTSEMEVENT_MAGIC;
        init_waitqueue_head(&pEventInt->Head);
        *pEventSem = pEventInt;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


RTDECL(int)  RTSemEventDestroy(RTSEMEVENT EventSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTINTERNAL pEventInt = (PRTSEMEVENTINTERNAL)EventSem;
    if (!pEventInt)
        return VERR_INVALID_PARAMETER;
    if (pEventInt->u32Magic != RTSEMEVENT_MAGIC)
    {
        AssertMsgFailed(("pEventInt->u32Magic=%RX32 pEventInt=%p\n", pEventInt->u32Magic, pEventInt));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Invalidate it and signal the object just in case.
     */
    ASMAtomicIncU32(&pEventInt->u32Magic);
    ASMAtomicXchgU32(&pEventInt->fState, 0);
    Assert(!waitqueue_active(&pEventInt->Head));
    wake_up_all(&pEventInt->Head);
    RTMemFree(pEventInt);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventSignal(RTSEMEVENT EventSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTINTERNAL pEventInt = (PRTSEMEVENTINTERNAL)EventSem;
    if (!pEventInt)
        return VERR_INVALID_PARAMETER;
    if (    !pEventInt
        ||  pEventInt->u32Magic != RTSEMEVENT_MAGIC)
    {
        AssertMsgFailed(("pEventInt->u32Magic=%RX32 pEventInt=%p\n", pEventInt ? pEventInt->u32Magic : 0, pEventInt));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Signal the event object.
     */
    ASMAtomicXchgU32(&pEventInt->fState, 1);
    wake_up(&pEventInt->Head);

    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventWait(RTSEMEVENT EventSem, unsigned cMillies)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTINTERNAL pEventInt = (PRTSEMEVENTINTERNAL)EventSem;
    if (!pEventInt)
        return VERR_INVALID_PARAMETER;
    if (    !pEventInt
        ||  pEventInt->u32Magic != RTSEMEVENT_MAGIC)
    {
        AssertMsgFailed(("pEventInt->u32Magic=%RX32 pEventInt=%p\n", pEventInt ? pEventInt->u32Magic : 0, pEventInt));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Try get it.
     */
    if (ASMAtomicCmpXchgU32(&pEventInt->fState, 0, 1))
        return VINF_SUCCESS;
    else
    {
        /*
         * Ok wait for it.
         */
        DEFINE_WAIT(Wait);
        int     rc       = VINF_SUCCESS;
        long    lTimeout = cMillies == RT_INDEFINITE_WAIT ? MAX_SCHEDULE_TIMEOUT : msecs_to_jiffies(cMillies);
        for (;;)
        {
            /* make everything thru schedule() atomic scheduling wise. */
            prepare_to_wait(&pEventInt->Head, &Wait, TASK_INTERRUPTIBLE);

            /* check the condition. */
            if (ASMAtomicCmpXchgU32(&pEventInt->fState, 0, 1))
                break;

            /* check for pending signals. */
            if (signal_pending(current))
            {
                rc = VERR_INTERRUPTED; /** @todo VERR_INTERRUPTED isn't correct anylonger. please fix r0drv stuff! */
                break;
            }

            /* wait */
            lTimeout = schedule_timeout(lTimeout);

            /* Check if someone destroyed the semaphore while we were waiting. */
            if (pEventInt->u32Magic != RTSEMEVENT_MAGIC)
            {
                rc = VERR_SEM_DESTROYED;
                break;
            }

            /* check for timeout. */
            if (!lTimeout)
            {
                rc = VERR_TIMEOUT;
                break;
            }
        }
        finish_wait(&pEventInt->Head, &Wait);
        return rc;
    }
}


RTDECL(int)  RTSemMutexCreate(PRTSEMMUTEX pMutexSem)
{
    PRTSEMMUTEXINTERNAL pMutexInt = (PRTSEMMUTEXINTERNAL)RTMemAlloc(sizeof(*pMutexInt));
    if (pMutexInt)
    {
        pMutexInt->u32Magic = RTSEMMUTEX_MAGIC;
        init_waitqueue_head(&pMutexInt->Head);
        *pMutexSem = pMutexInt;
AssertReleaseMsgFailed(("This mutex implementation is buggy, fix it!\n"));
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


RTDECL(int)  RTSemMutexDestroy(RTSEMMUTEX MutexSem)
{
    /*
     * Validate input.
     */
    PRTSEMMUTEXINTERNAL pMutexInt = (PRTSEMMUTEXINTERNAL)MutexSem;
    if (!pMutexInt)
        return VERR_INVALID_PARAMETER;
    if (pMutexInt->u32Magic != RTSEMMUTEX_MAGIC)
    {
        AssertMsgFailed(("pMutexInt->u32Magic=%RX32 pMutexInt=%p\n", pMutexInt->u32Magic, pMutexInt));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Invalidate it and signal the object just in case.
     */
    ASMAtomicIncU32(&pMutexInt->u32Magic);
    ASMAtomicXchgU32(&pMutexInt->cRecursion, 0);
    Assert(!waitqueue_active(&pMutexInt->Head));
    wake_up_all(&pMutexInt->Head);
    RTMemFree(pMutexInt);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemMutexRequest(RTSEMMUTEX MutexSem, unsigned cMillies)
{
    /*
     * Validate input.
     */
    PRTSEMMUTEXINTERNAL pMutexInt = (PRTSEMMUTEXINTERNAL)MutexSem;
    if (!pMutexInt)
        return VERR_INVALID_PARAMETER;
    if (    !pMutexInt
        ||  pMutexInt->u32Magic != RTSEMMUTEX_MAGIC)
    {
        AssertMsgFailed(("pMutexInt->u32Magic=%RX32 pMutexInt=%p\n", pMutexInt ? pMutexInt->u32Magic : 0, pMutexInt));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Check for recursive request.
     */
    if (pMutexInt->pOwner == current)
    {
        Assert(pMutexInt->cRecursion < 1000);
        ASMAtomicIncU32(&pMutexInt->cRecursion);
        return VINF_SUCCESS;
    }

    /*
     * Try aquire it.
     */
    if (ASMAtomicCmpXchgU32(&pMutexInt->cRecursion, 1, 0))
    {
        ASMAtomicXchgPtr(&pMutexInt->pOwner, current);
        return VINF_SUCCESS;
    }
    else
    {
        /*
         * Ok wait for it.
         */
        DEFINE_WAIT(Wait);
        int     rc       = VINF_SUCCESS;
        long    lTimeout = cMillies == RT_INDEFINITE_WAIT ? MAX_SCHEDULE_TIMEOUT : msecs_to_jiffies(cMillies);
        for (;;)
        {
            /* make everything thru schedule() atomic scheduling wise. */
            prepare_to_wait(&pMutexInt->Head, &Wait, TASK_INTERRUPTIBLE);

            /* check the condition. */
            if (ASMAtomicCmpXchgU32(&pMutexInt->cRecursion, 1, 0))
            {
                ASMAtomicXchgPtr(&pMutexInt->pOwner, current);
                break;
            }

            /* check for pending signals. */
            if (signal_pending(current))
            {
                rc = VERR_INTERRUPTED; /** @todo VERR_INTERRUPTED isn't correct anylonger. please fix r0drv stuff! */
                break;
            }

            /* wait */
            lTimeout = schedule_timeout(lTimeout);

            /* Check if someone destroyed the semaphore while we was waiting. */
            if (pMutexInt->u32Magic != RTSEMMUTEX_MAGIC)
            {
                rc = VERR_SEM_DESTROYED;
                break;
            }

            /* check for timeout. */
            if (!lTimeout)
            {
                rc = VERR_TIMEOUT;
                break;
            }
        }
        finish_wait(&pMutexInt->Head, &Wait);
        return rc;
    }
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemMutexRelease(RTSEMMUTEX MutexSem)
{
    /*
     * Validate input.
     */
    PRTSEMMUTEXINTERNAL pMutexInt = (PRTSEMMUTEXINTERNAL)MutexSem;
    if (!pMutexInt)
        return VERR_INVALID_PARAMETER;
    if (    !pMutexInt
        ||  pMutexInt->u32Magic != RTSEMMUTEX_MAGIC)
    {
        AssertMsgFailed(("pMutexInt->u32Magic=%RX32 pMutexInt=%p\n", pMutexInt ? pMutexInt->u32Magic : 0, pMutexInt));
        return VERR_INVALID_PARAMETER;
    }
    if (pMutexInt->pOwner != current)
    {
        AssertMsgFailed(("Not owner, pOwner=%p current=%p\n", (void *)pMutexInt->pOwner, (void *)current));
        return VERR_NOT_OWNER;
    }

    /*
     * Release the mutex.
     */
    if (pMutexInt->cRecursion == 1)
    {
        ASMAtomicXchgPtr(&pMutexInt->pOwner, NULL);
        ASMAtomicXchgU32(&pMutexInt->cRecursion, 0);
    }
    else
        ASMAtomicDecU32(&pMutexInt->cRecursion);

    return VINF_SUCCESS;
}



RTDECL(int)  RTSemFastMutexCreate(PRTSEMFASTMUTEX pMutexSem)
{
    /*
     * Allocate.
     */
    PRTSEMFASTMUTEXINTERNAL pFastInt;
    pFastInt = (PRTSEMFASTMUTEXINTERNAL)RTMemAlloc(sizeof(*pFastInt));
    if (!pFastInt)
        return VERR_NO_MEMORY;

    /*
     * Initialize.
     */
    pFastInt->u32Magic = RTSEMFASTMUTEX_MAGIC;
    sema_init(&pFastInt->Semaphore, 1);
    *pMutexSem = pFastInt;
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemFastMutexDestroy(RTSEMFASTMUTEX MutexSem)
{
    /*
     * Validate.
     */
    PRTSEMFASTMUTEXINTERNAL pFastInt = (PRTSEMFASTMUTEXINTERNAL)MutexSem;
    if (!pFastInt)
        return VERR_INVALID_PARAMETER;
    if (pFastInt->u32Magic != RTSEMFASTMUTEX_MAGIC)
    {
        AssertMsgFailed(("pFastInt->u32Magic=%RX32 pMutexInt=%p\n", pFastInt->u32Magic, pFastInt));
        return VERR_INVALID_PARAMETER;
    }

    ASMAtomicIncU32(&pFastInt->u32Magic);
    RTMemFree(pFastInt);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemFastMutexRequest(RTSEMFASTMUTEX MutexSem)
{
    /*
     * Validate.
     */
    PRTSEMFASTMUTEXINTERNAL pFastInt = (PRTSEMFASTMUTEXINTERNAL)MutexSem;
    if (    !pFastInt
        ||  pFastInt->u32Magic != RTSEMFASTMUTEX_MAGIC)
    {
        AssertMsgFailed(("pFastInt->u32Magic=%RX32 pMutexInt=%p\n", pFastInt ? pFastInt->u32Magic : 0, pFastInt));
        return VERR_INVALID_PARAMETER;
    }

    down(&pFastInt->Semaphore);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemFastMutexRelease(RTSEMFASTMUTEX MutexSem)
{
    /*
     * Validate.
     */
    PRTSEMFASTMUTEXINTERNAL pFastInt = (PRTSEMFASTMUTEXINTERNAL)MutexSem;
    if (    !pFastInt
        ||  pFastInt->u32Magic != RTSEMFASTMUTEX_MAGIC)
    {
        AssertMsgFailed(("pFastInt->u32Magic=%RX32 pMutexInt=%p\n", pFastInt ? pFastInt->u32Magic : 0, pFastInt));
        return VERR_INVALID_PARAMETER;
    }

    up(&pFastInt->Semaphore);
    return VINF_SUCCESS;
}

