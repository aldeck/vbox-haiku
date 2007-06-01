/* $Id$ */
/** @file
 * MMRamGC - Guest Context Ram access Routines, pair for MMRamGCA.asm.
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
#define LOG_GROUP LOG_GROUP_MM
#include <VBox/mm.h>
#include <VBox/cpum.h>
#include <VBox/trpm.h>
#include "MMInternal.h"
#include <VBox/vm.h>
#include <VBox/pgm.h>

#include <iprt/assert.h>
#include <VBox/param.h>
#include <VBox/err.h>


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static DECLCALLBACK(int) mmgcramTrap0eHandler(PVM pVM, PCPUMCTXCORE pRegFrame);

DECLASM(void) MMGCRamReadNoTrapHandler_EndProc(void);
DECLASM(void) MMGCRamWriteNoTrapHandler_EndProc(void);

DECLASM(void) MMGCRamRead_Error(void);
DECLASM(void) MMGCRamWrite_Error(void);


/**
 * Install MMGCRam Hypervisor page fault handler for normal working
 * of MMGCRamRead and MMGCRamWrite calls.
 * This handler will be automatically removed at page fault.
 * In other case it must be removed by MMGCRamDeregisterTrapHandler call.
 *
 * @param   pVM         VM handle.
 */
MMGCDECL(void) MMGCRamRegisterTrapHandler(PVM pVM)
{
    TRPMGCSetTempHandler(pVM, 0xe, mmgcramTrap0eHandler);
}

/**
 * Remove MMGCRam Hypervisor page fault handler.
 * See description of MMGCRamRegisterTrapHandler call.
 *
 * @param   pVM         VM handle.
 */
MMGCDECL(void) MMGCRamDeregisterTrapHandler(PVM pVM)
{
    TRPMGCSetTempHandler(pVM, 0xe, NULL);
}


/**
 * Read data in guest context with #PF control.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   pDst        Where to store the readed data.
 * @param   pSrc        Pointer to the data to read.
 * @param   cb          Size of data to read, only 1/2/4/8 is valid.
 */
MMGCDECL(int) MMGCRamRead(PVM pVM, void *pDst, void *pSrc, size_t cb)
{
    int rc;

    TRPMSaveTrap(pVM);  /* save the current trap info, because it will get trashed if our access failed. */

    MMGCRamRegisterTrapHandler(pVM);
    rc = MMGCRamReadNoTrapHandler(pDst, pSrc, cb);
    MMGCRamDeregisterTrapHandler(pVM);
    if (VBOX_FAILURE(rc))
        TRPMRestoreTrap(pVM);

    return rc;
}

/**
 * Write data in guest context with #PF control.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   pDst        Where to write the data.
 * @param   pSrc        Pointer to the data to write.
 * @param   cb          Size of data to write, only 1/2/4 is valid.
 */
MMGCDECL(int) MMGCRamWrite(PVM pVM, void *pDst, void *pSrc, size_t cb)
{
    int rc;

    TRPMSaveTrap(pVM);  /* save the current trap info, because it will get trashed if our access failed. */

    MMGCRamRegisterTrapHandler(pVM);
    rc = MMGCRamWriteNoTrapHandler(pDst, pSrc, cb);
    MMGCRamDeregisterTrapHandler(pVM);
    if (VBOX_FAILURE(rc))
        TRPMRestoreTrap(pVM);

    /*
     * And mark the relevant guest page as accessed and dirty.
     */
    PGMGstModifyPage(pVM, pDst, cb, X86_PTE_A | X86_PTE_D, ~(uint64_t)(X86_PTE_A | X86_PTE_D));

    return rc;
}


/**
 * \#PF Handler for servicing traps inside MMGCRamReadNoTrapHandler and MMGCRamWriteNoTrapHandler functions.
 *
 * @internal
 */
DECLCALLBACK(int) mmgcramTrap0eHandler(PVM pVM, PCPUMCTXCORE pRegFrame)
{
    /*
     * Check where the trap was occurred.
     */
    if (    (uintptr_t)&MMGCRamReadNoTrapHandler < (uintptr_t)pRegFrame->eip
        &&  (uintptr_t)pRegFrame->eip < (uintptr_t)&MMGCRamReadNoTrapHandler_EndProc)
    {
        /*
         * Page fault inside MMGCRamRead() func.
         */
        RTGCUINT uErrorCode = TRPMGetErrorCode(pVM);

        /* Must be read violation. */
        if (uErrorCode & X86_TRAP_PF_RW)
            return VERR_INTERNAL_ERROR;

        /* Return execution to func at error label. */
        pRegFrame->eip = (uintptr_t)&MMGCRamRead_Error;
        return VINF_SUCCESS;
    }
    else if (    (uintptr_t)&MMGCRamWriteNoTrapHandler < (uintptr_t)pRegFrame->eip
             &&  (uintptr_t)pRegFrame->eip < (uintptr_t)&MMGCRamWriteNoTrapHandler_EndProc)
    {
        /*
         * Page fault inside MMGCRamWrite() func.
         */
        RTGCUINT uErrorCode = TRPMGetErrorCode(pVM);

        /* Must be write violation. */
        if (!(uErrorCode & X86_TRAP_PF_RW))
            return VERR_INTERNAL_ERROR;

        /* Return execution to func at error label. */
        pRegFrame->eip = (uintptr_t)&MMGCRamWrite_Error;
        return VINF_SUCCESS;
    }

    /* #PF is not handled - kill the Hypervisor. */
    return VERR_INTERNAL_ERROR;
}


