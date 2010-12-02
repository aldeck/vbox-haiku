/* $Id$ */
/** @file
 * IPRT - Threads, POSIX.
 */

/*
 * Copyright (C) 2006-2007 Oracle Corporation
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
#define LOG_GROUP RTLOGGROUP_THREAD
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#if defined(RT_OS_LINUX)
# include <unistd.h>
# include <sys/syscall.h>
#endif
#if defined(RT_OS_SOLARIS)
# include <sched.h>
#endif

#include <iprt/thread.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/err.h>
#include <iprt/string.h>
#include "internal/thread.h"


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#ifndef IN_GUEST
/** Includes RTThreadPoke. */
# define RTTHREAD_POSIX_WITH_POKE
#endif


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** The pthread key in which we store the pointer to our own PRTTHREAD structure. */
static pthread_key_t    g_SelfKey;
#ifdef RTTHREAD_POSIX_WITH_POKE
/** The signal we use for poking threads.
 * This is set to -1 if no available signal was found. */
static int              g_iSigPokeThread = -1;
#endif


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static void *rtThreadNativeMain(void *pvArgs);
static void rtThreadKeyDestruct(void *pvValue);
static void rtThreadPosixPokeSignal(int iSignal);


int rtThreadNativeInit(void)
{
    /*
     * Allocate the TLS (key in posix terms) where we store the pointer to
     * a threads RTTHREADINT structure.
     */
    int rc = pthread_key_create(&g_SelfKey, rtThreadKeyDestruct);
    if (rc)
        return VERR_NO_TLS_FOR_SELF;

#ifdef RTTHREAD_POSIX_WITH_POKE
    /*
     * Try register the dummy signal handler for RTThreadPoke.
     * Avoid SIGRTMIN thru SIGRTMIN+2 because of LinuxThreads.
     */
    static const int s_aiSigCandidates[] =
    {
# ifdef SIGRTMAX
        SIGRTMAX-3,
        SIGRTMAX-2,
        SIGRTMAX-1,
# endif
# ifndef RT_OS_SOLARIS
        SIGUSR2,
# endif
        SIGWINCH
    };

    g_iSigPokeThread = -1;
    for (unsigned iSig = 0; iSig < RT_ELEMENTS(s_aiSigCandidates); iSig++)
    {
        struct sigaction SigActOld;
        if (!sigaction(s_aiSigCandidates[iSig], NULL, &SigActOld))
        {
            if (   SigActOld.sa_handler == SIG_DFL
                || SigActOld.sa_handler == rtThreadPosixPokeSignal)
            {
                struct sigaction SigAct;
                RT_ZERO(SigAct);
                SigAct.sa_handler = rtThreadPosixPokeSignal;
                SigAct.sa_flags   = 0;
                sigfillset(&SigAct.sa_mask);

                /* ASSUMES no sigaction race... (lazy bird) */
                if (!sigaction(s_aiSigCandidates[iSig], &SigAct, NULL))
                {
                    g_iSigPokeThread = s_aiSigCandidates[iSig];
                    break;
                }
                AssertMsgFailed(("rc=%Rrc errno=%d\n", RTErrConvertFromErrno(errno), errno));
            }
        }
        else
            AssertMsgFailed(("rc=%Rrc errno=%d\n", RTErrConvertFromErrno(errno), errno));
    }
#endif /* RTTHREAD_POSIX_WITH_POKE */
    return rc;
}


/**
 * Destructor called when a thread terminates.
 * @param   pvValue     The key value. PRTTHREAD in our case.
 */
static void rtThreadKeyDestruct(void *pvValue)
{
    /*
     * Deal with alien threads.
     */
    PRTTHREADINT pThread = (PRTTHREADINT)pvValue;
    if (pThread->fIntFlags & RTTHREADINT_FLAGS_ALIEN)
    {
        pthread_setspecific(g_SelfKey, pThread);
        rtThreadTerminate(pThread, 0);
        pthread_setspecific(g_SelfKey, NULL);
    }
}


#ifdef RTTHREAD_POSIX_WITH_POKE
/**
 * Dummy signal handler for the poke signal.
 *
 * @param   iSignal     The signal number.
 */
static void rtThreadPosixPokeSignal(int iSignal)
{
    Assert(iSignal == g_iSigPokeThread);
    NOREF(iSignal);
}
#endif


/**
 * Adopts a thread, this is called immediately after allocating the
 * thread structure.
 *
 * @param   pThread     Pointer to the thread structure.
 */
int rtThreadNativeAdopt(PRTTHREADINT pThread)
{
    /*
     * Block SIGALRM - required for timer-posix.cpp.
     * This is done to limit harm done by OSes which doesn't do special SIGALRM scheduling.
     * It will not help much if someone creates threads directly using pthread_create. :/
     */
    sigset_t SigSet;
    sigemptyset(&SigSet);
    sigaddset(&SigSet, SIGALRM);
    sigprocmask(SIG_BLOCK, &SigSet, NULL);
#ifdef RTTHREAD_POSIX_WITH_POKE
    if (g_iSigPokeThread != -1)
        siginterrupt(g_iSigPokeThread, 1);
#endif

    int rc = pthread_setspecific(g_SelfKey, pThread);
    if (!rc)
        return VINF_SUCCESS;
    return VERR_FAILED_TO_SET_SELF_TLS;
}


void rtThreadNativeDestroy(PRTTHREADINT pThread)
{
    if (pThread == (PRTTHREADINT)pthread_getspecific(g_SelfKey))
        pthread_setspecific(g_SelfKey, NULL);
}


/**
 * Wrapper which unpacks the params and calls thread function.
 */
static void *rtThreadNativeMain(void *pvArgs)
{
    PRTTHREADINT  pThread = (PRTTHREADINT)pvArgs;

#if defined(RT_OS_LINUX)
    /*
     * Set the TID.
     */
    pThread->tid = syscall(__NR_gettid);
    ASMMemoryFence();
#endif

    /*
     * Block SIGALRM - required for timer-posix.cpp.
     * This is done to limit harm done by OSes which doesn't do special SIGALRM scheduling.
     * It will not help much if someone creates threads directly using pthread_create. :/
     */
    sigset_t SigSet;
    sigemptyset(&SigSet);
    sigaddset(&SigSet, SIGALRM);
    sigprocmask(SIG_BLOCK, &SigSet, NULL);
#ifdef RTTHREAD_POSIX_WITH_POKE
    if (g_iSigPokeThread != -1)
        siginterrupt(g_iSigPokeThread, 1);
#endif

    int rc = pthread_setspecific(g_SelfKey, pThread);
    AssertReleaseMsg(!rc, ("failed to set self TLS. rc=%d thread '%s'\n", rc, pThread->szName));

    /*
     * Call common main.
     */
    pthread_t Self = pthread_self();
    Assert((uintptr_t)Self == (RTNATIVETHREAD)Self && (uintptr_t)Self != NIL_RTNATIVETHREAD);
    rc = rtThreadMain(pThread, (uintptr_t)Self, &pThread->szName[0]);

    pthread_setspecific(g_SelfKey, NULL);
    pthread_exit((void *)rc);
    return (void *)rc;
}


int rtThreadNativeCreate(PRTTHREADINT pThread, PRTNATIVETHREAD pNativeThread)
{
    /*
     * Set the default stack size.
     */
    if (!pThread->cbStack)
        pThread->cbStack = 512*1024;

#ifdef RT_OS_LINUX
    pThread->tid = -1;
#endif

    /*
     * Setup thread attributes.
     */
    pthread_attr_t  ThreadAttr;
    int rc = pthread_attr_init(&ThreadAttr);
    if (!rc)
    {
        rc = pthread_attr_setdetachstate(&ThreadAttr, PTHREAD_CREATE_DETACHED);
        if (!rc)
        {
            rc = pthread_attr_setstacksize(&ThreadAttr, pThread->cbStack);
            if (!rc)
            {
                /*
                 * Create the thread.
                 */
                pthread_t ThreadId;
                rc = pthread_create(&ThreadId, &ThreadAttr, rtThreadNativeMain, pThread);
                if (!rc)
                {
                    *pNativeThread = (uintptr_t)ThreadId;
                    return VINF_SUCCESS;
                }
            }
        }
        pthread_attr_destroy(&ThreadAttr);
    }
    return RTErrConvertFromErrno(rc);
}


RTDECL(RTTHREAD) RTThreadSelf(void)
{
    PRTTHREADINT pThread = (PRTTHREADINT)pthread_getspecific(g_SelfKey);
    /** @todo import alien threads? */
    return pThread;
}


RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    return (RTNATIVETHREAD)pthread_self();
}


RTDECL(int) RTThreadSleep(RTMSINTERVAL cMillies)
{
    LogFlow(("RTThreadSleep: cMillies=%d\n", cMillies));
    if (!cMillies)
    {
        /* pthread_yield() isn't part of SuS, thus this fun. */
#ifdef RT_OS_DARWIN
        pthread_yield_np();
#elif defined(RT_OS_FREEBSD) /* void pthread_yield */
        pthread_yield();
#elif defined(RT_OS_SOLARIS)
        sched_yield();
#else
        if (!pthread_yield())
#endif
        {
            LogFlow(("RTThreadSleep: returning %Rrc (cMillies=%d)\n", VINF_SUCCESS, cMillies));
            return VINF_SUCCESS;
        }
    }
    else
    {
        struct timespec ts;
        struct timespec tsrem = {0,0};

        ts.tv_nsec = (cMillies % 1000) * 1000000;
        ts.tv_sec  = cMillies / 1000;
        if (!nanosleep(&ts, &tsrem))
        {
            LogFlow(("RTThreadSleep: returning %Rrc (cMillies=%d)\n", VINF_SUCCESS, cMillies));
            return VINF_SUCCESS;
        }
    }

    int rc = RTErrConvertFromErrno(errno);
    LogFlow(("RTThreadSleep: returning %Rrc (cMillies=%d)\n", rc, cMillies));
    return rc;
}


RTDECL(bool) RTThreadYield(void)
{
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    uint64_t u64TS = ASMReadTSC();
#endif
#ifdef RT_OS_DARWIN
    pthread_yield_np();
#elif defined(RT_OS_SOLARIS)
    sched_yield();
#else
    pthread_yield();
#endif
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    u64TS = ASMReadTSC() - u64TS;
    bool fRc = u64TS > 1500;
    LogFlow(("RTThreadYield: returning %d (%llu ticks)\n", fRc, u64TS));
#else
    bool fRc = true; /* PORTME: Add heuristics for determining whether the cpus was yielded. */
#endif
    return fRc;
}


RTR3DECL(uint64_t) RTThreadGetAffinity(void)
{
    return 1;
}


RTR3DECL(int) RTThreadSetAffinity(uint64_t u64Mask)
{
    if (u64Mask != 1)
        return VERR_INVALID_PARAMETER;
    return VINF_SUCCESS;
}


#ifdef RTTHREAD_POSIX_WITH_POKE
RTDECL(int) RTThreadPoke(RTTHREAD hThread)
{
    AssertReturn(hThread != RTThreadSelf(), VERR_INVALID_PARAMETER);
    PRTTHREADINT pThread = rtThreadGet(hThread);
    AssertReturn(pThread, VERR_INVALID_HANDLE);

    int rc;
    if (g_iSigPokeThread != -1)
    {
        rc = pthread_kill((pthread_t)(uintptr_t)pThread->Core.Key, g_iSigPokeThread);
        rc = RTErrConvertFromErrno(rc);
    }
    else
        rc = VERR_NOT_SUPPORTED;

    rtThreadRelease(pThread);
    return rc;
}
#endif

RTR3DECL(int) RTThreadGetExecutionTimeMilli(uint64_t *pKernelTime, uint64_t *pUserTime)
{
#ifndef RT_OS_DARWIN
    struct timespec ts;
    int rc = clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    if (rc)
        return RTErrConvertFromErrno(rc);

    *pKernelTime = 0;
    *pUserTime = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return VINF_SUCCESS;
#else
    return VERR_NOT_IMPLEMENTED;
#endif
}
