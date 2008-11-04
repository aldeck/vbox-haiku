/* $Id$ */
/** @file
 * MM - Memory Manager - Hypervisor Memory Area.
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
#define LOG_GROUP LOG_GROUP_MM_HYPER
#include <VBox/pgm.h>
#include <VBox/mm.h>
#include <VBox/dbgf.h>
#include "MMInternal.h"
#include <VBox/vm.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/log.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/string.h>


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static DECLCALLBACK(bool) mmR3HyperRelocateCallback(PVM pVM, RTGCPTR GCPtrOld, RTGCPTR GCPtrNew, PGMRELOCATECALL enmMode, void *pvUser);
static int mmR3HyperMap(PVM pVM, const size_t cb, const char *pszDesc, PRTGCPTR pGCPtr, PMMLOOKUPHYPER *ppLookup);
static int mmR3HyperHeapCreate(PVM pVM, const size_t cb, PMMHYPERHEAP *ppHeap);
static int mmR3HyperHeapMap(PVM pVM, PMMHYPERHEAP pHeap, PRTGCPTR ppHeapGC);
static DECLCALLBACK(void) mmR3HyperInfoHma(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);




/**
 * Initializes the hypvervisor related MM stuff without
 * calling down to PGM.
 *
 * PGM is not initialized at this  point, PGM relies on
 * the heap to initialize.
 *
 * @returns VBox status.
 */
int mmR3HyperInit(PVM pVM)
{
    LogFlow(("mmR3HyperInit:\n"));

    /*
     * Decide Hypervisor mapping in the guest context
     * And setup various hypervisor area and heap parameters.
     */
    pVM->mm.s.pvHyperAreaGC = (RTGCPTR)MM_HYPER_AREA_ADDRESS;
    pVM->mm.s.cbHyperArea   = MM_HYPER_AREA_MAX_SIZE;
    AssertRelease(RT_ALIGN_T(pVM->mm.s.pvHyperAreaGC, 1 << X86_PD_SHIFT, RTGCPTR) == pVM->mm.s.pvHyperAreaGC);
    Assert(pVM->mm.s.pvHyperAreaGC < 0xff000000);

    /** @todo @bugref{1865}, @bugref{3202}: Change the cbHyperHeap default
     *        depending on whether VT-x/AMD-V is enabled or not! Don't waste
     *        precious kernel space on heap for the PATM. */
    uint32_t cbHyperHeap;
    int rc = CFGMR3QueryU32(CFGMR3GetChild(CFGMR3GetRoot(pVM), "MM"), "cbHyperHeap", &cbHyperHeap);
    if (rc == VERR_CFGM_NO_PARENT || rc == VERR_CFGM_VALUE_NOT_FOUND)
        cbHyperHeap = 1280*_1K;
    else if (RT_FAILURE(rc))
    {
        LogRel(("MM/cbHyperHeap query -> %Rrc\n", rc));
        AssertRCReturn(rc, rc);
    }
    cbHyperHeap = RT_ALIGN_32(cbHyperHeap, PAGE_SIZE);

    /*
     * Allocate the hypervisor heap.
     *
     * (This must be done before we start adding memory to the
     * hypervisor static area because lookup records are allocated from it.)
     */
    rc = mmR3HyperHeapCreate(pVM, cbHyperHeap, &pVM->mm.s.pHyperHeapR3);
    if (RT_SUCCESS(rc))
    {
        pVM->mm.s.pHyperHeapR0 = (uintptr_t)pVM->mm.s.pHyperHeapR3; /** @todo #1865: map into ring-0 / whatever. */

        /*
         * Make a small head fence to fend of accidental sequential access.
         */
        MMR3HyperReserve(pVM, PAGE_SIZE, "fence", NULL);

        /*
         * Map the VM structure into the hypervisor space.
         */
        AssertRelease(pVM->cbSelf == RT_UOFFSETOF(VM, aCpus[pVM->cCPUs]));
        RTGCPTR GCPtr;
        rc = MMR3HyperMapPages(pVM, pVM, pVM->pVMR0, RT_ALIGN_Z(pVM->cbSelf, PAGE_SIZE) >> PAGE_SHIFT, pVM->paVMPagesR3, "VM", &GCPtr);
        if (RT_SUCCESS(rc))
        {
            pVM->pVMRC = (RTRCPTR)GCPtr;
            pVM->pVMGC = pVM->pVMRC;
            for (uint32_t i = 0; i < pVM->cCPUs; i++)
                pVM->aCpus[i].pVMRC = pVM->pVMRC;

            /* Reserve a page for fencing. */
            MMR3HyperReserve(pVM, PAGE_SIZE, "fence", NULL);

            /*
             * Map the heap into the hypervisor space.
             */
            rc = mmR3HyperHeapMap(pVM, pVM->mm.s.pHyperHeapR3, &GCPtr);
            if (RT_SUCCESS(rc))
            {
                pVM->mm.s.pHyperHeapRC = (RTRCPTR)GCPtr;
                Assert(pVM->mm.s.pHyperHeapRC == GCPtr);

                /*
                 * Register info handlers.
                 */
                DBGFR3InfoRegisterInternal(pVM, "hma", "Show the layout of the Hypervisor Memory Area.", mmR3HyperInfoHma);

                LogFlow(("mmR3HyperInit: returns VINF_SUCCESS\n"));
                return VINF_SUCCESS;
            }
            /* Caller will do proper cleanup. */
        }
    }

    LogFlow(("mmR3HyperInit: returns %Rrc\n", rc));
    return rc;
}


/**
 * Finalizes the HMA mapping.
 *
 * This is called later during init, most (all) HMA allocations should be done
 * by the time this function is called.
 *
 * @returns VBox status.
 */
VMMR3DECL(int) MMR3HyperInitFinalize(PVM pVM)
{
    LogFlow(("MMR3HyperInitFinalize:\n"));

    /*
     * Adjust and create the HMA mapping.
     */
    while ((RTINT)pVM->mm.s.offHyperNextStatic + 64*_1K < (RTINT)pVM->mm.s.cbHyperArea - _4M)
        pVM->mm.s.cbHyperArea -= _4M;
    int rc = PGMR3MapPT(pVM, pVM->mm.s.pvHyperAreaGC, pVM->mm.s.cbHyperArea,
                        mmR3HyperRelocateCallback, NULL, "Hypervisor Memory Area");
    if (RT_FAILURE(rc))
        return rc;
    pVM->mm.s.fPGMInitialized = true;

    /*
     * Do all the delayed mappings.
     */
    PMMLOOKUPHYPER  pLookup = (PMMLOOKUPHYPER)((uintptr_t)pVM->mm.s.pHyperHeapR3 + pVM->mm.s.offLookupHyper);
    for (;;)
    {
        RTGCPTR     GCPtr = pVM->mm.s.pvHyperAreaGC + pLookup->off;
        unsigned    cPages = pLookup->cb >> PAGE_SHIFT;
        switch (pLookup->enmType)
        {
            case MMLOOKUPHYPERTYPE_LOCKED:
                rc = mmR3MapLocked(pVM, pLookup->u.Locked.pLockedMem, GCPtr, 0, cPages, 0);
                break;

            case MMLOOKUPHYPERTYPE_HCPHYS:
                rc = PGMMap(pVM, GCPtr, pLookup->u.HCPhys.HCPhys, pLookup->cb, 0);
                break;

            case MMLOOKUPHYPERTYPE_GCPHYS:
            {
                const RTGCPHYS  GCPhys = pLookup->u.GCPhys.GCPhys;
                const size_t    cb = pLookup->cb;
                for (unsigned off = 0; off < cb; off += PAGE_SIZE)
                {
                    RTHCPHYS HCPhys;
                    rc = PGMPhysGCPhys2HCPhys(pVM, GCPhys + off, &HCPhys);
                    if (RT_FAILURE(rc))
                        break;
                    rc = PGMMap(pVM, GCPtr + off, HCPhys, PAGE_SIZE, 0);
                    if (RT_FAILURE(rc))
                        break;
                }
                break;
            }

            case MMLOOKUPHYPERTYPE_MMIO2:
            {
                const RTGCPHYS offEnd = pLookup->u.MMIO2.off + pLookup->cb;
                for (RTGCPHYS offCur = pLookup->u.MMIO2.off; offCur < offEnd; offCur += PAGE_SIZE)
                {
                    RTHCPHYS HCPhys;
                    rc = PGMR3PhysMMIO2GetHCPhys(pVM, pLookup->u.MMIO2.pDevIns, pLookup->u.MMIO2.iRegion, offCur, &HCPhys);
                    if (RT_FAILURE(rc))
                        break;
                    rc = PGMMap(pVM, GCPtr + (offCur - pLookup->u.MMIO2.off), HCPhys, PAGE_SIZE, 0);
                    if (RT_FAILURE(rc))
                        break;
                }
                break;
            }

            case MMLOOKUPHYPERTYPE_DYNAMIC:
                /* do nothing here since these are either fences or managed by someone else using PGM. */
                break;

            default:
                AssertMsgFailed(("enmType=%d\n", pLookup->enmType));
                break;
        }

        if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("rc=%Rrc cb=%d GCPtr=%VGv enmType=%d pszDesc=%s\n",
                             rc, pLookup->cb, pLookup->enmType, pLookup->pszDesc));
            return rc;
        }

        /* next */
        if (pLookup->offNext == (int32_t)NIL_OFFSET)
            break;
        pLookup = (PMMLOOKUPHYPER)((uintptr_t)pLookup + pLookup->offNext);
    }

    LogFlow(("MMR3HyperInitFinalize: returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}


/**
 * Callback function which will be called when PGM is trying to find
 * a new location for the mapping.
 *
 * The callback is called in two modes, 1) the check mode and 2) the relocate mode.
 * In 1) the callback should say if it objects to a suggested new location. If it
 * accepts the new location, it is called again for doing it's relocation.
 *
 *
 * @returns true if the location is ok.
 * @returns false if another location should be found.
 * @param   pVM         The VM handle.
 * @param   GCPtrOld    The old virtual address.
 * @param   GCPtrNew    The new virtual address.
 * @param   enmMode     Used to indicate the callback mode.
 * @param   pvUser      User argument. Ignored.
 * @remark  The return value is no a failure indicator, it's an acceptance
 *          indicator. Relocation can not fail!
 */
static DECLCALLBACK(bool) mmR3HyperRelocateCallback(PVM pVM, RTGCPTR GCPtrOld, RTGCPTR GCPtrNew, PGMRELOCATECALL enmMode, void *pvUser)
{
    switch (enmMode)
    {
        /*
         * Verify location - all locations are good for us.
         */
        case PGMRELOCATECALL_SUGGEST:
            return true;

        /*
         * Execute the relocation.
         */
        case PGMRELOCATECALL_RELOCATE:
        {
            /*
             * Accepted!
             */
            AssertMsg(GCPtrOld == pVM->mm.s.pvHyperAreaGC, ("GCPtrOld=%VGv pVM->mm.s.pvHyperAreaGC=%VGv\n", GCPtrOld, pVM->mm.s.pvHyperAreaGC));
            Log(("Relocating the hypervisor from %VGv to %VGv\n", GCPtrOld, GCPtrNew));

            /*
             * Relocate the VM structure and ourselves.
             */
            RTGCINTPTR      offDelta = GCPtrNew - GCPtrOld;
            pVM->pVMRC                          += offDelta;
            pVM->pVMGC                          = pVM->pVMRC;
            for (uint32_t i = 0; i < pVM->cCPUs; i++)
                pVM->aCpus[i].pVMRC             = pVM->pVMRC;

            pVM->mm.s.pvHyperAreaGC             += offDelta;
            Assert(pVM->mm.s.pvHyperAreaGC < _4G);
            pVM->mm.s.pHyperHeapRC              += offDelta;
            pVM->mm.s.pHyperHeapR3->pbHeapRC    += offDelta;
            pVM->mm.s.pHyperHeapR3->pVMRC        = pVM->pVMRC;

            /*
             * Relocate the rest.
             */
            VMR3Relocate(pVM, offDelta);
            return true;
        }

        default:
            AssertMsgFailed(("Invalid relocation mode %d\n", enmMode));
    }

    return false;
}


/**
 * Maps contiguous HC physical memory into the hypervisor region in the GC.
 *
 * @return VBox status code.
 *
 * @param   pVM         VM handle.
 * @param   pvR3        Host context address of the memory. Must be page
 *                      aligned!
 * @param   HCPhys      Host context physical address of the memory to be
 *                      mapped. Must be page aligned!
 * @param   cb          Size of the memory. Will be rounded up to nearest page.
 * @param   pszDesc     Description.
 * @param   pGCPtr      Where to store the GC address.
 */
VMMR3DECL(int) MMR3HyperMapHCPhys(PVM pVM, void *pvR3, RTHCPHYS HCPhys, size_t cb, const char *pszDesc, PRTGCPTR pGCPtr)
{
    LogFlow(("MMR3HyperMapHCPhys: pvR3=%p HCPhys=%RHp cb=%d pszDesc=%p:{%s} pGCPtr=%p\n", pvR3, HCPhys, (int)cb, pszDesc, pszDesc, pGCPtr));

    /*
     * Validate input.
     */
    AssertReturn(RT_ALIGN_P(pvR3, PAGE_SIZE) == pvR3, VERR_INVALID_PARAMETER);
    AssertReturn(RT_ALIGN_T(HCPhys, PAGE_SIZE, RTHCPHYS) == HCPhys, VERR_INVALID_PARAMETER);
    AssertReturn(pszDesc && *pszDesc, VERR_INVALID_PARAMETER);

    /*
     * Add the memory to the hypervisor area.
     */
    uint32_t cbAligned = RT_ALIGN_32(cb, PAGE_SIZE);
    AssertReturn(cbAligned >= cb, VERR_INVALID_PARAMETER);
    RTGCPTR         GCPtr;
    PMMLOOKUPHYPER  pLookup;
    int rc = mmR3HyperMap(pVM, cbAligned, pszDesc, &GCPtr, &pLookup);
    if (RT_SUCCESS(rc))
    {
        pLookup->enmType = MMLOOKUPHYPERTYPE_HCPHYS;
        pLookup->u.HCPhys.pvR3   = pvR3;
        pLookup->u.HCPhys.HCPhys = HCPhys;

        /*
         * Update the page table.
         */
        if (pVM->mm.s.fPGMInitialized)
            rc = PGMMap(pVM, GCPtr, HCPhys, cbAligned, 0);
        if (RT_SUCCESS(rc))
            *pGCPtr = GCPtr;
    }
    return rc;
}


/**
 * Maps contiguous GC physical memory into the hypervisor region in the GC.
 *
 * @return VBox status code.
 *
 * @param   pVM         VM handle.
 * @param   GCPhys      Guest context physical address of the memory to be mapped. Must be page aligned!
 * @param   cb          Size of the memory. Will be rounded up to nearest page.
 * @param   pszDesc     Mapping description.
 * @param   pGCPtr      Where to store the GC address.
 */
VMMR3DECL(int) MMR3HyperMapGCPhys(PVM pVM, RTGCPHYS GCPhys, size_t cb, const char *pszDesc, PRTGCPTR pGCPtr)
{
    LogFlow(("MMR3HyperMapGCPhys: GCPhys=%VGp cb=%d pszDesc=%p:{%s} pGCPtr=%p\n", GCPhys, (int)cb, pszDesc, pszDesc, pGCPtr));

    /*
     * Validate input.
     */
    AssertReturn(RT_ALIGN_T(GCPhys, PAGE_SIZE, RTGCPHYS) == GCPhys, VERR_INVALID_PARAMETER);
    AssertReturn(pszDesc && *pszDesc, VERR_INVALID_PARAMETER);

    /*
     * Add the memory to the hypervisor area.
     */
    cb = RT_ALIGN_Z(cb, PAGE_SIZE);
    RTGCPTR         GCPtr;
    PMMLOOKUPHYPER  pLookup;
    int rc = mmR3HyperMap(pVM, cb, pszDesc, &GCPtr, &pLookup);
    if (RT_SUCCESS(rc))
    {
        pLookup->enmType = MMLOOKUPHYPERTYPE_GCPHYS;
        pLookup->u.GCPhys.GCPhys = GCPhys;

        /*
         * Update the page table.
         */
        for (unsigned off = 0; off < cb; off += PAGE_SIZE)
        {
            RTHCPHYS HCPhys;
            rc = PGMPhysGCPhys2HCPhys(pVM, GCPhys + off, &HCPhys);
            AssertRC(rc);
            if (RT_FAILURE(rc))
            {
                AssertMsgFailed(("rc=%Rrc GCPhys=%VGv off=%#x %s\n", rc, GCPhys, off, pszDesc));
                break;
            }
            if (pVM->mm.s.fPGMInitialized)
            {
                rc = PGMMap(pVM, GCPtr + off, HCPhys, PAGE_SIZE, 0);
                AssertRC(rc);
                if (RT_FAILURE(rc))
                {
                    AssertMsgFailed(("rc=%Rrc GCPhys=%VGv off=%#x %s\n", rc, GCPhys, off, pszDesc));
                    break;
                }
            }
        }

        if (RT_SUCCESS(rc) && pGCPtr)
            *pGCPtr = GCPtr;
    }
    return rc;
}


/**
 * Maps a portion of an MMIO2 region into the hypervisor region.
 *
 * Callers of this API must never deregister the MMIO2 region before the
 * VM is powered off. If this becomes a requirement MMR3HyperUnmapMMIO2
 * API will be needed to perform cleanups.
 *
 * @return VBox status code.
 *
 * @param   pVM         Pointer to the shared VM structure.
 * @param   pDevIns     The device owning the MMIO2 memory.
 * @param   iRegion     The region.
 * @param   off         The offset into the region. Will be rounded down to closest page boundrary.
 * @param   cb          The number of bytes to map. Will be rounded up to the closest page boundrary.
 * @param   pszDesc     Mapping description.
 * @param   pRCPtr      Where to store the RC address.
 */
VMMR3DECL(int) MMR3HyperMapMMIO2(PVM pVM, PPDMDEVINS pDevIns, uint32_t iRegion, RTGCPHYS off, RTGCPHYS cb,
                                const char *pszDesc, PRTRCPTR pRCPtr)
{
    LogFlow(("MMR3HyperMapMMIO2: pDevIns=%p iRegion=%#x off=%VGp cb=%VGp pszDesc=%p:{%s} pRCPtr=%p\n",
             pDevIns, iRegion, off, cb, pszDesc, pszDesc, pRCPtr));
    int rc;

    /*
     * Validate input.
     */
    AssertReturn(pszDesc && *pszDesc, VERR_INVALID_PARAMETER);
    AssertReturn(off + cb > off, VERR_INVALID_PARAMETER);
    uint32_t const offPage = off & PAGE_OFFSET_MASK;
    off &= ~(RTGCPHYS)PAGE_OFFSET_MASK;
    cb += offPage;
    cb = RT_ALIGN_Z(cb, PAGE_SIZE);
    const RTGCPHYS offEnd = off + cb;
    AssertReturn(offEnd > off, VERR_INVALID_PARAMETER);
    for (RTGCPHYS offCur = off; offCur < offEnd; offCur += PAGE_SIZE)
    {
        RTHCPHYS HCPhys;
        rc = PGMR3PhysMMIO2GetHCPhys(pVM, pDevIns, iRegion, offCur, &HCPhys);
        AssertMsgRCReturn(rc, ("rc=%Rrc - iRegion=%d off=%RGp\n", rc, iRegion, off), rc);
    }

    /*
     * Add the memory to the hypervisor area.
     */
    RTGCPTR         GCPtr;
    PMMLOOKUPHYPER  pLookup;
    rc = mmR3HyperMap(pVM, cb, pszDesc, &GCPtr, &pLookup);
    if (RT_SUCCESS(rc))
    {
        pLookup->enmType = MMLOOKUPHYPERTYPE_MMIO2;
        pLookup->u.MMIO2.pDevIns = pDevIns;
        pLookup->u.MMIO2.iRegion = iRegion;
        pLookup->u.MMIO2.off = off;

        /*
         * Update the page table.
         */
        if (pVM->mm.s.fPGMInitialized)
        {
            for (RTGCPHYS offCur = off; offCur < offEnd; offCur += PAGE_SIZE)
            {
                RTHCPHYS HCPhys;
                rc = PGMR3PhysMMIO2GetHCPhys(pVM, pDevIns, iRegion, offCur, &HCPhys);
                AssertRCReturn(rc, VERR_INTERNAL_ERROR);
                rc = PGMMap(pVM, GCPtr + (offCur - off), HCPhys, PAGE_SIZE, 0);
                if (RT_FAILURE(rc))
                {
                    AssertMsgFailed(("rc=%Rrc offCur=%RGp %s\n", rc, offCur, pszDesc));
                    break;
                }
            }
        }

        if (RT_SUCCESS(rc))
        {
            GCPtr |= offPage;
            *pRCPtr = GCPtr;
            AssertLogRelReturn(*pRCPtr == GCPtr, VERR_INTERNAL_ERROR);
        }
    }
    return rc;
}


/**
 * Locks and Maps HC virtual memory into the hypervisor region in the GC.
 *
 * @return VBox status code.
 *
 * @param   pVM         VM handle.
 * @param   pvR3        Host context address of the memory (may be not page
 *                      aligned).
 * @param   cb          Size of the memory. Will be rounded up to nearest page.
 * @param   fFree       Set this if MM is responsible for freeing the memory
 *                      using SUPPageFree.
 * @param   pszDesc     Mapping description.
 * @param   pGCPtr      Where to store the GC address corresponding to pvR3.
 */
VMMR3DECL(int) MMR3HyperMapHCRam(PVM pVM, void *pvR3, size_t cb, bool fFree, const char *pszDesc, PRTGCPTR pGCPtr)
{
    LogFlow(("MMR3HyperMapHCRam: pvR3=%p cb=%d fFree=%d pszDesc=%p:{%s} pGCPtr=%p\n", pvR3, (int)cb, fFree, pszDesc, pszDesc, pGCPtr));

    /*
     * Validate input.
     */
    if (    !pvR3
        ||  cb <= 0
        ||  !pszDesc
        ||  !*pszDesc)
    {
        AssertMsgFailed(("Invalid parameter\n"));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Page align address and size.
     */
    void *pvR3Page = (void *)((uintptr_t)pvR3 & PAGE_BASE_HC_MASK);
    cb += (uintptr_t)pvR3 & PAGE_OFFSET_MASK;
    cb = RT_ALIGN_Z(cb, PAGE_SIZE);

    /*
     * Add the memory to the hypervisor area.
     */
    RTGCPTR         GCPtr;
    PMMLOOKUPHYPER  pLookup;
    int rc = mmR3HyperMap(pVM, cb, pszDesc, &GCPtr, &pLookup);
    if (RT_SUCCESS(rc))
    {
        /*
         * Lock the heap memory and tell PGM about the locked pages.
         */
        PMMLOCKEDMEM    pLockedMem;
        rc = mmR3LockMem(pVM, pvR3Page, cb, fFree ? MM_LOCKED_TYPE_HYPER : MM_LOCKED_TYPE_HYPER_NOFREE, &pLockedMem, false /* fSilentFailure */);
        if (RT_SUCCESS(rc))
        {
            /* map the stuff into guest address space. */
            if (pVM->mm.s.fPGMInitialized)
                rc = mmR3MapLocked(pVM, pLockedMem, GCPtr, 0, ~(size_t)0, 0);
            if (RT_SUCCESS(rc))
            {
                pLookup->enmType = MMLOOKUPHYPERTYPE_LOCKED;
                pLookup->u.Locked.pvR3       = pvR3;
                pLookup->u.Locked.pvR0       = NIL_RTR0PTR;
                pLookup->u.Locked.pLockedMem = pLockedMem;

                /* done. */
                GCPtr    |= (uintptr_t)pvR3 & PAGE_OFFSET_MASK;
                *pGCPtr   = GCPtr;
                return rc;
            }
            /* Don't care about failure clean, we're screwed if this fails anyway. */
        }
    }

    return rc;
}


/**
 * Maps locked R3 virtual memory into the hypervisor region in the GC.
 *
 * @return VBox status code.
 *
 * @param   pVM         VM handle.
 * @param   pvR3        The ring-3 address of the memory, must be page aligned.
 * @param   pvR0        The ring-0 address of the memory, must be page aligned. (optional)
 * @param   cPages      The number of pages.
 * @param   paPages     The page descriptors.
 * @param   pszDesc     Mapping description.
 * @param   pGCPtr      Where to store the GC address corresponding to pvR3.
 */
VMMR3DECL(int) MMR3HyperMapPages(PVM pVM, void *pvR3, RTR0PTR pvR0, size_t cPages, PCSUPPAGE paPages, const char *pszDesc, PRTGCPTR pGCPtr)
{
    LogFlow(("MMR3HyperMapPages: pvR3=%p pvR0=%p cPages=%zu paPages=%p pszDesc=%p:{%s} pGCPtr=%p\n",
             pvR3, pvR0, cPages, paPages, pszDesc, pszDesc, pGCPtr));

    /*
     * Validate input.
     */
    AssertPtrReturn(pvR3, VERR_INVALID_POINTER);
    AssertPtrReturn(paPages, VERR_INVALID_POINTER);
    AssertReturn(cPages > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cPages < 1024, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszDesc, VERR_INVALID_POINTER);
    AssertReturn(*pszDesc, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pGCPtr, VERR_INVALID_PARAMETER);

    /*
     * Add the memory to the hypervisor area.
     */
    RTGCPTR         GCPtr;
    PMMLOOKUPHYPER  pLookup;
    int rc = mmR3HyperMap(pVM, cPages << PAGE_SHIFT, pszDesc, &GCPtr, &pLookup);
    if (RT_SUCCESS(rc))
    {
        /*
         * Create a locked memory record and tell PGM about this.
         */
        PMMLOCKEDMEM pLockedMem = (PMMLOCKEDMEM)MMR3HeapAlloc(pVM, MM_TAG_MM, RT_OFFSETOF(MMLOCKEDMEM, aPhysPages[cPages]));
        if (pLockedMem)
        {
            pLockedMem->pv      = pvR3;
            pLockedMem->cb      = cPages << PAGE_SHIFT;
            pLockedMem->eType   = MM_LOCKED_TYPE_HYPER_PAGES;
            memset(&pLockedMem->u, 0, sizeof(pLockedMem->u));
            for (size_t i = 0; i < cPages; i++)
            {
                AssertReleaseReturn(paPages[i].Phys != 0 && paPages[i].Phys != NIL_RTHCPHYS && !(paPages[i].Phys & PAGE_OFFSET_MASK), VERR_INTERNAL_ERROR);
                pLockedMem->aPhysPages[i].Phys = paPages[i].Phys;
                pLockedMem->aPhysPages[i].uReserved = (RTHCUINTPTR)pLockedMem;
            }

            /* map the stuff into guest address space. */
            if (pVM->mm.s.fPGMInitialized)
                rc = mmR3MapLocked(pVM, pLockedMem, GCPtr, 0, ~(size_t)0, 0);
            if (RT_SUCCESS(rc))
            {
                pLookup->enmType = MMLOOKUPHYPERTYPE_LOCKED;
                pLookup->u.Locked.pvR3       = pvR3;
                pLookup->u.Locked.pvR0       = pvR0;
                pLookup->u.Locked.pLockedMem = pLockedMem;

                /* done. */
                *pGCPtr   = GCPtr;
                return rc;
            }
            /* Don't care about failure clean, we're screwed if this fails anyway. */
        }
    }

    return rc;
}


/**
 * Reserves a hypervisor memory area.
 * Most frequent usage is fence pages and dynamically mappings like the guest PD and PDPT.
 *
 * @return VBox status code.
 *
 * @param   pVM         VM handle.
 * @param   cb          Size of the memory. Will be rounded up to nearest page.
 * @param   pszDesc     Mapping description.
 * @param   pGCPtr      Where to store the assigned GC address. Optional.
 */
VMMR3DECL(int) MMR3HyperReserve(PVM pVM, unsigned cb, const char *pszDesc, PRTGCPTR pGCPtr)
{
    LogFlow(("MMR3HyperMapHCRam: cb=%d pszDesc=%p:{%s} pGCPtr=%p\n", (int)cb, pszDesc, pszDesc, pGCPtr));

    /*
     * Validate input.
     */
    if (    cb <= 0
        ||  !pszDesc
        ||  !*pszDesc)
    {
        AssertMsgFailed(("Invalid parameter\n"));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Add the memory to the hypervisor area.
     */
    RTGCPTR         GCPtr;
    PMMLOOKUPHYPER  pLookup;
    int rc = mmR3HyperMap(pVM, cb, pszDesc, &GCPtr, &pLookup);
    if (RT_SUCCESS(rc))
    {
        pLookup->enmType = MMLOOKUPHYPERTYPE_DYNAMIC;
        if (pGCPtr)
            *pGCPtr = GCPtr;
        return VINF_SUCCESS;
    }
    return rc;
}


/**
 * Adds memory to the hypervisor memory arena.
 *
 * @return VBox status code.
 * @param   pVM         The VM handle.
 * @param   cb          Size of the memory. Will be rounded up to neares page.
 * @param   pszDesc     The description of the memory.
 * @param   pGCPtr      Where to store the GC address.
 * @param   ppLookup    Where to store the pointer to the lookup record.
 * @remark  We assume the threading structure of VBox imposes natural
 *          serialization of most functions, this one included.
 */
static int mmR3HyperMap(PVM pVM, const size_t cb, const char *pszDesc, PRTGCPTR pGCPtr, PMMLOOKUPHYPER *ppLookup)
{
    /*
     * Validate input.
     */
    const uint32_t cbAligned = RT_ALIGN(cb, PAGE_SIZE);
    AssertReturn(cbAligned >= cb, VERR_INVALID_PARAMETER);
    if (pVM->mm.s.offHyperNextStatic + cbAligned >= pVM->mm.s.cbHyperArea) /* don't use the last page, it's a fence. */
    {
        AssertMsgFailed(("Out of static mapping space in the HMA! offHyperAreaGC=%x cbAligned=%x\n",
                         pVM->mm.s.offHyperNextStatic, cbAligned));
        return VERR_NO_MEMORY;
    }

    /*
     * Allocate lookup record.
     */
    PMMLOOKUPHYPER  pLookup;
    int rc = MMHyperAlloc(pVM, sizeof(*pLookup), 1, MM_TAG_MM, (void **)&pLookup);
    if (RT_SUCCESS(rc))
    {
        /*
         * Initialize it and insert it.
         */
        pLookup->offNext        = pVM->mm.s.offLookupHyper;
        pLookup->cb             = cbAligned;
        pLookup->off            = pVM->mm.s.offHyperNextStatic;
        pVM->mm.s.offLookupHyper = (uint8_t *)pLookup - (uint8_t *)pVM->mm.s.pHyperHeapR3;
        if (pLookup->offNext != (int32_t)NIL_OFFSET)
            pLookup->offNext   -= pVM->mm.s.offLookupHyper;
        pLookup->enmType        = MMLOOKUPHYPERTYPE_INVALID;
        memset(&pLookup->u, 0xff, sizeof(pLookup->u));
        pLookup->pszDesc        = pszDesc;

        /* Mapping. */
        *pGCPtr = pVM->mm.s.pvHyperAreaGC + pVM->mm.s.offHyperNextStatic;
        pVM->mm.s.offHyperNextStatic += cbAligned;

        /* Return pointer. */
        *ppLookup = pLookup;
    }

    AssertRC(rc);
    LogFlow(("mmR3HyperMap: returns %Rrc *pGCPtr=%VGv\n", rc, *pGCPtr));
    return rc;
}


/**
 * Allocates a new heap.
 *
 * @returns VBox status code.
 * @param   pVM     The VM handle.
 * @param   cb      The size of the new heap.
 * @param   ppHeap  Where to store the heap pointer on successful return.
 */
static int mmR3HyperHeapCreate(PVM pVM, const size_t cb, PMMHYPERHEAP *ppHeap)
{
    /*
     * Allocate the hypervisor heap.
     */
    const uint32_t cbAligned = RT_ALIGN_Z(cb, PAGE_SIZE);
    AssertReturn(cbAligned >= cb, VERR_INVALID_PARAMETER);
    void *pv;
    int rc = SUPPageAlloc(cbAligned >> PAGE_SHIFT, &pv); /** @todo #1865: heap allocation must be changed for osx (only). */
    if (RT_SUCCESS(rc))
    {
        /*
         * Initialize the heap and first free chunk.
         */
        PMMHYPERHEAP pHeap = (PMMHYPERHEAP)pv;
        pHeap->u32Magic             = MMHYPERHEAP_MAGIC;
        pHeap->pbHeapR3             = (uint8_t *)pHeap + MMYPERHEAP_HDR_SIZE;
        pHeap->pbHeapR0             = (uintptr_t)pHeap->pbHeapR3; /** @todo #1865: Map heap into ring-0 on darwin. */
        //pHeap->pbHeapGC           = 0; // set by mmR3HyperHeapMap()
        pHeap->pVMR3                = pVM;
        pHeap->pVMR0                = pVM->pVMR0;
        pHeap->pVMRC                = pVM->pVMRC;
        pHeap->cbHeap               = cbAligned - MMYPERHEAP_HDR_SIZE;
        pHeap->cbFree               = pHeap->cbHeap - sizeof(MMHYPERCHUNK);
        //pHeap->offFreeHead        = 0;
        //pHeap->offFreeTail        = 0;
        pHeap->offPageAligned       = pHeap->cbHeap;
        //pHeap->HyperHeapStatTree  = 0;

        PMMHYPERCHUNKFREE pFree = (PMMHYPERCHUNKFREE)pHeap->pbHeapR3;
        pFree->cb                   = pHeap->cbFree;
        //pFree->core.offNext       = 0;
        MMHYPERCHUNK_SET_TYPE(&pFree->core, MMHYPERCHUNK_FLAGS_FREE);
        pFree->core.offHeap         = -(int32_t)MMYPERHEAP_HDR_SIZE;
        //pFree->offNext            = 0;
        //pFree->offPrev            = 0;

        STAMR3Register(pVM, &pHeap->cbHeap, STAMTYPE_U32, STAMVISIBILITY_ALWAYS, "/MM/HyperHeap/cbHeap",  STAMUNIT_BYTES, "The heap size.");
        STAMR3Register(pVM, &pHeap->cbFree, STAMTYPE_U32, STAMVISIBILITY_ALWAYS, "/MM/HyperHeap/cbFree",  STAMUNIT_BYTES, "The free space.");

        *ppHeap = pHeap;
        return VINF_SUCCESS;
    }
    AssertMsgFailed(("SUPPageAlloc(%d,) -> %Rrc\n", cbAligned >> PAGE_SHIFT, rc));

    *ppHeap = NULL;
    return rc;
}


/**
 * Allocates a new heap.
 */
static int mmR3HyperHeapMap(PVM pVM, PMMHYPERHEAP pHeap, PRTGCPTR ppHeapGC)
{
    int rc = MMR3HyperMapHCRam(pVM, pHeap, pHeap->cbHeap + MMYPERHEAP_HDR_SIZE, true, "Heap", ppHeapGC);
    if (RT_SUCCESS(rc))
    {
        pHeap->pVMRC    = pVM->pVMRC;
        pHeap->pbHeapRC = *ppHeapGC + MMYPERHEAP_HDR_SIZE;
        /* Reserve a page for fencing. */
        MMR3HyperReserve(pVM, PAGE_SIZE, "fence", NULL);
    }
    return rc;
}


#if 0
/**
 * Destroys a heap.
 */
static int mmR3HyperHeapDestroy(PVM pVM, PMMHYPERHEAP pHeap)
{
    /* all this is dealt with when unlocking and freeing locked memory. */
}
#endif


/**
 * Allocates memory in the Hypervisor (GC VMM) area which never will
 * be freed and doesn't have any offset based relation to other heap blocks.
 *
 * The latter means that two blocks allocated by this API will not have the
 * same relative position to each other in GC and HC. In short, never use
 * this API for allocating nodes for an offset based AVL tree!
 *
 * The returned memory is of course zeroed.
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 * @param   cb          Number of bytes to allocate.
 * @param   uAlignment  Required memory alignment in bytes.
 *                      Values are 0,8,16,32 and PAGE_SIZE.
 *                      0 -> default alignment, i.e. 8 bytes.
 * @param   enmTag      The statistics tag.
 * @param   ppv         Where to store the address to the allocated
 *                      memory.
 * @remark  This is assumed not to be used at times when serialization is required.
 */
VMMDECL(int) MMR3HyperAllocOnceNoRel(PVM pVM, size_t cb, unsigned uAlignment, MMTAG enmTag, void **ppv)
{
    AssertMsg(cb >= 8, ("Hey! Do you really mean to allocate less than 8 bytes?! cb=%d\n", cb));

    /*
     * Choose between allocating a new chunk of HMA memory
     * and the heap. We will only do BIG allocations from HMA.
     */
    if (    cb < _64K
        &&  (   uAlignment != PAGE_SIZE
             || cb < 48*_1K))
    {
        int rc = MMHyperAlloc(pVM, cb, uAlignment, enmTag, ppv);
        if (    rc != VERR_MM_HYPER_NO_MEMORY
            ||  cb <= 8*_1K)
        {
            Log2(("MMR3HyperAllocOnceNoRel: cb=%#zx uAlignment=%#x returns %Rrc and *ppv=%p\n",
                  cb, uAlignment, rc, *ppv));
            return rc;
        }
    }

    /*
     * Validate alignment.
     */
    switch (uAlignment)
    {
        case 0:
        case 8:
        case 16:
        case 32:
        case PAGE_SIZE:
            break;
        default:
            AssertMsgFailed(("Invalid alignment %u\n", uAlignment));
            return VERR_INVALID_PARAMETER;
    }

    /*
     * Allocate the pages and the HMA space.
     */
    cb = RT_ALIGN(cb, PAGE_SIZE);
    void *pvPages;
    int rc = SUPPageAlloc(cb >> PAGE_SHIFT, &pvPages);
    if (RT_SUCCESS(rc))
    {
        RTGCPTR GCPtr;
        rc = MMR3HyperMapHCRam(pVM, pvPages, cb, true,
                               MMR3HeapAPrintf(pVM, MM_TAG_MM, "alloc once (%s)", mmR3GetTagName(enmTag)),
                               &GCPtr);
        if (RT_SUCCESS(rc))
        {
            *ppv = pvPages;
            Log2(("MMR3HyperAllocOnceNoRel: cb=%#x uAlignment=%#x returns VINF_SUCCESS and *ppv=%p\n",
                  cb, uAlignment, *ppv));
            return rc;
        }
        AssertMsgFailed(("Failed to allocate %zd bytes! %Rrc\n", cb, rc));
        SUPPageFree(pvPages, cb >> PAGE_SHIFT);

        /*
         * HACK ALERT! Try allocate it off the heap so that we don't freak
         * out during vga/vmmdev mmio2 allocation with certain ram sizes.
         */
        /** @todo make a proper fix for this so we will never end up in this kind of situation! */
        Log(("MMR3HyperAllocOnceNoRel: MMR3HyperMapHCRam failed with rc=%Rrc, try MMHyperAlloc(,%#d,,) instead\n",  rc, cb));
        int rc2 = MMHyperAlloc(pVM, cb, uAlignment, enmTag, ppv);
        if (RT_SUCCESS(rc2))
        {
            Log2(("MMR3HyperAllocOnceNoRel: cb=%#x uAlignment=%#x returns %Rrc and *ppv=%p\n",
                  cb, uAlignment, rc, *ppv));
            return rc;
        }
    }
    else
        AssertMsgFailed(("Failed to allocate %zd bytes! %Rrc\n", cb, rc));

    if (rc == VERR_NO_MEMORY)
        rc = VERR_MM_HYPER_NO_MEMORY;
    LogRel(("MMR3HyperAllocOnceNoRel: cb=%#zx uAlignment=%#x returns %Rrc\n", cb, uAlignment, rc));
    return rc;
}


/**
 * Convert hypervisor HC virtual address to HC physical address.
 *
 * @returns HC physical address.
 * @param   pVM         VM Handle
 * @param   pvR3        Host context virtual address.
 */
VMMR3DECL(RTHCPHYS) MMR3HyperHCVirt2HCPhys(PVM pVM, void *pvR3)
{
    PMMLOOKUPHYPER  pLookup = (PMMLOOKUPHYPER)((uint8_t *)pVM->mm.s.pHyperHeapR3 + pVM->mm.s.offLookupHyper);
    for (;;)
    {
        switch (pLookup->enmType)
        {
            case MMLOOKUPHYPERTYPE_LOCKED:
            {
                unsigned off = (uint8_t *)pvR3 - (uint8_t *)pLookup->u.Locked.pvR3;
                if (off < pLookup->cb)
                    return (pLookup->u.Locked.pLockedMem->aPhysPages[off >> PAGE_SHIFT].Phys & X86_PTE_PAE_PG_MASK) | (off & PAGE_OFFSET_MASK);
                break;
            }

            case MMLOOKUPHYPERTYPE_HCPHYS:
            {
                unsigned off = (uint8_t *)pvR3 - (uint8_t *)pLookup->u.HCPhys.pvR3;
                if (off < pLookup->cb)
                    return pLookup->u.HCPhys.HCPhys + off;
                break;
            }

            case MMLOOKUPHYPERTYPE_GCPHYS:
            case MMLOOKUPHYPERTYPE_MMIO2:
            case MMLOOKUPHYPERTYPE_DYNAMIC:
                /* can (or don't want to) convert these kind of records. */
                break;

            default:
                AssertMsgFailed(("enmType=%d\n", pLookup->enmType));
                break;
        }

        /* next */
        if ((unsigned)pLookup->offNext == NIL_OFFSET)
            break;
        pLookup = (PMMLOOKUPHYPER)((uint8_t *)pLookup + pLookup->offNext);
    }

    AssertMsgFailed(("pvR3=%p is not inside the hypervisor memory area!\n", pvR3));
    return NIL_RTHCPHYS;
}


#if 0 /* unused, not implemented */
/**
 * Convert hypervisor HC physical address to HC virtual address.
 *
 * @returns HC virtual address.
 * @param   pVM         VM Handle
 * @param   HCPhys      Host context physical address.
 */
VMMR3DECL(void *) MMR3HyperHCPhys2HCVirt(PVM pVM, RTHCPHYS HCPhys)
{
    void *pv;
    int rc = MMR3HyperHCPhys2HCVirtEx(pVM, HCPhys, &pv);
    if (RT_SUCCESS(rc))
        return pv;
    AssertMsgFailed(("Invalid address HCPhys=%x rc=%d\n", HCPhys, rc));
    return NULL;
}


/**
 * Convert hypervisor HC physical address to HC virtual address.
 *
 * @returns VBox status.
 * @param   pVM         VM Handle
 * @param   HCPhys      Host context physical address.
 * @param   ppv         Where to store the HC virtual address.
 */
VMMR3DECL(int)   MMR3HyperHCPhys2HCVirtEx(PVM pVM, RTHCPHYS HCPhys, void **ppv)
{
    /*
     * Linear search.
     */
    /** @todo implement when actually used. */
    return VERR_INVALID_POINTER;
}
#endif /* unused, not implemented */


/**
 * Read hypervisor memory from GC virtual address.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   pvDst       Destination address (HC of course).
 * @param   GCPtr       GC virtual address.
 * @param   cb          Number of bytes to read.
 *
 * @remarks For DBGF only.
 */
VMMR3DECL(int) MMR3HyperReadGCVirt(PVM pVM, void *pvDst, RTGCPTR GCPtr, size_t cb)
{
    if (GCPtr - pVM->mm.s.pvHyperAreaGC >= pVM->mm.s.cbHyperArea)
        return VERR_INVALID_PARAMETER;
    return PGMR3MapRead(pVM, pvDst, GCPtr, cb);
}


/**
 * Info handler for 'hma', it dumps the list of lookup records for the hypervisor memory area.
 *
 * @param   pVM         The VM handle.
 * @param   pHlp        Callback functions for doing output.
 * @param   pszArgs     Argument string. Optional and specific to the handler.
 */
static DECLCALLBACK(void) mmR3HyperInfoHma(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    pHlp->pfnPrintf(pHlp, "Hypervisor Memory Area (HMA) Layout: Base %VGv, 0x%08x bytes\n",
                    pVM->mm.s.pvHyperAreaGC, pVM->mm.s.cbHyperArea);

    PMMLOOKUPHYPER  pLookup = (PMMLOOKUPHYPER)((uint8_t *)pVM->mm.s.pHyperHeapR3 + pVM->mm.s.offLookupHyper);
    for (;;)
    {
        switch (pLookup->enmType)
        {
            case MMLOOKUPHYPERTYPE_LOCKED:
                pHlp->pfnPrintf(pHlp, "%VGv-%VGv %RHv LOCKED  %-*s %s\n",
                                pLookup->off + pVM->mm.s.pvHyperAreaGC,
                                pLookup->off + pVM->mm.s.pvHyperAreaGC + pLookup->cb,
                                pLookup->u.Locked.pvR3,
                                sizeof(RTHCPTR) * 2,
                                pLookup->u.Locked.pLockedMem->eType == MM_LOCKED_TYPE_HYPER_NOFREE  ? "nofree"
                                : pLookup->u.Locked.pLockedMem->eType == MM_LOCKED_TYPE_HYPER       ? "autofree"
                                : pLookup->u.Locked.pLockedMem->eType == MM_LOCKED_TYPE_HYPER_PAGES ? "pages"
                                : pLookup->u.Locked.pLockedMem->eType == MM_LOCKED_TYPE_PHYS        ? "gstphys"
                                : "??",
                                pLookup->pszDesc);
                break;

            case MMLOOKUPHYPERTYPE_HCPHYS:
                pHlp->pfnPrintf(pHlp, "%VGv-%VGv %RHv HCPHYS  %RHp %s\n",
                                pLookup->off + pVM->mm.s.pvHyperAreaGC,
                                pLookup->off + pVM->mm.s.pvHyperAreaGC + pLookup->cb,
                                pLookup->u.HCPhys.pvR3, pLookup->u.HCPhys.HCPhys,
                                pLookup->pszDesc);
                break;

            case MMLOOKUPHYPERTYPE_GCPHYS:
                pHlp->pfnPrintf(pHlp, "%VGv-%VGv %*s GCPHYS  %VGp%*s %s\n",
                                pLookup->off + pVM->mm.s.pvHyperAreaGC,
                                pLookup->off + pVM->mm.s.pvHyperAreaGC + pLookup->cb,
                                sizeof(RTHCPTR) * 2, "",
                                pLookup->u.GCPhys.GCPhys, RT_ABS((int)(sizeof(RTHCPHYS) - sizeof(RTGCPHYS))) * 2, "",
                                pLookup->pszDesc);
                break;

            case MMLOOKUPHYPERTYPE_MMIO2:
                pHlp->pfnPrintf(pHlp, "%VGv-%VGv %*s MMIO2   %VGp%*s %s\n",
                                pLookup->off + pVM->mm.s.pvHyperAreaGC,
                                pLookup->off + pVM->mm.s.pvHyperAreaGC + pLookup->cb,
                                sizeof(RTHCPTR) * 2, "",
                                pLookup->u.MMIO2.off, RT_ABS((int)(sizeof(RTHCPHYS) - sizeof(RTGCPHYS))) * 2, "",
                                pLookup->pszDesc);
                break;

            case MMLOOKUPHYPERTYPE_DYNAMIC:
                pHlp->pfnPrintf(pHlp, "%VGv-%VGv %*s DYNAMIC %*s %s\n",
                                pLookup->off + pVM->mm.s.pvHyperAreaGC,
                                pLookup->off + pVM->mm.s.pvHyperAreaGC + pLookup->cb,
                                sizeof(RTHCPTR) * 2, "",
                                sizeof(RTHCPTR) * 2, "",
                                pLookup->pszDesc);
                break;

            default:
                AssertMsgFailed(("enmType=%d\n", pLookup->enmType));
                break;
        }

        /* next */
        if ((unsigned)pLookup->offNext == NIL_OFFSET)
            break;
        pLookup = (PMMLOOKUPHYPER)((uint8_t *)pLookup + pLookup->offNext);
    }
}

