/* $Id$ */
/** @file
 * HWACCM - All contexts.
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
#define LOG_GROUP LOG_GROUP_HWACCM
#include <VBox/hwaccm.h>
#include "HWACCMInternal.h"
#include <VBox/vm.h>
#include <VBox/x86.h>
#include <VBox/hwacc_vmx.h>
#include <VBox/hwacc_svm.h>
#include <VBox/pgm.h>
#include <VBox/pdm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/selm.h>
#include <VBox/iom.h>
#include <iprt/param.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/memobj.h>
#include <iprt/cpuset.h>

/**
 * Invalidates a guest page
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 * @param   GCVirt      Page to invalidate
 */
VMMDECL(int) HWACCMInvalidatePage(PVM pVM, RTGCPTR GCVirt)
{
    /** @todo Intel for nested paging */
#ifdef IN_RING0
    if (pVM->hwaccm.s.svm.fSupported)
        return SVMR0InvalidatePage(pVM, GCVirt);
#endif

    return VINF_SUCCESS;
}

/**
 * Flushes the guest TLB
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
VMMDECL(int) HWACCMFlushTLB(PVM pVM)
{
    /** @todo Intel for nested paging */
    if (pVM->hwaccm.s.svm.fSupported)
    {
        LogFlow(("HWACCMFlushTLB\n"));
        pVM->hwaccm.s.svm.fForceTLBFlush = true;
        STAM_COUNTER_INC(&pVM->hwaccm.s.StatFlushTLBManual);
    }
    return VINF_SUCCESS;
}

/**
 * Checks if nested paging is enabled
 *
 * @returns boolean
 * @param   pVM         The VM to operate on.
 */
VMMDECL(bool) HWACCMIsNestedPagingActive(PVM pVM)
{
    return HWACCMIsEnabled(pVM) && pVM->hwaccm.s.fNestedPaging;
}

/**
 * Return the shadow paging mode for nested paging/ept
 *
 * @returns shadow paging mode
 * @param   pVM         The VM to operate on.
 */
VMMDECL(PGMMODE) HWACCMGetPagingMode(PVM pVM)
{
    Assert(HWACCMIsNestedPagingActive(pVM));
    if (pVM->hwaccm.s.svm.fSupported)
        return PGMMODE_NESTED;
    Assert(pVM->hwaccm.s.vmx.fSupported);
    return PGMMODE_EPT;
}

/**
 * Invalidates a guest page by physical address
 *
 * NOTE: Assumes the current instruction references this physical page though a virtual address!!
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 * @param   GCPhys      Page to invalidate
 */
VMMDECL(int) HWACCMInvalidatePhysPage(PVM pVM, RTGCPHYS GCPhys)
{
    if (!HWACCMIsNestedPagingActive(pVM))
        return VINF_SUCCESS;

#ifdef IN_RING0
    /** @todo Intel for nested paging */
    if (pVM->hwaccm.s.svm.fSupported)
    {
        SVMR0InvalidatePhysPage(pVM, GCPhys);
    }
#else
    HWACCMFlushTLB(pVM);
#endif
    return VINF_SUCCESS;
}

/**
 * Checks if an interrupt event is currently pending.
 *
 * @returns Interrupt event pending state.
 * @param   pVM         The VM to operate on.
 */
VMMDECL(bool) HWACCMHasPendingIrq(PVM pVM)
{
    return !!pVM->hwaccm.s.Event.fPending;
}

