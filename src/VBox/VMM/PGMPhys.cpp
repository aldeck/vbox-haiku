/* $Id$ */
/** @file
 * PGM - Page Manager and Monitor, Physical Memory Addressing.
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_PGM_PHYS
#include <VBox/pgm.h>
#include <VBox/iom.h>
#include <VBox/mm.h>
#include <VBox/stam.h>
#include <VBox/rem.h>
#include <VBox/pdmdev.h>
#include "PGMInternal.h"
#include <VBox/vm.h>
#include <VBox/sup.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/thread.h>
#include <iprt/string.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** The number of pages to free in one batch. */
#define PGMPHYS_FREE_PAGE_BATCH_SIZE    128


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static DECLCALLBACK(int) pgmR3PhysRomWriteHandler(PVM pVM, RTGCPHYS GCPhys, void *pvPhys, void *pvBuf, size_t cbBuf, PGMACCESSTYPE enmAccessType, void *pvUser);
static int pgmPhysFreePage(PVM pVM, PGMMFREEPAGESREQ pReq, uint32_t *pcPendingPages, PPGMPAGE pPage, RTGCPHYS GCPhys);


/*
 * PGMR3PhysReadU8-64
 * PGMR3PhysWriteU8-64
 */
#define PGMPHYSFN_READNAME  PGMR3PhysReadU8
#define PGMPHYSFN_WRITENAME PGMR3PhysWriteU8
#define PGMPHYS_DATASIZE    1
#define PGMPHYS_DATATYPE    uint8_t
#include "PGMPhysRWTmpl.h"

#define PGMPHYSFN_READNAME  PGMR3PhysReadU16
#define PGMPHYSFN_WRITENAME PGMR3PhysWriteU16
#define PGMPHYS_DATASIZE    2
#define PGMPHYS_DATATYPE    uint16_t
#include "PGMPhysRWTmpl.h"

#define PGMPHYSFN_READNAME  PGMR3PhysReadU32
#define PGMPHYSFN_WRITENAME PGMR3PhysWriteU32
#define PGMPHYS_DATASIZE    4
#define PGMPHYS_DATATYPE    uint32_t
#include "PGMPhysRWTmpl.h"

#define PGMPHYSFN_READNAME  PGMR3PhysReadU64
#define PGMPHYSFN_WRITENAME PGMR3PhysWriteU64
#define PGMPHYS_DATASIZE    8
#define PGMPHYS_DATATYPE    uint64_t
#include "PGMPhysRWTmpl.h"


/**
 * EMT worker for PGMR3PhysReadExternal.
 */
static DECLCALLBACK(int) pgmR3PhysReadExternalEMT(PVM pVM, PRTGCPHYS pGCPhys, void *pvBuf, size_t cbRead)
{
    PGMPhysRead(pVM, *pGCPhys, pvBuf, cbRead);
    return VINF_SUCCESS;
}


/**
 * Write to physical memory, external users.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS.
 *
 * @param   pVM             VM Handle.
 * @param   GCPhys          Physical address to write to.
 * @param   pvBuf           What to write.
 * @param   cbWrite         How many bytes to write.
 *
 * @thread  Any but EMTs.
 */
VMMR3DECL(int) PGMR3PhysReadExternal(PVM pVM, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead)
{
    VM_ASSERT_OTHER_THREAD(pVM);

    AssertMsgReturn(cbRead > 0, ("don't even think about reading zero bytes!\n"), VINF_SUCCESS);
    LogFlow(("PGMR3PhysReadExternal: %RGp %d\n", GCPhys, cbRead));

    pgmLock(pVM);

    /*
     * Copy loop on ram ranges.
     */
    PPGMRAMRANGE pRam = pVM->pgm.s.CTX_SUFF(pRamRanges);
    for (;;)
    {
        /* Find range. */
        while (pRam && GCPhys > pRam->GCPhysLast)
            pRam = pRam->CTX_SUFF(pNext);
        /* Inside range or not? */
        if (pRam && GCPhys >= pRam->GCPhys)
        {
            /*
             * Must work our way thru this page by page.
             */
            RTGCPHYS off = GCPhys - pRam->GCPhys;
            while (off < pRam->cb)
            {
                unsigned iPage = off >> PAGE_SHIFT;
                PPGMPAGE pPage = &pRam->aPages[iPage];

                /*
                 * If the page has an ALL access handler, we'll have to
                 * delegate the job to EMT.
                 */
                if (PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPage))
                {
                    pgmUnlock(pVM);

                    PVMREQ pReq = NULL;
                    int rc = VMR3ReqCall(pVM, VMCPUID_ANY, &pReq, RT_INDEFINITE_WAIT,
                                         (PFNRT)pgmR3PhysReadExternalEMT, 4, pVM, &GCPhys, pvBuf, cbRead);
                    if (RT_SUCCESS(rc))
                    {
                        rc = pReq->iStatus;
                        VMR3ReqFree(pReq);
                    }
                    return rc;
                }
                Assert(!PGM_PAGE_IS_MMIO(pPage));

                /*
                 * Simple stuff, go ahead.
                 */
                size_t   cb    = PAGE_SIZE - (off & PAGE_OFFSET_MASK);
                if (cb > cbRead)
                    cb = cbRead;
                const void *pvSrc;
                int rc = pgmPhysGCPhys2CCPtrInternalReadOnly(pVM, pPage, pRam->GCPhys + off, &pvSrc);
                if (RT_SUCCESS(rc))
                    memcpy(pvBuf, pvSrc, cb);
                else
                {
                    AssertLogRelMsgFailed(("pgmPhysGCPhys2CCPtrInternalReadOnly failed on %RGp / %R[pgmpage] -> %Rrc\n",
                                           pRam->GCPhys + off, pPage, rc));
                    memset(pvBuf, 0xff, cb);
                }

                /* next page */
                if (cb >= cbRead)
                {
                    pgmUnlock(pVM);
                    return VINF_SUCCESS;
                }
                cbRead -= cb;
                off    += cb;
                GCPhys += cb;
                pvBuf   = (char *)pvBuf + cb;
            } /* walk pages in ram range. */
        }
        else
        {
            LogFlow(("PGMPhysRead: Unassigned %RGp size=%u\n", GCPhys, cbRead));

            /*
             * Unassigned address space.
             */
            if (!pRam)
                break;
            size_t cb = pRam->GCPhys - GCPhys;
            if (cb >= cbRead)
            {
                memset(pvBuf, 0xff, cbRead);
                break;
            }
            memset(pvBuf, 0xff, cb);

            cbRead -= cb;
            pvBuf   = (char *)pvBuf + cb;
            GCPhys += cb;
        }
    } /* Ram range walk */

    pgmUnlock(pVM);

    return VINF_SUCCESS;
}


/**
 * EMT worker for PGMR3PhysWriteExternal.
 */
static DECLCALLBACK(int) pgmR3PhysWriteExternalEMT(PVM pVM, PRTGCPHYS pGCPhys, const void *pvBuf, size_t cbWrite)
{
    /** @todo VERR_EM_NO_MEMORY */
    PGMPhysWrite(pVM, *pGCPhys, pvBuf, cbWrite);
    return VINF_SUCCESS;
}


/**
 * Write to physical memory, external users.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS.
 * @retval  VERR_EM_NO_MEMORY.
 *
 * @param   pVM             VM Handle.
 * @param   GCPhys          Physical address to write to.
 * @param   pvBuf           What to write.
 * @param   cbWrite         How many bytes to write.
 *
 * @thread  Any but EMTs.
 */
VMMDECL(int) PGMR3PhysWriteExternal(PVM pVM, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite)
{
    VM_ASSERT_OTHER_THREAD(pVM);

    AssertMsg(!pVM->pgm.s.fNoMorePhysWrites, ("Calling PGMR3PhysWriteExternal after pgmR3Save()!\n"));
    AssertMsgReturn(cbWrite > 0, ("don't even think about writing zero bytes!\n"), VINF_SUCCESS);
    LogFlow(("PGMR3PhysWriteExternal: %RGp %d\n", GCPhys, cbWrite));

    pgmLock(pVM);

    /*
     * Copy loop on ram ranges, stop when we hit something difficult.
     */
    PPGMRAMRANGE    pRam = pVM->pgm.s.CTX_SUFF(pRamRanges);
    for (;;)
    {
        /* Find range. */
        while (pRam && GCPhys > pRam->GCPhysLast)
            pRam = pRam->CTX_SUFF(pNext);
        /* Inside range or not? */
        if (pRam && GCPhys >= pRam->GCPhys)
        {
            /*
             * Must work our way thru this page by page.
             */
            RTGCPTR off = GCPhys - pRam->GCPhys;
            while (off < pRam->cb)
            {
                RTGCPTR     iPage = off >> PAGE_SHIFT;
                PPGMPAGE    pPage = &pRam->aPages[iPage];

                /*
                 * It the page is in any way problematic, we have to
                 * do the work on the EMT. Anything that needs to be made
                 * writable or involves access handlers is problematic.
                 */
                if (    PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage)
                    ||  PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED)
                {
                    pgmUnlock(pVM);

                    PVMREQ pReq = NULL;
                    int rc = VMR3ReqCall(pVM, VMCPUID_ANY, &pReq, RT_INDEFINITE_WAIT,
                                         (PFNRT)pgmR3PhysWriteExternalEMT, 4, pVM, &GCPhys, pvBuf, cbWrite);
                    if (RT_SUCCESS(rc))
                    {
                        rc = pReq->iStatus;
                        VMR3ReqFree(pReq);
                    }
                    return rc;
                }
                Assert(!PGM_PAGE_IS_MMIO(pPage));

                /*
                 * Simple stuff, go ahead.
                 */
                size_t      cb    = PAGE_SIZE - (off & PAGE_OFFSET_MASK);
                if (cb > cbWrite)
                    cb = cbWrite;
                void *pvDst;
                int rc = pgmPhysGCPhys2CCPtrInternal(pVM, pPage, pRam->GCPhys + off, &pvDst);
                if (RT_SUCCESS(rc))
                    memcpy(pvDst, pvBuf, cb);
                else
                    AssertLogRelMsgFailed(("pgmPhysGCPhys2CCPtrInternal failed on %RGp / %R[pgmpage] -> %Rrc\n",
                                           pRam->GCPhys + off, pPage, rc));

                /* next page */
                if (cb >= cbWrite)
                {
                    pgmUnlock(pVM);
                    return VINF_SUCCESS;
                }

                cbWrite -= cb;
                off     += cb;
                GCPhys  += cb;
                pvBuf    = (const char *)pvBuf + cb;
            } /* walk pages in ram range */
        }
        else
        {
            /*
             * Unassigned address space, skip it.
             */
            if (!pRam)
                break;
            size_t cb = pRam->GCPhys - GCPhys;
            if (cb >= cbWrite)
                break;
            cbWrite -= cb;
            pvBuf   = (const char *)pvBuf + cb;
            GCPhys += cb;
        }
    } /* Ram range walk */

    pgmUnlock(pVM);
    return VINF_SUCCESS;
}


/**
 * VMR3ReqCall worker for PGMR3PhysGCPhys2CCPtrExternal to make pages writable.
 *
 * @returns see PGMR3PhysGCPhys2CCPtrExternal
 * @param   pVM         The VM handle.
 * @param   pGCPhys     Pointer to the guest physical address.
 * @param   ppv         Where to store the mapping address.
 * @param   pLock       Where to store the lock.
 */
static DECLCALLBACK(int) pgmR3PhysGCPhys2CCPtrDelegated(PVM pVM, PRTGCPHYS pGCPhys, void **ppv, PPGMPAGEMAPLOCK pLock)
{
    /*
     * Just hand it to PGMPhysGCPhys2CCPtr and check that it's not a page with
     * an access handler after it succeeds.
     */
    int rc = pgmLock(pVM);
    AssertRCReturn(rc, rc);

    rc = PGMPhysGCPhys2CCPtr(pVM, *pGCPhys, ppv, pLock);
    if (RT_SUCCESS(rc))
    {
        PPGMPAGEMAPTLBE pTlbe;
        int rc2 = pgmPhysPageQueryTlbe(&pVM->pgm.s, *pGCPhys, &pTlbe);
        AssertFatalRC(rc2);
        PPGMPAGE pPage = pTlbe->pPage;
        if (PGM_PAGE_IS_MMIO(pPage))
        {
            PGMPhysReleasePageMappingLock(pVM, pLock);
            rc = VERR_PGM_PHYS_PAGE_RESERVED;
        }
        else
        if (    PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage)
#ifdef PGMPOOL_WITH_OPTIMIZED_DIRTY_PT
            ||  pgmPoolIsDirtyPage(pVM, *pGCPhys)
#endif
           )
        {
            /* We *must* flush any corresponding pgm pool page here, otherwise we'll
             * not be informed about writes and keep bogus gst->shw mappings around.
             */
            pgmPoolFlushPageByGCPhys(pVM, *pGCPhys);
            Assert(!PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage));
            /** @todo r=bird: return VERR_PGM_PHYS_PAGE_RESERVED here if it still has
             *        active handlers, see the PGMR3PhysGCPhys2CCPtrExternal docs. */
        }
    }

    pgmUnlock(pVM);
    return rc;
}


/**
 * Requests the mapping of a guest page into ring-3, external threads.
 *
 * When you're done with the page, call PGMPhysReleasePageMappingLock() ASAP to
 * release it.
 *
 * This API will assume your intention is to write to the page, and will
 * therefore replace shared and zero pages. If you do not intend to modify the
 * page, use the PGMR3PhysGCPhys2CCPtrReadOnlyExternal() API.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical
 *          backing or if the page has any active access handlers. The caller
 *          must fall back on using PGMR3PhysWriteExternal.
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 *
 * @param   pVM         The VM handle.
 * @param   GCPhys      The guest physical address of the page that should be mapped.
 * @param   ppv         Where to store the address corresponding to GCPhys.
 * @param   pLock       Where to store the lock information that PGMPhysReleasePageMappingLock needs.
 *
 * @remark  Avoid calling this API from within critical sections (other than the
 *          PGM one) because of the deadlock risk when we have to delegating the
 *          task to an EMT.
 * @thread  Any.
 */
VMMR3DECL(int) PGMR3PhysGCPhys2CCPtrExternal(PVM pVM, RTGCPHYS GCPhys, void **ppv, PPGMPAGEMAPLOCK pLock)
{
    AssertPtr(ppv);
    AssertPtr(pLock);

    Assert(VM_IS_EMT(pVM) || !PGMIsLockOwner(pVM));

    int rc = pgmLock(pVM);
    AssertRCReturn(rc, rc);

    /*
     * Query the Physical TLB entry for the page (may fail).
     */
    PPGMPAGEMAPTLBE pTlbe;
    rc = pgmPhysPageQueryTlbe(&pVM->pgm.s, GCPhys, &pTlbe);
    if (RT_SUCCESS(rc))
    {
        PPGMPAGE pPage = pTlbe->pPage;
        if (PGM_PAGE_IS_MMIO(pPage))
            rc = VERR_PGM_PHYS_PAGE_RESERVED;
        else
        {
            /*
             * If the page is shared, the zero page, or being write monitored
             * it must be converted to an page that's writable if possible.
             * This has to be done on an EMT.
             */
            if (    PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage)
#ifdef PGMPOOL_WITH_OPTIMIZED_DIRTY_PT
                ||  pgmPoolIsDirtyPage(pVM, GCPhys)
#endif
                ||  RT_UNLIKELY(PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED))
            {
                pgmUnlock(pVM);

                PVMREQ pReq = NULL;
                rc = VMR3ReqCall(pVM, VMCPUID_ANY, &pReq, RT_INDEFINITE_WAIT,
                                 (PFNRT)pgmR3PhysGCPhys2CCPtrDelegated, 4, pVM, &GCPhys, ppv, pLock);
                if (RT_SUCCESS(rc))
                {
                    rc = pReq->iStatus;
                    VMR3ReqFree(pReq);
                }
                return rc;
            }

            /*
             * Now, just perform the locking and calculate the return address.
             */
            PPGMPAGEMAP pMap = pTlbe->pMap;
            pMap->cRefs++;
#if 0 /** @todo implement locking properly */
            if (RT_LIKELY(pPage->cLocks != PGM_PAGE_MAX_LOCKS))
                if (RT_UNLIKELY(++pPage->cLocks == PGM_PAGE_MAX_LOCKS))
                {
                    AssertMsgFailed(("%RGp is entering permanent locked state!\n", GCPhys));
                    pMap->cRefs++; /* Extra ref to prevent it from going away. */
                }
#endif
            *ppv = (void *)((uintptr_t)pTlbe->pv | (GCPhys & PAGE_OFFSET_MASK));
            pLock->pvPage = pPage;
            pLock->pvMap = pMap;
        }
    }

    pgmUnlock(pVM);
    return rc;
}


/**
 * Requests the mapping of a guest page into ring-3, external threads.
 *
 * When you're done with the page, call PGMPhysReleasePageMappingLock() ASAP to
 * release it.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical
 *          backing or if the page as an active ALL access handler. The caller
 *          must fall back on using PGMPhysRead.
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 *
 * @param   pVM         The VM handle.
 * @param   GCPhys      The guest physical address of the page that should be mapped.
 * @param   ppv         Where to store the address corresponding to GCPhys.
 * @param   pLock       Where to store the lock information that PGMPhysReleasePageMappingLock needs.
 *
 * @remark  Avoid calling this API from within critical sections (other than
 *          the PGM one) because of the deadlock risk.
 * @thread  Any.
 */
VMMR3DECL(int) PGMR3PhysGCPhys2CCPtrReadOnlyExternal(PVM pVM, RTGCPHYS GCPhys, void const **ppv, PPGMPAGEMAPLOCK pLock)
{
    int rc = pgmLock(pVM);
    AssertRCReturn(rc, rc);

    /*
     * Query the Physical TLB entry for the page (may fail).
     */
    PPGMPAGEMAPTLBE pTlbe;
    rc = pgmPhysPageQueryTlbe(&pVM->pgm.s, GCPhys, &pTlbe);
    if (RT_SUCCESS(rc))
    {
        PPGMPAGE pPage = pTlbe->pPage;
#if 1
        /* MMIO pages doesn't have any readable backing. */
        if (PGM_PAGE_IS_MMIO(pPage))
            rc = VERR_PGM_PHYS_PAGE_RESERVED;
#else
        if (PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPage))
            rc = VERR_PGM_PHYS_PAGE_RESERVED;
#endif
        else
        {
            /*
             * Now, just perform the locking and calculate the return address.
             */
            PPGMPAGEMAP pMap = pTlbe->pMap;
            pMap->cRefs++;
#if 0 /** @todo implement locking properly */
            if (RT_LIKELY(pPage->cLocks != PGM_PAGE_MAX_LOCKS))
                if (RT_UNLIKELY(++pPage->cLocks == PGM_PAGE_MAX_LOCKS))
                {
                    AssertMsgFailed(("%RGp is entering permanent locked state!\n", GCPhys));
                    pMap->cRefs++; /* Extra ref to prevent it from going away. */
                }
#endif
            *ppv = (void *)((uintptr_t)pTlbe->pv | (GCPhys & PAGE_OFFSET_MASK));
            pLock->pvPage = pPage;
            pLock->pvMap = pMap;
        }
    }

    pgmUnlock(pVM);
    return rc;
}


/**
 * Relinks the RAM ranges using the pSelfRC and pSelfR0 pointers.
 *
 * Called when anything was relocated.
 *
 * @param   pVM         Pointer to the shared VM structure.
 */
void pgmR3PhysRelinkRamRanges(PVM pVM)
{
    PPGMRAMRANGE pCur;

#ifdef VBOX_STRICT
    for (pCur = pVM->pgm.s.pRamRangesR3; pCur; pCur = pCur->pNextR3)
    {
        Assert((pCur->fFlags & PGM_RAM_RANGE_FLAGS_FLOATING) || pCur->pSelfR0 == MMHyperCCToR0(pVM, pCur));
        Assert((pCur->fFlags & PGM_RAM_RANGE_FLAGS_FLOATING) || pCur->pSelfRC == MMHyperCCToRC(pVM, pCur));
        Assert((pCur->GCPhys     & PAGE_OFFSET_MASK) == 0);
        Assert((pCur->GCPhysLast & PAGE_OFFSET_MASK) == PAGE_OFFSET_MASK);
        Assert((pCur->cb         & PAGE_OFFSET_MASK) == 0);
        Assert(pCur->cb == pCur->GCPhysLast - pCur->GCPhys + 1);
        for (PPGMRAMRANGE pCur2 = pVM->pgm.s.pRamRangesR3; pCur2; pCur2 = pCur2->pNextR3)
            Assert(   pCur2 == pCur
                   || strcmp(pCur2->pszDesc, pCur->pszDesc)); /** @todo fix MMIO ranges!! */
    }
#endif

    pCur = pVM->pgm.s.pRamRangesR3;
    if (pCur)
    {
        pVM->pgm.s.pRamRangesR0 = pCur->pSelfR0;
        pVM->pgm.s.pRamRangesRC = pCur->pSelfRC;

        for (; pCur->pNextR3; pCur = pCur->pNextR3)
        {
            pCur->pNextR0 = pCur->pNextR3->pSelfR0;
            pCur->pNextRC = pCur->pNextR3->pSelfRC;
        }

        Assert(pCur->pNextR0 == NIL_RTR0PTR);
        Assert(pCur->pNextRC == NIL_RTRCPTR);
    }
    else
    {
        Assert(pVM->pgm.s.pRamRangesR0 == NIL_RTR0PTR);
        Assert(pVM->pgm.s.pRamRangesRC == NIL_RTRCPTR);
    }
}


/**
 * Links a new RAM range into the list.
 *
 * @param   pVM         Pointer to the shared VM structure.
 * @param   pNew        Pointer to the new list entry.
 * @param   pPrev       Pointer to the previous list entry. If NULL, insert as head.
 */
static void pgmR3PhysLinkRamRange(PVM pVM, PPGMRAMRANGE pNew, PPGMRAMRANGE pPrev)
{
    AssertMsg(pNew->pszDesc, ("%RGp-%RGp\n", pNew->GCPhys, pNew->GCPhysLast));
    Assert((pNew->fFlags & PGM_RAM_RANGE_FLAGS_FLOATING) || pNew->pSelfR0 == MMHyperCCToR0(pVM, pNew));
    Assert((pNew->fFlags & PGM_RAM_RANGE_FLAGS_FLOATING) || pNew->pSelfRC == MMHyperCCToRC(pVM, pNew));

    pgmLock(pVM);

    PPGMRAMRANGE pRam = pPrev ? pPrev->pNextR3 : pVM->pgm.s.pRamRangesR3;
    pNew->pNextR3 = pRam;
    pNew->pNextR0 = pRam ? pRam->pSelfR0 : NIL_RTR0PTR;
    pNew->pNextRC = pRam ? pRam->pSelfRC : NIL_RTRCPTR;

    if (pPrev)
    {
        pPrev->pNextR3 = pNew;
        pPrev->pNextR0 = pNew->pSelfR0;
        pPrev->pNextRC = pNew->pSelfRC;
    }
    else
    {
        pVM->pgm.s.pRamRangesR3 = pNew;
        pVM->pgm.s.pRamRangesR0 = pNew->pSelfR0;
        pVM->pgm.s.pRamRangesRC = pNew->pSelfRC;
    }

    pgmUnlock(pVM);
}


/**
 * Unlink an existing RAM range from the list.
 *
 * @param   pVM         Pointer to the shared VM structure.
 * @param   pRam        Pointer to the new list entry.
 * @param   pPrev       Pointer to the previous list entry. If NULL, insert as head.
 */
static void pgmR3PhysUnlinkRamRange2(PVM pVM, PPGMRAMRANGE pRam, PPGMRAMRANGE pPrev)
{
    Assert(pPrev ? pPrev->pNextR3 == pRam : pVM->pgm.s.pRamRangesR3 == pRam);
    Assert((pRam->fFlags & PGM_RAM_RANGE_FLAGS_FLOATING) || pRam->pSelfR0 == MMHyperCCToR0(pVM, pRam));
    Assert((pRam->fFlags & PGM_RAM_RANGE_FLAGS_FLOATING) || pRam->pSelfRC == MMHyperCCToRC(pVM, pRam));

    pgmLock(pVM);

    PPGMRAMRANGE pNext = pRam->pNextR3;
    if (pPrev)
    {
        pPrev->pNextR3 = pNext;
        pPrev->pNextR0 = pNext ? pNext->pSelfR0 : NIL_RTR0PTR;
        pPrev->pNextRC = pNext ? pNext->pSelfRC : NIL_RTRCPTR;
    }
    else
    {
        Assert(pVM->pgm.s.pRamRangesR3 == pRam);
        pVM->pgm.s.pRamRangesR3 = pNext;
        pVM->pgm.s.pRamRangesR0 = pNext ? pNext->pSelfR0 : NIL_RTR0PTR;
        pVM->pgm.s.pRamRangesRC = pNext ? pNext->pSelfRC : NIL_RTRCPTR;
    }

    pgmUnlock(pVM);
}


/**
 * Unlink an existing RAM range from the list.
 *
 * @param   pVM         Pointer to the shared VM structure.
 * @param   pRam        Pointer to the new list entry.
 */
static void pgmR3PhysUnlinkRamRange(PVM pVM, PPGMRAMRANGE pRam)
{
    pgmLock(pVM);

    /* find prev. */
    PPGMRAMRANGE pPrev = NULL;
    PPGMRAMRANGE pCur = pVM->pgm.s.pRamRangesR3;
    while (pCur != pRam)
    {
        pPrev = pCur;
        pCur = pCur->pNextR3;
    }
    AssertFatal(pCur);

    pgmR3PhysUnlinkRamRange2(pVM, pRam, pPrev);

    pgmUnlock(pVM);
}


/**
 * Frees a range of pages, replacing them with ZERO pages of the specified type.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 * @param   pRam        The RAM range in which the pages resides.
 * @param   GCPhys      The address of the first page.
 * @param   GCPhysLast  The address of the last page.
 * @param   uType       The page type to replace then with.
 */
static int pgmR3PhysFreePageRange(PVM pVM, PPGMRAMRANGE pRam, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast, uint8_t uType)
{
    uint32_t            cPendingPages = 0;
    PGMMFREEPAGESREQ    pReq;
    int rc = GMMR3FreePagesPrepare(pVM, &pReq, PGMPHYS_FREE_PAGE_BATCH_SIZE, GMMACCOUNT_BASE);
    AssertLogRelRCReturn(rc, rc);

    /* Itegerate the pages. */
    PPGMPAGE pPageDst   = &pRam->aPages[(GCPhys - pRam->GCPhys) >> PAGE_SHIFT];
    uint32_t cPagesLeft = ((GCPhysLast - GCPhys) >> PAGE_SHIFT) + 1;
    while (cPagesLeft-- > 0)
    {
        rc = pgmPhysFreePage(pVM, pReq, &cPendingPages, pPageDst, GCPhys);
        AssertLogRelRCReturn(rc, rc); /* We're done for if this goes wrong. */

        PGM_PAGE_SET_TYPE(pPageDst, uType);

        GCPhys += PAGE_SIZE;
        pPageDst++;
    }

    if (cPendingPages)
    {
        rc = GMMR3FreePagesPerform(pVM, pReq, cPendingPages);
        AssertLogRelRCReturn(rc, rc);
    }
    GMMR3FreePagesCleanup(pReq);

    return rc;
}


/**
 * PGMR3PhysRegisterRam worker that initializes and links a RAM range.
 *
 * @param   pVM             The VM handle.
 * @param   pNew            The new RAM range.
 * @param   GCPhys          The address of the RAM range.
 * @param   GCPhysLast      The last address of the RAM range.
 * @param   RCPtrNew        The RC address if the range is floating. NIL_RTRCPTR
 *                          if in HMA.
 * @param   R0PtrNew        Ditto for R0.
 * @param   pszDesc         The description.
 * @param   pPrev           The previous RAM range (for linking).
 */
static void pgmR3PhysInitAndLinkRamRange(PVM pVM, PPGMRAMRANGE pNew, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast,
                                         RTRCPTR RCPtrNew, RTR0PTR R0PtrNew, const char *pszDesc, PPGMRAMRANGE pPrev)
{
    /*
     * Initialize the range.
     */
    pNew->pSelfR0       = R0PtrNew != NIL_RTR0PTR ? R0PtrNew : MMHyperCCToR0(pVM, pNew);
    pNew->pSelfRC       = RCPtrNew != NIL_RTRCPTR ? RCPtrNew : MMHyperCCToRC(pVM, pNew);
    pNew->GCPhys        = GCPhys;
    pNew->GCPhysLast    = GCPhysLast;
    pNew->cb            = GCPhysLast - GCPhys + 1;
    pNew->pszDesc       = pszDesc;
    pNew->fFlags        = RCPtrNew != NIL_RTRCPTR ? PGM_RAM_RANGE_FLAGS_FLOATING : 0;
    pNew->pvR3          = NULL;

    uint32_t const cPages = pNew->cb >> PAGE_SHIFT;
    RTGCPHYS iPage = cPages;
    while (iPage-- > 0)
        PGM_PAGE_INIT_ZERO(&pNew->aPages[iPage], pVM, PGMPAGETYPE_RAM);

    /* Update the page count stats. */
    pVM->pgm.s.cZeroPages += cPages;
    pVM->pgm.s.cAllPages  += cPages;

    /*
     * Link it.
     */
    pgmR3PhysLinkRamRange(pVM, pNew, pPrev);
}


/**
 * Relocate a floating RAM range.
 *
 * @copydoc FNPGMRELOCATE.
 */
static DECLCALLBACK(bool) pgmR3PhysRamRangeRelocate(PVM pVM, RTGCPTR GCPtrOld, RTGCPTR GCPtrNew, PGMRELOCATECALL enmMode, void *pvUser)
{
    PPGMRAMRANGE pRam = (PPGMRAMRANGE)pvUser;
    Assert(pRam->fFlags & PGM_RAM_RANGE_FLAGS_FLOATING);
    Assert(pRam->pSelfRC == GCPtrOld + PAGE_SIZE);

    switch (enmMode)
    {
        case PGMRELOCATECALL_SUGGEST:
            return true;
        case PGMRELOCATECALL_RELOCATE:
        {
            /* Update myself and then relink all the ranges. */
            pgmLock(pVM);
            pRam->pSelfRC = (RTRCPTR)(GCPtrNew + PAGE_SIZE);
            pgmR3PhysRelinkRamRanges(pVM);
            pgmUnlock(pVM);
            return true;
        }

        default:
            AssertFailedReturn(false);
    }
}


/**
 * PGMR3PhysRegisterRam worker that registers a high chunk.
 *
 * @returns VBox status code.
 * @param   pVM             The VM handle.
 * @param   GCPhys          The address of the RAM.
 * @param   cRamPages       The number of RAM pages to register.
 * @param   cbChunk         The size of the PGMRAMRANGE guest mapping.
 * @param   iChunk          The chunk number.
 * @param   pszDesc         The RAM range description.
 * @param   ppPrev          Previous RAM range pointer. In/Out.
 */
static int pgmR3PhysRegisterHighRamChunk(PVM pVM, RTGCPHYS GCPhys, uint32_t cRamPages,
                                         uint32_t cbChunk, uint32_t iChunk, const char *pszDesc,
                                         PPGMRAMRANGE *ppPrev)
{
    const char *pszDescChunk = iChunk == 0
                             ? pszDesc
                             : MMR3HeapAPrintf(pVM, MM_TAG_PGM_PHYS, "%s (#%u)", pszDesc, iChunk + 1);
    AssertReturn(pszDescChunk, VERR_NO_MEMORY);

    /*
     * Allocate memory for the new chunk.
     */
    size_t const cChunkPages  = RT_ALIGN_Z(RT_UOFFSETOF(PGMRAMRANGE, aPages[cRamPages]), PAGE_SIZE) >> PAGE_SHIFT;
    PSUPPAGE     paChunkPages = (PSUPPAGE)RTMemTmpAllocZ(sizeof(SUPPAGE) * cChunkPages);
    AssertReturn(paChunkPages, VERR_NO_TMP_MEMORY);
    RTR0PTR      R0PtrChunk   = NIL_RTR0PTR;
    void        *pvChunk      = NULL;
    int rc = SUPR3PageAllocEx(cChunkPages, 0 /*fFlags*/, &pvChunk,
#ifdef VBOX_WITH_2X_4GB_ADDR_SPACE
                              VMMIsHwVirtExtForced(pVM) ? &R0PtrChunk : NULL,
#else
                              NULL,
#endif
                              paChunkPages);
    if (RT_SUCCESS(rc))
    {
#ifdef VBOX_WITH_2X_4GB_ADDR_SPACE
        if (!VMMIsHwVirtExtForced(pVM))
            R0PtrChunk = NIL_RTR0PTR;
#else
        R0PtrChunk = (uintptr_t)pvChunk;
#endif
        memset(pvChunk, 0, cChunkPages << PAGE_SHIFT);

        PPGMRAMRANGE pNew = (PPGMRAMRANGE)pvChunk;

        /*
         * Create a mapping and map the pages into it.
         * We push these in below the HMA.
         */
        RTGCPTR GCPtrChunkMap = pVM->pgm.s.GCPtrPrevRamRangeMapping - cbChunk;
        rc = PGMR3MapPT(pVM, GCPtrChunkMap, cbChunk, 0 /*fFlags*/, pgmR3PhysRamRangeRelocate, pNew, pszDescChunk);
        if (RT_SUCCESS(rc))
        {
            pVM->pgm.s.GCPtrPrevRamRangeMapping = GCPtrChunkMap;

            RTGCPTR const   GCPtrChunk = GCPtrChunkMap + PAGE_SIZE;
            RTGCPTR         GCPtrPage  = GCPtrChunk;
            for (uint32_t iPage = 0; iPage < cChunkPages && RT_SUCCESS(rc); iPage++, GCPtrPage += PAGE_SIZE)
                rc = PGMMap(pVM, GCPtrPage, paChunkPages[iPage].Phys, PAGE_SIZE, 0);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Ok, init and link the range.
                 */
                pgmR3PhysInitAndLinkRamRange(pVM, pNew, GCPhys, GCPhys + ((RTGCPHYS)cRamPages << PAGE_SHIFT) - 1,
                                             (RTRCPTR)GCPtrChunk, R0PtrChunk, pszDescChunk, *ppPrev);
                *ppPrev = pNew;
            }
        }

        if (RT_FAILURE(rc))
            SUPR3PageFreeEx(pvChunk, cChunkPages);
    }

    RTMemTmpFree(paChunkPages);
    return rc;
}


/**
 * Sets up a range RAM.
 *
 * This will check for conflicting registrations, make a resource
 * reservation for the memory (with GMM), and setup the per-page
 * tracking structures (PGMPAGE).
 *
 * @returns VBox stutus code.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   GCPhys          The physical address of the RAM.
 * @param   cb              The size of the RAM.
 * @param   pszDesc         The description - not copied, so, don't free or change it.
 */
VMMR3DECL(int) PGMR3PhysRegisterRam(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, const char *pszDesc)
{
   /*
     * Validate input.
     */
    Log(("PGMR3PhysRegisterRam: GCPhys=%RGp cb=%RGp pszDesc=%s\n", GCPhys, cb, pszDesc));
    AssertReturn(RT_ALIGN_T(GCPhys, PAGE_SIZE, RTGCPHYS) == GCPhys, VERR_INVALID_PARAMETER);
    AssertReturn(RT_ALIGN_T(cb, PAGE_SIZE, RTGCPHYS) == cb, VERR_INVALID_PARAMETER);
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);
    RTGCPHYS GCPhysLast = GCPhys + (cb - 1);
    AssertMsgReturn(GCPhysLast > GCPhys, ("The range wraps! GCPhys=%RGp cb=%RGp\n", GCPhys, cb), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszDesc, VERR_INVALID_POINTER);
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);

    pgmLock(pVM);

    /*
     * Find range location and check for conflicts.
     * (We don't lock here because the locking by EMT is only required on update.)
     */
    PPGMRAMRANGE    pPrev = NULL;
    PPGMRAMRANGE    pRam = pVM->pgm.s.pRamRangesR3;
    while (pRam && GCPhysLast >= pRam->GCPhys)
    {
        if (    GCPhysLast >= pRam->GCPhys
            &&  GCPhys     <= pRam->GCPhysLast)
            AssertLogRelMsgFailedReturn(("%RGp-%RGp (%s) conflicts with existing %RGp-%RGp (%s)\n",
                                         GCPhys, GCPhysLast, pszDesc,
                                         pRam->GCPhys, pRam->GCPhysLast, pRam->pszDesc),
                                        VERR_PGM_RAM_CONFLICT);

        /* next */
        pPrev = pRam;
        pRam = pRam->pNextR3;
    }

    /*
     * Register it with GMM (the API bitches).
     */
    const RTGCPHYS cPages = cb >> PAGE_SHIFT;
    int rc = MMR3IncreaseBaseReservation(pVM, cPages);
    if (RT_FAILURE(rc))
    {
        pgmUnlock(pVM);
        return rc;
    }

    if (    GCPhys >= _4G
        &&  cPages > 256)
    {
        /*
         * The PGMRAMRANGE structures for the high memory can get very big.
         * In order to avoid SUPR3PageAllocEx allocation failures due to the
         * allocation size limit there and also to avoid being unable to find
         * guest mapping space for them, we split this memory up into 4MB in
         * (potential) raw-mode configs and 16MB chunks in forced AMD-V/VT-x
         * mode.
         *
         * The first and last page of each mapping are guard pages and marked
         * not-present. So, we've got 4186112 and 16769024 bytes available for
         * the PGMRAMRANGE structure.
         *
         * Note! The sizes used here will influence the saved state.
         */
        uint32_t cbChunk;
        uint32_t cPagesPerChunk;
        if (VMMIsHwVirtExtForced(pVM))
        {
            cbChunk = 16U*_1M;
            cPagesPerChunk = 1048048; /* max ~1048059 */
            AssertCompile(sizeof(PGMRAMRANGE) + sizeof(PGMPAGE) * 1048048 < 16U*_1M - PAGE_SIZE * 2);
        }
        else
        {
            cbChunk = 4U*_1M;
            cPagesPerChunk = 261616; /* max ~261627 */
            AssertCompile(sizeof(PGMRAMRANGE) + sizeof(PGMPAGE) * 261616  <  4U*_1M - PAGE_SIZE * 2);
        }
        AssertRelease(RT_UOFFSETOF(PGMRAMRANGE, aPages[cPagesPerChunk]) + PAGE_SIZE * 2 <= cbChunk);

        RTGCPHYS cPagesLeft  = cPages;
        RTGCPHYS GCPhysChunk = GCPhys;
        uint32_t iChunk      = 0;
        while (cPagesLeft > 0)
        {
            uint32_t cPagesInChunk = cPagesLeft;
            if (cPagesInChunk > cPagesPerChunk)
                cPagesInChunk = cPagesPerChunk;

            rc = pgmR3PhysRegisterHighRamChunk(pVM, GCPhysChunk, cPagesInChunk, cbChunk, iChunk, pszDesc, &pPrev);
            AssertRCReturn(rc, rc);

            /* advance */
            GCPhysChunk += (RTGCPHYS)cPagesInChunk << PAGE_SHIFT;
            cPagesLeft  -= cPagesInChunk;
            iChunk++;
        }
    }
    else
    {
        /*
         * Allocate, initialize and link the new RAM range.
         */
        const size_t cbRamRange = RT_OFFSETOF(PGMRAMRANGE, aPages[cPages]);
        PPGMRAMRANGE pNew;
        rc = MMR3HyperAllocOnceNoRel(pVM, cbRamRange, 0, MM_TAG_PGM_PHYS, (void **)&pNew);
        AssertLogRelMsgRCReturn(rc, ("cbRamRange=%zu\n", cbRamRange), rc);

        pgmR3PhysInitAndLinkRamRange(pVM, pNew, GCPhys, GCPhysLast, NIL_RTRCPTR, NIL_RTR0PTR, pszDesc, pPrev);
    }
    pgmUnlock(pVM);

    /*
     * Notify REM.
     */
    REMR3NotifyPhysRamRegister(pVM, GCPhys, cb, REM_NOTIFY_PHYS_RAM_FLAGS_RAM);

    return VINF_SUCCESS;
}


/**
 * Worker called by PGMR3InitFinalize if we're configured to pre-allocate RAM.
 *
 * We do this late in the init process so that all the ROM and MMIO ranges have
 * been registered already and we don't go wasting memory on them.
 *
 * @returns VBox status code.
 *
 * @param   pVM     Pointer to the shared VM structure.
 */
int pgmR3PhysRamPreAllocate(PVM pVM)
{
    Assert(pVM->pgm.s.fRamPreAlloc);
    Log(("pgmR3PhysRamPreAllocate: enter\n"));

    /*
     * Walk the RAM ranges and allocate all RAM pages, halt at
     * the first allocation error.
     */
    uint64_t cPages = 0;
    uint64_t NanoTS = RTTimeNanoTS();
    pgmLock(pVM);
    for (PPGMRAMRANGE pRam = pVM->pgm.s.pRamRangesR3; pRam; pRam = pRam->pNextR3)
    {
        PPGMPAGE    pPage  = &pRam->aPages[0];
        RTGCPHYS    GCPhys = pRam->GCPhys;
        uint32_t    cLeft  = pRam->cb >> PAGE_SHIFT;
        while (cLeft-- > 0)
        {
            if (PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_RAM)
            {
                switch (PGM_PAGE_GET_STATE(pPage))
                {
                    case PGM_PAGE_STATE_ZERO:
                    {
                        int rc = pgmPhysAllocPage(pVM, pPage, GCPhys);
                        if (RT_FAILURE(rc))
                        {
                            LogRel(("PGM: RAM Pre-allocation failed at %RGp (in %s) with rc=%Rrc\n", GCPhys, pRam->pszDesc, rc));
                            pgmUnlock(pVM);
                            return rc;
                        }
                        cPages++;
                        break;
                    }

                    case PGM_PAGE_STATE_ALLOCATED:
                    case PGM_PAGE_STATE_WRITE_MONITORED:
                    case PGM_PAGE_STATE_SHARED:
                        /* nothing to do here. */
                        break;
                }
            }

            /* next */
            pPage++;
            GCPhys += PAGE_SIZE;
        }
    }
    pgmUnlock(pVM);
    NanoTS = RTTimeNanoTS() - NanoTS;

    LogRel(("PGM: Pre-allocated %llu pages in %llu ms\n", cPages, NanoTS / 1000000));
    Log(("pgmR3PhysRamPreAllocate: returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}


/**
 * Resets (zeros) the RAM.
 *
 * ASSUMES that the caller owns the PGM lock.
 *
 * @returns VBox status code.
 * @param   pVM     Pointer to the shared VM structure.
 */
int pgmR3PhysRamReset(PVM pVM)
{
    Assert(PGMIsLockOwner(pVM));
    /*
     * We batch up pages before freeing them.
     */
    uint32_t            cPendingPages = 0;
    PGMMFREEPAGESREQ    pReq;
    int rc = GMMR3FreePagesPrepare(pVM, &pReq, PGMPHYS_FREE_PAGE_BATCH_SIZE, GMMACCOUNT_BASE);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Walk the ram ranges.
     */
    for (PPGMRAMRANGE pRam = pVM->pgm.s.pRamRangesR3; pRam; pRam = pRam->pNextR3)
    {
        uint32_t iPage = pRam->cb >> PAGE_SHIFT;
        AssertMsg(((RTGCPHYS)iPage << PAGE_SHIFT) == pRam->cb, ("%RGp %RGp\n", (RTGCPHYS)iPage << PAGE_SHIFT, pRam->cb));

        if (!pVM->pgm.s.fRamPreAlloc)
        {
            /* Replace all RAM pages by ZERO pages. */
            while (iPage-- > 0)
            {
                PPGMPAGE pPage = &pRam->aPages[iPage];
                switch (PGM_PAGE_GET_TYPE(pPage))
                {
                    case PGMPAGETYPE_RAM:
                        if (!PGM_PAGE_IS_ZERO(pPage))
                        {
                            rc = pgmPhysFreePage(pVM, pReq, &cPendingPages, pPage, pRam->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT));
                            AssertLogRelRCReturn(rc, rc);
                        }
                        break;

                    case PGMPAGETYPE_MMIO2_ALIAS_MMIO:
                        pgmHandlerPhysicalResetAliasedPage(pVM, pPage, pRam->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT));
                        break;

                    case PGMPAGETYPE_MMIO2:
                    case PGMPAGETYPE_ROM_SHADOW: /* handled by pgmR3PhysRomReset. */
                    case PGMPAGETYPE_ROM:
                    case PGMPAGETYPE_MMIO:
                        break;
                    default:
                        AssertFailed();
                }
            } /* for each page */
        }
        else
        {
            /* Zero the memory. */
            while (iPage-- > 0)
            {
                PPGMPAGE pPage = &pRam->aPages[iPage];
                switch (PGM_PAGE_GET_TYPE(pPage))
                {
                    case PGMPAGETYPE_RAM:
                        switch (PGM_PAGE_GET_STATE(pPage))
                        {
                            case PGM_PAGE_STATE_ZERO:
                                break;
                            case PGM_PAGE_STATE_SHARED:
                            case PGM_PAGE_STATE_WRITE_MONITORED:
                                rc = pgmPhysPageMakeWritable(pVM, pPage, pRam->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT));
                                AssertLogRelRCReturn(rc, rc);
                            case PGM_PAGE_STATE_ALLOCATED:
                            {
                                void *pvPage;
                                PPGMPAGEMAP pMapIgnored;
                                rc = pgmPhysPageMap(pVM, pPage, pRam->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT), &pMapIgnored, &pvPage);
                                AssertLogRelRCReturn(rc, rc);
                                ASMMemZeroPage(pvPage);
                                break;
                            }
                        }
                        break;

                    case PGMPAGETYPE_MMIO2_ALIAS_MMIO:
                        pgmHandlerPhysicalResetAliasedPage(pVM, pPage, pRam->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT));
                        break;

                    case PGMPAGETYPE_MMIO2:
                    case PGMPAGETYPE_ROM_SHADOW:
                    case PGMPAGETYPE_ROM:
                    case PGMPAGETYPE_MMIO:
                        break;
                    default:
                        AssertFailed();

                }
            } /* for each page */
        }

    }

    /*
     * Finish off any pages pending freeing.
     */
    if (cPendingPages)
    {
        rc = GMMR3FreePagesPerform(pVM, pReq, cPendingPages);
        AssertLogRelRCReturn(rc, rc);
    }
    GMMR3FreePagesCleanup(pReq);

    return VINF_SUCCESS;
}


/**
 * This is the interface IOM is using to register an MMIO region.
 *
 * It will check for conflicts and ensure that a RAM range structure
 * is present before calling the PGMR3HandlerPhysicalRegister API to
 * register the callbacks.
 *
 * @returns VBox status code.
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   GCPhys          The start of the MMIO region.
 * @param   cb              The size of the MMIO region.
 * @param   pfnHandlerR3    The address of the ring-3 handler. (IOMR3MMIOHandler)
 * @param   pvUserR3        The user argument for R3.
 * @param   pfnHandlerR0    The address of the ring-0 handler. (IOMMMIOHandler)
 * @param   pvUserR0        The user argument for R0.
 * @param   pfnHandlerRC    The address of the RC handler. (IOMMMIOHandler)
 * @param   pvUserRC        The user argument for RC.
 * @param   pszDesc         The description of the MMIO region.
 */
VMMR3DECL(int) PGMR3PhysMMIORegister(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb,
                                     R3PTRTYPE(PFNPGMR3PHYSHANDLER) pfnHandlerR3, RTR3PTR pvUserR3,
                                     R0PTRTYPE(PFNPGMR0PHYSHANDLER) pfnHandlerR0, RTR0PTR pvUserR0,
                                     RCPTRTYPE(PFNPGMRCPHYSHANDLER) pfnHandlerRC, RTRCPTR pvUserRC,
                                     R3PTRTYPE(const char *) pszDesc)
{
    /*
     * Assert on some assumption.
     */
    VM_ASSERT_EMT(pVM);
    AssertReturn(!(cb & PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(!(GCPhys & PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszDesc, VERR_INVALID_POINTER);
    AssertReturn(*pszDesc, VERR_INVALID_PARAMETER);

    /*
     * Make sure there's a RAM range structure for the region.
     */
    int rc;
    RTGCPHYS GCPhysLast = GCPhys + (cb - 1);
    bool fRamExists = false;
    PPGMRAMRANGE pRamPrev = NULL;
    PPGMRAMRANGE pRam = pVM->pgm.s.pRamRangesR3;
    while (pRam && GCPhysLast >= pRam->GCPhys)
    {
        if (    GCPhysLast >= pRam->GCPhys
            &&  GCPhys     <= pRam->GCPhysLast)
        {
            /* Simplification: all within the same range. */
            AssertLogRelMsgReturn(   GCPhys     >= pRam->GCPhys
                                  && GCPhysLast <= pRam->GCPhysLast,
                                  ("%RGp-%RGp (MMIO/%s) falls partly outside %RGp-%RGp (%s)\n",
                                   GCPhys, GCPhysLast, pszDesc,
                                   pRam->GCPhys, pRam->GCPhysLast, pRam->pszDesc),
                                  VERR_PGM_RAM_CONFLICT);

            /* Check that it's all RAM or MMIO pages. */
            PCPGMPAGE pPage = &pRam->aPages[(GCPhys - pRam->GCPhys) >> PAGE_SHIFT];
            uint32_t cLeft = cb >> PAGE_SHIFT;
            while (cLeft-- > 0)
            {
                AssertLogRelMsgReturn(   PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_RAM
                                      || PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO,
                                      ("%RGp-%RGp (MMIO/%s): %RGp is not a RAM or MMIO page - type=%d desc=%s\n",
                                       GCPhys, GCPhysLast, pszDesc, PGM_PAGE_GET_TYPE(pPage), pRam->pszDesc),
                                      VERR_PGM_RAM_CONFLICT);
                pPage++;
            }

            /* Looks good. */
            fRamExists = true;
            break;
        }

        /* next */
        pRamPrev = pRam;
        pRam = pRam->pNextR3;
    }
    PPGMRAMRANGE pNew;
    if (fRamExists)
    {
        pNew = NULL;

        /*
         * Make all the pages in the range MMIO/ZERO pages, freeing any
         * RAM pages currently mapped here. This might not be 100% correct
         * for PCI memory, but we're doing the same thing for MMIO2 pages.
         */
        rc = pgmLock(pVM);
        if (RT_SUCCESS(rc))
        {
            rc = pgmR3PhysFreePageRange(pVM, pRam, GCPhys, GCPhysLast, PGMPAGETYPE_MMIO);
            pgmUnlock(pVM);
        }
        AssertRCReturn(rc, rc);
    }
    else
    {
        pgmLock(pVM);

        /*
         * No RAM range, insert an ad-hoc one.
         *
         * Note that we don't have to tell REM about this range because
         * PGMHandlerPhysicalRegisterEx will do that for us.
         */
        Log(("PGMR3PhysMMIORegister: Adding ad-hoc MMIO range for %RGp-%RGp %s\n", GCPhys, GCPhysLast, pszDesc));

        const uint32_t cPages = cb >> PAGE_SHIFT;
        const size_t cbRamRange = RT_OFFSETOF(PGMRAMRANGE, aPages[cPages]);
        rc = MMHyperAlloc(pVM, RT_OFFSETOF(PGMRAMRANGE, aPages[cPages]), 16, MM_TAG_PGM_PHYS, (void **)&pNew);
        AssertLogRelMsgRCReturn(rc, ("cbRamRange=%zu\n", cbRamRange), rc);

        /* Initialize the range. */
        pNew->pSelfR0       = MMHyperCCToR0(pVM, pNew);
        pNew->pSelfRC       = MMHyperCCToRC(pVM, pNew);
        pNew->GCPhys        = GCPhys;
        pNew->GCPhysLast    = GCPhysLast;
        pNew->cb            = cb;
        pNew->pszDesc       = pszDesc;
        pNew->fFlags        = 0; /** @todo add some kind of ad-hoc flag? */

        pNew->pvR3          = NULL;

        uint32_t iPage = cPages;
        while (iPage-- > 0)
            PGM_PAGE_INIT_ZERO_REAL(&pNew->aPages[iPage], pVM, PGMPAGETYPE_MMIO);
        Assert(PGM_PAGE_GET_TYPE(&pNew->aPages[0]) == PGMPAGETYPE_MMIO);

        /* update the page count stats. */
        pVM->pgm.s.cZeroPages += cPages;
        pVM->pgm.s.cAllPages  += cPages;

        /* link it */
        pgmR3PhysLinkRamRange(pVM, pNew, pRamPrev);

        pgmUnlock(pVM);
    }

    /*
     * Register the access handler.
     */
    rc = PGMHandlerPhysicalRegisterEx(pVM, PGMPHYSHANDLERTYPE_MMIO, GCPhys, GCPhysLast,
                                      pfnHandlerR3, pvUserR3,
                                      pfnHandlerR0, pvUserR0,
                                      pfnHandlerRC, pvUserRC, pszDesc);
    if (    RT_FAILURE(rc)
        &&  !fRamExists)
    {
        pVM->pgm.s.cZeroPages -= cb >> PAGE_SHIFT;
        pVM->pgm.s.cAllPages  -= cb >> PAGE_SHIFT;

        /* remove the ad-hoc range. */
        pgmR3PhysUnlinkRamRange2(pVM, pNew, pRamPrev);
        pNew->cb = pNew->GCPhys = pNew->GCPhysLast = NIL_RTGCPHYS;
        MMHyperFree(pVM, pRam);
    }

    return rc;
}


/**
 * This is the interface IOM is using to register an MMIO region.
 *
 * It will take care of calling PGMHandlerPhysicalDeregister and clean up
 * any ad-hoc PGMRAMRANGE left behind.
 *
 * @returns VBox status code.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   GCPhys          The start of the MMIO region.
 * @param   cb              The size of the MMIO region.
 */
VMMR3DECL(int) PGMR3PhysMMIODeregister(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb)
{
    VM_ASSERT_EMT(pVM);

    /*
     * First deregister the handler, then check if we should remove the ram range.
     */
    int rc = PGMHandlerPhysicalDeregister(pVM, GCPhys);
    if (RT_SUCCESS(rc))
    {
        RTGCPHYS        GCPhysLast  = GCPhys + (cb - 1);
        PPGMRAMRANGE    pRamPrev    = NULL;
        PPGMRAMRANGE    pRam        = pVM->pgm.s.pRamRangesR3;
        while (pRam && GCPhysLast >= pRam->GCPhys)
        {
            /** @todo We're being a bit too careful here. rewrite. */
            if (    GCPhysLast == pRam->GCPhysLast
                &&  GCPhys     == pRam->GCPhys)
            {
                Assert(pRam->cb == cb);

                /*
                 * See if all the pages are dead MMIO pages.
                 */
                uint32_t const  cPages   = cb >> PAGE_SHIFT;
                bool            fAllMMIO = true;
                uint32_t        iPage    = 0;
                uint32_t        cLeft    = cPages;
                while (cLeft-- > 0)
                {
                    PPGMPAGE    pPage    = &pRam->aPages[iPage];
                    if (    PGM_PAGE_GET_TYPE(pPage) != PGMPAGETYPE_MMIO
                        /*|| not-out-of-action later */)
                    {
                        fAllMMIO = false;
                        Assert(PGM_PAGE_GET_TYPE(pPage) != PGMPAGETYPE_MMIO2_ALIAS_MMIO);
                        AssertMsgFailed(("%RGp %R[pgmpage]\n", pRam->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT), pPage));
                        break;
                    }
                    Assert(PGM_PAGE_IS_ZERO(pPage));
                    pPage++;
                }
                if (fAllMMIO)
                {
                    /*
                     * Ad-hoc range, unlink and free it.
                     */
                    Log(("PGMR3PhysMMIODeregister: Freeing ad-hoc MMIO range for %RGp-%RGp %s\n",
                         GCPhys, GCPhysLast, pRam->pszDesc));

                    pVM->pgm.s.cAllPages  -= cPages;
                    pVM->pgm.s.cZeroPages -= cPages;

                    pgmR3PhysUnlinkRamRange2(pVM, pRam, pRamPrev);
                    pRam->cb = pRam->GCPhys = pRam->GCPhysLast = NIL_RTGCPHYS;
                    MMHyperFree(pVM, pRam);
                    break;
                }
            }

            /*
             * Range match? It will all be within one range (see PGMAllHandler.cpp).
             */
            if (    GCPhysLast >= pRam->GCPhys
                &&  GCPhys     <= pRam->GCPhysLast)
            {
                Assert(GCPhys     >= pRam->GCPhys);
                Assert(GCPhysLast <= pRam->GCPhysLast);

                /*
                 * Turn the pages back into RAM pages.
                 */
                uint32_t iPage = (GCPhys - pRam->GCPhys) >> PAGE_SHIFT;
                uint32_t cLeft = cb >> PAGE_SHIFT;
                while (cLeft--)
                {
                    PPGMPAGE pPage = &pRam->aPages[iPage];
                    AssertMsg(PGM_PAGE_IS_MMIO(pPage), ("%RGp %R[pgmpage]\n", pRam->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT), pPage));
                    AssertMsg(PGM_PAGE_IS_ZERO(pPage), ("%RGp %R[pgmpage]\n", pRam->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT), pPage));
                    if (PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO)
                        PGM_PAGE_SET_TYPE(pPage, PGMPAGETYPE_RAM);
                }
                break;
            }

            /* next */
            pRamPrev = pRam;
            pRam = pRam->pNextR3;
        }
    }

    return rc;
}


/**
 * Locate a MMIO2 range.
 *
 * @returns Pointer to the MMIO2 range.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   pDevIns         The device instance owning the region.
 * @param   iRegion         The region.
 */
DECLINLINE(PPGMMMIO2RANGE) pgmR3PhysMMIO2Find(PVM pVM, PPDMDEVINS pDevIns, uint32_t iRegion)
{
    /*
     * Search the list.
     */
    for (PPGMMMIO2RANGE pCur = pVM->pgm.s.pMmio2RangesR3; pCur; pCur = pCur->pNextR3)
        if (   pCur->pDevInsR3 == pDevIns
            && pCur->iRegion == iRegion)
            return pCur;
    return NULL;
}


/**
 * Allocate and register an MMIO2 region.
 *
 * As mentioned elsewhere, MMIO2 is just RAM spelled differently. It's
 * RAM associated with a device. It is also non-shared memory with a
 * permanent ring-3 mapping and page backing (presently).
 *
 * A MMIO2 range may overlap with base memory if a lot of RAM
 * is configured for the VM, in which case we'll drop the base
 * memory pages. Presently we will make no attempt to preserve
 * anything that happens to be present in the base memory that
 * is replaced, this is of course incorrectly but it's too much
 * effort.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success, *ppv pointing to the R3 mapping of the memory.
 * @retval  VERR_ALREADY_EXISTS if the region already exists.
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   pDevIns         The device instance owning the region.
 * @param   iRegion         The region number. If the MMIO2 memory is a PCI I/O region
 *                          this number has to be the number of that region. Otherwise
 *                          it can be any number safe UINT8_MAX.
 * @param   cb              The size of the region. Must be page aligned.
 * @param   fFlags          Reserved for future use, must be zero.
 * @param   ppv             Where to store the pointer to the ring-3 mapping of the memory.
 * @param   pszDesc         The description.
 */
VMMR3DECL(int) PGMR3PhysMMIO2Register(PVM pVM, PPDMDEVINS pDevIns, uint32_t iRegion, RTGCPHYS cb, uint32_t fFlags, void **ppv, const char *pszDesc)
{
    /*
     * Validate input.
     */
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertPtrReturn(pDevIns, VERR_INVALID_PARAMETER);
    AssertReturn(iRegion <= UINT8_MAX, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppv, VERR_INVALID_POINTER);
    AssertPtrReturn(pszDesc, VERR_INVALID_POINTER);
    AssertReturn(*pszDesc, VERR_INVALID_PARAMETER);
    AssertReturn(pgmR3PhysMMIO2Find(pVM, pDevIns, iRegion) == NULL, VERR_ALREADY_EXISTS);
    AssertReturn(!(cb & PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(cb, VERR_INVALID_PARAMETER);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);

    const uint32_t cPages = cb >> PAGE_SHIFT;
    AssertLogRelReturn(((RTGCPHYS)cPages << PAGE_SHIFT) == cb, VERR_INVALID_PARAMETER);
    AssertLogRelReturn(cPages <= INT32_MAX / 2, VERR_NO_MEMORY);

    /*
     * For the 2nd+ instance, mangle the description string so it's unique.
     */
    if (pDevIns->iInstance > 0) /** @todo Move to PDMDevHlp.cpp and use a real string cache. */
    {
        pszDesc = MMR3HeapAPrintf(pVM, MM_TAG_PGM_PHYS, "%s [%u]", pszDesc, pDevIns->iInstance);
        if (!pszDesc)
            return VERR_NO_MEMORY;
    }

    /*
     * Try reserve and allocate the backing memory first as this is what is
     * most likely to fail.
     */
    int rc = MMR3AdjustFixedReservation(pVM, cPages, pszDesc);
    if (RT_SUCCESS(rc))
    {
        void *pvPages;
        PSUPPAGE paPages = (PSUPPAGE)RTMemTmpAlloc(cPages * sizeof(SUPPAGE));
        if (RT_SUCCESS(rc))
            rc = SUPR3PageAllocEx(cPages, 0 /*fFlags*/, &pvPages, NULL /*pR0Ptr*/, paPages);
        if (RT_SUCCESS(rc))
        {
            memset(pvPages, 0, cPages * PAGE_SIZE);

            /*
             * Create the MMIO2 range record for it.
             */
            const size_t cbRange = RT_OFFSETOF(PGMMMIO2RANGE, RamRange.aPages[cPages]);
            PPGMMMIO2RANGE pNew;
            rc = MMR3HyperAllocOnceNoRel(pVM, cbRange, 0, MM_TAG_PGM_PHYS, (void **)&pNew);
            AssertLogRelMsgRC(rc, ("cbRamRange=%zu\n", cbRange));
            if (RT_SUCCESS(rc))
            {
                pNew->pDevInsR3             = pDevIns;
                pNew->pvR3                  = pvPages;
                //pNew->pNext               = NULL;
                //pNew->fMapped             = false;
                //pNew->fOverlapping        = false;
                pNew->iRegion               = iRegion;
                pNew->RamRange.pSelfR0      = MMHyperCCToR0(pVM, &pNew->RamRange);
                pNew->RamRange.pSelfRC      = MMHyperCCToRC(pVM, &pNew->RamRange);
                pNew->RamRange.GCPhys       = NIL_RTGCPHYS;
                pNew->RamRange.GCPhysLast   = NIL_RTGCPHYS;
                pNew->RamRange.pszDesc      = pszDesc;
                pNew->RamRange.cb           = cb;
                //pNew->RamRange.fFlags     = 0; /// @todo MMIO2 flag?

                pNew->RamRange.pvR3         = pvPages;

                uint32_t iPage = cPages;
                while (iPage-- > 0)
                {
                    PGM_PAGE_INIT(&pNew->RamRange.aPages[iPage],
                                  paPages[iPage].Phys & X86_PTE_PAE_PG_MASK, NIL_GMM_PAGEID,
                                  PGMPAGETYPE_MMIO2, PGM_PAGE_STATE_ALLOCATED);
                }

                /* update page count stats */
                pVM->pgm.s.cAllPages     += cPages;
                pVM->pgm.s.cPrivatePages += cPages;

                /*
                 * Link it into the list.
                 * Since there is no particular order, just push it.
                 */
                pgmLock(pVM);
                pNew->pNextR3 = pVM->pgm.s.pMmio2RangesR3;
                pVM->pgm.s.pMmio2RangesR3 = pNew;
                pgmUnlock(pVM);

                *ppv = pvPages;
                RTMemTmpFree(paPages);
                return VINF_SUCCESS;
            }

            SUPR3PageFreeEx(pvPages, cPages);
        }
        RTMemTmpFree(paPages);
        MMR3AdjustFixedReservation(pVM, -(int32_t)cPages, pszDesc);
    }
    if (pDevIns->iInstance > 0)
        MMR3HeapFree((void *)pszDesc);
    return rc;
}


/**
 * Deregisters and frees an MMIO2 region.
 *
 * Any physical (and virtual) access handlers registered for the region must
 * be deregistered before calling this function.
 *
 * @returns VBox status code.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   pDevIns         The device instance owning the region.
 * @param   iRegion         The region. If it's UINT32_MAX it'll be a wildcard match.
 */
VMMR3DECL(int) PGMR3PhysMMIO2Deregister(PVM pVM, PPDMDEVINS pDevIns, uint32_t iRegion)
{
    /*
     * Validate input.
     */
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertPtrReturn(pDevIns, VERR_INVALID_PARAMETER);
    AssertReturn(iRegion <= UINT8_MAX || iRegion == UINT32_MAX, VERR_INVALID_PARAMETER);

    pgmLock(pVM);
    int rc = VINF_SUCCESS;
    unsigned cFound = 0;
    PPGMMMIO2RANGE pPrev = NULL;
    PPGMMMIO2RANGE pCur = pVM->pgm.s.pMmio2RangesR3;
    while (pCur)
    {
        if (    pCur->pDevInsR3 == pDevIns
            &&  (   iRegion == UINT32_MAX
                 || pCur->iRegion == iRegion))
        {
            cFound++;

            /*
             * Unmap it if it's mapped.
             */
            if (pCur->fMapped)
            {
                int rc2 = PGMR3PhysMMIO2Unmap(pVM, pCur->pDevInsR3, pCur->iRegion, pCur->RamRange.GCPhys);
                AssertRC(rc2);
                if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                    rc = rc2;
            }

            /*
             * Unlink it
             */
            PPGMMMIO2RANGE pNext = pCur->pNextR3;
            if (pPrev)
                pPrev->pNextR3 = pNext;
            else
                pVM->pgm.s.pMmio2RangesR3 = pNext;
            pCur->pNextR3 = NULL;

            /*
             * Free the memory.
             */
            int rc2 = SUPR3PageFreeEx(pCur->pvR3, pCur->RamRange.cb >> PAGE_SHIFT);
            AssertRC(rc2);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;

            uint32_t const cPages = pCur->RamRange.cb >> PAGE_SHIFT;
            rc2 = MMR3AdjustFixedReservation(pVM, -(int32_t)cPages, pCur->RamRange.pszDesc);
            AssertRC(rc2);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;

            /* we're leaking hyper memory here if done at runtime. */
#ifdef VBOX_STRICT
            VMSTATE const enmState = VMR3GetState(pVM);
            AssertMsg(   enmState == VMSTATE_POWERING_OFF
                      || enmState == VMSTATE_POWERING_OFF_LS
                      || enmState == VMSTATE_OFF
                      || enmState == VMSTATE_OFF_LS
                      || enmState == VMSTATE_DESTROYING
                      || enmState == VMSTATE_TERMINATED
                      || enmState == VMSTATE_CREATING
                      , ("%s\n", VMR3GetStateName(enmState)));
#endif
            /*rc = MMHyperFree(pVM, pCur);
            AssertRCReturn(rc, rc); - not safe, see the alloc call. */


            /* update page count stats */
            pVM->pgm.s.cAllPages     -= cPages;
            pVM->pgm.s.cPrivatePages -= cPages;

            /* next */
            pCur = pNext;
        }
        else
        {
            pPrev = pCur;
            pCur = pCur->pNextR3;
        }
    }
    pgmUnlock(pVM);
    return !cFound && iRegion != UINT32_MAX ? VERR_NOT_FOUND : rc;
}


/**
 * Maps a MMIO2 region.
 *
 * This is done when a guest / the bios / state loading changes the
 * PCI config. The replacing of base memory has the same restrictions
 * as during registration, of course.
 *
 * @returns VBox status code.
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   pDevIns         The
 */
VMMR3DECL(int) PGMR3PhysMMIO2Map(PVM pVM, PPDMDEVINS pDevIns, uint32_t iRegion, RTGCPHYS GCPhys)
{
    /*
     * Validate input
     */
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertPtrReturn(pDevIns, VERR_INVALID_PARAMETER);
    AssertReturn(iRegion <= UINT8_MAX, VERR_INVALID_PARAMETER);
    AssertReturn(GCPhys != NIL_RTGCPHYS, VERR_INVALID_PARAMETER);
    AssertReturn(GCPhys != 0, VERR_INVALID_PARAMETER);
    AssertReturn(!(GCPhys & PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);

    PPGMMMIO2RANGE pCur = pgmR3PhysMMIO2Find(pVM, pDevIns, iRegion);
    AssertReturn(pCur, VERR_NOT_FOUND);
    AssertReturn(!pCur->fMapped, VERR_WRONG_ORDER);
    Assert(pCur->RamRange.GCPhys == NIL_RTGCPHYS);
    Assert(pCur->RamRange.GCPhysLast == NIL_RTGCPHYS);

    const RTGCPHYS GCPhysLast = GCPhys + pCur->RamRange.cb - 1;
    AssertReturn(GCPhysLast > GCPhys, VERR_INVALID_PARAMETER);

    /*
     * Find our location in the ram range list, checking for
     * restriction we don't bother implementing yet (partially overlapping).
     */
    bool fRamExists = false;
    PPGMRAMRANGE pRamPrev = NULL;
    PPGMRAMRANGE pRam = pVM->pgm.s.pRamRangesR3;
    while (pRam && GCPhysLast >= pRam->GCPhys)
    {
        if (    GCPhys     <= pRam->GCPhysLast
            &&  GCPhysLast >= pRam->GCPhys)
        {
            /* completely within? */
            AssertLogRelMsgReturn(   GCPhys     >= pRam->GCPhys
                                  && GCPhysLast <= pRam->GCPhysLast,
                                  ("%RGp-%RGp (MMIO2/%s) falls partly outside %RGp-%RGp (%s)\n",
                                   GCPhys, GCPhysLast, pCur->RamRange.pszDesc,
                                   pRam->GCPhys, pRam->GCPhysLast, pRam->pszDesc),
                                  VERR_PGM_RAM_CONFLICT);
            fRamExists = true;
            break;
        }

        /* next */
        pRamPrev = pRam;
        pRam = pRam->pNextR3;
    }
    if (fRamExists)
    {
        PPGMPAGE pPage = &pRam->aPages[(GCPhys - pRam->GCPhys) >> PAGE_SHIFT];
        uint32_t cPagesLeft = pCur->RamRange.cb >> PAGE_SHIFT;
        while (cPagesLeft-- > 0)
        {
            AssertLogRelMsgReturn(PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_RAM,
                                  ("%RGp isn't a RAM page (%d) - mapping %RGp-%RGp (MMIO2/%s).\n",
                                   GCPhys, PGM_PAGE_GET_TYPE(pPage), GCPhys, GCPhysLast, pCur->RamRange.pszDesc),
                                  VERR_PGM_RAM_CONFLICT);
            pPage++;
        }
    }
    Log(("PGMR3PhysMMIO2Map: %RGp-%RGp fRamExists=%RTbool %s\n",
         GCPhys, GCPhysLast, fRamExists, pCur->RamRange.pszDesc));

    /*
     * Make the changes.
     */
    pgmLock(pVM);

    pCur->RamRange.GCPhys = GCPhys;
    pCur->RamRange.GCPhysLast = GCPhysLast;
    pCur->fMapped = true;
    pCur->fOverlapping = fRamExists;

    if (fRamExists)
    {
/** @todo use pgmR3PhysFreePageRange here. */
        uint32_t            cPendingPages = 0;
        PGMMFREEPAGESREQ    pReq;
        int rc = GMMR3FreePagesPrepare(pVM, &pReq, PGMPHYS_FREE_PAGE_BATCH_SIZE, GMMACCOUNT_BASE);
        AssertLogRelRCReturn(rc, rc);

        /* replace the pages, freeing all present RAM pages. */
        PPGMPAGE pPageSrc = &pCur->RamRange.aPages[0];
        PPGMPAGE pPageDst = &pRam->aPages[(GCPhys - pRam->GCPhys) >> PAGE_SHIFT];
        uint32_t cPagesLeft = pCur->RamRange.cb >> PAGE_SHIFT;
        while (cPagesLeft-- > 0)
        {
            rc = pgmPhysFreePage(pVM, pReq, &cPendingPages, pPageDst, GCPhys);
            AssertLogRelRCReturn(rc, rc); /* We're done for if this goes wrong. */

            RTHCPHYS const HCPhys = PGM_PAGE_GET_HCPHYS(pPageSrc);
            PGM_PAGE_SET_HCPHYS(pPageDst, HCPhys);
            PGM_PAGE_SET_TYPE(pPageDst, PGMPAGETYPE_MMIO2);
            PGM_PAGE_SET_STATE(pPageDst, PGM_PAGE_STATE_ALLOCATED);

            pVM->pgm.s.cZeroPages--;
            GCPhys += PAGE_SIZE;
            pPageSrc++;
            pPageDst++;
        }

        if (cPendingPages)
        {
            rc = GMMR3FreePagesPerform(pVM, pReq, cPendingPages);
            AssertLogRelRCReturn(rc, rc);
        }
        GMMR3FreePagesCleanup(pReq);
        pgmUnlock(pVM);
    }
    else
    {
        RTGCPHYS cb = pCur->RamRange.cb;

        /* link in the ram range */
        pgmR3PhysLinkRamRange(pVM, &pCur->RamRange, pRamPrev);
        pgmUnlock(pVM);

        REMR3NotifyPhysRamRegister(pVM, GCPhys, cb, REM_NOTIFY_PHYS_RAM_FLAGS_MMIO2);
    }

    return VINF_SUCCESS;
}


/**
 * Unmaps a MMIO2 region.
 *
 * This is done when a guest / the bios / state loading changes the
 * PCI config. The replacing of base memory has the same restrictions
 * as during registration, of course.
 */
VMMR3DECL(int) PGMR3PhysMMIO2Unmap(PVM pVM, PPDMDEVINS pDevIns, uint32_t iRegion, RTGCPHYS GCPhys)
{
    bool        fInformREM = false;
    RTGCPHYS    GCPhysRangeREM;
    RTGCPHYS    cbRangeREM;

    /*
     * Validate input
     */
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertPtrReturn(pDevIns, VERR_INVALID_PARAMETER);
    AssertReturn(iRegion <= UINT8_MAX, VERR_INVALID_PARAMETER);
    AssertReturn(GCPhys != NIL_RTGCPHYS, VERR_INVALID_PARAMETER);
    AssertReturn(GCPhys != 0, VERR_INVALID_PARAMETER);
    AssertReturn(!(GCPhys & PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);

    PPGMMMIO2RANGE pCur = pgmR3PhysMMIO2Find(pVM, pDevIns, iRegion);
    AssertReturn(pCur, VERR_NOT_FOUND);
    AssertReturn(pCur->fMapped, VERR_WRONG_ORDER);
    AssertReturn(pCur->RamRange.GCPhys == GCPhys, VERR_INVALID_PARAMETER);
    Assert(pCur->RamRange.GCPhysLast != NIL_RTGCPHYS);

    Log(("PGMR3PhysMMIO2Unmap: %RGp-%RGp %s\n",
         pCur->RamRange.GCPhys, pCur->RamRange.GCPhysLast, pCur->RamRange.pszDesc));

    /*
     * Unmap it.
     */
    pgmLock(pVM);

    if (pCur->fOverlapping)
    {
        /* Restore the RAM pages we've replaced. */
        PPGMRAMRANGE pRam = pVM->pgm.s.pRamRangesR3;
        while (pRam->GCPhys > pCur->RamRange.GCPhysLast)
            pRam = pRam->pNextR3;

        RTHCPHYS const HCPhysZeroPg = pVM->pgm.s.HCPhysZeroPg;
        Assert(HCPhysZeroPg != 0 && HCPhysZeroPg != NIL_RTHCPHYS);
        PPGMPAGE pPageDst = &pRam->aPages[(pCur->RamRange.GCPhys - pRam->GCPhys) >> PAGE_SHIFT];
        uint32_t cPagesLeft = pCur->RamRange.cb >> PAGE_SHIFT;
        while (cPagesLeft-- > 0)
        {
            PGM_PAGE_SET_HCPHYS(pPageDst, HCPhysZeroPg);
            PGM_PAGE_SET_TYPE(pPageDst, PGMPAGETYPE_RAM);
            PGM_PAGE_SET_STATE(pPageDst, PGM_PAGE_STATE_ZERO);
            PGM_PAGE_SET_PAGEID(pPageDst, NIL_GMM_PAGEID);

            pVM->pgm.s.cZeroPages++;
            pPageDst++;
        }
    }
    else
    {
        GCPhysRangeREM = pCur->RamRange.GCPhys;
        cbRangeREM     = pCur->RamRange.cb;
        fInformREM     = true;

        pgmR3PhysUnlinkRamRange(pVM, &pCur->RamRange);
    }

    pCur->RamRange.GCPhys = NIL_RTGCPHYS;
    pCur->RamRange.GCPhysLast = NIL_RTGCPHYS;
    pCur->fOverlapping = false;
    pCur->fMapped = false;

    pgmUnlock(pVM);

    if (fInformREM)
        REMR3NotifyPhysRamDeregister(pVM, GCPhysRangeREM, cbRangeREM);

    return VINF_SUCCESS;
}


/**
 * Checks if the given address is an MMIO2 base address or not.
 *
 * @returns true/false accordingly.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   pDevIns         The owner of the memory, optional.
 * @param   GCPhys          The address to check.
 */
VMMR3DECL(bool) PGMR3PhysMMIO2IsBase(PVM pVM, PPDMDEVINS pDevIns, RTGCPHYS GCPhys)
{
    /*
     * Validate input
     */
    VM_ASSERT_EMT_RETURN(pVM, false);
    AssertPtrReturn(pDevIns, false);
    AssertReturn(GCPhys != NIL_RTGCPHYS, false);
    AssertReturn(GCPhys != 0, false);
    AssertReturn(!(GCPhys & PAGE_OFFSET_MASK), false);

    /*
     * Search the list.
     */
    pgmLock(pVM);
    for (PPGMMMIO2RANGE pCur = pVM->pgm.s.pMmio2RangesR3; pCur; pCur = pCur->pNextR3)
        if (pCur->RamRange.GCPhys == GCPhys)
        {
            Assert(pCur->fMapped);
            pgmUnlock(pVM);
            return true;
        }
    pgmUnlock(pVM);
    return false;
}


/**
 * Gets the HC physical address of a page in the MMIO2 region.
 *
 * This is API is intended for MMHyper and shouldn't be called
 * by anyone else...
 *
 * @returns VBox status code.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   pDevIns         The owner of the memory, optional.
 * @param   iRegion         The region.
 * @param   off             The page expressed an offset into the MMIO2 region.
 * @param   pHCPhys         Where to store the result.
 */
VMMR3DECL(int) PGMR3PhysMMIO2GetHCPhys(PVM pVM, PPDMDEVINS pDevIns, uint32_t iRegion, RTGCPHYS off, PRTHCPHYS pHCPhys)
{
    /*
     * Validate input
     */
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertPtrReturn(pDevIns, VERR_INVALID_PARAMETER);
    AssertReturn(iRegion <= UINT8_MAX, VERR_INVALID_PARAMETER);

    pgmLock(pVM);
    PPGMMMIO2RANGE pCur = pgmR3PhysMMIO2Find(pVM, pDevIns, iRegion);
    AssertReturn(pCur, VERR_NOT_FOUND);
    AssertReturn(off < pCur->RamRange.cb, VERR_INVALID_PARAMETER);

    PCPGMPAGE pPage = &pCur->RamRange.aPages[off >> PAGE_SHIFT];
    *pHCPhys = PGM_PAGE_GET_HCPHYS(pPage);
    pgmUnlock(pVM);
    return VINF_SUCCESS;
}


/**
 * Maps a portion of an MMIO2 region into kernel space (host).
 *
 * The kernel mapping will become invalid when the MMIO2 memory is deregistered
 * or the VM is terminated.
 *
 * @return VBox status code.
 *
 * @param   pVM         Pointer to the shared VM structure.
 * @param   pDevIns     The device owning the MMIO2 memory.
 * @param   iRegion     The region.
 * @param   off         The offset into the region. Must be page aligned.
 * @param   cb          The number of bytes to map. Must be page aligned.
 * @param   pszDesc     Mapping description.
 * @param   pR0Ptr      Where to store the R0 address.
 */
VMMR3DECL(int) PGMR3PhysMMIO2MapKernel(PVM pVM, PPDMDEVINS pDevIns, uint32_t iRegion, RTGCPHYS off, RTGCPHYS cb,
                                       const char *pszDesc, PRTR0PTR pR0Ptr)
{
    /*
     * Validate input.
     */
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertPtrReturn(pDevIns, VERR_INVALID_PARAMETER);
    AssertReturn(iRegion <= UINT8_MAX, VERR_INVALID_PARAMETER);

    PPGMMMIO2RANGE pCur = pgmR3PhysMMIO2Find(pVM, pDevIns, iRegion);
    AssertReturn(pCur, VERR_NOT_FOUND);
    AssertReturn(off < pCur->RamRange.cb, VERR_INVALID_PARAMETER);
    AssertReturn(cb <= pCur->RamRange.cb, VERR_INVALID_PARAMETER);
    AssertReturn(off + cb <= pCur->RamRange.cb, VERR_INVALID_PARAMETER);

    /*
     * Pass the request on to the support library/driver.
     */
    int rc = SUPR3PageMapKernel(pCur->pvR3, off, cb, 0, pR0Ptr);

    return rc;
}


/**
 * Registers a ROM image.
 *
 * Shadowed ROM images requires double the amount of backing memory, so,
 * don't use that unless you have to. Shadowing of ROM images is process
 * where we can select where the reads go and where the writes go. On real
 * hardware the chipset provides means to configure this. We provide
 * PGMR3PhysProtectROM() for this purpose.
 *
 * A read-only copy of the ROM image will always be kept around while we
 * will allocate RAM pages for the changes on demand (unless all memory
 * is configured to be preallocated).
 *
 * @returns VBox status.
 * @param   pVM                 VM Handle.
 * @param   pDevIns             The device instance owning the ROM.
 * @param   GCPhys              First physical address in the range.
 *                              Must be page aligned!
 * @param   cbRange             The size of the range (in bytes).
 *                              Must be page aligned!
 * @param   pvBinary            Pointer to the binary data backing the ROM image.
 *                              This must be exactly \a cbRange in size.
 * @param   fFlags              Mask of flags. PGMPHYS_ROM_FLAGS_SHADOWED
 *                              and/or PGMPHYS_ROM_FLAGS_PERMANENT_BINARY.
 * @param   pszDesc             Pointer to description string. This must not be freed.
 *
 * @remark  There is no way to remove the rom, automatically on device cleanup or
 *          manually from the device yet. This isn't difficult in any way, it's
 *          just not something we expect to be necessary for a while.
 */
VMMR3DECL(int) PGMR3PhysRomRegister(PVM pVM, PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTGCPHYS cb,
                                    const void *pvBinary, uint32_t fFlags, const char *pszDesc)
{
    Log(("PGMR3PhysRomRegister: pDevIns=%p GCPhys=%RGp(-%RGp) cb=%RGp pvBinary=%p fFlags=%#x pszDesc=%s\n",
         pDevIns, GCPhys, GCPhys + cb, cb, pvBinary, fFlags, pszDesc));

    /*
     * Validate input.
     */
    AssertPtrReturn(pDevIns, VERR_INVALID_PARAMETER);
    AssertReturn(RT_ALIGN_T(GCPhys, PAGE_SIZE, RTGCPHYS) == GCPhys, VERR_INVALID_PARAMETER);
    AssertReturn(RT_ALIGN_T(cb, PAGE_SIZE, RTGCPHYS) == cb, VERR_INVALID_PARAMETER);
    RTGCPHYS GCPhysLast = GCPhys + (cb - 1);
    AssertReturn(GCPhysLast > GCPhys, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pvBinary, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszDesc, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~(PGMPHYS_ROM_FLAGS_SHADOWED | PGMPHYS_ROM_FLAGS_PERMANENT_BINARY)), VERR_INVALID_PARAMETER);
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

    const uint32_t cPages = cb >> PAGE_SHIFT;

    /*
     * Find the ROM location in the ROM list first.
     */
    PPGMROMRANGE    pRomPrev = NULL;
    PPGMROMRANGE    pRom = pVM->pgm.s.pRomRangesR3;
    while (pRom && GCPhysLast >= pRom->GCPhys)
    {
        if (    GCPhys     <= pRom->GCPhysLast
            &&  GCPhysLast >= pRom->GCPhys)
            AssertLogRelMsgFailedReturn(("%RGp-%RGp (%s) conflicts with existing %RGp-%RGp (%s)\n",
                                         GCPhys, GCPhysLast, pszDesc,
                                         pRom->GCPhys, pRom->GCPhysLast, pRom->pszDesc),
                                        VERR_PGM_RAM_CONFLICT);
        /* next */
        pRomPrev = pRom;
        pRom = pRom->pNextR3;
    }

    /*
     * Find the RAM location and check for conflicts.
     *
     * Conflict detection is a bit different than for RAM
     * registration since a ROM can be located within a RAM
     * range. So, what we have to check for is other memory
     * types (other than RAM that is) and that we don't span
     * more than one RAM range (layz).
     */
    bool            fRamExists = false;
    PPGMRAMRANGE    pRamPrev = NULL;
    PPGMRAMRANGE    pRam = pVM->pgm.s.pRamRangesR3;
    while (pRam && GCPhysLast >= pRam->GCPhys)
    {
        if (    GCPhys     <= pRam->GCPhysLast
            &&  GCPhysLast >= pRam->GCPhys)
        {
            /* completely within? */
            AssertLogRelMsgReturn(   GCPhys     >= pRam->GCPhys
                                  && GCPhysLast <= pRam->GCPhysLast,
                                  ("%RGp-%RGp (%s) falls partly outside %RGp-%RGp (%s)\n",
                                   GCPhys, GCPhysLast, pszDesc,
                                   pRam->GCPhys, pRam->GCPhysLast, pRam->pszDesc),
                                  VERR_PGM_RAM_CONFLICT);
            fRamExists = true;
            break;
        }

        /* next */
        pRamPrev = pRam;
        pRam = pRam->pNextR3;
    }
    if (fRamExists)
    {
        PPGMPAGE pPage = &pRam->aPages[(GCPhys - pRam->GCPhys) >> PAGE_SHIFT];
        uint32_t cPagesLeft = cPages;
        while (cPagesLeft-- > 0)
        {
            AssertLogRelMsgReturn(PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_RAM,
                                  ("%RGp (%R[pgmpage]) isn't a RAM page - registering %RGp-%RGp (%s).\n",
                                   pRam->GCPhys + ((RTGCPHYS)(uintptr_t)(pPage - &pRam->aPages[0]) << PAGE_SHIFT),
                                   pPage, GCPhys, GCPhysLast, pszDesc), VERR_PGM_RAM_CONFLICT);
            Assert(PGM_PAGE_IS_ZERO(pPage));
            pPage++;
        }
    }

    /*
     * Update the base memory reservation if necessary.
     */
    uint32_t cExtraBaseCost = fRamExists ? cPages : 0;
    if (fFlags & PGMPHYS_ROM_FLAGS_SHADOWED)
        cExtraBaseCost += cPages;
    if (cExtraBaseCost)
    {
        int rc = MMR3IncreaseBaseReservation(pVM, cExtraBaseCost);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Allocate memory for the virgin copy of the RAM.
     */
    PGMMALLOCATEPAGESREQ pReq;
    int rc = GMMR3AllocatePagesPrepare(pVM, &pReq, cPages, GMMACCOUNT_BASE);
    AssertRCReturn(rc, rc);

    for (uint32_t iPage = 0; iPage < cPages; iPage++)
    {
        pReq->aPages[iPage].HCPhysGCPhys = GCPhys + (iPage << PAGE_SHIFT);
        pReq->aPages[iPage].idPage = NIL_GMM_PAGEID;
        pReq->aPages[iPage].idSharedPage = NIL_GMM_PAGEID;
    }

    pgmLock(pVM);
    rc = GMMR3AllocatePagesPerform(pVM, pReq);
    pgmUnlock(pVM);
    if (RT_FAILURE(rc))
    {
        GMMR3AllocatePagesCleanup(pReq);
        return rc;
    }

    /*
     * Allocate the new ROM range and RAM range (if necessary).
     */
    PPGMROMRANGE pRomNew;
    rc = MMHyperAlloc(pVM, RT_OFFSETOF(PGMROMRANGE, aPages[cPages]), 0, MM_TAG_PGM_PHYS, (void **)&pRomNew);
    if (RT_SUCCESS(rc))
    {
        PPGMRAMRANGE pRamNew = NULL;
        if (!fRamExists)
            rc = MMHyperAlloc(pVM, RT_OFFSETOF(PGMRAMRANGE, aPages[cPages]), sizeof(PGMPAGE), MM_TAG_PGM_PHYS, (void **)&pRamNew);
        if (RT_SUCCESS(rc))
        {
            pgmLock(pVM);

            /*
             * Initialize and insert the RAM range (if required).
             */
            PPGMROMPAGE pRomPage = &pRomNew->aPages[0];
            if (!fRamExists)
            {
                pRamNew->pSelfR0       = MMHyperCCToR0(pVM, pRamNew);
                pRamNew->pSelfRC       = MMHyperCCToRC(pVM, pRamNew);
                pRamNew->GCPhys        = GCPhys;
                pRamNew->GCPhysLast    = GCPhysLast;
                pRamNew->cb            = cb;
                pRamNew->pszDesc       = pszDesc;
                pRamNew->fFlags        = 0;
                pRamNew->pvR3          = NULL;

                PPGMPAGE pPage = &pRamNew->aPages[0];
                for (uint32_t iPage = 0; iPage < cPages; iPage++, pPage++, pRomPage++)
                {
                    PGM_PAGE_INIT(pPage,
                                  pReq->aPages[iPage].HCPhysGCPhys,
                                  pReq->aPages[iPage].idPage,
                                  PGMPAGETYPE_ROM,
                                  PGM_PAGE_STATE_ALLOCATED);

                    pRomPage->Virgin = *pPage;
                }

                pVM->pgm.s.cAllPages += cPages;
                pgmR3PhysLinkRamRange(pVM, pRamNew, pRamPrev);
            }
            else
            {
                PPGMPAGE pPage = &pRam->aPages[(GCPhys - pRam->GCPhys) >> PAGE_SHIFT];
                for (uint32_t iPage = 0; iPage < cPages; iPage++, pPage++, pRomPage++)
                {
                    PGM_PAGE_SET_TYPE(pPage,   PGMPAGETYPE_ROM);
                    PGM_PAGE_SET_HCPHYS(pPage, pReq->aPages[iPage].HCPhysGCPhys);
                    PGM_PAGE_SET_STATE(pPage,  PGM_PAGE_STATE_ALLOCATED);
                    PGM_PAGE_SET_PAGEID(pPage, pReq->aPages[iPage].idPage);

                    pRomPage->Virgin = *pPage;
                }

                pRamNew = pRam;

                pVM->pgm.s.cZeroPages -= cPages;
            }
            pVM->pgm.s.cPrivatePages += cPages;

            pgmUnlock(pVM);


            /*
             * !HACK ALERT!  REM + (Shadowed) ROM ==> mess.
             *
             * If it's shadowed we'll register the handler after the ROM notification
             * so we get the access handler callbacks that we should. If it isn't
             * shadowed we'll do it the other way around to make REM use the built-in
             * ROM behavior and not the handler behavior (which is to route all access
             * to PGM atm).
             */
            if (fFlags & PGMPHYS_ROM_FLAGS_SHADOWED)
            {
                REMR3NotifyPhysRomRegister(pVM, GCPhys, cb, NULL, true /* fShadowed */);
                rc = PGMR3HandlerPhysicalRegister(pVM,
                                                  fFlags & PGMPHYS_ROM_FLAGS_SHADOWED
                                                  ? PGMPHYSHANDLERTYPE_PHYSICAL_ALL
                                                  : PGMPHYSHANDLERTYPE_PHYSICAL_WRITE,
                                                  GCPhys, GCPhysLast,
                                                  pgmR3PhysRomWriteHandler, pRomNew,
                                                  NULL, "pgmPhysRomWriteHandler", MMHyperCCToR0(pVM, pRomNew),
                                                  NULL, "pgmPhysRomWriteHandler", MMHyperCCToRC(pVM, pRomNew), pszDesc);
            }
            else
            {
                rc = PGMR3HandlerPhysicalRegister(pVM,
                                                  fFlags & PGMPHYS_ROM_FLAGS_SHADOWED
                                                  ? PGMPHYSHANDLERTYPE_PHYSICAL_ALL
                                                  : PGMPHYSHANDLERTYPE_PHYSICAL_WRITE,
                                                  GCPhys, GCPhysLast,
                                                  pgmR3PhysRomWriteHandler, pRomNew,
                                                  NULL, "pgmPhysRomWriteHandler", MMHyperCCToR0(pVM, pRomNew),
                                                  NULL, "pgmPhysRomWriteHandler", MMHyperCCToRC(pVM, pRomNew), pszDesc);
                REMR3NotifyPhysRomRegister(pVM, GCPhys, cb, NULL, false /* fShadowed */);
            }
            if (RT_SUCCESS(rc))
            {
                pgmLock(pVM);

                /*
                 * Copy the image over to the virgin pages.
                 * This must be done after linking in the RAM range.
                 */
                PPGMPAGE pRamPage = &pRamNew->aPages[(GCPhys - pRamNew->GCPhys) >> PAGE_SHIFT];
                for (uint32_t iPage = 0; iPage < cPages; iPage++, pRamPage++)
                {
                    void *pvDstPage;
                    PPGMPAGEMAP pMapIgnored;
                    rc = pgmPhysPageMap(pVM, pRamPage, GCPhys + (iPage << PAGE_SHIFT), &pMapIgnored, &pvDstPage);
                    if (RT_FAILURE(rc))
                    {
                        VMSetError(pVM, rc, RT_SRC_POS, "Failed to map virgin ROM page at %RGp", GCPhys);
                        break;
                    }
                    memcpy(pvDstPage, (const uint8_t *)pvBinary + (iPage << PAGE_SHIFT), PAGE_SIZE);
                }
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Initialize the ROM range.
                     * Note that the Virgin member of the pages has already been initialized above.
                     */
                    pRomNew->GCPhys = GCPhys;
                    pRomNew->GCPhysLast = GCPhysLast;
                    pRomNew->cb = cb;
                    pRomNew->fFlags = fFlags;
                    pRomNew->pvOriginal = fFlags & PGMPHYS_ROM_FLAGS_PERMANENT_BINARY ? pvBinary : NULL;
                    pRomNew->pszDesc = pszDesc;

                    for (unsigned iPage = 0; iPage < cPages; iPage++)
                    {
                        PPGMROMPAGE pPage = &pRomNew->aPages[iPage];
                        pPage->enmProt = PGMROMPROT_READ_ROM_WRITE_IGNORE;
                        PGM_PAGE_INIT_ZERO_REAL(&pPage->Shadow, pVM, PGMPAGETYPE_ROM_SHADOW);
                    }

                    /* update the page count stats */
                    pVM->pgm.s.cZeroPages += cPages;
                    pVM->pgm.s.cAllPages  += cPages;

                    /*
                     * Insert the ROM range, tell REM and return successfully.
                     */
                    pRomNew->pNextR3 = pRom;
                    pRomNew->pNextR0 = pRom ? MMHyperCCToR0(pVM, pRom) : NIL_RTR0PTR;
                    pRomNew->pNextRC = pRom ? MMHyperCCToRC(pVM, pRom) : NIL_RTRCPTR;

                    if (pRomPrev)
                    {
                        pRomPrev->pNextR3 = pRomNew;
                        pRomPrev->pNextR0 = MMHyperCCToR0(pVM, pRomNew);
                        pRomPrev->pNextRC = MMHyperCCToRC(pVM, pRomNew);
                    }
                    else
                    {
                        pVM->pgm.s.pRomRangesR3 = pRomNew;
                        pVM->pgm.s.pRomRangesR0 = MMHyperCCToR0(pVM, pRomNew);
                        pVM->pgm.s.pRomRangesRC = MMHyperCCToRC(pVM, pRomNew);
                    }

                    GMMR3AllocatePagesCleanup(pReq);
                    pgmUnlock(pVM);
                    return VINF_SUCCESS;
                }

                /* bail out */

                pgmUnlock(pVM);
                int rc2 = PGMHandlerPhysicalDeregister(pVM, GCPhys);
                AssertRC(rc2);
                pgmLock(pVM);
            }

            if (!fRamExists)
            {
                pgmR3PhysUnlinkRamRange2(pVM, pRamNew, pRamPrev);
                MMHyperFree(pVM, pRamNew);
            }
        }
        MMHyperFree(pVM, pRomNew);
    }

    /** @todo Purge the mapping cache or something... */
    GMMR3FreeAllocatedPages(pVM, pReq);
    GMMR3AllocatePagesCleanup(pReq);
    pgmUnlock(pVM);
    return rc;
}


/**
 * \#PF Handler callback for ROM write accesses.
 *
 * @returns VINF_SUCCESS if the handler have carried out the operation.
 * @returns VINF_PGM_HANDLER_DO_DEFAULT if the caller should carry out the access operation.
 * @param   pVM             VM Handle.
 * @param   GCPhys          The physical address the guest is writing to.
 * @param   pvPhys          The HC mapping of that address.
 * @param   pvBuf           What the guest is reading/writing.
 * @param   cbBuf           How much it's reading/writing.
 * @param   enmAccessType   The access type.
 * @param   pvUser          User argument.
 */
static DECLCALLBACK(int) pgmR3PhysRomWriteHandler(PVM pVM, RTGCPHYS GCPhys, void *pvPhys, void *pvBuf, size_t cbBuf, PGMACCESSTYPE enmAccessType, void *pvUser)
{
    PPGMROMRANGE    pRom     = (PPGMROMRANGE)pvUser;
    const uint32_t  iPage    = (GCPhys - pRom->GCPhys) >> PAGE_SHIFT;
    Assert(iPage < (pRom->cb >> PAGE_SHIFT));
    PPGMROMPAGE     pRomPage = &pRom->aPages[iPage];
    Log5(("pgmR3PhysRomWriteHandler: %d %c %#08RGp %#04zx\n", pRomPage->enmProt, enmAccessType == PGMACCESSTYPE_READ ? 'R' : 'W', GCPhys, cbBuf));

    if (enmAccessType == PGMACCESSTYPE_READ)
    {
        switch (pRomPage->enmProt)
        {
            /*
             * Take the default action.
             */
            case PGMROMPROT_READ_ROM_WRITE_IGNORE:
            case PGMROMPROT_READ_RAM_WRITE_IGNORE:
            case PGMROMPROT_READ_ROM_WRITE_RAM:
            case PGMROMPROT_READ_RAM_WRITE_RAM:
                return VINF_PGM_HANDLER_DO_DEFAULT;

            default:
                AssertMsgFailedReturn(("enmProt=%d iPage=%d GCPhys=%RGp\n",
                                       pRom->aPages[iPage].enmProt, iPage, GCPhys),
                                      VERR_INTERNAL_ERROR);
        }
    }
    else
    {
        Assert(enmAccessType == PGMACCESSTYPE_WRITE);
        switch (pRomPage->enmProt)
        {
            /*
             * Ignore writes.
             */
            case PGMROMPROT_READ_ROM_WRITE_IGNORE:
            case PGMROMPROT_READ_RAM_WRITE_IGNORE:
                return VINF_SUCCESS;

            /*
             * Write to the ram page.
             */
            case PGMROMPROT_READ_ROM_WRITE_RAM:
            case PGMROMPROT_READ_RAM_WRITE_RAM: /* yes this will get here too, it's *way* simpler that way. */
            {
                /* This should be impossible now, pvPhys doesn't work cross page anylonger. */
                Assert(((GCPhys - pRom->GCPhys + cbBuf - 1) >> PAGE_SHIFT) == iPage);

                /*
                 * Take the lock, do lazy allocation, map the page and copy the data.
                 *
                 * Note that we have to bypass the mapping TLB since it works on
                 * guest physical addresses and entering the shadow page would
                 * kind of screw things up...
                 */
                int rc = pgmLock(pVM);
                AssertRC(rc);
                PPGMPAGE pShadowPage = &pRomPage->Shadow;
                if (!PGMROMPROT_IS_ROM(pRomPage->enmProt))
                {
                    pShadowPage = pgmPhysGetPage(&pVM->pgm.s, GCPhys);
                    AssertLogRelReturn(pShadowPage, VERR_INTERNAL_ERROR);
                }

                if (RT_UNLIKELY(PGM_PAGE_GET_STATE(pShadowPage) != PGM_PAGE_STATE_ALLOCATED))
                {
                    rc = pgmPhysPageMakeWritable(pVM, pShadowPage, GCPhys);
                    if (RT_FAILURE(rc))
                    {
                        pgmUnlock(pVM);
                        return rc;
                    }
                    AssertMsg(rc == VINF_SUCCESS || rc == VINF_PGM_SYNC_CR3 /* returned */, ("%Rrc\n", rc));
                }

                void       *pvDstPage;
                PPGMPAGEMAP pMapIgnored;
                int rc2 = pgmPhysPageMap(pVM, pShadowPage, GCPhys & X86_PTE_PG_MASK, &pMapIgnored, &pvDstPage);
                if (RT_SUCCESS(rc2))
                    memcpy((uint8_t *)pvDstPage + (GCPhys & PAGE_OFFSET_MASK), pvBuf, cbBuf);
                else
                    rc = rc2;

                pgmUnlock(pVM);
                return rc;
            }

            default:
                AssertMsgFailedReturn(("enmProt=%d iPage=%d GCPhys=%RGp\n",
                                       pRom->aPages[iPage].enmProt, iPage, GCPhys),
                                      VERR_INTERNAL_ERROR);
        }
    }
}


/**
 * Called by PGMR3Reset to reset the shadow, switch to the virgin,
 * and verify that the virgin part is untouched.
 *
 * This is done after the normal memory has been cleared.
 *
 * ASSUMES that the caller owns the PGM lock.
 *
 * @param   pVM         The VM handle.
 */
int pgmR3PhysRomReset(PVM pVM)
{
    Assert(PGMIsLockOwner(pVM));
    for (PPGMROMRANGE pRom = pVM->pgm.s.pRomRangesR3; pRom; pRom = pRom->pNextR3)
    {
        const uint32_t cPages = pRom->cb >> PAGE_SHIFT;

        if (pRom->fFlags & PGMPHYS_ROM_FLAGS_SHADOWED)
        {
            /*
             * Reset the physical handler.
             */
            int rc = PGMR3PhysRomProtect(pVM, pRom->GCPhys, pRom->cb, PGMROMPROT_READ_ROM_WRITE_IGNORE);
            AssertRCReturn(rc, rc);

            /*
             * What we do with the shadow pages depends on the memory
             * preallocation option. If not enabled, we'll just throw
             * out all the dirty pages and replace them by the zero page.
             */
            if (!pVM->pgm.s.fRamPreAlloc)
            {
                /* Free the dirty pages. */
                uint32_t            cPendingPages = 0;
                PGMMFREEPAGESREQ    pReq;
                rc = GMMR3FreePagesPrepare(pVM, &pReq, PGMPHYS_FREE_PAGE_BATCH_SIZE, GMMACCOUNT_BASE);
                AssertRCReturn(rc, rc);

                for (uint32_t iPage = 0; iPage < cPages; iPage++)
                    if (PGM_PAGE_GET_STATE(&pRom->aPages[iPage].Shadow) != PGM_PAGE_STATE_ZERO)
                    {
                        Assert(PGM_PAGE_GET_STATE(&pRom->aPages[iPage].Shadow) == PGM_PAGE_STATE_ALLOCATED);
                        rc = pgmPhysFreePage(pVM, pReq, &cPendingPages, &pRom->aPages[iPage].Shadow, pRom->GCPhys + (iPage << PAGE_SHIFT));
                        AssertLogRelRCReturn(rc, rc);
                    }

                if (cPendingPages)
                {
                    rc = GMMR3FreePagesPerform(pVM, pReq, cPendingPages);
                    AssertLogRelRCReturn(rc, rc);
                }
                GMMR3FreePagesCleanup(pReq);
            }
            else
            {
                /* clear all the shadow pages. */
                for (uint32_t iPage = 0; iPage < cPages; iPage++)
                {
                    Assert(PGM_PAGE_GET_STATE(&pRom->aPages[iPage].Shadow) != PGM_PAGE_STATE_ZERO);

                    const RTGCPHYS GCPhys = pRom->GCPhys + (iPage << PAGE_SHIFT);
                    rc = pgmPhysPageMakeWritable(pVM, &pRom->aPages[iPage].Shadow, GCPhys);
                    if (RT_FAILURE(rc))
                        break;

                    void *pvDstPage;
                    PPGMPAGEMAP pMapIgnored;
                    rc = pgmPhysPageMap(pVM, &pRom->aPages[iPage].Shadow, GCPhys, &pMapIgnored, &pvDstPage);
                    if (RT_FAILURE(rc))
                        break;
                    ASMMemZeroPage(pvDstPage);
                }
                AssertRCReturn(rc, rc);
            }
        }

#ifdef VBOX_STRICT
        /*
         * Verify that the virgin page is unchanged if possible.
         */
        if (pRom->pvOriginal)
        {
            uint8_t const *pbSrcPage = (uint8_t const *)pRom->pvOriginal;
            for (uint32_t iPage = 0; iPage < cPages; iPage++, pbSrcPage += PAGE_SIZE)
            {
                const RTGCPHYS GCPhys = pRom->GCPhys + (iPage << PAGE_SHIFT);
                PPGMPAGEMAP pMapIgnored;
                void *pvDstPage;
                int rc = pgmPhysPageMap(pVM, &pRom->aPages[iPage].Virgin, GCPhys, &pMapIgnored, &pvDstPage);
                if (RT_FAILURE(rc))
                    break;
                if (memcmp(pvDstPage, pbSrcPage, PAGE_SIZE))
                    LogRel(("pgmR3PhysRomReset: %RGp rom page changed (%s) - loaded saved state?\n",
                            GCPhys, pRom->pszDesc));
            }
        }
#endif
    }

    return VINF_SUCCESS;
}


/**
 * Change the shadowing of a range of ROM pages.
 *
 * This is intended for implementing chipset specific memory registers
 * and will not be very strict about the input. It will silently ignore
 * any pages that are not the part of a shadowed ROM.
 *
 * @returns VBox status code.
 * @retval  VINF_PGM_SYNC_CR3
 *
 * @param   pVM         Pointer to the shared VM structure.
 * @param   GCPhys      Where to start. Page aligned.
 * @param   cb          How much to change. Page aligned.
 * @param   enmProt     The new ROM protection.
 */
VMMR3DECL(int) PGMR3PhysRomProtect(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, PGMROMPROT enmProt)
{
    /*
     * Check input
     */
    if (!cb)
        return VINF_SUCCESS;
    AssertReturn(!(GCPhys & PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(!(cb & PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);
    RTGCPHYS GCPhysLast = GCPhys + (cb - 1);
    AssertReturn(GCPhysLast > GCPhys, VERR_INVALID_PARAMETER);
    AssertReturn(enmProt >= PGMROMPROT_INVALID && enmProt <= PGMROMPROT_END, VERR_INVALID_PARAMETER);

    /*
     * Process the request.
     */
    pgmLock(pVM);
    int  rc = VINF_SUCCESS;
    bool fFlushTLB = false;
    for (PPGMROMRANGE pRom = pVM->pgm.s.pRomRangesR3; pRom; pRom = pRom->pNextR3)
    {
        if (    GCPhys     <= pRom->GCPhysLast
            &&  GCPhysLast >= pRom->GCPhys
            &&  (pRom->fFlags & PGMPHYS_ROM_FLAGS_SHADOWED))
        {
            /*
             * Iterate the relevant pages and make necessary the changes.
             */
            bool fChanges = false;
            uint32_t const cPages = pRom->GCPhysLast <= GCPhysLast
                                  ? pRom->cb >> PAGE_SHIFT
                                  : (GCPhysLast - pRom->GCPhys + 1) >> PAGE_SHIFT;
            for (uint32_t iPage = (GCPhys - pRom->GCPhys) >> PAGE_SHIFT;
                 iPage < cPages;
                 iPage++)
            {
                PPGMROMPAGE pRomPage = &pRom->aPages[iPage];
                if (PGMROMPROT_IS_ROM(pRomPage->enmProt) != PGMROMPROT_IS_ROM(enmProt))
                {
                    fChanges = true;

                    /* flush references to the page. */
                    PPGMPAGE pRamPage = pgmPhysGetPage(&pVM->pgm.s, pRom->GCPhys + (iPage << PAGE_SHIFT));
                    int rc2 = pgmPoolTrackFlushGCPhys(pVM, pRamPage, &fFlushTLB);
                    if (rc2 != VINF_SUCCESS && (rc == VINF_SUCCESS || RT_FAILURE(rc2)))
                        rc = rc2;

                    PPGMPAGE pOld = PGMROMPROT_IS_ROM(pRomPage->enmProt) ? &pRomPage->Virgin : &pRomPage->Shadow;
                    PPGMPAGE pNew = PGMROMPROT_IS_ROM(pRomPage->enmProt) ? &pRomPage->Shadow : &pRomPage->Virgin;

                    *pOld = *pRamPage;
                    *pRamPage = *pNew;
                    /** @todo preserve the volatile flags (handlers) when these have been moved out of HCPhys! */
                }
                pRomPage->enmProt = enmProt;
            }

            /*
             * Reset the access handler if we made changes, no need
             * to optimize this.
             */
            if (fChanges)
            {
                int rc = PGMHandlerPhysicalReset(pVM, pRom->GCPhys);
                if (RT_FAILURE(rc))
                {
                    pgmUnlock(pVM);
                    AssertRC(rc);
                    return rc;
                }
            }

            /* Advance - cb isn't updated. */
            GCPhys = pRom->GCPhys + (cPages << PAGE_SHIFT);
        }
    }
    pgmUnlock(pVM);
    if (fFlushTLB)
        PGM_INVL_ALL_VCPU_TLBS(pVM);

    return rc;
}


/**
 * Sets the Address Gate 20 state.
 *
 * @param   pVCpu       The VCPU to operate on.
 * @param   fEnable     True if the gate should be enabled.
 *                      False if the gate should be disabled.
 */
VMMDECL(void) PGMR3PhysSetA20(PVMCPU pVCpu, bool fEnable)
{
    LogFlow(("PGMR3PhysSetA20 %d (was %d)\n", fEnable, pVCpu->pgm.s.fA20Enabled));
    if (pVCpu->pgm.s.fA20Enabled != fEnable)
    {
        pVCpu->pgm.s.fA20Enabled = fEnable;
        pVCpu->pgm.s.GCPhysA20Mask = ~(RTGCPHYS)(!fEnable << 20);
        REMR3A20Set(pVCpu->pVMR3, pVCpu, fEnable);
        /** @todo we're not handling this correctly for VT-x / AMD-V. See #2911 */
    }
}


/**
 * Tree enumeration callback for dealing with age rollover.
 * It will perform a simple compression of the current age.
 */
static DECLCALLBACK(int) pgmR3PhysChunkAgeingRolloverCallback(PAVLU32NODECORE pNode, void *pvUser)
{
    Assert(PGMIsLockOwner((PVM)pvUser));
    /* Age compression - ASSUMES iNow == 4. */
    PPGMCHUNKR3MAP pChunk = (PPGMCHUNKR3MAP)pNode;
    if (pChunk->iAge >= UINT32_C(0xffffff00))
        pChunk->iAge = 3;
    else if (pChunk->iAge >= UINT32_C(0xfffff000))
        pChunk->iAge = 2;
    else if (pChunk->iAge)
        pChunk->iAge = 1;
    else /* iAge = 0 */
        pChunk->iAge = 4;

    /* reinsert */
    PVM pVM = (PVM)pvUser;
    RTAvllU32Remove(&pVM->pgm.s.ChunkR3Map.pAgeTree, pChunk->AgeCore.Key);
    pChunk->AgeCore.Key = pChunk->iAge;
    RTAvllU32Insert(&pVM->pgm.s.ChunkR3Map.pAgeTree, &pChunk->AgeCore);
    return 0;
}


/**
 * Tree enumeration callback that updates the chunks that have
 * been used since the last
 */
static DECLCALLBACK(int) pgmR3PhysChunkAgeingCallback(PAVLU32NODECORE pNode, void *pvUser)
{
    PPGMCHUNKR3MAP pChunk = (PPGMCHUNKR3MAP)pNode;
    if (!pChunk->iAge)
    {
        PVM pVM = (PVM)pvUser;
        RTAvllU32Remove(&pVM->pgm.s.ChunkR3Map.pAgeTree, pChunk->AgeCore.Key);
        pChunk->AgeCore.Key = pChunk->iAge = pVM->pgm.s.ChunkR3Map.iNow;
        RTAvllU32Insert(&pVM->pgm.s.ChunkR3Map.pAgeTree, &pChunk->AgeCore);
    }

    return 0;
}


/**
 * Performs ageing of the ring-3 chunk mappings.
 *
 * @param   pVM         The VM handle.
 */
VMMR3DECL(void) PGMR3PhysChunkAgeing(PVM pVM)
{
    pgmLock(pVM);
    pVM->pgm.s.ChunkR3Map.AgeingCountdown = RT_MIN(pVM->pgm.s.ChunkR3Map.cMax / 4, 1024);
    pVM->pgm.s.ChunkR3Map.iNow++;
    if (pVM->pgm.s.ChunkR3Map.iNow == 0)
    {
        pVM->pgm.s.ChunkR3Map.iNow = 4;
        RTAvlU32DoWithAll(&pVM->pgm.s.ChunkR3Map.pTree, true /*fFromLeft*/, pgmR3PhysChunkAgeingRolloverCallback, pVM);
    }
    else
        RTAvlU32DoWithAll(&pVM->pgm.s.ChunkR3Map.pTree, true /*fFromLeft*/, pgmR3PhysChunkAgeingCallback, pVM);
    pgmUnlock(pVM);
}


/**
 * The structure passed in the pvUser argument of pgmR3PhysChunkUnmapCandidateCallback().
 */
typedef struct PGMR3PHYSCHUNKUNMAPCB
{
    PVM                 pVM;            /**< The VM handle. */
    PPGMCHUNKR3MAP      pChunk;         /**< The chunk to unmap. */
} PGMR3PHYSCHUNKUNMAPCB, *PPGMR3PHYSCHUNKUNMAPCB;


/**
 * Callback used to find the mapping that's been unused for
 * the longest time.
 */
static DECLCALLBACK(int) pgmR3PhysChunkUnmapCandidateCallback(PAVLLU32NODECORE pNode, void *pvUser)
{
    do
    {
        PPGMCHUNKR3MAP pChunk = (PPGMCHUNKR3MAP)((uint8_t *)pNode - RT_OFFSETOF(PGMCHUNKR3MAP, AgeCore));
        if (    pChunk->iAge
            &&  !pChunk->cRefs)
        {
            /*
             * Check that it's not in any of the TLBs.
             */
            PVM pVM = ((PPGMR3PHYSCHUNKUNMAPCB)pvUser)->pVM;
            for (unsigned i = 0; i < RT_ELEMENTS(pVM->pgm.s.ChunkR3Map.Tlb.aEntries); i++)
                if (pVM->pgm.s.ChunkR3Map.Tlb.aEntries[i].pChunk == pChunk)
                {
                    pChunk = NULL;
                    break;
                }
            if (pChunk)
                for (unsigned i = 0; i < RT_ELEMENTS(pVM->pgm.s.PhysTlbHC.aEntries); i++)
                    if (pVM->pgm.s.PhysTlbHC.aEntries[i].pMap == pChunk)
                    {
                        pChunk = NULL;
                        break;
                    }
            if (pChunk)
            {
                ((PPGMR3PHYSCHUNKUNMAPCB)pvUser)->pChunk = pChunk;
                return 1; /* done */
            }
        }

        /* next with the same age - this version of the AVL API doesn't enumerate the list, so we have to do it. */
        pNode = pNode->pList;
    } while (pNode);
    return 0;
}


/**
 * Finds a good candidate for unmapping when the ring-3 mapping cache is full.
 *
 * The candidate will not be part of any TLBs, so no need to flush
 * anything afterwards.
 *
 * @returns Chunk id.
 * @param   pVM         The VM handle.
 */
static int32_t pgmR3PhysChunkFindUnmapCandidate(PVM pVM)
{
    Assert(PGMIsLockOwner(pVM));

    /*
     * Do tree ageing first?
     */
    if (pVM->pgm.s.ChunkR3Map.AgeingCountdown-- == 0)
        PGMR3PhysChunkAgeing(pVM);

    /*
     * Enumerate the age tree starting with the left most node.
     */
    PGMR3PHYSCHUNKUNMAPCB Args;
    Args.pVM = pVM;
    Args.pChunk = NULL;
    if (RTAvllU32DoWithAll(&pVM->pgm.s.ChunkR3Map.pAgeTree, true /*fFromLeft*/, pgmR3PhysChunkUnmapCandidateCallback, pVM))
        return Args.pChunk->Core.Key;
    return INT32_MAX;
}


/**
 * Maps the given chunk into the ring-3 mapping cache.
 *
 * This will call ring-0.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 * @param   idChunk     The chunk in question.
 * @param   ppChunk     Where to store the chunk tracking structure.
 *
 * @remarks Called from within the PGM critical section.
 */
int pgmR3PhysChunkMap(PVM pVM, uint32_t idChunk, PPPGMCHUNKR3MAP ppChunk)
{
    int rc;

    Assert(PGMIsLockOwner(pVM));
    /*
     * Allocate a new tracking structure first.
     */
#ifdef VBOX_WITH_2X_4GB_ADDR_SPACE
    PPGMCHUNKR3MAP pChunk = (PPGMCHUNKR3MAP)MMR3HeapAlloc(pVM, MM_TAG_PGM_CHUNK_MAPPING, sizeof(*pChunk));
#else
    PPGMCHUNKR3MAP pChunk = (PPGMCHUNKR3MAP)MMR3UkHeapAlloc(pVM, MM_TAG_PGM_CHUNK_MAPPING, sizeof(*pChunk), NULL);
#endif
    AssertReturn(pChunk, VERR_NO_MEMORY);
    pChunk->Core.Key = idChunk;
    pChunk->AgeCore.Key = pVM->pgm.s.ChunkR3Map.iNow;
    pChunk->iAge = 0;
    pChunk->cRefs = 0;
    pChunk->cPermRefs = 0;
    pChunk->pv = NULL;

    /*
     * Request the ring-0 part to map the chunk in question and if
     * necessary unmap another one to make space in the mapping cache.
     */
    GMMMAPUNMAPCHUNKREQ Req;
    Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);
    Req.pvR3 = NULL;
    Req.idChunkMap = idChunk;
    Req.idChunkUnmap = NIL_GMM_CHUNKID;
    if (pVM->pgm.s.ChunkR3Map.c >= pVM->pgm.s.ChunkR3Map.cMax)
        Req.idChunkUnmap = pgmR3PhysChunkFindUnmapCandidate(pVM);
/** @todo This is wrong. Any thread in the VM process should be able to do this,
 *        there are depenenecies on this.  What currently saves the day is that
 *        we don't unmap anything and that all non-zero memory will therefore
 *        be present when non-EMTs tries to access it.  */
    rc = VMMR3CallR0(pVM, VMMR0_DO_GMM_MAP_UNMAP_CHUNK, 0, &Req.Hdr);
    if (RT_SUCCESS(rc))
    {
        /*
         * Update the tree.
         */
        /* insert the new one. */
        AssertPtr(Req.pvR3);
        pChunk->pv = Req.pvR3;
        bool fRc = RTAvlU32Insert(&pVM->pgm.s.ChunkR3Map.pTree, &pChunk->Core);
        AssertRelease(fRc);
        pVM->pgm.s.ChunkR3Map.c++;

        fRc = RTAvllU32Insert(&pVM->pgm.s.ChunkR3Map.pAgeTree, &pChunk->AgeCore);
        AssertRelease(fRc);

        /* remove the unmapped one. */
        if (Req.idChunkUnmap != NIL_GMM_CHUNKID)
        {
            PPGMCHUNKR3MAP pUnmappedChunk = (PPGMCHUNKR3MAP)RTAvlU32Remove(&pVM->pgm.s.ChunkR3Map.pTree, Req.idChunkUnmap);
            AssertRelease(pUnmappedChunk);
            pUnmappedChunk->pv = NULL;
            pUnmappedChunk->Core.Key = UINT32_MAX;
#ifdef VBOX_WITH_2X_4GB_ADDR_SPACE
            MMR3HeapFree(pUnmappedChunk);
#else
            MMR3UkHeapFree(pVM, pUnmappedChunk, MM_TAG_PGM_CHUNK_MAPPING);
#endif
            pVM->pgm.s.ChunkR3Map.c--;
        }
    }
    else
    {
        AssertRC(rc);
#ifdef VBOX_WITH_2X_4GB_ADDR_SPACE
        MMR3HeapFree(pChunk);
#else
        MMR3UkHeapFree(pVM, pChunk, MM_TAG_PGM_CHUNK_MAPPING);
#endif
        pChunk = NULL;
    }

    *ppChunk = pChunk;
    return rc;
}


/**
 * For VMMCALLRING3_PGM_MAP_CHUNK, considered internal.
 *
 * @returns see pgmR3PhysChunkMap.
 * @param   pVM         The VM handle.
 * @param   idChunk     The chunk to map.
 */
VMMR3DECL(int) PGMR3PhysChunkMap(PVM pVM, uint32_t idChunk)
{
    PPGMCHUNKR3MAP pChunk;
    int rc;

    pgmLock(pVM);
    rc = pgmR3PhysChunkMap(pVM, idChunk, &pChunk);
    pgmUnlock(pVM);
    return rc;
}


/**
 * Invalidates the TLB for the ring-3 mapping cache.
 *
 * @param   pVM         The VM handle.
 */
VMMR3DECL(void) PGMR3PhysChunkInvalidateTLB(PVM pVM)
{
    pgmLock(pVM);
    for (unsigned i = 0; i < RT_ELEMENTS(pVM->pgm.s.ChunkR3Map.Tlb.aEntries); i++)
    {
        pVM->pgm.s.ChunkR3Map.Tlb.aEntries[i].idChunk = NIL_GMM_CHUNKID;
        pVM->pgm.s.ChunkR3Map.Tlb.aEntries[i].pChunk = NULL;
    }
    pgmUnlock(pVM);
}


/**
 * Response to VM_FF_PGM_NEED_HANDY_PAGES and VMMCALLRING3_PGM_ALLOCATE_HANDY_PAGES.
 *
 * This function will also work the VM_FF_PGM_NO_MEMORY force action flag, to
 * signal and clear the out of memory condition. When contracted, this API is
 * used to try clear the condition when the user wants to resume.
 *
 * @returns The following VBox status codes.
 * @retval  VINF_SUCCESS on success. FFs cleared.
 * @retval  VINF_EM_NO_MEMORY if we're out of memory. The FF is not cleared in
 *          this case and it gets accompanied by VM_FF_PGM_NO_MEMORY.
 *
 * @param   pVM         The VM handle.
 *
 * @remarks The VINF_EM_NO_MEMORY status is for the benefit of the FF processing
 *          in EM.cpp and shouldn't be propagated outside TRPM, HWACCM, EM and
 *          pgmPhysEnsureHandyPage. There is one exception to this in the \#PF
 *          handler.
 */
VMMR3DECL(int) PGMR3PhysAllocateHandyPages(PVM pVM)
{
    pgmLock(pVM);

    /*
     * Allocate more pages, noting down the index of the first new page.
     */
    uint32_t iClear = pVM->pgm.s.cHandyPages;
    AssertMsgReturn(iClear <= RT_ELEMENTS(pVM->pgm.s.aHandyPages), ("%d", iClear), VERR_INTERNAL_ERROR);
    Log(("PGMR3PhysAllocateHandyPages: %d -> %d\n", iClear, RT_ELEMENTS(pVM->pgm.s.aHandyPages)));
    int rcAlloc = VINF_SUCCESS;
    int rcSeed  = VINF_SUCCESS;
    int rc = VMMR3CallR0(pVM, VMMR0_DO_PGM_ALLOCATE_HANDY_PAGES, 0, NULL);
    while (rc == VERR_GMM_SEED_ME)
    {
        void *pvChunk;
        rcAlloc = rc = SUPR3PageAlloc(GMM_CHUNK_SIZE >> PAGE_SHIFT, &pvChunk);
        if (RT_SUCCESS(rc))
        {
            rcSeed = rc = VMMR3CallR0(pVM, VMMR0_DO_GMM_SEED_CHUNK, (uintptr_t)pvChunk, NULL);
            if (RT_FAILURE(rc))
                SUPR3PageFree(pvChunk, GMM_CHUNK_SIZE >> PAGE_SHIFT);
        }
        if (RT_SUCCESS(rc))
            rc = VMMR3CallR0(pVM, VMMR0_DO_PGM_ALLOCATE_HANDY_PAGES, 0, NULL);
    }

    if (RT_SUCCESS(rc))
    {
        AssertMsg(rc == VINF_SUCCESS, ("%Rrc\n", rc));
        Assert(pVM->pgm.s.cHandyPages > 0);
        VM_FF_CLEAR(pVM, VM_FF_PGM_NEED_HANDY_PAGES);
        VM_FF_CLEAR(pVM, VM_FF_PGM_NO_MEMORY);

        /*
         * Clear the pages.
         */
        while (iClear < pVM->pgm.s.cHandyPages)
        {
            PGMMPAGEDESC pPage = &pVM->pgm.s.aHandyPages[iClear];
            void *pv;
            rc = pgmPhysPageMapByPageID(pVM, pPage->idPage, pPage->HCPhysGCPhys, &pv);
            AssertLogRelMsgBreak(RT_SUCCESS(rc), ("idPage=%#x HCPhysGCPhys=%RHp rc=%Rrc", pPage->idPage, pPage->HCPhysGCPhys, rc));
            ASMMemZeroPage(pv);
            iClear++;
            Log3(("PGMR3PhysAllocateHandyPages: idPage=%#x HCPhys=%RGp\n", pPage->idPage, pPage->HCPhysGCPhys));
        }
    }
    else
    {
        /*
         * We should never get here unless there is a genuine shortage of
         * memory (or some internal error). Flag the error so the VM can be
         * suspended ASAP and the user informed. If we're totally out of
         * handy pages we will return failure.
         */
        /* Report the failure. */
        LogRel(("PGM: Failed to procure handy pages; rc=%Rrc rcAlloc=%Rrc rcSeed=%Rrc cHandyPages=%#x\n"
                "     cAllPages=%#x cPrivatePages=%#x cSharedPages=%#x cZeroPages=%#x\n",
                rc, rcAlloc, rcSeed,
                pVM->pgm.s.cHandyPages,
                pVM->pgm.s.cAllPages,
                pVM->pgm.s.cPrivatePages,
                pVM->pgm.s.cSharedPages,
                pVM->pgm.s.cZeroPages));
        if (    rc != VERR_NO_MEMORY
            &&  rc != VERR_LOCK_FAILED)
        {
            for (uint32_t i = 0; i < RT_ELEMENTS(pVM->pgm.s.aHandyPages); i++)
            {
                LogRel(("PGM: aHandyPages[#%#04x] = {.HCPhysGCPhys=%RHp, .idPage=%#08x, .idSharedPage=%#08x}\n",
                        i, pVM->pgm.s.aHandyPages[i].HCPhysGCPhys, pVM->pgm.s.aHandyPages[i].idPage,
                        pVM->pgm.s.aHandyPages[i].idSharedPage));
                uint32_t const idPage = pVM->pgm.s.aHandyPages[i].idPage;
                if (idPage != NIL_GMM_PAGEID)
                {
                    for (PPGMRAMRANGE pRam = pVM->pgm.s.pRamRangesR3;
                         pRam;
                         pRam = pRam->pNextR3)
                    {
                        uint32_t const cPages = pRam->cb >> PAGE_SHIFT;
                        for (uint32_t iPage = 0; iPage < cPages; iPage++)
                            if (PGM_PAGE_GET_PAGEID(&pRam->aPages[iPage]) == idPage)
                                LogRel(("PGM: Used by %RGp %R[pgmpage] (%s)\n",
                                        pRam->GCPhys + ((RTGCPHYS)iPage << PAGE_SHIFT), &pRam->aPages[iPage], pRam->pszDesc));
                    }
                }
            }
        }

        /* Set the FFs and adjust rc. */
        VM_FF_SET(pVM, VM_FF_PGM_NEED_HANDY_PAGES);
        VM_FF_SET(pVM, VM_FF_PGM_NO_MEMORY);
        if (    rc == VERR_NO_MEMORY
            ||  rc == VERR_LOCK_FAILED)
            rc = VINF_EM_NO_MEMORY;
    }

    pgmUnlock(pVM);
    return rc;
}


/**
 * Frees the specified RAM page and replaces it with the ZERO page.
 *
 * This is used by ballooning, remapping MMIO2 and RAM reset.
 *
 * @param   pVM         Pointer to the shared VM structure.
 * @param   pReq        Pointer to the request.
 * @param   pPage       Pointer to the page structure.
 * @param   GCPhys      The guest physical address of the page, if applicable.
 *
 * @remarks The caller must own the PGM lock.
 */
static int pgmPhysFreePage(PVM pVM, PGMMFREEPAGESREQ pReq, uint32_t *pcPendingPages, PPGMPAGE pPage, RTGCPHYS GCPhys)
{
    /*
     * Assert sanity.
     */
    Assert(PGMIsLockOwner(pVM));
    if (RT_UNLIKELY(    PGM_PAGE_GET_TYPE(pPage) != PGMPAGETYPE_RAM
                    &&  PGM_PAGE_GET_TYPE(pPage) != PGMPAGETYPE_ROM_SHADOW))
    {
        AssertMsgFailed(("GCPhys=%RGp pPage=%R[pgmpage]\n", GCPhys, pPage));
        return VMSetError(pVM, VERR_PGM_PHYS_NOT_RAM, RT_SRC_POS, "GCPhys=%RGp type=%d", GCPhys, PGM_PAGE_GET_TYPE(pPage));
    }

    if (PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ZERO)
        return VINF_SUCCESS;

    const uint32_t idPage = PGM_PAGE_GET_PAGEID(pPage);
    Log3(("pgmPhysFreePage: idPage=%#x HCPhys=%RGp pPage=%R[pgmpage]\n", idPage, pPage));
    if (RT_UNLIKELY(    idPage == NIL_GMM_PAGEID
                    ||  idPage > GMM_PAGEID_LAST
                    ||  PGM_PAGE_GET_CHUNKID(pPage) == NIL_GMM_CHUNKID))
    {
        AssertMsgFailed(("GCPhys=%RGp pPage=%R[pgmpage]\n", GCPhys, pPage));
        return VMSetError(pVM, VERR_PGM_PHYS_INVALID_PAGE_ID, RT_SRC_POS, "GCPhys=%RGp idPage=%#x", GCPhys, pPage);
    }

    /* update page count stats. */
    if (PGM_PAGE_IS_SHARED(pPage))
        pVM->pgm.s.cSharedPages--;
    else
        pVM->pgm.s.cPrivatePages--;
    pVM->pgm.s.cZeroPages++;

    /*
     * pPage = ZERO page.
     */
    PGM_PAGE_SET_HCPHYS(pPage, pVM->pgm.s.HCPhysZeroPg);
    PGM_PAGE_SET_STATE(pPage, PGM_PAGE_STATE_ZERO);
    PGM_PAGE_SET_PAGEID(pPage, NIL_GMM_PAGEID);

    /*
     * Make sure it's not in the handy page array.
     */
    for (uint32_t i = pVM->pgm.s.cHandyPages; i < RT_ELEMENTS(pVM->pgm.s.aHandyPages); i++)
    {
        if (pVM->pgm.s.aHandyPages[i].idPage == idPage)
        {
            pVM->pgm.s.aHandyPages[i].idPage = NIL_GMM_PAGEID;
            break;
        }
        if (pVM->pgm.s.aHandyPages[i].idSharedPage == idPage)
        {
            pVM->pgm.s.aHandyPages[i].idSharedPage = NIL_GMM_PAGEID;
            break;
        }
    }

    /*
     * Push it onto the page array.
     */
    uint32_t iPage = *pcPendingPages;
    Assert(iPage < PGMPHYS_FREE_PAGE_BATCH_SIZE);
    *pcPendingPages += 1;

    pReq->aPages[iPage].idPage = idPage;

    if (iPage + 1 < PGMPHYS_FREE_PAGE_BATCH_SIZE)
        return VINF_SUCCESS;

    /*
     * Flush the pages.
     */
    int rc = GMMR3FreePagesPerform(pVM, pReq, PGMPHYS_FREE_PAGE_BATCH_SIZE);
    if (RT_SUCCESS(rc))
    {
        GMMR3FreePagesRePrep(pVM, pReq, PGMPHYS_FREE_PAGE_BATCH_SIZE, GMMACCOUNT_BASE);
        *pcPendingPages = 0;
    }
    return rc;
}


/**
 * Converts a GC physical address to a HC ring-3 pointer, with some
 * additional checks.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_PGM_PHYS_TLB_CATCH_WRITE and *ppv set if the page has a write
 *          access handler of some kind.
 * @retval  VERR_PGM_PHYS_TLB_CATCH_ALL if the page has a handler catching all
 *          accesses or is odd in any way.
 * @retval  VERR_PGM_PHYS_TLB_UNASSIGNED if the page doesn't exist.
 *
 * @param   pVM         The VM handle.
 * @param   GCPhys      The GC physical address to convert.
 * @param   fWritable   Whether write access is required.
 * @param   ppv         Where to store the pointer corresponding to GCPhys on
 *                      success.
 */
VMMR3DECL(int) PGMR3PhysTlbGCPhys2Ptr(PVM pVM, RTGCPHYS GCPhys, bool fWritable, void **ppv)
{
    pgmLock(pVM);

    PPGMRAMRANGE pRam;
    PPGMPAGE pPage;
    int rc = pgmPhysGetPageAndRangeEx(&pVM->pgm.s, GCPhys, &pPage, &pRam);
    if (RT_SUCCESS(rc))
    {
        if (!PGM_PAGE_HAS_ANY_HANDLERS(pPage))
            rc = VINF_SUCCESS;
        else
        {
            if (PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPage)) /* catches MMIO */
                rc = VERR_PGM_PHYS_TLB_CATCH_ALL;
            else if (PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage))
            {
                /** @todo Handle TLB loads of virtual handlers so ./test.sh can be made to work
                 *        in -norawr0 mode. */
                if (fWritable)
                    rc = VINF_PGM_PHYS_TLB_CATCH_WRITE;
            }
            else
            {
                /* Temporarily disabled physical handler(s), since the recompiler
                   doesn't get notified when it's reset we'll have to pretend it's
                   operating normally. */
                if (pgmHandlerPhysicalIsAll(pVM, GCPhys))
                    rc = VERR_PGM_PHYS_TLB_CATCH_ALL;
                else
                    rc = VINF_PGM_PHYS_TLB_CATCH_WRITE;
            }
        }
        if (RT_SUCCESS(rc))
        {
            int rc2;

            /* Make sure what we return is writable. */
            if (fWritable && rc != VINF_PGM_PHYS_TLB_CATCH_WRITE)
                switch (PGM_PAGE_GET_STATE(pPage))
                {
                    case PGM_PAGE_STATE_ALLOCATED:
                        break;
                    case PGM_PAGE_STATE_ZERO:
                    case PGM_PAGE_STATE_SHARED:
                    case PGM_PAGE_STATE_WRITE_MONITORED:
                        rc2 = pgmPhysPageMakeWritable(pVM, pPage, GCPhys & ~(RTGCPHYS)PAGE_OFFSET_MASK);
                        AssertLogRelRCReturn(rc2, rc2);
                        break;
                }

            /* Get a ring-3 mapping of the address. */
            PPGMPAGER3MAPTLBE pTlbe;
            rc2 = pgmPhysPageQueryTlbe(&pVM->pgm.s, GCPhys, &pTlbe);
            AssertLogRelRCReturn(rc2, rc2);
            *ppv = (void *)((uintptr_t)pTlbe->pv | (GCPhys & PAGE_OFFSET_MASK));
            /** @todo mapping/locking hell; this isn't horribly efficient since
             *        pgmPhysPageLoadIntoTlb will repeat the lookup we've done here. */

            Log6(("PGMR3PhysTlbGCPhys2Ptr: GCPhys=%RGp rc=%Rrc pPage=%R[pgmpage] *ppv=%p\n", GCPhys, rc, pPage, *ppv));
        }
        else
            Log6(("PGMR3PhysTlbGCPhys2Ptr: GCPhys=%RGp rc=%Rrc pPage=%R[pgmpage]\n", GCPhys, rc, pPage));

        /* else: handler catching all access, no pointer returned. */
    }
    else
        rc = VERR_PGM_PHYS_TLB_UNASSIGNED;

    pgmUnlock(pVM);
    return rc;
}



