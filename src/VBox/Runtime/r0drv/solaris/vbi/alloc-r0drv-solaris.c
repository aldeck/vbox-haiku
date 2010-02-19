/* $Id$ */
/** @file
 * IPRT - Memory Allocation, Ring-0 Driver, Solaris.
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "../the-solaris-kernel.h"
#include "internal/iprt.h"
#include <iprt/mem.h>

#include <iprt/assert.h>
#include <iprt/param.h>
#include <iprt/thread.h>
#include "r0drv/alloc-r0drv.h"



/**
 * OS specific allocation function.
 */
PRTMEMHDR rtMemAlloc(size_t cb, uint32_t fFlags)
{
    size_t cbAllocated = cb;
    PRTMEMHDR pHdr;
#ifdef RT_ARCH_AMD64
    if (fFlags & RTMEMHDR_FLAG_EXEC)
    {
        cbAllocated = RT_ALIGN_Z(cb + sizeof(*pHdr), PAGE_SIZE) - sizeof(*pHdr);
        pHdr = (PRTMEMHDR)vbi_text_alloc(cbAllocated + sizeof(*pHdr));
    }
    else
#endif
    if (fFlags & RTMEMHDR_FLAG_ZEROED)
        pHdr = (PRTMEMHDR)kmem_zalloc(cb + sizeof(*pHdr), KM_SLEEP);
    else
        pHdr = (PRTMEMHDR)kmem_alloc(cb + sizeof(*pHdr), KM_SLEEP);
    if (pHdr)
    {
        pHdr->u32Magic  = RTMEMHDR_MAGIC;
        pHdr->fFlags    = fFlags;
        pHdr->cb        = cbAllocated;
        pHdr->cbReq     = cb;
    }
    else
        cmn_err(CE_NOTE, "rtMemAlloc(%ld, %x) failed\n", cb + sizeof(*pHdr), fFlags);
    return pHdr;
}


/**
 * OS specific free function.
 */
void rtMemFree(PRTMEMHDR pHdr)
{
    pHdr->u32Magic += 1;
#ifdef RT_ARCH_AMD64
    if (pHdr->fFlags & RTMEMHDR_FLAG_EXEC)
        vbi_text_free(pHdr, pHdr->cb + sizeof(*pHdr));
    else
#endif
        kmem_free(pHdr, pHdr->cb + sizeof(*pHdr));
}


RTR0DECL(void *) RTMemContAlloc(PRTCCPHYS pPhys, size_t cb)
{
    AssertPtr(pPhys);
    Assert(cb > 0);
    RT_ASSERT_PREEMPTIBLE();

    /* Allocate physically contiguous page-aligned memory. */
    caddr_t virtAddr;
    uint64_t phys = (unsigned)0xffffffff;       /* insist on below 4Gig */

    virtAddr = vbi_contig_alloc(&phys, cb);
    if (virtAddr == NULL)
    {
        cmn_err("vbi_contig_alloc for %u failed\n", cb);
        return NULL;
    }

    Assert(phys < (uint64_t)1 << 32);

    *pPhys = phys;
    return virtAddr;
}


RTR0DECL(void) RTMemContFree(void *pv, size_t cb)
{
    RT_ASSERT_PREEMPTIBLE();
    if (pv)
        vbi_contig_free(pv, cb);
}

