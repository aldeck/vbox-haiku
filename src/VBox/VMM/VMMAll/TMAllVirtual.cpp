/* $Id$ */
/** @file
 * TM - Timeout Manager, Virtual Time, All Contexts.
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
#define LOG_GROUP LOG_GROUP_TM
#include <VBox/tm.h>
#ifdef IN_RING3
# include <VBox/rem.h>
# include <iprt/thread.h>
#endif
#include "TMInternal.h"
#include <VBox/vm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/sup.h>

#include <iprt/time.h>
#include <iprt/assert.h>
#include <iprt/asm.h>


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static DECLCALLBACK(int) tmVirtualSetWarpDrive(PVM pVM, uint32_t u32Percent);



/**
 * Get the time when we're not running at 100%
 *
 * @returns The timestamp.
 * @param   pVM     The VM handle.
 */
static uint64_t tmVirtualGetRawNonNormal(PVM pVM)
{
    /*
     * Recalculate the RTTimeNanoTS() value for the period where
     * warp drive has been enabled.
     */
    uint64_t u64 = RTTimeNanoTS();
    u64 -= pVM->tm.s.u64VirtualWarpDriveStart;
    u64 *= pVM->tm.s.u32VirtualWarpDrivePercentage;
    u64 /= 100;
    u64 += pVM->tm.s.u64VirtualWarpDriveStart;

    /*
     * Now we apply the virtual time offset.
     * (Which is the negate RTTimeNanoTS() value for when the virtual machine
     * started if it had been running continuously without any suspends.)
     */
    u64 -= pVM->tm.s.u64VirtualOffset;
    return u64;
}


/**
 * Get the raw virtual time.
 *
 * @returns The current time stamp.
 * @param   pVM     The VM handle.
 */
DECLINLINE(uint64_t) tmVirtualGetRaw(PVM pVM)
{
    if (RT_LIKELY(!pVM->tm.s.fVirtualWarpDrive))
        return RTTimeNanoTS() - pVM->tm.s.u64VirtualOffset;
    return tmVirtualGetRawNonNormal(pVM);
}


/**
 * Inlined version of tmVirtualGetEx.
 */
DECLINLINE(uint64_t) tmVirtualGet(PVM pVM, bool fCheckTimers)
{
    uint64_t u64;
    if (RT_LIKELY(pVM->tm.s.fVirtualTicking))
    {
        STAM_COUNTER_INC(&pVM->tm.s.StatVirtualGet);
        u64 = tmVirtualGetRaw(pVM);

        /*
         * Use the chance to check for expired timers.
         */
        if (    fCheckTimers
            &&  !VM_FF_ISSET(pVM, VM_FF_TIMER)
            &&  (   pVM->tm.s.CTXALLSUFF(paTimerQueues)[TMCLOCK_VIRTUAL].u64Expire <= u64
                 || (   pVM->tm.s.fVirtualSyncTicking
                     && pVM->tm.s.CTXALLSUFF(paTimerQueues)[TMCLOCK_VIRTUAL_SYNC].u64Expire <= u64 - pVM->tm.s.offVirtualSync
                    )
                )
           )
        {
            VM_FF_SET(pVM, VM_FF_TIMER);
            STAM_COUNTER_INC(&pVM->tm.s.StatVirtualGetSetFF);
#ifdef IN_RING3
            REMR3NotifyTimerPending(pVM);
            VMR3NotifyFF(pVM, true);
#endif
        }
    }
    else
        u64 = pVM->tm.s.u64Virtual;
    return u64;
}


/**
 * Gets the current TMCLOCK_VIRTUAL time
 *
 * @returns The timestamp.
 * @param   pVM     VM handle.
 *
 * @remark  While the flow of time will never go backwards, the speed of the
 *          progress varies due to inaccurate RTTimeNanoTS and TSC. The latter can be
 *          influenced by power saving (SpeedStep, PowerNow!), while the former
 *          makes use of TSC and kernel timers.
 */
TMDECL(uint64_t) TMVirtualGet(PVM pVM)
{
    return TMVirtualGetEx(pVM, true /* check timers */);
}


/**
 * Gets the current TMCLOCK_VIRTUAL time
 *
 * @returns The timestamp.
 * @param   pVM             VM handle.
 * @param   fCheckTimers    Check timers or not
 *
 * @remark  While the flow of time will never go backwards, the speed of the
 *          progress varies due to inaccurate RTTimeNanoTS and TSC. The latter can be
 *          influenced by power saving (SpeedStep, PowerNow!), while the former
 *          makes use of TSC and kernel timers.
 */
TMDECL(uint64_t) TMVirtualGetEx(PVM pVM, bool fCheckTimers)
{
    return tmVirtualGet(pVM, fCheckTimers);
}


/**
 * Gets the current TMCLOCK_VIRTUAL_SYNC time.
 *
 * @returns The timestamp.
 * @param   pVM     VM handle.
 * @param   fCheckTimers    Check timers or not
 * @thread  EMT.
 */
TMDECL(uint64_t) TMVirtualSyncGetEx(PVM pVM, bool fCheckTimers)
{
    VM_ASSERT_EMT(pVM);

    uint64_t u64;
    if (pVM->tm.s.fVirtualSyncTicking)
    {
        STAM_COUNTER_INC(&pVM->tm.s.StatVirtualGetSync);

        /*
         * Query the virtual clock and do the usual expired timer check.
         */
        Assert(pVM->tm.s.fVirtualTicking);
        u64 = tmVirtualGetRaw(pVM);
const uint64_t u64VirtualNow = u64;
        if (    fCheckTimers
            &&  !VM_FF_ISSET(pVM, VM_FF_TIMER)
            &&  pVM->tm.s.CTXALLSUFF(paTimerQueues)[TMCLOCK_VIRTUAL].u64Expire <= u64)
        {
            VM_FF_SET(pVM, VM_FF_TIMER);
#ifdef IN_RING3
            REMR3NotifyTimerPending(pVM);
            VMR3NotifyFF(pVM, true);
#endif
            STAM_COUNTER_INC(&pVM->tm.s.StatVirtualGetSyncSetFF);
        }

        /*
         * Read the offset and adjust if we're playing catch-up.
         *
         * The catch-up adjusting work by us decrementing the offset by a percentage of
         * the time elapsed since the previous TMVirtualGetSync call.
         *
         * It's possible to get a very long or even negative interval between two read
         * for the following reasons:
         *  - Someone might have suspended the process execution, frequently the case when
         *    debugging the process.
         *  - We might be on a different CPU which TSC isn't quite in sync with the
         *    other CPUs in the system.
         *  - RTTimeNanoTS() is returning sligtly different values in GC, R0 and R3 because
         *    of the static variable it uses with the previous read time.
         *  - Another thread is racing us and we might have been preemnted while inside
         *    this function.
         *
         * Assuming nano second virtual time, we can simply ignore any intervals which has
         * any of the upper 32 bits set.
         */
        AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
        uint64_t off = pVM->tm.s.offVirtualSync;
        if (pVM->tm.s.fVirtualSyncCatchUp)
        {
            const uint64_t u64Prev = pVM->tm.s.u64VirtualSyncCatchUpPrev;
            uint64_t u64Delta = u64 - u64Prev;
            if (RT_LIKELY(!(u64Delta >> 32)))
            {
                uint64_t u64Sub = ASMMultU64ByU32DivByU32(u64Delta, pVM->tm.s.u32VirtualSyncCatchUpPercentage, 100);
                if (off > u64Sub + pVM->tm.s.offVirtualSyncGivenUp)
                {
                    off -= u64Sub;
                    ASMAtomicXchgU64(&pVM->tm.s.offVirtualSync, off);
                    pVM->tm.s.u64VirtualSyncCatchUpPrev = u64;
                    Log4(("TM: %RU64/%RU64: sub %RU32\n", u64 - off, pVM->tm.s.offVirtualSync - pVM->tm.s.offVirtualSyncGivenUp, u64Sub));
                }
                else
                {
                    /* we've completely caught up. */
                    STAM_PROFILE_ADV_STOP(&pVM->tm.s.StatVirtualSyncCatchup, c);
                    off = pVM->tm.s.offVirtualSyncGivenUp;
                    ASMAtomicXchgU64(&pVM->tm.s.offVirtualSync, off);
                    ASMAtomicXchgBool(&pVM->tm.s.fVirtualSyncCatchUp, false);
                    pVM->tm.s.u64VirtualSyncCatchUpPrev = u64;
                    Log4(("TM: %RU64/0: caught up\n", u64));
                }
            }
            else
            {
                /* More than 4 seconds since last time (or negative), ignore it. */
                if (!(u64Delta & RT_BIT_64(63)))
                    pVM->tm.s.u64VirtualSyncCatchUpPrev = u64;
                Log(("TMVirtualGetSync: u64Delta=%RX64\n", u64Delta));
            }
        }

        /*
         * Complete the calculation of the current TMCLOCK_VIRTUAL_SYNC time. The current
         * approach is to never pass the head timer. So, when we do stop the clock and
         * set the the timer pending flag.
         */
        u64 -= off;
        const uint64_t u64Expire = pVM->tm.s.CTXALLSUFF(paTimerQueues)[TMCLOCK_VIRTUAL_SYNC].u64Expire;
        if (u64 >= u64Expire)
        {
            u64 = u64Expire;
            ASMAtomicXchgU64(&pVM->tm.s.u64VirtualSync, u64);
            ASMAtomicXchgBool(&pVM->tm.s.fVirtualSyncTicking, false);
//debugging - remove this later - start
pVM->tm.s.u64VirtualSyncStoppedTS = u64VirtualNow;
#ifdef IN_GC
pVM->tm.s.fVirtualSyncStoppedInGC = true;
#else
pVM->tm.s.fVirtualSyncStoppedInGC = false;
#endif
pVM->tm.s.u8VirtualSyncStoppedApicId = ASMGetApicId();
#ifdef IN_RING0
PCSUPGLOBALINFOPAGE pGip = &g_SUPGlobalInfoPage;
#else
PCSUPGLOBALINFOPAGE pGip = g_pSUPGlobalInfoPage;
#endif
if (pGip)
{
    PCSUPGIPCPU pCpu = &pGip->aCPUs[0];
    if (pGip->u32Mode == SUPGIPMODE_ASYNC_TSC)
        pCpu = &pGip->aCPUs[pVM->tm.s.u8VirtualSyncStoppedApicId];
    pVM->tm.s.u32VirtualSyncStoppedCpuHz = (uint32_t)pCpu->u64CpuHz;
}
//debugging - remove this later - end
            if (    fCheckTimers
                &&  !VM_FF_ISSET(pVM, VM_FF_TIMER))
            {
                VM_FF_SET(pVM, VM_FF_TIMER);
#ifdef IN_RING3
                REMR3NotifyTimerPending(pVM);
                VMR3NotifyFF(pVM, true);
#endif
                STAM_COUNTER_INC(&pVM->tm.s.StatVirtualGetSyncSetFF);
                Log4(("TM: %RU64/%RU64: exp tmr=>ff\n", u64, pVM->tm.s.offVirtualSync - pVM->tm.s.offVirtualSyncGivenUp));
            }
            else
                Log4(("TM: %RU64/%RU64: exp tmr\n", u64, pVM->tm.s.offVirtualSync - pVM->tm.s.offVirtualSyncGivenUp));
        }
    }
    else
        u64 = pVM->tm.s.u64VirtualSync;
    return u64;
}


/**
 * Gets the current TMCLOCK_VIRTUAL_SYNC time.
 *
 * @returns The timestamp.
 * @param   pVM             VM handle.
 * @thread  EMT.
 */
TMDECL(uint64_t) TMVirtualSyncGet(PVM pVM)
{
    return TMVirtualSyncGetEx(pVM, true /* check timers */);
}


/**
 * Gets the current lag of the synchronous virtual clock (relative to the virtual clock).
 *
 * @return  The current lag.
 * @param   pVM     VM handle.
 */
TMDECL(uint64_t) TMVirtualSyncGetLag(PVM pVM)
{
    return pVM->tm.s.offVirtualSync - pVM->tm.s.offVirtualSyncGivenUp;
}


/**
 * Get the current catch-up percent.
 *
 * @return  The current catch0up percent. 0 means running at the same speed as the virtual clock.
 * @param   pVM     VM handle.
 */
TMDECL(uint32_t) TMVirtualSyncGetCatchUpPct(PVM pVM)
{
    if (pVM->tm.s.fVirtualSyncCatchUp)
        return pVM->tm.s.u32VirtualSyncCatchUpPercentage;
    return 0;
}


/**
 * Gets the current TMCLOCK_VIRTUAL frequency.
 *
 * @returns The freqency.
 * @param   pVM     VM handle.
 */
TMDECL(uint64_t) TMVirtualGetFreq(PVM pVM)
{
    return TMCLOCK_FREQ_VIRTUAL;
}


/**
 * Resumes the virtual clock.
 *
 * @returns VINF_SUCCESS on success.
 * @returns VINF_INTERNAL_ERROR and VBOX_STRICT assertion if called out of order.
 * @param   pVM     VM handle.
 */
TMDECL(int) TMVirtualResume(PVM pVM)
{
    if (!pVM->tm.s.fVirtualTicking)
    {
        STAM_COUNTER_INC(&pVM->tm.s.StatVirtualResume);
        pVM->tm.s.u64VirtualWarpDriveStart = RTTimeNanoTS();
        pVM->tm.s.u64VirtualOffset = pVM->tm.s.u64VirtualWarpDriveStart - pVM->tm.s.u64Virtual;
        pVM->tm.s.fVirtualTicking = true;
        pVM->tm.s.fVirtualSyncTicking = true;
        return VINF_SUCCESS;
    }

    AssertFailed();
    return VERR_INTERNAL_ERROR;
}


/**
 * Pauses the virtual clock.
 *
 * @returns VINF_SUCCESS on success.
 * @returns VINF_INTERNAL_ERROR and VBOX_STRICT assertion if called out of order.
 * @param   pVM     VM handle.
 */
TMDECL(int) TMVirtualPause(PVM pVM)
{
    if (pVM->tm.s.fVirtualTicking)
    {
        STAM_COUNTER_INC(&pVM->tm.s.StatVirtualPause);
        pVM->tm.s.u64Virtual = tmVirtualGetRaw(pVM);
        pVM->tm.s.fVirtualSyncTicking = false;
        pVM->tm.s.fVirtualTicking = false;
        return VINF_SUCCESS;
    }

    AssertFailed();
    return VERR_INTERNAL_ERROR;
}


/**
 * Gets the current warp drive percent.
 *
 * @returns The warp drive percent.
 * @param   pVM         The VM handle.
 */
TMDECL(uint32_t) TMVirtualGetWarpDrive(PVM pVM)
{
    return pVM->tm.s.u32VirtualWarpDrivePercentage;
}


/**
 * Sets the warp drive percent of the virtual time.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 * @param   u32Percent  The new percentage. 100 means normal operation.
 */
TMDECL(int) TMVirtualSetWarpDrive(PVM pVM, uint32_t u32Percent)
{
/** @todo This isn't a feature specific to virtual time, move to TM level. (It
 * should affect the TMR3UCTNow as well! */
#ifdef IN_RING3
    PVMREQ pReq;
    int rc = VMR3ReqCall(pVM, &pReq, RT_INDEFINITE_WAIT, (PFNRT)tmVirtualSetWarpDrive, 2, pVM, u32Percent);
    if (VBOX_SUCCESS(rc))
        rc = pReq->iStatus;
    VMR3ReqFree(pReq);
    return rc;
#else

    return tmVirtualSetWarpDrive(pVM, u32Percent);
#endif
}


/**
 * EMT worker for tmVirtualSetWarpDrive.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 * @param   u32Percent  See TMVirtualSetWarpDrive().
 * @internal
 */
static DECLCALLBACK(int) tmVirtualSetWarpDrive(PVM pVM, uint32_t u32Percent)
{
    /*
     * Validate it.
     */
    AssertMsgReturn(u32Percent >= 2 && u32Percent <= 20000,
                    ("%RX32 is not between 2 and 20000 (inclusive).\n", u32Percent),
                    VERR_INVALID_PARAMETER);

    /*
     * If the time is running we'll have to pause it before we can change
     * the warp drive settings.
     */
    bool fPaused = pVM->tm.s.fVirtualTicking;
    if (fPaused)
    {
        int rc = TMVirtualPause(pVM);
        AssertRCReturn(rc, rc);
        rc = TMCpuTickPause(pVM);
        AssertRCReturn(rc, rc);
    }

    pVM->tm.s.u32VirtualWarpDrivePercentage = u32Percent;
    pVM->tm.s.fVirtualWarpDrive = u32Percent != 100;
    LogRel(("TM: u32VirtualWarpDrivePercentage=%RI32 fVirtualWarpDrive=%RTbool\n",
            pVM->tm.s.u32VirtualWarpDrivePercentage, pVM->tm.s.fVirtualWarpDrive));

    if (fPaused)
    {
        int rc = TMVirtualResume(pVM);
        AssertRCReturn(rc, rc);
        rc = TMCpuTickResume(pVM);
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}


/**
 * Converts from virtual ticks to nanoseconds.
 *
 * @returns nanoseconds.
 * @param   pVM             The VM handle.
 * @param   u64VirtualTicks The virtual ticks to convert.
 * @remark  There could be rounding errors here. We just do a simple integere divide
 *          without any adjustments.
 */
TMDECL(uint64_t) TMVirtualToNano(PVM pVM, uint64_t u64VirtualTicks)
{
    AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
    return u64VirtualTicks;
}


/**
 * Converts from virtual ticks to microseconds.
 *
 * @returns microseconds.
 * @param   pVM             The VM handle.
 * @param   u64VirtualTicks The virtual ticks to convert.
 * @remark  There could be rounding errors here. We just do a simple integere divide
 *          without any adjustments.
 */
TMDECL(uint64_t) TMVirtualToMicro(PVM pVM, uint64_t u64VirtualTicks)
{
    AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
    return u64VirtualTicks / 1000;
}


/**
 * Converts from virtual ticks to milliseconds.
 *
 * @returns milliseconds.
 * @param   pVM             The VM handle.
 * @param   u64VirtualTicks The virtual ticks to convert.
 * @remark  There could be rounding errors here. We just do a simple integere divide
 *          without any adjustments.
 */
TMDECL(uint64_t) TMVirtualToMilli(PVM pVM, uint64_t u64VirtualTicks)
{
        AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
    return u64VirtualTicks / 1000000;
}


/**
 * Converts from nanoseconds to virtual ticks.
 *
 * @returns virtual ticks.
 * @param   pVM             The VM handle.
 * @param   u64NanoTS       The nanosecond value ticks to convert.
 * @remark  There could be rounding and overflow errors here.
 */
TMDECL(uint64_t) TMVirtualFromNano(PVM pVM, uint64_t u64NanoTS)
{
    AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
    return u64NanoTS;
}


/**
 * Converts from microseconds to virtual ticks.
 *
 * @returns virtual ticks.
 * @param   pVM             The VM handle.
 * @param   u64MicroTS      The microsecond value ticks to convert.
 * @remark  There could be rounding and overflow errors here.
 */
TMDECL(uint64_t) TMVirtualFromMicro(PVM pVM, uint64_t u64MicroTS)
{
    AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
    return u64MicroTS * 1000;
}


/**
 * Converts from milliseconds to virtual ticks.
 *
 * @returns virtual ticks.
 * @param   pVM             The VM handle.
 * @param   u64MilliTS      The millisecond value ticks to convert.
 * @remark  There could be rounding and overflow errors here.
 */
TMDECL(uint64_t) TMVirtualFromMilli(PVM pVM, uint64_t u64MilliTS)
{
    AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
    return u64MilliTS * 1000000;
}

