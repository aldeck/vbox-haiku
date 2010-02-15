/* $Id$ */
/** @file
 * PDM Async I/O - Transport data asynchronous in R3 using EMT.
 */

/*
 * Copyright (C) 2006-2009 Sun Microsystems, Inc.
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_PDM_ASYNC_COMPLETION
#define RT_STRICT
//#define DEBUG
#include "PDMInternal.h"
#include <VBox/pdm.h>
#include <VBox/mm.h>
#include <VBox/vm.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/path.h>

#include "PDMAsyncCompletionFileInternal.h"

/**
 * Frees a task.
 *
 * @returns nothing.
 * @param   pEndpoint    Pointer to the endpoint the segment was for.
 * @param   pTask        The task to free.
 */
void pdmacFileTaskFree(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint,
                       PPDMACTASKFILE pTask)
{
    PPDMASYNCCOMPLETIONEPCLASSFILE pEpClass = (PPDMASYNCCOMPLETIONEPCLASSFILE)pEndpoint->Core.pEpClass;

    LogFlowFunc((": pEndpoint=%p pTask=%p\n", pEndpoint, pTask));

    /* Try the per endpoint cache first. */
    if (pEndpoint->cTasksCached < pEpClass->cTasksCacheMax)
    {
        /* Add it to the list. */
        pEndpoint->pTasksFreeTail->pNext = pTask;
        pEndpoint->pTasksFreeTail        = pTask;
        ASMAtomicIncU32(&pEndpoint->cTasksCached);
    }
    else if (false)
    {
        /* Bigger class cache */
    }
    else
    {
        Log(("Freeing task %p because all caches are full\n", pTask));
        MMR3HeapFree(pTask);
    }
}

/**
 * Allocates a task segment
 *
 * @returns Pointer to the new task segment or NULL
 * @param   pEndpoint    Pointer to the endpoint
 */
PPDMACTASKFILE pdmacFileTaskAlloc(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint)
{
    PPDMACTASKFILE pTask = NULL;

    /* Try the small per endpoint cache first. */
    if (pEndpoint->pTasksFreeHead == pEndpoint->pTasksFreeTail)
    {
        /* Try the bigger endpoint class cache. */
        PPDMASYNCCOMPLETIONEPCLASSFILE pEndpointClass = (PPDMASYNCCOMPLETIONEPCLASSFILE)pEndpoint->Core.pEpClass;

#if 0
        /* We start with the assigned slot id to distribute the load when allocating new tasks. */
        unsigned iSlot = pEndpoint->iSlotStart;
        do
        {
            pTask = (PPDMASYNCCOMPLETIONTASK)ASMAtomicXchgPtr((void * volatile *)&pEndpointClass->apTaskCache[iSlot], NULL);
            if (pTask)
                break;

            iSlot = (iSlot + 1) % RT_ELEMENTS(pEndpointClass->apTaskCache);
        } while (iSlot != pEndpoint->iSlotStart);
#endif
        if (!pTask)
        {
            /*
             * Allocate completely new.
             * If this fails we return NULL.
             */
            int rc = MMR3HeapAllocZEx(pEndpointClass->Core.pVM, MM_TAG_PDM_ASYNC_COMPLETION,
                                      sizeof(PDMACTASKFILE),
                                      (void **)&pTask);
            if (RT_FAILURE(rc))
                pTask = NULL;

            LogFlow(("Allocated task %p\n", pTask));
        }
#if 0
        else
        {
            /* Remove the first element and put the rest into the slot again. */
            PPDMASYNCCOMPLETIONTASK pTaskHeadNew = pTask->pNext;

            pTaskHeadNew->pPrev = NULL;

            /* Put back into the list adding any new tasks. */
            while (true)
            {
                bool fChanged = ASMAtomicCmpXchgPtr((void * volatile *)&pEndpointClass->apTaskCache[iSlot], pTaskHeadNew, NULL);

                if (fChanged)
                    break;

                PPDMASYNCCOMPLETIONTASK pTaskHead = (PPDMASYNCCOMPLETIONTASK)ASMAtomicXchgPtr((void * volatile *)&pEndpointClass->apTaskCache[iSlot], NULL);

                /* The new task could be taken inbetween */
                if (pTaskHead)
                {
                    /* Go to the end of the probably much shorter new list. */
                    PPDMASYNCCOMPLETIONTASK pTaskTail = pTaskHead;
                    while (pTaskTail->pNext)
                        pTaskTail = pTaskTail->pNext;

                    /* Concatenate */
                    pTaskTail->pNext = pTaskHeadNew;

                    pTaskHeadNew = pTaskHead;
                }
                /* Another round trying to change the list. */
            }
            /* We got a task from the global cache so decrement the counter */
            ASMAtomicDecU32(&pEndpointClass->cTasksCached);
        }
#endif
    }
    else
    {
        /* Grab a free task from the head. */
        AssertMsg(pEndpoint->cTasksCached > 0, ("No tasks cached but list contains more than one element\n"));

        pTask = pEndpoint->pTasksFreeHead;
        pEndpoint->pTasksFreeHead = pTask->pNext;
        ASMAtomicDecU32(&pEndpoint->cTasksCached);
    }

    pTask->pNext = NULL;

    return pTask;
}

PPDMACTASKFILE pdmacFileEpGetNewTasks(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint)
{
    PPDMACTASKFILE pTasks = NULL;

    /*
     * Get pending tasks.
     */
    pTasks = (PPDMACTASKFILE)ASMAtomicXchgPtr((void * volatile *)&pEndpoint->pTasksNewHead, NULL);

    /* Reverse the list to process in FIFO order. */
    if (pTasks)
    {
        PPDMACTASKFILE pTask = pTasks;

        pTasks = NULL;

        while (pTask)
        {
            PPDMACTASKFILE pCur = pTask;
            pTask = pTask->pNext;
            pCur->pNext = pTasks;
            pTasks = pCur;
        }
    }

    return pTasks;
}

static void pdmacFileAioMgrWakeup(PPDMACEPFILEMGR pAioMgr)
{
    bool fWokenUp = ASMAtomicXchgBool(&pAioMgr->fWokenUp, true);

    if (!fWokenUp)
    {
        int rc = VINF_SUCCESS;
        bool fWaitingEventSem = ASMAtomicReadBool(&pAioMgr->fWaitingEventSem);

        if (fWaitingEventSem)
            rc = RTSemEventSignal(pAioMgr->EventSem);

        AssertRC(rc);
    }
}

static int pdmacFileAioMgrWaitForBlockingEvent(PPDMACEPFILEMGR pAioMgr, PDMACEPFILEAIOMGRBLOCKINGEVENT enmEvent)
{
    int rc = VINF_SUCCESS;

    ASMAtomicWriteU32((volatile uint32_t *)&pAioMgr->enmBlockingEvent, enmEvent);
    Assert(!pAioMgr->fBlockingEventPending);
    ASMAtomicXchgBool(&pAioMgr->fBlockingEventPending, true);

    /* Wakeup the async I/O manager */
    pdmacFileAioMgrWakeup(pAioMgr);

    /* Wait for completion. */
    rc = RTSemEventWait(pAioMgr->EventSemBlock, RT_INDEFINITE_WAIT);
    AssertRC(rc);

    ASMAtomicXchgBool(&pAioMgr->fBlockingEventPending, false);
    ASMAtomicWriteU32((volatile uint32_t *)&pAioMgr->enmBlockingEvent, PDMACEPFILEAIOMGRBLOCKINGEVENT_INVALID);

    return rc;
}

int pdmacFileAioMgrAddEndpoint(PPDMACEPFILEMGR pAioMgr, PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint)
{
    int rc;

    rc = RTCritSectEnter(&pAioMgr->CritSectBlockingEvent);
    AssertRCReturn(rc, rc);

    ASMAtomicWritePtr((void * volatile *)&pAioMgr->BlockingEventData.AddEndpoint.pEndpoint, pEndpoint);
    rc = pdmacFileAioMgrWaitForBlockingEvent(pAioMgr, PDMACEPFILEAIOMGRBLOCKINGEVENT_ADD_ENDPOINT);

    RTCritSectLeave(&pAioMgr->CritSectBlockingEvent);

    if (RT_SUCCESS(rc))
        ASMAtomicWritePtr((void * volatile *)&pEndpoint->pAioMgr, pAioMgr);

    return rc;
}

static int pdmacFileAioMgrRemoveEndpoint(PPDMACEPFILEMGR pAioMgr, PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint)
{
    int rc;

    rc = RTCritSectEnter(&pAioMgr->CritSectBlockingEvent);
    AssertRCReturn(rc, rc);

    ASMAtomicWritePtr((void * volatile *)&pAioMgr->BlockingEventData.RemoveEndpoint.pEndpoint, pEndpoint);
    rc = pdmacFileAioMgrWaitForBlockingEvent(pAioMgr, PDMACEPFILEAIOMGRBLOCKINGEVENT_REMOVE_ENDPOINT);

    RTCritSectLeave(&pAioMgr->CritSectBlockingEvent);

    return rc;
}

static int pdmacFileAioMgrCloseEndpoint(PPDMACEPFILEMGR pAioMgr, PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint)
{
    int rc;

    rc = RTCritSectEnter(&pAioMgr->CritSectBlockingEvent);
    AssertRCReturn(rc, rc);

    ASMAtomicWritePtr((void * volatile *)&pAioMgr->BlockingEventData.CloseEndpoint.pEndpoint, pEndpoint);
    rc = pdmacFileAioMgrWaitForBlockingEvent(pAioMgr, PDMACEPFILEAIOMGRBLOCKINGEVENT_CLOSE_ENDPOINT);

    RTCritSectLeave(&pAioMgr->CritSectBlockingEvent);

    return rc;
}

static int pdmacFileAioMgrShutdown(PPDMACEPFILEMGR pAioMgr)
{
    int rc;

    rc = RTCritSectEnter(&pAioMgr->CritSectBlockingEvent);
    AssertRCReturn(rc, rc);

    rc = pdmacFileAioMgrWaitForBlockingEvent(pAioMgr, PDMACEPFILEAIOMGRBLOCKINGEVENT_SHUTDOWN);

    RTCritSectLeave(&pAioMgr->CritSectBlockingEvent);

    return rc;
}

int pdmacFileEpAddTask(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint, PPDMACTASKFILE pTask)
{
    PPDMACTASKFILE pNext;
    do
    {
        pNext = pEndpoint->pTasksNewHead;
        pTask->pNext = pNext;
    } while (!ASMAtomicCmpXchgPtr((void * volatile *)&pEndpoint->pTasksNewHead, (void *)pTask, (void *)pNext));

    pdmacFileAioMgrWakeup((PPDMACEPFILEMGR)ASMAtomicReadPtr((void * volatile *)&pEndpoint->pAioMgr));

    return VINF_SUCCESS;
}

void pdmacFileEpTaskCompleted(PPDMACTASKFILE pTask, void *pvUser)
{
    PPDMASYNCCOMPLETIONTASKFILE pTaskFile = (PPDMASYNCCOMPLETIONTASKFILE)pvUser;

    if (pTask->enmTransferType == PDMACTASKFILETRANSFER_FLUSH)
    {
        pdmR3AsyncCompletionCompleteTask(&pTaskFile->Core, true);
    }
    else
    {
        Assert((uint32_t)pTask->DataSeg.cbSeg == pTask->DataSeg.cbSeg && (int32_t)pTask->DataSeg.cbSeg >= 0);
        uint32_t uOld = ASMAtomicSubS32(&pTaskFile->cbTransferLeft, (int32_t)pTask->DataSeg.cbSeg);

        if (!(uOld - pTask->DataSeg.cbSeg)
            && !ASMAtomicXchgBool(&pTaskFile->fCompleted, true))
            pdmR3AsyncCompletionCompleteTask(&pTaskFile->Core, true);
    }
}

int pdmacFileEpTaskInitiate(PPDMASYNCCOMPLETIONTASK pTask,
                            PPDMASYNCCOMPLETIONENDPOINT pEndpoint, RTFOFF off,
                            PCPDMDATASEG paSegments, size_t cSegments,
                            size_t cbTransfer, PDMACTASKFILETRANSFER enmTransfer)
{
    int rc = VINF_SUCCESS;
    PPDMASYNCCOMPLETIONENDPOINTFILE pEpFile = (PPDMASYNCCOMPLETIONENDPOINTFILE)pEndpoint;
    PPDMASYNCCOMPLETIONTASKFILE pTaskFile = (PPDMASYNCCOMPLETIONTASKFILE)pTask;
    PPDMACEPFILEMGR pAioMgr = pEpFile->pAioMgr;

    Assert(   (enmTransfer == PDMACTASKFILETRANSFER_READ)
           || (enmTransfer == PDMACTASKFILETRANSFER_WRITE));

    Assert((uint32_t)cbTransfer == cbTransfer && (uint32_t)cbTransfer >= 0);
    ASMAtomicWriteS32(&pTaskFile->cbTransferLeft, (int32_t)cbTransfer);
    ASMAtomicWriteBool(&pTaskFile->fCompleted, false);

    for (unsigned i = 0; i < cSegments; i++)
    {
        PPDMACTASKFILE pIoTask = pdmacFileTaskAlloc(pEpFile);
        AssertPtr(pIoTask);

        pIoTask->pEndpoint       = pEpFile;
        pIoTask->enmTransferType = enmTransfer;
        pIoTask->Off             = off;
        pIoTask->DataSeg.cbSeg   = paSegments[i].cbSeg;
        pIoTask->DataSeg.pvSeg   = paSegments[i].pvSeg;
        pIoTask->pvUser          = pTaskFile;
        pIoTask->pfnCompleted    = pdmacFileEpTaskCompleted;

        /* Send it off to the I/O manager. */
        pdmacFileEpAddTask(pEpFile, pIoTask);
        off        += paSegments[i].cbSeg;
        cbTransfer -= paSegments[i].cbSeg;
    }

    AssertMsg(!cbTransfer, ("Incomplete transfer %u bytes left\n", cbTransfer));

    if (ASMAtomicReadS32(&pTaskFile->cbTransferLeft) == 0
        && !ASMAtomicXchgBool(&pTaskFile->fCompleted, true))
        pdmR3AsyncCompletionCompleteTask(pTask, false);
    else
        rc = VINF_AIO_TASK_PENDING;

    return rc;
}

/**
 * Creates a new async I/O manager.
 *
 * @returns VBox status code.
 * @param   pEpClass    Pointer to the endpoint class data.
 * @param   ppAioMgr    Where to store the pointer to the new async I/O manager on success.
 * @param   fFailsafe   Flag to force a failsafe manager even if the global flag is not set.
 */
int pdmacFileAioMgrCreate(PPDMASYNCCOMPLETIONEPCLASSFILE pEpClass, PPPDMACEPFILEMGR ppAioMgr, bool fFailsafe)
{
    int rc = VINF_SUCCESS;
    PPDMACEPFILEMGR pAioMgrNew;

    LogFlowFunc((": Entered\n"));

    rc = MMR3HeapAllocZEx(pEpClass->Core.pVM, MM_TAG_PDM_ASYNC_COMPLETION, sizeof(PDMACEPFILEMGR), (void **)&pAioMgrNew);
    if (RT_SUCCESS(rc))
    {
        pAioMgrNew->fFailsafe = fFailsafe || pEpClass->fFailsafe;

        rc = RTSemEventCreate(&pAioMgrNew->EventSem);
        if (RT_SUCCESS(rc))
        {
            rc = RTSemEventCreate(&pAioMgrNew->EventSemBlock);
            if (RT_SUCCESS(rc))
            {
                rc = RTCritSectInit(&pAioMgrNew->CritSectBlockingEvent);
                if (RT_SUCCESS(rc))
                {
                    /* Init the rest of the manager. */
                    if (!pAioMgrNew->fFailsafe)
                        rc = pdmacFileAioMgrNormalInit(pAioMgrNew);

                    if (RT_SUCCESS(rc))
                    {
                        pAioMgrNew->enmState = PDMACEPFILEMGRSTATE_RUNNING;

                        rc = RTThreadCreateF(&pAioMgrNew->Thread,
                                             pAioMgrNew->fFailsafe
                                             ? pdmacFileAioMgrFailsafe
                                             : pdmacFileAioMgrNormal,
                                             pAioMgrNew,
                                             0,
                                             RTTHREADTYPE_IO,
                                             0,
                                             "AioMgr%d-%s", pEpClass->cAioMgrs,
                                             pAioMgrNew->fFailsafe
                                             ? "F"
                                             : "N");
                        if (RT_SUCCESS(rc))
                        {
                            /* Link it into the list. */
                            RTCritSectEnter(&pEpClass->CritSect);
                            pAioMgrNew->pNext = pEpClass->pAioMgrHead;
                            if (pEpClass->pAioMgrHead)
                                pEpClass->pAioMgrHead->pPrev = pAioMgrNew;
                            pEpClass->pAioMgrHead = pAioMgrNew;
                            pEpClass->cAioMgrs++;
                            RTCritSectLeave(&pEpClass->CritSect);

                            *ppAioMgr = pAioMgrNew;

                            Log(("PDMAC: Successfully created new file AIO Mgr {%s}\n", RTThreadGetName(pAioMgrNew->Thread)));
                            return VINF_SUCCESS;
                        }
                        pdmacFileAioMgrNormalDestroy(pAioMgrNew);
                    }
                    RTCritSectDelete(&pAioMgrNew->CritSectBlockingEvent);
                }
                RTSemEventDestroy(pAioMgrNew->EventSem);
            }
            RTSemEventDestroy(pAioMgrNew->EventSemBlock);
        }
        MMR3HeapFree(pAioMgrNew);
    }

    LogFlowFunc((": Leave rc=%Rrc\n", rc));

    return rc;
}

/**
 * Destroys a async I/O manager.
 *
 * @returns nothing.
 * @param   pAioMgr    The async I/O manager to destroy.
 */
static void pdmacFileAioMgrDestroy(PPDMASYNCCOMPLETIONEPCLASSFILE pEpClassFile, PPDMACEPFILEMGR pAioMgr)
{
    int rc = pdmacFileAioMgrShutdown(pAioMgr);
    AssertRC(rc);

    /* Unlink from the list. */
    rc = RTCritSectEnter(&pEpClassFile->CritSect);
    AssertRC(rc);

    PPDMACEPFILEMGR pPrev = pAioMgr->pPrev;
    PPDMACEPFILEMGR pNext = pAioMgr->pNext;

    if (pPrev)
        pPrev->pNext = pNext;
    else
        pEpClassFile->pAioMgrHead = pNext;

    if (pNext)
        pNext->pPrev = pPrev;

    pEpClassFile->cAioMgrs--;

    rc = RTCritSectLeave(&pEpClassFile->CritSect);
    AssertRC(rc);

    /* Free the ressources. */
    RTCritSectDelete(&pAioMgr->CritSectBlockingEvent);
    RTSemEventDestroy(pAioMgr->EventSem);
    if (!pAioMgr->fFailsafe)
        pdmacFileAioMgrNormalDestroy(pAioMgr);

    MMR3HeapFree(pAioMgr);
}

static int pdmacFileInitialize(PPDMASYNCCOMPLETIONEPCLASS pClassGlobals, PCFGMNODE pCfgNode)
{
    int rc = VINF_SUCCESS;
    RTFILEAIOLIMITS AioLimits; /** < Async I/O limitations. */

    PPDMASYNCCOMPLETIONEPCLASSFILE pEpClassFile = (PPDMASYNCCOMPLETIONEPCLASSFILE)pClassGlobals;

    rc = RTFileAioGetLimits(&AioLimits);
#ifdef DEBUG
    if (RT_SUCCESS(rc) && RTEnvExist("VBOX_ASYNC_IO_FAILBACK"))
        rc = VERR_ENV_VAR_NOT_FOUND;
#endif
    if (RT_FAILURE(rc))
    {
        LogRel(("AIO: Async I/O manager not supported (rc=%Rrc). Falling back to failsafe manager\n",
                rc));
        pEpClassFile->fFailsafe = true;
    }
    else
    {
        pEpClassFile->uBitmaskAlignment   = AioLimits.cbBufferAlignment ? ~((RTR3UINTPTR)AioLimits.cbBufferAlignment - 1) : RTR3UINTPTR_MAX;
        pEpClassFile->cReqsOutstandingMax = AioLimits.cReqsOutstandingMax;

        /* The user can force the failsafe manager. */
        rc = CFGMR3QueryBoolDef(pCfgNode, "UseFailsafeIo", &pEpClassFile->fFailsafe, false);
        AssertLogRelRCReturn(rc, rc);

        if (pEpClassFile->fFailsafe)
            LogRel(("AIOMgr: Failsafe I/O was requested by user\n"));
    }

    /* Init critical section. */
    rc = RTCritSectInit(&pEpClassFile->CritSect);
    if (RT_SUCCESS(rc))
    {
        /* Check if the host cache should be used too. */
#ifndef RT_OS_LINUX
        rc = CFGMR3QueryBoolDef(pCfgNode, "HostCacheEnabled", &pEpClassFile->fHostCacheEnabled, false);
        AssertLogRelRCReturn(rc, rc);
#else
        /*
         * Host cache + async I/O is not supported on Linux. Check if the user enabled the cache,
         * leave a warning and disable it always.
         */
        bool fDummy;
        rc = CFGMR3QueryBool(pCfgNode, "HostCacheEnabled", &fDummy);
        if (RT_SUCCESS(rc))
            LogRel(("AIOMgr: The host cache is not supported with async I/O on Linux\n"));

        pEpClassFile->fHostCacheEnabled = false;
#endif

        /* Check if the cache was disabled by the user. */
        rc = CFGMR3QueryBoolDef(pCfgNode, "CacheEnabled", &pEpClassFile->fCacheEnabled, true);
        AssertLogRelRCReturn(rc, rc);

        if (pEpClassFile->fCacheEnabled)
        {
            /* Init cache structure */
            rc = pdmacFileCacheInit(pEpClassFile, pCfgNode);
            if (RT_FAILURE(rc))
            {
                RTCritSectDelete(&pEpClassFile->CritSect);
                pEpClassFile->fCacheEnabled = false;
                LogRel(("AIOMgr: Failed to initialise the cache (rc=%Rrc), disabled caching\n"));
            }
        }
        else
            LogRel(("AIOMgr: Cache was globally disabled\n"));
    }

    return rc;
}

static void pdmacFileTerminate(PPDMASYNCCOMPLETIONEPCLASS pClassGlobals)
{
    PPDMASYNCCOMPLETIONEPCLASSFILE pEpClassFile = (PPDMASYNCCOMPLETIONEPCLASSFILE)pClassGlobals;

    /* All endpoints should be closed at this point. */
    AssertMsg(!pEpClassFile->Core.pEndpointsHead, ("There are still endpoints left\n"));

    /* Destroy all left async I/O managers. */
    while (pEpClassFile->pAioMgrHead)
        pdmacFileAioMgrDestroy(pEpClassFile, pEpClassFile->pAioMgrHead);

    /* Destroy the cache. */
    if (pEpClassFile->fCacheEnabled)
        pdmacFileCacheDestroy(pEpClassFile);

    RTCritSectDelete(&pEpClassFile->CritSect);
}

static int pdmacFileEpInitialize(PPDMASYNCCOMPLETIONENDPOINT pEndpoint,
                                 const char *pszUri, uint32_t fFlags)
{
    int rc = VINF_SUCCESS;
    PPDMASYNCCOMPLETIONENDPOINTFILE pEpFile = (PPDMASYNCCOMPLETIONENDPOINTFILE)pEndpoint;
    PPDMASYNCCOMPLETIONEPCLASSFILE pEpClassFile = (PPDMASYNCCOMPLETIONEPCLASSFILE)pEndpoint->pEpClass;
    bool fUseFailsafeManager = pEpClassFile->fFailsafe;

    AssertMsgReturn((fFlags & ~(PDMACEP_FILE_FLAGS_READ_ONLY | PDMACEP_FILE_FLAGS_CACHING)) == 0,
                    ("PDMAsyncCompletion: Invalid flag specified\n"), VERR_INVALID_PARAMETER);

    unsigned fFileFlags = fFlags & PDMACEP_FILE_FLAGS_READ_ONLY
                         ? RTFILE_O_READ      | RTFILE_O_OPEN | RTFILE_O_DENY_NONE
                         : RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE;

    if (!pEpClassFile->fFailsafe)
    {
        fFileFlags |= (RTFILE_O_ASYNC_IO | RTFILE_O_WRITE_THROUGH);

        /*
         * We only disable the cache if the size of the file is a multiple of 512.
         * Certain hosts like Windows, Linux and Solaris require that transfer sizes
         * are aligned to the volume sector size.
         * If not we just make sure that the data is written to disk with RTFILE_O_WRITE_THROUGH
         * which will trash the host cache but ensures that the host cache will not
         * contain dirty buffers.
         */
        RTFILE File = NIL_RTFILE;

        rc = RTFileOpen(&File, pszUri, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
        if (RT_SUCCESS(rc))
        {
            uint64_t cbSize;

            rc = RTFileGetSize(File, &cbSize);
            if (RT_SUCCESS(rc) && ((cbSize % 512) == 0))
            {
                fFileFlags &= ~RTFILE_O_WRITE_THROUGH;

#if defined(RT_OS_LINUX)
                AssertMsg(!pEpClassFile->fHostCacheEnabled, ("Host cache + async I/O is not supported on Linux\n"));
                fFileFlags |= RTFILE_O_NO_CACHE;
#else
                if (!pEpClassFile->fHostCacheEnabled)
                    fFileFlags |= RTFILE_O_NO_CACHE;
#endif
            }

            pEpFile->cbFile = cbSize;

            RTFileClose(File);
        }
    }

    /* Open with final flags. */
    rc = RTFileOpen(&pEpFile->File, pszUri, fFileFlags);
    if ((rc == VERR_INVALID_FUNCTION) || (rc == VERR_INVALID_PARAMETER))
    {
        LogRel(("pdmacFileEpInitialize: RTFileOpen %s / %08x failed with %Rrc\n",
               pszUri, fFileFlags, rc));
        /*
         * Solaris doesn't support directio on ZFS so far. :-\
         * Trying to enable it returns VERR_INVALID_FUNCTION
         * (ENOTTY). Remove it and hope for the best.
         * ZFS supports write throttling in case applications
         * write more data than can be synced to the disk
         * without blocking the whole application.
         *
         * On Linux we have the same problem with cifs.
         * Have to disable async I/O here too because it requires O_DIRECT.
         */
        fFileFlags &= ~RTFILE_O_NO_CACHE;

#ifdef RT_OS_LINUX
        fFileFlags &= ~RTFILE_O_ASYNC_IO;
        fUseFailsafeManager = true;
#endif

        /* Open again. */
        rc = RTFileOpen(&pEpFile->File, pszUri, fFileFlags);

        if (RT_FAILURE(rc))
        {
            LogRel(("pdmacFileEpInitialize: RTFileOpen %s / %08x failed AGAIN(!) with %Rrc\n",
                        pszUri, fFileFlags, rc));
        }
    }

    if (RT_SUCCESS(rc))
    {
        pEpFile->fFlags = fFileFlags;

        rc = RTFileGetSize(pEpFile->File, (uint64_t *)&pEpFile->cbFile);
        if (RT_SUCCESS(rc) && (pEpFile->cbFile == 0))
        {
            /* Could be a block device */
            rc = RTFileSeek(pEpFile->File, 0, RTFILE_SEEK_END, (uint64_t *)&pEpFile->cbFile);
        }

        if (RT_SUCCESS(rc))
        {
            /* Initialize the segment cache */
            rc = MMR3HeapAllocZEx(pEpClassFile->Core.pVM, MM_TAG_PDM_ASYNC_COMPLETION,
                                  sizeof(PDMACTASKFILE),
                                  (void **)&pEpFile->pTasksFreeHead);
            if (RT_SUCCESS(rc))
            {
                PPDMACEPFILEMGR pAioMgr = NULL;

                pEpFile->pTasksFreeTail = pEpFile->pTasksFreeHead;
                pEpFile->cTasksCached = 0;

                if (fUseFailsafeManager)
                {
                    /* Safe mode. Every file has its own async I/O manager. */
                    rc = pdmacFileAioMgrCreate(pEpClassFile, &pAioMgr, true);
                    AssertRC(rc);
                }
                else
                {
                    if (   (fFlags & PDMACEP_FILE_FLAGS_CACHING)
                        && (pEpClassFile->fCacheEnabled))
                    {
                        pEpFile->fCaching = true;
                        rc = pdmacFileEpCacheInit(pEpFile, pEpClassFile);
                        if (RT_FAILURE(rc))
                        {
                            LogRel(("AIOMgr: Endpoint for \"%s\" was opened with caching but initializing cache failed. Disabled caching\n", pszUri));
                            pEpFile->fCaching = false;
                        }
                    }

                    pAioMgr = pEpClassFile->pAioMgrHead;

                    /* Check for an idling not failsafe one or create new if not found */
                    while (pAioMgr && pAioMgr->fFailsafe)
                        pAioMgr = pAioMgr->pNext;

                    if (!pAioMgr)
                    {
                        rc = pdmacFileAioMgrCreate(pEpClassFile, &pAioMgr, false);
                        AssertRC(rc);
                    }
                }

                pEpFile->AioMgr.pTreeRangesLocked = (PAVLRFOFFTREE)RTMemAllocZ(sizeof(AVLRFOFFTREE));
                if (!pEpFile->AioMgr.pTreeRangesLocked)
                    rc = VERR_NO_MEMORY;
                else
                {
                    pEpFile->enmState = PDMASYNCCOMPLETIONENDPOINTFILESTATE_ACTIVE;

                    /* Assign the endpoint to the thread. */
                    rc = pdmacFileAioMgrAddEndpoint(pAioMgr, pEpFile);
                    if (RT_FAILURE(rc))
                    {
                        RTMemFree(pEpFile->AioMgr.pTreeRangesLocked);
                        MMR3HeapFree(pEpFile->pTasksFreeHead);
                    }
                }
            }
        }

        if (RT_FAILURE(rc))
            RTFileClose(pEpFile->File);
    }

#ifdef VBOX_WITH_STATISTICS
    if (RT_SUCCESS(rc))
    {
        STAMR3RegisterF(pEpClassFile->Core.pVM, &pEpFile->StatRead,
                       STAMTYPE_PROFILE_ADV, STAMVISIBILITY_ALWAYS,
                       STAMUNIT_TICKS_PER_CALL, "Time taken to read from the endpoint",
                       "/PDM/AsyncCompletion/File/%s/Read", RTPathFilename(pEpFile->Core.pszUri));

        STAMR3RegisterF(pEpClassFile->Core.pVM, &pEpFile->StatWrite,
                       STAMTYPE_PROFILE_ADV, STAMVISIBILITY_ALWAYS,
                       STAMUNIT_TICKS_PER_CALL, "Time taken to write to the endpoint",
                       "/PDM/AsyncCompletion/File/%s/Write", RTPathFilename(pEpFile->Core.pszUri));
    }
#endif

    return rc;
}

static int pdmacFileEpRangesLockedDestroy(PAVLRFOFFNODECORE pNode, void *pvUser)
{
    AssertMsgFailed(("The locked ranges tree should be empty at that point\n"));
    return VINF_SUCCESS;
}

static int pdmacFileEpClose(PPDMASYNCCOMPLETIONENDPOINT pEndpoint)
{
    PPDMASYNCCOMPLETIONENDPOINTFILE pEpFile = (PPDMASYNCCOMPLETIONENDPOINTFILE)pEndpoint;
    PPDMASYNCCOMPLETIONEPCLASSFILE pEpClassFile = (PPDMASYNCCOMPLETIONEPCLASSFILE)pEndpoint->pEpClass;

    /* Make sure that all tasks finished for this endpoint. */
    int rc = pdmacFileAioMgrCloseEndpoint(pEpFile->pAioMgr, pEpFile);
    AssertRC(rc);

    /*
     * If the async I/O manager is in failsafe mode this is the only endpoint
     * he processes and thus can be destroyed now.
     */
    if (pEpFile->pAioMgr->fFailsafe)
        pdmacFileAioMgrDestroy(pEpClassFile, pEpFile->pAioMgr);

    /* Free cached tasks. */
    PPDMACTASKFILE pTask = pEpFile->pTasksFreeHead;

    while (pTask)
    {
        PPDMACTASKFILE pTaskFree = pTask;
        pTask = pTask->pNext;
        MMR3HeapFree(pTaskFree);
    }

    /* Free the cached data. */
    if (pEpFile->fCaching)
        pdmacFileEpCacheDestroy(pEpFile);

    /* Destroy the locked ranges tree now. */
    RTAvlrFileOffsetDestroy(pEpFile->AioMgr.pTreeRangesLocked, pdmacFileEpRangesLockedDestroy, NULL);

    RTFileClose(pEpFile->File);

#ifdef VBOX_WITH_STATISTICS
    STAMR3Deregister(pEpClassFile->Core.pVM, &pEpFile->StatRead);
    STAMR3Deregister(pEpClassFile->Core.pVM, &pEpFile->StatWrite);
#endif

    return VINF_SUCCESS;
}

static int pdmacFileEpRead(PPDMASYNCCOMPLETIONTASK pTask,
                           PPDMASYNCCOMPLETIONENDPOINT pEndpoint, RTFOFF off,
                           PCPDMDATASEG paSegments, size_t cSegments,
                           size_t cbRead)
{
    int rc = VINF_SUCCESS;
    PPDMASYNCCOMPLETIONENDPOINTFILE pEpFile = (PPDMASYNCCOMPLETIONENDPOINTFILE)pEndpoint;

    STAM_PROFILE_ADV_START(&pEpFile->StatRead, Read);

    if (pEpFile->fCaching)
        rc = pdmacFileEpCacheRead(pEpFile, (PPDMASYNCCOMPLETIONTASKFILE)pTask,
                                  off, paSegments, cSegments, cbRead);
    else
        rc = pdmacFileEpTaskInitiate(pTask, pEndpoint, off, paSegments, cSegments, cbRead,
                                     PDMACTASKFILETRANSFER_READ);

    STAM_PROFILE_ADV_STOP(&pEpFile->StatRead, Read);

    return rc;
}

static int pdmacFileEpWrite(PPDMASYNCCOMPLETIONTASK pTask,
                            PPDMASYNCCOMPLETIONENDPOINT pEndpoint, RTFOFF off,
                            PCPDMDATASEG paSegments, size_t cSegments,
                            size_t cbWrite)
{
    int rc = VINF_SUCCESS;
    PPDMASYNCCOMPLETIONENDPOINTFILE pEpFile = (PPDMASYNCCOMPLETIONENDPOINTFILE)pEndpoint;

    if (RT_UNLIKELY(pEpFile->fReadonly))
        return VERR_NOT_SUPPORTED;

    STAM_PROFILE_ADV_START(&pEpFile->StatWrite, Write);

    if (pEpFile->fCaching)
        rc = pdmacFileEpCacheWrite(pEpFile, (PPDMASYNCCOMPLETIONTASKFILE)pTask,
                                   off, paSegments, cSegments, cbWrite);
    else
        rc = pdmacFileEpTaskInitiate(pTask, pEndpoint, off, paSegments, cSegments, cbWrite,
                                     PDMACTASKFILETRANSFER_WRITE);

    STAM_PROFILE_ADV_STOP(&pEpFile->StatWrite, Write);

    return rc;
}

static int pdmacFileEpFlush(PPDMASYNCCOMPLETIONTASK pTask,
                            PPDMASYNCCOMPLETIONENDPOINT pEndpoint)
{
    int rc = VINF_SUCCESS;
    PPDMASYNCCOMPLETIONENDPOINTFILE pEpFile = (PPDMASYNCCOMPLETIONENDPOINTFILE)pEndpoint;
    PPDMASYNCCOMPLETIONTASKFILE pTaskFile = (PPDMASYNCCOMPLETIONTASKFILE)pTask;

    if (RT_UNLIKELY(pEpFile->fReadonly))
        return VERR_NOT_SUPPORTED;

    pTaskFile->cbTransferLeft = 0;

    if (pEpFile->fCaching)
        rc = pdmacFileEpCacheFlush(pEpFile, pTaskFile);
    else
    {
        PPDMACTASKFILE pIoTask = pdmacFileTaskAlloc(pEpFile);
        AssertPtr(pIoTask);

        pIoTask->pEndpoint       = pEpFile;
        pIoTask->enmTransferType = PDMACTASKFILETRANSFER_FLUSH;
        pIoTask->pvUser          = pTaskFile;
        pIoTask->pfnCompleted    = pdmacFileEpTaskCompleted;
        pdmacFileEpAddTask(pEpFile, pIoTask);
        rc = VINF_AIO_TASK_PENDING;
    }

    return rc;
}

static int pdmacFileEpGetSize(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, uint64_t *pcbSize)
{
    PPDMASYNCCOMPLETIONENDPOINTFILE pEpFile = (PPDMASYNCCOMPLETIONENDPOINTFILE)pEndpoint;

    *pcbSize = ASMAtomicReadU64(&pEpFile->cbFile);

    return VINF_SUCCESS;
}

const PDMASYNCCOMPLETIONEPCLASSOPS g_PDMAsyncCompletionEndpointClassFile =
{
    /* u32Version */
    PDMAC_EPCLASS_OPS_VERSION,
    /* pcszName */
    "File",
    /* enmClassType */
    PDMASYNCCOMPLETIONEPCLASSTYPE_FILE,
    /* cbEndpointClassGlobal */
    sizeof(PDMASYNCCOMPLETIONEPCLASSFILE),
    /* cbEndpoint */
    sizeof(PDMASYNCCOMPLETIONENDPOINTFILE),
    /* cbTask */
    sizeof(PDMASYNCCOMPLETIONTASKFILE),
    /* pfnInitialize */
    pdmacFileInitialize,
    /* pfnTerminate */
    pdmacFileTerminate,
    /* pfnEpInitialize. */
    pdmacFileEpInitialize,
    /* pfnEpClose */
    pdmacFileEpClose,
    /* pfnEpRead */
    pdmacFileEpRead,
    /* pfnEpWrite */
    pdmacFileEpWrite,
    /* pfnEpFlush */
    pdmacFileEpFlush,
    /* pfnEpGetSize */
    pdmacFileEpGetSize,
    /* u32VersionEnd */
    PDMAC_EPCLASS_OPS_VERSION
};

