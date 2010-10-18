/* $Id$ */
/** @file
 * PDM Async I/O - Transport data asynchronous in R3 using EMT.
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

#ifndef ___PDMAsyncCompletionFileInternal_h
#define ___PDMAsyncCompletionFileInternal_h

#include <VBox/cfgm.h>
#include <VBox/stam.h>
#include <VBox/tm.h>
#include <iprt/types.h>
#include <iprt/file.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/critsect.h>
#include <iprt/avl.h>
#include <iprt/list.h>
#include <iprt/spinlock.h>
#include <iprt/memcache.h>

#include "PDMAsyncCompletionInternal.h"

/** @todo: Revise the caching of tasks. We have currently four caches:
 *  Per endpoint task cache
 *  Per class cache
 *  Per endpoint task segment cache
 *  Per class task segment cache
 *
 *  We could use the RT heap for this probably or extend MMR3Heap (uses RTMemAlloc
 *  instead of managing larger blocks) to have this global for the whole VM.
 */

RT_C_DECLS_BEGIN

/**
 * A few forward declerations.
 */
typedef struct PDMASYNCCOMPLETIONENDPOINTFILE *PPDMASYNCCOMPLETIONENDPOINTFILE;
/** Pointer to a request segment. */
typedef struct PDMACTASKFILE *PPDMACTASKFILE;
/** Pointer to the endpoint class data. */
typedef struct PDMASYNCCOMPLETIONTASKFILE *PPDMASYNCCOMPLETIONTASKFILE;
/** Pointer to a cache LRU list. */
typedef struct PDMACFILELRULIST *PPDMACFILELRULIST;
/** Pointer to the global cache structure. */
typedef struct PDMACFILECACHEGLOBAL *PPDMACFILECACHEGLOBAL;
/** Pointer to a task segment. */
typedef struct PDMACFILETASKSEG *PPDMACFILETASKSEG;

/**
 * Blocking event types.
 */
typedef enum PDMACEPFILEAIOMGRBLOCKINGEVENT
{
    /** Invalid tye */
    PDMACEPFILEAIOMGRBLOCKINGEVENT_INVALID = 0,
    /** An endpoint is added to the manager. */
    PDMACEPFILEAIOMGRBLOCKINGEVENT_ADD_ENDPOINT,
    /** An endpoint is removed from the manager. */
    PDMACEPFILEAIOMGRBLOCKINGEVENT_REMOVE_ENDPOINT,
    /** An endpoint is about to be closed. */
    PDMACEPFILEAIOMGRBLOCKINGEVENT_CLOSE_ENDPOINT,
    /** The manager is requested to terminate */
    PDMACEPFILEAIOMGRBLOCKINGEVENT_SHUTDOWN,
    /** The manager is requested to suspend */
    PDMACEPFILEAIOMGRBLOCKINGEVENT_SUSPEND,
    /** The manager is requested to resume */
    PDMACEPFILEAIOMGRBLOCKINGEVENT_RESUME,
    /** 32bit hack */
    PDMACEPFILEAIOMGRBLOCKINGEVENT_32BIT_HACK = 0x7fffffff
} PDMACEPFILEAIOMGRBLOCKINGEVENT;

/**
 * I/O manager type.
 */
typedef enum PDMACEPFILEMGRTYPE
{
    /** Simple aka failsafe */
    PDMACEPFILEMGRTYPE_SIMPLE = 0,
    /** Async I/O with host cache enabled. */
    PDMACEPFILEMGRTYPE_ASYNC,
    /** 32bit hack */
    PDMACEPFILEMGRTYPE_32BIT_HACK = 0x7fffffff
} PDMACEPFILEMGRTYPE;
/** Pointer to a I/O manager type */
typedef PDMACEPFILEMGRTYPE *PPDMACEPFILEMGRTYPE;

/**
 * States of the I/O manager.
 */
typedef enum PDMACEPFILEMGRSTATE
{
    /** Invalid state. */
    PDMACEPFILEMGRSTATE_INVALID = 0,
    /** Normal running state accepting new requests
     * and processing them.
     */
    PDMACEPFILEMGRSTATE_RUNNING,
    /** Fault state - not accepting new tasks for endpoints but waiting for
     * remaining ones to finish.
     */
    PDMACEPFILEMGRSTATE_FAULT,
    /** Suspending state - not accepting new tasks for endpoints but waiting
     * for remaining ones to finish.
     */
    PDMACEPFILEMGRSTATE_SUSPENDING,
    /** Shutdown state - not accepting new tasks for endpoints but waiting
     * for remaining ones to finish.
     */
    PDMACEPFILEMGRSTATE_SHUTDOWN,
    /** The I/O manager waits for all active requests to complete and doesn't queue
     * new ones because it needs to grow to handle more requests.
     */
    PDMACEPFILEMGRSTATE_GROWING,
    /** 32bit hack */
    PDMACEPFILEMGRSTATE_32BIT_HACK = 0x7fffffff
} PDMACEPFILEMGRSTATE;

/**
 * State of a async I/O manager.
 */
typedef struct PDMACEPFILEMGR
{
    /** Next Aio manager in the list. */
    R3PTRTYPE(struct PDMACEPFILEMGR *)     pNext;
    /** Previous Aio manager in the list. */
    R3PTRTYPE(struct PDMACEPFILEMGR *)     pPrev;
    /** Manager type */
    PDMACEPFILEMGRTYPE                     enmMgrType;
    /** Current state of the manager. */
    PDMACEPFILEMGRSTATE                    enmState;
    /** Event semaphore the manager sleeps on when waiting for new requests. */
    RTSEMEVENT                             EventSem;
    /** Flag whether the thread waits in the event semaphore. */
    volatile bool                          fWaitingEventSem;
    /** Thread data */
    RTTHREAD                               Thread;
    /** The async I/O context for this manager. */
    RTFILEAIOCTX                           hAioCtx;
    /** Flag whether the I/O manager was woken up. */
    volatile bool                          fWokenUp;
    /** List of endpoints assigned to this manager. */
    R3PTRTYPE(PPDMASYNCCOMPLETIONENDPOINTFILE) pEndpointsHead;
    /** Number of endpoints assigned to the manager. */
    unsigned                               cEndpoints;
    /** Number of requests active currently. */
    unsigned                               cRequestsActive;
    /** Number of maximum requests active. */
    uint32_t                               cRequestsActiveMax;
    /** Pointer to an array of free async I/O request handles. */
    RTFILEAIOREQ                          *pahReqsFree;
    /** Index of the next free entry in the cache. */
    uint32_t                               iFreeEntry;
    /** Size of the array. */
    unsigned                               cReqEntries;
    /** Flag whether at least one endpoint reached its bandwidth limit. */
    bool                                   fBwLimitReached;
    /** Memory cache for file range locks. */
    RTMEMCACHE                             hMemCacheRangeLocks;
    /** Critical section protecting the blocking event handling. */
    RTCRITSECT                             CritSectBlockingEvent;
    /** Event sempahore for blocking external events.
     * The caller waits on it until the async I/O manager
     * finished processing the event. */
    RTSEMEVENT                             EventSemBlock;
    /** Flag whether a blocking event is pending and needs
     * processing by the I/O manager. */
    volatile bool                          fBlockingEventPending;
    /** Blocking event type */
    volatile PDMACEPFILEAIOMGRBLOCKINGEVENT enmBlockingEvent;
    /** Event type data */
    union
    {
        /** Add endpoint event. */
        struct
        {
            /** The endpoint to be added */
            volatile PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint;
        } AddEndpoint;
        /** Remove endpoint event. */
        struct
        {
            /** The endpoint to be removed */
            volatile PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint;
        } RemoveEndpoint;
        /** Close endpoint event. */
        struct
        {
            /** The endpoint to be closed */
            volatile PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint;
        } CloseEndpoint;
    } BlockingEventData;
} PDMACEPFILEMGR;
/** Pointer to a async I/O manager state. */
typedef PDMACEPFILEMGR *PPDMACEPFILEMGR;
/** Pointer to a async I/O manager state pointer. */
typedef PPDMACEPFILEMGR *PPPDMACEPFILEMGR;

/**
 * A file access range lock.
 */
typedef struct PDMACFILERANGELOCK
{
    /** AVL node in the locked range tree of the endpoint. */
    AVLRFOFFNODECORE            Core;
    /** How many tasks have locked this range. */
    uint32_t                    cRefs;
    /** Flag whether this is a read or write lock. */
    bool                        fReadLock;
    /** List of tasks which are waiting that the range gets unlocked. */
    PPDMACTASKFILE              pWaitingTasksHead;
    /** List of tasks which are waiting that the range gets unlocked. */
    PPDMACTASKFILE              pWaitingTasksTail;
} PDMACFILERANGELOCK, *PPDMACFILERANGELOCK;

/**
 * Data for one request segment waiting for cache entry.
 */
typedef struct PDMACFILETASKSEG
{
    /** Next task segment in the list. */
    struct PDMACFILETASKSEG    *pNext;
    /** Task this segment is for. */
    PPDMASYNCCOMPLETIONTASKFILE pTask;
    /** Offset into the cache entry buffer to start reading from. */
    uint32_t                    uBufOffset;
    /** Number of bytes to transfer. */
    size_t                      cbTransfer;
    /** Pointer to the buffer. */
    void                       *pvBuf;
    /** Flag whether this entry writes data to the cache. */
    bool                        fWrite;
} PDMACFILETASKSEG;

/**
 * A cache entry
 */
typedef struct PDMACFILECACHEENTRY
{
    /** The AVL entry data. */
    AVLRFOFFNODECORE                Core;
    /** Pointer to the previous element. Used in one of the LRU lists.*/
    struct PDMACFILECACHEENTRY     *pPrev;
    /** Pointer to the next element. Used in one of the LRU lists.*/
    struct PDMACFILECACHEENTRY     *pNext;
    /** Pointer to the list the entry is in. */
    PPDMACFILELRULIST               pList;
    /** Pointer to the global cache structure. */
    PPDMACFILECACHEGLOBAL           pCache;
    /** Endpoint the entry belongs to. */
    PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint;
    /** Flags for this entry. Combinations of PDMACFILECACHE_* #defines */
    volatile uint32_t               fFlags;
    /** Reference counter. Prevents eviction of the entry if > 0. */
    volatile uint32_t               cRefs;
    /** Size of the entry. */
    size_t                          cbData;
    /** Pointer to the memory containing the data. */
    uint8_t                        *pbData;
    /** Head of list of tasks waiting for this one to finish. */
    PPDMACFILETASKSEG               pWaitingHead;
    /** Tail of list of tasks waiting for this one to finish. */
    PPDMACFILETASKSEG               pWaitingTail;
    /** Node for dirty but not yet committed entries list per endpoint. */
    RTLISTNODE                      NodeNotCommitted;
} PDMACFILECACHEENTRY, *PPDMACFILECACHEENTRY;
/** I/O is still in progress for this entry. This entry is not evictable. */
#define PDMACFILECACHE_ENTRY_IO_IN_PROGRESS RT_BIT(0)
/** Entry is locked and thus not evictable. */
#define PDMACFILECACHE_ENTRY_LOCKED         RT_BIT(1)
/** Entry is dirty */
#define PDMACFILECACHE_ENTRY_IS_DIRTY       RT_BIT(2)
/** Entry is not evictable. */
#define PDMACFILECACHE_NOT_EVICTABLE  (PDMACFILECACHE_ENTRY_LOCKED | PDMACFILECACHE_ENTRY_IO_IN_PROGRESS | PDMACFILECACHE_ENTRY_IS_DIRTY)

/**
 * LRU list data
 */
typedef struct PDMACFILELRULIST
{
    /** Head of the list. */
    PPDMACFILECACHEENTRY  pHead;
    /** Tail of the list. */
    PPDMACFILECACHEENTRY  pTail;
    /** Number of bytes cached in the list. */
    uint32_t              cbCached;
} PDMACFILELRULIST;

/**
 * Global cache data.
 */
typedef struct PDMACFILECACHEGLOBAL
{
    /** Maximum size of the cache in bytes. */
    uint32_t         cbMax;
    /** Current size of the cache in bytes. */
    uint32_t         cbCached;
    /** Critical section protecting the cache. */
    RTCRITSECT       CritSect;
    /** Maximum number of bytes cached. */
    uint32_t         cbRecentlyUsedInMax;
    /** Maximum number of bytes in the paged out list .*/
    uint32_t         cbRecentlyUsedOutMax;
    /** Recently used cache entries list */
    PDMACFILELRULIST LruRecentlyUsedIn;
    /** Scorecard cache entry list. */
    PDMACFILELRULIST LruRecentlyUsedOut;
    /** List of frequently used cache entries */
    PDMACFILELRULIST LruFrequentlyUsed;
    /** Commit timeout in milli seconds */
    uint32_t         u32CommitTimeoutMs;
    /** Number of dirty bytes needed to start a commit of the data to the disk. */
    uint32_t         cbCommitDirtyThreshold;
    /** Current number of dirty bytes in the cache. */
    volatile uint32_t cbDirty;
    /** Flag whether a commit is currently in progress. */
    volatile bool     fCommitInProgress;
    /** Commit interval timer */
    PTMTIMERR3        pTimerCommit;
    /** Number of endpoints using the cache. */
    uint32_t          cRefs;
    /** List of all endpoints using this cache. */
    RTLISTNODE        ListEndpoints;
#ifdef VBOX_WITH_STATISTICS
    /** Alignment */
    uint32_t         u32Alignment;
    /** Hit counter. */
    STAMCOUNTER      cHits;
    /** Partial hit counter. */
    STAMCOUNTER      cPartialHits;
    /** Miss counter. */
    STAMCOUNTER      cMisses;
    /** Bytes read from cache. */
    STAMCOUNTER      StatRead;
    /** Bytes written to the cache. */
    STAMCOUNTER      StatWritten;
    /** Time spend to get an entry in the AVL tree. */
    STAMPROFILEADV   StatTreeGet;
    /** Time spend to insert an entry in the AVL tree. */
    STAMPROFILEADV   StatTreeInsert;
    /** Time spend to remove an entry in the AVL tree. */
    STAMPROFILEADV   StatTreeRemove;
    /** Number of times a buffer could be reused. */
    STAMCOUNTER      StatBuffersReused;
#endif
} PDMACFILECACHEGLOBAL;
#ifdef VBOX_WITH_STATISTICS
AssertCompileMemberAlignment(PDMACFILECACHEGLOBAL, cHits, sizeof(uint64_t));
#endif

/**
 * Per endpoint cache data.
 */
typedef struct PDMACFILEENDPOINTCACHE
{
    /** AVL tree managing cache entries. */
    PAVLRFOFFTREE                        pTree;
    /** R/W semaphore protecting cached entries for this endpoint. */
    RTSEMRW                              SemRWEntries;
    /** Pointer to the gobal cache data */
    PPDMACFILECACHEGLOBAL                pCache;
    /** Lock protecting the dirty entries list. */
    RTSPINLOCK                           LockList;
    /** List of dirty but not committed entries for this endpoint. */
    RTLISTNODE                           ListDirtyNotCommitted;
    /** Node of the cache endpoint list. */
    RTLISTNODE                           NodeCacheEndpoint;
#ifdef VBOX_WITH_STATISTICS
    /** Number of times a write was deferred because the cache entry was still in progress */
    STAMCOUNTER                          StatWriteDeferred;
#endif
} PDMACFILEENDPOINTCACHE, *PPDMACFILEENDPOINTCACHE;
#ifdef VBOX_WITH_STATISTICS
AssertCompileMemberAlignment(PDMACFILEENDPOINTCACHE, StatWriteDeferred, sizeof(uint64_t));
#endif

/**
 * Backend type for the endpoint.
 */
typedef enum PDMACFILEEPBACKEND
{
    /** Non buffered. */
    PDMACFILEEPBACKEND_NON_BUFFERED = 0,
    /** Buffered (i.e host cache enabled) */
    PDMACFILEEPBACKEND_BUFFERED,
    /** 32bit hack */
    PDMACFILEEPBACKEND_32BIT_HACK = 0x7fffffff
} PDMACFILEEPBACKEND;
/** Pointer to a backend type. */
typedef PDMACFILEEPBACKEND *PPDMACFILEEPBACKEND;

/**
 * Global data for the file endpoint class.
 */
typedef struct PDMASYNCCOMPLETIONEPCLASSFILE
{
    /** Common data. */
    PDMASYNCCOMPLETIONEPCLASS           Core;
    /** Override I/O manager type - set to SIMPLE after failure. */
    PDMACEPFILEMGRTYPE                  enmMgrTypeOverride;
    /** Default backend type for the endpoint. */
    PDMACFILEEPBACKEND                  enmEpBackendDefault;
    /** Flag whether the file data cache is enabled. */
    bool                                fCacheEnabled;
    /** Critical section protecting the list of async I/O managers. */
    RTCRITSECT                          CritSect;
    /** Pointer to the head of the async I/O managers. */
    R3PTRTYPE(PPDMACEPFILEMGR)          pAioMgrHead;
    /** Number of async I/O managers currently running. */
    unsigned                            cAioMgrs;
    /** Maximum number of segments to cache per endpoint */
    unsigned                            cTasksCacheMax;
    /** Maximum number of simultaneous outstandingrequests. */
    uint32_t                            cReqsOutstandingMax;
    /** Bitmask for checking the alignment of a buffer. */
    RTR3UINTPTR                         uBitmaskAlignment;
#ifdef VBOX_WITH_STATISTICS
    uint32_t                            u32Alignment;
#endif
    /** Global cache data. */
    PDMACFILECACHEGLOBAL                Cache;
    /** Flag whether the out of resources warning was printed already. */
    bool                                fOutOfResourcesWarningPrinted;
} PDMASYNCCOMPLETIONEPCLASSFILE;
/** Pointer to the endpoint class data. */
typedef PDMASYNCCOMPLETIONEPCLASSFILE *PPDMASYNCCOMPLETIONEPCLASSFILE;
#ifdef VBOX_WITH_STATISTICS
AssertCompileMemberAlignment(PDMASYNCCOMPLETIONEPCLASSFILE, Cache, sizeof(uint64_t));
#endif

typedef enum PDMACEPFILEBLOCKINGEVENT
{
    /** The invalid event type */
    PDMACEPFILEBLOCKINGEVENT_INVALID = 0,
    /** A task is about to be canceled */
    PDMACEPFILEBLOCKINGEVENT_CANCEL,
    /** Usual 32bit hack */
    PDMACEPFILEBLOCKINGEVENT_32BIT_HACK = 0x7fffffff
} PDMACEPFILEBLOCKINGEVENT;

/**
 * States of the endpoint.
 */
typedef enum PDMASYNCCOMPLETIONENDPOINTFILESTATE
{
    /** Invalid state. */
    PDMASYNCCOMPLETIONENDPOINTFILESTATE_INVALID = 0,
    /** Normal running state accepting new requests
     * and processing them.
     */
    PDMASYNCCOMPLETIONENDPOINTFILESTATE_ACTIVE,
    /** The endpoint is about to be closed - not accepting new tasks for endpoints but waiting for
     *  remaining ones to finish.
     */
    PDMASYNCCOMPLETIONENDPOINTFILESTATE_CLOSING,
    /** Removing from current I/O manager state - not processing new tasks for endpoints but waiting
     * for remaining ones to finish.
     */
    PDMASYNCCOMPLETIONENDPOINTFILESTATE_REMOVING,
    /** The current endpoint will be migrated to another I/O manager. */
    PDMASYNCCOMPLETIONENDPOINTFILESTATE_MIGRATING,
    /** 32bit hack */
    PDMASYNCCOMPLETIONENDPOINTFILESTATE_32BIT_HACK = 0x7fffffff
} PDMASYNCCOMPLETIONENDPOINTFILESTATE;

/**
 * Data for the file endpoint.
 */
typedef struct PDMASYNCCOMPLETIONENDPOINTFILE
{
    /** Common data. */
    PDMASYNCCOMPLETIONENDPOINT             Core;
    /** Current state of the endpoint. */
    PDMASYNCCOMPLETIONENDPOINTFILESTATE    enmState;
    /** The backend to use for this endpoint. */
    PDMACFILEEPBACKEND                     enmBackendType;
    /** async I/O manager this endpoint is assigned to. */
    R3PTRTYPE(volatile PPDMACEPFILEMGR)    pAioMgr;
    /** Flags for opening the file. */
    unsigned                               fFlags;
    /** File handle. */
    RTFILE                                 File;
    /** Size of the endpoint.
     * Updated while data is appended even if it is
     * only in the cache yet and not written to the file.
     */
    volatile uint64_t                      cbEndpoint;
    /**
     * Real size of the file. Only updated if
     * data is appended.
     */
    volatile uint64_t                      cbFile;
    /** List of new tasks. */
    R3PTRTYPE(volatile PPDMACTASKFILE)     pTasksNewHead;

    /** Head of the small cache for allocated task segments for exclusive
     * use by this endpoint. */
    R3PTRTYPE(volatile PPDMACTASKFILE)     pTasksFreeHead;
    /** Tail of the small cache for allocated task segments for exclusive
     * use by this endpoint. */
    R3PTRTYPE(volatile PPDMACTASKFILE)     pTasksFreeTail;
    /** Number of elements in the cache. */
    volatile uint32_t                      cTasksCached;

#ifdef VBOX_WITH_STATISTICS
    uint32_t                               u32Alignment;
#endif
    /** Cache of endpoint data. */
    PDMACFILEENDPOINTCACHE                 DataCache;

    /** Flag whether a flush request is currently active */
    PPDMACTASKFILE                         pFlushReq;

#ifdef VBOX_WITH_STATISTICS
    /** Time spend in a read. */
    STAMPROFILEADV                         StatRead;
    /** Time spend in a write. */
    STAMPROFILEADV                         StatWrite;
#endif

    /** Event sempahore for blocking external events.
     * The caller waits on it until the async I/O manager
     * finished processing the event. */
    RTSEMEVENT                             EventSemBlock;
    /** Flag whether caching is enabled for this file. */
    bool                                   fCaching;
    /** Flag whether the file was opened readonly. */
    bool                                   fReadonly;
    /** Flag whether the host supports the async flush API. */
    bool                                   fAsyncFlushSupported;
    /** Flag whether a blocking event is pending and needs
     * processing by the I/O manager. */
    bool                                   fBlockingEventPending;
    /** Blocking event type */
    PDMACEPFILEBLOCKINGEVENT               enmBlockingEvent;

    /** Additional data needed for the event types. */
    union
    {
        /** Cancelation event. */
        struct
        {
            /** The task to cancel. */
            PPDMACTASKFILE                 pTask;
        } Cancel;
    } BlockingEventData;
    /** Data for exclusive use by the assigned async I/O manager. */
    struct
    {
        /** Pointer to the next endpoint assigned to the manager. */
        R3PTRTYPE(PPDMASYNCCOMPLETIONENDPOINTFILE) pEndpointNext;
        /** Pointer to the previous endpoint assigned to the manager. */
        R3PTRTYPE(PPDMASYNCCOMPLETIONENDPOINTFILE) pEndpointPrev;
        /** List of pending requests (not submitted due to usage restrictions
         *  or a pending flush request) */
        R3PTRTYPE(PPDMACTASKFILE)                  pReqsPendingHead;
        /** Tail of pending requests. */
        R3PTRTYPE(PPDMACTASKFILE)                  pReqsPendingTail;
        /** Tree of currently locked ranges.
         * If a write task is enqueued the range gets locked and any other
         * task writing to that range has to wait until the task completes.
         */
        PAVLRFOFFTREE                              pTreeRangesLocked;
        /** Number of requests currently being processed for this endpoint
         * (excluded flush requests). */
        unsigned                                   cRequestsActive;
        /** Number of requests processed during the last second. */
        unsigned                                   cReqsPerSec;
        /** Current number of processed requests for the current update period. */
        unsigned                                   cReqsProcessed;
        /** Flag whether the endpoint is about to be moved to another manager. */
        bool                                       fMoving;
        /** Destination I/O manager. */
        PPDMACEPFILEMGR                            pAioMgrDst;
    } AioMgr;
} PDMASYNCCOMPLETIONENDPOINTFILE;
/** Pointer to the endpoint class data. */
typedef PDMASYNCCOMPLETIONENDPOINTFILE *PPDMASYNCCOMPLETIONENDPOINTFILE;
#ifdef VBOX_WITH_STATISTICS
AssertCompileMemberAlignment(PDMASYNCCOMPLETIONENDPOINTFILE, StatRead, sizeof(uint64_t));
AssertCompileMemberAlignment(PDMASYNCCOMPLETIONENDPOINTFILE, DataCache, sizeof(uint64_t));
#endif

/** Request completion function */
typedef DECLCALLBACK(void)   FNPDMACTASKCOMPLETED(PPDMACTASKFILE pTask, void *pvUser, int rc);
/** Pointer to a request completion function. */
typedef FNPDMACTASKCOMPLETED *PFNPDMACTASKCOMPLETED;

/**
 * Transfer type.
 */
typedef enum PDMACTASKFILETRANSFER
{
    /** Invalid. */
    PDMACTASKFILETRANSFER_INVALID = 0,
    /** Read transfer. */
    PDMACTASKFILETRANSFER_READ,
    /** Write transfer. */
    PDMACTASKFILETRANSFER_WRITE,
    /** Flush transfer. */
    PDMACTASKFILETRANSFER_FLUSH
} PDMACTASKFILETRANSFER;

/**
 * Data of a request.
 */
typedef struct PDMACTASKFILE
{
    /** Pointer to the range lock we are waiting for */
    PPDMACFILERANGELOCK                  pRangeLock;
    /** Next task in the list. (Depending on the state) */
    struct PDMACTASKFILE                *pNext;
    /** Endpoint */
    PPDMASYNCCOMPLETIONENDPOINTFILE      pEndpoint;
    /** Transfer type. */
    PDMACTASKFILETRANSFER                enmTransferType;
    /** Start offset */
    RTFOFF                               Off;
    /** Data segment. */
    RTSGSEG                              DataSeg;
    /** When non-zero the segment uses a bounce buffer because the provided buffer
     * doesn't meet host requirements. */
    size_t                               cbBounceBuffer;
    /** Pointer to the used bounce buffer if any. */
    void                                *pvBounceBuffer;
    /** Start offset in the bounce buffer to copy from. */
    uint32_t                             offBounceBuffer;
    /** Flag whether this is a prefetch request. */
    bool                                 fPrefetch;
    /** Already prepared native I/O request.
     * Used if the request is prepared already but
     * was not queued because the host has not enough
     * resources. */
    RTFILEAIOREQ                         hReq;
    /** Completion function to call on completion. */
    PFNPDMACTASKCOMPLETED                pfnCompleted;
    /** User data */
    void                                *pvUser;
} PDMACTASKFILE;

/**
 * Per task data.
 */
typedef struct PDMASYNCCOMPLETIONTASKFILE
{
    /** Common data. */
    PDMASYNCCOMPLETIONTASK Core;
    /** Number of bytes to transfer until this task completes. */
    volatile int32_t      cbTransferLeft;
    /** Flag whether the task completed. */
    volatile bool         fCompleted;
    /** Return code. */
    volatile int          rc;
} PDMASYNCCOMPLETIONTASKFILE;

int pdmacFileAioMgrFailsafe(RTTHREAD ThreadSelf, void *pvUser);
int pdmacFileAioMgrNormal(RTTHREAD ThreadSelf, void *pvUser);

int pdmacFileAioMgrNormalInit(PPDMACEPFILEMGR pAioMgr);
void pdmacFileAioMgrNormalDestroy(PPDMACEPFILEMGR pAioMgr);

int pdmacFileAioMgrCreate(PPDMASYNCCOMPLETIONEPCLASSFILE pEpClass, PPPDMACEPFILEMGR ppAioMgr, PDMACEPFILEMGRTYPE enmMgrType);

int pdmacFileAioMgrAddEndpoint(PPDMACEPFILEMGR pAioMgr, PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint);

PPDMACTASKFILE pdmacFileEpGetNewTasks(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint);
PPDMACTASKFILE pdmacFileTaskAlloc(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint);
void pdmacFileTaskFree(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint,
                       PPDMACTASKFILE pTask);

int pdmacFileEpAddTask(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint, PPDMACTASKFILE pTask);

void pdmacFileEpTaskCompleted(PPDMACTASKFILE pTask, void *pvUser, int rc);

int pdmacFileCacheInit(PPDMASYNCCOMPLETIONEPCLASSFILE pClassFile, PCFGMNODE pCfgNode);
void pdmacFileCacheDestroy(PPDMASYNCCOMPLETIONEPCLASSFILE pClassFile);
int pdmacFileEpCacheInit(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint, PPDMASYNCCOMPLETIONEPCLASSFILE pClassFile);
void pdmacFileEpCacheDestroy(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint);

int pdmacFileEpCacheRead(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint, PPDMASYNCCOMPLETIONTASKFILE pTask,
                         RTFOFF off, PCRTSGSEG paSegments, size_t cSegments,
                         size_t cbRead);
int pdmacFileEpCacheWrite(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint, PPDMASYNCCOMPLETIONTASKFILE pTask,
                          RTFOFF off, PCRTSGSEG paSegments, size_t cSegments,
                          size_t cbWrite);
int pdmacFileEpCacheFlush(PPDMASYNCCOMPLETIONENDPOINTFILE pEndpoint);

RT_C_DECLS_END

#endif

