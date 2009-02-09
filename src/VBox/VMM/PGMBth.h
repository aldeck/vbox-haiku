/* $Id$ */
/** @file
 * VBox - Page Manager / Monitor, Shadow+Guest Paging Template.
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
*   Internal Functions                                                         *
*******************************************************************************/
__BEGIN_DECLS
PGM_BTH_DECL(int, InitData)(PVM pVM, PPGMMODEDATA pModeData, bool fResolveGCAndR0);
PGM_BTH_DECL(int, Enter)(PVM pVM, RTGCPHYS GCPhysCR3);
PGM_BTH_DECL(int, Relocate)(PVM pVM, RTGCPTR offDelta);

PGM_BTH_DECL(int, Trap0eHandler)(PVM pVM, RTGCUINT uErr, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault);
PGM_BTH_DECL(int, SyncCR3)(PVM pVM, uint64_t cr0, uint64_t cr3, uint64_t cr4, bool fGlobal);
PGM_BTH_DECL(int, SyncPage)(PVM pVM, X86PDE PdeSrc, RTGCPTR GCPtrPage, unsigned cPages, unsigned uError);
PGM_BTH_DECL(int, VerifyAccessSyncPage)(PVM pVM, RTGCPTR Addr, unsigned fPage, unsigned uError);
PGM_BTH_DECL(int, InvalidatePage)(PVM pVM, RTGCPTR GCPtrPage);
PGM_BTH_DECL(int, PrefetchPage)(PVM pVM, RTGCPTR GCPtrPage);
PGM_BTH_DECL(unsigned, AssertCR3)(PVM pVM, uint64_t cr3, uint64_t cr4, RTGCPTR GCPtr = 0, RTGCPTR cb = ~(RTGCPTR)0);
PGM_BTH_DECL(int, MapCR3)(PVM pVM, RTGCPHYS GCPhysCR3);
PGM_BTH_DECL(int, UnmapCR3)(PVM pVM);
__END_DECLS


/**
 * Initializes the both bit of the paging mode data.
 *
 * @returns VBox status code.
 * @param   pVM             The VM handle.
 * @param   fResolveGCAndR0 Indicate whether or not GC and Ring-0 symbols can be resolved now.
 *                          This is used early in the init process to avoid trouble with PDM
 *                          not being initialized yet.
 */
PGM_BTH_DECL(int, InitData)(PVM pVM, PPGMMODEDATA pModeData, bool fResolveGCAndR0)
{
    Assert(pModeData->uShwType == PGM_SHW_TYPE); Assert(pModeData->uGstType == PGM_GST_TYPE);

    /* Ring 3 */
    pModeData->pfnR3BthRelocate             = PGM_BTH_NAME(Relocate);
    pModeData->pfnR3BthSyncCR3              = PGM_BTH_NAME(SyncCR3);
    pModeData->pfnR3BthInvalidatePage       = PGM_BTH_NAME(InvalidatePage);
    pModeData->pfnR3BthSyncPage             = PGM_BTH_NAME(SyncPage);
    pModeData->pfnR3BthPrefetchPage         = PGM_BTH_NAME(PrefetchPage);
    pModeData->pfnR3BthVerifyAccessSyncPage = PGM_BTH_NAME(VerifyAccessSyncPage);
#ifdef VBOX_STRICT
    pModeData->pfnR3BthAssertCR3            = PGM_BTH_NAME(AssertCR3);
#endif
    pModeData->pfnR3BthMapCR3               = PGM_BTH_NAME(MapCR3);
    pModeData->pfnR3BthUnmapCR3             = PGM_BTH_NAME(UnmapCR3);

    if (fResolveGCAndR0)
    {
        int rc;

#if PGM_SHW_TYPE != PGM_TYPE_AMD64 && PGM_SHW_TYPE != PGM_TYPE_NESTED && PGM_SHW_TYPE != PGM_TYPE_EPT /* No AMD64 for traditional virtualization, only VT-x and AMD-V. */
        /* GC */
        rc = PDMR3LdrGetSymbolRC(pVM, NULL,       PGM_BTH_NAME_RC_STR(Trap0eHandler),       &pModeData->pfnRCBthTrap0eHandler);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_RC_STR(Trap0eHandler),  rc), rc);
        rc = PDMR3LdrGetSymbolRC(pVM, NULL,       PGM_BTH_NAME_RC_STR(InvalidatePage),      &pModeData->pfnRCBthInvalidatePage);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_RC_STR(InvalidatePage), rc), rc);
        rc = PDMR3LdrGetSymbolRC(pVM, NULL,       PGM_BTH_NAME_RC_STR(SyncCR3),             &pModeData->pfnRCBthSyncCR3);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_RC_STR(SyncPage), rc), rc);
        rc = PDMR3LdrGetSymbolRC(pVM, NULL,       PGM_BTH_NAME_RC_STR(SyncPage),            &pModeData->pfnRCBthSyncPage);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_RC_STR(SyncPage), rc), rc);
        rc = PDMR3LdrGetSymbolRC(pVM, NULL,       PGM_BTH_NAME_RC_STR(PrefetchPage),        &pModeData->pfnRCBthPrefetchPage);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_RC_STR(PrefetchPage), rc), rc);
        rc = PDMR3LdrGetSymbolRC(pVM, NULL,       PGM_BTH_NAME_RC_STR(VerifyAccessSyncPage),&pModeData->pfnRCBthVerifyAccessSyncPage);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_RC_STR(VerifyAccessSyncPage), rc), rc);
# ifdef VBOX_STRICT
        rc = PDMR3LdrGetSymbolRC(pVM, NULL,       PGM_BTH_NAME_RC_STR(AssertCR3),           &pModeData->pfnRCBthAssertCR3);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_RC_STR(AssertCR3), rc), rc);
# endif
        rc = PDMR3LdrGetSymbolRC(pVM, NULL,       PGM_BTH_NAME_RC_STR(MapCR3),              &pModeData->pfnRCBthMapCR3);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_RC_STR(MapCR3), rc), rc);
        rc = PDMR3LdrGetSymbolRC(pVM, NULL,       PGM_BTH_NAME_RC_STR(UnmapCR3),            &pModeData->pfnRCBthUnmapCR3);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_RC_STR(UnmapCR3), rc), rc);
#endif /* Not AMD64 shadow paging. */

        /* Ring 0 */
        rc = PDMR3LdrGetSymbolR0(pVM, NULL,       PGM_BTH_NAME_R0_STR(Trap0eHandler),       &pModeData->pfnR0BthTrap0eHandler);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_R0_STR(Trap0eHandler),  rc), rc);
        rc = PDMR3LdrGetSymbolR0(pVM, NULL,       PGM_BTH_NAME_R0_STR(InvalidatePage),      &pModeData->pfnR0BthInvalidatePage);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_R0_STR(InvalidatePage), rc), rc);
        rc = PDMR3LdrGetSymbolR0(pVM, NULL,       PGM_BTH_NAME_R0_STR(SyncCR3),             &pModeData->pfnR0BthSyncCR3);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_R0_STR(SyncCR3), rc), rc);
        rc = PDMR3LdrGetSymbolR0(pVM, NULL,       PGM_BTH_NAME_R0_STR(SyncPage),            &pModeData->pfnR0BthSyncPage);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_R0_STR(SyncPage), rc), rc);
        rc = PDMR3LdrGetSymbolR0(pVM, NULL,       PGM_BTH_NAME_R0_STR(PrefetchPage),        &pModeData->pfnR0BthPrefetchPage);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_R0_STR(PrefetchPage), rc), rc);
        rc = PDMR3LdrGetSymbolR0(pVM, NULL,       PGM_BTH_NAME_R0_STR(VerifyAccessSyncPage),&pModeData->pfnR0BthVerifyAccessSyncPage);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_R0_STR(VerifyAccessSyncPage), rc), rc);
#ifdef VBOX_STRICT
        rc = PDMR3LdrGetSymbolR0(pVM, NULL,       PGM_BTH_NAME_R0_STR(AssertCR3),           &pModeData->pfnR0BthAssertCR3);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_R0_STR(AssertCR3), rc), rc);
#endif
        rc = PDMR3LdrGetSymbolR0(pVM, NULL,       PGM_BTH_NAME_R0_STR(MapCR3),              &pModeData->pfnR0BthMapCR3);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_R0_STR(MapCR3), rc), rc);
        rc = PDMR3LdrGetSymbolR0(pVM, NULL,       PGM_BTH_NAME_R0_STR(UnmapCR3),            &pModeData->pfnR0BthUnmapCR3);
        AssertMsgRCReturn(rc, ("%s -> rc=%Rrc\n", PGM_BTH_NAME_R0_STR(UnmapCR3), rc), rc);
    }
    return VINF_SUCCESS;
}


/**
 * Enters the shadow+guest mode.
 *
 * @returns VBox status code.
 * @param   pVM         VM handle.
 * @param   GCPhysCR3   The physical address from the CR3 register.
 */
PGM_BTH_DECL(int, Enter)(PVM pVM, RTGCPHYS GCPhysCR3)
{
#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    /* Here we deal with allocation of the root shadow page table for real and protected mode during mode switches;
     * Other modes rely on MapCR3/UnmapCR3 to setup the shadow root page tables. 
     */
# if  (   (   PGM_SHW_TYPE == PGM_TYPE_32BITS \
           || PGM_SHW_TYPE == PGM_TYPE_PAE    \
           || PGM_SHW_TYPE == PGM_TYPE_AMD64) \
       && (   PGM_GST_TYPE == PGM_TYPE_REAL   \
           || PGM_GST_TYPE == PGM_TYPE_PROT))

    Assert(!HWACCMIsNestedPagingActive(pVM));
    /* We only need shadow paging in real and protected mode for VT-x and AMD-V (excluding nested paging/EPT modes) */
    if (HWACCMIsEnabled(pVM))
    {
        /* Free the previous root mapping if still active. */
        PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool);
        if (pVM->pgm.s.CTX_SUFF(pShwPageCR3))
        {
            /* It might have been freed already by a pool flush (see e.g. PGMR3MappingsUnfix). */
            /** @todo Coordinate this better with the pool. */
            if (pVM->pgm.s.CTX_SUFF(pShwPageCR3)->enmKind != PGMPOOLKIND_FREE)
                pgmPoolFreeByPage(pPool, pVM->pgm.s.CTX_SUFF(pShwPageCR3), pVM->pgm.s.iShwUser, pVM->pgm.s.iShwUserTable);
            pVM->pgm.s.pShwPageCR3R3 = 0;
            pVM->pgm.s.pShwPageCR3R0 = 0;
            pVM->pgm.s.pShwRootR3    = 0;
#  ifndef VBOX_WITH_2X_4GB_ADDR_SPACE
            pVM->pgm.s.pShwRootR0    = 0;
#  endif
            pVM->pgm.s.HCPhysShwCR3  = 0;
            pVM->pgm.s.iShwUser      = 0;
            pVM->pgm.s.iShwUserTable = 0;
        }

        /* contruct a fake address */
        RTGCPHYS GCPhysCR3 = RT_BIT_64(63);
        pVM->pgm.s.iShwUser      = SHW_POOL_ROOT_IDX;
        pVM->pgm.s.iShwUserTable = GCPhysCR3 >> PAGE_SHIFT;
        int rc = pgmPoolAlloc(pVM, GCPhysCR3, BTH_PGMPOOLKIND_ROOT, pVM->pgm.s.iShwUser, pVM->pgm.s.iShwUserTable, &pVM->pgm.s.CTX_SUFF(pShwPageCR3));
        if (rc == VERR_PGM_POOL_FLUSHED)
        {
            Log(("Bth-Enter: PGM pool flushed -> signal sync cr3\n"));
            Assert(VM_FF_ISSET(pVM, VM_FF_PGM_SYNC_CR3));
            return VINF_PGM_SYNC_CR3;
        }
        AssertRCReturn(rc, rc);
#  ifdef IN_RING0
        pVM->pgm.s.pShwPageCR3R3 = MMHyperCCToR3(pVM, pVM->pgm.s.CTX_SUFF(pShwPageCR3));
#  else
        pVM->pgm.s.pShwPageCR3R0 = MMHyperCCToR0(pVM, pVM->pgm.s.CTX_SUFF(pShwPageCR3));
#  endif
        pVM->pgm.s.pShwRootR3    = (R3PTRTYPE(void *))pVM->pgm.s.CTX_SUFF(pShwPageCR3)->pvPageR3;
        Assert(pVM->pgm.s.pShwRootR3);
#  ifndef VBOX_WITH_2X_4GB_ADDR_SPACE
        pVM->pgm.s.pShwRootR0    = (R0PTRTYPE(void *))PGMPOOL_PAGE_2_PTR(pPool->CTX_SUFF(pVM), pVM->pgm.s.CTX_SUFF(pShwPageCR3));
#  endif
        pVM->pgm.s.HCPhysShwCR3  = pVM->pgm.s.CTX_SUFF(pShwPageCR3)->Core.Key;
    }
# endif
#else
    /* nothing special to do here - InitData does the job. */
#endif
    return VINF_SUCCESS;
}


/**
 * Relocate any GC pointers related to shadow mode paging.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 * @param   offDelta    The reloation offset.
 */
PGM_BTH_DECL(int, Relocate)(PVM pVM, RTGCPTR offDelta)
{
    /* nothing special to do here - InitData does the job. */
    return VINF_SUCCESS;
}

