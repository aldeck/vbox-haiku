/** @file
 * TM - Time Monitor.
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
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
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef ___VBox_tm_h
#define ___VBox_tm_h

#include <VBox/cdefs.h>
#include <VBox/types.h>
#ifdef IN_RING3
# include <iprt/time.h>
#endif

__BEGIN_DECLS

/** @defgroup grp_tm        The Time Monitor API
 * @{
 */

/** Enable a timer hack which improves the timer response/resolution a bit. */
#define VBOX_HIGH_RES_TIMERS_HACK


/**
 * Clock type.
 */
typedef enum TMCLOCK
{
    /** Real host time.
     * This clock ticks all the time, so use with care. */
    TMCLOCK_REAL = 0,
    /** Virtual guest time.
     * This clock only ticks when the guest is running. It's implemented
     * as an offset to real time. */
    TMCLOCK_VIRTUAL,
    /** Virtual guest synchronized timer time.
     * This is a special clock and timer queue for synchronizing virtual timers and
     * virtual time sources. This clock is trying to keep up with TMCLOCK_VIRTUAL,
     * but will wait for timers to be executed. If it lags too far behind TMCLOCK_VIRTUAL,
     * it will try speed up to close the distance.
     * @remarks Do not use this unless you *must*. */
    TMCLOCK_VIRTUAL_SYNC,
    /** Virtual CPU timestamp. (Running only when we're executing guest code.) */
    TMCLOCK_TSC,
    /** Number of clocks. */
    TMCLOCK_MAX
} TMCLOCK;


TMDECL(void) TMNotifyStartOfExecution(PVM pVM);
TMDECL(void) TMNotifyEndOfExecution(PVM pVM);
TMDECL(void) TMNotifyStartOfHalt(PVM pVM);
TMDECL(void) TMNotifyEndOfHalt(PVM pVM);


/** @name Real Clock Methods
 * @{
 */
TMDECL(uint64_t) TMRealGet(PVM pVM);
TMDECL(uint64_t) TMRealGetFreq(PVM pVM);
/** @} */


/** @name Virtual Clock Methods
 * @{
 */
TMDECL(uint64_t) TMVirtualGet(PVM pVM);
TMDECL(uint64_t) TMVirtualGetEx(PVM pVM, bool fCheckTimers);
TMDECL(uint64_t) TMVirtualSyncGetLag(PVM pVM);
TMDECL(uint32_t) TMVirtualSyncGetCatchUpPct(PVM pVM);
TMDECL(uint64_t) TMVirtualGetFreq(PVM pVM);
TMDECL(uint64_t) TMVirtualSyncGetEx(PVM pVM, bool fCheckTimers);
TMDECL(uint64_t) TMVirtualSyncGet(PVM pVM);
TMDECL(int) TMVirtualResume(PVM pVM);
TMDECL(int) TMVirtualPause(PVM pVM);
TMDECL(uint64_t) TMVirtualToNano(PVM pVM, uint64_t u64VirtualTicks);
TMDECL(uint64_t) TMVirtualToMicro(PVM pVM, uint64_t u64VirtualTicks);
TMDECL(uint64_t) TMVirtualToMilli(PVM pVM, uint64_t u64VirtualTicks);
TMDECL(uint64_t) TMVirtualFromNano(PVM pVM, uint64_t u64NanoTS);
TMDECL(uint64_t) TMVirtualFromMicro(PVM pVM, uint64_t u64MicroTS);
TMDECL(uint64_t) TMVirtualFromMilli(PVM pVM, uint64_t u64MilliTS);
TMDECL(uint32_t) TMVirtualGetWarpDrive(PVM pVM);
TMDECL(int) TMVirtualSetWarpDrive(PVM pVM, uint32_t u32Percent);
/** @} */


/** @name CPU Clock Methods
 * @{
 */
TMDECL(int) TMCpuTickResume(PVM pVM);
TMDECL(int) TMCpuTickPause(PVM pVM);
TMDECL(uint64_t) TMCpuTickGet(PVM pVM);
TMDECL(bool) TMCpuTickCanUseRealTSC(PVM pVM, uint64_t *poffRealTSC);
TMDECL(int) TMCpuTickSet(PVM pVM, uint64_t u64Tick);
TMDECL(uint64_t) TMCpuTicksPerSecond(PVM pVM);
/** @} */


/** @name Timer Methods
 * @{
 */
/**
 * Device timer callback function.
 *
 * @param   pDevIns         Device instance of the device which registered the timer.
 * @param   pTimer          The timer handle.
 */
typedef DECLCALLBACK(void) FNTMTIMERDEV(PPDMDEVINS pDevIns, PTMTIMER pTimer);
/** Pointer to a device timer callback function. */
typedef FNTMTIMERDEV *PFNTMTIMERDEV;

/**
 * Driver timer callback function.
 *
 * @param   pDrvIns         Device instance of the device which registered the timer.
 * @param   pTimer          The timer handle.
 */
typedef DECLCALLBACK(void) FNTMTIMERDRV(PPDMDRVINS pDrvIns, PTMTIMER pTimer);
/** Pointer to a driver timer callback function. */
typedef FNTMTIMERDRV *PFNTMTIMERDRV;

/**
 * Service timer callback function.
 *
 * @param   pSrvIns         Service instance of the device which registered the timer.
 * @param   pTimer          The timer handle.
 */
typedef DECLCALLBACK(void) FNTMTIMERSRV(PPDMSRVINS pSrvIns, PTMTIMER pTimer);
/** Pointer to a service timer callback function. */
typedef FNTMTIMERSRV *PFNTMTIMERSRV;

/**
 * Internal timer callback function.
 *
 * @param   pVM             The VM.
 * @param   pTimer          The timer handle.
 * @param   pvUser          User argument specified upon timer creation.
 */
typedef DECLCALLBACK(void) FNTMTIMERINT(PVM pVM, PTMTIMER pTimer, void *pvUser);
/** Pointer to internal timer callback function. */
typedef FNTMTIMERINT *PFNTMTIMERINT;

/**
 * External timer callback function.
 *
 * @param   pvUser          User argument as specified when the timer was created.
 */
typedef DECLCALLBACK(void) FNTMTIMEREXT(void *pvUser);
/** Pointer to an external timer callback function. */
typedef FNTMTIMEREXT *PFNTMTIMEREXT;

TMDECL(PTMTIMERR3) TMTimerR3Ptr(PTMTIMER pTimer);
TMDECL(PTMTIMERR0) TMTimerR0Ptr(PTMTIMER pTimer);
TMDECL(PTMTIMERRC) TMTimerRCPtr(PTMTIMER pTimer);
TMDECL(int) TMTimerDestroy(PTMTIMER pTimer);
TMDECL(int) TMTimerSet(PTMTIMER pTimer, uint64_t u64Expire);
TMDECL(int) TMTimerSetMillies(PTMTIMER pTimer, uint32_t cMilliesToNext);
TMDECL(int) TMTimerSetMicro(PTMTIMER pTimer, uint64_t cMicrosToNext);
TMDECL(int) TMTimerSetNano(PTMTIMER pTimer, uint64_t cNanosToNext);
TMDECL(uint64_t) TMTimerGet(PTMTIMER pTimer);
TMDECL(uint64_t) TMTimerGetNano(PTMTIMER pTimer);
TMDECL(uint64_t) TMTimerGetMicro(PTMTIMER pTimer);
TMDECL(uint64_t) TMTimerGetMilli(PTMTIMER pTimer);
TMDECL(uint64_t) TMTimerGetFreq(PTMTIMER pTimer);
TMDECL(uint64_t) TMTimerGetExpire(PTMTIMER pTimer);
TMDECL(uint64_t) TMTimerToNano(PTMTIMER pTimer, uint64_t u64Ticks);
TMDECL(uint64_t) TMTimerToMicro(PTMTIMER pTimer, uint64_t u64Ticks);
TMDECL(uint64_t) TMTimerToMilli(PTMTIMER pTimer, uint64_t u64Ticks);
TMDECL(uint64_t) TMTimerFromNano(PTMTIMER pTimer, uint64_t u64NanoTS);
TMDECL(uint64_t) TMTimerFromMicro(PTMTIMER pTimer, uint64_t u64MicroTS);
TMDECL(uint64_t) TMTimerFromMilli(PTMTIMER pTimer, uint64_t u64MilliTS);
TMDECL(int) TMTimerStop(PTMTIMER pTimer);
TMDECL(bool) TMTimerIsActive(PTMTIMER pTimer);
TMDECL(uint64_t) TMTimerPoll(PVM pVM);
TMDECL(uint64_t) TMTimerPollGIP(PVM pVM, uint64_t *pu64Delta);

/** @} */


#ifdef IN_RING3
/** @defgroup grp_tm_r3     The TM Host Context Ring-3 API
 * @ingroup grp_tm
 * @{
 */
TMR3DECL(int) TMR3Init(PVM pVM);
TMR3DECL(int) TMR3InitFinalize(PVM pVM);
TMR3DECL(void) TMR3Relocate(PVM pVM, RTGCINTPTR offDelta);
TMR3DECL(int) TMR3Term(PVM pVM);
TMR3DECL(void) TMR3Reset(PVM pVM);
TMR3DECL(int) TMR3GetImportGC(PVM pVM, const char *pszSymbol, PRTGCPTR pGCPtrValue);
TMR3DECL(int) TMR3TimerCreateDevice(PVM pVM, PPDMDEVINS pDevIns, TMCLOCK enmClock, PFNTMTIMERDEV pfnCallback, const char *pszDesc, PPTMTIMERR3 ppTimer);
TMR3DECL(int) TMR3TimerCreateDriver(PVM pVM, PPDMDRVINS pDrvIns, TMCLOCK enmClock, PFNTMTIMERDRV pfnCallback, const char *pszDesc, PPTMTIMERR3 ppTimer);
TMR3DECL(int) TMR3TimerCreateInternal(PVM pVM, TMCLOCK enmClock, PFNTMTIMERINT pfnCallback, void *pvUser, const char *pszDesc, PPTMTIMERR3 ppTimer);
TMR3DECL(PTMTIMERR3) TMR3TimerCreateExternal(PVM pVM, TMCLOCK enmClock, PFNTMTIMEREXT pfnCallback, void *pvUser, const char *pszDesc);
TMR3DECL(int) TMR3TimerDestroyDevice(PVM pVM, PPDMDEVINS pDevIns);
TMR3DECL(int) TMR3TimerDestroyDriver(PVM pVM, PPDMDRVINS pDrvIns);
TMR3DECL(int) TMR3TimerSave(PTMTIMERR3 pTimer, PSSMHANDLE pSSM);
TMR3DECL(int) TMR3TimerLoad(PTMTIMERR3 pTimer, PSSMHANDLE pSSM);
TMR3DECL(void) TMR3TimerQueuesDo(PVM pVM);
TMR3DECL(PRTTIMESPEC) TMR3UTCNow(PVM pVM, PRTTIMESPEC pTime);
/** @} */
#endif /* IN_RING3 */


/** @} */

__END_DECLS

#endif

