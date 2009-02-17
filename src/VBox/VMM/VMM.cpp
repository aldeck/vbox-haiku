/* $Id$ */
/** @file
 * VMM - The Virtual Machine Monitor Core.
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

//#define NO_SUPCALLR0VMM

/** @page pg_vmm        VMM - The Virtual Machine Monitor
 *
 * The VMM component is two things at the moment, it's a component doing a few
 * management and routing tasks, and it's the whole virtual machine monitor
 * thing.  For hysterical reasons, it is not doing all the management that one
 * would expect, this is instead done by @ref pg_vm.  We'll address this
 * misdesign eventually.
 *
 * @see grp_vmm, grp_vm
 *
 *
 * @section sec_vmmstate        VMM State
 *
 * @image html VM_Statechart_Diagram.gif
 *
 * To be written.
 *
 *
 * @subsection  subsec_vmm_init     VMM Initialization
 *
 * To be written.
 *
 *
 * @subsection  subsec_vmm_term     VMM Termination
 *
 * To be written.
 *
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_VMM
#include <VBox/vmm.h>
#include <VBox/vmapi.h>
#include <VBox/pgm.h>
#include <VBox/cfgm.h>
#include <VBox/pdmqueue.h>
#include <VBox/pdmapi.h>
#include <VBox/cpum.h>
#include <VBox/mm.h>
#include <VBox/iom.h>
#include <VBox/trpm.h>
#include <VBox/selm.h>
#include <VBox/em.h>
#include <VBox/sup.h>
#include <VBox/dbgf.h>
#include <VBox/csam.h>
#include <VBox/patm.h>
#include <VBox/rem.h>
#include <VBox/ssm.h>
#include <VBox/tm.h>
#include "VMMInternal.h"
#include "VMMSwitcher/VMMSwitcher.h"
#include <VBox/vm.h>

#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/version.h>
#include <VBox/x86.h>
#include <VBox/hwaccm.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/time.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/stdarg.h>
#include <iprt/ctype.h>



/** The saved state version. */
#define VMM_SAVED_STATE_VERSION     3


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static int                  vmmR3InitStacks(PVM pVM);
static int                  vmmR3InitLoggers(PVM pVM);
static void                 vmmR3InitRegisterStats(PVM pVM);
static DECLCALLBACK(int)    vmmR3Save(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(int)    vmmR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t u32Version);
static DECLCALLBACK(void)   vmmR3YieldEMT(PVM pVM, PTMTIMER pTimer, void *pvUser);
static int                  vmmR3ServiceCallHostRequest(PVM pVM);
static DECLCALLBACK(void)   vmmR3InfoFF(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);


/**
 * Initializes the VMM.
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
VMMR3DECL(int) VMMR3Init(PVM pVM)
{
    LogFlow(("VMMR3Init\n"));

    /*
     * Assert alignment, sizes and order.
     */
    AssertMsg(pVM->vmm.s.offVM == 0, ("Already initialized!\n"));
    AssertMsg(sizeof(pVM->vmm.padding) >= sizeof(pVM->vmm.s),
              ("pVM->vmm.padding is too small! vmm.padding %d while vmm.s is %d\n",
               sizeof(pVM->vmm.padding), sizeof(pVM->vmm.s)));

    /*
     * Init basic VM VMM members.
     */
    pVM->vmm.s.offVM = RT_OFFSETOF(VM, vmm);
    int rc = CFGMR3QueryU32(CFGMR3GetRoot(pVM), "YieldEMTInterval", &pVM->vmm.s.cYieldEveryMillies);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pVM->vmm.s.cYieldEveryMillies = 23; /* Value arrived at after experimenting with the grub boot prompt. */
        //pVM->vmm.s.cYieldEveryMillies = 8; //debugging
    else
        AssertMsgRCReturn(rc, ("Configuration error. Failed to query \"YieldEMTInterval\", rc=%Rrc\n", rc), rc);

    /* GC switchers are enabled by default. Turned off by HWACCM. */
    pVM->vmm.s.fSwitcherDisabled = false;

    /*
     * Register the saved state data unit.
     */
    rc = SSMR3RegisterInternal(pVM, "vmm", 1, VMM_SAVED_STATE_VERSION, VMM_STACK_SIZE + sizeof(RTGCPTR),
                               NULL, vmmR3Save, NULL,
                               NULL, vmmR3Load, NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register the Ring-0 VM handle with the session for fast ioctl calls.
     */
    rc = SUPSetVMForFastIOCtl(pVM->pVMR0);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Init various sub-components.
     */
    rc = vmmR3SwitcherInit(pVM);
    if (RT_SUCCESS(rc))
    {
        rc = vmmR3InitStacks(pVM);
        if (RT_SUCCESS(rc))
        {
            rc = vmmR3InitLoggers(pVM);

#ifdef VBOX_WITH_NMI
            /*
             * Allocate mapping for the host APIC.
             */
            if (RT_SUCCESS(rc))
            {
                rc = MMR3HyperReserve(pVM, PAGE_SIZE, "Host APIC", &pVM->vmm.s.GCPtrApicBase);
                AssertRC(rc);
            }
#endif
            if (RT_SUCCESS(rc))
            {
                rc = RTCritSectInit(&pVM->vmm.s.CritSectVMLock);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Debug info and statistics.
                     */
                    DBGFR3InfoRegisterInternal(pVM, "ff", "Displays the current Forced actions Flags.", vmmR3InfoFF);
                    vmmR3InitRegisterStats(pVM);

                    return VINF_SUCCESS;
                }
            }
        }
        /** @todo: Need failure cleanup. */

        //more todo in here?
        //if (RT_SUCCESS(rc))
        //{
        //}
        //int rc2 = vmmR3TermCoreCode(pVM);
        //AssertRC(rc2));
    }

    return rc;
}


/**
 * Allocate & setup the VMM RC stack(s) (for EMTs).
 *
 * The stacks are also used for long jumps in Ring-0.
 *
 * @returns VBox status code.
 * @param   pVM     Pointer to the shared VM structure.
 *
 * @remarks The optional guard page gets it protection setup up during R3 init
 *          completion because of init order issues.
 */
static int vmmR3InitStacks(PVM pVM)
{
    /** @todo SMP: On stack per vCPU. */
#ifdef VBOX_STRICT_VMM_STACK
    int rc = MMR3HyperAllocOnceNoRel(pVM, VMM_STACK_SIZE + PAGE_SIZE + PAGE_SIZE, PAGE_SIZE, MM_TAG_VMM, (void **)&pVM->vmm.s.pbEMTStackR3);
#else
    int rc = MMR3HyperAllocOnceNoRel(pVM, VMM_STACK_SIZE, PAGE_SIZE, MM_TAG_VMM, (void **)&pVM->vmm.s.pbEMTStackR3);
#endif
    if (RT_SUCCESS(rc))
    {
#ifdef VBOX_WITH_2X_4GB_ADDR_SPACE
        /* MMHyperR3ToR0 returns R3 when not doing hardware assisted virtualization. */
        if (!VMMIsHwVirtExtForced(pVM))
            pVM->vmm.s.CallHostR0JmpBuf.pvSavedStack = NIL_RTR0PTR;
        else
#endif
            pVM->vmm.s.CallHostR0JmpBuf.pvSavedStack = MMHyperR3ToR0(pVM, pVM->vmm.s.pbEMTStackR3);
        pVM->vmm.s.pbEMTStackRC = MMHyperR3ToRC(pVM, pVM->vmm.s.pbEMTStackR3);
        pVM->vmm.s.pbEMTStackBottomRC = pVM->vmm.s.pbEMTStackRC + VMM_STACK_SIZE;
        AssertRelease(pVM->vmm.s.pbEMTStackRC);

        CPUMSetHyperESP(pVM, pVM->vmm.s.pbEMTStackBottomRC);
    }

    return rc;
}


/**
 * Initialize the loggers.
 *
 * @returns VBox status code.
 * @param   pVM         Pointer to the shared VM structure.
 */
static int vmmR3InitLoggers(PVM pVM)
{
    int rc;

    /*
     * Allocate RC & R0 Logger instances (they are finalized in the relocator).
     */
#ifdef LOG_ENABLED
    PRTLOGGER pLogger = RTLogDefaultInstance();
    if (pLogger)
    {
        pVM->vmm.s.cbRCLogger = RT_OFFSETOF(RTLOGGERRC, afGroups[pLogger->cGroups]);
        rc = MMR3HyperAllocOnceNoRel(pVM, pVM->vmm.s.cbRCLogger, 0, MM_TAG_VMM, (void **)&pVM->vmm.s.pRCLoggerR3);
        if (RT_FAILURE(rc))
            return rc;
        pVM->vmm.s.pRCLoggerRC = MMHyperR3ToRC(pVM, pVM->vmm.s.pRCLoggerR3);

# ifdef VBOX_WITH_R0_LOGGING
        rc = MMR3HyperAllocOnceNoRel(pVM, RT_OFFSETOF(VMMR0LOGGER, Logger.afGroups[pLogger->cGroups]),
                                     0, MM_TAG_VMM, (void **)&pVM->vmm.s.pR0LoggerR3);
        if (RT_FAILURE(rc))
            return rc;
        pVM->vmm.s.pR0LoggerR3->pVM = pVM->pVMR0;
        //pVM->vmm.s.pR0LoggerR3->fCreated = false;
        pVM->vmm.s.pR0LoggerR3->cbLogger = RT_OFFSETOF(RTLOGGER, afGroups[pLogger->cGroups]);
        pVM->vmm.s.pR0LoggerR0 = MMHyperR3ToR0(pVM, pVM->vmm.s.pR0LoggerR3);
# endif
    }
#endif /* LOG_ENABLED */

#ifdef VBOX_WITH_RC_RELEASE_LOGGING
    /*
     * Allocate RC release logger instances (finalized in the relocator).
     */
    PRTLOGGER pRelLogger = RTLogRelDefaultInstance();
    if (pRelLogger)
    {
        pVM->vmm.s.cbRCRelLogger = RT_OFFSETOF(RTLOGGERRC, afGroups[pRelLogger->cGroups]);
        rc = MMR3HyperAllocOnceNoRel(pVM, pVM->vmm.s.cbRCRelLogger, 0, MM_TAG_VMM, (void **)&pVM->vmm.s.pRCRelLoggerR3);
        if (RT_FAILURE(rc))
            return rc;
        pVM->vmm.s.pRCRelLoggerRC = MMHyperR3ToRC(pVM, pVM->vmm.s.pRCRelLoggerR3);
    }
#endif /* VBOX_WITH_RC_RELEASE_LOGGING */
    return VINF_SUCCESS;
}


/**
 * VMMR3Init worker that register the statistics with STAM.
 *
 * @param   pVM         The shared VM structure.
 */
static void vmmR3InitRegisterStats(PVM pVM)
{
    /*
     * Statistics.
     */
    STAM_REG(pVM, &pVM->vmm.s.StatRunRC,                    STAMTYPE_COUNTER, "/VMM/RunRC",                     STAMUNIT_OCCURENCES, "Number of context switches.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetNormal,              STAMTYPE_COUNTER, "/VMM/RZRet/Normal",              STAMUNIT_OCCURENCES, "Number of VINF_SUCCESS returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetInterrupt,           STAMTYPE_COUNTER, "/VMM/RZRet/Interrupt",           STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_INTERRUPT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetInterruptHyper,      STAMTYPE_COUNTER, "/VMM/RZRet/InterruptHyper",      STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_INTERRUPT_HYPER returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetGuestTrap,           STAMTYPE_COUNTER, "/VMM/RZRet/GuestTrap",           STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_GUEST_TRAP returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetRingSwitch,          STAMTYPE_COUNTER, "/VMM/RZRet/RingSwitch",          STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_RING_SWITCH returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetRingSwitchInt,       STAMTYPE_COUNTER, "/VMM/RZRet/RingSwitchInt",       STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_RING_SWITCH_INT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetExceptionPrivilege,  STAMTYPE_COUNTER, "/VMM/RZRet/ExceptionPrivilege",  STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_EXCEPTION_PRIVILEGED returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetStaleSelector,       STAMTYPE_COUNTER, "/VMM/RZRet/StaleSelector",       STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_STALE_SELECTOR returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetIRETTrap,            STAMTYPE_COUNTER, "/VMM/RZRet/IRETTrap",            STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_IRET_TRAP returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetEmulate,             STAMTYPE_COUNTER, "/VMM/RZRet/Emulate",             STAMUNIT_OCCURENCES, "Number of VINF_EM_EXECUTE_INSTRUCTION returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPatchEmulate,        STAMTYPE_COUNTER, "/VMM/RZRet/PatchEmulate",        STAMUNIT_OCCURENCES, "Number of VINF_PATCH_EMULATE_INSTR returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetIORead,              STAMTYPE_COUNTER, "/VMM/RZRet/IORead",              STAMUNIT_OCCURENCES, "Number of VINF_IOM_HC_IOPORT_READ returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetIOWrite,             STAMTYPE_COUNTER, "/VMM/RZRet/IOWrite",             STAMUNIT_OCCURENCES, "Number of VINF_IOM_HC_IOPORT_WRITE returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMMIORead,            STAMTYPE_COUNTER, "/VMM/RZRet/MMIORead",            STAMUNIT_OCCURENCES, "Number of VINF_IOM_HC_MMIO_READ returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMMIOWrite,           STAMTYPE_COUNTER, "/VMM/RZRet/MMIOWrite",           STAMUNIT_OCCURENCES, "Number of VINF_IOM_HC_MMIO_WRITE returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMMIOReadWrite,       STAMTYPE_COUNTER, "/VMM/RZRet/MMIOReadWrite",       STAMUNIT_OCCURENCES, "Number of VINF_IOM_HC_MMIO_READ_WRITE returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMMIOPatchRead,       STAMTYPE_COUNTER, "/VMM/RZRet/MMIOPatchRead",       STAMUNIT_OCCURENCES, "Number of VINF_IOM_HC_MMIO_PATCH_READ returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMMIOPatchWrite,      STAMTYPE_COUNTER, "/VMM/RZRet/MMIOPatchWrite",      STAMUNIT_OCCURENCES, "Number of VINF_IOM_HC_MMIO_PATCH_WRITE returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetLDTFault,            STAMTYPE_COUNTER, "/VMM/RZRet/LDTFault",            STAMUNIT_OCCURENCES, "Number of VINF_EM_EXECUTE_INSTRUCTION_GDT_FAULT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetGDTFault,            STAMTYPE_COUNTER, "/VMM/RZRet/GDTFault",            STAMUNIT_OCCURENCES, "Number of VINF_EM_EXECUTE_INSTRUCTION_LDT_FAULT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetIDTFault,            STAMTYPE_COUNTER, "/VMM/RZRet/IDTFault",            STAMUNIT_OCCURENCES, "Number of VINF_EM_EXECUTE_INSTRUCTION_IDT_FAULT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetTSSFault,            STAMTYPE_COUNTER, "/VMM/RZRet/TSSFault",            STAMUNIT_OCCURENCES, "Number of VINF_EM_EXECUTE_INSTRUCTION_TSS_FAULT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPDFault,             STAMTYPE_COUNTER, "/VMM/RZRet/PDFault",             STAMUNIT_OCCURENCES, "Number of VINF_EM_EXECUTE_INSTRUCTION_PD_FAULT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetCSAMTask,            STAMTYPE_COUNTER, "/VMM/RZRet/CSAMTask",            STAMUNIT_OCCURENCES, "Number of VINF_CSAM_PENDING_ACTION returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetSyncCR3,             STAMTYPE_COUNTER, "/VMM/RZRet/SyncCR",              STAMUNIT_OCCURENCES, "Number of VINF_PGM_SYNC_CR3 returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetMisc,                STAMTYPE_COUNTER, "/VMM/RZRet/Misc",                STAMUNIT_OCCURENCES, "Number of misc returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPatchInt3,           STAMTYPE_COUNTER, "/VMM/RZRet/PatchInt3",           STAMUNIT_OCCURENCES, "Number of VINF_PATM_PATCH_INT3 returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPatchPF,             STAMTYPE_COUNTER, "/VMM/RZRet/PatchPF",             STAMUNIT_OCCURENCES, "Number of VINF_PATM_PATCH_TRAP_PF returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPatchGP,             STAMTYPE_COUNTER, "/VMM/RZRet/PatchGP",             STAMUNIT_OCCURENCES, "Number of VINF_PATM_PATCH_TRAP_GP returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPatchIretIRQ,        STAMTYPE_COUNTER, "/VMM/RZRet/PatchIret",           STAMUNIT_OCCURENCES, "Number of VINF_PATM_PENDING_IRQ_AFTER_IRET returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPageOverflow,        STAMTYPE_COUNTER, "/VMM/RZRet/InvlpgOverflow",      STAMUNIT_OCCURENCES, "Number of VERR_REM_FLUSHED_PAGES_OVERFLOW returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetRescheduleREM,       STAMTYPE_COUNTER, "/VMM/RZRet/ScheduleREM",         STAMUNIT_OCCURENCES, "Number of VINF_EM_RESCHEDULE_REM returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetToR3,                STAMTYPE_COUNTER, "/VMM/RZRet/ToR3",                STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TO_R3 returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetTimerPending,        STAMTYPE_COUNTER, "/VMM/RZRet/TimerPending",        STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_TIMER_PENDING returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetInterruptPending,    STAMTYPE_COUNTER, "/VMM/RZRet/InterruptPending",    STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_INTERRUPT_PENDING returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPATMDuplicateFn,     STAMTYPE_COUNTER, "/VMM/RZRet/PATMDuplicateFn",     STAMUNIT_OCCURENCES, "Number of VINF_PATM_DUPLICATE_FUNCTION returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPGMChangeMode,       STAMTYPE_COUNTER, "/VMM/RZRet/PGMChangeMode",       STAMUNIT_OCCURENCES, "Number of VINF_PGM_CHANGE_MODE returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetEmulHlt,             STAMTYPE_COUNTER, "/VMM/RZRet/EmulHlt",             STAMUNIT_OCCURENCES, "Number of VINF_EM_RAW_EMULATE_INSTR_HLT returns.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZRetPendingRequest,      STAMTYPE_COUNTER, "/VMM/RZRet/PendingRequest",      STAMUNIT_OCCURENCES, "Number of VINF_EM_PENDING_REQUEST returns.");

    STAM_REG(pVM, &pVM->vmm.s.StatRZRetCallHost,            STAMTYPE_COUNTER, "/VMM/RZCallR3/Misc",             STAMUNIT_OCCURENCES, "Number of Other ring-3 calls.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZCallPDMLock,            STAMTYPE_COUNTER, "/VMM/RZCallR3/PDMLock",          STAMUNIT_OCCURENCES, "Number of VMMCALLHOST_PDM_LOCK calls.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZCallPDMQueueFlush,      STAMTYPE_COUNTER, "/VMM/RZCallR3/PDMQueueFlush",    STAMUNIT_OCCURENCES, "Number of VMMCALLHOST_PDM_QUEUE_FLUSH calls.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZCallPGMLock,            STAMTYPE_COUNTER, "/VMM/RZCallR3/PGMLock",          STAMUNIT_OCCURENCES, "Number of VMMCALLHOST_PGM_LOCK calls.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZCallPGMPoolGrow,        STAMTYPE_COUNTER, "/VMM/RZCallR3/PGMPoolGrow",      STAMUNIT_OCCURENCES, "Number of VMMCALLHOST_PGM_POOL_GROW calls.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZCallPGMMapChunk,        STAMTYPE_COUNTER, "/VMM/RZCallR3/PGMMapChunk",      STAMUNIT_OCCURENCES, "Number of VMMCALLHOST_PGM_MAP_CHUNK calls.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZCallPGMAllocHandy,      STAMTYPE_COUNTER, "/VMM/RZCallR3/PGMAllocHandy",    STAMUNIT_OCCURENCES, "Number of VMMCALLHOST_PGM_ALLOCATE_HANDY_PAGES calls.");
#ifndef VBOX_WITH_NEW_PHYS_CODE
    STAM_REG(pVM, &pVM->vmm.s.StatRZCallPGMGrowRAM,         STAMTYPE_COUNTER, "/VMM/RZCallR3/PGMGrowRAM",       STAMUNIT_OCCURENCES, "Number of VMMCALLHOST_PGM_RAM_GROW_RANGE calls.");
#endif
    STAM_REG(pVM, &pVM->vmm.s.StatRZCallRemReplay,          STAMTYPE_COUNTER, "/VMM/RZCallR3/REMReplay",        STAMUNIT_OCCURENCES, "Number of VMMCALLHOST_REM_REPLAY_HANDLER_NOTIFICATIONS calls.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZCallLogFlush,           STAMTYPE_COUNTER, "/VMM/RZCallR3/VMMLogFlush",      STAMUNIT_OCCURENCES, "Number of VMMCALLHOST_VMM_LOGGER_FLUSH calls.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZCallVMSetError,         STAMTYPE_COUNTER, "/VMM/RZCallR3/VMSetError",       STAMUNIT_OCCURENCES, "Number of VMMCALLHOST_VM_SET_ERROR calls.");
    STAM_REG(pVM, &pVM->vmm.s.StatRZCallVMSetRuntimeError,  STAMTYPE_COUNTER, "/VMM/RZCallR3/VMRuntimeError",   STAMUNIT_OCCURENCES, "Number of VMMCALLHOST_VM_SET_RUNTIME_ERROR calls.");
}


/**
 * Initializes the per-VCPU VMM.
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
VMMR3DECL(int) VMMR3InitCPU(PVM pVM)
{
    LogFlow(("VMMR3InitCPU\n"));
    return VINF_SUCCESS;
}


/**
 * Ring-3 init finalizing.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 */
VMMR3DECL(int) VMMR3InitFinalize(PVM pVM)
{
#ifdef VBOX_STRICT_VMM_STACK
    /*
     * Two inaccessible pages at each sides of the stack to catch over/under-flows.
     */
    memset(pVM->vmm.s.pbEMTStackR3 - PAGE_SIZE, 0xcc, PAGE_SIZE);
    PGMMapSetPage(pVM, MMHyperR3ToRC(pVM, pVM->vmm.s.pbEMTStackR3 - PAGE_SIZE), PAGE_SIZE, 0);
    RTMemProtect(pVM->vmm.s.pbEMTStackR3 - PAGE_SIZE, PAGE_SIZE, RTMEM_PROT_NONE);

    memset(pVM->vmm.s.pbEMTStackR3 + VMM_STACK_SIZE, 0xcc, PAGE_SIZE);
    PGMMapSetPage(pVM, MMHyperR3ToRC(pVM, pVM->vmm.s.pbEMTStackR3 + VMM_STACK_SIZE), PAGE_SIZE, 0);
    RTMemProtect(pVM->vmm.s.pbEMTStackR3 + VMM_STACK_SIZE, PAGE_SIZE, RTMEM_PROT_NONE);
#endif

    /*
     * Set page attributes to r/w for stack pages.
     */
    int rc = PGMMapSetPage(pVM, pVM->vmm.s.pbEMTStackRC, VMM_STACK_SIZE, X86_PTE_P | X86_PTE_A | X86_PTE_D | X86_PTE_RW);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        /*
         * Create the EMT yield timer.
         */
        rc = TMR3TimerCreateInternal(pVM, TMCLOCK_REAL, vmmR3YieldEMT, NULL, "EMT Yielder", &pVM->vmm.s.pYieldTimer);
        if (RT_SUCCESS(rc))
           rc = TMTimerSetMillies(pVM->vmm.s.pYieldTimer, pVM->vmm.s.cYieldEveryMillies);
    }

#ifdef VBOX_WITH_NMI
    /*
     * Map the host APIC into GC - This is AMD/Intel + Host OS specific!
     */
    if (RT_SUCCESS(rc))
        rc = PGMMap(pVM, pVM->vmm.s.GCPtrApicBase, 0xfee00000, PAGE_SIZE,
                    X86_PTE_P | X86_PTE_RW | X86_PTE_PWT | X86_PTE_PCD | X86_PTE_A | X86_PTE_D);
#endif
    return rc;
}


/**
 * Initializes the R0 VMM.
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
VMMR3DECL(int) VMMR3InitR0(PVM pVM)
{
    int rc;

    /*
     * Initialize the ring-0 logger if we haven't done so yet.
     */
    if (    pVM->vmm.s.pR0LoggerR3
        &&  !pVM->vmm.s.pR0LoggerR3->fCreated)
    {
        rc = VMMR3UpdateLoggers(pVM);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Call Ring-0 entry with init code.
     */
    for (;;)
    {
#ifdef NO_SUPCALLR0VMM
        //rc = VERR_GENERAL_FAILURE;
        rc = VINF_SUCCESS;
#else
        rc = SUPCallVMMR0Ex(pVM->pVMR0, VMMR0_DO_VMMR0_INIT, VMMGetSvnRev(), NULL);
#endif
        if (    pVM->vmm.s.pR0LoggerR3
            &&  pVM->vmm.s.pR0LoggerR3->Logger.offScratch > 0)
            RTLogFlushToLogger(&pVM->vmm.s.pR0LoggerR3->Logger, NULL);
        if (rc != VINF_VMM_CALL_HOST)
            break;
        rc = vmmR3ServiceCallHostRequest(pVM);
        if (RT_FAILURE(rc) || (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST))
            break;
        /* Resume R0 */
    }

    if (RT_FAILURE(rc) || (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST))
    {
        LogRel(("R0 init failed, rc=%Rra\n", rc));
        if (RT_SUCCESS(rc))
            rc = VERR_INTERNAL_ERROR;
    }
    return rc;
}


/**
 * Initializes the RC VMM.
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
VMMR3DECL(int) VMMR3InitRC(PVM pVM)
{
    /* In VMX mode, there's no need to init RC. */
    if (pVM->vmm.s.fSwitcherDisabled)
        return VINF_SUCCESS;

    /*
     * Call VMMGCInit():
     *      -# resolve the address.
     *      -# setup stackframe and EIP to use the trampoline.
     *      -# do a generic hypervisor call.
     */
    RTRCPTR RCPtrEP;
    int rc = PDMR3LdrGetSymbolRC(pVM, VMMGC_MAIN_MODULE_NAME, "VMMGCEntry", &RCPtrEP);
    if (RT_SUCCESS(rc))
    {
        CPUMHyperSetCtxCore(pVM, NULL);
        CPUMSetHyperESP(pVM, pVM->vmm.s.pbEMTStackBottomRC); /* Clear the stack. */
        uint64_t u64TS = RTTimeProgramStartNanoTS();
        CPUMPushHyper(pVM, (uint32_t)(u64TS >> 32));    /* Param 3: The program startup TS - Hi. */
        CPUMPushHyper(pVM, (uint32_t)u64TS);            /* Param 3: The program startup TS - Lo. */
        CPUMPushHyper(pVM, VMMGetSvnRev());             /* Param 2: Version argument. */
        CPUMPushHyper(pVM, VMMGC_DO_VMMGC_INIT);        /* Param 1: Operation. */
        CPUMPushHyper(pVM, pVM->pVMRC);                 /* Param 0: pVM */
        CPUMPushHyper(pVM, 5 * sizeof(RTRCPTR));        /* trampoline param: stacksize.  */
        CPUMPushHyper(pVM, RCPtrEP);                    /* Call EIP. */
        CPUMSetHyperEIP(pVM, pVM->vmm.s.pfnCallTrampolineRC);

        for (;;)
        {
#ifdef NO_SUPCALLR0VMM
            //rc = VERR_GENERAL_FAILURE;
            rc = VINF_SUCCESS;
#else
            rc = SUPCallVMMR0(pVM->pVMR0, VMMR0_DO_CALL_HYPERVISOR, NULL);
#endif
#ifdef LOG_ENABLED
            PRTLOGGERRC pLogger = pVM->vmm.s.pRCLoggerR3;
            if (    pLogger
                &&  pLogger->offScratch > 0)
                RTLogFlushRC(NULL, pLogger);
#endif
#ifdef VBOX_WITH_RC_RELEASE_LOGGING
            PRTLOGGERRC pRelLogger = pVM->vmm.s.pRCRelLoggerR3;
            if (RT_UNLIKELY(pRelLogger && pRelLogger->offScratch > 0))
                RTLogFlushRC(RTLogRelDefaultInstance(), pRelLogger);
#endif
            if (rc != VINF_VMM_CALL_HOST)
                break;
            rc = vmmR3ServiceCallHostRequest(pVM);
            if (RT_FAILURE(rc) || (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST))
                break;
        }

        if (RT_FAILURE(rc) || (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST))
        {
            VMMR3FatalDump(pVM, rc);
            if (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST)
                rc = VERR_INTERNAL_ERROR;
        }
        AssertRC(rc);
    }
    return rc;
}


/**
 * Terminate the VMM bits.
 *
 * @returns VINF_SUCCESS.
 * @param   pVM         The VM handle.
 */
VMMR3DECL(int) VMMR3Term(PVM pVM)
{
    /*
     * Call Ring-0 entry with termination code.
     */
    int rc;
    for (;;)
    {
#ifdef NO_SUPCALLR0VMM
        //rc = VERR_GENERAL_FAILURE;
        rc = VINF_SUCCESS;
#else
        rc = SUPCallVMMR0Ex(pVM->pVMR0, VMMR0_DO_VMMR0_TERM, 0, NULL);
#endif
        if (    pVM->vmm.s.pR0LoggerR3
            &&  pVM->vmm.s.pR0LoggerR3->Logger.offScratch > 0)
            RTLogFlushToLogger(&pVM->vmm.s.pR0LoggerR3->Logger, NULL);
        if (rc != VINF_VMM_CALL_HOST)
            break;
        rc = vmmR3ServiceCallHostRequest(pVM);
        if (RT_FAILURE(rc) || (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST))
            break;
        /* Resume R0 */
    }
    if (RT_FAILURE(rc) || (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST))
    {
        LogRel(("VMMR3Term: R0 term failed, rc=%Rra. (warning)\n", rc));
        if (RT_SUCCESS(rc))
            rc = VERR_INTERNAL_ERROR;
    }

#ifdef VBOX_STRICT_VMM_STACK
    /*
     * Make the two stack guard pages present again.
     */
    RTMemProtect(pVM->vmm.s.pbEMTStackR3 - PAGE_SIZE,      PAGE_SIZE, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
    RTMemProtect(pVM->vmm.s.pbEMTStackR3 + VMM_STACK_SIZE, PAGE_SIZE, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
#endif
    return rc;
}


/**
 * Terminates the per-VCPU VMM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM it self is at this point powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
VMMR3DECL(int) VMMR3TermCPU(PVM pVM)
{
    return VINF_SUCCESS;
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * The VMM will need to apply relocations to the core code.
 *
 * @param   pVM         The VM handle.
 * @param   offDelta    The relocation delta.
 */
VMMR3DECL(void) VMMR3Relocate(PVM pVM, RTGCINTPTR offDelta)
{
    LogFlow(("VMMR3Relocate: offDelta=%RGv\n", offDelta));

    /*
     * Recalc the RC address.
     */
    pVM->vmm.s.pvCoreCodeRC = MMHyperR3ToRC(pVM, pVM->vmm.s.pvCoreCodeR3);

    /*
     * The stack.
     */
    CPUMSetHyperESP(pVM, CPUMGetHyperESP(pVM) + offDelta);
    pVM->vmm.s.pbEMTStackRC = MMHyperR3ToRC(pVM, pVM->vmm.s.pbEMTStackR3);
    pVM->vmm.s.pbEMTStackBottomRC = pVM->vmm.s.pbEMTStackRC + VMM_STACK_SIZE;

    /*
     * All the switchers.
     */
    vmmR3SwitcherRelocate(pVM, offDelta);

    /*
     * Get other RC entry points.
     */
    int rc = PDMR3LdrGetSymbolRC(pVM, VMMGC_MAIN_MODULE_NAME, "CPUMGCResumeGuest", &pVM->vmm.s.pfnCPUMRCResumeGuest);
    AssertReleaseMsgRC(rc, ("CPUMGCResumeGuest not found! rc=%Rra\n", rc));

    rc = PDMR3LdrGetSymbolRC(pVM, VMMGC_MAIN_MODULE_NAME, "CPUMGCResumeGuestV86", &pVM->vmm.s.pfnCPUMRCResumeGuestV86);
    AssertReleaseMsgRC(rc, ("CPUMGCResumeGuestV86 not found! rc=%Rra\n", rc));

    /*
     * Update the logger.
     */
    VMMR3UpdateLoggers(pVM);
}


/**
 * Updates the settings for the RC and R0 loggers.
 *
 * @returns VBox status code.
 * @param   pVM     The VM handle.
 */
VMMR3DECL(int)  VMMR3UpdateLoggers(PVM pVM)
{
    /*
     * Simply clone the logger instance (for RC).
     */
    int rc = VINF_SUCCESS;
    RTRCPTR RCPtrLoggerFlush = 0;

    if (pVM->vmm.s.pRCLoggerR3
#ifdef VBOX_WITH_RC_RELEASE_LOGGING
        || pVM->vmm.s.pRCRelLoggerR3
#endif
       )
    {
        rc = PDMR3LdrGetSymbolRC(pVM, VMMGC_MAIN_MODULE_NAME, "vmmGCLoggerFlush", &RCPtrLoggerFlush);
        AssertReleaseMsgRC(rc, ("vmmGCLoggerFlush not found! rc=%Rra\n", rc));
    }

    if (pVM->vmm.s.pRCLoggerR3)
    {
        RTRCPTR RCPtrLoggerWrapper = 0;
        rc = PDMR3LdrGetSymbolRC(pVM, VMMGC_MAIN_MODULE_NAME, "vmmGCLoggerWrapper", &RCPtrLoggerWrapper);
        AssertReleaseMsgRC(rc, ("vmmGCLoggerWrapper not found! rc=%Rra\n", rc));

        pVM->vmm.s.pRCLoggerRC = MMHyperR3ToRC(pVM, pVM->vmm.s.pRCLoggerR3);
        rc = RTLogCloneRC(NULL /* default */, pVM->vmm.s.pRCLoggerR3, pVM->vmm.s.cbRCLogger,
                          RCPtrLoggerWrapper,  RCPtrLoggerFlush, RTLOGFLAGS_BUFFERED);
        AssertReleaseMsgRC(rc, ("RTLogCloneRC failed! rc=%Rra\n", rc));
    }

#ifdef VBOX_WITH_RC_RELEASE_LOGGING
    if (pVM->vmm.s.pRCRelLoggerR3)
    {
        RTRCPTR RCPtrLoggerWrapper = 0;
        rc = PDMR3LdrGetSymbolRC(pVM, VMMGC_MAIN_MODULE_NAME, "vmmGCRelLoggerWrapper", &RCPtrLoggerWrapper);
        AssertReleaseMsgRC(rc, ("vmmGCRelLoggerWrapper not found! rc=%Rra\n", rc));

        pVM->vmm.s.pRCRelLoggerRC = MMHyperR3ToRC(pVM, pVM->vmm.s.pRCRelLoggerR3);
        rc = RTLogCloneRC(RTLogRelDefaultInstance(), pVM->vmm.s.pRCRelLoggerR3, pVM->vmm.s.cbRCRelLogger,
                          RCPtrLoggerWrapper,  RCPtrLoggerFlush, RTLOGFLAGS_BUFFERED);
        AssertReleaseMsgRC(rc, ("RTLogCloneRC failed! rc=%Rra\n", rc));
    }
#endif /* VBOX_WITH_RC_RELEASE_LOGGING */

    /*
     * For the ring-0 EMT logger, we use a per-thread logger instance
     * in ring-0. Only initialize it once.
     */
    PVMMR0LOGGER pR0LoggerR3 = pVM->vmm.s.pR0LoggerR3;
    if (pR0LoggerR3)
    {
        if (!pR0LoggerR3->fCreated)
        {
            RTR0PTR pfnLoggerWrapper = NIL_RTR0PTR;
            rc = PDMR3LdrGetSymbolR0(pVM, VMMR0_MAIN_MODULE_NAME, "vmmR0LoggerWrapper", &pfnLoggerWrapper);
            AssertReleaseMsgRCReturn(rc, ("VMMLoggerWrapper not found! rc=%Rra\n", rc), rc);

            RTR0PTR pfnLoggerFlush = NIL_RTR0PTR;
            rc = PDMR3LdrGetSymbolR0(pVM, VMMR0_MAIN_MODULE_NAME, "vmmR0LoggerFlush", &pfnLoggerFlush);
            AssertReleaseMsgRCReturn(rc, ("VMMLoggerFlush not found! rc=%Rra\n", rc), rc);

            rc = RTLogCreateForR0(&pR0LoggerR3->Logger, pR0LoggerR3->cbLogger,
                                  *(PFNRTLOGGER *)&pfnLoggerWrapper, *(PFNRTLOGFLUSH *)&pfnLoggerFlush,
                                  RTLOGFLAGS_BUFFERED, RTLOGDEST_DUMMY);
            AssertReleaseMsgRCReturn(rc, ("RTLogCreateForR0 failed! rc=%Rra\n", rc), rc);
            pR0LoggerR3->fCreated = true;
            pR0LoggerR3->fFlushingDisabled = false;
        }

        rc = RTLogCopyGroupsAndFlags(&pR0LoggerR3->Logger, NULL /* default */, pVM->vmm.s.pRCLoggerR3->fFlags, RTLOGFLAGS_BUFFERED);
        AssertRC(rc);
    }

    return rc;
}


/**
 * Gets the pointer to a buffer containing the R0/RC AssertMsg1 output.
 *
 * @returns Pointer to the buffer.
 * @param   pVM         The VM handle.
 */
VMMR3DECL(const char *) VMMR3GetRZAssertMsg1(PVM pVM)
{
    if (HWACCMIsEnabled(pVM))
        return pVM->vmm.s.szRing0AssertMsg1;

    RTRCPTR RCPtr;
    int rc = PDMR3LdrGetSymbolRC(pVM, NULL, "g_szRTAssertMsg1", &RCPtr);
    if (RT_SUCCESS(rc))
        return (const char *)MMHyperRCToR3(pVM, RCPtr);

    return NULL;
}


/**
 * Gets the pointer to a buffer containing the R0/RC AssertMsg2 output.
 *
 * @returns Pointer to the buffer.
 * @param   pVM         The VM handle.
 */
VMMR3DECL(const char *) VMMR3GetRZAssertMsg2(PVM pVM)
{
    if (HWACCMIsEnabled(pVM))
        return pVM->vmm.s.szRing0AssertMsg2;

    RTRCPTR RCPtr;
    int rc = PDMR3LdrGetSymbolRC(pVM, NULL, "g_szRTAssertMsg2", &RCPtr);
    if (RT_SUCCESS(rc))
        return (const char *)MMHyperRCToR3(pVM, RCPtr);

    return NULL;
}


/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pVM             VM Handle.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) vmmR3Save(PVM pVM, PSSMHANDLE pSSM)
{
    LogFlow(("vmmR3Save:\n"));

    /*
     * The hypervisor stack.
     * Note! See not in vmmR3Load.
     */
    SSMR3PutRCPtr(pSSM, pVM->vmm.s.pbEMTStackBottomRC);
    RTRCPTR RCPtrESP = CPUMGetHyperESP(pVM);
    AssertMsg(pVM->vmm.s.pbEMTStackBottomRC - RCPtrESP <= VMM_STACK_SIZE, ("Bottom %RRv ESP=%RRv\n", pVM->vmm.s.pbEMTStackBottomRC, RCPtrESP));
    SSMR3PutRCPtr(pSSM, RCPtrESP);
    SSMR3PutMem(pSSM, pVM->vmm.s.pbEMTStackR3, VMM_STACK_SIZE);
    return SSMR3PutU32(pSSM, ~0); /* terminator */
}


/**
 * Execute state load operation.
 *
 * @returns VBox status code.
 * @param   pVM             VM Handle.
 * @param   pSSM            SSM operation handle.
 * @param   u32Version      Data layout version.
 */
static DECLCALLBACK(int) vmmR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t u32Version)
{
    LogFlow(("vmmR3Load:\n"));

    /*
     * Validate version.
     */
    if (u32Version != VMM_SAVED_STATE_VERSION)
    {
        AssertMsgFailed(("vmmR3Load: Invalid version u32Version=%d!\n", u32Version));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    /*
     * Check that the stack is in the same place, or that it's fearly empty.
     *
     * Note! This can be skipped next time we update saved state as we will
     *       never be in a R0/RC -> ring-3 call when saving the state. The
     *       stack and the two associated pointers are not required.
     */
    RTRCPTR RCPtrStackBottom;
    SSMR3GetRCPtr(pSSM, &RCPtrStackBottom);
    RTRCPTR RCPtrESP;
    int rc = SSMR3GetRCPtr(pSSM, &RCPtrESP);
    if (RT_FAILURE(rc))
        return rc;

    /* restore the stack.  */
    SSMR3GetMem(pSSM, pVM->vmm.s.pbEMTStackR3, VMM_STACK_SIZE);

    /* terminator */
    uint32_t u32;
    rc = SSMR3GetU32(pSSM, &u32);
    if (RT_FAILURE(rc))
        return rc;
    if (u32 != ~0U)
    {
        AssertMsgFailed(("u32=%#x\n", u32));
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    }
    return VINF_SUCCESS;
}


/**
 * Resolve a builtin RC symbol.
 *
 * Called by PDM when loading or relocating RC modules.
 *
 * @returns VBox status
 * @param   pVM             VM Handle.
 * @param   pszSymbol       Symbol to resolv
 * @param   pRCPtrValue     Where to store the symbol value.
 *
 * @remark  This has to work before VMMR3Relocate() is called.
 */
VMMR3DECL(int) VMMR3GetImportRC(PVM pVM, const char *pszSymbol, PRTRCPTR pRCPtrValue)
{
    if (!strcmp(pszSymbol, "g_Logger"))
    {
        if (pVM->vmm.s.pRCLoggerR3)
            pVM->vmm.s.pRCLoggerRC = MMHyperR3ToRC(pVM, pVM->vmm.s.pRCLoggerR3);
        *pRCPtrValue = pVM->vmm.s.pRCLoggerRC;
    }
    else if (!strcmp(pszSymbol, "g_RelLogger"))
    {
#ifdef VBOX_WITH_RC_RELEASE_LOGGING
        if (pVM->vmm.s.pRCRelLoggerR3)
            pVM->vmm.s.pRCRelLoggerRC = MMHyperR3ToRC(pVM, pVM->vmm.s.pRCRelLoggerR3);
        *pRCPtrValue = pVM->vmm.s.pRCRelLoggerRC;
#else
        *pRCPtrValue = NIL_RTRCPTR;
#endif
    }
    else
        return VERR_SYMBOL_NOT_FOUND;
    return VINF_SUCCESS;
}


/**
 * Suspends the CPU yielder.
 *
 * @param   pVM             The VM handle.
 */
VMMR3DECL(void) VMMR3YieldSuspend(PVM pVM)
{
    if (!pVM->vmm.s.cYieldResumeMillies)
    {
        uint64_t u64Now = TMTimerGet(pVM->vmm.s.pYieldTimer);
        uint64_t u64Expire = TMTimerGetExpire(pVM->vmm.s.pYieldTimer);
        if (u64Now >= u64Expire || u64Expire == ~(uint64_t)0)
            pVM->vmm.s.cYieldResumeMillies = pVM->vmm.s.cYieldEveryMillies;
        else
            pVM->vmm.s.cYieldResumeMillies = TMTimerToMilli(pVM->vmm.s.pYieldTimer, u64Expire - u64Now);
        TMTimerStop(pVM->vmm.s.pYieldTimer);
    }
    pVM->vmm.s.u64LastYield = RTTimeNanoTS();
}


/**
 * Stops the CPU yielder.
 *
 * @param   pVM             The VM handle.
 */
VMMR3DECL(void) VMMR3YieldStop(PVM pVM)
{
    if (!pVM->vmm.s.cYieldResumeMillies)
        TMTimerStop(pVM->vmm.s.pYieldTimer);
    pVM->vmm.s.cYieldResumeMillies = pVM->vmm.s.cYieldEveryMillies;
    pVM->vmm.s.u64LastYield = RTTimeNanoTS();
}


/**
 * Resumes the CPU yielder when it has been a suspended or stopped.
 *
 * @param   pVM             The VM handle.
 */
VMMR3DECL(void) VMMR3YieldResume(PVM pVM)
{
    if (pVM->vmm.s.cYieldResumeMillies)
    {
        TMTimerSetMillies(pVM->vmm.s.pYieldTimer, pVM->vmm.s.cYieldResumeMillies);
        pVM->vmm.s.cYieldResumeMillies = 0;
    }
}


/**
 * Internal timer callback function.
 *
 * @param   pVM             The VM.
 * @param   pTimer          The timer handle.
 * @param   pvUser          User argument specified upon timer creation.
 */
static DECLCALLBACK(void) vmmR3YieldEMT(PVM pVM, PTMTIMER pTimer, void *pvUser)
{
    /*
     * This really needs some careful tuning. While we shouldn't be too greedy since
     * that'll cause the rest of the system to stop up, we shouldn't be too nice either
     * because that'll cause us to stop up.
     *
     * The current logic is to use the default interval when there is no lag worth
     * mentioning, but when we start accumulating lag we don't bother yielding at all.
     *
     * (This depends on the TMCLOCK_VIRTUAL_SYNC to be scheduled before TMCLOCK_REAL
     * so the lag is up to date.)
     */
    const uint64_t u64Lag = TMVirtualSyncGetLag(pVM);
    if (    u64Lag     <   50000000 /* 50ms */
        ||  (   u64Lag < 1000000000 /*  1s */
             && RTTimeNanoTS() - pVM->vmm.s.u64LastYield < 500000000 /* 500 ms */)
       )
    {
        uint64_t u64Elapsed = RTTimeNanoTS();
        pVM->vmm.s.u64LastYield = u64Elapsed;

        RTThreadYield();

#ifdef LOG_ENABLED
        u64Elapsed = RTTimeNanoTS() - u64Elapsed;
        Log(("vmmR3YieldEMT: %RI64 ns\n", u64Elapsed));
#endif
    }
    TMTimerSetMillies(pTimer, pVM->vmm.s.cYieldEveryMillies);
}


/**
 * Acquire global VM lock.
 *
 * @returns VBox status code
 * @param   pVM         The VM to operate on.
 *
 * @remarks The global VMM lock isn't really used for anything any longer.
 */
VMMR3DECL(int) VMMR3Lock(PVM pVM)
{
    return RTCritSectEnter(&pVM->vmm.s.CritSectVMLock);
}


/**
 * Release global VM lock.
 *
 * @returns VBox status code
 * @param   pVM         The VM to operate on.
 *
 * @remarks The global VMM lock isn't really used for anything any longer.
 */
VMMR3DECL(int) VMMR3Unlock(PVM pVM)
{
    return RTCritSectLeave(&pVM->vmm.s.CritSectVMLock);
}


/**
 * Return global VM lock owner.
 *
 * @returns Thread id of owner.
 * @returns NIL_RTTHREAD if no owner.
 * @param   pVM         The VM to operate on.
 *
 * @remarks The global VMM lock isn't really used for anything any longer.
 */
VMMR3DECL(RTNATIVETHREAD) VMMR3LockGetOwner(PVM pVM)
{
    return RTCritSectGetOwner(&pVM->vmm.s.CritSectVMLock);
}


/**
 * Checks if the current thread is the owner of the global VM lock.
 *
 * @returns true if owner.
 * @returns false if not owner.
 * @param   pVM         The VM to operate on.
 *
 * @remarks The global VMM lock isn't really used for anything any longer.
 */
VMMR3DECL(bool) VMMR3LockIsOwner(PVM pVM)
{
    return RTCritSectIsOwner(&pVM->vmm.s.CritSectVMLock);
}


/**
 * Executes guest code in the raw-mode context.
 *
 * @param   pVM         VM handle.
 */
VMMR3DECL(int) VMMR3RawRunGC(PVM pVM)
{
    Log2(("VMMR3RawRunGC: (cs:eip=%04x:%08x)\n", CPUMGetGuestCS(pVM), CPUMGetGuestEIP(pVM)));

    /*
     * Set the EIP and ESP.
     */
    CPUMSetHyperEIP(pVM, CPUMGetGuestEFlags(pVM) & X86_EFL_VM
                    ? pVM->vmm.s.pfnCPUMRCResumeGuestV86
                    : pVM->vmm.s.pfnCPUMRCResumeGuest);
    CPUMSetHyperESP(pVM, pVM->vmm.s.pbEMTStackBottomRC);

    /*
     * We hide log flushes (outer) and hypervisor interrupts (inner).
     */
    for (;;)
    {
        Assert(CPUMGetHyperCR3(pVM) && CPUMGetHyperCR3(pVM) == PGMGetHyperCR3(pVM));
        int rc;
        do
        {
#ifdef NO_SUPCALLR0VMM
            rc = VERR_GENERAL_FAILURE;
#else
            rc = SUPCallVMMR0Fast(pVM->pVMR0, VMMR0_DO_RAW_RUN, 0);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = pVM->vmm.s.iLastGZRc;
#endif
        } while (rc == VINF_EM_RAW_INTERRUPT_HYPER);

        /*
         * Flush the logs.
         */
#ifdef LOG_ENABLED
        PRTLOGGERRC pLogger = pVM->vmm.s.pRCLoggerR3;
        if (    pLogger
            &&  pLogger->offScratch > 0)
            RTLogFlushRC(NULL, pLogger);
#endif
#ifdef VBOX_WITH_RC_RELEASE_LOGGING
        PRTLOGGERRC pRelLogger = pVM->vmm.s.pRCRelLoggerR3;
        if (RT_UNLIKELY(pRelLogger && pRelLogger->offScratch > 0))
            RTLogFlushRC(RTLogRelDefaultInstance(), pRelLogger);
#endif
        if (rc != VINF_VMM_CALL_HOST)
        {
            Log2(("VMMR3RawRunGC: returns %Rrc (cs:eip=%04x:%08x)\n", rc, CPUMGetGuestCS(pVM), CPUMGetGuestEIP(pVM)));
            return rc;
        }
        rc = vmmR3ServiceCallHostRequest(pVM);
        if (RT_FAILURE(rc))
            return rc;
        /* Resume GC */
    }
}


/**
 * Executes guest code (Intel VT-x and AMD-V).
 *
 * @param   pVM         VM handle.
 * @param   idCpu       VMCPU id.
 */
VMMR3DECL(int) VMMR3HwAccRunGC(PVM pVM, RTCPUID idCpu)
{
    Log2(("VMMR3HwAccRunGC: (cs:eip=%04x:%08x)\n", CPUMGetGuestCS(pVM), CPUMGetGuestEIP(pVM)));

    for (;;)
    {
        int rc;
        do
        {
#ifdef NO_SUPCALLR0VMM
            rc = VERR_GENERAL_FAILURE;
#else
            rc = SUPCallVMMR0Fast(pVM->pVMR0, VMMR0_DO_HWACC_RUN, idCpu);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = pVM->vmm.s.iLastGZRc;
#endif
        } while (rc == VINF_EM_RAW_INTERRUPT_HYPER);

#ifdef LOG_ENABLED
        /*
         * Flush the log
         */
        PVMMR0LOGGER pR0LoggerR3 = pVM->vmm.s.pR0LoggerR3;
        if (    pR0LoggerR3
            &&  pR0LoggerR3->Logger.offScratch > 0)
            RTLogFlushToLogger(&pR0LoggerR3->Logger, NULL);
#endif /* !LOG_ENABLED */
        if (rc != VINF_VMM_CALL_HOST)
        {
            Log2(("VMMR3HwAccRunGC: returns %Rrc (cs:eip=%04x:%08x)\n", rc, CPUMGetGuestCS(pVM), CPUMGetGuestEIP(pVM)));
            return rc;
        }
        rc = vmmR3ServiceCallHostRequest(pVM);
        if (RT_FAILURE(rc))
            return rc;
        /* Resume R0 */
    }
}


/**
 * Calls a RC function.
 *
 * @param   pVM         The VM handle.
 * @param   RCPtrEntry  The address of the RC function.
 * @param   cArgs       The number of arguments in the ....
 * @param   ...         Arguments to the function.
 */
VMMR3DECL(int) VMMR3CallRC(PVM pVM, RTRCPTR RCPtrEntry, unsigned cArgs, ...)
{
    va_list args;
    va_start(args, cArgs);
    int rc = VMMR3CallRCV(pVM, RCPtrEntry, cArgs, args);
    va_end(args);
    return rc;
}


/**
 * Calls a RC function.
 *
 * @param   pVM         The VM handle.
 * @param   RCPtrEntry  The address of the RC function.
 * @param   cArgs       The number of arguments in the ....
 * @param   args        Arguments to the function.
 */
VMMR3DECL(int) VMMR3CallRCV(PVM pVM, RTRCPTR RCPtrEntry, unsigned cArgs, va_list args)
{
    Log2(("VMMR3CallGCV: RCPtrEntry=%RRv cArgs=%d\n", RCPtrEntry, cArgs));

    /*
     * Setup the call frame using the trampoline.
     */
    CPUMHyperSetCtxCore(pVM, NULL);
    memset(pVM->vmm.s.pbEMTStackR3, 0xaa, VMM_STACK_SIZE); /* Clear the stack. */
    CPUMSetHyperESP(pVM, pVM->vmm.s.pbEMTStackBottomRC - cArgs * sizeof(RTGCUINTPTR32));
    PRTGCUINTPTR32 pFrame = (PRTGCUINTPTR32)(pVM->vmm.s.pbEMTStackR3 + VMM_STACK_SIZE) - cArgs;
    int i = cArgs;
    while (i-- > 0)
        *pFrame++ = va_arg(args, RTGCUINTPTR32);

    CPUMPushHyper(pVM, cArgs * sizeof(RTGCUINTPTR32));                          /* stack frame size */
    CPUMPushHyper(pVM, RCPtrEntry);                                             /* what to call */
    CPUMSetHyperEIP(pVM, pVM->vmm.s.pfnCallTrampolineRC);

    /*
     * We hide log flushes (outer) and hypervisor interrupts (inner).
     */
    for (;;)
    {
        int rc;
        Assert(CPUMGetHyperCR3(pVM) && CPUMGetHyperCR3(pVM) == PGMGetHyperCR3(pVM));
        do
        {
#ifdef NO_SUPCALLR0VMM
            rc = VERR_GENERAL_FAILURE;
#else
            rc = SUPCallVMMR0Fast(pVM->pVMR0, VMMR0_DO_RAW_RUN, 0);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = pVM->vmm.s.iLastGZRc;
#endif
        } while (rc == VINF_EM_RAW_INTERRUPT_HYPER);

        /*
         * Flush the logs.
         */
#ifdef LOG_ENABLED
        PRTLOGGERRC pLogger = pVM->vmm.s.pRCLoggerR3;
        if (    pLogger
            &&  pLogger->offScratch > 0)
            RTLogFlushRC(NULL, pLogger);
#endif
#ifdef VBOX_WITH_RC_RELEASE_LOGGING
        PRTLOGGERRC pRelLogger = pVM->vmm.s.pRCRelLoggerR3;
        if (RT_UNLIKELY(pRelLogger && pRelLogger->offScratch > 0))
            RTLogFlushRC(RTLogRelDefaultInstance(), pRelLogger);
#endif
        if (rc == VERR_TRPM_PANIC || rc == VERR_TRPM_DONT_PANIC)
            VMMR3FatalDump(pVM, rc);
        if (rc != VINF_VMM_CALL_HOST)
        {
            Log2(("VMMR3CallGCV: returns %Rrc (cs:eip=%04x:%08x)\n", rc, CPUMGetGuestCS(pVM), CPUMGetGuestEIP(pVM)));
            return rc;
        }
        rc = vmmR3ServiceCallHostRequest(pVM);
        if (RT_FAILURE(rc))
            return rc;
    }
}


/**
 * Resumes executing hypervisor code when interrupted by a queue flush or a
 * debug event.
 *
 * @returns VBox status code.
 * @param   pVM         VM handle.
 */
VMMR3DECL(int) VMMR3ResumeHyper(PVM pVM)
{
    Log(("VMMR3ResumeHyper: eip=%RRv esp=%RRv\n", CPUMGetHyperEIP(pVM), CPUMGetHyperESP(pVM)));

    /*
     * We hide log flushes (outer) and hypervisor interrupts (inner).
     */
    for (;;)
    {
        int rc;
        Assert(CPUMGetHyperCR3(pVM) && CPUMGetHyperCR3(pVM) == PGMGetHyperCR3(pVM));
        do
        {
#ifdef NO_SUPCALLR0VMM
            rc = VERR_GENERAL_FAILURE;
#else
            rc = SUPCallVMMR0Fast(pVM->pVMR0, VMMR0_DO_RAW_RUN, 0);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = pVM->vmm.s.iLastGZRc;
#endif
        } while (rc == VINF_EM_RAW_INTERRUPT_HYPER);

        /*
         * Flush the loggers,
         */
#ifdef LOG_ENABLED
        PRTLOGGERRC pLogger = pVM->vmm.s.pRCLoggerR3;
        if (    pLogger
            &&  pLogger->offScratch > 0)
            RTLogFlushRC(NULL, pLogger);
#endif
#ifdef VBOX_WITH_RC_RELEASE_LOGGING
        PRTLOGGERRC pRelLogger = pVM->vmm.s.pRCRelLoggerR3;
        if (RT_UNLIKELY(pRelLogger && pRelLogger->offScratch > 0))
            RTLogFlushRC(RTLogRelDefaultInstance(), pRelLogger);
#endif
        if (rc == VERR_TRPM_PANIC || rc == VERR_TRPM_DONT_PANIC)
            VMMR3FatalDump(pVM, rc);
        if (rc != VINF_VMM_CALL_HOST)
        {
            Log(("VMMR3ResumeHyper: returns %Rrc\n", rc));
            return rc;
        }
        rc = vmmR3ServiceCallHostRequest(pVM);
        if (RT_FAILURE(rc))
            return rc;
    }
}


/**
 * Service a call to the ring-3 host code.
 *
 * @returns VBox status code.
 * @param   pVM     VM handle.
 * @remark  Careful with critsects.
 */
static int vmmR3ServiceCallHostRequest(PVM pVM)
{
    switch (pVM->vmm.s.enmCallHostOperation)
    {
        /*
         * Acquire the PDM lock.
         */
        case VMMCALLHOST_PDM_LOCK:
        {
            pVM->vmm.s.rcCallHost = PDMR3LockCall(pVM);
            break;
        }

        /*
         * Flush a PDM queue.
         */
        case VMMCALLHOST_PDM_QUEUE_FLUSH:
        {
            PDMR3QueueFlushWorker(pVM, NULL);
            pVM->vmm.s.rcCallHost = VINF_SUCCESS;
            break;
        }

        /*
         * Grow the PGM pool.
         */
        case VMMCALLHOST_PGM_POOL_GROW:
        {
            pVM->vmm.s.rcCallHost = PGMR3PoolGrow(pVM);
            break;
        }

        /*
         * Maps an page allocation chunk into ring-3 so ring-0 can use it.
         */
        case VMMCALLHOST_PGM_MAP_CHUNK:
        {
            pVM->vmm.s.rcCallHost = PGMR3PhysChunkMap(pVM, pVM->vmm.s.u64CallHostArg);
            break;
        }

        /*
         * Allocates more handy pages.
         */
        case VMMCALLHOST_PGM_ALLOCATE_HANDY_PAGES:
        {
            pVM->vmm.s.rcCallHost = PGMR3PhysAllocateHandyPages(pVM);
            break;
        }
#ifndef VBOX_WITH_NEW_PHYS_CODE

        case VMMCALLHOST_PGM_RAM_GROW_RANGE:
        {
            const RTGCPHYS GCPhys = pVM->vmm.s.u64CallHostArg;
            pVM->vmm.s.rcCallHost = PGM3PhysGrowRange(pVM, &GCPhys);
            break;
        }
#endif

        /*
         * Acquire the PGM lock.
         */
        case VMMCALLHOST_PGM_LOCK:
        {
            pVM->vmm.s.rcCallHost = PGMR3LockCall(pVM);
            break;
        }

        /*
         * Flush REM handler notifications.
         */
        case VMMCALLHOST_REM_REPLAY_HANDLER_NOTIFICATIONS:
        {
            REMR3ReplayHandlerNotifications(pVM);
            pVM->vmm.s.rcCallHost = VINF_SUCCESS;
            break;
        }

        /*
         * This is a noop. We just take this route to avoid unnecessary
         * tests in the loops.
         */
        case VMMCALLHOST_VMM_LOGGER_FLUSH:
            pVM->vmm.s.rcCallHost = VINF_SUCCESS;
            LogAlways(("*FLUSH*\n"));
            break;

        /*
         * Set the VM error message.
         */
        case VMMCALLHOST_VM_SET_ERROR:
            VMR3SetErrorWorker(pVM);
            pVM->vmm.s.rcCallHost = VINF_SUCCESS;
            break;

        /*
         * Set the VM runtime error message.
         */
        case VMMCALLHOST_VM_SET_RUNTIME_ERROR:
            VMR3SetRuntimeErrorWorker(pVM);
            pVM->vmm.s.rcCallHost = VINF_SUCCESS;
            break;

        /*
         * Signal a ring 0 hypervisor assertion.
         * Cancel the longjmp operation that's in progress.
         */
        case VMMCALLHOST_VM_R0_ASSERTION:
            pVM->vmm.s.enmCallHostOperation = VMMCALLHOST_INVALID;
            pVM->vmm.s.CallHostR0JmpBuf.fInRing3Call = false;
#ifdef RT_ARCH_X86
            pVM->vmm.s.CallHostR0JmpBuf.eip = 0;
#else
            pVM->vmm.s.CallHostR0JmpBuf.rip = 0;
#endif
            LogRel((pVM->vmm.s.szRing0AssertMsg1));
            LogRel((pVM->vmm.s.szRing0AssertMsg2));
            return VERR_VMM_RING0_ASSERTION;

        /* 
         * A forced switch to ring 0 for preemption purposes. 
         */
        case VMMCALLHOST_VM_R0_PREEMPT:
            pVM->vmm.s.rcCallHost = VINF_SUCCESS;
            break;

        default:
            AssertMsgFailed(("enmCallHostOperation=%d\n", pVM->vmm.s.enmCallHostOperation));
            return VERR_INTERNAL_ERROR;
    }

    pVM->vmm.s.enmCallHostOperation = VMMCALLHOST_INVALID;
    return VINF_SUCCESS;
}


/**
 * Displays the Force action Flags.
 *
 * @param   pVM         The VM handle.
 * @param   pHlp        The output helpers.
 * @param   pszArgs     The additional arguments (ignored).
 */
static DECLCALLBACK(void) vmmR3InfoFF(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    const uint32_t fForcedActions = pVM->fForcedActions;

    pHlp->pfnPrintf(pHlp, "Forced action Flags: %#RX32", fForcedActions);

    /* show the flag mnemonics  */
    int c = 0;
    uint32_t f = fForcedActions;
#define PRINT_FLAG(flag) do { \
        if (f & (flag)) \
        { \
            static const char *s_psz = #flag; \
            if (!(c % 6)) \
                pHlp->pfnPrintf(pHlp, "%s\n    %s", c ? "," : "", s_psz + 6); \
            else \
                pHlp->pfnPrintf(pHlp, ", %s", s_psz + 6); \
            c++; \
            f &= ~(flag); \
        } \
    } while (0)
    PRINT_FLAG(VM_FF_INTERRUPT_APIC);
    PRINT_FLAG(VM_FF_INTERRUPT_PIC);
    PRINT_FLAG(VM_FF_TIMER);
    PRINT_FLAG(VM_FF_PDM_QUEUES);
    PRINT_FLAG(VM_FF_PDM_DMA);
    PRINT_FLAG(VM_FF_PDM_CRITSECT);
    PRINT_FLAG(VM_FF_DBGF);
    PRINT_FLAG(VM_FF_REQUEST);
    PRINT_FLAG(VM_FF_TERMINATE);
    PRINT_FLAG(VM_FF_RESET);
    PRINT_FLAG(VM_FF_PGM_SYNC_CR3);
    PRINT_FLAG(VM_FF_PGM_SYNC_CR3_NON_GLOBAL);
    PRINT_FLAG(VM_FF_TRPM_SYNC_IDT);
    PRINT_FLAG(VM_FF_SELM_SYNC_TSS);
    PRINT_FLAG(VM_FF_SELM_SYNC_GDT);
    PRINT_FLAG(VM_FF_SELM_SYNC_LDT);
    PRINT_FLAG(VM_FF_INHIBIT_INTERRUPTS);
    PRINT_FLAG(VM_FF_CSAM_SCAN_PAGE);
    PRINT_FLAG(VM_FF_CSAM_PENDING_ACTION);
    PRINT_FLAG(VM_FF_TO_R3);
    PRINT_FLAG(VM_FF_DEBUG_SUSPEND);
    if (f)
        pHlp->pfnPrintf(pHlp, "%s\n    Unknown bits: %#RX32\n", c ? "," : "", f);
    else
        pHlp->pfnPrintf(pHlp, "\n");
#undef PRINT_FLAG

    /* the groups */
    c = 0;
#define PRINT_GROUP(grp) do { \
        if (fForcedActions & (grp)) \
        { \
            static const char *s_psz = #grp; \
            if (!(c % 5)) \
                pHlp->pfnPrintf(pHlp, "%s    %s", c ? ",\n" : "Groups:\n", s_psz + 6); \
            else \
                pHlp->pfnPrintf(pHlp, ", %s", s_psz + 6); \
            c++; \
        } \
    } while (0)
    PRINT_GROUP(VM_FF_EXTERNAL_SUSPENDED_MASK);
    PRINT_GROUP(VM_FF_EXTERNAL_HALTED_MASK);
    PRINT_GROUP(VM_FF_HIGH_PRIORITY_PRE_MASK);
    PRINT_GROUP(VM_FF_HIGH_PRIORITY_PRE_RAW_MASK);
    PRINT_GROUP(VM_FF_HIGH_PRIORITY_POST_MASK);
    PRINT_GROUP(VM_FF_NORMAL_PRIORITY_POST_MASK);
    PRINT_GROUP(VM_FF_NORMAL_PRIORITY_MASK);
    PRINT_GROUP(VM_FF_RESUME_GUEST_MASK);
    PRINT_GROUP(VM_FF_ALL_BUT_RAW_MASK);
    if (c)
        pHlp->pfnPrintf(pHlp, "\n");
#undef PRINT_GROUP
}

