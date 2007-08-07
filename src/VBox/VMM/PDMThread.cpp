/* $Id$ */
/** @file
 * PDM Thread - VM Thread Management.
 */

/*
 * Copyright (C) 2007 innotek GmbH
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
///@todo #define LOG_GROUP LOG_GROUP_PDM_THREAD
#include "PDMInternal.h"
#include <VBox/pdm.h>
#include <VBox/mm.h>
#include <VBox/vm.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/semaphore.h>
#include <iprt/assert.h>
#include <iprt/thread.h>


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static DECLCALLBACK(int) pdmR3ThreadMain(RTTHREAD Thread, void *pvUser);


/**
 * Wrapper around ASMAtomicCmpXchgSize.
 */
DECLINLINE(bool) pdmR3AtomicCmpXchgState(PPDMTHREAD pThread, PDMTHREADSTATE enmNewState, PDMTHREADSTATE enmOldState)
{
    bool fRc;
    ASMAtomicCmpXchgSize(&pThread->enmState, enmNewState, enmOldState, fRc);
    return fRc;
}


/**
 * Does the wakeup call.
 *
 * @returns VBox status code. Already asserted on failure.
 * @param   pThread     The PDM thread.
 */
static DECLCALLBACK(int) pdmR3ThreadWakeup(PPDMTHREAD pThread)
{
    int rc;
    switch (pThread->Internal.s.enmType)
    {
        case PDMTHREADTYPE_DEVICE:
            rc = pThread->u.Dev.pfnWakeup(pThread->u.Dev.pDevIns, pThread);
            break;
            
        case PDMTHREADTYPE_USB:
            rc = pThread->u.Usb.pfnWakeup(pThread->u.Usb.pUsbIns, pThread);
            break;

        case PDMTHREADTYPE_DRIVER:
            rc = pThread->u.Drv.pfnWakeup(pThread->u.Drv.pDrvIns, pThread);
            break;

        case PDMTHREADTYPE_INTERNAL:
            rc = pThread->u.Int.pfnWakeup(pThread->Internal.s.pVM, pThread);
            break;

        case PDMTHREADTYPE_EXTERNAL:
            rc = pThread->u.Ext.pfnWakeup(pThread);
            break;

        default:
            AssertMsgFailed(("%d\n", pThread->Internal.s.enmType));
            rc = VERR_INTERNAL_ERROR;
            break;
    }
    AssertRC(rc);
    return rc;
}


/**
 * Allocates new thread instance.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 * @param   ppThread    Where to store the pointer to the instance.
 */
static int pdmR3ThreadNew(PVM pVM, PPPDMTHREAD ppThread)
{
    PPDMTHREAD pThread;
    int rc = MMR3HeapAllocZEx(pVM, MM_TAG_PDM_THREAD, sizeof(*pThread), (void **)&pThread);
    if (RT_FAILURE(rc))
        return rc;

    pThread->u32Version     = PDMTHREAD_VERSION;
    pThread->enmState       = PDMTHREADSTATE_INITIALIZING;
    pThread->Thread         = NIL_RTTHREAD;
    pThread->Internal.s.pVM = pVM;

    *ppThread = pThread;
    return VINF_SUCCESS;
}



/**
 * Initialize a new thread, this actually creates the thread.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 * @param   ppThread    Where the thread instance data handle is.
 * @param   cbStack     The stack size, see RTThreadCreate().
 * @param   enmType     The thread type, see RTThreadCreate().
 * @param   pszName     The thread name, see RTThreadCreate().
 */
static int pdmR3ThreadInit(PVM pVM, PPPDMTHREAD ppThread, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    PPDMTHREAD pThread = *ppThread;

    /*
     * Initialize the remainder of the structure.
     */
    pThread->Internal.s.pVM = pVM;

    int rc = RTSemEventMultiCreate(&pThread->Internal.s.BlockEvent);
    if (RT_SUCCESS(rc))
    {
        /*
         * Create the thread and wait for it to initialize.
         */
        rc = RTThreadCreate(&pThread->Thread, pdmR3ThreadMain, pThread, cbStack, enmType, RTTHREADFLAGS_WAITABLE, pszName);
        if (RT_SUCCESS(rc))
        {
            rc = RTThreadUserWait(pThread->Thread, 60*1000);
            if (    RT_SUCCESS(rc)
                &&  pThread->enmState != PDMTHREADSTATE_SUSPENDED)
                rc = VERR_INTERNAL_ERROR;
            if (RT_SUCCESS(rc))
            {
                rc = RTThreadUserReset(pThread->Thread);
                AssertRC(rc);
                return rc;
            }

            /* bailout */
            RTThreadWait(pThread->Thread, 60*1000, NULL);
        }
        RTSemEventMultiDestroy(pThread->Internal.s.BlockEvent);
        pThread->Internal.s.BlockEvent = NIL_RTSEMEVENTMULTI;
    }
    MMHyperFree(pVM, pThread);
    *ppThread = NULL;

    return rc;
}


/**
 * Device Helper for creating a thread associated with a device.
 * 
 * @returns VBox status code.
 * @param   pVM         The VM handle. 
 * @param   pDevIns     The device instance.
 * @param   ppThread    Where to store the thread 'handle'.
 * @param   pvUser      The user argument to the thread function.
 * @param   pfnThread   The thread function.
 * @param   pfnWakeup   The wakup callback. This is called on the EMT thread when
 *                      a state change is pending.
 * @param   cbStack     See RTThreadCreate.
 * @param   enmType     See RTThreadCreate.
 * @param   pszName     See RTThreadCreate.
 */
int pdmR3ThreadCreateDevice(PVM pVM, PPDMDEVINS pDevIns, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADDEV pfnThread,
                            PFNPDMTHREADWAKEUPDEV pfnWakeup, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    int rc = pdmR3ThreadNew(pVM, ppThread);
    if (RT_SUCCESS(rc))
    {
        (*ppThread)->pvUser = pvUser;
        (*ppThread)->Internal.s.enmType = PDMTHREADTYPE_DEVICE;
        (*ppThread)->u.Dev.pDevIns = pDevIns;
        (*ppThread)->u.Dev.pfnThread = pfnThread;
        (*ppThread)->u.Dev.pfnWakeup = pfnWakeup;
        rc = pdmR3ThreadInit(pVM, ppThread, cbStack, enmType, pszName);
    }
    return rc;
}


/**
 * USB Device Helper for creating a thread associated with an USB device.
 * 
 * @returns VBox status code.
 * @param   pVM         The VM handle. 
 * @param   pUsbIns     The USB device instance.
 * @param   ppThread    Where to store the thread 'handle'.
 * @param   pvUser      The user argument to the thread function.
 * @param   pfnThread   The thread function.
 * @param   pfnWakeup   The wakup callback. This is called on the EMT thread when
 *                      a state change is pending.
 * @param   cbStack     See RTThreadCreate.
 * @param   enmType     See RTThreadCreate.
 * @param   pszName     See RTThreadCreate.
 */
int pdmR3ThreadCreateUsb(PVM pVM, PPDMUSBINS pUsbIns, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADUSB pfnThread,
                         PFNPDMTHREADWAKEUPUSB pfnWakeup, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    int rc = pdmR3ThreadNew(pVM, ppThread);
    if (RT_SUCCESS(rc))
    {
        (*ppThread)->pvUser = pvUser;
        (*ppThread)->Internal.s.enmType = PDMTHREADTYPE_USB;
        (*ppThread)->u.Usb.pUsbIns = pUsbIns;
        (*ppThread)->u.Usb.pfnThread = pfnThread;
        (*ppThread)->u.Usb.pfnWakeup = pfnWakeup;
        rc = pdmR3ThreadInit(pVM, ppThread, cbStack, enmType, pszName);
    }
    return rc;
}


/**
 * Driver Helper for creating a thread associated with a driver.
 * 
 * @returns VBox status code.
 * @param   pVM         The VM handle. 
 * @param   pDrvIns     The driver instance.
 * @param   ppThread    Where to store the thread 'handle'.
 * @param   pvUser      The user argument to the thread function.
 * @param   pfnThread   The thread function.
 * @param   pfnWakeup   The wakup callback. This is called on the EMT thread when
 *                      a state change is pending.
 * @param   cbStack     See RTThreadCreate.
 * @param   enmType     See RTThreadCreate.
 * @param   pszName     See RTThreadCreate.
 */
int pdmR3ThreadCreateDriver(PVM pVM, PPDMDRVINS pDrvIns, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADDRV pfnThread,
                            PFNPDMTHREADWAKEUPDRV pfnWakeup, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    int rc = pdmR3ThreadNew(pVM, ppThread);
    if (RT_SUCCESS(rc))
    {
        (*ppThread)->pvUser = pvUser;
        (*ppThread)->Internal.s.enmType = PDMTHREADTYPE_DRIVER;
        (*ppThread)->u.Drv.pDrvIns = pDrvIns;
        (*ppThread)->u.Drv.pfnThread = pfnThread;
        (*ppThread)->u.Drv.pfnWakeup = pfnWakeup;
        rc = pdmR3ThreadInit(pVM, ppThread, cbStack, enmType, pszName);
    }
    return rc;
}


/**
 * Creates a PDM thread for internal use in the VM.
 * 
 * @returns VBox status code.
 * @param   pVM         The VM handle. 
 * @param   ppThread    Where to store the thread 'handle'.
 * @param   pvUser      The user argument to the thread function.
 * @param   pfnThread   The thread function.
 * @param   pfnWakeup   The wakup callback. This is called on the EMT thread when
 *                      a state change is pending.
 * @param   cbStack     See RTThreadCreate.
 * @param   enmType     See RTThreadCreate.
 * @param   pszName     See RTThreadCreate.
 */
PDMR3DECL(int) PDMR3ThreadCreate(PVM pVM, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADINT pfnThread,
                                 PFNPDMTHREADWAKEUPINT pfnWakeup, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    int rc = pdmR3ThreadNew(pVM, ppThread);
    if (RT_SUCCESS(rc))
    {
        (*ppThread)->pvUser = pvUser;
        (*ppThread)->Internal.s.enmType = PDMTHREADTYPE_INTERNAL;
        (*ppThread)->u.Int.pfnThread = pfnThread;
        (*ppThread)->u.Int.pfnWakeup = pfnWakeup;
        rc = pdmR3ThreadInit(pVM, ppThread, cbStack, enmType, pszName);
    }
    return rc;
}


/**
 * Creates a PDM thread for VM use by some external party.
 * 
 * @returns VBox status code.
 * @param   pVM         The VM handle. 
 * @param   ppThread    Where to store the thread 'handle'.
 * @param   pvUser      The user argument to the thread function.
 * @param   pfnThread   The thread function.
 * @param   pfnWakeup   The wakup callback. This is called on the EMT thread when
 *                      a state change is pending.
 * @param   cbStack     See RTThreadCreate.
 * @param   enmType     See RTThreadCreate.
 * @param   pszName     See RTThreadCreate.
 */
PDMR3DECL(int) PDMR3ThreadCreateExternal(PVM pVM, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADEXT pfnThread,
                                         PFNPDMTHREADWAKEUPEXT pfnWakeup, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    int rc = pdmR3ThreadNew(pVM, ppThread);
    if (RT_SUCCESS(rc))
    {
        (*ppThread)->pvUser = pvUser;
        (*ppThread)->Internal.s.enmType = PDMTHREADTYPE_EXTERNAL;
        (*ppThread)->u.Ext.pfnThread = pfnThread;
        (*ppThread)->u.Ext.pfnWakeup = pfnWakeup;
        rc = pdmR3ThreadInit(pVM, ppThread, cbStack, enmType, pszName);
    }
    return rc;
}


/**
 * Destroys a PDM thread.
 *
 * This will wakeup the thread, tell it to terminate, and wait for it terminate.
 *
 * @returns VBox status code.
 *          This reflects the success off destroying the thread and not the exit code
 *          of the thread as this is stored in *pRcThread.
 * @param   pThread         The thread to destroy.
 * @param   pRcThread       Where to store the thread exit code. Optional.
 * @thread  The emulation thread (EMT).
 */
PDMR3DECL(int) PDMR3ThreadDestroy(PPDMTHREAD pThread, int *pRcThread)
{
    /*
     * Assert sanity.
     */
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);
    AssertReturn(pThread->u32Version == PDMTHREAD_VERSION, VERR_INVALID_MAGIC);
    Assert(pThread->Thread != RTThreadSelf());
    AssertPtrNullReturn(pRcThread, VERR_INVALID_POINTER);
    PVM pVM = pThread->Internal.s.pVM;
    VM_ASSERT_EMT(pVM);

    /*
     * Advance the thread to the terminating state.
     */
    int rc = VINF_SUCCESS;
    if (pThread->enmState <= PDMTHREADSTATE_TERMINATING)
    {
        for (;;)
        {
            PDMTHREADSTATE enmState = pThread->enmState;
            switch (enmState)
            {
                case PDMTHREADSTATE_RUNNING:
                    if (!pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
                        continue;
                    rc = pdmR3ThreadWakeup(pThread);
                    break;

                case PDMTHREADSTATE_SUSPENDING:
                case PDMTHREADSTATE_SUSPENDED:
                case PDMTHREADSTATE_RESUMING:
                case PDMTHREADSTATE_INITIALIZING:
                    if (!pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
                        continue;
                    break;

                case PDMTHREADSTATE_TERMINATING:
                case PDMTHREADSTATE_TERMINATED:
                    break;

                default:
                    AssertMsgFailed(("enmState=%d\n", enmState));
                    rc = VERR_INTERNAL_ERROR;
                    break;
            }
            break;
        }
    }

    /*
     * Wait for it to terminate and the do cleanups.
     */
    int rc2 = RTThreadWait(pThread->Thread, RT_SUCCESS(rc) ? 60*1000 : 150, pRcThread);
    if (RT_SUCCESS(rc2))
    {
        /* make it invalid. */
        pThread->u32Version = 0xffffffff;
        pThread->enmState = PDMTHREADSTATE_INVALID;
        pThread->Thread = NIL_RTTHREAD;

        /* unlink */
        if (pVM->pdm.s.pThreads == pThread)
            pVM->pdm.s.pThreads = pThread->Internal.s.pNext;
        else
        {
            PPDMTHREAD pPrev = pVM->pdm.s.pThreads;
            while (pPrev && pPrev->Internal.s.pNext != pThread)
                pPrev = pPrev->Internal.s.pNext;
            Assert(pPrev);
            if (pPrev)
                pPrev->Internal.s.pNext = pThread->Internal.s.pNext;
        }
        if (pVM->pdm.s.pThreadsTail == pThread)
        {
            Assert(pVM->pdm.s.pThreads == NULL);
            pVM->pdm.s.pThreadsTail = NULL;
        }
        pThread->Internal.s.pNext = NULL;

        /* free it */
        MMR3HeapFree(pThread);
    }
    else if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}


/**
 * Destroys all threads associated with a device.
 *
 * This function is called by PDMDevice when a device is 
 * destroyed (not currently implemented).
 * 
 * @returns VBox status code of the first failure.
 * @param   pVM         The VM handle.
 * @param   pDevIns     the device instance.
 */
int pdmR3ThreadDestroyDevice(PVM pVM, PPDMDEVINS pDevIns)
{
    int rc = VINF_SUCCESS;

    AssertPtr(pDevIns);
    PPDMTHREAD pThread = pVM->pdm.s.pThreads;
    while (pThread)
    {
        PPDMTHREAD pNext = pThread->Internal.s.pNext;
        if (    pThread->Internal.s.enmType == PDMTHREADTYPE_DEVICE
            &&  pThread->u.Dev.pDevIns == pDevIns)
        {
            int rc2 = PDMR3ThreadDestroy(pThread, NULL);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
        }
        pThread = pNext;
    }

    return rc;
}


/**
 * Destroys all threads associated with an USB device.
 *
 * This function is called by PDMUsb when a device is destroyed.
 * 
 * @returns VBox status code of the first failure.
 * @param   pVM         The VM handle.
 * @param   pUsbIns     The USB device instance.
 */
int pdmR3ThreadDestroyUsb(PVM pVM, PPDMUSBINS pUsbIns)
{
    int rc = VINF_SUCCESS;

    AssertPtr(pUsbIns);
    PPDMTHREAD pThread = pVM->pdm.s.pThreads;
    while (pThread)
    {
        PPDMTHREAD pNext = pThread->Internal.s.pNext;
        if (    pThread->Internal.s.enmType == PDMTHREADTYPE_DEVICE
            &&  pThread->u.Usb.pUsbIns == pUsbIns)
        {
            int rc2 = PDMR3ThreadDestroy(pThread, NULL);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
        }
        pThread = pNext;
    }

    return rc;
}


/**
 * Destroys all threads associated with a driver.
 *
 * This function is called by PDMDriver when a driver is destroyed.
 * 
 * @returns VBox status code of the first failure.
 * @param   pVM         The VM handle.
 * @param   pDrvIns     The driver instance.
 */
int pdmR3ThreadDestroyDriver(PVM pVM, PPDMDRVINS pDrvIns)
{
    int rc = VINF_SUCCESS;

    AssertPtr(pDrvIns);
    PPDMTHREAD pThread = pVM->pdm.s.pThreads;
    while (pThread)
    {
        PPDMTHREAD pNext = pThread->Internal.s.pNext;
        if (    pThread->Internal.s.enmType == PDMTHREADTYPE_DRIVER
            &&  pThread->u.Drv.pDrvIns == pDrvIns)
        {
            int rc2 = PDMR3ThreadDestroy(pThread, NULL);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
        }
        pThread = pNext;
    }

    return rc;
}


/**
 * Called For VM power off.
 *
 * @param   pVM         The VM handle.
 */
void pdmR3ThreadDestroyAll(PVM pVM)
{
    PPDMTHREAD pThread = pVM->pdm.s.pThreads;
    while (pThread)
    {
        PPDMTHREAD pNext = pThread->Internal.s.pNext;
        int rc2 = PDMR3ThreadDestroy(pThread, NULL);
        AssertRC(rc2);
        pThread = pNext;
    }
    Assert(!pVM->pdm.s.pThreads && !pVM->pdm.s.pThreadsTail);
}


/**
 * Initiate termination of the thread (self) because something failed in a bad way.
 *
 * @param   pThread         The PDM thread.
 */
static void pdmR3ThreadBailMeOut(PPDMTHREAD pThread)
{
    for (;;)
    {
        PDMTHREADSTATE enmState = pThread->enmState;
        switch (enmState)
        {
            case PDMTHREADSTATE_SUSPENDING:
            case PDMTHREADSTATE_SUSPENDED:
            case PDMTHREADSTATE_RESUMING:
            case PDMTHREADSTATE_RUNNING:
                if (!pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
                    continue;
                break;

            case PDMTHREADSTATE_TERMINATING:
            case PDMTHREADSTATE_TERMINATED:
                break;

            case PDMTHREADSTATE_INITIALIZING:
            default:
                AssertMsgFailed(("enmState=%d\n", enmState));
                break;
        }
        break;
    }
}


/**
 * Called by the PDM thread in response to a wakeup call with
 * suspending as the new state.
 *
 * The thread will block in side this call until the state is changed in
 * response to a VM state change or to the device/driver/whatever calling the
 * PDMR3ThreadResume API.
 *
 * @returns VBox status code.
 *          On failure, terminate the thread.
 * @param   pThread     The PDM thread.
 */
PDMR3DECL(int) PDMR3ThreadIAmSuspending(PPDMTHREAD pThread)
{
    /*
     * Assert sanity.
     */
    AssertPtr(pThread);
    AssertReturn(pThread->u32Version == PDMTHREAD_VERSION, VERR_INVALID_MAGIC);
    Assert(pThread->Thread == RTThreadSelf() || pThread->enmState == PDMTHREADSTATE_INITIALIZING);
    PDMTHREADSTATE enmState = pThread->enmState;
    Assert(     enmState == PDMTHREADSTATE_SUSPENDING
           ||   enmState == PDMTHREADSTATE_INITIALIZING);

    /*
     * Update the state, notify the control thread (the API caller) and go to sleep.
     */
    int rc = VERR_WRONG_ORDER;
    if (pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_SUSPENDED, enmState))
    {
        rc = RTThreadUserSignal(pThread->Thread);
        if (RT_SUCCESS(rc))
        {
            rc = RTSemEventMultiWait(pThread->Internal.s.BlockEvent, RT_INDEFINITE_WAIT);
            if (    RT_SUCCESS(rc)
                &&  pThread->enmState != PDMTHREADSTATE_SUSPENDED)
                return rc;

            if (RT_SUCCESS(rc))
                rc = VERR_INTERNAL_ERROR;
        }
    }

    AssertMsgFailed(("rc=%d enmState=%d\n", rc, pThread->enmState));
    pdmR3ThreadBailMeOut(pThread);
    return rc;
}


/**
 * Called by the PDM thread in response to a resuming state.
 *
 * The purpose of this API is to tell the PDMR3ThreadResume caller that
 * the the PDM thread has successfully resumed. It will also do the
 * state transition from the resuming to the running state.
 *
 * @returns VBox status code.
 *          On failure, terminate the thread.
 * @param   pThread     The PDM thread.
 */
PDMR3DECL(int) PDMR3ThreadIAmRunning(PPDMTHREAD pThread)
{
    /*
     * Assert sanity.
     */
    Assert(pThread->enmState == PDMTHREADSTATE_RESUMING);
    Assert(pThread->Thread == RTThreadSelf());

    /*
     * Update the state and tell the control thread (the guy calling the resume API).
     */
    int rc = VERR_WRONG_ORDER;
    if (pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_RUNNING, PDMTHREADSTATE_RESUMING))
    {
        rc = RTThreadUserSignal(pThread->Thread);
        if (RT_SUCCESS(rc))
            return rc;
    }

    AssertMsgFailed(("rc=%d enmState=%d\n", rc, pThread->enmState));
    pdmR3ThreadBailMeOut(pThread);
    return rc;
}


/**
 * The PDM thread function.
 *
 * @returns return from pfnThread.
 *
 * @param   Thread  The thread handle.
 * @param   pvUser  Pointer to the PDMTHREAD structure.
 */
static DECLCALLBACK(int) pdmR3ThreadMain(RTTHREAD Thread, void *pvUser)
{
    PPDMTHREAD pThread = (PPDMTHREAD)pvUser;
    Log(("PDMThread: Initializing thread %RTthrd / %p / '%s'...\n", Thread, pThread, RTThreadGetName(Thread)));

    /*
     * The run loop.
     *
     * It handles simple thread functions which returns when they see a suspending
     * request and leaves the PDMR3ThreadIAmSuspending and PDMR3ThreadIAmRunning
     * parts to us.
     */
    int rc;
    for (;;)
    {
        switch (pThread->Internal.s.enmType)
        {
            case PDMTHREADTYPE_DEVICE:
                rc = pThread->u.Dev.pfnThread(pThread->u.Dev.pDevIns, pThread);
                break;

            case PDMTHREADTYPE_USB:
                rc = pThread->u.Usb.pfnThread(pThread->u.Usb.pUsbIns, pThread);
                break;

            case PDMTHREADTYPE_DRIVER:
                rc = pThread->u.Drv.pfnThread(pThread->u.Drv.pDrvIns, pThread);
                break;

            case PDMTHREADTYPE_INTERNAL:
                rc = pThread->u.Int.pfnThread(pThread->Internal.s.pVM, pThread);
                break;

            case PDMTHREADTYPE_EXTERNAL:
                rc = pThread->u.Ext.pfnThread(pThread);
                break;

            default:
                AssertMsgFailed(("%d\n", pThread->Internal.s.enmType));
                rc = VERR_INTERNAL_ERROR;
                break;
        }
        if (RT_FAILURE(rc))
            break;

        /*
         * If this is a simple thread function, the state will be suspending
         * or initializing now. If it isn't we're supposed to terminate.
         */
        if (    pThread->enmState != PDMTHREADSTATE_SUSPENDING
            &&  pThread->enmState != PDMTHREADSTATE_INITIALIZING)
        {
            Assert(pThread->enmState == PDMTHREADSTATE_TERMINATING);
            break;
        }
        rc = PDMR3ThreadIAmSuspending(pThread);
        if (RT_FAILURE(rc))
            break;
        if (pThread->enmState != PDMTHREADSTATE_RESUMING)
        {
            Assert(pThread->enmState == PDMTHREADSTATE_TERMINATING);
            break;
        }

        rc = PDMR3ThreadIAmRunning(pThread);
        if (RT_FAILURE(rc))
            break;
    }

    /*
     * Advance the state to terminating and then on to terminated.
     */
    for (;;)
    {
        PDMTHREADSTATE enmState = pThread->enmState;
        if (    enmState == PDMTHREADSTATE_TERMINATING
            ||  pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
            break;
    }

    ASMAtomicXchgSize(&pThread->enmState, PDMTHREADSTATE_TERMINATED);
    int rc2 = RTThreadUserSignal(Thread); AssertRC(rc2);

    Log(("PDMThread: Terminating thread %RTthrd / %p / '%s': %Rrc\n", Thread, pThread, RTThreadGetName(Thread), rc));
    return rc;
}


/**
 * Initiate termination of the thread because something failed in a bad way.
 *
 * @param   pThread         The PDM thread.
 */
static void pdmR3ThreadBailOut(PPDMTHREAD pThread)
{
    for (;;)
    {
        PDMTHREADSTATE enmState = pThread->enmState;
        switch (enmState)
        {
            case PDMTHREADSTATE_SUSPENDING:
            case PDMTHREADSTATE_SUSPENDED:
                if (!pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
                    continue;
                RTSemEventMultiSignal(pThread->Internal.s.BlockEvent);
                break;

            case PDMTHREADSTATE_RESUMING:
                if (!pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
                    continue;
                break;

            case PDMTHREADSTATE_RUNNING:
                if (!pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
                    continue;
                pdmR3ThreadWakeup(pThread);
                break;

            case PDMTHREADSTATE_TERMINATING:
            case PDMTHREADSTATE_TERMINATED:
                break;

            case PDMTHREADSTATE_INITIALIZING:
            default:
                AssertMsgFailed(("enmState=%d\n", enmState));
                break;
        }
        break;
    }
}


/**
 * Suspends the thread.
 *
 * This can be called at the power off / suspend notifications to suspend the
 * PDM thread a bit early. The thread will be automatically suspend upon
 * completion of the device/driver notification cycle.
 * 
 * The caller is responsible for serializing the control operations on the
 * thread. That basically means, always do these calls from the EMT.
 *
 * @returns VBox status code.
 * @param   pThread     The PDM thread.
 */
PDMR3DECL(int) PDMR3ThreadSuspend(PPDMTHREAD pThread)
{
    /*
     * Assert sanity.
     */
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);
    AssertReturn(pThread->u32Version == PDMTHREAD_VERSION, VERR_INVALID_MAGIC);
    Assert(pThread->Thread != RTThreadSelf());

    /*
     * Change the state to resuming and kick the thread.
     */
    int rc = RTSemEventMultiReset(pThread->Internal.s.BlockEvent);
    if (RT_SUCCESS(rc))
    {
        rc = RTThreadUserReset(pThread->Thread);
        if (RT_SUCCESS(rc))
        {
            rc = VERR_WRONG_ORDER;
            if (pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_SUSPENDING, PDMTHREADSTATE_RUNNING))
            {
                rc = pdmR3ThreadWakeup(pThread);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Wait for the thread to reach the suspended state.
                     */
                    if (pThread->enmState != PDMTHREADSTATE_SUSPENDED)
                        rc = RTThreadUserWait(pThread->Thread, 60*1000);
                    if (    RT_SUCCESS(rc)
                        &&  pThread->enmState != PDMTHREADSTATE_SUSPENDED)
                        rc = VERR_INTERNAL_ERROR;
                    if (RT_SUCCESS(rc))
                        return rc;
                }
            }
        }
    }

    /*
     * Something failed, initialize termination.
     */
    AssertMsgFailed(("PDMR3ThreadSuspend -> rc=%Rrc enmState=%d\n", rc, pThread->enmState));
    pdmR3ThreadBailOut(pThread);
    return rc;
}


/**
 * Suspend all running threads.
 *
 * This is called by PDMR3Suspend() and PDMR3PowerOff() after all the devices
 * and drivers have been notified about the suspend / power off.
 *
 * @return VBox status code.
 * @param   pVM         The VM handle.
 */
int pdmR3ThreadSuspendAll(PVM pVM)
{
    for (PPDMTHREAD pThread = pVM->pdm.s.pThreads; pThread; pThread = pThread->Internal.s.pNext)
        switch (pThread->enmState)
        {
            case PDMTHREADSTATE_RUNNING:
            {
                int rc = PDMR3ThreadSuspend(pThread);
                AssertRCReturn(rc, rc);
                break;
            }

            default:
                AssertMsgFailed(("pThread=%p enmState=%d\n", pThread, pThread->enmState));
                break;
        }
    return VINF_SUCCESS;
}


/**
 * Resumes the thread.
 *
 * This can be called the power on / resume notifications to resume the
 * PDM thread a bit early. The thread will be automatically resumed upon
 * return from these two notification callbacks (devices/drivers).
 *
 * The caller is responsible for serializing the control operations on the
 * thread. That basically means, always do these calls from the EMT.
 *
 * @returns VBox status code.
 * @param   pThread     The PDM thread.
 */
PDMR3DECL(int) PDMR3ThreadResume(PPDMTHREAD pThread)
{
    /*
     * Assert sanity.
     */
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);
    AssertReturn(pThread->u32Version == PDMTHREAD_VERSION, VERR_INVALID_MAGIC);
    Assert(pThread->Thread != RTThreadSelf());

    /*
     * Change the state to resuming and kick the thread.
     */
    int rc = RTThreadUserReset(pThread->Thread);
    if (RT_SUCCESS(rc))
    {
        rc = VERR_WRONG_ORDER;
        if (pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_RESUMING, PDMTHREADSTATE_SUSPENDED))
        {
            rc = RTSemEventMultiSignal(pThread->Internal.s.BlockEvent);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Wait for the thread to reach the running state.
                 */
                rc = RTThreadUserWait(pThread->Thread, 60*1000);
                if (    RT_SUCCESS(rc)
                    &&  pThread->enmState != PDMTHREADSTATE_RUNNING)
                    rc = VERR_INTERNAL_ERROR;
                if (RT_SUCCESS(rc))
                    return rc;
            }
        }
    }

    /*
     * Something failed, initialize termination.
     */
    AssertMsgFailed(("PDMR3ThreadResume -> rc=%Rrc enmState=%d\n", rc, pThread->enmState));
    pdmR3ThreadBailOut(pThread);
    return rc;
}


/**
 * Resumes all threads not running.
 *
 * This is called by PDMR3Resume() and PDMR3PowerOn() after all the devices
 * and drivers have been notified about the resume / power on .
 *
 * @return VBox status code.
 * @param   pVM         The VM handle.
 */
int pdmR3ThreadResumeAll(PVM pVM)
{
    for (PPDMTHREAD pThread = pVM->pdm.s.pThreads; pThread; pThread = pThread->Internal.s.pNext)
        switch (pThread->enmState)
        {
            case PDMTHREADSTATE_SUSPENDED:
            {
                int rc = PDMR3ThreadResume(pThread);
                AssertRCReturn(rc, rc);
                break;
            }

            default:
                AssertMsgFailed(("pThread=%p enmState=%d\n", pThread, pThread->enmState));
                break;
        }
    return VINF_SUCCESS;
}

