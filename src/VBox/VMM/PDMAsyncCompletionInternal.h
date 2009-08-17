/* $Id$ */
/** @file
 * PDM - Pluggable Device Manager, Async I/O Completion internal header.
 */

/*
 * Copyright (C) 2006-2008 Sun Microsystems, Inc.
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
#ifndef __PDMAsyncCompletionInternal_h
#define __PDMAsyncCompletionInternal_h

#include "PDMInternal.h"
#include <iprt/cdefs.h>
#include <iprt/critsect.h>
#include <VBox/types.h>
#include <VBox/cfgm.h>
#include <VBox/pdmasynccompletion.h>

RT_C_DECLS_BEGIN

/**
 * Supported endpoint classes.
 */
typedef enum PDMASYNCCOMPLETIONEPCLASSTYPE
{
    /** File class. */
    PDMASYNCCOMPLETIONEPCLASSTYPE_FILE = 0,
    /** Number of supported classes. */
    PDMASYNCCOMPLETIONEPCLASSTYPE_MAX,
    /** 32bit hack. */
    PDMASYNCCOMPLETIONEPCLASSTYPE_32BIT_HACK = 0x7fffffff
} PDMASYNCCOMPLETIONEPCLASSTYPE;

/**
 * PDM Async completion endpoint operations.
 */
typedef struct PDMASYNCCOMPLETIONEPCLASSOPS
{
    /** Version identifier. */
    uint32_t                      u32Version;
    /** Name of the endpoint class. */
    const char                   *pcszName;
    /** Class type. */
    PDMASYNCCOMPLETIONEPCLASSTYPE enmClassType;
    /** Size of the global endpoint class data in bytes. */
    size_t                        cbEndpointClassGlobal;
    /** Size of an endpoint in bytes. */
    size_t                        cbEndpoint;
    /** size of a task in bytes. */
    size_t                        cbTask;

    /**
     * Initializes the global data for a endpoint class.
     *
     * @returns VBox status code.
     * @param   pClassGlobals    Pointer to the uninitialized globals data.
     * @param   pCfgNode         Node for querying configuration data.
     */
    DECLR3CALLBACKMEMBER(int, pfnInitialize, (PPDMASYNCCOMPLETIONEPCLASS pClassGlobals, PCFGMNODE pCfgNode));

    /**
     * Frees all allocated ressources which were allocated during init.
     *
     * @returns VBox status code.
     * @param   pClassGlobals    Pointer to the globals data.
     */
    DECLR3CALLBACKMEMBER(void, pfnTerminate, (PPDMASYNCCOMPLETIONEPCLASS pClassGlobals));

    /**
     * Initializes a given endpoint.
     *
     * @returns VBox status code.
     * @param   pEndpoint     Pointer to the uninitialized endpoint.
     * @param   pszUri        Pointer to the string containing the endpoint
     *                        destination (filename, IP address, ...)
     * @param   fFlags        Creation flags.
     */
    DECLR3CALLBACKMEMBER(int, pfnEpInitialize, (PPDMASYNCCOMPLETIONENDPOINT pEndpoint,
                                                const char *pszUri, uint32_t fFlags));

    /**
     * Closes a endpoint finishing all tasks.
     *
     * @returns VBox status code.
     * @param   pEndpoint     Pointer to the endpoint to be closed.
     */
    DECLR3CALLBACKMEMBER(int, pfnEpClose, (PPDMASYNCCOMPLETIONENDPOINT pEndpoint));

    /**
     * Initiates a read request from the given endpoint.
     *
     * @returns VBox status code.
     * @param   pTask         Pointer to the task object associated with the request.
     * @param   pEndpoint     Endpoint the request is for.
     * @param   off           Where to start reading from.
     * @param   paSegments    Scatter gather list to store the data in.
     * @param   cSegments     Number of segments in the list.
     * @param   cbRead        The overall number of bytes to read.
     */
    DECLR3CALLBACKMEMBER(int, pfnEpRead, (PPDMASYNCCOMPLETIONTASK pTask,
                                          PPDMASYNCCOMPLETIONENDPOINT pEndpoint, RTFOFF off,
                                          PCPDMDATASEG paSegments, size_t cSegments,
                                         size_t cbRead));

    /**
     * Initiates a write request to the given endpoint.
     *
     * @returns VBox status code.
     * @param   pTask         Pointer to the task object associated with the request.
     * @param   pEndpoint     Endpoint the request is for.
     * @param   off           Where to start writing to.
     * @param   paSegments    Scatter gather list to store the data in.
     * @param   cSegments     Number of segments in the list.
     * @param   cbRead        The overall number of bytes to write.
     */
    DECLR3CALLBACKMEMBER(int, pfnEpWrite, (PPDMASYNCCOMPLETIONTASK pTask,
                                           PPDMASYNCCOMPLETIONENDPOINT pEndpoint, RTFOFF off,
                                           PCPDMDATASEG paSegments, size_t cSegments,
                                           size_t cbWrite));

    /**
     * Initiates a flush request on the given endpoint.
     *
     * @returns VBox status code.
     * @param   pTask         Pointer to the task object associated with the request.
     * @param   pEndpoint     Endpoint the request is for.
     */
    DECLR3CALLBACKMEMBER(int, pfnEpFlush, (PPDMASYNCCOMPLETIONTASK pTask,
                                           PPDMASYNCCOMPLETIONENDPOINT pEndpoint));

    /**
     * Initiates a flush request on the given endpoint.
     *
     * @returns VBox status code.
     * @param   pEndpoint     Endpoint the request is for.
     * @param   pcbSize       Where to store the size of the endpoint.
     */
    DECLR3CALLBACKMEMBER(int, pfnEpGetSize, (PPDMASYNCCOMPLETIONENDPOINT pEndpoint,
                                             uint64_t *pcbSize));

    /** Initialization safety marker. */
    uint32_t    u32VersionEnd;
} PDMASYNCCOMPLETIONEPCLASSOPS;
/** Pointer to a async completion endpoint class operation table. */
typedef PDMASYNCCOMPLETIONEPCLASSOPS *PPDMASYNCCOMPLETIONEPCLASSOPS;
/** Const pointer to a async completion endpoint class operation table. */
typedef const PDMASYNCCOMPLETIONEPCLASSOPS *PCPDMASYNCCOMPLETIONEPCLASSOPS;

/** Version for the endpoint class operations structure. */
#define PDMAC_EPCLASS_OPS_VERSION 0x00000001

/**
 * PDM Async completion endpoint class.
 * Common data.
 */
typedef struct PDMASYNCCOMPLETIONEPCLASS
{
    /** Pointer to the shared VM structure. */
    PVM                                         pVM;
    /** @name   Things configurable through CFGM
     * @{ */
    /** Size of the per endpoint cache. */
    uint32_t                                    cEndpointCacheSize;
    /** Size of the per class cache. */
    uint32_t                                    cEpClassCacheSize;
    /** @} */
    /** Critical section protecting the endpoint list. */
    RTCRITSECT                                  CritSect;
    /** Number of endpoints in the list. */
    volatile unsigned                           cEndpoints;
    /** Head of endpoints with this class. */
    R3PTRTYPE(PPDMASYNCCOMPLETIONENDPOINT)      pEndpointsHead;
    /** Pointer to the callback table. */
    R3PTRTYPE(PCPDMASYNCCOMPLETIONEPCLASSOPS)   pEndpointOps;
    /** Bigger cache for free task items used by all endpoints
     * of this class. */
    R3PTRTYPE(volatile PPDMASYNCCOMPLETIONTASK) apTaskCache[10];
    /** Number of tasks cached */
    volatile uint32_t                           cTasksCached;
} PDMASYNCCOMPLETIONEPCLASS;
/** Pointer to the PDM async completion endpoint class data. */
typedef PDMASYNCCOMPLETIONEPCLASS *PPDMASYNCCOMPLETIONEPCLASS;

/**
 * A PDM Async completion endpoint.
 * Common data.
 */
typedef struct PDMASYNCCOMPLETIONENDPOINT
{
    /** Next endpoint in the list. */
    R3PTRTYPE(PPDMASYNCCOMPLETIONENDPOINT)      pNext;
    /** Previous endpoint in the list. */
    R3PTRTYPE(PPDMASYNCCOMPLETIONENDPOINT)      pPrev;
    /** Pointer to the class this endpoint belongs to. */
    R3PTRTYPE(PPDMASYNCCOMPLETIONEPCLASS)       pEpClass;
    /** Head of the small cache for allocated task structures for exclusive
     * use by this endpoint. */
    R3PTRTYPE(volatile PPDMASYNCCOMPLETIONTASK) pTasksFreeHead;
    /** Tail of the small cache for allocated task structures for exclusive
     * use by this endpoint. */
    R3PTRTYPE(volatile PPDMASYNCCOMPLETIONTASK) pTasksFreeTail;
    /** Number of elements in the cache. */
    volatile uint32_t                           cTasksCached;
    /** Start slot for the global task cache. */
    unsigned                                    iSlotStart;
    /** ID of the next task to ensure consistency. */
    volatile uint32_t                           uTaskIdNext;
    /** Flag whether a wraparound occurred for the ID counter. */
    bool                                        fTaskIdWraparound;
    /** Template associated with this endpoint. */
    PPDMASYNCCOMPLETIONTEMPLATE                 pTemplate;
    /** Reference count. */
    unsigned                                    cUsers;
    /** URI describing the endpoint */
    char                                       *pszUri;
} PDMASYNCCOMPLETIONENDPOINT;

/**
 * A PDM async completion task handle.
 * Common data.
 */
typedef struct PDMASYNCCOMPLETIONTASK
{
    /** Next task in the list
     * (for free and assigned tasks). */
    R3PTRTYPE(PPDMASYNCCOMPLETIONTASK)      pNext;
    /** Previous task in the list
     * (for free and assigned tasks). */
    R3PTRTYPE(PPDMASYNCCOMPLETIONTASK)      pPrev;
    /** Endpoint this task is assigned to. */
    R3PTRTYPE(PPDMASYNCCOMPLETIONENDPOINT)  pEndpoint;
    /** Opaque user data for this task. */
    void                                   *pvUser;
    /** Task id. */
    uint32_t                                uTaskId;
} PDMASYNCCOMPLETIONTASK;

/**
 * Called by the endpoint if a task has finished.
 *
 * @returns nothing
 * @param   pTask    Pointer to the finished task.
 */
void pdmR3AsyncCompletionCompleteTask(PPDMASYNCCOMPLETIONTASK pTask);

RT_C_DECLS_END

extern const PDMASYNCCOMPLETIONEPCLASSOPS g_PDMAsyncCompletionEndpointClassFile;

#endif /* __PDMAsyncCompletionInternal_h */
