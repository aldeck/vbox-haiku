/* $Id$ */
/** @file
 * TM - Timeout Manager, all contexts.
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
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_TM
#include <VBox/tm.h>
#include <VBox/mm.h>
#ifdef IN_RING3
# include <VBox/rem.h>
#endif
#include "TMInternal.h"
#include <VBox/vm.h>

#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/sup.h>
#include <iprt/time.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#ifdef IN_RING3
# include <iprt/thread.h>
#endif


/**
 * Schedule the queue which was changed.
 */
DECLINLINE(void) tmSchedule(PTMTIMER pTimer)
{
    PVM pVM = pTimer->CTXALLSUFF(pVM);
    if (VM_IS_EMT(pVM))
    {
        STAM_PROFILE_START(&pVM->tm.s.CTXALLSUFF(StatScheduleOne), a);
        PTMTIMERQUEUE pQueue = &pVM->tm.s.CTXALLSUFF(paTimerQueues)[pTimer->enmClock];
        Log3(("tmSchedule: tmTimerQueueSchedule\n"));
        tmTimerQueueSchedule(pVM, pQueue);
#ifdef VBOX_STRICT
        tmTimerQueuesSanityChecks(pVM, "tmSchedule");
#endif
        STAM_PROFILE_STOP(&pVM->tm.s.CTXALLSUFF(StatScheduleOne), a);
    }
    else if (!VM_FF_ISSET(pVM, VM_FF_TIMER))  /**@todo only do this when arming the timer. */
    {
        STAM_COUNTER_INC(&pVM->tm.s.StatScheduleSetFF);
        VM_FF_SET(pVM, VM_FF_TIMER);
#ifdef IN_RING3
        REMR3NotifyTimerPending(pVM);
        VMR3NotifyFF(pVM, true);
#endif
    }
}


/**
 * Try change the state to enmStateNew from enmStateOld
 * and link the timer into the scheduling queue.
 *
 * @returns Success indicator.
 * @param   pTimer          Timer in question.
 * @param   enmStateNew     The new timer state.
 * @param   enmStateOld     The old timer state.
 */
DECLINLINE(bool) tmTimerTry(PTMTIMER pTimer, TMTIMERSTATE enmStateNew, TMTIMERSTATE enmStateOld)
{
    /*
     * Attempt state change.
     */
    bool fRc;
    TM_TRY_SET_STATE(pTimer, enmStateNew, enmStateOld, fRc);
    return fRc;
}


/**
 * Links the timer onto the scheduling queue.
 *
 * @param   pQueue  The timer queue the timer belongs to.
 * @param   pTimer  The timer.
 */
DECLINLINE(void) tmTimerLink(PTMTIMERQUEUE pQueue, PTMTIMER pTimer)
{
    Assert(!pTimer->offScheduleNext);
    const int32_t offHeadNew = (intptr_t)pTimer - (intptr_t)pQueue;
    int32_t offHead;
    do
    {
        offHead = pQueue->offSchedule;
        if (offHead)
            pTimer->offScheduleNext = ((intptr_t)pQueue + offHead) - (intptr_t)pTimer;
        else
            pTimer->offScheduleNext = 0;
    } while (!ASMAtomicCmpXchgS32(&pQueue->offSchedule, offHeadNew, offHead));
}


/**
 * Try change the state to enmStateNew from enmStateOld
 * and link the timer into the scheduling queue.
 *
 * @returns Success indicator.
 * @param   pTimer          Timer in question.
 * @param   enmStateNew     The new timer state.
 * @param   enmStateOld     The old timer state.
 */
DECLINLINE(bool) tmTimerTryWithLink(PTMTIMER pTimer, TMTIMERSTATE enmStateNew, TMTIMERSTATE enmStateOld)
{
    if (tmTimerTry(pTimer, enmStateNew, enmStateOld))
    {
        tmTimerLink(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(paTimerQueues)[pTimer->enmClock], pTimer);
        return true;
    }
    return false;
}


#ifdef VBOX_HIGH_RES_TIMERS_HACK
/**
 * Set FF if we've passed the next virtual event.
 *
 * This function is called before FFs are checked in the inner execution EM loops.
 *
 * @returns Virtual timer ticks to the next event.
 * @param   pVM         Pointer to the shared VM structure.
 * @thread  The emulation thread.
 */
TMDECL(uint64_t) TMTimerPoll(PVM pVM)
{
    /*
     * Return straight away if the timer FF is already set.
     */
    if (VM_FF_ISSET(pVM, VM_FF_TIMER))
    {
        STAM_COUNTER_INC(&pVM->tm.s.StatPollAlreadySet);
        return 0;
    }

    /*
     * Get current time and check the expire times of the two relevant queues.
     */
    const uint64_t u64Now = TMVirtualGet(pVM);

    /*
     * TMCLOCK_VIRTUAL
     */
    const uint64_t u64Expire1 = pVM->tm.s.CTXALLSUFF(paTimerQueues)[TMCLOCK_VIRTUAL].u64Expire;
    const int64_t i64Delta1 = u64Expire1 - u64Now;
    if (i64Delta1 <= 0)
    {
        LogFlow(("TMTimerPoll: expire1=%RU64 <= now=%RU64\n", u64Expire1, u64Now));
        STAM_COUNTER_INC(&pVM->tm.s.StatPollVirtual);
        VM_FF_SET(pVM, VM_FF_TIMER);
#ifdef IN_RING3
        REMR3NotifyTimerPending(pVM);
#endif
        return 0;
    }

    /*
     * TMCLOCK_VIRTUAL_SYNC
     * This isn't quite as stright forward if in a catch-up, not only do
     * we have to adjust the 'now' but when have to adjust the delta as well.
     */
    const uint64_t u64Expire2 = pVM->tm.s.CTXALLSUFF(paTimerQueues)[TMCLOCK_VIRTUAL_SYNC].u64Expire;
    uint64_t u64VirtualSyncNow;
    if (!pVM->tm.s.fVirtualSyncTicking)
        u64VirtualSyncNow = pVM->tm.s.u64VirtualSync;
    else
    {
        if (!pVM->tm.s.fVirtualSyncCatchUp)
            u64VirtualSyncNow = u64Now - pVM->tm.s.offVirtualSync;
        else
        {
            uint64_t off = pVM->tm.s.offVirtualSync;
            uint64_t u64Delta = u64Now - pVM->tm.s.u64VirtualSyncCatchUpPrev;
            if (RT_LIKELY(!(u64Delta >> 32)))
            {
                uint64_t u64Sub = ASMMultU64ByU32DivByU32(u64Delta, pVM->tm.s.u32VirtualSyncCatchUpPercentage, 100);
                if (off > u64Sub + pVM->tm.s.offVirtualSyncGivenUp)
                    off -= u64Sub;
                else
                    off = pVM->tm.s.offVirtualSyncGivenUp;
            }
            u64VirtualSyncNow = u64Now - off;
        }
    }
    int64_t i64Delta2 = u64Expire2 - u64VirtualSyncNow;
    if (i64Delta2 <= 0)
    {
        LogFlow(("TMTimerPoll: expire2=%RU64 <= now=%RU64\n", u64Expire2, u64Now));
        STAM_COUNTER_INC(&pVM->tm.s.StatPollVirtualSync);
        VM_FF_SET(pVM, VM_FF_TIMER);
#ifdef IN_RING3
        REMR3NotifyTimerPending(pVM);
#endif
        return 0;
    }
    if (pVM->tm.s.fVirtualSyncCatchUp)
        i64Delta2 = ASMMultU64ByU32DivByU32(i64Delta2, 100, pVM->tm.s.u32VirtualSyncCatchUpPercentage + 100);

    /*
     * Return the time left to the next event.
     */
    STAM_COUNTER_INC(&pVM->tm.s.StatPollMiss);
    return RT_MIN(i64Delta1, i64Delta2);
}


/**
 * Set FF if we've passed the next virtual event.
 *
 * This function is called before FFs are checked in the inner execution EM loops.
 *
 * @returns The GIP timestamp of the next event.
 *          0 if the next event has already expired.
 * @param   pVM         Pointer to the shared VM structure.
 * @param   pVM         Pointer to the shared VM structure.
 * @param   pu64Delta   Where to store the delta.
 * @thread  The emulation thread.
 */
TMDECL(uint64_t) TMTimerPollGIP(PVM pVM, uint64_t *pu64Delta)
{
    /*
     * Return straight away if the timer FF is already set.
     */
    if (VM_FF_ISSET(pVM, VM_FF_TIMER))
    {
        STAM_COUNTER_INC(&pVM->tm.s.StatPollAlreadySet);
        *pu64Delta = 0;
        return 0;
    }

    /*
     * Get current time and check the expire times of the two relevant queues.
     */
    const uint64_t u64Now = TMVirtualGet(pVM);

    /*
     * TMCLOCK_VIRTUAL
     */
    const uint64_t u64Expire1 = pVM->tm.s.CTXALLSUFF(paTimerQueues)[TMCLOCK_VIRTUAL].u64Expire;
    const int64_t i64Delta1 = u64Expire1 - u64Now;
    if (i64Delta1 <= 0)
    {
        LogFlow(("TMTimerPoll: expire1=%RU64 <= now=%RU64\n", u64Expire1, u64Now));
        STAM_COUNTER_INC(&pVM->tm.s.StatPollVirtual);
        VM_FF_SET(pVM, VM_FF_TIMER);
#ifdef IN_RING3
        REMR3NotifyTimerPending(pVM);
#endif
        *pu64Delta = 0;
        return 0;
    }

    /*
     * TMCLOCK_VIRTUAL_SYNC
     * This isn't quite as stright forward if in a catch-up, not only do
     * we have to adjust the 'now' but when have to adjust the delta as well.
     */
    const uint64_t u64Expire2 = pVM->tm.s.CTXALLSUFF(paTimerQueues)[TMCLOCK_VIRTUAL_SYNC].u64Expire;
    uint64_t u64VirtualSyncNow;
    if (!pVM->tm.s.fVirtualSyncTicking)
        u64VirtualSyncNow = pVM->tm.s.u64VirtualSync;
    else
    {
        if (!pVM->tm.s.fVirtualSyncCatchUp)
            u64VirtualSyncNow = u64Now - pVM->tm.s.offVirtualSync;
        else
        {
            uint64_t off = pVM->tm.s.offVirtualSync;
            uint64_t u64Delta = u64Now - pVM->tm.s.u64VirtualSyncCatchUpPrev;
            if (RT_LIKELY(!(u64Delta >> 32)))
            {
                uint64_t u64Sub = ASMMultU64ByU32DivByU32(u64Delta, pVM->tm.s.u32VirtualSyncCatchUpPercentage, 100);
                if (off > u64Sub + pVM->tm.s.offVirtualSyncGivenUp)
                    off -= u64Sub;
                else
                    off = pVM->tm.s.offVirtualSyncGivenUp;
            }
            u64VirtualSyncNow = u64Now - off;
        }
    }
    int64_t i64Delta2 = u64Expire2 - u64VirtualSyncNow;
    if (i64Delta2 <= 0)
    {
        LogFlow(("TMTimerPoll: expire2=%RU64 <= now=%RU64\n", u64Expire2, u64Now));
        STAM_COUNTER_INC(&pVM->tm.s.StatPollVirtualSync);
        VM_FF_SET(pVM, VM_FF_TIMER);
#ifdef IN_RING3
        REMR3NotifyTimerPending(pVM);
#endif
        *pu64Delta = 0;
        return 0;
    }
    if (pVM->tm.s.fVirtualSyncCatchUp)
        i64Delta2 = ASMMultU64ByU32DivByU32(i64Delta2, 100, pVM->tm.s.u32VirtualSyncCatchUpPercentage + 100);

    /*
     * Return the GIP time of the next event.
     * This is the reverse of what tmVirtualGetRaw is doing.
     */
    STAM_COUNTER_INC(&pVM->tm.s.StatPollMiss);
    uint64_t u64GipTime = RT_MIN(i64Delta1, i64Delta2);
    *pu64Delta = u64GipTime;
    u64GipTime += u64Now + pVM->tm.s.u64VirtualOffset;
    if (RT_UNLIKELY(!pVM->tm.s.fVirtualWarpDrive))
    {
        u64GipTime -= pVM->tm.s.u64VirtualWarpDriveStart; /* the start is GIP time. */
        u64GipTime *= 100;
        u64GipTime /= pVM->tm.s.u32VirtualWarpDrivePercentage;
        u64GipTime += pVM->tm.s.u64VirtualWarpDriveStart;
    }
    return u64GipTime;
}
#endif


/**
 * Gets the host context ring-3 pointer of the timer.
 *
 * @returns HC R3 pointer.
 * @param   pTimer      Timer handle as returned by one of the create functions.
 */
TMDECL(PTMTIMERR3) TMTimerR3Ptr(PTMTIMER pTimer)
{
    return (PTMTIMERR3)MMHyperCCToR3(pTimer->CTXALLSUFF(pVM), pTimer);
}


/**
 * Gets the host context ring-0 pointer of the timer.
 *
 * @returns HC R0 pointer.
 * @param   pTimer      Timer handle as returned by one of the create functions.
 */
TMDECL(PTMTIMERR0) TMTimerR0Ptr(PTMTIMER pTimer)
{
    return (PTMTIMERR0)MMHyperCCToR0(pTimer->CTXALLSUFF(pVM), pTimer);
}


/**
 * Gets the GC pointer of the timer.
 *
 * @returns GC pointer.
 * @param   pTimer      Timer handle as returned by one of the create functions.
 */
TMDECL(PTMTIMERGC) TMTimerGCPtr(PTMTIMER pTimer)
{
    return (PTMTIMERGC)MMHyperCCToGC(pTimer->CTXALLSUFF(pVM), pTimer);
}


/**
 * Destroy a timer
 *
 * @returns VBox status.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 */
TMDECL(int) TMTimerDestroy(PTMTIMER pTimer)
{
    int cRetries = 1000;
    do
    {
        /*
         * Change to any of the DESTROY states if valid.
         */
        TMTIMERSTATE enmState = pTimer->enmState;
        Log2(("TMTimerDestroy: pTimer=%p:{.enmState=%s, .pszDesc='%s'} cRetries=%d\n",
              pTimer, tmTimerState(enmState), HCSTRING(pTimer->pszDesc), cRetries));
        switch (enmState)
        {
            case TMTIMERSTATE_EXPIRED:
                if (!VM_IS_EMT(pTimer->CTXALLSUFF(pVM)))
                {
                    AssertMsgFailed(("Attempted timer destruction from other thread while expire pending! (%s)\n", HCSTRING(pTimer->pszDesc)));
                    return VERR_INVALID_PARAMETER;
                }
                /* fall thru */
            case TMTIMERSTATE_STOPPED:
                if (tmTimerTryWithLink(pTimer, TMTIMERSTATE_PENDING_DESTROY, enmState))
                {
                    tmSchedule(pTimer);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_ACTIVE:
                if (tmTimerTryWithLink(pTimer, TMTIMERSTATE_PENDING_STOP_DESTROY, enmState))
                {
                    tmSchedule(pTimer);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_PENDING_STOP:
            case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
                if (tmTimerTry(pTimer, TMTIMERSTATE_PENDING_STOP_DESTROY, enmState))
                {
                    tmSchedule(pTimer);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_PENDING_DESTROY:
            case TMTIMERSTATE_PENDING_STOP_DESTROY:
                AssertMsgFailed(("How many times do you think you can destroy the same timer... (%s)\n", HCSTRING(pTimer->pszDesc)));
                return VERR_INVALID_PARAMETER;

            case TMTIMERSTATE_PENDING_RESCHEDULE:
                if (tmTimerTry(pTimer, TMTIMERSTATE_PENDING_STOP_DESTROY, enmState))
                {
                    tmSchedule(pTimer);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_PENDING_SCHEDULE:
                if (tmTimerTry(pTimer, TMTIMERSTATE_PENDING_DESTROY, enmState))
                {
                    tmSchedule(pTimer);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
            case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
#ifdef IN_RING3
                if (!RTThreadYield())
                    RTThreadSleep(1);
#endif
                break;

            /*
             * Invalid states.
             */
            case TMTIMERSTATE_FREE:
                AssertMsgFailed(("Invalid timer state %d (%s)\n", enmState, HCSTRING(pTimer->pszDesc)));
                return VERR_TM_INVALID_STATE;
            default:
                AssertMsgFailed(("Unknown timer state %d (%s)\n", enmState, HCSTRING(pTimer->pszDesc)));
                return VERR_TM_UNKNOWN_STATE;
        }
    } while (cRetries-- > 0);

    AssertMsgFailed(("Failed waiting for stable state. state=%d (%s)\n", pTimer->enmState, HCSTRING(pTimer->pszDesc)));
    return VERR_INTERNAL_ERROR;
}


/**
 * Arm a timer with a (new) expire time.
 *
 * @returns VBox status.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 * @param   u64Expire       New expire time.
 */
TMDECL(int) TMTimerSet(PTMTIMER pTimer, uint64_t u64Expire)
{
    STAM_PROFILE_START(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(StatTimerSet), a);

    /** @todo find the most frequently used paths and make them skip tmSchedule and tmTimerTryWithLink. */
    int cRetries = 1000;
    do
    {
        /*
         * Change to any of the SET_EXPIRE states if valid and then to SCHEDULE or RESCHEDULE.
         */
        TMTIMERSTATE    enmState = pTimer->enmState;
        Log2(("TMTimerSet: pTimer=%p:{.enmState=%s, .pszDesc='%s'} cRetries=%d u64Expire=%llu\n",
              pTimer, tmTimerState(enmState), HCSTRING(pTimer->pszDesc), cRetries, u64Expire));
        switch (enmState)
        {
            case TMTIMERSTATE_EXPIRED:
            case TMTIMERSTATE_STOPPED:
                if (tmTimerTryWithLink(pTimer, TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE, enmState))
                {
                    Assert(!pTimer->offPrev);
                    Assert(!pTimer->offNext);
                    AssertMsg(      pTimer->enmClock != TMCLOCK_VIRTUAL_SYNC
                              ||    pTimer->CTXALLSUFF(pVM)->tm.s.fVirtualSyncTicking
                              ||    u64Expire >= pTimer->CTXALLSUFF(pVM)->tm.s.u64VirtualSync,
                              ("%RU64 < %RU64 %s\n", u64Expire, pTimer->CTXALLSUFF(pVM)->tm.s.u64VirtualSync, R3STRING(pTimer->pszDesc)));
                    pTimer->u64Expire = u64Expire;
                    TM_SET_STATE(pTimer, TMTIMERSTATE_PENDING_SCHEDULE);
                    tmSchedule(pTimer);
                    STAM_PROFILE_STOP(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(StatTimerSet), a);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_PENDING_SCHEDULE:
            case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
                if (tmTimerTry(pTimer, TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE, enmState))
                {
                    Assert(!pTimer->offPrev);
                    Assert(!pTimer->offNext);
                    pTimer->u64Expire = u64Expire;
                    TM_SET_STATE(pTimer, TMTIMERSTATE_PENDING_SCHEDULE);
                    tmSchedule(pTimer);
                    STAM_PROFILE_STOP(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(StatTimerSet), a);
                    return VINF_SUCCESS;
                }
                break;


            case TMTIMERSTATE_ACTIVE:
                if (tmTimerTryWithLink(pTimer, TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE, enmState))
                {
                    pTimer->u64Expire = u64Expire;
                    TM_SET_STATE(pTimer, TMTIMERSTATE_PENDING_RESCHEDULE);
                    tmSchedule(pTimer);
                    STAM_PROFILE_STOP(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(StatTimerSet), a);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_PENDING_RESCHEDULE:
            case TMTIMERSTATE_PENDING_STOP:
                if (tmTimerTry(pTimer, TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE, enmState))
                {
                    pTimer->u64Expire = u64Expire;
                    TM_SET_STATE(pTimer, TMTIMERSTATE_PENDING_RESCHEDULE);
                    tmSchedule(pTimer);
                    STAM_PROFILE_STOP(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(StatTimerSet), a);
                    return VINF_SUCCESS;
                }
                break;


            case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
            case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
#ifdef IN_RING3
                if (!RTThreadYield())
                    RTThreadSleep(1);
#else
/** @todo call host context and yield after a couple of iterations */
#endif
                break;

            /*
             * Invalid states.
             */
            case TMTIMERSTATE_PENDING_DESTROY:
            case TMTIMERSTATE_PENDING_STOP_DESTROY:
            case TMTIMERSTATE_FREE:
                AssertMsgFailed(("Invalid timer state %d (%s)\n", enmState, HCSTRING(pTimer->pszDesc)));
                return VERR_TM_INVALID_STATE;
            default:
                AssertMsgFailed(("Unknown timer state %d (%s)\n", enmState, HCSTRING(pTimer->pszDesc)));
                return VERR_TM_UNKNOWN_STATE;
        }
    } while (cRetries-- > 0);

    AssertMsgFailed(("Failed waiting for stable state. state=%d (%s)\n", pTimer->enmState, HCSTRING(pTimer->pszDesc)));
    STAM_PROFILE_STOP(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(StatTimerSet), a);
    return VERR_INTERNAL_ERROR;
}


/**
 * Arm a timer with a (new) expire time relative to current time.
 *
 * @returns VBox status.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 * @param   cMilliesToNext  Number of millieseconds to the next tick.
 */
TMDECL(int) TMTimerSetMillies(PTMTIMER pTimer, uint32_t cMilliesToNext)
{
    PVM pVM = pTimer->CTXALLSUFF(pVM);
    switch (pTimer->enmClock)
    {
        case TMCLOCK_VIRTUAL:
            return TMTimerSet(pTimer, cMilliesToNext * (uint64_t)TMCLOCK_FREQ_VIRTUAL / 1000 + TMVirtualGet(pVM));
        case TMCLOCK_VIRTUAL_SYNC:
            return TMTimerSet(pTimer, cMilliesToNext * (uint64_t)TMCLOCK_FREQ_VIRTUAL / 1000 + TMVirtualSyncGet(pVM));
        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return TMTimerSet(pTimer, cMilliesToNext + TMRealGet(pVM));
        case TMCLOCK_TSC:
            return TMTimerSet(pTimer, cMilliesToNext * pVM->tm.s.cTSCTicksPerSecond / 1000 + TMCpuTickGet(pVM));

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pTimer->enmClock));
            return VERR_INTERNAL_ERROR;
    }
}


/**
 * Arm a timer with a (new) expire time relative to current time.
 *
 * @returns VBox status.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 * @param   cMicrosToNext   Number of microseconds to the next tick.
 */
TMDECL(int) TMTimerSetMicro(PTMTIMER pTimer, uint64_t cMicrosToNext)
{
    PVM pVM = pTimer->CTXALLSUFF(pVM);
    switch (pTimer->enmClock)
    {
        case TMCLOCK_VIRTUAL:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return TMTimerSet(pTimer, cMicrosToNext * 1000 + TMVirtualGet(pVM));

        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return TMTimerSet(pTimer, cMicrosToNext * 1000 + TMVirtualSyncGet(pVM));

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return TMTimerSet(pTimer, cMicrosToNext / 1000 + TMRealGet(pVM));

        case TMCLOCK_TSC:
            return TMTimerSet(pTimer, TMTimerFromMicro(pTimer, cMicrosToNext) + TMCpuTickGet(pVM));

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pTimer->enmClock));
            return VERR_INTERNAL_ERROR;
    }
}


/**
 * Arm a timer with a (new) expire time relative to current time.
 *
 * @returns VBox status.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 * @param   cNanosToNext    Number of nanoseconds to the next tick.
 */
TMDECL(int) TMTimerSetNano(PTMTIMER pTimer, uint64_t cNanosToNext)
{
    PVM pVM = pTimer->CTXALLSUFF(pVM);
    switch (pTimer->enmClock)
    {
        case TMCLOCK_VIRTUAL:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return TMTimerSet(pTimer, cNanosToNext + TMVirtualGet(pVM));

        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return TMTimerSet(pTimer, cNanosToNext + TMVirtualSyncGet(pVM));

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return TMTimerSet(pTimer, cNanosToNext / 1000000 + TMRealGet(pVM));

        case TMCLOCK_TSC:
            return TMTimerSet(pTimer, TMTimerFromNano(pTimer, cNanosToNext) + TMCpuTickGet(pVM));

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pTimer->enmClock));
            return VERR_INTERNAL_ERROR;
    }
}


/**
 * Stop the timer.
 * Use TMR3TimerArm() to "un-stop" the timer.
 *
 * @returns VBox status.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 */
TMDECL(int) TMTimerStop(PTMTIMER pTimer)
{
    STAM_PROFILE_START(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(StatTimerStop), a);
    /** @todo see if this function needs optimizing. */
    int cRetries = 1000;
    do
    {
        /*
         * Change to any of the SET_EXPIRE states if valid and then to SCHEDULE or RESCHEDULE.
         */
        TMTIMERSTATE    enmState = pTimer->enmState;
        Log2(("TMTimerStop: pTimer=%p:{.enmState=%s, .pszDesc='%s'} cRetries=%d\n",
              pTimer, tmTimerState(enmState), HCSTRING(pTimer->pszDesc), cRetries));
        switch (enmState)
        {
            case TMTIMERSTATE_EXPIRED:
                //AssertMsgFailed(("You don't stop an expired timer dude!\n"));
                return VERR_INVALID_PARAMETER;

            case TMTIMERSTATE_STOPPED:
            case TMTIMERSTATE_PENDING_STOP:
            case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
                STAM_PROFILE_STOP(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(StatTimerStop), a);
                return VINF_SUCCESS;

            case TMTIMERSTATE_PENDING_SCHEDULE:
                if (tmTimerTry(pTimer, TMTIMERSTATE_PENDING_STOP_SCHEDULE, enmState))
                {
                    Assert(!pTimer->offPrev);
                    Assert(!pTimer->offNext);
                    tmSchedule(pTimer);
                    STAM_PROFILE_STOP(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(StatTimerStop), a);
                    return VINF_SUCCESS;
                }

            case TMTIMERSTATE_PENDING_RESCHEDULE:
                if (tmTimerTry(pTimer, TMTIMERSTATE_PENDING_STOP, enmState))
                {
                    tmSchedule(pTimer);
                    STAM_PROFILE_STOP(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(StatTimerStop), a);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_ACTIVE:
                if (tmTimerTryWithLink(pTimer, TMTIMERSTATE_PENDING_STOP, enmState))
                {
                    tmSchedule(pTimer);
                    STAM_PROFILE_STOP(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(StatTimerStop), a);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
            case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
#ifdef IN_RING3
                if (!RTThreadYield())
                    RTThreadSleep(1);
#else
/**@todo call host and yield cpu after a while. */
#endif
                break;

            /*
             * Invalid states.
             */
            case TMTIMERSTATE_PENDING_DESTROY:
            case TMTIMERSTATE_PENDING_STOP_DESTROY:
            case TMTIMERSTATE_FREE:
                AssertMsgFailed(("Invalid timer state %d (%s)\n", enmState, HCSTRING(pTimer->pszDesc)));
                return VERR_TM_INVALID_STATE;
            default:
                AssertMsgFailed(("Unknown timer state %d (%s)\n", enmState, HCSTRING(pTimer->pszDesc)));
                return VERR_TM_UNKNOWN_STATE;
        }
    } while (cRetries-- > 0);

    AssertMsgFailed(("Failed waiting for stable state. state=%d (%s)\n", pTimer->enmState, HCSTRING(pTimer->pszDesc)));
    STAM_PROFILE_STOP(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(StatTimerStop), a);
    return VERR_INTERNAL_ERROR;
}


/**
 * Get the current clock time.
 * Handy for calculating the new expire time.
 *
 * @returns Current clock time.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 */
TMDECL(uint64_t) TMTimerGet(PTMTIMER pTimer)
{
    uint64_t u64;
    PVM pVM = pTimer->CTXALLSUFF(pVM);
    switch (pTimer->enmClock)
    {
        case TMCLOCK_VIRTUAL:
            u64 = TMVirtualGet(pVM);
            break;
        case TMCLOCK_VIRTUAL_SYNC:
            u64 = TMVirtualSyncGet(pVM);
            break;
        case TMCLOCK_REAL:
            u64 = TMRealGet(pVM);
            break;
        case TMCLOCK_TSC:
            u64 = TMCpuTickGet(pVM);
            break;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pTimer->enmClock));
            return ~(uint64_t)0;
    }
    //Log2(("TMTimerGet: returns %llu (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
    //      u64, pTimer, tmTimerState(pTimer->enmState), HCSTRING(pTimer->pszDesc)));
    return u64;
}


/**
 * Get the freqency of the timer clock.
 *
 * @returns Clock frequency (as Hz of course).
 * @param   pTimer          Timer handle as returned by one of the create functions.
 */
TMDECL(uint64_t) TMTimerGetFreq(PTMTIMER pTimer)
{
    switch (pTimer->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            return TMCLOCK_FREQ_VIRTUAL;

        case TMCLOCK_REAL:
            return TMCLOCK_FREQ_REAL;

        case TMCLOCK_TSC:
            return TMCpuTicksPerSecond(pTimer->CTXALLSUFF(pVM));

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pTimer->enmClock));
            return 0;
    }
}


/**
 * Get the current clock time as nanoseconds.
 *
 * @returns The timer clock as nanoseconds.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 */
TMDECL(uint64_t) TMTimerGetNano(PTMTIMER pTimer)
{
    return TMTimerToNano(pTimer, TMTimerGet(pTimer));
}


/**
 * Get the current clock time as microseconds.
 *
 * @returns The timer clock as microseconds.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 */
TMDECL(uint64_t) TMTimerGetMicro(PTMTIMER pTimer)
{
    return TMTimerToMicro(pTimer, TMTimerGet(pTimer));
}


/**
 * Get the current clock time as milliseconds.
 *
 * @returns The timer clock as milliseconds.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 */
TMDECL(uint64_t) TMTimerGetMilli(PTMTIMER pTimer)
{
    return TMTimerToMilli(pTimer, TMTimerGet(pTimer));
}


/**
 * Converts the specified timer clock time to nanoseconds.
 *
 * @returns nanoseconds.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 * @param   u64Ticks        The clock ticks.
 * @remark  There could be rounding errors here. We just do a simple integere divide
 *          without any adjustments.
 */
TMDECL(uint64_t) TMTimerToNano(PTMTIMER pTimer, uint64_t u64Ticks)
{
    switch (pTimer->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return u64Ticks;

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return u64Ticks * 1000000;

        case TMCLOCK_TSC:
            AssertReleaseMsgFailed(("TMCLOCK_TSC conversions are not implemented\n"));
            return 0;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pTimer->enmClock));
            return 0;
    }
}


/**
 * Converts the specified timer clock time to microseconds.
 *
 * @returns microseconds.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 * @param   u64Ticks        The clock ticks.
 * @remark  There could be rounding errors here. We just do a simple integere divide
 *          without any adjustments.
 */
TMDECL(uint64_t) TMTimerToMicro(PTMTIMER pTimer, uint64_t u64Ticks)
{
    switch (pTimer->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return u64Ticks / 1000;

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return u64Ticks * 1000;

        case TMCLOCK_TSC:
            AssertReleaseMsgFailed(("TMCLOCK_TSC conversions are not implemented\n"));
            return 0;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pTimer->enmClock));
            return 0;
    }
}


/**
 * Converts the specified timer clock time to milliseconds.
 *
 * @returns milliseconds.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 * @param   u64Ticks        The clock ticks.
 * @remark  There could be rounding errors here. We just do a simple integere divide
 *          without any adjustments.
 */
TMDECL(uint64_t) TMTimerToMilli(PTMTIMER pTimer, uint64_t u64Ticks)
{
    switch (pTimer->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return u64Ticks / 1000000;

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return u64Ticks;

        case TMCLOCK_TSC:
            AssertReleaseMsgFailed(("TMCLOCK_TSC conversions are not implemented\n"));
            return 0;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pTimer->enmClock));
            return 0;
    }
}


/**
 * Converts the specified nanosecond timestamp to timer clock ticks.
 *
 * @returns timer clock ticks.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 * @param   u64NanoTS       The nanosecond value ticks to convert.
 * @remark  There could be rounding and overflow errors here.
 */
TMDECL(uint64_t) TMTimerFromNano(PTMTIMER pTimer, uint64_t u64NanoTS)
{
    switch (pTimer->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return u64NanoTS;

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return u64NanoTS / 1000000;

        case TMCLOCK_TSC:
            AssertReleaseMsgFailed(("TMCLOCK_TSC conversions are not implemented\n"));
            return 0;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pTimer->enmClock));
            return 0;
    }
}


/**
 * Converts the specified microsecond timestamp to timer clock ticks.
 *
 * @returns timer clock ticks.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 * @param   u64MicroTS      The microsecond value ticks to convert.
 * @remark  There could be rounding and overflow errors here.
 */
TMDECL(uint64_t) TMTimerFromMicro(PTMTIMER pTimer, uint64_t u64MicroTS)
{
    switch (pTimer->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return u64MicroTS * 1000;

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return u64MicroTS / 1000;

        case TMCLOCK_TSC:
            AssertReleaseMsgFailed(("TMCLOCK_TSC conversions are not implemented\n"));
            return 0;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pTimer->enmClock));
            return 0;
    }
}


/**
 * Converts the specified millisecond timestamp to timer clock ticks.
 *
 * @returns timer clock ticks.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 * @param   u64MilliTS      The millisecond value ticks to convert.
 * @remark  There could be rounding and overflow errors here.
 */
TMDECL(uint64_t) TMTimerFromMilli(PTMTIMER pTimer, uint64_t u64MilliTS)
{
    switch (pTimer->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return u64MilliTS * 1000000;

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return u64MilliTS;

        case TMCLOCK_TSC:
            AssertReleaseMsgFailed(("TMCLOCK_TSC conversions are not implemented\n"));
            return 0;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pTimer->enmClock));
            return 0;
    }
}


/**
 * Get the expire time of the timer.
 * Only valid for active timers.
 *
 * @returns Expire time of the timer.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 */
TMDECL(uint64_t) TMTimerGetExpire(PTMTIMER pTimer)
{
    int cRetries = 1000;
    do
    {
        TMTIMERSTATE    enmState = pTimer->enmState;
        switch (enmState)
        {
            case TMTIMERSTATE_EXPIRED:
            case TMTIMERSTATE_STOPPED:
            case TMTIMERSTATE_PENDING_STOP:
            case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
                Log2(("TMTimerGetExpire: returns ~0 (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
                      pTimer, tmTimerState(pTimer->enmState), HCSTRING(pTimer->pszDesc)));
                return ~(uint64_t)0;

            case TMTIMERSTATE_ACTIVE:
            case TMTIMERSTATE_PENDING_RESCHEDULE:
            case TMTIMERSTATE_PENDING_SCHEDULE:
                Log2(("TMTimerGetExpire: returns %llu (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
                      pTimer->u64Expire, pTimer, tmTimerState(pTimer->enmState), HCSTRING(pTimer->pszDesc)));
                return pTimer->u64Expire;

            case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
            case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
#ifdef IN_RING3
                if (!RTThreadYield())
                    RTThreadSleep(1);
#endif
                break;

            /*
             * Invalid states.
             */
            case TMTIMERSTATE_PENDING_DESTROY:
            case TMTIMERSTATE_PENDING_STOP_DESTROY:
            case TMTIMERSTATE_FREE:
                AssertMsgFailed(("Invalid timer state %d (%s)\n", enmState, HCSTRING(pTimer->pszDesc)));
                Log2(("TMTimerGetExpire: returns ~0 (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
                      pTimer, tmTimerState(pTimer->enmState), HCSTRING(pTimer->pszDesc)));
                return ~(uint64_t)0;
            default:
                AssertMsgFailed(("Unknown timer state %d (%s)\n", enmState, HCSTRING(pTimer->pszDesc)));
                return ~(uint64_t)0;
        }
    } while (cRetries-- > 0);

    AssertMsgFailed(("Failed waiting for stable state. state=%d (%s)\n", pTimer->enmState, HCSTRING(pTimer->pszDesc)));
    Log2(("TMTimerGetExpire: returns ~0 (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
          pTimer, tmTimerState(pTimer->enmState), HCSTRING(pTimer->pszDesc)));
    return ~(uint64_t)0;
}


/**
 * Checks if a timer is active or not.
 *
 * @returns True if active.
 * @returns False if not active.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 */
TMDECL(bool) TMTimerIsActive(PTMTIMER pTimer)
{
    TMTIMERSTATE    enmState = pTimer->enmState;
    switch (enmState)
    {
        case TMTIMERSTATE_STOPPED:
        case TMTIMERSTATE_EXPIRED:
        case TMTIMERSTATE_PENDING_STOP:
        case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
            Log2(("TMTimerIsActive: returns false (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
                  pTimer, tmTimerState(pTimer->enmState), HCSTRING(pTimer->pszDesc)));
            return false;

        case TMTIMERSTATE_ACTIVE:
        case TMTIMERSTATE_PENDING_RESCHEDULE:
        case TMTIMERSTATE_PENDING_SCHEDULE:
        case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
        case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
            Log2(("TMTimerIsActive: returns true (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
                  pTimer, tmTimerState(pTimer->enmState), HCSTRING(pTimer->pszDesc)));
            return true;

        /*
         * Invalid states.
         */
        case TMTIMERSTATE_PENDING_DESTROY:
        case TMTIMERSTATE_PENDING_STOP_DESTROY:
        case TMTIMERSTATE_FREE:
            AssertMsgFailed(("Invalid timer state %s (%s)\n", tmTimerState(enmState), HCSTRING(pTimer->pszDesc)));
            Log2(("TMTimerIsActive: returns false (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
                  pTimer, tmTimerState(pTimer->enmState), HCSTRING(pTimer->pszDesc)));
            return false;
        default:
            AssertMsgFailed(("Unknown timer state %d (%s)\n", enmState, HCSTRING(pTimer->pszDesc)));
            return false;
    }
}


/**
 * Convert state to string.
 *
 * @returns Readonly status name.
 * @param   enmState    State.
 */
const char *tmTimerState(TMTIMERSTATE enmState)
{
    switch (enmState)
    {
#define CASE(state) case state: return #state + sizeof("TMTIMERSTATE_") - 1
        CASE(TMTIMERSTATE_STOPPED);
        CASE(TMTIMERSTATE_ACTIVE);
        CASE(TMTIMERSTATE_EXPIRED);
        CASE(TMTIMERSTATE_PENDING_STOP);
        CASE(TMTIMERSTATE_PENDING_STOP_SCHEDULE);
        CASE(TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE);
        CASE(TMTIMERSTATE_PENDING_SCHEDULE);
        CASE(TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE);
        CASE(TMTIMERSTATE_PENDING_RESCHEDULE);
        CASE(TMTIMERSTATE_PENDING_STOP_DESTROY);
        CASE(TMTIMERSTATE_PENDING_DESTROY);
        CASE(TMTIMERSTATE_FREE);
        default:
            AssertMsgFailed(("Invalid state enmState=%d\n", enmState));
            return "Invalid state!";
#undef CASE
    }
}


/**
 * Schedules the given timer on the given queue.
 *
 * @param   pQueue      The timer queue.
 * @param   pTimer      The timer that needs scheduling.
 */
DECLINLINE(void) tmTimerQueueScheduleOne(PTMTIMERQUEUE pQueue, PTMTIMER pTimer)
{
    /*
     * Processing.
     */
    unsigned cRetries = 2;
    do
    {
        TMTIMERSTATE enmState = pTimer->enmState;
        switch (enmState)
        {
            /*
             * Reschedule timer (in the active list).
             */
            case TMTIMERSTATE_PENDING_RESCHEDULE:
            {
                if (RT_UNLIKELY(!tmTimerTry(pTimer, TMTIMERSTATE_PENDING_SCHEDULE, TMTIMERSTATE_PENDING_RESCHEDULE)))
                    break; /* retry */

                const PTMTIMER pPrev = TMTIMER_GET_PREV(pTimer);
                const PTMTIMER pNext = TMTIMER_GET_NEXT(pTimer);
                if (pPrev)
                    TMTIMER_SET_NEXT(pPrev, pNext);
                else
                {
                    TMTIMER_SET_HEAD(pQueue, pNext);
                    pQueue->u64Expire = pNext ? pNext->u64Expire : INT64_MAX;
                }
                if (pNext)
                    TMTIMER_SET_PREV(pNext, pPrev);
                pTimer->offNext = 0;
                pTimer->offPrev = 0;
                /* fall thru */
            }

            /*
             * Schedule timer (insert into the active list).
             */
            case TMTIMERSTATE_PENDING_SCHEDULE:
            {
                Assert(!pTimer->offNext); Assert(!pTimer->offPrev);
                if (RT_UNLIKELY(!tmTimerTry(pTimer, TMTIMERSTATE_ACTIVE, TMTIMERSTATE_PENDING_SCHEDULE)))
                    break; /* retry */

                PTMTIMER pCur = TMTIMER_GET_HEAD(pQueue);
                if (pCur)
                {
                    const uint64_t u64Expire = pTimer->u64Expire;
                    for (;; pCur = TMTIMER_GET_NEXT(pCur))
                    {
                        if (pCur->u64Expire > u64Expire)
                        {
                            const PTMTIMER pPrev = TMTIMER_GET_PREV(pCur);
                            TMTIMER_SET_NEXT(pTimer, pCur);
                            TMTIMER_SET_PREV(pTimer, pPrev);
                            if (pPrev)
                                TMTIMER_SET_NEXT(pPrev, pTimer);
                            else
                            {
                                TMTIMER_SET_HEAD(pQueue, pTimer);
                                pQueue->u64Expire = u64Expire;
                            }
                            TMTIMER_SET_PREV(pCur, pTimer);
                            return;
                        }
                        if (!pCur->offNext)
                        {
                            TMTIMER_SET_NEXT(pCur, pTimer);
                            TMTIMER_SET_PREV(pTimer, pCur);
                            return;
                        }
                    }
                }
                else
                {
                    TMTIMER_SET_HEAD(pQueue, pTimer);
                    pQueue->u64Expire = pTimer->u64Expire;
                }
                return;
            }

            /*
             * Stop the timer in active list.
             */
            case TMTIMERSTATE_PENDING_STOP:
            {
                if (RT_UNLIKELY(!tmTimerTry(pTimer, TMTIMERSTATE_PENDING_STOP_SCHEDULE, TMTIMERSTATE_PENDING_STOP)))
                    break; /* retry */

                const PTMTIMER pPrev = TMTIMER_GET_PREV(pTimer);
                const PTMTIMER pNext = TMTIMER_GET_NEXT(pTimer);
                if (pPrev)
                    TMTIMER_SET_NEXT(pPrev, pNext);
                else
                {
                    TMTIMER_SET_HEAD(pQueue, pNext);
                    pQueue->u64Expire = pNext ? pNext->u64Expire : INT64_MAX;
                }
                if (pNext)
                    TMTIMER_SET_PREV(pNext, pPrev);
                pTimer->offNext = 0;
                pTimer->offPrev = 0;
                /* fall thru */
            }

            /*
             * Stop the timer (not on the active list).
             */
            case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
                Assert(!pTimer->offNext); Assert(!pTimer->offPrev);
                if (RT_UNLIKELY(!tmTimerTry(pTimer, TMTIMERSTATE_STOPPED, TMTIMERSTATE_PENDING_STOP_SCHEDULE)))
                    break;
                return;

            /*
             * Stop & destroy the timer.
             */
            case TMTIMERSTATE_PENDING_STOP_DESTROY:
            {
                const PTMTIMER pPrev = TMTIMER_GET_PREV(pTimer);
                const PTMTIMER pNext = TMTIMER_GET_NEXT(pTimer);
                if (pPrev)
                    TMTIMER_SET_NEXT(pPrev, pNext);
                else
                {
                    TMTIMER_SET_HEAD(pQueue, pNext);
                    pQueue->u64Expire = pNext ? pNext->u64Expire : INT64_MAX;
                }
                if (pNext)
                    TMTIMER_SET_PREV(pNext, pPrev);
                pTimer->offNext = 0;
                pTimer->offPrev = 0;
                /* fall thru */
            }

            /*
             * Destroy the timer.
             */
            case TMTIMERSTATE_PENDING_DESTROY:
            {
                Assert(!pTimer->offNext); Assert(!pTimer->offPrev);
                PVM pVM = pTimer->CTXALLSUFF(pVM);
                const PTMTIMER pBigPrev = (PTMTIMER)(pTimer->pBigPrev ? MMHyperR3ToCC(pVM, pTimer->pBigPrev) : NULL);
                const PTMTIMER pBigNext = (PTMTIMER)(pTimer->pBigNext ? MMHyperR3ToCC(pVM, pTimer->pBigNext) : NULL);

                /* unlink from created list */
                if (pBigPrev)
                    pBigPrev->pBigNext = pTimer->pBigNext;
                else
                    pVM->tm.s.pCreated = pTimer->pBigNext;
                if (pBigNext)
                    pBigNext->pBigPrev = pTimer->pBigPrev;
                pTimer->pBigNext = 0;
                pTimer->pBigPrev = 0;

                /* free */
                Log2(("TM: Inserting %p into the free list ahead of %p!\n", pTimer, pVM->tm.s.pFree));
                pTimer->pBigNext = pVM->tm.s.pFree;
                pVM->tm.s.pFree = (PTMTIMERR3)MMHyperCCToR3(pVM, pTimer);
                TM_SET_STATE(pTimer, TMTIMERSTATE_FREE);
                return;
            }

            /*
             * Postpone these until they get into the right state.
             */
            case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
            case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
                tmTimerLink(pQueue, pTimer);
                STAM_COUNTER_INC(&pTimer->CTXALLSUFF(pVM)->tm.s.CTXALLSUFF(StatPostponed));
                return;

            /*
             * None of these can be in the schedule.
             */
            case TMTIMERSTATE_FREE:
            case TMTIMERSTATE_STOPPED:
            case TMTIMERSTATE_ACTIVE:
            case TMTIMERSTATE_EXPIRED:
            default:
                AssertMsgFailed(("Timer (%p) in the scheduling list has an invalid state %s (%d)!",
                                 pTimer, tmTimerState(pTimer->enmState), pTimer->enmState));
                return;
        }
    } while (cRetries-- > 0);
}


/**
 * Schedules the specified timer queue.
 *
 * @param   pVM             The VM to run the timers for.
 * @param   pQueue          The queue to schedule.
 */
void tmTimerQueueSchedule(PVM pVM, PTMTIMERQUEUE pQueue)
{
    VM_ASSERT_EMT(pVM);

    /*
     * Dequeue the scheduling list and iterate it.
     */
    int32_t offNext = ASMAtomicXchgS32(&pQueue->offSchedule, 0);
    Log2(("tmTimerQueueSchedule: pQueue=%p:{.enmClock=%d, offNext=%RI32}\n", pQueue, pQueue->enmClock, offNext));
    if (!offNext)
        return;
    PTMTIMER pNext = (PTMTIMER)((intptr_t)pQueue + offNext);
    while (pNext)
    {
        /*
         * Unlink the head timer and find the next one.
         */
        PTMTIMER pTimer = pNext;
        pNext = pNext->offScheduleNext ? (PTMTIMER)((intptr_t)pNext + pNext->offScheduleNext) : NULL;
        pTimer->offScheduleNext = 0;

        /*
         * Do the scheduling.
         */
        Log2(("tmTimerQueueSchedule: pTimer=%p:{.enmState=%s, .enmClock=%d, .enmType=%d, .pszDesc=%s}\n",
              pTimer, tmTimerState(pTimer->enmState), pTimer->enmClock, pTimer->enmType, HCSTRING(pTimer->pszDesc)));
        tmTimerQueueScheduleOne(pQueue, pTimer);
        Log2(("tmTimerQueueSchedule: new %s\n", tmTimerState(pTimer->enmState)));
    } /* foreach timer in current schedule batch. */
}


#ifdef VBOX_STRICT
/**
 * Checks that the timer queues are sane.
 *
 * @param   pVM     VM handle.
 */
void tmTimerQueuesSanityChecks(PVM pVM, const char *pszWhere)
{
    /*
     * Check the linking of the active lists.
     */
    for (int i = 0; i < TMCLOCK_MAX; i++)
    {
        PTMTIMERQUEUE pQueue = &pVM->tm.s.CTXALLSUFF(paTimerQueues)[i];
        Assert((int)pQueue->enmClock == i);
        PTMTIMER pPrev = NULL;
        for (PTMTIMER pCur = TMTIMER_GET_HEAD(pQueue); pCur; pPrev = pCur, pCur = TMTIMER_GET_NEXT(pCur))
        {
            AssertMsg((int)pCur->enmClock == i, ("%s: %d != %d\n", pszWhere, pCur->enmClock, i));
            AssertMsg(TMTIMER_GET_PREV(pCur) == pPrev, ("%s: %p != %p\n", pszWhere, TMTIMER_GET_PREV(pCur), pPrev));
            TMTIMERSTATE enmState = pCur->enmState;
            switch (enmState)
            {
                case TMTIMERSTATE_ACTIVE:
                    AssertMsg(!pCur->offScheduleNext, ("%s: %RI32\n", pszWhere, pCur->offScheduleNext));
                    break;
                case TMTIMERSTATE_PENDING_STOP:
                case TMTIMERSTATE_PENDING_STOP_DESTROY:
                case TMTIMERSTATE_PENDING_RESCHEDULE:
                    break;
                default:
                    AssertMsgFailed(("%s: Invalid state enmState=%d %s\n", pszWhere, enmState, tmTimerState(enmState)));
                    break;
            }
        }
    }


# ifdef IN_RING3
    /*
     * Do the big list and check that active timers all are in the active lists.
     */
    PTMTIMERR3 pPrev = NULL;
    for (PTMTIMERR3 pCur = pVM->tm.s.pCreated; pCur; pPrev = pCur, pCur = pCur->pBigNext)
    {
        Assert(pCur->pBigPrev == pPrev);
        Assert((unsigned)pCur->enmClock < (unsigned)TMCLOCK_MAX);

        TMTIMERSTATE enmState = pCur->enmState;
        switch (enmState)
        {
            case TMTIMERSTATE_ACTIVE:
            case TMTIMERSTATE_PENDING_STOP:
            case TMTIMERSTATE_PENDING_STOP_DESTROY:
            case TMTIMERSTATE_PENDING_RESCHEDULE:
            case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
            {
                PTMTIMERR3 pCurAct = TMTIMER_GET_HEAD(&pVM->tm.s.CTXALLSUFF(paTimerQueues)[pCur->enmClock]);
                Assert(pCur->offPrev || pCur == pCurAct);
                while (pCurAct && pCurAct != pCur)
                    pCurAct = TMTIMER_GET_NEXT(pCurAct);
                Assert(pCurAct == pCur);
                break;
            }

            case TMTIMERSTATE_PENDING_DESTROY:
            case TMTIMERSTATE_PENDING_SCHEDULE:
            case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
            case TMTIMERSTATE_STOPPED:
            case TMTIMERSTATE_EXPIRED:
            {
                Assert(!pCur->offNext);
                Assert(!pCur->offPrev);
                for (PTMTIMERR3 pCurAct = TMTIMER_GET_HEAD(&pVM->tm.s.CTXALLSUFF(paTimerQueues)[pCur->enmClock]);
                      pCurAct;
                      pCurAct = TMTIMER_GET_NEXT(pCurAct))
                {
                    Assert(pCurAct != pCur);
                    Assert(TMTIMER_GET_NEXT(pCurAct) != pCur);
                    Assert(TMTIMER_GET_PREV(pCurAct) != pCur);
                }
                break;
            }

            /* ignore */
            case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
                break;

            /* shouldn't get here! */
            default:
                AssertMsgFailed(("Invalid state enmState=%d %s\n", enmState, tmTimerState(enmState)));
                break;
        }
    }
# endif /* IN_RING3 */
}
#endif /* !VBOX_STRICT */

