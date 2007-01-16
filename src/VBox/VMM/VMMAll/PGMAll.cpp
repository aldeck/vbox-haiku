/* $Id$ */
/** @file
 * PGM - Page Manager and Monitor - All context code.
 */

/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
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
#define LOG_GROUP LOG_GROUP_PGM
#include <VBox/pgm.h>
#include <VBox/cpum.h>
#include <VBox/selm.h>
#include <VBox/iom.h>
#include <VBox/sup.h>
#include <VBox/mm.h>
#include <VBox/stam.h>
#include <VBox/csam.h>
#include <VBox/trpm.h>
#include <VBox/rem.h>
#include <VBox/em.h>
#include "PGMInternal.h"
#include <VBox/vm.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <VBox/log.h>
#include <VBox/param.h>
#include <VBox/err.h>


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Stated structure for PGM_GST_NAME(HandlerVirtualUpdate) that's
 * passed to PGM_GST_NAME(VirtHandlerUpdateOne) during enumeration.
 */
typedef struct PGMHVUSTATE
{
    /** The VM handle. */
    PVM         pVM;
    /** The todo flags. */
    RTUINT      fTodo;
    /** The CR4 register value. */
    uint32_t    cr4;
} PGMHVUSTATE,  *PPGMHVUSTATE;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
/** @def DUMP_PDE_BIG
 * Debug routine for dumping a big PDE.
 */
#ifdef DEBUG_Sander
/** Debug routine for dumping a big PDE. */
static void pgmDumpPDEBig(const char *pszPrefix, int iPD, VBOXPDE Pde)
{
    Log(("%s: BIG %d u10PageNo=%08X P=%d W=%d U=%d CACHE=%d ACC=%d DIR=%d GBL=%d\n", pszPrefix, iPD, Pde.b.u10PageNo, Pde.b.u1Present, Pde.b.u1Write, Pde.b.u1User, Pde.b.u1CacheDisable, Pde.b.u1Accessed, Pde.b.u1Dirty, Pde.b.u1Global));
    Log(("%s: BIG %d WRT=%d AVAIL=%X RSV=%X PAT=%d\n", pszPrefix, iPD, Pde.b.u1WriteThru, Pde.b.u3Available, Pde.b.u8PageNoHigh, Pde.b.u1PAT));
}
#define DUMP_PDE_BIG(a, b, c) pgmDumpPDEBig(a, b, c)
#else
#define DUMP_PDE_BIG(a, b, c) do { } while (0)
#endif



#if 1///@todo ndef __AMD64__
/*
 * Shadow - 32-bit mode
 */
#define PGM_SHW_TYPE                PGM_TYPE_32BIT
#define PGM_SHW_NAME(name)          PGM_SHW_NAME_32BIT(name)
#include "PGMAllShw.h"

/* Guest - real mode */
#define PGM_GST_TYPE                PGM_TYPE_REAL
#define PGM_GST_NAME(name)          PGM_GST_NAME_REAL(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_32BIT_REAL(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_32BIT_PT_FOR_PHYS
#include "PGMAllGst.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - protected mode */
#define PGM_GST_TYPE                PGM_TYPE_PROT
#define PGM_GST_NAME(name)          PGM_GST_NAME_PROT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_32BIT_PROT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_32BIT_PT_FOR_PHYS
#include "PGMAllGst.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - 32-bit mode */
#define PGM_GST_TYPE                PGM_TYPE_32BIT
#define PGM_GST_NAME(name)          PGM_GST_NAME_32BIT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_32BIT_32BIT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_32BIT_PT_FOR_32BIT_PT
#define BTH_PGMPOOLKIND_PT_FOR_BIG  PGMPOOLKIND_32BIT_PT_FOR_32BIT_4MB
#include "PGMAllGst.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_BIG
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

#undef PGM_SHW_TYPE
#undef PGM_SHW_NAME
#endif /* !__AMD64__ */


/*
 * Shadow - PAE mode
 */
#define PGM_SHW_TYPE                PGM_TYPE_PAE
#define PGM_SHW_NAME(name)          PGM_SHW_NAME_PAE(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_PAE_REAL(name)
#include "PGMAllShw.h"

/* Guest - real mode */
#define PGM_GST_TYPE                PGM_TYPE_REAL
#define PGM_GST_NAME(name)          PGM_GST_NAME_REAL(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_PAE_REAL(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_PAE_PT_FOR_PHYS
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - protected mode */
#define PGM_GST_TYPE                PGM_TYPE_PROT
#define PGM_GST_NAME(name)          PGM_GST_NAME_PROT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_PAE_PROT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_PAE_PT_FOR_PHYS
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - 32-bit mode */
#define PGM_GST_TYPE                PGM_TYPE_32BIT
#define PGM_GST_NAME(name)          PGM_GST_NAME_32BIT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_PAE_32BIT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_PAE_PT_FOR_32BIT_PT
#define BTH_PGMPOOLKIND_PT_FOR_BIG  PGMPOOLKIND_PAE_PT_FOR_32BIT_4MB
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_BIG
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME


/* Guest - PAE mode */
#define PGM_GST_TYPE                PGM_TYPE_PAE
#define PGM_GST_NAME(name)          PGM_GST_NAME_PAE(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_PAE_PAE(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_PAE_PT_FOR_PAE_PT
#define BTH_PGMPOOLKIND_PT_FOR_BIG  PGMPOOLKIND_PAE_PT_FOR_PAE_2MB
#include "PGMAllGst.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_BIG
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

#undef PGM_SHW_TYPE
#undef PGM_SHW_NAME


/*
 * Shadow - AMD64 mode
 */
#define PGM_SHW_TYPE                PGM_TYPE_AMD64
#define PGM_SHW_NAME(name)          PGM_SHW_NAME_AMD64(name)
#include "PGMAllShw.h"

/* Guest - real mode */
#define PGM_GST_TYPE                PGM_TYPE_REAL
#define PGM_GST_NAME(name)          PGM_GST_NAME_REAL(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_AMD64_REAL(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_PAE_PT_FOR_PHYS
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_NAME
#undef PGM_GST_TYPE

/* Guest - protected mode */
#define PGM_GST_TYPE                PGM_TYPE_PROT
#define PGM_GST_NAME(name)          PGM_GST_NAME_PROT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_AMD64_PROT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_PAE_PT_FOR_PHYS
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - AMD64 mode */
#define PGM_GST_TYPE                PGM_TYPE_AMD64
#define PGM_GST_NAME(name)          PGM_GST_NAME_AMD64(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_AMD64_AMD64(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_PAE_PT_FOR_PAE_PT
#define BTH_PGMPOOLKIND_PT_FOR_BIG  PGMPOOLKIND_PAE_PT_FOR_PAE_2MB
#include "PGMAllGst.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_BIG
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

#undef PGM_SHW_TYPE
#undef PGM_SHW_NAME



/**
 * #PF Handler.
 *
 * @returns VBox status code (appropriate for trap handling and GC return).
 * @param   pVM         VM Handle.
 * @param   uErr        The trap error code.
 * @param   pRegFrame   Trap register frame.
 * @param   pvFault     The fault address.
 */
PGMDECL(int)     PGMTrap0eHandler(PVM pVM, RTGCUINT uErr, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault)
{
    LogFlow(("PGMTrap0eHandler: uErr=%#x pvFault=%VGv eip=%VGv\n", uErr, pvFault, pRegFrame->eip));
    STAM_PROFILE_START(&pVM->pgm.s.StatGCTrap0e, a);
    STAM_STATS({ pVM->pgm.s.CTXSUFF(pStatTrap0eAttribution) = NULL; } );


#ifdef VBOX_WITH_STATISTICS
    /*
     * Error code stats.
     */
    if (uErr & X86_TRAP_PF_US)
    {
        if (!(uErr & X86_TRAP_PF_P))
        {
            if (uErr & X86_TRAP_PF_RW)
                STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eUSNotPresentWrite);
            else
                STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eUSNotPresentRead);
        }
        else if (uErr & X86_TRAP_PF_RW)
            STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eUSWrite);
        else if (uErr & X86_TRAP_PF_RSVD)
            STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eUSReserved);
        else
            STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eUSRead);
    }
    else
    {   //supervisor
        if (!(uErr & X86_TRAP_PF_P))
        {
            if (uErr & X86_TRAP_PF_RW)
                STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eSVNotPresentWrite);
            else
                STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eSVNotPresentRead);
        }
        else if (uErr & X86_TRAP_PF_RW)
            STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eSVWrite);
        else if (uErr & X86_TRAP_PF_RSVD)
            STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eSVReserved);
    }
#endif

    /*
     * Call the worker.
     */
    int rc = PGM_BTH_PFN(Trap0eHandler, pVM)(pVM, uErr, pRegFrame, pvFault);
    if (rc == VINF_PGM_SYNCPAGE_MODIFIED_PDE)
        rc = VINF_SUCCESS;
    STAM_STATS({ if (!pVM->pgm.s.CTXSUFF(pStatTrap0eAttribution))
                    pVM->pgm.s.CTXSUFF(pStatTrap0eAttribution) = &pVM->pgm.s.StatTrap0eMisc; });
    STAM_PROFILE_STOP_EX(&pVM->pgm.s.StatGCTrap0e, pVM->pgm.s.CTXSUFF(pStatTrap0eAttribution), a);
    return rc;
}


/**
 * Prefetch a page
 *
 * Typically used to sync commonly used pages before entering raw mode
 * after a CR3 reload.
 *
 * @returns VBox status code suitable for scheduling.
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_PGM_SYNC_CR3 if we're out of shadow pages or something like that.
 * @param   pVM         VM handle.
 * @param   GCPtrPage   Page to invalidate.
 */
PGMDECL(int) PGMPrefetchPage(PVM pVM, RTGCPTR GCPtrPage)
{
    STAM_PROFILE_START(&pVM->pgm.s.StatHCPrefetch, a);
    int rc = PGM_BTH_PFN(PrefetchPage, pVM)(pVM, (RTGCUINTPTR)GCPtrPage);
    STAM_PROFILE_STOP(&pVM->pgm.s.StatHCPrefetch, a);
    AssertMsg(rc == VINF_SUCCESS || rc == VINF_PGM_SYNC_CR3 || VBOX_FAILURE(rc), ("rc=%Vrc\n", rc));
    return rc;
}


/**
 * Gets the mapping corresponding to the specified address (if any).
 *
 * @returns Pointer to the mapping.
 * @returns NULL if not
 *
 * @param   pVM         The virtual machine.
 * @param   GCPtr       The guest context pointer.
 */
PPGMMAPPING pgmGetMapping(PVM pVM, RTGCPTR GCPtr)
{
    PPGMMAPPING pMapping = CTXSUFF(pVM->pgm.s.pMappings);
    while (pMapping)
    {
        if ((uintptr_t)GCPtr < (uintptr_t)pMapping->GCPtr)
            break;
        if ((uintptr_t)GCPtr - (uintptr_t)pMapping->GCPtr < pMapping->cb)
        {
            STAM_COUNTER_INC(&pVM->pgm.s.StatGCSyncPTConflict);
            return pMapping;
        }
        pMapping = CTXSUFF(pMapping->pNext);
    }
    return NULL;
}


/**
 * Verifies a range of pages for read or write access
 *
 * Only checks the guest's page tables
 *
 * @returns VBox status code.
 * @param   pVM         VM handle.
 * @param   Addr        Guest virtual address to check
 * @param   cbSize      Access size
 * @param   fAccess     Access type (r/w, user/supervisor (X86_PTE_*))
 */
PGMDECL(int) PGMIsValidAccess(PVM pVM, RTGCUINTPTR Addr, uint32_t cbSize, uint32_t fAccess)
{
    /*
     * Validate input.
     */
    if (fAccess & ~(X86_PTE_US | X86_PTE_RW))
    {
        AssertMsgFailed(("PGMIsValidAccess: invalid access type %08x\n", fAccess));
        return VERR_INVALID_PARAMETER;
    }

    uint64_t fPage;
    int rc = PGMGstGetPage(pVM, (RTGCPTR)Addr, &fPage, NULL);
    if (VBOX_FAILURE(rc))
    {
        Log(("PGMIsValidAccess: access violation for %VGv rc=%d\n", Addr, rc));
        return VINF_EM_RAW_GUEST_TRAP;
    }

    /*
     * Check if the access would cause a page fault
     *
     * Note that hypervisor page directories are not present in the guest's tables, so this check
     * is sufficient.
     */
    bool fWrite = !!(fAccess & X86_PTE_RW);
    bool fUser  = !!(fAccess & X86_PTE_US);
    if (  !(fPage & X86_PTE_P)
        || (fWrite && !(fPage & X86_PTE_RW))
        || (fUser  && !(fPage & X86_PTE_US)) )
    {
        Log(("PGMIsValidAccess: access violation for %VGv attr %#llx vs %d:%d\n", Addr, fPage, fWrite, fUser));
        return VINF_EM_RAW_GUEST_TRAP;
    }
    if (    VBOX_SUCCESS(rc)
        &&  PAGE_ADDRESS(Addr) != PAGE_ADDRESS(Addr + cbSize))
        return PGMIsValidAccess(pVM, Addr + PAGE_SIZE, (cbSize > PAGE_SIZE) ? cbSize - PAGE_SIZE : 1, fAccess);
    return rc;
}


/**
 * Verifies a range of pages for read or write access
 *
 * Supports handling of pages marked for dirty bit tracking and CSAM
 *
 * @returns VBox status code.
 * @param   pVM         VM handle.
 * @param   Addr        Guest virtual address to check
 * @param   cbSize      Access size
 * @param   fAccess     Access type (r/w, user/supervisor (X86_PTE_*))
 */
PGMDECL(int) PGMVerifyAccess(PVM pVM, RTGCUINTPTR Addr, uint32_t cbSize, uint32_t fAccess)
{
    /*
     * Validate input.
     */
    if (fAccess & ~(X86_PTE_US | X86_PTE_RW))
    {
        AssertMsgFailed(("PGMVerifyAccess: invalid access type %08x\n", fAccess));
        return VERR_INVALID_PARAMETER;
    }

    uint64_t fPageGst;
    int rc = PGMGstGetPage(pVM, (RTGCPTR)Addr, &fPageGst, NULL);
    if (VBOX_FAILURE(rc))
    {
        Log(("PGMVerifyAccess: access violation for %VGv rc=%d\n", Addr, rc));
        return VINF_EM_RAW_GUEST_TRAP;
    }

    /*
     * Check if the access would cause a page fault
     *
     * Note that hypervisor page directories are not present in the guest's tables, so this check
     * is sufficient.
     */
    const bool fWrite = !!(fAccess & X86_PTE_RW);
    const bool fUser  = !!(fAccess & X86_PTE_US);
    if (  !(fPageGst & X86_PTE_P)
        || (fWrite  && !(fPageGst & X86_PTE_RW))
        || (fUser   && !(fPageGst & X86_PTE_US)) )
    {
        Log(("PGMVerifyAccess: access violation for %VGv attr %#llx vs %d:%d\n", Addr, fPageGst, fWrite, fUser));
        return VINF_EM_RAW_GUEST_TRAP;
    }

    /*
     * Next step is to verify if we protected this page for dirty bit tracking or for CSAM scanning
     */
    rc = PGMShwGetPage(pVM, (RTGCPTR)Addr, NULL, NULL);
    if (rc == VERR_PAGE_NOT_PRESENT)
    {
        /*
         * Page is not present in our page tables.
         * Try to sync it!
         */
        Assert(X86_TRAP_PF_RW == X86_PTE_RW && X86_TRAP_PF_US == X86_PTE_US);
        uint32_t uErr = fAccess & (X86_TRAP_PF_RW | X86_TRAP_PF_US);
        rc = PGM_BTH_PFN(VerifyAccessSyncPage, pVM)(pVM, Addr, fPageGst, uErr);
        if (rc != VINF_SUCCESS)
            return rc;
    }
    else if (rc == VERR_PAGE_TABLE_NOT_PRESENT)
    {
        /*
         * Page table is not present; can't do much here (?)
         * We could of course try sync the page table.... (?)
         */
        return VINF_EM_RAW_EMULATE_INSTR;
    }
    else
        AssertMsg(rc == VINF_SUCCESS, ("PGMShwGetPage %VGv failed with %Vrc\n", Addr, rc));

#if 0 /* def VBOX_STRICT; triggers too often now */
    /*
     * This check is a bit paranoid, but useful.
     */
    /** @note this will assert when writing to monitored pages (a bit annoying actually) */
    uint64_t fPageShw;
    rc = PGMShwGetPage(pVM, (RTGCPTR)Addr, &fPageShw, NULL);
    if (    (rc == VERR_PAGE_NOT_PRESENT || VBOX_FAILURE(rc))
        || (fWrite && !(fPageShw & X86_PTE_RW))
        || (fUser  && !(fPageShw & X86_PTE_US)) )
    {
        AssertMsgFailed(("Unexpected access violation for %VGv! rc=%Vrc write=%d user=%d\n",
                         Addr, rc, fWrite && !(fPageShw & X86_PTE_RW), fUser && !(fPageShw & X86_PTE_US)));
        return VINF_EM_RAW_GUEST_TRAP;
    }
#endif

    if (    VBOX_SUCCESS(rc)
        &&  (   PAGE_ADDRESS(Addr) != PAGE_ADDRESS(Addr + cbSize - 1)
             || Addr + cbSize < Addr))
        return PGMVerifyAccess(pVM, Addr + PAGE_SIZE, cbSize > PAGE_SIZE ? cbSize - PAGE_SIZE : 1, fAccess);
    return rc;
}

#ifndef IN_GC
/**
 * Emulation of the invlpg instruction (HC only actually).
 *
 * @returns VBox status code.
 * @param   pVM         VM handle.
 * @param   GCPtrPage   Page to invalidate.
 * @remark  ASSUMES the page table entry or page directory is
 *          valid. Fairly safe, but there could be edge cases!
 * @todo    Flush page or page directory only if necessary!
 */
PGMDECL(int) PGMInvalidatePage(PVM pVM, RTGCPTR GCPtrPage)
{
    LogFlow(("PGMInvalidatePage: GCPtrPage=%VGv\n", GCPtrPage));

    STAM_PROFILE_START(&CTXMID(pVM->pgm.s.Stat,InvalidatePage), a);
    int rc = PGM_BTH_PFN(InvalidatePage, pVM)(pVM, GCPtrPage);
    STAM_PROFILE_STOP(&CTXMID(pVM->pgm.s.Stat,InvalidatePage), a);

#ifndef IN_RING0
    /*
     * Check if we have a pending update of the CR3 monitoring.
     */
    if (    VBOX_SUCCESS(rc)
        &&  (pVM->pgm.s.fSyncFlags & PGM_SYNC_MONITOR_CR3))
    {
        pVM->pgm.s.fSyncFlags &= ~PGM_SYNC_MONITOR_CR3;
        Assert(!pVM->pgm.s.fMappingsFixed);
        Assert(pVM->pgm.s.GCPhysCR3 == pVM->pgm.s.GCPhysGstCR3Monitored);
        rc = PGM_GST_PFN(MonitorCR3, pVM)(pVM, pVM->pgm.s.GCPhysCR3);
    }
#endif

#ifdef IN_RING3
    /*
     * Inform CSAM about the flush
     */
    /** @note this is to check if monitored pages have been changed; when we implement callbacks for virtual handlers, this is no longer required. */
    CSAMR3FlushPage(pVM, GCPtrPage);
#endif
    return rc;
}
#endif


/**
 * Executes an instruction using the interpreter.
 *
 * @returns VBox status code (appropriate for trap handling and GC return).
 * @param   pVM         VM handle.
 * @param   pRegFrame   Register frame.
 * @param   pvFault     Fault address.
 */
PGMDECL(int) PGMInterpretInstruction(PVM pVM, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault)
{
#ifdef IN_RING0
    /** @todo */
    int rc = VINF_EM_RAW_EMULATE_INSTR;
#else
    uint32_t cb;
    int rc = EMInterpretInstruction(pVM, pRegFrame, pvFault, &cb);
    if (rc == VERR_EM_INTERPRETER)
        rc = VINF_EM_RAW_EMULATE_INSTR;
    if (rc != VINF_SUCCESS)
        Log(("PGMInterpretInstruction: returns %Rrc (pvFault=%VGv)\n", rc, pvFault));
#endif
    return rc;
}


/**
 * Gets effective page information (from the VMM page directory).
 *
 * @returns VBox status.
 * @param   pVM         VM Handle.
 * @param   GCPtr       Guest Context virtual address of the page.
 * @param   pfFlags     Where to store the flags. These are X86_PTE_*.
 * @param   pHCPhys     Where to store the HC physical address of the page.
 *                      This is page aligned.
 * @remark  You should use PGMMapGetPage() for pages in a mapping.
 */
PGMDECL(int) PGMShwGetPage(PVM pVM, RTGCPTR GCPtr, uint64_t *pfFlags, PRTHCPHYS pHCPhys)
{
    return PGM_SHW_PFN(GetPage,pVM)(pVM, (RTGCUINTPTR)GCPtr, pfFlags, pHCPhys);
}


/**
 * Sets (replaces) the page flags for a range of pages in the shadow context.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPtr       The address of the first page.
 * @param   cb          The size of the range in bytes.
 * @param   fFlags      Page flags X86_PTE_*, excluding the page mask of course.
 * @remark  You must use PGMMapSetPage() for pages in a mapping.
 */
PGMDECL(int) PGMShwSetPage(PVM pVM, RTGCPTR GCPtr, size_t cb, uint64_t fFlags)
{
    return PGMShwModifyPage(pVM, GCPtr, cb, fFlags, 0);
}


/**
 * Modify page flags for a range of pages in the shadow context.
 *
 * The existing flags are ANDed with the fMask and ORed with the fFlags.
 *
 * @returns VBox status code.
 * @param   pVM         VM handle.
 * @param   GCPtr       Virtual address of the first page in the range.
 * @param   cb          Size (in bytes) of the range to apply the modification to.
 * @param   fFlags      The OR  mask - page flags X86_PTE_*, excluding the page mask of course.
 * @param   fMask       The AND mask - page flags X86_PTE_*.
 *                      Be very CAREFUL when ~'ing constants which could be 32-bit!
 * @remark  You must use PGMMapModifyPage() for pages in a mapping.
 */
PGMDECL(int)  PGMShwModifyPage(PVM pVM, RTGCPTR GCPtr, size_t cb, uint64_t fFlags, uint64_t fMask)
{
    /*
     * Validate input.
     */
    if (fFlags & X86_PTE_PAE_PG_MASK)
    {
        AssertMsgFailed(("fFlags=%#llx\n", fFlags));
        return VERR_INVALID_PARAMETER;
    }
    if (!cb)
    {
        AssertFailed();
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Align the input.
     */
    cb     += (RTGCUINTPTR)GCPtr & PAGE_OFFSET_MASK;
    cb      = RT_ALIGN_Z(cb, PAGE_SIZE);
    GCPtr   = (RTGCPTR)((RTGCUINTPTR)GCPtr & PAGE_BASE_GC_MASK); /** @todo this ain't necessary, right... */

    /*
     * Call worker.
     */
    return PGM_SHW_PFN(ModifyPage, pVM)(pVM, (RTGCUINTPTR)GCPtr, cb, fFlags, fMask);
}


/**
 * Gets effective Guest OS page information.
 *
 * When GCPtr is in a big page, the function will return as if it was a normal
 * 4KB page. If the need for distinguishing between big and normal page becomes
 * necessary at a later point, a PGMGstGetPage() will be created for that
 * purpose.
 *
 * @returns VBox status.
 * @param   pVM         VM Handle.
 * @param   GCPtr       Guest Context virtual address of the page.
 * @param   pfFlags     Where to store the flags. These are X86_PTE_*, even for big pages.
 * @param   pGCPhys     Where to store the GC physical address of the page.
 *                      This is page aligned. The fact that the
 */
PGMDECL(int) PGMGstGetPage(PVM pVM, RTGCPTR GCPtr, uint64_t *pfFlags, PRTGCPHYS pGCPhys)
{
    return PGM_GST_PFN(GetPage,pVM)(pVM, (RTGCUINTPTR)GCPtr, pfFlags, pGCPhys);
}


/**
 * Checks if the page is present.
 *
 * @returns true if the page is present.
 * @returns false if the page is not present.
 * @param   pVM         The VM handle.
 * @param   GCPtr       Address within the page.
 */
PGMDECL(bool) PGMGstIsPagePresent(PVM pVM, RTGCPTR GCPtr)
{
    int rc = PGMGstGetPage(pVM, GCPtr, NULL, NULL);
    return VBOX_SUCCESS(rc);
}


/**
 * Sets (replaces) the page flags for a range of pages in the guest's tables.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPtr       The address of the first page.
 * @param   cb          The size of the range in bytes.
 * @param   fFlags      Page flags X86_PTE_*, excluding the page mask of course.
 */
PGMDECL(int)  PGMGstSetPage(PVM pVM, RTGCPTR GCPtr, size_t cb, uint64_t fFlags)
{
    return PGMGstModifyPage(pVM, GCPtr, cb, fFlags, 0);
}


/**
 * Modify page flags for a range of pages in the guest's tables
 *
 * The existing flags are ANDed with the fMask and ORed with the fFlags.
 *
 * @returns VBox status code.
 * @param   pVM         VM handle.
 * @param   GCPtr       Virtual address of the first page in the range.
 * @param   cb          Size (in bytes) of the range to apply the modification to.
 * @param   fFlags      The OR  mask - page flags X86_PTE_*, excluding the page mask of course.
 * @param   fMask       The AND mask - page flags X86_PTE_*, excluding the page mask of course.
 *                      Be very CAREFUL when ~'ing constants which could be 32-bit!
 */
PGMDECL(int)  PGMGstModifyPage(PVM pVM, RTGCPTR GCPtr, size_t cb, uint64_t fFlags, uint64_t fMask)
{
    STAM_PROFILE_START(&CTXMID(pVM->pgm.s.Stat,GstModifyPage), a);

    /*
     * Validate input.
     */
    if (fFlags & X86_PTE_PAE_PG_MASK)
    {
        AssertMsgFailed(("fFlags=%#llx\n", fFlags));
        STAM_PROFILE_STOP(&CTXMID(pVM->pgm.s.Stat,GstModifyPage), a);
        return VERR_INVALID_PARAMETER;
    }

    if (!cb)
    {
        AssertFailed();
        STAM_PROFILE_STOP(&CTXMID(pVM->pgm.s.Stat,GstModifyPage), a);
        return VERR_INVALID_PARAMETER;
    }

    LogFlow(("PGMGstModifyPage %VGv %d bytes fFlags=%08llx fMask=%08llx\n", GCPtr, cb, fFlags, fMask));

    /*
     * Adjust input.
     */
    cb     += (RTGCUINTPTR)GCPtr & PAGE_OFFSET_MASK;
    cb      = RT_ALIGN_Z(cb, PAGE_SIZE);
    GCPtr   = (RTGCPTR)((RTGCUINTPTR)GCPtr & PAGE_BASE_GC_MASK);

    /*
     * Call worker.
     */
    int rc = PGM_GST_PFN(ModifyPage, pVM)(pVM, (RTGCUINTPTR)GCPtr, cb, fFlags, fMask);

    STAM_PROFILE_STOP(&CTXMID(pVM->pgm.s.Stat,GstModifyPage), a);
    return rc;
}


/**
 * Temporarily turns off the access monitoring of a page within a monitored
 * physical write/all page access handler region.
 *
 * Use this when no further \#PFs are required for that page. Be aware that
 * a page directory sync might reset the flags, and turn on access monitoring
 * for the page.
 *
 * The caller must do required page table modifications.
 *
 * @returns VBox status code.
 * @param   pVM         VM Handle
 * @param   GCPhys      Start physical address earlier passed to PGMR3HandlerPhysicalRegister().
 * @param   GCPhysPage  Physical address of the page to turn off access monitoring for.
 */
PGMDECL(int)  PGMHandlerPhysicalPageTempOff(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage)
{
    /*
     * Validate the range.
     */
    PPGMPHYSHANDLER pCur = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTXSUFF(pTrees)->PhysHandlers, GCPhys);
    if (pCur)
    {
        if (    GCPhysPage >= pCur->Core.Key
            &&  GCPhysPage <= pCur->Core.KeyLast)
        {
            /*
             * Ok, check that the type is right and then clear the flag.
             */
            unsigned fFlag;
            switch (pCur->enmType)
            {
                case PGMPHYSHANDLERTYPE_PHYSICAL_WRITE:
                    fFlag = MM_RAM_FLAGS_PHYSICAL_WRITE;
                    break;

                case PGMPHYSHANDLERTYPE_PHYSICAL_ALL:
                    fFlag = MM_RAM_FLAGS_PHYSICAL_ALL;
                    break;

                case PGMPHYSHANDLERTYPE_MMIO:
                case PGMPHYSHANDLERTYPE_PHYSICAL:
                    AssertMsgFailed(("Cannot disable an MMIO or natural PHYSICAL access handler! enmType=%d\n", pCur->enmType));
                    return VERR_ACCESS_DENIED;

                default:
                    AssertMsgFailed(("Invalid mapping type %d\n", pCur->enmType));
                    return VERR_INTERNAL_ERROR;
            }

            /** @todo add a function which does both clear and set! */
            /* clear and set */
            PPGMRAMRANGE pHint = NULL;
            int rc = PGMRamFlagsClearByGCPhysWithHint(&pVM->pgm.s, GCPhysPage, fFlag, &pHint);
            if (VBOX_SUCCESS(rc))
                rc = PGMRamFlagsSetByGCPhysWithHint(&pVM->pgm.s, GCPhysPage, MM_RAM_FLAGS_PHYSICAL_TEMP_OFF, &pHint);
            return rc;
        }
        AssertMsgFailed(("The page %#x is outside the range %#x-%#x\n",
                         GCPhysPage, pCur->Core.Key, pCur->Core.KeyLast));
        return VERR_INVALID_PARAMETER;
    }

    AssertMsgFailed(("Specified physical handler start address %#x is invalid.\n", GCPhys));
    return VERR_PGM_HANDLER_NOT_FOUND;
}


/**
 * Turns access monitoring of a page within a monitored
 * physical write/all page access handler regio back on.
 *
 * The caller must do required page table modifications.
 *
 * @returns VBox status code.
 * @param   pVM         VM Handle
 * @param   GCPhys      Start physical address earlier passed to PGMR3HandlerPhysicalRegister().
 * @param   GCPhysPage  Physical address of the page to turn on access monitoring for.
 */
PGMDECL(int)  PGMHandlerPhysicalPageReset(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage)
{
    /*
     * Validate the range.
     */
    PPGMPHYSHANDLER pCur = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTXSUFF(pTrees)->PhysHandlers, GCPhys);
    if (pCur)
    {
        if (    GCPhysPage >= pCur->Core.Key
            &&  GCPhysPage <= pCur->Core.KeyLast)
        {
            /*
             * Ok, check that the type is right and then clear the flag.
             */
            unsigned fFlag;
            switch (pCur->enmType)
            {
                case PGMPHYSHANDLERTYPE_PHYSICAL_WRITE:
                    fFlag = MM_RAM_FLAGS_PHYSICAL_WRITE;
                    break;

                case PGMPHYSHANDLERTYPE_PHYSICAL_ALL:
                    fFlag = MM_RAM_FLAGS_PHYSICAL_ALL;
                    break;

                case PGMPHYSHANDLERTYPE_MMIO:
                case PGMPHYSHANDLERTYPE_PHYSICAL:
                    AssertMsgFailed(("Cannot enable an MMIO or natural PHYSICAL access handler! enmType=%d\n", pCur->enmType));
                    return VERR_ACCESS_DENIED;

                default:
                    AssertMsgFailed(("Invalid mapping type %d\n", pCur->enmType));
                    return VERR_INTERNAL_ERROR;
            }

            /** @todo add a function which does both clear and set! */
            /* set and clear */
            PPGMRAMRANGE pHint = NULL;
            int rc = PGMRamFlagsSetByGCPhysWithHint(&pVM->pgm.s, GCPhysPage, fFlag, &pHint);
            if (VBOX_SUCCESS(rc))
                rc = PGMRamFlagsClearByGCPhysWithHint(&pVM->pgm.s, GCPhysPage, MM_RAM_FLAGS_PHYSICAL_TEMP_OFF, &pHint);
            return rc;

        }
        AssertMsgFailed(("The page %#x is outside the range %#x-%#x\n",
                         GCPhysPage, pCur->Core.Key, pCur->Core.KeyLast));
        return VERR_INVALID_PARAMETER;
    }

    AssertMsgFailed(("Specified physical handler start address %#x is invalid.\n", GCPhys));
    return VERR_PGM_HANDLER_NOT_FOUND;
}


/**
 * Checks if a physical range is handled
 *
 * @returns boolean
 * @param   pVM         VM Handle
 * @param   GCPhys      Start physical address earlier passed to PGMR3HandlerPhysicalRegister().
 */
PGMDECL(bool) PGMHandlerPhysicalIsRegistered(PVM pVM, RTGCPHYS GCPhys)
{
    /*
     * Find the handler.
     */
    PPGMPHYSHANDLER pCur = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTXSUFF(pTrees)->PhysHandlers, GCPhys);
    if (pCur)
    {
        if (    GCPhys >= pCur->Core.Key
            &&  GCPhys <= pCur->Core.KeyLast)
        {
            /*
             * Validate type.
             */
            switch (pCur->enmType)
            {
                case PGMPHYSHANDLERTYPE_PHYSICAL_WRITE:
                case PGMPHYSHANDLERTYPE_PHYSICAL_ALL:
                case PGMPHYSHANDLERTYPE_PHYSICAL:
                case PGMPHYSHANDLERTYPE_MMIO:
                    return true;

                default:
                    AssertMsgFailed(("Invalid type %d! Corruption!\n",  pCur->enmType));
                    return false;
            }
        }
    }

    return false;
}


#ifdef VBOX_STRICT
DECLCALLBACK(int) pgmVirtHandlerDumpPhysRange(PAVLROGCPHYSNODECORE pNode, void *pvUser)
{
    PPGMPHYS2VIRTHANDLER pCur = (PPGMPHYS2VIRTHANDLER)pNode;
    PPGMVIRTHANDLER       pVirt = (PPGMVIRTHANDLER)((uintptr_t)pCur + pCur->offVirtHandler);
    Log(("PHYS2VIRT: Range %VGp-%VGp for virtual handler: %s\n", pCur->Core.Key, pCur->Core.KeyLast, pVirt->pszDesc));
    return 0;
}


void pgmHandlerVirtualDumpPhysPages(PVM pVM)
{
    RTAvlroGCPhysDoWithAll(CTXSUFF(&pVM->pgm.s.pTrees)->PhysToVirtHandlers, true, pgmVirtHandlerDumpPhysRange, 0);
}
#endif /* VBOX_STRICT */


/**
 * Gets the current CR3 register value for the shadow memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
PGMDECL(uint32_t) PGMGetHyperCR3(PVM pVM)
{
    switch (pVM->pgm.s.enmShadowMode)
    {
        case PGMMODE_32_BIT:
            return pVM->pgm.s.HCPhys32BitPD;

        case PGMMODE_PAE:
        case PGMMODE_PAE_NX:
            return pVM->pgm.s.HCPhysPaePDPTR;

        case PGMMODE_AMD64:
        case PGMMODE_AMD64_NX:
            return pVM->pgm.s.HCPhysPaePML4;

        default:
            AssertMsgFailed(("enmShadowMode=%d\n", pVM->pgm.s.enmShadowMode));
            return ~0;
    }
}


/**
 * Gets the CR3 register value for the 32-Bit shadow memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
PGMDECL(uint32_t) PGMGetHyper32BitCR3(PVM pVM)
{
    return pVM->pgm.s.HCPhys32BitPD;
}


/**
 * Gets the CR3 register value for the PAE shadow memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
PGMDECL(uint32_t) PGMGetHyperPaeCR3(PVM pVM)
{
    return pVM->pgm.s.HCPhysPaePDPTR;
}


/**
 * Gets the CR3 register value for the AMD64 shadow memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
PGMDECL(uint32_t) PGMGetHyperAmd64CR3(PVM pVM)
{
    return pVM->pgm.s.HCPhysPaePML4;
}


/**
 * Gets the current CR3 register value for the HC intermediate memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
PGMDECL(uint32_t) PGMGetInterHCCR3(PVM pVM)
{
    switch (pVM->pgm.s.enmHostMode)
    {
        case SUPPAGINGMODE_32_BIT:
        case SUPPAGINGMODE_32_BIT_GLOBAL:
            return pVM->pgm.s.HCPhysInterPD;

        case SUPPAGINGMODE_PAE:
        case SUPPAGINGMODE_PAE_GLOBAL:
        case SUPPAGINGMODE_PAE_NX:
        case SUPPAGINGMODE_PAE_GLOBAL_NX:
            return pVM->pgm.s.HCPhysInterPaePDPTR;

        case SUPPAGINGMODE_AMD64:
        case SUPPAGINGMODE_AMD64_GLOBAL:
        case SUPPAGINGMODE_AMD64_NX:
        case SUPPAGINGMODE_AMD64_GLOBAL_NX:
            return pVM->pgm.s.HCPhysInterPaePDPTR;

        default:
            AssertMsgFailed(("enmHostMode=%d\n", pVM->pgm.s.enmHostMode));
            return ~0;
    }
}


/**
 * Gets the current CR3 register value for the GC intermediate memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
PGMDECL(uint32_t) PGMGetInterGCCR3(PVM pVM)
{
    switch (pVM->pgm.s.enmShadowMode)
    {
        case PGMMODE_32_BIT:
            return pVM->pgm.s.HCPhysInterPD;

        case PGMMODE_PAE:
        case PGMMODE_PAE_NX:
            return pVM->pgm.s.HCPhysInterPaePDPTR;

        case PGMMODE_AMD64:
        case PGMMODE_AMD64_NX:
            return pVM->pgm.s.HCPhysInterPaePML4;

        default:
            AssertMsgFailed(("enmShadowMode=%d\n", pVM->pgm.s.enmShadowMode));
            return ~0;
    }
}


/**
 * Gets the CR3 register value for the 32-Bit intermediate memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
PGMDECL(uint32_t) PGMGetInter32BitCR3(PVM pVM)
{
    return pVM->pgm.s.HCPhysInterPD;
}


/**
 * Gets the CR3 register value for the PAE intermediate memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
PGMDECL(uint32_t) PGMGetInterPaeCR3(PVM pVM)
{
    return pVM->pgm.s.HCPhysInterPaePDPTR;
}


/**
 * Gets the CR3 register value for the AMD64 intermediate memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
PGMDECL(uint32_t) PGMGetInterAmd64CR3(PVM pVM)
{
    return pVM->pgm.s.HCPhysInterPaePML4;
}


/**
 * Performs and schedules necessary updates following a CR3 load or reload.
 *
 * This will normally involve mapping the guest PD or nPDPTR
 *
 * @returns VBox status code.
 * @retval  VINF_PGM_SYNC_CR3 if monitoring requires a CR3 sync. This can
 *          safely be ignored and overridden since the FF will be set too then.
 * @param   pVM         VM handle.
 * @param   cr3         The new cr3.
 * @param   fGlobal     Indicates whether this is a global flush or not.
 */
PGMDECL(int) PGMFlushTLB(PVM pVM, uint32_t cr3, bool fGlobal)
{
    /*
     * When in real or protected mode there is no TLB flushing, but
     * we may still be called because of REM not caring/knowing this.
     * REM is simple and we wish to keep it that way.
     */
    if (pVM->pgm.s.enmGuestMode <= PGMMODE_PROTECTED)
        return VINF_SUCCESS;
    LogFlow(("PGMFlushTLB: cr3=%#x OldCr3=%#x fGlobal=%d\n", cr3, pVM->pgm.s.GCPhysCR3, fGlobal));
    STAM_PROFILE_START(&pVM->pgm.s.StatFlushTLB, a);

    /*
     * Flag the necessary updates.
     */
    VM_FF_SET(pVM, VM_FF_PGM_SYNC_CR3_NON_GLOBAL);
    if (fGlobal)
        VM_FF_SET(pVM, VM_FF_PGM_SYNC_CR3);

    /*
     * Remap the CR3 content and adjust the monitoring if CR3 was actually changed.
     */
    int rc = VINF_SUCCESS;
    RTGCPHYS GCPhysCR3;
    if (    pVM->pgm.s.enmGuestMode == PGMMODE_PAE
        ||  pVM->pgm.s.enmGuestMode == PGMMODE_PAE_NX
        ||  pVM->pgm.s.enmGuestMode == PGMMODE_AMD64
        ||  pVM->pgm.s.enmGuestMode == PGMMODE_AMD64_NX)
        GCPhysCR3 = (RTGCPHYS)(cr3 & X86_CR3_PAE_PAGE_MASK);
    else
        GCPhysCR3 = (RTGCPHYS)(cr3 & X86_CR3_PAGE_MASK);
    if (pVM->pgm.s.GCPhysCR3 != GCPhysCR3)
    {
        pVM->pgm.s.GCPhysCR3 = GCPhysCR3;
        rc = PGM_GST_PFN(MapCR3, pVM)(pVM, GCPhysCR3);
        if (VBOX_SUCCESS(rc) && !pVM->pgm.s.fMappingsFixed)
        {
            pVM->pgm.s.fSyncFlags &= ~PGM_SYNC_MONITOR_CR3;
            rc = PGM_GST_PFN(MonitorCR3, pVM)(pVM, GCPhysCR3);
        }
        if (fGlobal)
            STAM_COUNTER_INC(&pVM->pgm.s.StatFlushTLBNewCR3Global);
        else
            STAM_COUNTER_INC(&pVM->pgm.s.StatFlushTLBNewCR3);
    }
    else
    {
        /*
         * Check if we have a pending update of the CR3 monitoring.
         */
        if (pVM->pgm.s.fSyncFlags & PGM_SYNC_MONITOR_CR3)
        {
            pVM->pgm.s.fSyncFlags &= ~PGM_SYNC_MONITOR_CR3;
            Assert(!pVM->pgm.s.fMappingsFixed);
            rc = PGM_GST_PFN(MonitorCR3, pVM)(pVM, GCPhysCR3);
        }
        if (fGlobal)
            STAM_COUNTER_INC(&pVM->pgm.s.StatFlushTLBSameCR3Global);
        else
            STAM_COUNTER_INC(&pVM->pgm.s.StatFlushTLBSameCR3);
    }

    STAM_PROFILE_STOP(&pVM->pgm.s.StatFlushTLB, a);
    return rc;
}


/**
 * Synchronize the paging structures.
 *
 * This function is called in response to the VM_FF_PGM_SYNC_CR3 and
 * VM_FF_PGM_SYNC_CR3_NONGLOBAL. Those two force action flags are set
 * in several places, most importantly whenever the CR3 is loaded.
 *
 * @returns VBox status code.
 * @param   pVM         The virtual machine.
 * @param   cr0         Guest context CR0 register
 * @param   cr3         Guest context CR3 register
 * @param   cr4         Guest context CR4 register
 * @param   fGlobal     Including global page directories or not
 */
PGMDECL(int) PGMSyncCR3(PVM pVM, uint32_t cr0, uint32_t cr3, uint32_t cr4, bool fGlobal)
{
    /*
     * We might be called when we shouldn't.
     *
     * The mode switching will ensure that the PD is resynced
     * after every mode switch. So, if we find ourselves here
     * when in protected or real mode we can safely disable the
     * FF and return immediately.
     */
    if (pVM->pgm.s.enmGuestMode <= PGMMODE_PROTECTED)
    {
        Assert((cr0 & (X86_CR0_PG | X86_CR0_PE)) != (X86_CR0_PG | X86_CR0_PE));
        VM_FF_CLEAR(pVM, VM_FF_PGM_SYNC_CR3);
        VM_FF_CLEAR(pVM, VM_FF_PGM_SYNC_CR3_NON_GLOBAL);
        return VINF_SUCCESS;
    }

    /* If global pages are not supported, then all flushes are global */
    if (!(cr4 & X86_CR4_PGE))
        fGlobal = true;
    LogFlow(("PGMSyncCR3: cr0=%08x cr3=%08x cr4=%08x fGlobal=%d[%d,%d]\n", cr0, cr3, cr4, fGlobal,
             VM_FF_ISSET(pVM, VM_FF_PGM_SYNC_CR3), VM_FF_ISSET(pVM, VM_FF_PGM_SYNC_CR3_NON_GLOBAL)));

    /*
     * Let the 'Bth' function do the work and we'll just keep track of the flags.
     */
    STAM_PROFILE_START(&pVM->pgm.s.CTXMID(Stat,SyncCR3), a);
    int rc = PGM_BTH_PFN(SyncCR3, pVM)(pVM, cr0, cr3, cr4, fGlobal);
    STAM_PROFILE_STOP(&pVM->pgm.s.CTXMID(Stat,SyncCR3), a);
    AssertMsg(rc == VINF_SUCCESS || rc == VINF_PGM_SYNC_CR3 || VBOX_FAILURE(rc), ("rc=%VRc\n", rc));
    if (rc == VINF_SUCCESS)
    {
        if (!(pVM->pgm.s.fSyncFlags & PGM_SYNC_ALWAYS))
        {
            VM_FF_CLEAR(pVM, VM_FF_PGM_SYNC_CR3);
            VM_FF_CLEAR(pVM, VM_FF_PGM_SYNC_CR3_NON_GLOBAL);
        }

        /*
         * Check if we have a pending update of the CR3 monitoring.
         */
        if (pVM->pgm.s.fSyncFlags & PGM_SYNC_MONITOR_CR3)
        {
            pVM->pgm.s.fSyncFlags &= ~PGM_SYNC_MONITOR_CR3;
            Assert(!pVM->pgm.s.fMappingsFixed);
            Assert(pVM->pgm.s.GCPhysCR3 == pVM->pgm.s.GCPhysGstCR3Monitored);
            rc = PGM_GST_PFN(MonitorCR3, pVM)(pVM, pVM->pgm.s.GCPhysCR3);
        }
    }

    /*
     * Now flush the CR3 (guest context).
     */
    if (rc == VINF_SUCCESS)
        PGM_INVL_GUEST_TLBS();
    return rc;
}


/**
 * Called whenever CR0 or CR4 in a way which may change
 * the paging mode.
 *
 * @returns VBox status code fit for scheduling in GC and R0.
 * @retval  VINF_SUCCESS if the was no change, or it was successfully dealt with.
 * @retval  VINF_PGM_CHANGE_MODE if we're in GC or R0 and the mode changes.
 * @param   pVM         VM handle.
 * @param   cr0         The new cr0.
 * @param   cr4         The new cr4.
 * @param   efer        The new extended feature enable register.
 */
PGMDECL(int) PGMChangeMode(PVM pVM, uint32_t cr0, uint32_t cr4, uint64_t efer)
{
    PGMMODE enmGuestMode;

    /*
     * Calc the new guest mode.
     */
    if (!(cr0 & X86_CR0_PE))
        enmGuestMode = PGMMODE_REAL;
    else if (!(cr0 & X86_CR0_PG))
        enmGuestMode = PGMMODE_PROTECTED;
    else if (!(cr4 & X86_CR4_PAE))
        enmGuestMode = PGMMODE_32_BIT;
    else if (!(efer & MSR_K6_EFER_LME))
    {
        if (!(efer & MSR_K6_EFER_NXE))
            enmGuestMode = PGMMODE_PAE;
        else
            enmGuestMode = PGMMODE_PAE_NX;
    }
    else
    {
        if (!(efer & MSR_K6_EFER_NXE))
            enmGuestMode = PGMMODE_AMD64;
        else
            enmGuestMode = PGMMODE_AMD64_NX;
    }

    /*
     * Did it change?
     */
    if (pVM->pgm.s.enmGuestMode == enmGuestMode)
        return VINF_SUCCESS;
#ifdef IN_RING3
    return pgmR3ChangeMode(pVM, enmGuestMode);
#else
    Log(("PGMChangeMode: returns VINF_PGM_CHANGE_MODE.\n"));
    return VINF_PGM_CHANGE_MODE;
#endif
}


/**
 * Gets the current guest paging mode.
 *
 * @returns The current paging mode.
 * @param   pVM             The VM handle.
 */
PGMDECL(PGMMODE) PGMGetGuestMode(PVM pVM)
{
    return pVM->pgm.s.enmGuestMode;
}


/**
 * Get mode name.
 *
 * @returns read-only name string.
 * @param   enmMode     The mode which name is desired.
 */
PGMDECL(const char *) PGMGetModeName(PGMMODE enmMode)
{
    switch (enmMode)
    {
        case PGMMODE_REAL:      return "real";
        case PGMMODE_PROTECTED: return "protected";
        case PGMMODE_32_BIT:    return "32-bit";
        case PGMMODE_PAE:       return "PAE";
        case PGMMODE_PAE_NX:    return "PAE+NX";
        case PGMMODE_AMD64:     return "AMD64";
        case PGMMODE_AMD64_NX:  return "AMD64+NX";
        default:                return "unknown mode value";
    }
}


/**
 * Acquire the PGM lock.
 *
 * @returns VBox status code
 * @param   pVM         The VM to operate on.
 */
int pgmLock(PVM pVM)
{
    int rc = PDMCritSectEnter(&pVM->pgm.s.CritSect, VERR_SEM_BUSY);
#ifdef IN_GC
    if (rc == VERR_SEM_BUSY)
        rc = VMMGCCallHost(pVM, VMMCALLHOST_PGM_LOCK, 0);
#elif defined(IN_RING0)
    if (rc == VERR_SEM_BUSY)
        rc = VMMR0CallHost(pVM, VMMCALLHOST_PGM_LOCK, 0);
#endif
    AssertRC(rc);
    return rc;
}


/**
 * Release the PGM lock.
 *
 * @returns VBox status code
 * @param   pVM         The VM to operate on.
 */
void pgmUnlock(PVM pVM)
{
    PDMCritSectLeave(&pVM->pgm.s.CritSect);
}


#ifdef VBOX_STRICT

/**
 * State structure used by the PGMAssertHandlerAndFlagsInSync() function
 * and its AVL enumerators.
 */
typedef struct PGMAHAFIS
{
    /** The VM handle. */
    PVM         pVM;
    /** Number of errors. */
    unsigned    cErrors;
    /** The flags we've found. */
    unsigned    fFlagsFound;
    /** The flags we're matching up to.
     * This is also on the stack as a const, thus only valid during enumeration. */
    unsigned    fFlags;
    /** The current physical address. */
    RTGCPHYS    GCPhys;
} PGMAHAFIS, *PPGMAHAFIS;

/**
 * Verify virtual handler by matching physical address.
 *
 * @returns 0
 * @param   pNode   Pointer to a PGMVIRTHANDLER.
 * @param   pvUser  Pointer to user parameter.
 */
static DECLCALLBACK(int) pgmVirtHandlerVerifyOneByPhysAddr(PAVLROGCPTRNODECORE pNode, void *pvUser)
{
    PPGMVIRTHANDLER pCur = (PPGMVIRTHANDLER)pNode;
    PPGMAHAFIS      pState = (PPGMAHAFIS)pvUser;

    for (unsigned iPage = 0; iPage < pCur->cPages; iPage++)
    {
        if ((pCur->aPhysToVirt[iPage].Core.Key & X86_PTE_PAE_PG_MASK) == pState->GCPhys)
        {
            switch (pCur->enmType)
            {
                case PGMVIRTHANDLERTYPE_EIP:
                case PGMVIRTHANDLERTYPE_NORMAL:     pState->fFlagsFound |= MM_RAM_FLAGS_VIRTUAL_HANDLER; break;
                case PGMVIRTHANDLERTYPE_WRITE:      pState->fFlagsFound |= MM_RAM_FLAGS_VIRTUAL_HANDLER | MM_RAM_FLAGS_VIRTUAL_WRITE; break;
                case PGMVIRTHANDLERTYPE_ALL:        pState->fFlagsFound |= MM_RAM_FLAGS_VIRTUAL_HANDLER | MM_RAM_FLAGS_VIRTUAL_ALL; break;
                /* hypervisor handlers need no flags and wouldn't have nowhere to put them in any case. */
                case PGMVIRTHANDLERTYPE_HYPERVISOR:
                    return 0;
            }
            if (    (pState->fFlags & (MM_RAM_FLAGS_VIRTUAL_HANDLER  | MM_RAM_FLAGS_VIRTUAL_WRITE  | MM_RAM_FLAGS_VIRTUAL_ALL))
                ==  pState->fFlagsFound)
                break;
        }
    }
    return 0;
}


/**
 * Verify a virtual handler.
 *
 * @returns 0
 * @param   pNode   Pointer to a PGMVIRTHANDLER.
 * @param   pvUser  Pointer to user parameter.
 */
static DECLCALLBACK(int) pgmVirtHandlerVerifyOne(PAVLROGCPTRNODECORE pNode, void *pvUser)
{
    PPGMVIRTHANDLER pVirt   = (PPGMVIRTHANDLER)pNode;
    PPGMAHAFIS      pState = (PPGMAHAFIS)pvUser;
    PVM             pVM     = pState->pVM;

    if (    pVirt->aPhysToVirt[0].Core.Key != NIL_RTGCPHYS
        &&  (pVirt->aPhysToVirt[0].Core.Key & PAGE_OFFSET_MASK) != ((RTGCUINTPTR)pVirt->GCPtr & PAGE_OFFSET_MASK))
    {
        AssertMsgFailed(("virt handler phys out has incorrect key! %VGp %VGv %s\n",
                         pVirt->aPhysToVirt[0].Core.Key, pVirt->GCPtr, HCSTRING(pVirt->pszDesc)));
        pState->cErrors++;
    }

    /*
     * Calc flags.
     */
    unsigned    fFlags;
    switch (pVirt->enmType)
    {
        case PGMVIRTHANDLERTYPE_EIP:
        case PGMVIRTHANDLERTYPE_NORMAL:     fFlags = MM_RAM_FLAGS_VIRTUAL_HANDLER; break;
        case PGMVIRTHANDLERTYPE_WRITE:      fFlags = MM_RAM_FLAGS_VIRTUAL_HANDLER | MM_RAM_FLAGS_VIRTUAL_WRITE; break;
        case PGMVIRTHANDLERTYPE_ALL:        fFlags = MM_RAM_FLAGS_VIRTUAL_HANDLER | MM_RAM_FLAGS_VIRTUAL_ALL; break;
        /* hypervisor handlers need no flags and wouldn't have nowhere to put them in any case. */
        case PGMVIRTHANDLERTYPE_HYPERVISOR:
            return 0;
        default:
            AssertMsgFailed(("unknown enmType=%d\n", pVirt->enmType));
            return 0;
    }

    /*
     * Check pages against flags.
     */
    RTGCUINTPTR   GCPtr = (RTGCUINTPTR)pVirt->GCPtr;
    for (unsigned iPage = 0; iPage < pVirt->cPages; iPage++, GCPtr += PAGE_SIZE)
    {
        RTGCPHYS   GCPhysGst;
        uint64_t   fGst;
        int rc = PGMGstGetPage(pVM, (RTGCPTR)GCPtr, &fGst, &GCPhysGst);
        if (rc == VERR_PAGE_NOT_PRESENT)
        {
            if (pVirt->aPhysToVirt[iPage].Core.Key != NIL_RTGCPHYS)
            {
                AssertMsgFailed(("virt handler phys out of sync. %VGp GCPhysNew=~0 iPage=%#x %VGv %s\n",
                                 pVirt->aPhysToVirt[iPage].Core.Key, iPage, GCPtr, HCSTRING(pVirt->pszDesc)));
                pState->cErrors++;
            }
            continue;
        }

        AssertRCReturn(rc, 0);
        if ((pVirt->aPhysToVirt[iPage].Core.Key & X86_PTE_PAE_PG_MASK) != GCPhysGst)
        {
            AssertMsgFailed(("virt handler phys out of sync. %VGp GCPhysGst=%VGp iPage=%#x %VGv %s\n",
                             pVirt->aPhysToVirt[iPage].Core.Key, GCPhysGst, iPage, GCPtr, HCSTRING(pVirt->pszDesc)));
            pState->cErrors++;
            continue;
        }

        RTHCPHYS HCPhys;
        rc = PGMRamGCPhys2HCPhysWithFlags(&pVM->pgm.s, GCPhysGst, &HCPhys);
        if (VBOX_FAILURE(rc))
        {
            AssertMsgFailed(("virt handler getting ram flags rc=%Vrc. GCPhysGst=%VGp iPage=%#x %VGv %s\n",
                             rc, GCPhysGst, iPage, GCPtr, HCSTRING(pVirt->pszDesc)));
            pState->cErrors++;
            continue;
        }

        if ((HCPhys & fFlags) != fFlags)
        {
            AssertMsgFailed(("virt handler flags mismatch. HCPhys=%VHp fFlags=%#x GCPhysGst=%VGp iPage=%#x %VGv %s\n",
                             HCPhys, fFlags, GCPhysGst, iPage, GCPtr, HCSTRING(pVirt->pszDesc)));
            pState->cErrors++;
            continue;
        }
    } /* for pages in virtual mapping. */

    return 0;
}


/**
 * Asserts that the handlers+guest-page-tables == ramrange-flags and
 * that the physical addresses associated with virtual handlers are correct.
 *
 * @returns Number of mismatches.
 * @param   pVM     The VM handle.
 */
PGMDECL(unsigned) PGMAssertHandlerAndFlagsInSync(PVM pVM)
{
    PPGM        pPGM = &pVM->pgm.s;
    PGMAHAFIS   State;
    State.cErrors = 0;
    State.pVM     = pVM;

    /*
     * Check the RAM flags against the handlers.
     */
    for (PPGMRAMRANGE pRam = CTXSUFF(pPGM->pRamRanges); pRam; pRam = CTXSUFF(pRam->pNext))
    {
        const unsigned cPages = pRam->cb >> PAGE_SHIFT;
        for (unsigned iPage = 0; iPage < cPages; iPage++)
        {
            State.GCPhys = pRam->GCPhys + (iPage << PAGE_SHIFT);
            const unsigned fFlags = pRam->aHCPhys[iPage]
                                   & (  MM_RAM_FLAGS_VIRTUAL_HANDLER  | MM_RAM_FLAGS_VIRTUAL_WRITE  | MM_RAM_FLAGS_VIRTUAL_ALL
                                      | MM_RAM_FLAGS_PHYSICAL_HANDLER | MM_RAM_FLAGS_PHYSICAL_WRITE | MM_RAM_FLAGS_PHYSICAL_ALL | MM_RAM_FLAGS_PHYSICAL_TEMP_OFF);
            if (fFlags)
            {
                State.fFlagsFound = 0; /* build flags and compare. */

                /* physical first. (simple because of page alignment) */
                if (    !(fFlags & MM_RAM_FLAGS_PHYSICAL_TEMP_OFF)
                    &&  (fFlags & (MM_RAM_FLAGS_PHYSICAL_HANDLER | MM_RAM_FLAGS_PHYSICAL_WRITE | MM_RAM_FLAGS_PHYSICAL_ALL)))
                {
                    PPGMPHYSHANDLER pPhys = (PPGMPHYSHANDLER)RTAvlroGCPhysRangeGet(&pPGM->CTXSUFF(pTrees)->PhysHandlers, State.GCPhys);
                    if (!pPhys)
                    {
                        pPhys = (PPGMPHYSHANDLER)RTAvlroGCPhysGetBestFit(&pPGM->CTXSUFF(pTrees)->PhysHandlers, State.GCPhys, true);
                        if (    pPhys
                            &&  pPhys->Core.Key > (State.GCPhys + PAGE_SIZE - 1))
                            pPhys = NULL;
                        Assert(!pPhys || pPhys->Core.Key >= State.GCPhys);
                    }
                    if (pPhys)
                    {
                        switch (pPhys->enmType)
                        {
                            case PGMPHYSHANDLERTYPE_PHYSICAL:       State.fFlagsFound |= MM_RAM_FLAGS_PHYSICAL_HANDLER; break;
                            case PGMPHYSHANDLERTYPE_PHYSICAL_WRITE: State.fFlagsFound |= MM_RAM_FLAGS_PHYSICAL_HANDLER | MM_RAM_FLAGS_PHYSICAL_WRITE; break;
                            case PGMPHYSHANDLERTYPE_MMIO:
                            case PGMPHYSHANDLERTYPE_PHYSICAL_ALL:   State.fFlagsFound |= MM_RAM_FLAGS_PHYSICAL_HANDLER | MM_RAM_FLAGS_PHYSICAL_ALL; break;
                            default: AssertMsgFailed(("Invalid type phys type %d\n", pPhys->enmType)); State.cErrors++; break;
                        }
                        if (    (fFlags & (MM_RAM_FLAGS_PHYSICAL_HANDLER | MM_RAM_FLAGS_PHYSICAL_WRITE | MM_RAM_FLAGS_PHYSICAL_ALL))
                            !=  State.fFlagsFound)
                        {
                            AssertMsgFailed(("ram range vs phys handler flags mismatch. GCPhys=%#x fFlags=%#x fFlagsFound=%#x %s\n",
                                             State.GCPhys, fFlags, State.fFlagsFound, pPhys->pszDesc));
                            State.cErrors++;
                        }

#ifdef IN_RING3
                        /* validate that REM is handling it. */
                        if (!REMR3IsPageAccessHandled(pVM, State.GCPhys))
                        {
                            AssertMsgFailed(("ram range vs phys handler REM mismatch. GCPhys=%#x fFlags=%#x %s\n",
                                             State.GCPhys, fFlags, pPhys->pszDesc));
                            State.cErrors++;
                        }
#endif
                    }
                    else
                    {
                        AssertMsgFailed(("ram range vs phys handler mismatch. no handler for GCPhys=%#x\n", State.GCPhys));
                        State.cErrors++;
                    }
                }

                /* virtual flags. */
                if (fFlags & (MM_RAM_FLAGS_VIRTUAL_HANDLER  | MM_RAM_FLAGS_VIRTUAL_WRITE  | MM_RAM_FLAGS_VIRTUAL_ALL))
                {
                    State.fFlags      = fFlags;
                    RTAvlroGCPtrDoWithAll(CTXSUFF(&pVM->pgm.s.pTrees)->VirtHandlers, true, pgmVirtHandlerVerifyOneByPhysAddr, &State);
                    if (    (fFlags & (MM_RAM_FLAGS_VIRTUAL_HANDLER  | MM_RAM_FLAGS_VIRTUAL_WRITE  | MM_RAM_FLAGS_VIRTUAL_ALL))
                        !=  State.fFlagsFound)
                    {
                        AssertMsgFailed(("ram range vs virt handler flags mismatch. GCPhys=%#x fFlags=%#x fFlagsFound=%#x\n",
                                         State.GCPhys, fFlags, State.fFlagsFound));
                        State.cErrors++;
                    }

                }
            }
        } /* foreach page in ram range. */
    } /* foreach ram range. */

    /*
     * Check that the physical addresses of the virtual handlers matches up.
     */
    RTAvlroGCPtrDoWithAll(CTXSUFF(&pVM->pgm.s.pTrees)->VirtHandlers, true, pgmVirtHandlerVerifyOne, &State);

    return State.cErrors;
}


/**
 * Asserts that there are no mapping conflicts.
 *
 * @returns Number of conflicts.
 * @param   pVM     The VM Handle.
 */
PGMDECL(unsigned) PGMAssertNoMappingConflicts(PVM pVM)
{
    unsigned cErrors = 0;

    /*
     * Check for mapping conflicts.
     */
    for (PPGMMAPPING pMapping = CTXSUFF(pVM->pgm.s.pMappings);
         pMapping;
         pMapping = CTXSUFF(pMapping->pNext))
    {
        /** @todo This is slow and should be optimized, but since it's just assertions I don't care now. */
        for (RTGCUINTPTR GCPtr = (RTGCUINTPTR)pMapping->GCPtr;
              GCPtr <= (RTGCUINTPTR)pMapping->GCPtrLast;
              GCPtr += PAGE_SIZE)
        {
            int rc = PGMGstGetPage(pVM, (RTGCPTR)GCPtr, NULL, NULL);
            if (rc != VERR_PAGE_TABLE_NOT_PRESENT)
            {
                AssertMsgFailed(("Conflict at %VGv with %s\n", GCPtr, HCSTRING(pMapping->pszDesc)));
                cErrors++;
                break;
            }
        }
    }

    return cErrors;
}


/**
 * Asserts that everything related to the guest CR3 is correctly shadowed.
 *
 * This will call PGMAssertNoMappingConflicts() and PGMAssertHandlerAndFlagsInSync(),
 * and assert the correctness of the guest CR3 mapping before asserting that the
 * shadow page tables is in sync with the guest page tables.
 *
 * @returns Number of conflicts.
 * @param   pVM     The VM Handle.
 * @param   cr3     The current guest CR3 register value.
 * @param   cr4     The current guest CR4 register value.
 */
PGMDECL(unsigned) PGMAssertCR3(PVM pVM, uint32_t cr3, uint32_t cr4)
{
    STAM_PROFILE_START(&pVM->pgm.s.CTXMID(Stat,SyncCR3), a);
    unsigned cErrors = PGM_BTH_PFN(AssertCR3, pVM)(pVM, cr3, cr4, 0, ~(RTUINTPTR)0);
    STAM_PROFILE_STOP(&pVM->pgm.s.CTXMID(Stat,SyncCR3), a);
    return cErrors;
}

#endif /* VBOX_STRICT */
