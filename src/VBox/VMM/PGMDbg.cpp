/* $Id$ */
/** @file
 * PGM - Page Manager and Monitor - Debugger & Debugging APIs.
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
#define LOG_GROUP LOG_GROUP_PGM
#include <VBox/pgm.h>
#include <VBox/stam.h>
#include "PGMInternal.h"
#include <VBox/vm.h>
#include "PGMInline.h"
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <VBox/log.h>
#include <VBox/param.h>
#include <VBox/err.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** The max needle size that we will bother searching for
 * This must not be more than half a page! */
#define MAX_NEEDLE_SIZE     256


/**
 * Converts a R3 pointer to a GC physical address.
 *
 * Only for the debugger.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success, *pGCPhys is set.
 * @retval  VERR_INVALID_POINTER if the pointer is not within the GC physical memory.
 *
 * @param   pVM         The VM handle.
 * @param   R3Ptr       The R3 pointer to convert.
 * @param   pGCPhys     Where to store the GC physical address on success.
 */
VMMR3DECL(int) PGMR3DbgR3Ptr2GCPhys(PVM pVM, RTR3PTR R3Ptr, PRTGCPHYS pGCPhys)
{
    *pGCPhys = NIL_RTGCPHYS;
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Converts a R3 pointer to a HC physical address.
 *
 * Only for the debugger.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success, *pHCPhys is set.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid GC physical page but has no physical backing.
 * @retval  VERR_INVALID_POINTER if the pointer is not within the GC physical memory.
 *
 * @param   pVM         The VM handle.
 * @param   R3Ptr       The R3 pointer to convert.
 * @param   pHCPhys     Where to store the HC physical address on success.
 */
VMMR3DECL(int) PGMR3DbgR3Ptr2HCPhys(PVM pVM, RTR3PTR R3Ptr, PRTHCPHYS pHCPhys)
{
    *pHCPhys = NIL_RTHCPHYS;
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Converts a HC physical address to a GC physical address.
 *
 * Only for the debugger.
 *
 * @returns VBox status code
 * @retval  VINF_SUCCESS on success, *pGCPhys is set.
 * @retval  VERR_INVALID_POINTER if the HC physical address is not within the GC physical memory.
 *
 * @param   pVM     The VM handle.
 * @param   HCPhys  The HC physical address to convert.
 * @param   pGCPhys Where to store the GC physical address on success.
 */
VMMR3DECL(int) PGMR3DbgHCPhys2GCPhys(PVM pVM, RTHCPHYS HCPhys, PRTGCPHYS pGCPhys)
{
    /*
     * Validate and adjust the input a bit.
     */
    if (HCPhys == NIL_RTHCPHYS)
        return VERR_INVALID_POINTER;
    unsigned off = HCPhys & PAGE_OFFSET_MASK;
    HCPhys &= X86_PTE_PAE_PG_MASK;
    if (HCPhys == 0)
        return VERR_INVALID_POINTER;

    for (PPGMRAMRANGE pRam = pVM->pgm.s.CTX_SUFF(pRamRanges);
         pRam;
         pRam = pRam->CTX_SUFF(pNext))
    {
        uint32_t iPage = pRam->cb >> PAGE_SHIFT;
        while (iPage-- > 0)
            if (PGM_PAGE_GET_HCPHYS(&pRam->aPages[iPage]) == HCPhys)
            {
                *pGCPhys = pRam->GCPhys + (iPage << PAGE_SHIFT) + off;
                return VINF_SUCCESS;
            }
    }
    return VERR_INVALID_POINTER;
}


/**
 * Read physical memory API for the debugger, similar to
 * PGMPhysSimpleReadGCPhys.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The VM handle.
 * @param   pvDst       Where to store what's read.
 * @param   GCPhysDst   Where to start reading from.
 * @param   cb          The number of bytes to attempt reading.
 * @param   fFlags      Flags, MBZ.
 * @param   pcbRead     For store the actual number of bytes read, pass NULL if
 *                      partial reads are unwanted.
 * @todo    Unused?
 */
VMMR3DECL(int) PGMR3DbgReadGCPhys(PVM pVM, void *pvDst, RTGCPHYS GCPhysSrc, size_t cb, uint32_t fFlags, size_t *pcbRead)
{
    /* validate */
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertReturn(pVM, VERR_INVALID_PARAMETER);

    /* try simple first. */
    int rc = PGMPhysSimpleReadGCPhys(pVM, pvDst, GCPhysSrc, cb);
    if (RT_SUCCESS(rc) || !pcbRead)
        return rc;

    /* partial read that failed, chop it up in pages. */
    *pcbRead = 0;
    size_t const cbReq = cb;
    rc = VINF_SUCCESS;
    while (cb > 0)
    {
        size_t cbChunk = PAGE_SIZE;
        cbChunk -= GCPhysSrc & PAGE_OFFSET_MASK;
        if (cbChunk > cb)
            cbChunk = cb;

        rc = PGMPhysSimpleReadGCPhys(pVM, pvDst, GCPhysSrc, cbChunk);

        /* advance */
        if (RT_FAILURE(rc))
            break;
        *pcbRead  += cbChunk;
        cb        -= cbChunk;
        GCPhysSrc += cbChunk;
        pvDst = (uint8_t *)pvDst + cbChunk;
    }

    return *pcbRead && RT_FAILURE(rc) ? -rc : rc;
}


/**
 * Write physical memory API for the debugger, similar to
 * PGMPhysSimpleWriteGCPhys.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The VM handle.
 * @param   GCPhysDst   Where to start writing.
 * @param   pvSrc       What to write.
 * @param   cb          The number of bytes to attempt writing.
 * @param   fFlags      Flags, MBZ.
 * @param   pcbWritten  For store the actual number of bytes written, pass NULL
 *                      if partial writes are unwanted.
 * @todo    Unused?
 */
VMMR3DECL(int) PGMR3DbgWriteGCPhys(PVM pVM, RTGCPHYS GCPhysDst, const void *pvSrc, size_t cb, uint32_t fFlags, size_t *pcbWritten)
{
    /* validate */
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertReturn(pVM, VERR_INVALID_PARAMETER);

    /* try simple first. */
    int rc = PGMPhysSimpleWriteGCPhys(pVM, GCPhysDst, pvSrc, cb);
    if (RT_SUCCESS(rc) || !pcbWritten)
        return rc;

    /* partial write that failed, chop it up in pages. */
    *pcbWritten = 0;
    rc = VINF_SUCCESS;
    while (cb > 0)
    {
        size_t cbChunk = PAGE_SIZE;
        cbChunk -= GCPhysDst & PAGE_OFFSET_MASK;
        if (cbChunk > cb)
            cbChunk = cb;

        rc = PGMPhysSimpleWriteGCPhys(pVM, GCPhysDst, pvSrc, cbChunk);

        /* advance */
        if (RT_FAILURE(rc))
            break;
        *pcbWritten += cbChunk;
        cb          -= cbChunk;
        GCPhysDst   += cbChunk;
        pvSrc = (uint8_t const *)pvSrc + cbChunk;
    }

    return *pcbWritten && RT_FAILURE(rc) ? -rc : rc;

}


/**
 * Read virtual memory API for the debugger, similar to PGMPhysSimpleReadGCPtr.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The VM handle.
 * @param   pvDst       Where to store what's read.
 * @param   GCPtrDst    Where to start reading from.
 * @param   cb          The number of bytes to attempt reading.
 * @param   fFlags      Flags, MBZ.
 * @param   pcbRead     For store the actual number of bytes read, pass NULL if
 *                      partial reads are unwanted.
 * @todo    Unused?
 */
VMMR3DECL(int) PGMR3DbgReadGCPtr(PVM pVM, void *pvDst, RTGCPTR GCPtrSrc, size_t cb, uint32_t fFlags, size_t *pcbRead)
{
    /* validate */
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertReturn(pVM, VERR_INVALID_PARAMETER);

    /* @todo SMP support! */
    PVMCPU pVCpu = &pVM->aCpus[0];

/** @todo deal with HMA */
    /* try simple first. */
    int rc = PGMPhysSimpleReadGCPtr(pVCpu, pvDst, GCPtrSrc, cb);
    if (RT_SUCCESS(rc) || !pcbRead)
        return rc;

    /* partial read that failed, chop it up in pages. */
    *pcbRead = 0;
    rc = VINF_SUCCESS;
    while (cb > 0)
    {
        size_t cbChunk = PAGE_SIZE;
        cbChunk -= GCPtrSrc & PAGE_OFFSET_MASK;
        if (cbChunk > cb)
            cbChunk = cb;

        rc = PGMPhysSimpleReadGCPtr(pVCpu, pvDst, GCPtrSrc, cbChunk);

        /* advance */
        if (RT_FAILURE(rc))
            break;
        *pcbRead  += cbChunk;
        cb        -= cbChunk;
        GCPtrSrc  += cbChunk;
        pvDst = (uint8_t *)pvDst + cbChunk;
    }

    return *pcbRead && RT_FAILURE(rc) ? -rc : rc;

}


/**
 * Write virtual memory API for the debugger, similar to
 * PGMPhysSimpleWriteGCPtr.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The VM handle.
 * @param   GCPtrDst    Where to start writing.
 * @param   pvSrc       What to write.
 * @param   cb          The number of bytes to attempt writing.
 * @param   fFlags      Flags, MBZ.
 * @param   pcbWritten  For store the actual number of bytes written, pass NULL
 *                      if partial writes are unwanted.
 * @todo    Unused?
 */
VMMR3DECL(int) PGMR3DbgWriteGCPtr(PVM pVM, RTGCPTR GCPtrDst, void const *pvSrc, size_t cb, uint32_t fFlags, size_t *pcbWritten)
{
    /* validate */
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertReturn(pVM, VERR_INVALID_PARAMETER);

    /* @todo SMP support! */
    PVMCPU pVCpu = &pVM->aCpus[0];

/** @todo deal with HMA */
    /* try simple first. */
    int rc = PGMPhysSimpleWriteGCPtr(pVCpu, GCPtrDst, pvSrc, cb);
    if (RT_SUCCESS(rc) || !pcbWritten)
        return rc;

    /* partial write that failed, chop it up in pages. */
    *pcbWritten = 0;
    rc = VINF_SUCCESS;
    while (cb > 0)
    {
        size_t cbChunk = PAGE_SIZE;
        cbChunk -= GCPtrDst & PAGE_OFFSET_MASK;
        if (cbChunk > cb)
            cbChunk = cb;

        rc = PGMPhysSimpleWriteGCPtr(pVCpu, GCPtrDst, pvSrc, cbChunk);

        /* advance */
        if (RT_FAILURE(rc))
            break;
        *pcbWritten += cbChunk;
        cb          -= cbChunk;
        GCPtrDst    += cbChunk;
        pvSrc = (uint8_t const *)pvSrc + cbChunk;
    }

    return *pcbWritten && RT_FAILURE(rc) ? -rc : rc;

}


/**
 * memchr() with alignment considerations.
 *
 * @returns Pointer to matching byte, NULL if none found.
 * @param   pb                  Where to search. Aligned.
 * @param   b                   What to search for.
 * @param   cb                  How much to search .
 * @param   uAlign              The alignment restriction of the result.
 */
static const uint8_t *pgmR3DbgAlignedMemChr(const uint8_t *pb, uint8_t b, size_t cb, uint32_t uAlign)
{
    const uint8_t *pbRet;
    if (uAlign <= 32)
    {
        pbRet = (const uint8_t *)memchr(pb, b, cb);
        if ((uintptr_t)pbRet & (uAlign - 1))
        {
            do
            {
                pbRet++;
                size_t cbLeft = cb - (pbRet - pb);
                if (!cbLeft)
                {
                    pbRet = NULL;
                    break;
                }
                pbRet = (const uint8_t *)memchr(pbRet, b, cbLeft);
            } while ((uintptr_t)pbRet & (uAlign - 1));
        }
    }
    else
    {
        pbRet = NULL;
        if (cb)
        {
            for (;;)
            {
                if (*pb == b)
                {
                    pbRet = pb;
                    break;
                }
                if (cb <= uAlign)
                    break;
                cb -= uAlign;
                pb += uAlign;
            }
        }
    }
    return pbRet;
}


/**
 * Scans a page for a byte string, keeping track of potential
 * cross page matches.
 *
 * @returns true and *poff on match.
 *          false on mismatch.
 * @param   pbPage          Pointer to the current page.
 * @param   poff            Input: The offset into the page (aligned).
 *                          Output: The page offset of the match on success.
 * @param   cb              The number of bytes to search, starting of *poff.
 * @param   uAlign          The needle alignment. This is of course less than a page.
 * @param   pabNeedle       The byte string to search for.
 * @param   cbNeedle        The length of the byte string.
 * @param   pabPrev         The buffer that keeps track of a partial match that we
 *                          bring over from the previous page. This buffer must be
 *                          at least cbNeedle - 1 big.
 * @param   pcbPrev         Input: The number of partial matching bytes from the previous page.
 *                          Output: The number of partial matching bytes from this page.
 *                          Initialize to 0 before the first call to this function.
 */
static bool pgmR3DbgScanPage(const uint8_t *pbPage, int32_t *poff, uint32_t cb, uint32_t uAlign,
                             const uint8_t *pabNeedle, size_t cbNeedle,
                             uint8_t *pabPrev, size_t *pcbPrev)
{
    /*
     * Try complete any partial match from the previous page.
     */
    if (*pcbPrev > 0)
    {
        size_t cbPrev = *pcbPrev;
        Assert(!*poff);
        Assert(cbPrev < cbNeedle);
        if (!memcmp(pbPage, pabNeedle + cbPrev, cbNeedle - cbPrev))
        {
            if (cbNeedle - cbPrev > cb)
                return false;
            *poff = -(int32_t)cbPrev;
            return true;
        }

        /* check out the remainder of the previous page. */
        const uint8_t *pb = pabPrev;
        for (;;)
        {
            if (cbPrev <= uAlign)
                break;
            cbPrev -= uAlign;
            pb = pgmR3DbgAlignedMemChr(pb + uAlign, *pabNeedle, cbPrev, uAlign);
            if (!pb)
                break;
            cbPrev = *pcbPrev - (pb - pabPrev);
            if (    !memcmp(pb + 1, &pabNeedle[1], cbPrev - 1)
                &&  !memcmp(pbPage, pabNeedle + cbPrev, cbNeedle - cbPrev))
            {
                if (cbNeedle - cbPrev > cb)
                    return false;
                *poff = -(int32_t)cbPrev;
                return true;
            }
        }

        *pcbPrev = 0;
    }

    /*
     * Match the body of the page.
     */
    const uint8_t *pb = pbPage + *poff;
    const uint8_t *pbEnd = pb + cb;
    for (;;)
    {
        pb = pgmR3DbgAlignedMemChr(pb, *pabNeedle, cb, uAlign);
        if (!pb)
            break;
        cb = pbEnd - pb;
        if (cb >= cbNeedle)
        {
            /* match? */
            if (!memcmp(pb + 1, &pabNeedle[1], cbNeedle - 1))
            {
                *poff = pb - pbPage;
                return true;
            }
        }
        else
        {
            /* paritial match at the end of the page? */
            if (!memcmp(pb + 1, &pabNeedle[1], cb - 1))
            {
                /* We're copying one byte more that we really need here, but wtf. */
                memcpy(pabPrev, pb, cb);
                *pcbPrev = cb;
                return false;
            }
        }

        /* no match, skip ahead. */
        if (cb <= uAlign)
            break;
        pb += uAlign;
        cb -= uAlign;
    }

    return false;
}


/**
 * Scans guest physical memory for a byte string.
 *
 * @returns VBox status codes:
 * @retval  VINF_SUCCESS and *pGCPtrHit on success.
 * @retval  VERR_DBGF_MEM_NOT_FOUND if not found.
 * @retval  VERR_INVALID_POINTER if any of the pointer arguments are invalid.
 * @retval  VERR_INVALID_ARGUMENT if any other arguments are invalid.
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   GCPhys          Where to start searching.
 * @param   cbRange         The number of bytes to search.
 * @param   GCPhysAlign     The alignment of the needle. Must be a power of two
 *                          and less or equal to 4GB.
 * @param   pabNeedle       The byte string to search for.
 * @param   cbNeedle        The length of the byte string. Max 256 bytes.
 * @param   pGCPhysHit      Where to store the address of the first occurence on success.
 */
VMMR3DECL(int) PGMR3DbgScanPhysical(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cbRange, RTGCPHYS GCPhysAlign,
                                    const uint8_t *pabNeedle, size_t cbNeedle, PRTGCPHYS pGCPhysHit)
{
    /*
     * Validate and adjust the input a bit.
     */
    if (!VALID_PTR(pGCPhysHit))
        return VERR_INVALID_POINTER;
    *pGCPhysHit = NIL_RTGCPHYS;

    if (    !VALID_PTR(pabNeedle)
        ||  GCPhys == NIL_RTGCPHYS)
        return VERR_INVALID_POINTER;
    if (!cbNeedle)
        return VERR_INVALID_PARAMETER;
    if (cbNeedle > MAX_NEEDLE_SIZE)
        return VERR_INVALID_PARAMETER;

    if (!cbRange)
        return VERR_DBGF_MEM_NOT_FOUND;
    if (GCPhys + cbNeedle - 1 < GCPhys)
        return VERR_DBGF_MEM_NOT_FOUND;

    if (!GCPhysAlign)
        return VERR_INVALID_PARAMETER;
    if (GCPhysAlign > UINT32_MAX)
        return VERR_NOT_POWER_OF_TWO;
    if (GCPhysAlign & (GCPhysAlign - 1))
        return VERR_INVALID_PARAMETER;

    if (GCPhys & (GCPhysAlign - 1))
    {
        RTGCPHYS Adj = GCPhysAlign - (GCPhys & (GCPhysAlign - 1));
        if (    cbRange <= Adj
            ||  GCPhys + Adj < GCPhys)
            return VERR_DBGF_MEM_NOT_FOUND;
        GCPhys  += Adj;
        cbRange -= Adj;
    }

    const bool      fAllZero   = ASMMemIsAll8(pabNeedle, cbNeedle, 0) == NULL;
    const uint32_t  cIncPages  = GCPhysAlign <= PAGE_SIZE
                               ? 1
                               : GCPhysAlign >> PAGE_SHIFT;
    const RTGCPHYS  GCPhysLast = GCPhys + cbRange - 1 >= GCPhys
                               ? GCPhys + cbRange - 1
                               : ~(RTGCPHYS)0;

    /*
     * Search the memory - ignore MMIO and zero pages, also don't
     * bother to match across ranges.
     */
    pgmLock(pVM);
    for (PPGMRAMRANGE pRam = pVM->pgm.s.CTX_SUFF(pRamRanges);
         pRam;
         pRam = pRam->CTX_SUFF(pNext))
    {
        /*
         * If the search range starts prior to the current ram range record,
         * adjust the search range and possibly conclude the search.
         */
        RTGCPHYS off;
        if (GCPhys < pRam->GCPhys)
        {
            if (GCPhysLast < pRam->GCPhys)
                break;
            GCPhys = pRam->GCPhys;
            off = 0;
        }
        else
            off = GCPhys - pRam->GCPhys;
        if (off < pRam->cb)
        {
            /*
             * Iterate the relevant pages.
             */
            uint8_t         abPrev[MAX_NEEDLE_SIZE];
            size_t          cbPrev   = 0;
            const uint32_t  cPages   = pRam->cb >> PAGE_SHIFT;
            uint32_t        iPage    = off >> PAGE_SHIFT;
            uint32_t        offPage  = GCPhys & PAGE_OFFSET_MASK;
            GCPhys &= ~(RTGCPHYS)PAGE_OFFSET_MASK;
            for (;; offPage = 0)
            {
                PPGMPAGE pPage = &pRam->aPages[iPage];
                if (    (   !PGM_PAGE_IS_ZERO(pPage)
                         || fAllZero)
                    &&  !PGM_PAGE_IS_BALLOONED(pPage)
                    &&  !PGM_PAGE_IS_MMIO(pPage))
                {
                    void const     *pvPage;
                    PGMPAGEMAPLOCK  Lock;
                    int rc = PGMPhysGCPhys2CCPtrReadOnly(pVM, GCPhys, &pvPage, &Lock);
                    if (RT_SUCCESS(rc))
                    {
                        int32_t     offHit = offPage;
                        bool        fRc;
                        if (GCPhysAlign < PAGE_SIZE)
                        {
                            uint32_t cbSearch = (GCPhys ^ GCPhysLast) & ~(RTGCPHYS)PAGE_OFFSET_MASK
                                              ? PAGE_SIZE                           - (uint32_t)offPage
                                              : (GCPhysLast & PAGE_OFFSET_MASK) + 1 - (uint32_t)offPage;
                            fRc = pgmR3DbgScanPage((uint8_t const *)pvPage, &offHit, cbSearch, (uint32_t)GCPhysAlign,
                                                   pabNeedle, cbNeedle, &abPrev[0], &cbPrev);
                        }
                        else
                            fRc = memcmp(pvPage, pabNeedle, cbNeedle) == 0
                               && (GCPhysLast - GCPhys) >= cbNeedle;
                        PGMPhysReleasePageMappingLock(pVM, &Lock);
                        if (fRc)
                        {
                            *pGCPhysHit = GCPhys + offHit;
                            pgmUnlock(pVM);
                            return VINF_SUCCESS;
                        }
                    }
                    else
                        cbPrev = 0; /* ignore error. */
                }
                else
                    cbPrev = 0;

                /* advance to the next page. */
                GCPhys += (RTGCPHYS)cIncPages << PAGE_SHIFT;
                if (GCPhys >= GCPhysLast) /* (may not always hit, but we're run out of ranges.) */
                {
                    pgmUnlock(pVM);
                    return VERR_DBGF_MEM_NOT_FOUND;
                }
                iPage += cIncPages;
                if (    iPage < cIncPages
                    ||  iPage >= cPages)
                    break;
            }
        }
    }
    pgmUnlock(pVM);
    return VERR_DBGF_MEM_NOT_FOUND;
}


/**
 * Scans (guest) virtual memory for a byte string.
 *
 * @returns VBox status codes:
 * @retval  VINF_SUCCESS and *pGCPtrHit on success.
 * @retval  VERR_DBGF_MEM_NOT_FOUND if not found.
 * @retval  VERR_INVALID_POINTER if any of the pointer arguments are invalid.
 * @retval  VERR_INVALID_ARGUMENT if any other arguments are invalid.
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   pVCpu           The CPU context to search in.
 * @param   GCPtr           Where to start searching.
 * @param   GCPtrAlign      The alignment of the needle. Must be a power of two
 *                          and less or equal to 4GB.
 * @param   cbRange         The number of bytes to search. Max 256 bytes.
 * @param   pabNeedle       The byte string to search for.
 * @param   cbNeedle        The length of the byte string.
 * @param   pGCPtrHit       Where to store the address of the first occurence on success.
 */
VMMR3DECL(int) PGMR3DbgScanVirtual(PVM pVM, PVMCPU pVCpu, RTGCPTR GCPtr, RTGCPTR cbRange, RTGCPTR GCPtrAlign,
                                   const uint8_t *pabNeedle, size_t cbNeedle, PRTGCUINTPTR pGCPtrHit)
{
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Validate and adjust the input a bit.
     */
    if (!VALID_PTR(pGCPtrHit))
        return VERR_INVALID_POINTER;
    *pGCPtrHit = 0;

    if (!VALID_PTR(pabNeedle))
        return VERR_INVALID_POINTER;
    if (!cbNeedle)
        return VERR_INVALID_PARAMETER;
    if (cbNeedle > MAX_NEEDLE_SIZE)
        return VERR_INVALID_PARAMETER;

    if (!cbRange)
        return VERR_DBGF_MEM_NOT_FOUND;
    if (GCPtr + cbNeedle - 1 < GCPtr)
        return VERR_DBGF_MEM_NOT_FOUND;

    if (!GCPtrAlign)
        return VERR_INVALID_PARAMETER;
    if (GCPtrAlign > UINT32_MAX)
        return VERR_NOT_POWER_OF_TWO;
    if (GCPtrAlign & (GCPtrAlign - 1))
        return VERR_INVALID_PARAMETER;

    if (GCPtr & (GCPtrAlign - 1))
    {
        RTGCPTR Adj = GCPtrAlign - (GCPtr & (GCPtrAlign - 1));
        if (    cbRange <= Adj
            ||  GCPtr + Adj < GCPtr)
            return VERR_DBGF_MEM_NOT_FOUND;
        GCPtr   += Adj;
        cbRange -= Adj;
    }

    /*
     * Search the memory - ignore MMIO, zero and not-present pages.
     */
    const bool      fAllZero  = ASMMemIsAll8(pabNeedle, cbNeedle, 0) == NULL;
    PGMMODE         enmMode   = PGMGetGuestMode(pVCpu);
    RTGCPTR         GCPtrMask = PGMMODE_IS_LONG_MODE(enmMode) ? UINT64_MAX : UINT32_MAX;
    uint8_t         abPrev[MAX_NEEDLE_SIZE];
    size_t          cbPrev    = 0;
    const uint32_t  cIncPages = GCPtrAlign <= PAGE_SIZE
                              ? 1
                              : GCPtrAlign >> PAGE_SHIFT;
    const RTGCPTR   GCPtrLast = GCPtr + cbRange - 1 >= GCPtr
                              ? (GCPtr + cbRange - 1) & GCPtrMask
                              : GCPtrMask;
    RTGCPTR         cPages    = (((GCPtrLast - GCPtr) + (GCPtr & PAGE_OFFSET_MASK)) >> PAGE_SHIFT) + 1;
    uint32_t        offPage   = GCPtr & PAGE_OFFSET_MASK;
    GCPtr &= ~(RTGCPTR)PAGE_OFFSET_MASK;
    for (;; offPage = 0)
    {
        RTGCPHYS GCPhys;
        int rc = PGMPhysGCPtr2GCPhys(pVCpu, GCPtr, &GCPhys);
        if (RT_SUCCESS(rc))
        {
            PPGMPAGE pPage = pgmPhysGetPage(&pVM->pgm.s, GCPhys);
            if (    pPage
                &&  (   !PGM_PAGE_IS_ZERO(pPage)
                     || fAllZero)
                &&  !PGM_PAGE_IS_BALLOONED(pPage)
                &&  !PGM_PAGE_IS_MMIO(pPage))
            {
                void const *pvPage;
                PGMPAGEMAPLOCK Lock;
                rc = PGMPhysGCPhys2CCPtrReadOnly(pVM, GCPhys, &pvPage, &Lock);
                if (RT_SUCCESS(rc))
                {
                    int32_t offHit = offPage;
                    bool    fRc;
                    if (GCPtrAlign < PAGE_SIZE)
                    {
                        uint32_t cbSearch = cPages > 0
                                          ? PAGE_SIZE                          - (uint32_t)offPage
                                          : (GCPtrLast & PAGE_OFFSET_MASK) + 1 - (uint32_t)offPage;
                        fRc = pgmR3DbgScanPage((uint8_t const *)pvPage, &offHit, cbSearch, (uint32_t)GCPtrAlign,
                                               pabNeedle, cbNeedle, &abPrev[0], &cbPrev);
                    }
                    else
                        fRc = memcmp(pvPage, pabNeedle, cbNeedle) == 0
                           && (GCPtrLast - GCPtr) >= cbNeedle;
                    PGMPhysReleasePageMappingLock(pVM, &Lock);
                    if (fRc)
                    {
                        *pGCPtrHit = GCPtr + offHit;
                        return VINF_SUCCESS;
                    }
                }
                else
                    cbPrev = 0; /* ignore error. */
            }
            else
                cbPrev = 0;
        }
        else
            cbPrev = 0; /* ignore error. */

        /* advance to the next page. */
        if (cPages <= cIncPages)
            break;
        cPages -= cIncPages;
        GCPtr += (RTGCPTR)cIncPages << PAGE_SHIFT;
    }
    return VERR_DBGF_MEM_NOT_FOUND;
}

