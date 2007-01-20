/** @file
 *
 * InnoTek Portable Runtime - Timer.
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

#ifndef __iprt_timer_h__
#define __iprt_timer_h__


#include <iprt/cdefs.h>
#include <iprt/types.h>


__BEGIN_DECLS

/** @defgroup grp_rt_timer      RTTimer - Timer
 *
 * The IPRT timer API provides a simple abstraction of recurring and one-shot callback timers.
 *
 * Because of the great variation in the native APIs and the quality of
 * the service delivered by those native APIs, the timers are operated
 * on at best effort basis.
 *
 * All the ring-3 implementations are naturally at the mercy of the scheduler,
 * which means that the callback rate might vary quite a bit and we might skip
 * ticks. Many systems have a restriction that a process can only have one
 * timer. IPRT currently makes no efforts at multiplexing timers in those kind
 * of situations and will simply fail if you try to create more than one timer.
 *
 * Things are generally better in ring-0. The implementations will use interrupt
 * time callbacks wherever available, and if not, resort to a high priority
 * kernel thread.
 *
 * @ingroup grp_rt
 * @{
 */


/** Timer handle. */
typedef struct RTTIMER   *PRTTIMER;

/**
 * Timer callback function.
 *
 * The context this call is made in varies with different platforms and
 * kernel / user mode IPRT.
 *
 * In kernel mode a timer callback should not waste time, it shouldn't
 * waste stack and it should be prepared that some APIs might not work
 * correctly because of weird OS restrictions in this context that we
 * haven't discovered and avoided yet. Please fix those APIs so they
 * at least avoid panics and weird behaviour.
 *
 * @param   pTimer      Timer handle.
 * @param   pvUser      User argument.
 */
typedef DECLCALLBACK(void) FNRTTIMER(PRTTIMER pTimer, void *pvUser);
/** Pointer to FNRTTIMER() function. */
typedef FNRTTIMER *PFNRTTIMER;


/**
 * Create a recurring timer.
 *
 * @returns iprt status code.
 * @param   ppTimer             Where to store the timer handle.
 * @param   uMilliesInterval    Milliseconds between the timer ticks.
 *                              This is rounded up to the system granularity.
 * @param   pfnTimer            Callback function which shall be scheduled for execution
 *                              on every timer tick.
 * @param   pvUser              User argument for the callback.
 * @see     RTTimerDestroy, RTTimerStop
 */
RTDECL(int) RTTimerCreate(PRTTIMER *ppTimer, unsigned uMilliesInterval, PFNRTTIMER pfnTimer, void *pvUser);

/**
 * Create a suspended timer.
 *
 * @returns iprt status code.
 * @param   ppTimer             Where to store the timer handle.
 * @param   u64NanoInterval     The interval between timer ticks specified in nanoseconds if it's
 *                              a recurring timer. This is rounded to the fit the system timer granularity.
 *                              For one shot timers, pass 0.
 * @param   fFlags              Timer flags. No flags has been defined yet, pass 0.
 * @param   pfnTimer            Callback function which shall be scheduled for execution
 *                              on every timer tick.
 * @param   pvUser              User argument for the callback.
 * @see     RTTimerStart, RTTimerStop, RTTimerDestroy, RTTimerGetSystemGranularity
 */
RTDECL(int) RTTimerCreateEx(PRTTIMER *ppTimer, uint64_t u64NanoInterval, unsigned fFlags, PFNRTTIMER pfnTimer, void *pvUser);

/**
 * Stops and destroys a running timer.
 *
 * @returns iprt status code.
 * @param   pTimer      Timer to stop and destroy. NULL is ok.
 */
RTDECL(int) RTTimerDestroy(PRTTIMER pTimer);

/**
 * Stops an active timer.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_HANDLE if pTimer isn't valid.
 * @retval  VERR_TIMER_ACTIVE if the timer isn't suspended.
 *
 * @param   pTimer      The timer to activate.
 * @param   u64First    The RTTimeSystemNanoTS() for when the timer should start firing.
 *                      If 0 is specified, the timer will fire ASAP.
 * @see     RTTimerStop
 */
RTDECL(int) RTTimerStart(PRTTIMER pTimer, uint64_t u64First);

/**
 * Stops an active timer.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_HANDLE if pTimer isn't valid.
 * @retval  VERR_TIMER_SUSPENDED if the timer isn't active.
 * @retval  VERR_NOT_SUPPORTED if the IPRT implementation doesn't support stopping a timer.
 *
 * @param   pTimer  The timer to suspend.
 * @see     RTTimerStart
 */
RTDECL(int) RTTimerStop(PRTTIMER pTimer);


/**
 * Gets the (current) timer granularity of the system.
 *
 * @returns The timer granularity of the system in nanoseconds.
 * @see     RTTimerRequestSystemGranularity
 */
RTDECL(uint32_t) RTTimerGetSystemGranularity(void);

/**
 * Requests a specific system timer granularity.
 *
 * Successfull calls to this API must be coupled with the exact same number of
 * calls to RTTimerReleaseSystemGranularity() in order to undo any changes made.
 *
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if the requested value isn't supported by the host platform
 *          or if the host platform doesn't support modifying the system timer granularity.
 * @retval  VERR_PERMISSION_DENIED if the caller doesn't have the necessary privilege to
 *          modify the system timer granularity.
 *
 * @param   u32Request      The requested system timer granularity in nanoseconds.
 * @param   pu32Granted     Where to store the granted system granularity. This is the value
 *                          that should be passed to  RTTimerReleaseSystemGranularity(). It
 *                          is what RTTimerGetSystemGranularity() would return immediately
 *                          after the change was made.
 *
 *                          The value differ from the request in two ways; rounding and
 *                          scale. Meaning if your request is for 10.000.000 you might
 *                          be granted 10.000.055 or 1.000.000.
 * @see     RTTimerReleaseSystemGranularity, RTTimerGetSystemGranularity
 */
RTDECL(int) RTTimerRequestSystemGranularity(uint32_t u32Request, uint32_t *pu32Granted);

/**
 * Releases a system timer granularity grant acquired by RTTimerRequestSystemGranularity().
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if the host platform doesn't have any way of modifying
 *          the system timer granularity.
 * @retval  VERR_WRONG_ORDER if nobody call RTTimerRequestSystemGranularity() with the
 *          given grant value.
 * @param   u32Granted      The granted system granularity.
 * @see     RTTimerRequestSystemGranularity
 */
RTDECL(int) RTTimerReleaseSystemGranularity(uint32_t u32Granted);

/** @} */

__END_DECLS

#endif
