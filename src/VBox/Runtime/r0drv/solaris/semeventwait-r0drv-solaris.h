/* $Id$ */
/** @file
 * IPRT - Linux Ring-0 Driver Helpers for Abstracting Wait Queues,
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
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


#ifndef ___r0drv_solaris_semeventwait_r0drv_solaris_h
#define ___r0drv_solaris_semeventwait_r0drv_solaris_h

#include "the-solaris-kernel.h"

#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/time.h>

/**
 * Solaris semaphore wait structure.
 */
typedef struct RTR0SEMSOLWAIT
{
    /** The absolute timeout given as nano seconds since the start of the
     *  monotonic clock. */
    uint64_t        uNsAbsTimeout;
    /** The timeout in nano seconds relative to the start of the wait. */
    uint64_t        cNsRelTimeout;
    /** The native timeout value. */
    union
    {
        /** The timeout (abs lbolt) when fHighRes is false.  */
        clock_t     lTimeout;
    } u;
    /** Set if we use high resolution timeouts. */
    bool            fHighRes;
    /** Set if it's an indefinite wait. */
    bool            fIndefinite;
    /** Set if we've already timed out.
     * Set by rtR0SemSolWaitDoIt or rtR0SemSolWaitHighResTimeout, read by
     * rtR0SemSolWaitHasTimedOut. */
    bool volatile   fTimedOut;
    /** Whether the wait was interrupted. */
    bool            fInterrupted;
    /** Interruptible or uninterruptible wait. */
    bool            fInterruptible;
    /** The thread to wake up. */
    kthread_t      *pThread;
    /** Cylic timer ID (used by the timeout callback). */
    cyclic_id_t     idCy;
} RTR0SEMSOLWAIT;
/** Pointer to a solaris semaphore wait structure.  */
typedef RTR0SEMSOLWAIT *PRTR0SEMSOLWAIT;


/**
 * Initializes a wait.
 *
 * The caller MUST check the wait condition BEFORE calling this function or the
 * timeout logic will be flawed.
 *
 * @returns VINF_SUCCESS or VERR_TIMEOUT.
 * @param   pWait               The wait structure.
 * @param   fFlags              The wait flags.
 * @param   uTimeout            The timeout.
 * @param   pWaitQueue          The wait queue head.
 */
DECLINLINE(int) rtR0SemSolWaitInit(PRTR0SEMSOLWAIT pWait, uint32_t fFlags, uint64_t uTimeout)
{
    /*
     * Process the flags and timeout.
     */
    if (!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE))
    {
        if (fFlags & RTSEMWAIT_FLAGS_MILLISECS)
            uTimeout = uTimeout < UINT64_MAX / UINT32_C(1000000) * UINT32_C(1000000)
                     ? uTimeout * UINT32_C(1000000)
                     : UINT64_MAX;
        if (uTimeout == UINT64_MAX)
            fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
        else
        {
            uint64_t u64Now;
            if (fFlags & RTSEMWAIT_FLAGS_RELATIVE)
            {
                if (uTimeout == 0)
                    return VERR_TIMEOUT;

                u64Now = RTTimeSystemNanoTS();
                pWait->cNsRelTimeout = uTimeout;
                pWait->uNsAbsTimeout = u64Now + uTimeout;
                if (pWait->uNsAbsTimeout < u64Now) /* overflow */
                    fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
            }
            else
            {
                u64Now = RTTimeSystemNanoTS();
                if (u64Now >= uTimeout)
                    return VERR_TIMEOUT;

                pWait->cNsRelTimeout = uTimeout - u64Now;
                pWait->uNsAbsTimeout = uTimeout;
            }
        }
    }

    if (!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE))
    {
        pWait->fIndefinite      = false;
        if (   (fFlags & (RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_ABSOLUTE))
            || pWait->cNsRelTimeout < UINT32_C(1000000000) / 100 /*Hz*/ * 4)
            pWait->fHighRes     = true;
        else
        {
#if 1
            uint64_t cTicks     = NSEC_TO_TICK_ROUNDUP(uTimeout);
#else
            uint64_t cTicks     = drv_usectohz((clock_t)(uTimeout / 1000));
#endif
            if (cTicks >= LONG_MAX)
                fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
            else
            {
                pWait->u.lTimeout = ddi_get_lbolt() + cTicks;
                pWait->fHighRes = false;
            }
        }
    }

    if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
    {
        pWait->fIndefinite      = true;
        pWait->fHighRes         = false;
        pWait->uNsAbsTimeout    = UINT64_MAX;
        pWait->cNsRelTimeout    = UINT64_MAX;
        pWait->u.lTimeout       = LONG_MAX;
    }

    pWait->fTimedOut        = false;
    pWait->fInterrupted     = false;
    pWait->fInterruptible   = !!(fFlags & RTSEMWAIT_FLAGS_INTERRUPTIBLE);
    pWait->pThread          = curthread;
    pWait->idCy             = CYCLIC_NONE;

    return VINF_SUCCESS;
}


/**
 * Cyclic timeout callback that sets the timeout indicator and wakes up the
 * waiting thread.
 *
 * @param   pvUser              The wait structure.
 */
static void rtR0SemSolWaitHighResTimeout(void *pvUser)
{
    PRTR0SEMSOLWAIT pWait   = (PRTR0SEMSOLWAIT)pvUser;
    kthread_t      *pThread = pWait->pThread;
    if (VALID_PTR(pThread)) /* paranoia */
    {
        /* Note: Trying to take the cpu_lock here doesn't work. */
        if (mutex_owner(&cpu_lock) == curthread)
        {
            cyclic_remove(pWait->idCy);
            pWait->idCy = CYCLIC_NONE;
        }
        ASMAtomicWriteBool(&pWait->fTimedOut, true);
        setrun(pThread);
    }
}


/**
 * Do the actual wait.
 *
 * @param   pWait               The wait structure.
 * @param   pCnd                The condition variable to wait on.
 * @param   pMtx                The mutex related to the condition variable.
 *                              The caller has entered this.
 */
DECLINLINE(void) rtR0SemSolWaitDoIt(PRTR0SEMSOLWAIT pWait, kcondvar_t *pCnd, kmutex_t *pMtx)
{
    int rc = 1;
    if (pWait->fIndefinite)
    {
        /*
         * No timeout - easy.
         */
        if (pWait->fInterruptible)
            rc = cv_wait_sig(pCnd, pMtx);
        else
            cv_wait(pCnd, pMtx);
    }
/** @todo Use the new cv_*hires* stuff here when available. */
    else if (pWait->fHighRes)
    {
        /*
         * High resolution timeout - arm a one-shot cyclic for waking up
         * the thread at the desired time.
         */
        cyc_handler_t   Cyh;
        Cyh.cyh_arg      = pWait;
        Cyh.cyh_func     = rtR0SemSolWaitHighResTimeout;
        Cyh.cyh_level    = CY_LOW_LEVEL; /// @todo try CY_LOCK_LEVEL and CY_HIGH_LEVEL?

        cyc_time_t      Cyt;
        Cyt.cyt_when     = pWait->uNsAbsTimeout;
        Cyt.cyt_interval = UINT64_C(1000000000) * 60;

        mutex_enter(&cpu_lock);
        pWait->idCy = cyclic_add(&Cyh, &Cyt);
        mutex_exit(&cpu_lock);

        if (pWait->fInterruptible)
            rc = cv_wait_sig(pCnd, pMtx);
        else
            cv_wait(pCnd, pMtx);

        mutex_enter(&cpu_lock);
        if (pWait->idCy != CYCLIC_NONE)
        {
            cyclic_remove(pWait->idCy);
            pWait->idCy = CYCLIC_NONE;
        }
        mutex_exit(&cpu_lock);
    }
    else
    {
        /*
         * Normal timeout.
         */
        if (pWait->fInterruptible)
            rc = cv_timedwait_sig(pCnd, pMtx, pWait->u.lTimeout);
        else
            rc = cv_timedwait(pCnd, pMtx, pWait->u.lTimeout);
    }

    /* Above zero means normal wake-up. */
    if (rc > 0)
        return;

    /* Timeout is signalled by -1. */
    if (rc == -1)
        pWait->fTimedOut = true;
    /* Interruption is signalled by 0. */
    else
    {
        AssertMsg(rc == 0, ("rc=%d\n", rc));
        pWait->fInterrupted = true;
    }
}


/**
 * Checks if a solaris wait was interrupted.
 *
 * @returns true / false
 * @param   pWait               The wait structure.
 * @remarks This shall be called before the first rtR0SemSolWaitDoIt().
 */
DECLINLINE(bool) rtR0SemSolWaitWasInterrupted(PRTR0SEMSOLWAIT pWait)
{
    return pWait->fInterrupted;
}


/**
 * Checks if a solaris wait has timed out.
 *
 * @returns true / false
 * @param   pWait               The wait structure.
 */
DECLINLINE(bool) rtR0SemSolWaitHasTimedOut(PRTR0SEMSOLWAIT pWait)
{
    return pWait->fTimedOut;
}


/**
 * Deletes a solaris wait.
 *
 * @param   pWait               The wait structure.
 */
DECLINLINE(void) rtR0SemSolWaitDelete(PRTR0SEMSOLWAIT pWait)
{
    pWait->pThread = NULL;
}

/**
 * Enters the mutex, unpinning the underlying current thread if contended and
 * we're on an interrupt thread.
 *
 * The unpinning is done to prevent a deadlock, see s this could lead to a
 * deadlock (see #4259 for the full explanation)
 *
 * @param   pMtx            The mutex to enter.
 */
DECLINLINE(void) rtR0SemSolWaitEnterMutexWithUnpinningHack(kmutex_t *pMtx)
{
    int fAcquired = mutex_tryenter(pMtx);
    if (!fAcquired)
    {
        /*
         * Note! This assumes nobody is using the RTThreadPreemptDisable in an
         *       interrupt context and expects it to work right.  The swtch will
         *       result in a voluntary preemption.  To fix this, we would have to
         *       do our own counting in RTThreadPreemptDisable/Restore like we do
         *       on systems which doesn't do preemption (OS/2, linux, ...) and
         *       check whether preemption was disabled via RTThreadPreemptDisable
         *       or not and only call swtch if RTThreadPreemptDisable wasn't called.
         */
        if (curthread->t_intr && getpil() < DISP_LEVEL)
        {
            RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;
            RTThreadPreemptDisable(&PreemptState);
            preempt();
            RTThreadPreemptRestore(&PreemptState);
        }
        mutex_enter(pMtx);
    }
}

#endif
