/* $Id$ */
/** @file
 * PGM - Page Manager and Monitor - All context code.
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
#include <VBox/cpum.h>
#include <VBox/selm.h>
#include <VBox/iom.h>
#include <VBox/sup.h>
#include <VBox/mm.h>
#include <VBox/stam.h>
#include <VBox/csam.h>
#include <VBox/patm.h>
#include <VBox/trpm.h>
#include <VBox/rem.h>
#include <VBox/em.h>
#include <VBox/hwaccm.h>
#include <VBox/hwacc_vmx.h>
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


#ifndef IN_GC /* AMD64 implies VT-x/AMD-V */
/*
 * Shadow - AMD64 mode
 */
#define PGM_SHW_TYPE                PGM_TYPE_AMD64
#define PGM_SHW_NAME(name)          PGM_SHW_NAME_AMD64(name)
#include "PGMAllShw.h"

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

/*
 * Shadow - Nested paging mode
 */
#define PGM_SHW_TYPE                PGM_TYPE_NESTED
#define PGM_SHW_NAME(name)          PGM_SHW_NAME_NESTED(name)
#include "PGMAllShw.h"

/* Guest - real mode */
#define PGM_GST_TYPE                PGM_TYPE_REAL
#define PGM_GST_NAME(name)          PGM_GST_NAME_REAL(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_REAL(name)
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - protected mode */
#define PGM_GST_TYPE                PGM_TYPE_PROT
#define PGM_GST_NAME(name)          PGM_GST_NAME_PROT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_PROT(name)
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - 32-bit mode */
#define PGM_GST_TYPE                PGM_TYPE_32BIT
#define PGM_GST_NAME(name)          PGM_GST_NAME_32BIT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_32BIT(name)
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - PAE mode */
#define PGM_GST_TYPE                PGM_TYPE_PAE
#define PGM_GST_NAME(name)          PGM_GST_NAME_PAE(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_PAE(name)
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - AMD64 mode */
#define PGM_GST_TYPE                PGM_TYPE_AMD64
#define PGM_GST_NAME(name)          PGM_GST_NAME_AMD64(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_AMD64(name)
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

#undef PGM_SHW_TYPE
#undef PGM_SHW_NAME

/*
 * Shadow - EPT
 */
#define PGM_SHW_TYPE                PGM_TYPE_EPT
#define PGM_SHW_NAME(name)          PGM_SHW_NAME_EPT(name)
#include "PGMAllShw.h"

/* Guest - real mode */
#define PGM_GST_TYPE                PGM_TYPE_REAL
#define PGM_GST_NAME(name)          PGM_GST_NAME_REAL(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_EPT_REAL(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_EPT_PT_FOR_PHYS
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - protected mode */
#define PGM_GST_TYPE                PGM_TYPE_PROT
#define PGM_GST_NAME(name)          PGM_GST_NAME_PROT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_EPT_PROT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_EPT_PT_FOR_PHYS
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - 32-bit mode */
#define PGM_GST_TYPE                PGM_TYPE_32BIT
#define PGM_GST_NAME(name)          PGM_GST_NAME_32BIT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_EPT_32BIT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_EPT_PT_FOR_PHYS
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - PAE mode */
#define PGM_GST_TYPE                PGM_TYPE_PAE
#define PGM_GST_NAME(name)          PGM_GST_NAME_PAE(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_EPT_PAE(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_EPT_PT_FOR_PHYS
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - AMD64 mode */
#define PGM_GST_TYPE                PGM_TYPE_AMD64
#define PGM_GST_NAME(name)          PGM_GST_NAME_AMD64(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_EPT_AMD64(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_EPT_PT_FOR_PHYS
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

#undef PGM_SHW_TYPE
#undef PGM_SHW_NAME

#endif /* !IN_GC */


#ifndef IN_RING3
/**
 * #PF Handler.
 *
 * @returns VBox status code (appropriate for trap handling and GC return).
 * @param   pVM         VM Handle.
 * @param   uErr        The trap error code.
 * @param   pRegFrame   Trap register frame.
 * @param   pvFault     The fault address.
 */
VMMDECL(int)     PGMTrap0eHandler(PVM pVM, RTGCUINT uErr, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault)
{
    LogFlow(("PGMTrap0eHandler: uErr=%RGu pvFault=%VGv eip=%VGv\n", uErr, pvFault, pRegFrame->rip));
    STAM_PROFILE_START(&pVM->pgm.s.StatGCTrap0e, a);
    STAM_STATS({ pVM->pgm.s.CTX_SUFF(pStatTrap0eAttribution) = NULL; } );


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
        else if (uErr & X86_TRAP_PF_ID)
            STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eUSNXE);
        else
            STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eUSRead);
    }
    else
    {   /* Supervisor */
        if (!(uErr & X86_TRAP_PF_P))
        {
            if (uErr & X86_TRAP_PF_RW)
                STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eSVNotPresentWrite);
            else
                STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eSVNotPresentRead);
        }
        else if (uErr & X86_TRAP_PF_RW)
            STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eSVWrite);
        else if (uErr & X86_TRAP_PF_ID)
            STAM_COUNTER_INC(&pVM->pgm.s.StatGCTrap0eSNXE);
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
    STAM_STATS({ if (!pVM->pgm.s.CTX_SUFF(pStatTrap0eAttribution))
                    pVM->pgm.s.CTX_SUFF(pStatTrap0eAttribution) = &pVM->pgm.s.StatTrap0eMisc; });
    STAM_PROFILE_STOP_EX(&pVM->pgm.s.StatGCTrap0e, pVM->pgm.s.CTX_SUFF(pStatTrap0eAttribution), a);
    return rc;
}
#endif /* !IN_RING3 */


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
VMMDECL(int) PGMPrefetchPage(PVM pVM, RTGCPTR GCPtrPage)
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
    PPGMMAPPING pMapping = pVM->pgm.s.CTX_SUFF(pMappings);
    while (pMapping)
    {
        if ((uintptr_t)GCPtr < (uintptr_t)pMapping->GCPtr)
            break;
        if ((uintptr_t)GCPtr - (uintptr_t)pMapping->GCPtr < pMapping->cb)
            return pMapping;
        pMapping = pMapping->CTX_SUFF(pNext);
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
VMMDECL(int) PGMIsValidAccess(PVM pVM, RTGCUINTPTR Addr, uint32_t cbSize, uint32_t fAccess)
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
VMMDECL(int) PGMVerifyAccess(PVM pVM, RTGCUINTPTR Addr, uint32_t cbSize, uint32_t fAccess)
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

    if (!HWACCMIsNestedPagingActive(pVM))
    {
        /*
        * Next step is to verify if we protected this page for dirty bit tracking or for CSAM scanning
        */
        rc = PGMShwGetPage(pVM, (RTGCPTR)Addr, NULL, NULL);
        if (    rc == VERR_PAGE_NOT_PRESENT
            ||  rc == VERR_PAGE_TABLE_NOT_PRESENT)
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
        else
            AssertMsg(rc == VINF_SUCCESS, ("PGMShwGetPage %VGv failed with %Vrc\n", Addr, rc));
    }

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
    {
        /* Don't recursively call PGMVerifyAccess as we might run out of stack. */
        for (;;)
        {
            Addr += PAGE_SIZE;
            if (cbSize > PAGE_SIZE)
                cbSize -= PAGE_SIZE;
            else
                cbSize = 1;
            rc = PGMVerifyAccess(pVM, Addr, 1, fAccess);
            if (rc != VINF_SUCCESS)
                break;
            if (PAGE_ADDRESS(Addr) == PAGE_ADDRESS(Addr + cbSize - 1))
                break;
        }
    }
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
VMMDECL(int) PGMInvalidatePage(PVM pVM, RTGCPTR GCPtrPage)
{
    int rc;

    Log3(("PGMInvalidatePage: GCPtrPage=%VGv\n", GCPtrPage));

    /** @todo merge PGMGCInvalidatePage with this one */

#ifndef IN_RING3
    /*
     * Notify the recompiler so it can record this instruction.
     * Failure happens when it's out of space. We'll return to HC in that case.
     */
    rc = REMNotifyInvalidatePage(pVM, GCPtrPage);
    if (VBOX_FAILURE(rc))
        return rc;
#endif

    STAM_PROFILE_START(&pVM->pgm.s.CTX_MID_Z(Stat,InvalidatePage), a);
    rc = PGM_BTH_PFN(InvalidatePage, pVM)(pVM, GCPtrPage);
    STAM_PROFILE_STOP(&pVM->pgm.s.CTX_MID_Z(Stat,InvalidatePage), a);

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
VMMDECL(int) PGMInterpretInstruction(PVM pVM, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault)
{
    uint32_t cb;
    int rc = EMInterpretInstruction(pVM, pRegFrame, pvFault, &cb);
    if (rc == VERR_EM_INTERPRETER)
        rc = VINF_EM_RAW_EMULATE_INSTR;
    if (rc != VINF_SUCCESS)
        Log(("PGMInterpretInstruction: returns %Rrc (pvFault=%VGv)\n", rc, pvFault));
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
VMMDECL(int) PGMShwGetPage(PVM pVM, RTGCPTR GCPtr, uint64_t *pfFlags, PRTHCPHYS pHCPhys)
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
VMMDECL(int) PGMShwSetPage(PVM pVM, RTGCPTR GCPtr, size_t cb, uint64_t fFlags)
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
VMMDECL(int)  PGMShwModifyPage(PVM pVM, RTGCPTR GCPtr, size_t cb, uint64_t fFlags, uint64_t fMask)
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
 * Syncs the SHADOW page directory pointer for the specified address. Allocates
 * backing pages in case the PDPT entry is missing.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPtr       The address.
 * @param   pGstPdpe    Guest PDPT entry
 * @param   ppPD        Receives address of page directory
 */
VMMDECL(int) PGMShwSyncPAEPDPtr(PVM pVM, RTGCUINTPTR GCPtr, PX86PDPE pGstPdpe, PX86PDPAE *ppPD)
{
    PPGM           pPGM   = &pVM->pgm.s;
    PPGMPOOL       pPool  = pPGM->CTX_SUFF(pPool);
    PPGMPOOLPAGE   pShwPage;
    int            rc;

    Assert(!HWACCMIsNestedPagingActive(pVM));

    const unsigned iPdPt = (GCPtr >> X86_PDPT_SHIFT) & X86_PDPT_MASK_PAE;
    PX86PDPT  pPdpt = pVM->pgm.s.CTXMID(p,PaePDPT);
    PX86PDPE  pPdpe = &pPdpt->a[iPdPt];

    /* Allocate page directory if not present. */
    if (    !pPdpe->n.u1Present
        &&  !(pPdpe->u & X86_PDPE_PG_MASK))
    {
        PX86PDPE pPdptGst = &CTXSUFF(pPGM->pGstPaePDPT)->a[iPdPt];

        Assert(!(pPdpe->u & X86_PDPE_PG_MASK));
        /* Create a reference back to the PDPT by using the index in its shadow page. */
        rc = pgmPoolAlloc(pVM, pPdptGst->u & X86_PDPE_PG_MASK, PGMPOOLKIND_PAE_PD_FOR_PAE_PD, PGMPOOL_IDX_PDPT, iPdPt, &pShwPage);
        if (rc == VERR_PGM_POOL_FLUSHED)
        {
            Assert(pVM->pgm.s.fSyncFlags & PGM_SYNC_CLEAR_PGM_POOL);
            VM_FF_SET(pVM, VM_FF_PGM_SYNC_CR3);
            return VINF_PGM_SYNC_CR3;
        }
        AssertRCReturn(rc, rc);
    }
    else
    {
        pShwPage = pgmPoolGetPage(pPool, pPdpe->u & X86_PDPE_PG_MASK);
        AssertReturn(pShwPage, VERR_INTERNAL_ERROR);
    }
    /* The PD was cached or created; hook it up now. */
    pPdpe->u |=    pShwPage->Core.Key
                | (pGstPdpe->u & ~(X86_PDPE_PG_MASK | X86_PDPE_AVL_MASK | X86_PDPE_PCD | X86_PDPE_PWT));

    *ppPD = (PX86PDPAE)PGMPOOL_PAGE_2_PTR(pVM, pShwPage);
    return VINF_SUCCESS;
}

/**
 * Gets the SHADOW page directory pointer for the specified address.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPtr       The address.
 * @param   ppPdpt      Receives address of pdpt
 * @param   ppPD        Receives address of page directory
 */
VMMDECL(int) PGMShwGetPAEPDPtr(PVM pVM, RTGCUINTPTR GCPtr, PX86PDPT *ppPdpt, PX86PDPAE *ppPD)
{
    PPGM           pPGM   = &pVM->pgm.s;
    PPGMPOOL       pPool  = pPGM->CTX_SUFF(pPool);
    PPGMPOOLPAGE   pShwPage;

    Assert(!HWACCMIsNestedPagingActive(pVM));

    const unsigned iPdPt = (GCPtr >> X86_PDPT_SHIFT) & X86_PDPT_MASK_PAE;
    PX86PDPT  pPdpt = pVM->pgm.s.CTXMID(p,PaePDPT);
    PX86PDPE  pPdpe = &pPdpt->a[iPdPt];

    *ppPdpt = pPdpt;
    if (!pPdpe->n.u1Present)
        return VERR_PAGE_DIRECTORY_PTR_NOT_PRESENT;

    pShwPage = pgmPoolGetPage(pPool, pPdpe->u & X86_PDPE_PG_MASK);
    AssertReturn(pShwPage, VERR_INTERNAL_ERROR);

    *ppPD = (PX86PDPAE)PGMPOOL_PAGE_2_PTR(pVM, pShwPage);
    return VINF_SUCCESS;
}

#ifndef IN_GC
/**
 * Syncs the SHADOW page directory pointer for the specified address. Allocates
 * backing pages in case the PDPT or PML4 entry is missing.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPtr       The address.
 * @param   pGstPml4e   Guest PML4 entry
 * @param   pGstPdpe    Guest PDPT entry
 * @param   ppPD        Receives address of page directory
 */
VMMDECL(int) PGMShwSyncLongModePDPtr(PVM pVM, RTGCUINTPTR64 GCPtr, PX86PML4E pGstPml4e, PX86PDPE pGstPdpe, PX86PDPAE *ppPD)
{
    PPGM           pPGM   = &pVM->pgm.s;
    const unsigned iPml4e = (GCPtr >> X86_PML4_SHIFT) & X86_PML4_MASK;
    PPGMPOOL       pPool  = pPGM->CTX_SUFF(pPool);
    PX86PML4E      pPml4e;
    PPGMPOOLPAGE   pShwPage;
    int            rc;
    bool           fNestedPaging = HWACCMIsNestedPagingActive(pVM);

    Assert(pVM->pgm.s.pHCPaePML4);

    /* Allocate page directory pointer table if not present. */
    pPml4e = &pPGM->pHCPaePML4->a[iPml4e];
    if (    !pPml4e->n.u1Present
        &&  !(pPml4e->u & X86_PML4E_PG_MASK))
    {
        Assert(!(pPml4e->u & X86_PML4E_PG_MASK));

        if (!fNestedPaging)
        {
            Assert(pVM->pgm.s.pHCShwAmd64CR3);
            Assert(pPGM->pGstPaePML4HC);

            PX86PML4E pPml4eGst = &pPGM->pGstPaePML4HC->a[iPml4e];

            rc = pgmPoolAlloc(pVM, pPml4eGst->u & X86_PML4E_PG_MASK, PGMPOOLKIND_64BIT_PDPT_FOR_64BIT_PDPT, pVM->pgm.s.pHCShwAmd64CR3->idx, iPml4e, &pShwPage);
        }
        else
            rc = pgmPoolAlloc(pVM, GCPtr + RT_BIT_64(63) /* hack: make the address unique */, PGMPOOLKIND_64BIT_PDPT_FOR_PHYS, PGMPOOL_IDX_NESTED_ROOT, iPml4e, &pShwPage);

        if (rc == VERR_PGM_POOL_FLUSHED)
        {
            Log(("PGMShwSyncLongModePDPtr: PGM pool flushed (1) -> signal sync cr3\n"));
            Assert(pVM->pgm.s.fSyncFlags & PGM_SYNC_CLEAR_PGM_POOL);
            VM_FF_SET(pVM, VM_FF_PGM_SYNC_CR3);
            return VINF_PGM_SYNC_CR3;
        }
        AssertRCReturn(rc, rc);
    }
    else
    {
        pShwPage = pgmPoolGetPage(pPool, pPml4e->u & X86_PML4E_PG_MASK);
        AssertReturn(pShwPage, VERR_INTERNAL_ERROR);
    }
    /* The PDPT was cached or created; hook it up now. */
    pPml4e->u |=   pShwPage->Core.Key
                | (pGstPml4e->u & ~(X86_PML4E_PG_MASK | X86_PML4E_AVL_MASK | X86_PML4E_PCD | X86_PML4E_PWT));

    const unsigned iPdPt = (GCPtr >> X86_PDPT_SHIFT) & X86_PDPT_MASK_AMD64;
    PX86PDPT  pPdpt = (PX86PDPT)PGMPOOL_PAGE_2_PTR(pVM, pShwPage);
    PX86PDPE  pPdpe = &pPdpt->a[iPdPt];

    /* Allocate page directory if not present. */
    if (    !pPdpe->n.u1Present
        &&  !(pPdpe->u & X86_PDPE_PG_MASK))
    {
        if (!fNestedPaging)
        {
            Assert(pPGM->pGstPaePML4HC);

            PX86PML4E pPml4eGst = &pPGM->pGstPaePML4HC->a[iPml4e];
            PX86PDPT  pPdptGst;
            rc = PGM_GCPHYS_2_PTR(pVM, pPml4eGst->u & X86_PML4E_PG_MASK, &pPdptGst);
            AssertRCReturn(rc, rc);

            Assert(!(pPdpe->u & X86_PDPE_PG_MASK));
            /* Create a reference back to the PDPT by using the index in its shadow page. */
            rc = pgmPoolAlloc(pVM, pPdptGst->a[iPdPt].u & X86_PDPE_PG_MASK, PGMPOOLKIND_64BIT_PD_FOR_64BIT_PD, pShwPage->idx, iPdPt, &pShwPage);
        }
        else
            rc = pgmPoolAlloc(pVM, GCPtr + RT_BIT_64(62) /* hack: make the address unique */, PGMPOOLKIND_64BIT_PD_FOR_PHYS, pShwPage->idx, iPdPt, &pShwPage);

        if (rc == VERR_PGM_POOL_FLUSHED)
        {
            Log(("PGMShwSyncLongModePDPtr: PGM pool flushed (2) -> signal sync cr3\n"));
            Assert(pVM->pgm.s.fSyncFlags & PGM_SYNC_CLEAR_PGM_POOL);
            VM_FF_SET(pVM, VM_FF_PGM_SYNC_CR3);
            return VINF_PGM_SYNC_CR3;
        }
        AssertRCReturn(rc, rc);
    }
    else
    {
        pShwPage = pgmPoolGetPage(pPool, pPdpe->u & X86_PDPE_PG_MASK);
        AssertReturn(pShwPage, VERR_INTERNAL_ERROR);
    }
    /* The PD was cached or created; hook it up now. */
    pPdpe->u |=    pShwPage->Core.Key
                | (pGstPdpe->u & ~(X86_PDPE_PG_MASK | X86_PDPE_AVL_MASK | X86_PDPE_PCD | X86_PDPE_PWT));

    *ppPD = (PX86PDPAE)PGMPOOL_PAGE_2_PTR(pVM, pShwPage);
    return VINF_SUCCESS;
}

/**
 * Gets the SHADOW page directory pointer for the specified address.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPtr       The address.
 * @param   ppPdpt      Receives address of pdpt
 * @param   ppPD        Receives address of page directory
 */
VMMDECL(int) PGMShwGetLongModePDPtr(PVM pVM, RTGCUINTPTR64 GCPtr, PX86PDPT *ppPdpt, PX86PDPAE *ppPD)
{
    PPGM           pPGM   = &pVM->pgm.s;
    const unsigned iPml4e = (GCPtr >> X86_PML4_SHIFT) & X86_PML4_MASK;
    PPGMPOOL       pPool  = pPGM->CTX_SUFF(pPool);
    PX86PML4E      pPml4e;
    PPGMPOOLPAGE   pShwPage;

    AssertReturn(pVM->pgm.s.pHCPaePML4, VERR_INTERNAL_ERROR);

    pPml4e = &pPGM->pHCPaePML4->a[iPml4e];
    if (!pPml4e->n.u1Present)
        return VERR_PAGE_MAP_LEVEL4_NOT_PRESENT;

    pShwPage = pgmPoolGetPage(pPool, pPml4e->u & X86_PML4E_PG_MASK);
    AssertReturn(pShwPage, VERR_INTERNAL_ERROR);

    const unsigned iPdPt = (GCPtr >> X86_PDPT_SHIFT) & X86_PDPT_MASK_AMD64;
    PX86PDPT  pPdpt = (PX86PDPT)PGMPOOL_PAGE_2_PTR(pVM, pShwPage);
    PX86PDPE  pPdpe = &pPdpt->a[iPdPt];

    *ppPdpt = pPdpt;
    if (!pPdpe->n.u1Present)
        return VERR_PAGE_DIRECTORY_PTR_NOT_PRESENT;

    pShwPage = pgmPoolGetPage(pPool, pPdpe->u & X86_PDPE_PG_MASK);
    AssertReturn(pShwPage, VERR_INTERNAL_ERROR);

    *ppPD = (PX86PDPAE)PGMPOOL_PAGE_2_PTR(pVM, pShwPage);
    return VINF_SUCCESS;
}

/**
 * Syncs the SHADOW EPT page directory pointer for the specified address. Allocates
 * backing pages in case the PDPT or PML4 entry is missing.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPtr       The address.
 * @param   ppPdpt      Receives address of pdpt
 * @param   ppPD        Receives address of page directory
 */
VMMDECL(int) PGMShwGetEPTPDPtr(PVM pVM, RTGCUINTPTR64 GCPtr, PEPTPDPT *ppPdpt, PEPTPD *ppPD)
{
    PPGM           pPGM   = &pVM->pgm.s;
    const unsigned iPml4e = (GCPtr >> EPT_PML4_SHIFT) & EPT_PML4_MASK;
    PPGMPOOL       pPool  = pPGM->CTX_SUFF(pPool);
    PEPTPML4       pPml4  = (PEPTPML4)pPGM->pHCNestedRoot;
    PEPTPML4E      pPml4e;
    PPGMPOOLPAGE   pShwPage;
    int            rc;

    Assert(HWACCMIsNestedPagingActive(pVM));
    Assert(pPml4);

    /* Allocate page directory pointer table if not present. */
    pPml4e = &pPml4->a[iPml4e];
    if (    !pPml4e->n.u1Present
        &&  !(pPml4e->u & EPT_PML4E_PG_MASK))
    {
        Assert(!(pPml4e->u & EPT_PML4E_PG_MASK));

        rc = pgmPoolAlloc(pVM, (GCPtr & EPT_PML4E_PG_MASK) + RT_BIT_64(63) /* hack: make the address unique */, PGMPOOLKIND_EPT_PDPT_FOR_PHYS, PGMPOOL_IDX_NESTED_ROOT, iPml4e, &pShwPage);
        if (rc == VERR_PGM_POOL_FLUSHED)
        {
            Log(("PGMShwSyncEPTPDPtr: PGM pool flushed (1) -> signal sync cr3\n"));
            Assert(pVM->pgm.s.fSyncFlags & PGM_SYNC_CLEAR_PGM_POOL);
            VM_FF_SET(pVM, VM_FF_PGM_SYNC_CR3);
            return VINF_PGM_SYNC_CR3;
        }
        AssertRCReturn(rc, rc);
    }
    else
    {
        pShwPage = pgmPoolGetPage(pPool, pPml4e->u & EPT_PML4E_PG_MASK);
        AssertReturn(pShwPage, VERR_INTERNAL_ERROR);
    }
    /* The PDPT was cached or created; hook it up now and fill with the default value. */
    pPml4e->u           = pShwPage->Core.Key;
    pPml4e->n.u1Present = 1;
    pPml4e->n.u1Write   = 1;
    pPml4e->n.u1Execute = 1;

    const unsigned iPdPt = (GCPtr >> EPT_PDPT_SHIFT) & EPT_PDPT_MASK;
    PEPTPDPT  pPdpt = (PEPTPDPT)PGMPOOL_PAGE_2_PTR(pVM, pShwPage);
    PEPTPDPTE pPdpe = &pPdpt->a[iPdPt];

    if (ppPdpt)
        *ppPdpt = pPdpt;

    /* Allocate page directory if not present. */
    if (    !pPdpe->n.u1Present
        &&  !(pPdpe->u & EPT_PDPTE_PG_MASK))
    {
        rc = pgmPoolAlloc(pVM, (GCPtr & EPT_PDPTE_PG_MASK) + RT_BIT_64(62) /* hack: make the address unique */, PGMPOOLKIND_64BIT_PD_FOR_PHYS, pShwPage->idx, iPdPt, &pShwPage);
        if (rc == VERR_PGM_POOL_FLUSHED)
        {
            Log(("PGMShwSyncEPTPDPtr: PGM pool flushed (2) -> signal sync cr3\n"));
            Assert(pVM->pgm.s.fSyncFlags & PGM_SYNC_CLEAR_PGM_POOL);
            VM_FF_SET(pVM, VM_FF_PGM_SYNC_CR3);
            return VINF_PGM_SYNC_CR3;
        }
        AssertRCReturn(rc, rc);
    }
    else
    {
        pShwPage = pgmPoolGetPage(pPool, pPdpe->u & X86_PDPE_PG_MASK);
        AssertReturn(pShwPage, VERR_INTERNAL_ERROR);
    }
    /* The PD was cached or created; hook it up now and fill with the default value. */
    pPdpe->u            = pShwPage->Core.Key;
    pPdpe->n.u1Present  = 1;
    pPdpe->n.u1Write    = 1;
    pPdpe->n.u1Execute  = 1;

    *ppPD = (PEPTPD)PGMPOOL_PAGE_2_PTR(pVM, pShwPage);
    return VINF_SUCCESS;
}

#endif

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
VMMDECL(int) PGMGstGetPage(PVM pVM, RTGCPTR GCPtr, uint64_t *pfFlags, PRTGCPHYS pGCPhys)
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
VMMDECL(bool) PGMGstIsPagePresent(PVM pVM, RTGCPTR GCPtr)
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
VMMDECL(int)  PGMGstSetPage(PVM pVM, RTGCPTR GCPtr, size_t cb, uint64_t fFlags)
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
VMMDECL(int)  PGMGstModifyPage(PVM pVM, RTGCPTR GCPtr, size_t cb, uint64_t fFlags, uint64_t fMask)
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
 * Gets the current CR3 register value for the shadow memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
VMMDECL(RTHCPHYS) PGMGetHyperCR3(PVM pVM)
{
    PGMMODE enmShadowMode = pVM->pgm.s.enmShadowMode;
    switch (enmShadowMode)
    {
        case PGMMODE_32_BIT:
            return pVM->pgm.s.HCPhys32BitPD;

        case PGMMODE_PAE:
        case PGMMODE_PAE_NX:
            return pVM->pgm.s.HCPhysPaePDPT;

        case PGMMODE_AMD64:
        case PGMMODE_AMD64_NX:
            return pVM->pgm.s.HCPhysPaePML4;

        case PGMMODE_EPT:
            return pVM->pgm.s.HCPhysNestedRoot;

        case PGMMODE_NESTED:
            return PGMGetNestedCR3(pVM, PGMGetHostMode(pVM));

        default:
            AssertMsgFailed(("enmShadowMode=%d\n", enmShadowMode));
            return ~0;
    }
}

/**
 * Gets the current CR3 register value for the nested memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
VMMDECL(RTHCPHYS) PGMGetNestedCR3(PVM pVM, PGMMODE enmShadowMode)
{
    switch (enmShadowMode)
    {
        case PGMMODE_32_BIT:
            return pVM->pgm.s.HCPhys32BitPD;

        case PGMMODE_PAE:
        case PGMMODE_PAE_NX:
            return pVM->pgm.s.HCPhysPaePDPT;

        case PGMMODE_AMD64:
        case PGMMODE_AMD64_NX:
            return pVM->pgm.s.HCPhysPaePML4;

        default:
            AssertMsgFailed(("enmShadowMode=%d\n", enmShadowMode));
            return ~0;
    }
}


/**
 * Gets the CR3 register value for the 32-Bit shadow memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
VMMDECL(RTHCPHYS) PGMGetHyper32BitCR3(PVM pVM)
{
    return pVM->pgm.s.HCPhys32BitPD;
}


/**
 * Gets the CR3 register value for the PAE shadow memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
VMMDECL(RTHCPHYS) PGMGetHyperPaeCR3(PVM pVM)
{
    return pVM->pgm.s.HCPhysPaePDPT;
}


/**
 * Gets the CR3 register value for the AMD64 shadow memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
VMMDECL(RTHCPHYS) PGMGetHyperAmd64CR3(PVM pVM)
{
    return pVM->pgm.s.HCPhysPaePML4;
}


/**
 * Gets the current CR3 register value for the HC intermediate memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
VMMDECL(RTHCPHYS) PGMGetInterHCCR3(PVM pVM)
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
            return pVM->pgm.s.HCPhysInterPaePDPT;

        case SUPPAGINGMODE_AMD64:
        case SUPPAGINGMODE_AMD64_GLOBAL:
        case SUPPAGINGMODE_AMD64_NX:
        case SUPPAGINGMODE_AMD64_GLOBAL_NX:
            return pVM->pgm.s.HCPhysInterPaePDPT;

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
VMMDECL(RTHCPHYS) PGMGetInterGCCR3(PVM pVM)
{
    switch (pVM->pgm.s.enmShadowMode)
    {
        case PGMMODE_32_BIT:
            return pVM->pgm.s.HCPhysInterPD;

        case PGMMODE_PAE:
        case PGMMODE_PAE_NX:
            return pVM->pgm.s.HCPhysInterPaePDPT;

        case PGMMODE_AMD64:
        case PGMMODE_AMD64_NX:
            return pVM->pgm.s.HCPhysInterPaePML4;

        case PGMMODE_EPT:
        case PGMMODE_NESTED:
            return 0; /* not relevant */

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
VMMDECL(RTHCPHYS) PGMGetInter32BitCR3(PVM pVM)
{
    return pVM->pgm.s.HCPhysInterPD;
}


/**
 * Gets the CR3 register value for the PAE intermediate memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
VMMDECL(RTHCPHYS) PGMGetInterPaeCR3(PVM pVM)
{
    return pVM->pgm.s.HCPhysInterPaePDPT;
}


/**
 * Gets the CR3 register value for the AMD64 intermediate memory context.
 * @returns CR3 value.
 * @param   pVM         The VM handle.
 */
VMMDECL(RTHCPHYS) PGMGetInterAmd64CR3(PVM pVM)
{
    return pVM->pgm.s.HCPhysInterPaePML4;
}


/**
 * Performs and schedules necessary updates following a CR3 load or reload.
 *
 * This will normally involve mapping the guest PD or nPDPT
 *
 * @returns VBox status code.
 * @retval  VINF_PGM_SYNC_CR3 if monitoring requires a CR3 sync. This can
 *          safely be ignored and overridden since the FF will be set too then.
 * @param   pVM         VM handle.
 * @param   cr3         The new cr3.
 * @param   fGlobal     Indicates whether this is a global flush or not.
 */
VMMDECL(int) PGMFlushTLB(PVM pVM, uint64_t cr3, bool fGlobal)
{
    STAM_PROFILE_START(&pVM->pgm.s.StatFlushTLB, a);

    /*
     * Always flag the necessary updates; necessary for hardware acceleration
     */
    VM_FF_SET(pVM, VM_FF_PGM_SYNC_CR3_NON_GLOBAL);
    if (fGlobal)
        VM_FF_SET(pVM, VM_FF_PGM_SYNC_CR3);
    LogFlow(("PGMFlushTLB: cr3=%VX64 OldCr3=%VX64 fGlobal=%d\n", cr3, pVM->pgm.s.GCPhysCR3, fGlobal));

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
 * Performs and schedules necessary updates following a CR3 load or reload,
 * without actually flushing the TLB as with PGMFlushTLB.
 *
 * This will normally involve mapping the guest PD or nPDPT
 *
 * @returns VBox status code.
 * @retval  VINF_PGM_SYNC_CR3 if monitoring requires a CR3 sync. This can
 *          safely be ignored and overridden since the FF will be set too then.
 * @param   pVM         VM handle.
 * @param   cr3         The new cr3.
 */
VMMDECL(int) PGMUpdateCR3(PVM pVM, uint64_t cr3)
{
    LogFlow(("PGMUpdateCR3: cr3=%VX64 OldCr3=%VX64\n", cr3, pVM->pgm.s.GCPhysCR3));

    /* We assume we're only called in nested paging mode. */
    Assert(pVM->pgm.s.fMappingsFixed);
    Assert(!(pVM->pgm.s.fSyncFlags & PGM_SYNC_MONITOR_CR3));
    Assert(pVM->pgm.s.enmShadowMode == PGMMODE_NESTED || pVM->pgm.s.enmShadowMode == PGMMODE_EPT);

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
    }
    AssertRC(rc);
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
VMMDECL(int) PGMSyncCR3(PVM pVM, uint64_t cr0, uint64_t cr3, uint64_t cr4, bool fGlobal)
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
    LogFlow(("PGMSyncCR3: cr0=%VX64 cr3=%VX64 cr4=%VX64 fGlobal=%d[%d,%d]\n", cr0, cr3, cr4, fGlobal,
             VM_FF_ISSET(pVM, VM_FF_PGM_SYNC_CR3), VM_FF_ISSET(pVM, VM_FF_PGM_SYNC_CR3_NON_GLOBAL)));

    /*
     * Let the 'Bth' function do the work and we'll just keep track of the flags.
     */
    STAM_PROFILE_START(&pVM->pgm.s.CTX_MID_Z(Stat,SyncCR3), a);
    int rc = PGM_BTH_PFN(SyncCR3, pVM)(pVM, cr0, cr3, cr4, fGlobal);
    STAM_PROFILE_STOP(&pVM->pgm.s.CTX_MID_Z(Stat,SyncCR3), a);
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
VMMDECL(int) PGMChangeMode(PVM pVM, uint64_t cr0, uint64_t cr4, uint64_t efer)
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

    /* Flush the TLB */
    PGM_INVL_GUEST_TLBS();

#ifdef IN_RING3
    return PGMR3ChangeMode(pVM, enmGuestMode);
#else
    Log(("PGMChangeMode: returns VINF_PGM_CHANGE_MODE.\n"));
    return VINF_PGM_CHANGE_MODE;
#endif
}


/**
 * Gets the current guest paging mode.
 *
 * If you just need the CPU mode (real/protected/long), use CPUMGetGuestMode().
 *
 * @returns The current paging mode.
 * @param   pVM             The VM handle.
 */
VMMDECL(PGMMODE) PGMGetGuestMode(PVM pVM)
{
    return pVM->pgm.s.enmGuestMode;
}


/**
 * Gets the current shadow paging mode.
 *
 * @returns The current paging mode.
 * @param   pVM             The VM handle.
 */
VMMDECL(PGMMODE) PGMGetShadowMode(PVM pVM)
{
    return pVM->pgm.s.enmShadowMode;
}

/**
 * Gets the current host paging mode.
 *
 * @returns The current paging mode.
 * @param   pVM             The VM handle.
 */
VMMDECL(PGMMODE) PGMGetHostMode(PVM pVM)
{
    switch (pVM->pgm.s.enmHostMode)
    {
        case SUPPAGINGMODE_32_BIT:
        case SUPPAGINGMODE_32_BIT_GLOBAL:
            return PGMMODE_32_BIT;

        case SUPPAGINGMODE_PAE:
        case SUPPAGINGMODE_PAE_GLOBAL:
            return PGMMODE_PAE;

        case SUPPAGINGMODE_PAE_NX:
        case SUPPAGINGMODE_PAE_GLOBAL_NX:
            return PGMMODE_PAE_NX;

        case SUPPAGINGMODE_AMD64:
        case SUPPAGINGMODE_AMD64_GLOBAL:
            return PGMMODE_AMD64;

        case SUPPAGINGMODE_AMD64_NX:
        case SUPPAGINGMODE_AMD64_GLOBAL_NX:
            return PGMMODE_AMD64_NX;

        default: AssertMsgFailed(("enmHostMode=%d\n", pVM->pgm.s.enmHostMode)); break;
    }

    return PGMMODE_INVALID;
}


/**
 * Get mode name.
 *
 * @returns read-only name string.
 * @param   enmMode     The mode which name is desired.
 */
VMMDECL(const char *) PGMGetModeName(PGMMODE enmMode)
{
    switch (enmMode)
    {
        case PGMMODE_REAL:      return "Real";
        case PGMMODE_PROTECTED: return "Protected";
        case PGMMODE_32_BIT:    return "32-bit";
        case PGMMODE_PAE:       return "PAE";
        case PGMMODE_PAE_NX:    return "PAE+NX";
        case PGMMODE_AMD64:     return "AMD64";
        case PGMMODE_AMD64_NX:  return "AMD64+NX";
        case PGMMODE_NESTED:    return "Nested";
        case PGMMODE_EPT:       return "EPT";
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

#if defined(IN_GC) || defined(VBOX_WITH_2X_4GB_ADDR_SPACE_IN_R0)

/**
 * Temporarily maps one guest page specified by GC physical address.
 * These pages must have a physical mapping in HC, i.e. they cannot be MMIO pages.
 *
 * Be WARNED that the dynamic page mapping area is small, 8 pages, thus the space is
 * reused after 8 mappings (or perhaps a few more if you score with the cache).
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPhys      GC Physical address of the page.
 * @param   ppv         Where to store the address of the mapping.
 */
VMMDECL(int) PGMDynMapGCPage(PVM pVM, RTGCPHYS GCPhys, void **ppv)
{
    AssertMsg(!(GCPhys & PAGE_OFFSET_MASK), ("GCPhys=%RGp\n", GCPhys));

    /*
     * Get the ram range.
     */
    PPGMRAMRANGE pRam = pVM->pgm.s.CTX_SUFF(pRamRanges);
    while (pRam && GCPhys - pRam->GCPhys >= pRam->cb)
        pRam = pRam->CTX_SUFF(pNext);
    if (!pRam)
    {
        AssertMsgFailed(("Invalid physical address %RGp!\n", GCPhys));
        return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
    }

    /*
     * Pass it on to PGMDynMapHCPage.
     */
    RTHCPHYS HCPhys = PGM_PAGE_GET_HCPHYS(&pRam->aPages[(GCPhys - pRam->GCPhys) >> PAGE_SHIFT]);
    //Log(("PGMDynMapGCPage: GCPhys=%VGp HCPhys=%VHp\n", GCPhys, HCPhys));
    return PGMDynMapHCPage(pVM, HCPhys, ppv);
}


/**
 * Temporarily maps one guest page specified by unaligned GC physical address.
 * These pages must have a physical mapping in HC, i.e. they cannot be MMIO pages.
 *
 * Be WARNED that the dynamic page mapping area is small, 8 pages, thus the space is
 * reused after 8 mappings (or perhaps a few more if you score with the cache).
 *
 * The caller is aware that only the speicifed page is mapped and that really bad things
 * will happen if writing beyond the page!
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPhys      GC Physical address within the page to be mapped.
 * @param   ppv         Where to store the address of the mapping address corresponding to GCPhys.
 */
VMMDECL(int) PGMDynMapGCPageOff(PVM pVM, RTGCPHYS GCPhys, void **ppv)
{
    /*
     * Get the ram range.
     */
    PPGMRAMRANGE pRam = pVM->pgm.s.CTX_SUFF(pRamRanges);
    while (pRam && GCPhys - pRam->GCPhys >= pRam->cb)
        pRam = pRam->CTX_SUFF(pNext);
    if (!pRam)
    {
        AssertMsgFailed(("Invalid physical address %VGp!\n", GCPhys));
        return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
    }

    /*
     * Pass it on to PGMDynMapHCPageOff.
     */
    RTHCPHYS HCPhys = PGM_PAGE_GET_HCPHYS(&pRam->aPages[(GCPhys - pRam->GCPhys) >> PAGE_SHIFT]);
    int rc = PGMDynMapHCPhys(pVM, HCPhys, ppv);
    if (RT_SUCCESS(rc))
        *ppv = (void *)((uintptr_t)*ppv | (GCPhys & PAGE_OFFSET_MASK));
    return rc;
}


/**
 * Temporarily maps one host page specified by HC physical address.
 *
 * Be WARNED that the dynamic page mapping area is small, 8 pages, thus the space is
 * reused after 8 mappings (or perhaps a few more if you score with the cache).
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   HCPhys      HC Physical address of the page.
 * @param   ppv         Where to store the address of the mapping. This is the
 *                      address of the PAGE not the exact address corresponding
 *                      to HCPhys. Use PGMDynMapHCPageOff if you care for the
 *                      page offset.
 */
VMMDECL(int) PGMDynMapHCPage(PVM pVM, RTHCPHYS HCPhys, void **ppv)
{
    AssertMsg(!(HCPhys & PAGE_OFFSET_MASK), ("HCPhys=%RHp\n", HCPhys));
# ifdef IN_GC

    /*
     * Check the cache.
     */
    register unsigned iCache;
    if (    pVM->pgm.s.aHCPhysDynPageMapCache[iCache = 0] == HCPhys
        ||  pVM->pgm.s.aHCPhysDynPageMapCache[iCache = 1] == HCPhys
        ||  pVM->pgm.s.aHCPhysDynPageMapCache[iCache = 2] == HCPhys
        ||  pVM->pgm.s.aHCPhysDynPageMapCache[iCache = 3] == HCPhys)
    {
        static const uint8_t au8Trans[MM_HYPER_DYNAMIC_SIZE >> PAGE_SHIFT][RT_ELEMENTS(pVM->pgm.s.aHCPhysDynPageMapCache)] =
        {
            { 0, 5, 6, 7 },
            { 0, 1, 6, 7 },
            { 0, 1, 2, 7 },
            { 0, 1, 2, 3 },
            { 4, 1, 2, 3 },
            { 4, 5, 2, 3 },
            { 4, 5, 6, 3 },
            { 4, 5, 6, 7 },
        };
        Assert(RT_ELEMENTS(au8Trans) == 8);
        Assert(RT_ELEMENTS(au8Trans[0]) == 4);
        int iPage = au8Trans[pVM->pgm.s.iDynPageMapLast][iCache];
        void *pv = pVM->pgm.s.pbDynPageMapBaseGC + (iPage << PAGE_SHIFT);
        *ppv = pv;
        STAM_COUNTER_INC(&pVM->pgm.s.StatDynMapCacheHits);
        //Log(("PGMGCDynMapHCPage: HCPhys=%VHp pv=%VGv iPage=%d iCache=%d\n", HCPhys, pv, iPage, iCache));
        return VINF_SUCCESS;
    }
    Assert(RT_ELEMENTS(pVM->pgm.s.aHCPhysDynPageMapCache) == 4);
    STAM_COUNTER_INC(&pVM->pgm.s.StatDynMapCacheMisses);

    /*
     * Update the page tables.
     */
    register unsigned iPage = pVM->pgm.s.iDynPageMapLast;
    pVM->pgm.s.iDynPageMapLast = iPage = (iPage + 1) & ((MM_HYPER_DYNAMIC_SIZE >> PAGE_SHIFT) - 1);
    Assert((MM_HYPER_DYNAMIC_SIZE >> PAGE_SHIFT) == 8);

    pVM->pgm.s.aHCPhysDynPageMapCache[iPage & (RT_ELEMENTS(pVM->pgm.s.aHCPhysDynPageMapCache) - 1)] = HCPhys;
    pVM->pgm.s.paDynPageMap32BitPTEsGC[iPage].u = (uint32_t)HCPhys | X86_PTE_P | X86_PTE_A | X86_PTE_D;
    pVM->pgm.s.paDynPageMapPaePTEsGC[iPage].u   =           HCPhys | X86_PTE_P | X86_PTE_A | X86_PTE_D;

    void *pv = pVM->pgm.s.pbDynPageMapBaseGC + (iPage << PAGE_SHIFT);
    *ppv = pv;
    ASMInvalidatePage(pv);
    Log4(("PGMGCDynMapHCPage: HCPhys=%VHp pv=%VGv iPage=%d\n", HCPhys, pv, iPage));
    return VINF_SUCCESS;

#else  /* VBOX_WITH_2X_4GB_ADDR_SPACE_IN_R0 */
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
#endif /* VBOX_WITH_2X_4GB_ADDR_SPACE_IN_R0 */
}


/**
 * Temporarily maps one host page specified by HC physical address, returning
 * pointer within the page.
 *
 * Be WARNED that the dynamic page mapping area is small, 8 pages, thus the space is
 * reused after 8 mappings (or perhaps a few more if you score with the cache).
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   HCPhys      HC Physical address of the page.
 * @param   ppv         Where to store the address corresponding to HCPhys.
 */
VMMDECL(int) PGMDynMapHCPageOff(PVM pVM, RTHCPHYS HCPhys, void **ppv)
{
    int rc = PGMDynMapHCPage(pVM, HCPhys & ~(RTHCPHYS)PAGE_OFFSET_MASK, ppv);
    if (RT_SUCCESS(rc))
        *ppv = (void *)((uintptr_t)*ppv | (HCPhys & PAGE_OFFSET_MASK));
    return rc;
}

#endif /* IN_GC || VBOX_WITH_2X_4GB_ADDR_SPACE_IN_R0 */

#ifdef VBOX_STRICT

/**
 * Asserts that there are no mapping conflicts.
 *
 * @returns Number of conflicts.
 * @param   pVM     The VM Handle.
 */
VMMDECL(unsigned) PGMAssertNoMappingConflicts(PVM pVM)
{
    unsigned cErrors = 0;

    /*
     * Check for mapping conflicts.
     */
    for (PPGMMAPPING pMapping = pVM->pgm.s.CTX_SUFF(pMappings);
         pMapping;
         pMapping = pMapping->CTX_SUFF(pNext))
    {
        /** @todo This is slow and should be optimized, but since it's just assertions I don't care now. */
        for (RTGCUINTPTR GCPtr = (RTGCUINTPTR)pMapping->GCPtr;
              GCPtr <= (RTGCUINTPTR)pMapping->GCPtrLast;
              GCPtr += PAGE_SIZE)
        {
            int rc = PGMGstGetPage(pVM, (RTGCPTR)GCPtr, NULL, NULL);
            if (rc != VERR_PAGE_TABLE_NOT_PRESENT)
            {
                AssertMsgFailed(("Conflict at %VGv with %s\n", GCPtr, R3STRING(pMapping->pszDesc)));
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
VMMDECL(unsigned) PGMAssertCR3(PVM pVM, uint64_t cr3, uint64_t cr4)
{
    STAM_PROFILE_START(&pVM->pgm.s.CTX_MID_Z(Stat,SyncCR3), a);
    unsigned cErrors = PGM_BTH_PFN(AssertCR3, pVM)(pVM, cr3, cr4, 0, ~(RTGCUINTPTR)0);
    STAM_PROFILE_STOP(&pVM->pgm.s.CTX_MID_Z(Stat,SyncCR3), a);
    return cErrors;
    return 0;
}

#endif /* VBOX_STRICT */
