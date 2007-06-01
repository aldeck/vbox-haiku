/* $Id$ */
/** @file
 * innotek Portable Runtime - Memory Allocation, Ring-0 Driver.
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/string.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/param.h>
#include "r0drv/alloc-r0drv.h"


/**
 * Allocates temporary memory.
 *
 * Temporary memory blocks are used for not too large memory blocks which
 * are believed not to stick around for too long. Using this API instead
 * of RTMemAlloc() not only gives the heap manager room for optimization
 * but makes the code easier to read.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   cb      Size in bytes of the memory block to allocated.
 */
RTDECL(void *)  RTMemTmpAlloc(size_t cb)
{
    return RTMemAlloc(cb);
}


/**
 * Allocates zero'ed temporary memory.
 *
 * Same as RTMemTmpAlloc() but the memory will be zero'ed.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   cb      Size in bytes of the memory block to allocated.
 */
RTDECL(void *)  RTMemTmpAllocZ(size_t cb)
{
    return RTMemAllocZ(cb);
}


/**
 * Free temporary memory.
 *
 * @param   pv      Pointer to memory block.
 */
RTDECL(void)    RTMemTmpFree(void *pv)
{
    return RTMemFree(pv);
}


/**
 * Allocates memory.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   cb      Size in bytes of the memory block to allocated.
 */
RTDECL(void *)  RTMemAlloc(size_t cb)
{
    PRTMEMHDR pHdr = rtMemAlloc(cb, 0);
    if (pHdr)
        return pHdr + 1;
    return NULL;
}


/**
 * Allocates zero'ed memory.
 *
 * Instead of memset(pv, 0, sizeof()) use this when you want zero'ed
 * memory. This keeps the code smaller and the heap can skip the memset
 * in about 0.42% of calls :-).
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   cb      Size in bytes of the memory block to allocated.
 */
RTDECL(void *)  RTMemAllocZ(size_t cb)
{
    PRTMEMHDR pHdr = rtMemAlloc(cb, RTMEMHDR_FLAG_ZEROED);
    if (pHdr)
        return memset(pHdr + 1, 0, pHdr->cb);
    return NULL;
}


/**
 * Reallocates memory.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   pvOld   The memory block to reallocate.
 * @param   cbNew   The new block size (in bytes).
 */
RTDECL(void *) RTMemRealloc(void *pvOld, size_t cbNew)
{
    if (!cbNew)
        RTMemFree(pvOld);
    else if (!pvOld)
        return RTMemAlloc(cbNew);
    else
    {
        PRTMEMHDR pHdrOld = (PRTMEMHDR)pvOld - 1;
        if (pHdrOld->u32Magic == RTMEMHDR_MAGIC)
        {
            PRTMEMHDR pHdrNew;
            if (pHdrOld->cb >= cbNew && pHdrOld->cb - cbNew <= 128)
                return pvOld;
            pHdrNew = rtMemAlloc(cbNew, 0);
            if (pHdrNew)
            {
                size_t cbCopy = RT_MIN(pHdrOld->cb, pHdrNew->cb);
                memcpy(pHdrNew + 1, pvOld, cbCopy);
                rtMemFree(pHdrOld);
                return pHdrNew + 1;
            }
        }
        else
            AssertMsgFailed(("pHdrOld->u32Magic=%RX32 pvOld=%p cbNew=%#zx\n", pHdrOld->u32Magic, pvOld, cbNew));
    }

    return NULL;
}


/**
 * Free memory related to an virtual machine
 *
 * @param   pv      Pointer to memory block.
 */
RTDECL(void) RTMemFree(void *pv)
{
    PRTMEMHDR pHdr;
    if (!pv)
        return;
    pHdr = (PRTMEMHDR)pv - 1;
    if (pHdr->u32Magic == RTMEMHDR_MAGIC)
    {
        Assert(!(pHdr->fFlags & RTMEMHDR_FLAG_EXEC));
        rtMemFree(pHdr);
    }
    else
        AssertMsgFailed(("pHdr->u32Magic=%RX32 pv=%p\n", pHdr->u32Magic, pv));
}


/**
 * Allocates memory which may contain code.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   cb      Size in bytes of the memory block to allocate.
 */
RTDECL(void *)    RTMemExecAlloc(size_t cb)
{
    PRTMEMHDR pHdr = rtMemAlloc(cb, RTMEMHDR_FLAG_EXEC);
    if (pHdr)
        return pHdr + 1;
    return NULL;
}


/**
 * Free executable/read/write memory allocated by RTMemExecAlloc().
 *
 * @param   pv      Pointer to memory block.
 */
RTDECL(void)      RTMemExecFree(void *pv)
{
    PRTMEMHDR pHdr;
    if (!pv)
        return;
    pHdr = (PRTMEMHDR)pv - 1;
    if (pHdr->u32Magic == RTMEMHDR_MAGIC)
        rtMemFree(pHdr);
    else
        AssertMsgFailed(("pHdr->u32Magic=%RX32 pv=%p\n", pHdr->u32Magic, pv));
}

