/* $Id$ */
/** @file
 * IPRT - Handle Tables.
 */

/*
 * Copyright (C) 2008 Sun Microsystems, Inc.
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/handletable.h>
#include <iprt/mem.h>
#include <iprt/spinlock.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include "internal/magics.h"
#include "handletable.h"



RTDECL(int) RTHandleTableCreateEx(PRTHANDLETABLE phHandleTable, uint32_t fFlags, uint32_t uBase, uint32_t cMax,
                                  PFNRTHANDLETABLERETAIN pfnRetain, void *pvUser)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(phHandleTable, VERR_INVALID_POINTER);
    *phHandleTable = NIL_RTHANDLETABLE;
    AssertPtrNullReturn(pfnRetain, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTHANDLETABLE_FLAGS_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(cMax > 0, VERR_INVALID_PARAMETER);
    AssertReturn(UINT32_MAX - cMax >= uBase, VERR_INVALID_PARAMETER);

    /*
     * Adjust the cMax value so it is a multiple of the 2nd level tables.
     */
    if (cMax >= UINT32_MAX - RTHT_LEVEL2_ENTRIES)
        cMax = UINT32_MAX - RTHT_LEVEL2_ENTRIES + 1;
    cMax = ((cMax + RTHT_LEVEL2_ENTRIES - 1) / RTHT_LEVEL2_ENTRIES) * RTHT_LEVEL2_ENTRIES;

    uint32_t const cLevel1 = cMax / RTHT_LEVEL2_ENTRIES;
    Assert(cLevel1 * RTHT_LEVEL2_ENTRIES == cMax);

    /*
     * Allocate the structure, include the 1st level lookup table
     * if it's below the threshold size.
     */
    size_t cb = sizeof(RTHANDLETABLEINT);
    if (cLevel1 < RTHT_LEVEL1_DYN_ALLOC_THRESHOLD)
        cb = RT_ALIGN(cb, sizeof(void *)) + cLevel1 * sizeof(void *);
    PRTHANDLETABLEINT pThis = (PRTHANDLETABLEINT)RTMemAllocZ(cb);
    if (!pThis)
        return VERR_NO_MEMORY;

    /*
     * Initialize it.
     */
    pThis->u32Magic = RTHANDLETABLE_MAGIC;
    pThis->fFlags = fFlags;
    pThis->uBase = uBase;
    pThis->cCur = 0;
    pThis->hSpinlock = NIL_RTSPINLOCK;
    if (cLevel1 < RTHT_LEVEL1_DYN_ALLOC_THRESHOLD)
        pThis->papvLevel1 = (void **)((uint8_t *)pThis + RT_ALIGN(sizeof(*pThis), sizeof(void *)));
    else
        pThis->papvLevel1 = NULL;
    pThis->pfnRetain = pfnRetain;
    pThis->pvRetainUser = pvUser;
    pThis->cMax = cMax;
    pThis->cCurAllocated = 0;
    pThis->cLevel1 = cLevel1 < RTHT_LEVEL1_DYN_ALLOC_THRESHOLD ? cLevel1 : 0;
    pThis->iFreeHead = NIL_RTHT_INDEX;
    pThis->iFreeTail = NIL_RTHT_INDEX;
    if (fFlags & RTHANDLETABLE_FLAGS_LOCKED)
    {
        int rc = RTSpinlockCreate(&pThis->hSpinlock);
        if (RT_FAILURE(rc))
        {
            RTMemFree(pThis);
            return rc;
        }
    }

    *phHandleTable = pThis;
    return VINF_SUCCESS;
}


RTDECL(int) RTHandleTableCreate(PRTHANDLETABLE phHandleTable)
{
    return RTHandleTableCreateEx(phHandleTable, RTHANDLETABLE_FLAGS_LOCKED, 1, 65534, NULL, NULL);
}


RTDECL(int) RTHandleTableDestroy(RTHANDLETABLE hHandleTable, PFNRTHANDLETABLEDELETE pfnDelete, void *pvUser)
{
    /*
     * Validate input, quitely ignore the NIL handle.
     */
    if (hHandleTable == NIL_RTHANDLETABLE)
        return VINF_SUCCESS;
    PRTHANDLETABLEINT pThis = (PRTHANDLETABLEINT)hHandleTable;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTHANDLETABLE_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrNullReturn(pfnDelete, VERR_INVALID_POINTER);

    /*
     * Mark the thing as invalid / deleted.
     * Then kill the lock.
     */
    RTSPINLOCKTMP Tmp;
    rtHandleTableLock(pThis, &Tmp);
    ASMAtomicWriteU32(&pThis->u32Magic, ~RTHANDLETABLE_MAGIC);
    rtHandleTableUnlock(pThis, &Tmp);

    if (pThis->hSpinlock != NIL_RTSPINLOCK)
    {
        rtHandleTableLock(pThis, &Tmp);
        rtHandleTableUnlock(pThis, &Tmp);

        RTSpinlockDestroy(pThis->hSpinlock);
        pThis->hSpinlock = NIL_RTSPINLOCK;
    }

    if (pfnDelete)
    {
        /*
         * Walk all the tables looking for used handles.
         */
        uint32_t cLeft = pThis->cCurAllocated;
        if (pThis->fFlags & RTHANDLETABLE_FLAGS_CONTEXT)
        {
            for (uint32_t i1 = 0; cLeft > 0 && i1 < pThis->cLevel1; i1++)
            {
                PRTHTENTRYCTX paTable = (PRTHTENTRYCTX)pThis->papvLevel1[i1];
                if (paTable)
                    for (uint32_t i = 0; i < RTHT_LEVEL2_ENTRIES; i++)
                        if (!RTHT_IS_FREE(paTable[i].pvObj))
                        {
                            pfnDelete(hHandleTable, pThis->uBase + i + i1 * RTHT_LEVEL2_ENTRIES,
                                      paTable[i].pvObj, paTable[i].pvCtx, pvUser);
                            Assert(cLeft > 0);
                            cLeft--;
                        }
            }
        }
        else
        {
            for (uint32_t i1 = 0; cLeft > 0 && i1 < pThis->cLevel1; i1++)
            {
                PRTHTENTRY paTable = (PRTHTENTRY)pThis->papvLevel1[i1];
                if (paTable)
                    for (uint32_t i = 0; i < RTHT_LEVEL2_ENTRIES; i++)
                        if (!RTHT_IS_FREE(paTable[i].pvObj))
                        {
                            pfnDelete(hHandleTable, pThis->uBase + i + i1 * RTHT_LEVEL2_ENTRIES,
                                      paTable[i].pvObj, NULL, pvUser);
                            Assert(cLeft > 0);
                            cLeft--;
                        }
            }
        }
        Assert(!cLeft);
    }

    /*
     * Free the memory.
     */
    for (uint32_t i1 = 0; i1 < pThis->cLevel1; i1++)
        if (pThis->papvLevel1[i1])
        {
            RTMemFree(pThis->papvLevel1[i1]);
            pThis->papvLevel1[i1] = NULL;
        }

    if (pThis->cMax / RTHT_LEVEL2_ENTRIES >= RTHT_LEVEL1_DYN_ALLOC_THRESHOLD)
        RTMemFree(pThis->papvLevel1);

    RTMemFree(pThis);

    return VINF_SUCCESS;
}

