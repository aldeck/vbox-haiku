/** @file
 *
 * TRPM - The Trap Monitor, Guest Context
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
#define LOG_GROUP LOG_GROUP_TRPM
#include <VBox/trpm.h>
#include <VBox/cpum.h>
#include <VBox/vmm.h>
#include "TRPMInternal.h"
#include <VBox/vm.h>

#include <VBox/err.h>
#include <VBox/x86.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <VBox/log.h>



/**
 * Arms a temporary trap handler for traps in Hypervisor code.
 *
 * The operation is similar to a System V signal handler. I.e. when the handler
 * is called it is first set to default action. So, if you need to handler more
 * than one trap, you must reinstall the handler.
 *
 * To uninstall the temporary handler, call this function with pfnHandler set to NULL.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   iTrap       Trap number to install handler [0..255].
 * @param   pfnHandler  Pointer to the handler. Use NULL for uninstalling the handler.
 */
TRPMGCDECL(int) TRPMGCSetTempHandler(PVM pVM, unsigned iTrap, PFNTRPMGCTRAPHANDLER pfnHandler)
{
    /*
     * Validate input.
     */
    if (iTrap >= ELEMENTS(pVM->trpm.s.aTmpTrapHandlers))
    {
        AssertMsgFailed(("Trap handler iTrap=%u is out of range!\n", iTrap));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Install handler.
     */
    pVM->trpm.s.aTmpTrapHandlers[iTrap] = (RTGCPTR)(RTGCUINTPTR)pfnHandler;
    return VINF_SUCCESS;
}


/**
 * Return to host context from a hypervisor trap handler.
 *
 * This function will *never* return.
 * It will also reset any traps that are pending.
 *
 * @param   pVM     The VM handle.
 * @param   rc      The return code for host context.
 */
TRPMGCDECL(void) TRPMGCHyperReturnToHost(PVM pVM, int rc)
{
    LogFlow(("TRPMGCHyperReturnToHost: rc=%Vrc\n", rc));
    TRPMResetTrap(pVM);
    CPUMHyperSetCtxCore(pVM, NULL);
    VMMGCGuestToHost(pVM, rc);
    AssertReleaseFailed();
}


/**
 * \#PF Virtual Handler callback for Guest write access to the Guest's own current IDT.
 *
 * @returns VBox status code (appropritate for trap handling and GC return).
 * @param   pVM         VM Handle.
 * @param   uErrorCode   CPU Error code.
 * @param   pRegFrame   Trap register frame.
 * @param   pvFault     The fault address (cr2).
 * @param   pvRange     The base address of the handled virtual range.
 * @param   offRange    The offset of the access into this range.
 *                      (If it's a EIP range this's the EIP, if not it's pvFault.)
 */
TRPMGCDECL(int) trpmgcGuestIDTWriteHandler(PVM pVM, RTGCUINT uErrorCode, PCPUMCTXCORE pRegFrame, void *pvFault, void *pvRange, uintptr_t offRange)
{
    ////LogCom(("trpmgcGuestIDTWriteHandler: eip=%08X pvFault=%08X pvRange=%08X\r\n", pRegFrame->eip, pvFault, pvRange));
    VM_FF_SET(pVM, VM_FF_TRPM_SYNC_IDT);
    /** @todo Check which IDT entry and keep the update cost low in TRPMR3SyncIDT() and CSAMCheckGates(). */

    STAM_COUNTER_INC(&pVM->trpm.s.StatGCWriteGuestIDT);
    return VINF_EM_RAW_EMULATE_INSTR_IDT_FAULT;
}


/**
 * \#PF Virtual Handler callback for Guest write access to the VBox shadow IDT.
 *
 * @returns VBox status code (appropritate for trap handling and GC return).
 * @param   pVM         VM Handle.
 * @param   uErrorCode   CPU Error code.
 * @param   pRegFrame   Trap register frame.
 * @param   pvFault     The fault address (cr2).
 * @param   pvRange     The base address of the handled virtual range.
 * @param   offRange    The offset of the access into this range.
 *                      (If it's a EIP range this's the EIP, if not it's pvFault.)
 */
TRPMGCDECL(int) trpmgcShadowIDTWriteHandler(PVM pVM, RTGCUINT uErrorCode, PCPUMCTXCORE pRegFrame, void *pvFault, void *pvRange, uintptr_t offRange)
{
    ////LogCom(("trpmgcShadowIDTWriteHandler: eip=%08X pvFault=%08X pvRange=%08X\r\n", pRegFrame->eip, pvFault, pvRange));
    return VERR_TRPM_SHADOW_IDT_WRITE;
}

