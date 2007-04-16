/* $Id$ */
/** @file
 * VMM - Host Context Ring 0.
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
#define LOG_GROUP LOG_GROUP_VMM
#ifdef __AMD64__ /** @todo fix logging on __AMD64__ (swapgs) - this has been fixed now. please remove. */
# define LOG_DISABLED
#endif
#include <VBox/vmm.h>
#include <VBox/sup.h>
#include <VBox/trpm.h>
#include <VBox/cpum.h>
#include <VBox/stam.h>
#include <VBox/tm.h>
#include "VMMInternal.h"
#include <VBox/vm.h>
#include <VBox/intnet.h>
#include <VBox/hwaccm.h>

#include <VBox/err.h>
#include <VBox/version.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/stdarg.h>

#if defined(_MSC_VER) && defined(__AMD64__) /** @todo check this with with VC7! */
#  pragma intrinsic(_AddressOfReturnAddress)
#endif


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static int VMMR0Init(PVM pVM, unsigned uVersion);
static int VMMR0Term(PVM pVM);
__BEGIN_DECLS
VMMR0DECL(int) ModuleInit(void);
VMMR0DECL(void) ModuleTerm(void);
__END_DECLS


#ifdef DEBUG
#define DEBUG_NO_RING0_ASSERTIONS
#ifdef DEBUG_NO_RING0_ASSERTIONS
static PVM g_pVMAssert = 0;
#endif
#endif 

/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
#ifdef VBOX_WITH_INTERNAL_NETWORKING
/** Pointer to the internal networking service instance. */
PINTNET g_pIntNet = 0;
#endif 


/**
 * Initialize the module.
 * This is called when we're first loaded.
 *
 * @returns 0 on success.
 * @returns VBox status on failure.
 */
VMMR0DECL(int) ModuleInit(void)
{
#ifdef VBOX_WITH_INTERNAL_NETWORKING
    LogFlow(("ModuleInit: g_pIntNet=%p\n", g_pIntNet));
    g_pIntNet = NULL;
    LogFlow(("ModuleInit: g_pIntNet=%p should be NULL now...\n", g_pIntNet));
    int rc = INTNETR0Create(&g_pIntNet);
    if (VBOX_SUCCESS(rc))
    {
        LogFlow(("ModuleInit: returns success. g_pIntNet=%p\n", g_pIntNet));
        return 0;
    }
    g_pIntNet = NULL;
    LogFlow(("ModuleTerm: returns %Vrc\n", rc));
    return rc;
#else
    return 0;
#endif
}


/**
 * Terminate the module.
 * This is called when we're finally unloaded.
 */
VMMR0DECL(void) ModuleTerm(void)
{
#ifdef VBOX_WITH_INTERNAL_NETWORKING
    LogFlow(("ModuleTerm:\n"));
    if (g_pIntNet)
    {
        INTNETR0Destroy(g_pIntNet);
        g_pIntNet = NULL;
    }
    LogFlow(("ModuleTerm: returns\n"));
#endif
}


/**
 * Initaties the R0 driver for a particular VM instance.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The VM instance in question.
 * @param   uVersion    The minimum module version required.
 */
static int VMMR0Init(PVM pVM, unsigned uVersion)
{
    /*
     * Check if compatible version.
     */
    if (    uVersion != VBOX_VERSION
        &&  (   VBOX_GET_VERSION_MAJOR(uVersion) != VBOX_VERSION_MAJOR
             || VBOX_GET_VERSION_MINOR(uVersion) < VBOX_VERSION_MINOR))
        return VERR_VERSION_MISMATCH;
    if (    !VALID_PTR(pVM)
        ||  pVM->pVMR0 != pVM)
        return VERR_INVALID_PARAMETER;

    /*
     * Register the EMT R0 logger instance.
     */
    PVMMR0LOGGER pR0Logger = pVM->vmm.s.pR0Logger;
    if (pR0Logger)
    {
#if 0 /* testing of the logger. */
        LogCom(("VMMR0Init: before %p\n", RTLogDefaultInstance()));
        LogCom(("VMMR0Init: pfnFlush=%p actual=%p\n", pR0Logger->Logger.pfnFlush, vmmR0LoggerFlush));
        LogCom(("VMMR0Init: pfnLogger=%p actual=%p\n", pR0Logger->Logger.pfnLogger, vmmR0LoggerWrapper));
        LogCom(("VMMR0Init: offScratch=%d fFlags=%#x fDestFlags=%#x\n", pR0Logger->Logger.offScratch, pR0Logger->Logger.fFlags, pR0Logger->Logger.fDestFlags));

        RTLogSetDefaultInstanceThread(&pR0Logger->Logger, (uintptr_t)pVM->pSession);
        LogCom(("VMMR0Init: after %p reg\n", RTLogDefaultInstance()));
        RTLogSetDefaultInstanceThread(NULL, 0);
        LogCom(("VMMR0Init: after %p dereg\n", RTLogDefaultInstance()));

        pR0Logger->Logger.pfnLogger("hello ring-0 logger\n");
        LogCom(("VMMR0Init: returned succesfully from direct logger call.\n"));
        pR0Logger->Logger.pfnFlush(&pR0Logger->Logger);
        LogCom(("VMMR0Init: returned succesfully from direct flush call.\n"));

        RTLogSetDefaultInstanceThread(&pR0Logger->Logger, (uintptr_t)pVM->pSession);
        LogCom(("VMMR0Init: after %p reg2\n", RTLogDefaultInstance()));
        pR0Logger->Logger.pfnLogger("hello ring-0 logger\n");
        LogCom(("VMMR0Init: returned succesfully from direct logger call (2). offScratch=%d\n", pR0Logger->Logger.offScratch));
        RTLogSetDefaultInstanceThread(NULL, 0);
        LogCom(("VMMR0Init: after %p dereg2\n", RTLogDefaultInstance()));

        RTLogLoggerEx(&pR0Logger->Logger, 0, ~0U, "hello ring-0 logger (RTLogLoggerEx)\n");
        LogCom(("VMMR0Init: RTLogLoggerEx returned fine offScratch=%d\n", pR0Logger->Logger.offScratch));
#endif
        RTLogSetDefaultInstanceThread(&pR0Logger->Logger, (uintptr_t)pVM->pSession);
    }


    /*
     * Init VMXM.
     */
    HWACCMR0Init(pVM);

    /*
     * Init CPUM.
     */
    int rc = CPUMR0Init(pVM);

    if (RT_FAILURE(rc))
        RTLogSetDefaultInstanceThread(NULL, 0);
    return rc;
}


/**
 * Terminates the R0 driver for a particular VM instance.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The VM instance in question.
 */
static int VMMR0Term(PVM pVM)
{
    /*
     * Deregister the logger.
     */
    RTLogSetDefaultInstanceThread(NULL, 0);
    return VINF_SUCCESS;
}


/**
 * Calls the ring-3 host code.
 *
 * @returns VBox status code of the ring-3 call.
 * @param   pVM             The VM handle.
 * @param   enmOperation    The operation.
 * @param   uArg            The argument to the operation.
 */
VMMR0DECL(int) VMMR0CallHost(PVM pVM, VMMCALLHOST enmOperation, uint64_t uArg)
{
/** @todo profile this! */
    pVM->vmm.s.enmCallHostOperation = enmOperation;
    pVM->vmm.s.u64CallHostArg = uArg;
    pVM->vmm.s.rcCallHost = VERR_INTERNAL_ERROR;
    int rc = vmmR0CallHostLongJmp(&pVM->vmm.s.CallHostR0JmpBuf, VINF_VMM_CALL_HOST);
    if (rc == VINF_SUCCESS)
        rc = pVM->vmm.s.rcCallHost;
    return rc;
}


#ifdef VBOX_WITH_STATISTICS
/**
 * Record return code statistics
 * @param   pVM         The VM handle.
 * @param   rc          The status code.
 */
static void vmmR0RecordRC(PVM pVM, int rc)
{
    /*
     * Collect statistics.
     */
    switch (rc)
    {
        case VINF_SUCCESS:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetNormal);
            break;
        case VINF_EM_RAW_INTERRUPT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetInterrupt);
            break;
        case VINF_EM_RAW_INTERRUPT_HYPER:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetInterruptHyper);
            break;
        case VINF_EM_RAW_GUEST_TRAP:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetGuestTrap);
            break;
        case VINF_EM_RAW_RING_SWITCH:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetRingSwitch);
            break;
        case VINF_EM_RAW_RING_SWITCH_INT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetRingSwitchInt);
            break;
        case VINF_EM_RAW_EXCEPTION_PRIVILEGED:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetExceptionPrivilege);
            break;
        case VINF_EM_RAW_STALE_SELECTOR:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetStaleSelector);
            break;
        case VINF_EM_RAW_IRET_TRAP:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetIRETTrap);
            break;
        case VINF_IOM_HC_IOPORT_READ:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetIORead);
            break;
        case VINF_IOM_HC_IOPORT_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetIOWrite);
            break;
        case VINF_IOM_HC_IOPORT_READWRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetIOReadWrite);
            break;
        case VINF_IOM_HC_MMIO_READ:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetMMIORead);
            break;
        case VINF_IOM_HC_MMIO_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetMMIOWrite);
            break;
        case VINF_IOM_HC_MMIO_READ_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetMMIOReadWrite);
            break;
        case VINF_PATM_HC_MMIO_PATCH_READ:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetMMIOPatchRead);
            break;
        case VINF_PATM_HC_MMIO_PATCH_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetMMIOPatchWrite);
            break;
        case VINF_EM_RAW_EMULATE_INSTR:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetEmulate);
            break;
        case VINF_PATCH_EMULATE_INSTR:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPatchEmulate);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_LDT_FAULT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetLDTFault);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_GDT_FAULT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetGDTFault);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_IDT_FAULT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetIDTFault);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_TSS_FAULT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetTSSFault);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_PD_FAULT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPDFault);
            break;
        case VINF_CSAM_PENDING_ACTION:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetCSAMTask);
            break;
        case VINF_PGM_SYNC_CR3:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetSyncCR3);
            break;
        case VINF_PATM_PATCH_INT3:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPatchInt3);
            break;
        case VINF_PATM_PATCH_TRAP_PF:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPatchPF);
            break;
        case VINF_PATM_PATCH_TRAP_GP:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPatchGP);
            break;
        case VINF_PATM_PENDING_IRQ_AFTER_IRET:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPatchIretIRQ);
            break;
        case VERR_REM_FLUSHED_PAGES_OVERFLOW:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPageOverflow);
            break;
        case VINF_EM_RESCHEDULE_REM:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetRescheduleREM);
            break;
        case VINF_EM_RAW_TO_R3:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetToR3);
            break;
        case VINF_EM_RAW_TIMER_PENDING:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetTimerPending);
            break;
        case VINF_EM_RAW_INTERRUPT_PENDING:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetInterruptPending);
            break;
        case VINF_VMM_CALL_HOST:
            switch (pVM->vmm.s.enmCallHostOperation)
            {
                case VMMCALLHOST_PDM_LOCK:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPDMLock);
                    break;
                case VMMCALLHOST_PDM_QUEUE_FLUSH:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPDMQueueFlush);
                    break;
                case VMMCALLHOST_PGM_POOL_GROW:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPGMPoolGrow);
                    break;
                case VMMCALLHOST_PGM_LOCK:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPGMLock);
                    break;
                case VMMCALLHOST_REM_REPLAY_HANDLER_NOTIFICATIONS:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetRemReplay);
                    break;
                case VMMCALLHOST_PGM_RAM_GROW_RANGE:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPGMGrowRAM);
                    break;
                case VMMCALLHOST_VMM_LOGGER_FLUSH:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetLogFlush);
                    break;
                case VMMCALLHOST_VM_SET_ERROR:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetVMSetError);
                    break;
                case VMMCALLHOST_VM_SET_RUNTIME_ERROR:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetVMSetRuntimeError);
                    break;
                default:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetCallHost);
                    break;
            }
            break;
        case VINF_PATM_DUPLICATE_FUNCTION:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPATMDuplicateFn);
            break;
        case VINF_PGM_CHANGE_MODE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPGMChangeMode);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_HLT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetEmulHlt);
            break;
        case VINF_EM_PENDING_REQUEST:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetPendingRequest);
            break;
        default:
            STAM_COUNTER_INC(&pVM->vmm.s.StatGCRetMisc);
            break;
    }
}
#endif /* VBOX_WITH_STATISTICS */


/**
 * The Ring 0 entry point, called by the support library (SUP).
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 * @param   uOperation  Which operation to execute. (VMMR0OPERATION)
 * @param   pvArg       Argument to the operation.
 */
VMMR0DECL(int) VMMR0Entry(PVM pVM, unsigned /* make me an enum */ uOperation, void *pvArg)
{
    switch (uOperation)
    {
        /*
         * Switch to GC.
         * These calls return whatever the GC returns.
         */
        case VMMR0_DO_RAW_RUN:
        {
            /* Safety precaution as VMX disables the switcher. */
            Assert(!pVM->vmm.s.fSwitcherDisabled);
            if (pVM->vmm.s.fSwitcherDisabled)
                return VERR_NOT_SUPPORTED;

            STAM_COUNTER_INC(&pVM->vmm.s.StatRunGC);
            register int rc;
            pVM->vmm.s.iLastGCRc = rc = pVM->vmm.s.pfnR0HostToGuest(pVM);

#ifdef VBOX_WITH_STATISTICS
            vmmR0RecordRC(pVM, rc);
#endif

            /*
             * Check if there is an exit R0 action associated with the return code.
             */
            switch (rc)
            {
                /*
                 * Default - no action, just return.
                 */
                default:
                    return rc;

                /*
                 * We'll let TRPM change the stack frame so our return is different.
                 * Just keep in mind that after the call, things have changed!
                 */
                case VINF_EM_RAW_INTERRUPT:
                case VINF_EM_RAW_INTERRUPT_HYPER:
                {
#ifdef VBOX_WITHOUT_IDT_PATCHING
                    TRPMR0DispatchHostInterrupt(pVM);
#else /* !VBOX_WITHOUT_IDT_PATCHING */
                    /*
                     * Don't trust the compiler to get this right.
                     * gcc -fomit-frame-pointer screws up big time here. This works fine in 64-bit
                     * mode too because we push the arguments on the stack in the IDT patch code.
                     */
# if defined(__GNUC__)
                    void *pvRet = (uint8_t *)__builtin_frame_address(0) + sizeof(void *);
# elif defined(_MSC_VER) && defined(__AMD64__) /** @todo check this with with VC7! */
                    void *pvRet = (uint8_t *)_AddressOfReturnAddress();
# elif defined(__X86__)
                    void *pvRet = (uint8_t *)&pVM - sizeof(pVM);
# else
#  error "huh?"
# endif
                    if (    ((uintptr_t *)pvRet)[1] == (uintptr_t)pVM
                        &&  ((uintptr_t *)pvRet)[2] == (uintptr_t)uOperation
                        &&  ((uintptr_t *)pvRet)[3] == (uintptr_t)pvArg)
                        TRPMR0SetupInterruptDispatcherFrame(pVM, pvRet);
                    else
                    {
# if defined(DEBUG) || defined(LOG_ENABLED)
                        static bool  s_fHaveWarned = false;
                        if (!s_fHaveWarned)
                        {
                             s_fHaveWarned = true;
                             //RTLogPrintf("VMMR0.r0: The compiler can't find the stack frame!\n"); -- @todo export me!
                             RTLogComPrintf("VMMR0.r0: The compiler can't find the stack frame!\n");
                        }
# endif
                        TRPMR0DispatchHostInterrupt(pVM);
                    }
#endif /* !VBOX_WITHOUT_IDT_PATCHING */
                    return rc;
                }
            }
            /* Won't get here! */
            break;
        }

        /*
         * Run guest code using the available hardware acceleration technology.
         */
        case VMMR0_DO_HWACC_RUN:
        {
            int rc;

            STAM_COUNTER_INC(&pVM->vmm.s.StatRunGC);
            rc = HWACCMR0Enable(pVM);
            if (VBOX_SUCCESS(rc))
            {
#ifdef DEBUG_NO_RING0_ASSERTIONS
                g_pVMAssert = pVM;
#endif
                rc = vmmR0CallHostSetJmp(&pVM->vmm.s.CallHostR0JmpBuf, HWACCMR0RunGuestCode, pVM); /* this may resume code. */
#ifdef DEBUG_NO_RING0_ASSERTIONS
                g_pVMAssert = 0;
#endif
                int rc2 = HWACCMR0Disable(pVM);
                AssertRC(rc2);
            }
            pVM->vmm.s.iLastGCRc = rc;

#ifdef VBOX_WITH_STATISTICS
            vmmR0RecordRC(pVM, rc);
#endif
            /* No special action required for external interrupts, just return. */
            return rc;
        }

        /*
         * Initialize the R0 part of a VM instance.
         */
        case VMMR0_DO_VMMR0_INIT:
        {
            RTCCUINTREG fFlags = ASMIntDisableFlags();
            int rc = VMMR0Init(pVM, (unsigned)(uintptr_t)pvArg);
            ASMSetFlags(fFlags);
            return rc;
        }

        /*
         * Terminate the R0 part of a VM instance.
         */
        case VMMR0_DO_VMMR0_TERM:
        {
            RTCCUINTREG fFlags = ASMIntDisableFlags();
            int rc = VMMR0Term(pVM);
            ASMSetFlags(fFlags);
            return rc;
        }

        /*
         * Setup the hardware accelerated raw-mode session.
         */
        case VMMR0_DO_HWACC_SETUP_VM:
        {
            RTCCUINTREG fFlags = ASMIntDisableFlags();
            int rc = HWACCMR0SetupVMX(pVM);
            ASMSetFlags(fFlags);
            return rc;
        }

        /*
         * Switch to GC to execute Hypervisor function.
         */
        case VMMR0_DO_CALL_HYPERVISOR:
        {
            /* Safety precaution as VMX disables the switcher. */
            Assert(!pVM->vmm.s.fSwitcherDisabled);
            if (pVM->vmm.s.fSwitcherDisabled)
                return VERR_NOT_SUPPORTED;

            RTCCUINTREG fFlags = ASMIntDisableFlags();
            int rc = pVM->vmm.s.pfnR0HostToGuest(pVM);
            ASMSetFlags(fFlags);
            return rc;
        }

#ifdef VBOX_WITH_INTERNAL_NETWORKING
        /*
         * Services.
         */
        case VMMR0_DO_INTNET_OPEN:
        case VMMR0_DO_INTNET_IF_CLOSE:
        case VMMR0_DO_INTNET_IF_GET_RING3_BUFFER:
        case VMMR0_DO_INTNET_IF_SET_PROMISCUOUS_MODE:
        case VMMR0_DO_INTNET_IF_SEND:
        case VMMR0_DO_INTNET_IF_WAIT:
        {
            /*
             * Validate arguments a bit first.
             */
            if (!VALID_PTR(pvArg))
                return VERR_INVALID_POINTER;
            if (!VALID_PTR(pVM))
                return VERR_INVALID_POINTER;
            if (pVM->pVMR0 != pVM)
                return VERR_INVALID_POINTER;
            if (!VALID_PTR(pVM->pSession))
                return VERR_INVALID_POINTER;
            if (!g_pIntNet)
                return VERR_FILE_NOT_FOUND; ///@todo fix this status code!

            /*
             * Unpack the arguments and call the service.
             */
            switch (uOperation)
            {
                case VMMR0_DO_INTNET_OPEN:
                {
                    PINTNETOPENARGS pArgs = (PINTNETOPENARGS)pvArg;
                    return INTNETR0Open(g_pIntNet, pVM->pSession, &pArgs->szNetwork[0], pArgs->cbSend, pArgs->cbRecv, pArgs->fRestrictAccess, &pArgs->hIf);
                }

                case VMMR0_DO_INTNET_IF_CLOSE:
                {
                    PINTNETIFCLOSEARGS pArgs = (PINTNETIFCLOSEARGS)pvArg;
                    return INTNETR0IfClose(g_pIntNet, pArgs->hIf);
                }

                case VMMR0_DO_INTNET_IF_GET_RING3_BUFFER:
                {
                    PINTNETIFGETRING3BUFFERARGS pArgs = (PINTNETIFGETRING3BUFFERARGS)pvArg;
                    return INTNETR0IfGetRing3Buffer(g_pIntNet, pArgs->hIf, &pArgs->pRing3Buf);
                }

                case VMMR0_DO_INTNET_IF_SET_PROMISCUOUS_MODE:
                {
                    PINTNETIFSETPROMISCUOUSMODEARGS pArgs = (PINTNETIFSETPROMISCUOUSMODEARGS)pvArg;
                    return INTNETR0IfSetPromiscuousMode(g_pIntNet, pArgs->hIf, pArgs->fPromiscuous);
                }

                case VMMR0_DO_INTNET_IF_SEND:
                {
                    PINTNETIFSENDARGS pArgs = (PINTNETIFSENDARGS)pvArg;
                    return INTNETR0IfSend(g_pIntNet, pArgs->hIf, pArgs->pvFrame, pArgs->cbFrame);
                }

                case VMMR0_DO_INTNET_IF_WAIT:
                {
                    PINTNETIFWAITARGS pArgs = (PINTNETIFWAITARGS)pvArg;
                    return INTNETR0IfWait(g_pIntNet, pArgs->hIf, pArgs->cMillies);
                }

                default:
                    return VERR_NOT_SUPPORTED;
            }
        }
#endif /* VBOX_WITH_INTERNAL_NETWORKING */

        /*
         * For profiling.
         */
        case VMMR0_DO_NOP:
            return VINF_SUCCESS;

        /*
         * For testing Ring-0 APIs invoked in this environment.
         */
        case VMMR0_DO_TESTS:
            /** @todo make new test */
            return VINF_SUCCESS;


        default:
            /*
             * We're returning VERR_NOT_SUPPORT here so we've got something else
             * than -1 which the interrupt gate glue code might return.
             */
            Log(("operation %#x is not supported\n", uOperation));
            return VERR_NOT_SUPPORTED;
    }
}


/**
 * Internal R0 logger worker: Flush logger.
 *
 * @param   pLogger     The logger instance to flush.
 * @remark  This function must be exported!
 */
VMMR0DECL(void) vmmR0LoggerFlush(PRTLOGGER pLogger)
{
    /*
     * Convert the pLogger into a VM handle and 'call' back to Ring-3.
     * (This is a bit paranoid code.)
     */
    PVMMR0LOGGER pR0Logger = (PVMMR0LOGGER)((uintptr_t)pLogger - RT_OFFSETOF(VMMR0LOGGER, Logger));
    if (    !VALID_PTR(pR0Logger)
        ||  !VALID_PTR(pR0Logger + 1)
        ||  !VALID_PTR(pLogger)
        ||  pLogger->u32Magic != RTLOGGER_MAGIC)
    {
        LogCom(("vmmR0LoggerFlush: pLogger=%p!\n", pLogger));
        return;
    }

    PVM pVM = pR0Logger->pVM;
    if (    !VALID_PTR(pVM)
        ||  pVM->pVMHC != pVM)
    {
        LogCom(("vmmR0LoggerFlush: pVM=%p! pLogger=%p\n", pVM, pLogger));
        return;
    }

    /*
     * Check that the jump buffer is armed.
     */
#ifdef __X86__
    if (!pVM->vmm.s.CallHostR0JmpBuf.eip)
#else
    if (!pVM->vmm.s.CallHostR0JmpBuf.rip)
#endif
    {
        LogCom(("vmmR0LoggerFlush: Jump buffer isn't armed!\n"));
        pLogger->offScratch = 0;
        return;
    }

    VMMR0CallHost(pVM, VMMCALLHOST_VMM_LOGGER_FLUSH, 0);
}

void R0LogFlush()
{
    vmmR0LoggerFlush(RTLogDefaultInstance());
}

#ifdef DEBUG_NO_RING0_ASSERTIONS
/**
 * Check if we really want to hit a breakpoint.
 * Can jump back to ring-3 when the longjmp is armed.
 */
DECLEXPORT(bool) RTCALL  RTAssertDoBreakpoint()
{
    if (g_pVMAssert)
    {
        g_pVMAssert->vmm.s.enmCallHostOperation = VMMCALLHOST_VMM_LOGGER_FLUSH;
        g_pVMAssert->vmm.s.u64CallHostArg = 0;
        g_pVMAssert->vmm.s.rcCallHost = VERR_INTERNAL_ERROR;
        int rc = vmmR0CallHostLongJmp(&g_pVMAssert->vmm.s.CallHostR0JmpBuf, VERR_INTERNAL_ERROR);
        if (rc == VINF_SUCCESS)
            rc = g_pVMAssert->vmm.s.rcCallHost;
    }

    return false;
}


#undef LOG_GROUP
#define LOG_GROUP LOG_GROUP_EM

/** Runtime assert implementation for Native Win32 Ring-0. */
DECLEXPORT(void) RTCALL AssertMsg1(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    Log(("\n!!R0-Assertion Failed!!\n"
         "Expression: %s\n"
         "Location  : %s(%d) %s\n",
         pszExpr, pszFile, uLine, pszFunction));
}

/**
 * Callback for RTLogFormatV which writes to the com port.
 * See PFNLOGOUTPUT() for details.
 */
static DECLCALLBACK(size_t) rtLogOutput(void *pv, const char *pachChars, size_t cbChars)
{
    for (size_t i=0;i<cbChars;i++)
        Log(("%c", pachChars[i]));

    return cbChars;
}

DECLEXPORT(void) RTCALL AssertMsg2(const char *pszFormat, ...)
{
    PRTLOGGER pLog = RTLogDefaultInstance();
    if (pLog)
    {
        va_list args;

        va_start(args, pszFormat);
        RTLogFormatV(rtLogOutput, pLog, pszFormat, args);
        va_end(args);
        R0LogFlush();
    }
}

#endif
