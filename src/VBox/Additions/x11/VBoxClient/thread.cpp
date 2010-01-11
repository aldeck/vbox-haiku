/** @file
 *
 * VirtualBox additions client application: thread class.
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
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#include <VBox/log.h>
#include <iostream>   /* For std::exception */

#include "thread.h"

/** Stop the thread using its stop method and get the exit value. */
int VBoxGuestThread::stop(RTMSINTERVAL cMillies, int *prc)
{
    int rc = VINF_SUCCESS;

    LogFlowThisFunc(("\n"));
    if (NIL_RTTHREAD == mSelf)  /* Assertion */
    {
        LogRelThisFunc(("Attempted to stop thread %s which is not running!\n", mName));
        return VERR_INTERNAL_ERROR;
    }
    mExit = true;
    mFunction->stop();
    if (0 != (mFlags & RTTHREADFLAGS_WAITABLE))
    {
        rc = RTThreadWait(mSelf, cMillies, prc);
        if (RT_SUCCESS(rc))
        {
            mSelf = NIL_RTTHREAD;
        }
        else
        {
            LogRelThisFunc(("Failed to stop thread %s!\n", mName));
        }
    }
    LogFlowThisFunc(("returning %Rrc\n", rc));
    return rc;
}

/** Destroy the class, stopping the thread if necessary. */
VBoxGuestThread::~VBoxGuestThread(void)
{
    LogFlowThisFunc(("\n"));
    if (NIL_RTTHREAD != mSelf)
    {
        LogRelThisFunc(("Warning!  Stopping thread %s, as it is still running!\n", mName));
        try
        {
            stop(2000, 0);
        }
        catch(...) {}
    }
    LogFlowThisFunc(("returning\n"));
}

/** Start the thread. */
int VBoxGuestThread::start(void)
{
    int rc = VINF_SUCCESS;

    LogFlowThisFunc(("returning\n"));
    if (NIL_RTTHREAD != mSelf)  /* Assertion */
    {
        LogRelThisFunc(("Attempted to start thead %s twice!\n", mName));
        return VERR_INTERNAL_ERROR;
    }
    mExit = false;
    rc = RTThreadCreate(&mSelf, threadFunction, reinterpret_cast<void *>(this),
                          mStack, mType, mFlags, mName);
    LogFlowThisFunc(("returning %Rrc\n", rc));
    return rc;
}

/** Yield the CPU */
bool VBoxGuestThread::yield(void)
{
    return RTThreadYield();
}

/** The "real" thread function for the VBox runtime. */
int VBoxGuestThread::threadFunction(RTTHREAD self, void *pvUser)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("\n"));
    PSELF pSelf = reinterpret_cast<PSELF>(pvUser);
    pSelf->mRunning = true;
    try
    {
        rc = pSelf->mFunction->threadFunction(pSelf);
    }
    catch (const std::exception &e)
    {
        LogRelFunc(("Caught exception in thread: %s\n", e.what()));
        rc = VERR_UNRESOLVED_ERROR;
    }
    catch (...)
    {
        LogRelFunc(("Caught unknown exception in thread.\n"));
        rc = VERR_UNRESOLVED_ERROR;
    }
    pSelf->mRunning = false;
    LogFlowFunc(("returning %Rrc\n", rc));
    return rc;
}
