/* $Id$ */
/** @file
 * PDM Async I/O - Transport data asynchronous in R3 using EMT.
 * Simple File I/O manager.
 */

/*
 * Copyright (C) 2006-2008 Oracle Corporation
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
#define LOG_GROUP LOG_GROUP_PDM_ASYNC_COMPLETION
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <VBox/log.h>

#include "PDMAsyncCompletionFileInternal.h"


static int pdmacFileAioMgrFailsafeProcessEndpointTaskList(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint,
                                                          PPDMACTASKFILE pTasks)
{
    int rc = VINF_SUCCESS;

    while (pTasks)
    {
        PPDMACTASKFILE pCurr = pTasks;

        pTasks = pTasks->pNext;

        switch (pCurr->enmTransferType)
        {
            case PDMACTASKFILETRANSFER_FLUSH:
            {
                rc = RTFileFlush(pEndpoint->File);
                break;
            }
            case PDMACTASKFILETRANSFER_READ:
            case PDMACTASKFILETRANSFER_WRITE:
            {
                if (pCurr->enmTransferType == PDMACTASKFILETRANSFER_READ)
                {
                    rc = RTFileReadAt(pEndpoint->File, pCurr->Off,
                                      pCurr->DataSeg.pvSeg,
                                      pCurr->DataSeg.cbSeg,
                                      NULL);
                }
                else
                {
                    if (RT_UNLIKELY((uint64_t)pCurr->Off + pCurr->DataSeg.cbSeg > pEndpoint->cbFile))
                    {
                        ASMAtomicWriteU64(&pEndpoint->cbFile, pCurr->Off + pCurr->DataSeg.cbSeg);
                        RTFileSetSize(pEndpoint->File, pCurr->Off + pCurr->DataSeg.cbSeg);
                    }

                    rc = RTFileWriteAt(pEndpoint->File, pCurr->Off,
                                       pCurr->DataSeg.pvSeg,
                                       pCurr->DataSeg.cbSeg,
                                       NULL);
                }

                break;
            }
            default:
                AssertMsgFailed(("Invalid transfer type %d\n", pTasks->enmTransferType));
        }

        pCurr->pfnCompleted(pCurr, pCurr->pvUser, rc);
        pdmacFileTaskFree(pEndpoint, pCurr);
    }

    return VINF_SUCCESS;
}

static int pdmacFileAioMgrFailsafeProcessEndpoint(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint)
{
    int rc = VINF_SUCCESS;
    PPDMACTASKFILE pTasks = pEndpoint->AioMgr.pReqsPendingHead;

    pEndpoint->AioMgr.pReqsPendingHead = NULL;
    pEndpoint->AioMgr.pReqsPendingTail = NULL;

    /* Process the request pending list first in case the endpoint was migrated due to an error. */
    if (pTasks)
        rc = pdmacFileAioMgrFailsafeProcessEndpointTaskList(pEndpoint, pTasks);

    if (RT_SUCCESS(rc))
    {
        pTasks = pdmacFileEpGetNewTasks(pEndpoint);

        if (pTasks)
            rc = pdmacFileAioMgrFailsafeProcessEndpointTaskList(pEndpoint, pTasks);
    }

    return rc;
}

/**
 * A fallback method in case something goes wrong with the normal
 * I/O manager.
 */
int pdmacFileAioMgrFailsafe(RTTHREAD ThreadSelf, void *pvUser)
{
    int rc = VINF_SUCCESS;
    PPDMACEPFILEMGR pAioMgr = (PPDMACEPFILEMGR)pvUser;

    while (   (pAioMgr->enmState == PDMACEPFILEMGRSTATE_RUNNING)
           || (pAioMgr->enmState == PDMACEPFILEMGRSTATE_SUSPENDING))
    {
        if (!ASMAtomicReadBool(&pAioMgr->fWokenUp))
        {
            ASMAtomicWriteBool(&pAioMgr->fWaitingEventSem, true);
            rc = RTSemEventWait(pAioMgr->EventSem, RT_INDEFINITE_WAIT);
            ASMAtomicWriteBool(&pAioMgr->fWaitingEventSem, false);
            AssertRC(rc);
        }
        ASMAtomicXchgBool(&pAioMgr->fWokenUp, false);

        /* Process endpoint events first. */
        PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint = pAioMgr->pEndpointsHead;
        while (pEndpoint)
        {
            rc = pdmacFileAioMgrFailsafeProcessEndpoint(pEndpoint);
            AssertRC(rc);
            pEndpoint = pEndpoint->AioMgr.pEndpointNext;
        }

        /* Now check for an external blocking event. */
        if (pAioMgr->fBlockingEventPending)
        {
            switch (pAioMgr->enmBlockingEvent)
            {
                case PDMACEPFILEAIOMGRBLOCKINGEVENT_ADD_ENDPOINT:
                {
                    PPDMASYNCCOMPLETIONENDPOINTFILE pEndpointNew = pAioMgr->BlockingEventData.AddEndpoint.pEndpoint;
                    AssertMsg(VALID_PTR(pEndpointNew), ("Adding endpoint event without a endpoint to add\n"));

                    pEndpointNew->enmState = PDMASYNCCOMPLETIONENDPOINTFILESTATE_ACTIVE;

                    pEndpointNew->AioMgr.pEndpointNext = pAioMgr->pEndpointsHead;
                    pEndpointNew->AioMgr.pEndpointPrev = NULL;
                    if (pAioMgr->pEndpointsHead)
                        pAioMgr->pEndpointsHead->AioMgr.pEndpointPrev = pEndpointNew;
                    pAioMgr->pEndpointsHead = pEndpointNew;

                    pAioMgr->cEndpoints++;

                    /*
                     * Process the task list the first time. There might be pending requests
                     * if the endpoint was migrated from another endpoint.
                     */
                    rc = pdmacFileAioMgrFailsafeProcessEndpoint(pEndpointNew);
                    AssertRC(rc);
                    break;
                }
                case PDMACEPFILEAIOMGRBLOCKINGEVENT_REMOVE_ENDPOINT:
                {
                    PPDMASYNCCOMPLETIONENDPOINTFILE pEndpointRemove = pAioMgr->BlockingEventData.RemoveEndpoint.pEndpoint;
                    AssertMsg(VALID_PTR(pEndpointRemove), ("Removing endpoint event without a endpoint to remove\n"));

                    pEndpointRemove->enmState = PDMASYNCCOMPLETIONENDPOINTFILESTATE_REMOVING;

                    PPDMASYNCCOMPLETIONENDPOINTFILE pPrev = pEndpointRemove->AioMgr.pEndpointPrev;
                    PPDMASYNCCOMPLETIONENDPOINTFILE pNext = pEndpointRemove->AioMgr.pEndpointNext;

                    if (pPrev)
                        pPrev->AioMgr.pEndpointNext = pNext;
                    else
                        pAioMgr->pEndpointsHead = pNext;

                    if (pNext)
                        pNext->AioMgr.pEndpointPrev = pPrev;

                    pAioMgr->cEndpoints--;
                    break;
                }
                case PDMACEPFILEAIOMGRBLOCKINGEVENT_CLOSE_ENDPOINT:
                {
                    PPDMASYNCCOMPLETIONENDPOINTFILE pEndpointClose = pAioMgr->BlockingEventData.CloseEndpoint.pEndpoint;
                    AssertMsg(VALID_PTR(pEndpointClose), ("Close endpoint event without a endpoint to Close\n"));

                    pEndpointClose->enmState = PDMASYNCCOMPLETIONENDPOINTFILESTATE_CLOSING;

                    /* Make sure all tasks finished. */
                    rc = pdmacFileAioMgrFailsafeProcessEndpoint(pEndpointClose);
                    AssertRC(rc);
                    break;
                }
                case PDMACEPFILEAIOMGRBLOCKINGEVENT_SHUTDOWN:
                    pAioMgr->enmState = PDMACEPFILEMGRSTATE_SHUTDOWN;
                    break;
                case PDMACEPFILEAIOMGRBLOCKINGEVENT_SUSPEND:
                    pAioMgr->enmState = PDMACEPFILEMGRSTATE_SUSPENDING;
                    break;
                case PDMACEPFILEAIOMGRBLOCKINGEVENT_RESUME:
                    pAioMgr->enmState = PDMACEPFILEMGRSTATE_RUNNING;
                    break;
                default:
                    AssertMsgFailed(("Invalid event type %d\n", pAioMgr->enmBlockingEvent));
            }

            /* Release the waiting thread. */
            rc = RTSemEventSignal(pAioMgr->EventSemBlock);
            AssertRC(rc);
        }
    }

    return rc;
}

