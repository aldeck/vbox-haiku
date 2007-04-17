/* $Id$ */
/** @file
 * HWACCM VMX - Host Context Ring 0.
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
#define LOG_GROUP LOG_GROUP_HWACCM
#include <VBox/hwaccm.h>
#include "HWACCMInternal.h"
#include <VBox/vm.h>
#include <VBox/x86.h>
#include <VBox/pgm.h>
#include <VBox/pdm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/selm.h>
#include <VBox/iom.h>
#include <iprt/param.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include "HWVMXR0.h"


/* IO operation lookup arrays. */
static uint32_t aIOSize[4]  = {1, 2, 0, 4};
static uint32_t aIOOpAnd[4] = {0xff, 0xffff, 0, 0xffffffff};


/**
 * Sets up and activates VMX
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
HWACCMR0DECL(int) VMXR0Setup(PVM pVM)
{
    int rc = VINF_SUCCESS;
    uint32_t val;

    if (pVM == NULL)
        return VERR_INVALID_PARAMETER;

    /* Setup Intel VMX. */
    Assert(pVM->hwaccm.s.vmx.fSupported);

    /* Set revision dword at the beginning of both structures. */
    *(uint32_t *)pVM->hwaccm.s.vmx.pVMCS  = MSR_IA32_VMX_BASIC_INFO_VMCS_ID(pVM->hwaccm.s.vmx.msr.vmx_basic_info);
    *(uint32_t *)pVM->hwaccm.s.vmx.pVMXON = MSR_IA32_VMX_BASIC_INFO_VMCS_ID(pVM->hwaccm.s.vmx.msr.vmx_basic_info);

    /* @todo we should unmap the two pages from the virtual address space in order to prevent accidental corruption.
     * (which can have very bad consequences!!!)
     */

    /* Make sure the VMX instructions don't cause #UD faults. */
    ASMSetCR4(ASMGetCR4() | X86_CR4_VMXE);

    /* Enter VMX Root Mode */
    rc = VMXEnable(pVM->hwaccm.s.vmx.pVMXONPhys);
    if (VBOX_FAILURE(rc))
    {
        return rc;
    }

    /* Clear VM Control Structure. */
    rc = VMXClearVMCS(pVM->hwaccm.s.vmx.pVMCSPhys);
    if (VBOX_FAILURE(rc))
        goto vmx_end;

    /* Activate the VM Control Structure. */
    rc = VMXActivateVMCS(pVM->hwaccm.s.vmx.pVMCSPhys);
    if (VBOX_FAILURE(rc))
        goto vmx_end;

    /* VMX_VMCS_CTRL_PIN_EXEC_CONTROLS
     * Set required bits to one and zero according to the MSR capabilities.
     */
    val  = (pVM->hwaccm.s.vmx.msr.vmx_pin_ctls & 0xFFFFFFFF);
    /* External and non-maskable interrupts cause VM-exits. */
    val  = val | VMX_VMCS_CTRL_PIN_EXEC_CONTROLS_EXT_INT_EXIT | VMX_VMCS_CTRL_PIN_EXEC_CONTROLS_NMI_EXIT;
    val &= (pVM->hwaccm.s.vmx.msr.vmx_pin_ctls >> 32ULL);

    rc = VMXWriteVMCS(VMX_VMCS_CTRL_PIN_EXEC_CONTROLS, val);
    AssertRC(rc);

    /* VMX_VMCS_CTRL_PROC_EXEC_CONTROLS
     * Set required bits to one and zero according to the MSR capabilities.
     */
    val = (pVM->hwaccm.s.vmx.msr.vmx_proc_ctls & 0xFFFFFFFF);
    /* Program which event cause VM-exits and which features we want to use. */
    val = val | VMX_VMCS_CTRL_PROC_EXEC_CONTROLS_HLT_EXIT
              | VMX_VMCS_CTRL_PROC_EXEC_CONTROLS_TSC_OFFSET
              | VMX_VMCS_CTRL_PROC_EXEC_CONTROLS_INVLPG_EXIT
              | VMX_VMCS_CTRL_PROC_EXEC_CONTROLS_MOV_DR_EXIT
              | VMX_VMCS_CTRL_PROC_EXEC_CONTROLS_UNCOND_IO_EXIT
              | VMX_VMCS_CTRL_PROC_EXEC_CONTROLS_MWAIT_EXIT;    /* don't execute mwait or else we'll idle inside the guest (host thinks the cpu load is high) */

    /** @note VMX_VMCS_CTRL_PROC_EXEC_CONTROLS_MWAIT_EXIT might cause a vmlaunch failure with an invalid control fields error. (combined with some other exit reasons) */

    /*
     if AMD64 guest mode
         val |=   VMX_VMCS_CTRL_PROC_EXEC_CONTROLS_CR8_LOAD_EXIT
                | VMX_VMCS_CTRL_PROC_EXEC_CONTROLS_CR8_STORE_EXIT;
     */
    /* Mask away the bits that the CPU doesn't support */
    /** @todo make sure they don't conflict with the above requirements. */
    val &= (pVM->hwaccm.s.vmx.msr.vmx_proc_ctls >> 32ULL);
    pVM->hwaccm.s.vmx.proc_ctls = val;

    rc = VMXWriteVMCS(VMX_VMCS_CTRL_PROC_EXEC_CONTROLS, val);
    AssertRC(rc);

    /* VMX_VMCS_CTRL_CR3_TARGET_COUNT
     * Set required bits to one and zero according to the MSR capabilities.
     */
    rc = VMXWriteVMCS(VMX_VMCS_CTRL_CR3_TARGET_COUNT, 0);
    AssertRC(rc);

    /* VMX_VMCS_CTRL_ENTRY_CONTROLS
     * Set required bits to one and zero according to the MSR capabilities.
     */
    val  = (pVM->hwaccm.s.vmx.msr.vmx_entry & 0xFFFFFFFF);
    if (pVM->hwaccm.s.cpuid.u32AMDFeatureEDX & X86_CPUID_AMD_FEATURE_EDX_LONG_MODE)
    {
        /** @todo 32 bits guest mode only for now. */
        /* val |= VMX_VMCS_CTRL_ENTRY_CONTROLS_IA64_MODE; */
    }
    /* Mask away the bits that the CPU doesn't support */
    /** @todo make sure they don't conflict with the above requirements. */
    val &= (pVM->hwaccm.s.vmx.msr.vmx_entry >> 32ULL);
    /* else Must be zero when AMD64 is not available. */
    rc = VMXWriteVMCS(VMX_VMCS_CTRL_ENTRY_CONTROLS, val);
    AssertRC(rc);

    /* VMX_VMCS_CTRL_EXIT_CONTROLS
     * Set required bits to one and zero according to the MSR capabilities.
     */
    val  = (pVM->hwaccm.s.vmx.msr.vmx_exit & 0xFFFFFFFF);
#if HC_ARCH_BITS == 64
    val |= VMX_VMCS_CTRL_EXIT_CONTROLS_HOST_AMD64;
#else
    /* else Must be zero when AMD64 is not available. */
#endif
    val &= (pVM->hwaccm.s.vmx.msr.vmx_exit >> 32ULL);
    /* Don't acknowledge external interrupts on VM-exit. */
    rc = VMXWriteVMCS(VMX_VMCS_CTRL_EXIT_CONTROLS, val);
    AssertRC(rc);

    /* Forward all exception except #NM & #PF to the guest.
     * We always need to check pagefaults since our shadow page table can be out of sync.
     * And we always lazily sync the FPU & XMM state.
     */

    /*
     * @todo Possible optimization:
     * Keep the FPU and XMM state current in the EM thread. That way there's no need to
     * lazily sync anything, but the downside is that we can't use the FPU stack or XMM
     * registers ourselves of course.
     *
     * @note only possible if the current state is actually ours (X86_CR0_TS flag)
     */
    rc = VMXWriteVMCS(VMX_VMCS_CTRL_EXCEPTION_BITMAP, HWACCM_VMX_TRAP_MASK);
    AssertRC(rc);

    /* Don't filter page faults; all of them should cause a switch. */
    rc  = VMXWriteVMCS(VMX_VMCS_CTRL_PAGEFAULT_ERROR_MASK, 0);
    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_PAGEFAULT_ERROR_MATCH, 0);
    AssertRC(rc);

    /* Init TSC offset to zero. */
    rc  = VMXWriteVMCS(VMX_VMCS_CTRL_TSC_OFFSET_FULL, 0);
#if HC_ARCH_BITS == 32
    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_TSC_OFFSET_HIGH, 0);
#endif
    AssertRC(rc);

    rc  = VMXWriteVMCS(VMX_VMCS_CTRL_IO_BITMAP_A_FULL, 0);
    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_IO_BITMAP_A_HIGH, 0);
    AssertRC(rc);

    rc  = VMXWriteVMCS(VMX_VMCS_CTRL_IO_BITMAP_B_FULL, 0);
    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_IO_BITMAP_B_HIGH, 0);
    AssertRC(rc);

    /* Clear MSR controls. */
    if (pVM->hwaccm.s.vmx.msr.vmx_proc_ctls & VMX_VMCS_CTRL_PROC_EXEC_CONTROLS_USE_MSR_BITMAPS)
    {
        /* Optional */
        rc  = VMXWriteVMCS(VMX_VMCS_CTRL_MSR_BITMAP_FULL, 0);
        rc |= VMXWriteVMCS(VMX_VMCS_CTRL_MSR_BITMAP_HIGH, 0);
        AssertRC(rc);
    }
    rc  = VMXWriteVMCS(VMX_VMCS_CTRL_VMEXIT_MSR_STORE_FULL, 0);
    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_VMEXIT_MSR_STORE_HIGH, 0);
    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_VMEXIT_MSR_LOAD_FULL, 0);
    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_VMEXIT_MSR_LOAD_HIGH, 0);
    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_VMENTRY_MSR_LOAD_FULL, 0);
    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_VMENTRY_MSR_LOAD_HIGH, 0);
    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_EXIT_MSR_STORE_COUNT, 0);
    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_EXIT_MSR_LOAD_COUNT, 0);
    AssertRC(rc);

    if (pVM->hwaccm.s.vmx.msr.vmx_proc_ctls & VMX_VMCS_CTRL_PROC_EXEC_CONTROLS_USE_TPR_SHADOW)
    {
        /* Optional */
        rc  = VMXWriteVMCS(VMX_VMCS_CTRL_TPR_TRESHOLD, 0);
        rc |= VMXWriteVMCS(VMX_VMCS_CTRL_VAPIC_PAGEADDR_FULL, 0);
        rc |= VMXWriteVMCS(VMX_VMCS_CTRL_VAPIC_PAGEADDR_HIGH, 0);
        AssertRC(rc);
    }

    /* Set link pointer to -1. Not currently used. */
    rc  = VMXWriteVMCS(VMX_VMCS_GUEST_LINK_PTR_FULL, 0xFFFFFFFF);
    rc |= VMXWriteVMCS(VMX_VMCS_GUEST_LINK_PTR_HIGH, 0xFFFFFFFF);
    AssertRC(rc);

    /* Clear VM Control Structure. Marking it inactive, clearing implementation specific data and writing back VMCS data to memory. */
    rc = VMXClearVMCS(pVM->hwaccm.s.vmx.pVMCSPhys);
    AssertRC(rc);

vmx_end:
    /* Leave VMX Root Mode. */
    VMXDisable();
    return rc;
}


/**
 * Injects an event (trap or external interrupt)
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 * @param   pCtx        CPU Context
 * @param   intInfo     VMX interrupt info
 * @param   cbInstr     Opcode length of faulting instruction
 * @param   errCode     Error code (optional)
 */
static int VMXR0InjectEvent(PVM pVM, CPUMCTX *pCtx, uint32_t intInfo, uint32_t cbInstr, uint32_t errCode)
{
    int         rc;

#ifdef VBOX_STRICT
    uint32_t    iGate = VMX_EXIT_INTERRUPTION_INFO_VECTOR(intInfo);
    if (iGate == 0xE)
        Log2(("VMXR0InjectEvent: Injecting interrupt %d at %VGv error code=%08x CR2=%08x intInfo=%08x\n", iGate, pCtx->eip, errCode, pCtx->cr2, intInfo));
    else
    if (iGate < 0x20)
        Log2(("VMXR0InjectEvent: Injecting interrupt %d at %VGv error code=%08x\n", iGate, pCtx->eip, errCode));
    else
    {
        Log2(("INJ-EI: %x at %VGv\n", iGate, pCtx->eip));
        Assert(!VM_FF_ISSET(pVM, VM_FF_INHIBIT_INTERRUPTS));
        Assert(pCtx->eflags.u32 & X86_EFL_IF);
    }
#endif

    /* Set event injection state. */
    rc  = VMXWriteVMCS(VMX_VMCS_CTRL_ENTRY_IRQ_INFO,
                       intInfo | (1 << VMX_EXIT_INTERRUPTION_INFO_VALID_SHIFT)
                      );

    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_ENTRY_INSTR_LENGTH, cbInstr);
    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_ENTRY_EXCEPTION_ERRCODE, errCode);

    AssertRC(rc);
    return rc;
}


/**
 * Checks for pending guest interrupts and injects them
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 * @param   pCtx        CPU Context
 */
static int VMXR0CheckPendingInterrupt(PVM pVM, CPUMCTX *pCtx)
{
    int rc;

    /* Dispatch any pending interrupts. (injected before, but a VM exit occurred prematurely) */
    if (pVM->hwaccm.s.Event.fPending)
    {
        Log(("Reinjecting event %VX64 %08x at %VGv\n", pVM->hwaccm.s.Event.intInfo, pVM->hwaccm.s.Event.errCode, pCtx->eip));
        STAM_COUNTER_INC(&pVM->hwaccm.s.StatIntReinject);
        rc = VMXR0InjectEvent(pVM, pCtx, pVM->hwaccm.s.Event.intInfo, 0, pVM->hwaccm.s.Event.errCode);
        AssertRC(rc);

        pVM->hwaccm.s.Event.fPending = false;
        return VINF_SUCCESS;
    }

    /* When external interrupts are pending, we should exit the VM when IF is set. */
    if (    !TRPMHasTrap(pVM)
        &&  VM_FF_ISPENDING(pVM, (VM_FF_INTERRUPT_APIC|VM_FF_INTERRUPT_PIC)))
    {
        if (!(pCtx->eflags.u32 & X86_EFL_IF))
        {
            Log2(("Enable irq window exit!\n"));
            rc = VMXWriteVMCS(VMX_VMCS_CTRL_PROC_EXEC_CONTROLS, pVM->hwaccm.s.vmx.proc_ctls | VMX_VMCS_CTRL_PROC_EXEC_CONTROLS_IRQ_WINDOW_EXIT);
            AssertRC(rc);
        }
        else
        if (!VM_FF_ISSET(pVM, VM_FF_INHIBIT_INTERRUPTS))
        {
            uint8_t u8Interrupt;

            rc = PDMGetInterrupt(pVM, &u8Interrupt);
            Log(("Dispatch interrupt: u8Interrupt=%x (%d) rc=%Vrc\n", u8Interrupt, u8Interrupt, rc));
            if (VBOX_SUCCESS(rc))
            {
                rc = TRPMAssertTrap(pVM, u8Interrupt, TRPM_HARDWARE_INT);
                AssertRC(rc);
            }
            else
            {
                /* can't happen... */
                AssertFailed();
                STAM_COUNTER_INC(&pVM->hwaccm.s.StatSwitchGuestIrq);
                return VINF_EM_RAW_INTERRUPT_PENDING;
            }
        }
        else
            Log(("Pending interrupt blocked at %VGv by VM_FF_INHIBIT_INTERRUPTS!!\n", pCtx->eip));
    }

#ifdef VBOX_STRICT
    if (TRPMHasTrap(pVM))
    {
        uint8_t     u8Vector;
        rc = TRPMQueryTrapAll(pVM, &u8Vector, 0, 0, 0);
        AssertRC(rc);
    }
#endif

    if (    pCtx->eflags.u32 & X86_EFL_IF
        && (!VM_FF_ISSET(pVM, VM_FF_INHIBIT_INTERRUPTS))
        && TRPMHasTrap(pVM)
       )
    {
        uint8_t     u8Vector;
        int         rc;
        TRPMEVENT   enmType;
        RTGCUINTPTR intInfo, errCode;

        /* If a new event is pending, then dispatch it now. */
        rc = TRPMQueryTrapAll(pVM, &u8Vector, &enmType, &errCode, 0);
        AssertRC(rc);
        Assert(pCtx->eflags.Bits.u1IF == 1 || enmType == TRPM_TRAP);
        Assert(enmType != TRPM_SOFTWARE_INT);

        /* Clear the pending trap. */
        rc = TRPMResetTrap(pVM);
        AssertRC(rc);

        intInfo  = u8Vector;
        intInfo |= (1 << VMX_EXIT_INTERRUPTION_INFO_VALID_SHIFT);

        if (enmType == TRPM_TRAP)
        {
            switch (u8Vector) {
            case 8:
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
            case 17:
                /* Valid error codes. */
                intInfo |= VMX_EXIT_INTERRUPTION_INFO_ERROR_CODE_VALID;
                break;
            default:
                break;
            }
            if (u8Vector == X86_XCPT_BP || u8Vector == X86_XCPT_OF)
                intInfo |= (VMX_EXIT_INTERRUPTION_INFO_TYPE_SWEXCPT << VMX_EXIT_INTERRUPTION_INFO_TYPE_SHIFT);
            else
                intInfo |= (VMX_EXIT_INTERRUPTION_INFO_TYPE_HWEXCPT << VMX_EXIT_INTERRUPTION_INFO_TYPE_SHIFT);
        }
        else
            intInfo |= (VMX_EXIT_INTERRUPTION_INFO_TYPE_EXT << VMX_EXIT_INTERRUPTION_INFO_TYPE_SHIFT);

        STAM_COUNTER_INC(&pVM->hwaccm.s.StatIntInject);
        rc = VMXR0InjectEvent(pVM, pCtx, intInfo, 0, errCode);
        AssertRC(rc);
    } /* if (interrupts can be dispatched) */

    return VINF_SUCCESS;
}

/**
 * Save the host state
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
HWACCMR0DECL(int) VMXR0SaveHostState(PVM pVM)
{
    int rc = VINF_SUCCESS;

    /*
     * Host CPU Context
     */
    if (pVM->hwaccm.s.fContextUseFlags & HWACCM_CHANGED_HOST_CONTEXT)
    {
        RTIDTR    idtr;
        RTGDTR    gdtr;
        RTSEL     SelTR;
        PVBOXDESC pDesc;
        uintptr_t trBase;

        /* Control registers */
        rc  = VMXWriteVMCS(VMX_VMCS_HOST_CR0,               ASMGetCR0());
        rc |= VMXWriteVMCS(VMX_VMCS_HOST_CR3,               ASMGetCR3());
        rc |= VMXWriteVMCS(VMX_VMCS_HOST_CR4,               ASMGetCR4());
        AssertRC(rc);
        Log2(("VMX_VMCS_HOST_CR0 %08x\n", ASMGetCR0()));
        Log2(("VMX_VMCS_HOST_CR3 %08x\n", ASMGetCR3()));
        Log2(("VMX_VMCS_HOST_CR4 %08x\n", ASMGetCR4()));

        /* Selector registers. */
        rc  = VMXWriteVMCS(VMX_VMCS_HOST_FIELD_CS,          ASMGetCS());
        /** @note VMX is (again) very picky about the RPL of the selectors here; we'll restore them manually. */
        rc |= VMXWriteVMCS(VMX_VMCS_HOST_FIELD_DS,          0);
        rc |= VMXWriteVMCS(VMX_VMCS_HOST_FIELD_ES,          0);
        rc |= VMXWriteVMCS(VMX_VMCS_HOST_FIELD_FS,          0);
        rc |= VMXWriteVMCS(VMX_VMCS_HOST_FIELD_GS,          0);
        rc |= VMXWriteVMCS(VMX_VMCS_HOST_FIELD_SS,          ASMGetSS());
        SelTR = ASMGetTR();
        rc |= VMXWriteVMCS(VMX_VMCS_HOST_FIELD_TR,          SelTR);
        AssertRC(rc);
        Log2(("VMX_VMCS_HOST_FIELD_CS %08x\n", ASMGetCS()));
        Log2(("VMX_VMCS_HOST_FIELD_DS %08x\n", ASMGetDS()));
        Log2(("VMX_VMCS_HOST_FIELD_ES %08x\n", ASMGetES()));
        Log2(("VMX_VMCS_HOST_FIELD_FS %08x\n", ASMGetFS()));
        Log2(("VMX_VMCS_HOST_FIELD_GS %08x\n", ASMGetGS()));
        Log2(("VMX_VMCS_HOST_FIELD_SS %08x\n", ASMGetSS()));
        Log2(("VMX_VMCS_HOST_FIELD_TR %08x\n", ASMGetTR()));

        /* GDTR & IDTR */
        ASMGetGDTR(&gdtr);
        rc  = VMXWriteVMCS(VMX_VMCS_HOST_GDTR_BASE, gdtr.pGdt);
        ASMGetIDTR(&idtr);
        rc |= VMXWriteVMCS(VMX_VMCS_HOST_IDTR_BASE, idtr.pIdt);
        AssertRC(rc);
        Log2(("VMX_VMCS_HOST_GDTR_BASE %VGv\n", gdtr.pGdt));
        Log2(("VMX_VMCS_HOST_IDTR_BASE %VGv\n", idtr.pIdt));

        /* Save the base address of the TR selector. */
        if (SelTR > gdtr.cbGdt)
        {
            AssertMsgFailed(("Invalid TR selector %x. GDTR.cbGdt=%x\n", SelTR, gdtr.cbGdt));
            return VERR_VMX_INVALID_HOST_STATE;
        }

        pDesc  = &((PVBOXDESC)gdtr.pGdt)[SelTR >> X86_SEL_SHIFT];
        trBase = pDesc->Gen.u16BaseLow | (pDesc->Gen.u8BaseHigh1 << 16) | (pDesc->Gen.u8BaseHigh2 << 24);
        rc = VMXWriteVMCS(VMX_VMCS_HOST_TR_BASE, trBase);
        AssertRC(rc);
        Log2(("VMX_VMCS_HOST_TR_BASE %VGv\n", trBase));

        /* FS and GS base. */
#if HC_ARCH_BITS == 32
        rc  = VMXWriteVMCS(VMX_VMCS_HOST_FS_BASE,           0);
        rc |= VMXWriteVMCS(VMX_VMCS_HOST_GS_BASE,           0);
#else
        rc  = VMXWriteVMCS64(VMX_VMCS_HOST_FS_BASE,         ASMRdMsr(MSR_K8_FS_BASE));
        rc |= VMXWriteVMCS64(VMX_VMCS_HOST_GS_BASE,         ASMRdMsr(MSR_K8_GS_BASE));
#endif
        AssertRC(rc);

        /* Sysenter MSRs. */
        /** @todo expensive!! */
        rc  = VMXWriteVMCS(VMX_VMCS_HOST_SYSENTER_CS,       ASMRdMsr_Low(MSR_IA32_SYSENTER_CS));
#if HC_ARCH_BITS == 32
        rc |= VMXWriteVMCS(VMX_VMCS_HOST_SYSENTER_ESP,      ASMRdMsr_Low(MSR_IA32_SYSENTER_ESP));
        rc |= VMXWriteVMCS(VMX_VMCS_HOST_SYSENTER_EIP,      ASMRdMsr_Low(MSR_IA32_SYSENTER_EIP));
        Log2(("VMX_VMCS_HOST_SYSENTER_CS  %08x\n", ASMRdMsr_Low(MSR_IA32_SYSENTER_CS)));
        Log2(("VMX_VMCS_HOST_SYSENTER_EIP %VGv\n", ASMRdMsr_Low(MSR_IA32_SYSENTER_EIP)));
        Log2(("VMX_VMCS_HOST_SYSENTER_ESP %VGv\n", ASMRdMsr_Low(MSR_IA32_SYSENTER_ESP)));
#else
        rc |= VMXWriteVMCS64(VMX_VMCS_HOST_SYSENTER_ESP,      ASMRdMsr(MSR_IA32_SYSENTER_ESP));
        rc |= VMXWriteVMCS64(VMX_VMCS_HOST_SYSENTER_EIP,      ASMRdMsr(MSR_IA32_SYSENTER_EIP));
#endif
        AssertRC(rc);

        pVM->hwaccm.s.fContextUseFlags &= ~HWACCM_CHANGED_HOST_CONTEXT;
    }
    return rc;
}


/**
 * Loads the guest state
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 * @param   pCtx        Guest context
 */
HWACCMR0DECL(int) VMXR0LoadGuestState(PVM pVM, CPUMCTX *pCtx)
{
    int         rc = VINF_SUCCESS;
    RTGCUINTPTR val;

    /* Guest CPU context: ES, CS, SS, DS, FS, GS. */
    if (pVM->hwaccm.s.fContextUseFlags & HWACCM_CHANGED_GUEST_SEGMENT_REGS)
    {
        VMX_WRITE_SELREG(ES, es);
        AssertRC(rc);

        VMX_WRITE_SELREG(CS, cs);
        AssertRC(rc);

        VMX_WRITE_SELREG(SS, ss);
        AssertRC(rc);

        VMX_WRITE_SELREG(DS, ds);
        AssertRC(rc);

        VMX_WRITE_SELREG(FS, fs);
        AssertRC(rc);

        VMX_WRITE_SELREG(GS, gs);
        AssertRC(rc);
    }

    /* Guest CPU context: LDTR. */
    if (pVM->hwaccm.s.fContextUseFlags & HWACCM_CHANGED_GUEST_LDTR)
    {
        if (pCtx->ldtr == 0)
        {
            rc =  VMXWriteVMCS(VMX_VMCS_GUEST_FIELD_LDTR,         0);
            rc |= VMXWriteVMCS(VMX_VMCS_GUEST_LDTR_LIMIT,         0);
            rc |= VMXWriteVMCS(VMX_VMCS_GUEST_LDTR_BASE,          0);
            /** @note vmlaunch will fail with 0 or just 0x02. No idea why. */
            rc |= VMXWriteVMCS(VMX_VMCS_GUEST_LDTR_ACCESS_RIGHTS, 0x82 /* present, LDT */);
        }
        else
        {
            rc =  VMXWriteVMCS(VMX_VMCS_GUEST_FIELD_LDTR,         pCtx->ldtr);
            rc |= VMXWriteVMCS(VMX_VMCS_GUEST_LDTR_LIMIT,         pCtx->ldtrHid.u32Limit);
            rc |= VMXWriteVMCS(VMX_VMCS_GUEST_LDTR_BASE,          pCtx->ldtrHid.u32Base);
            rc |= VMXWriteVMCS(VMX_VMCS_GUEST_LDTR_ACCESS_RIGHTS, pCtx->ldtrHid.Attr.u);
        }
        AssertRC(rc);
    }
    /* Guest CPU context: TR. */
    if (pVM->hwaccm.s.fContextUseFlags & HWACCM_CHANGED_GUEST_TR)
    {
        rc =  VMXWriteVMCS(VMX_VMCS_GUEST_FIELD_TR,         pCtx->tr);
        rc |= VMXWriteVMCS(VMX_VMCS_GUEST_TR_LIMIT,         pCtx->trHid.u32Limit);
        rc |= VMXWriteVMCS(VMX_VMCS_GUEST_TR_BASE,          pCtx->trHid.u32Base);
        val = pCtx->trHid.Attr.u;

        /* The TSS selector must be busy. */
        if ((val & 0xF) == X86_SEL_TYPE_SYS_386_TSS_AVAIL)
            val = (val & ~0xF) | X86_SEL_TYPE_SYS_386_TSS_BUSY;
        else
        if ((val & 0xF) == X86_SEL_TYPE_SYS_286_TSS_AVAIL)
            val = (val & ~0xF) | X86_SEL_TYPE_SYS_286_TSS_BUSY;

        rc |= VMXWriteVMCS(VMX_VMCS_GUEST_TR_ACCESS_RIGHTS, val);
        AssertRC(rc);
    }
    /* Guest CPU context: GDTR. */
    if (pVM->hwaccm.s.fContextUseFlags & HWACCM_CHANGED_GUEST_GDTR)
    {
        rc  = VMXWriteVMCS(VMX_VMCS_GUEST_GDTR_LIMIT,       pCtx->gdtr.cbGdt);
        rc |= VMXWriteVMCS(VMX_VMCS_GUEST_GDTR_BASE,        pCtx->gdtr.pGdt);
        AssertRC(rc);
    }
    /* Guest CPU context: IDTR. */
    if (pVM->hwaccm.s.fContextUseFlags & HWACCM_CHANGED_GUEST_IDTR)
    {
        rc  = VMXWriteVMCS(VMX_VMCS_GUEST_IDTR_LIMIT,       pCtx->idtr.cbIdt);
        rc |= VMXWriteVMCS(VMX_VMCS_GUEST_IDTR_BASE,        pCtx->idtr.pIdt);
        AssertRC(rc);
    }

    /*
     * Sysenter MSRs
     */
    if (pVM->hwaccm.s.fContextUseFlags & HWACCM_CHANGED_GUEST_SYSENTER_MSR)
    {
        rc  = VMXWriteVMCS(VMX_VMCS_GUEST_SYSENTER_CS,      pCtx->SysEnter.cs);
        rc |= VMXWriteVMCS(VMX_VMCS_GUEST_SYSENTER_EIP,     pCtx->SysEnter.eip);
        rc |= VMXWriteVMCS(VMX_VMCS_GUEST_SYSENTER_ESP,     pCtx->SysEnter.esp);
        AssertRC(rc);
    }

    /* Control registers */
    if (pVM->hwaccm.s.fContextUseFlags & HWACCM_CHANGED_GUEST_CR0)
    {
        val = pCtx->cr0;
        rc  = VMXWriteVMCS(VMX_VMCS_CTRL_CR0_READ_SHADOW,   val);
        Log2(("Guest CR0-shadow %08x\n", val));
        if (CPUMIsGuestFPUStateActive(pVM) == false)
        {
            /* Always use #NM exceptions to load the FPU/XMM state on demand. */
            val |= X86_CR0_TS | X86_CR0_ET | X86_CR0_NE | X86_CR0_MP;
        }
        else
        {
            Assert(pVM->hwaccm.s.vmx.fResumeVM == true);
            /** @todo check if we support the old style mess correctly. */
            if (!(val & X86_CR0_NE))
            {
                Log(("Forcing X86_CR0_NE!!!\n"));

                /* Also catch floating point exceptions as we need to report them to the guest in a different way. */
                if (!pVM->hwaccm.s.fFPUOldStyleOverride)
                {
                    rc = VMXWriteVMCS(VMX_VMCS_CTRL_EXCEPTION_BITMAP, HWACCM_VMX_TRAP_MASK | BIT(16));
                    AssertRC(rc);
                    pVM->hwaccm.s.fFPUOldStyleOverride = true;
                }
            }

            val |= X86_CR0_NE;  /* always turn on the native mechanism to report FPU errors (old style uses interrupts) */
        }
        rc |= VMXWriteVMCS(VMX_VMCS_GUEST_CR0,              val);
        Log2(("Guest CR0 %08x\n", val));
        /* CR0 flags owned by the host; if the guests attempts to change them, then
         * the VM will exit.
         */
        val =   X86_CR0_PE
              | X86_CR0_WP  /** @todo do we care? (we do if we start patching the guest) */
              | X86_CR0_PG
              | X86_CR0_TS
              | X86_CR0_ET
              | X86_CR0_NE
              | X86_CR0_MP;
        pVM->hwaccm.s.vmx.cr0_mask = val;

        rc |= VMXWriteVMCS(VMX_VMCS_CTRL_CR0_MASK, val);
        Log2(("Guest CR0-mask %08x\n", val));
        AssertRC(rc);
    }
    if (pVM->hwaccm.s.fContextUseFlags & HWACCM_CHANGED_GUEST_CR4)
    {
        /* CR4 */
        rc  = VMXWriteVMCS(VMX_VMCS_CTRL_CR4_READ_SHADOW,   pCtx->cr4);
        Log2(("Guest CR4-shadow %08x\n", pCtx->cr4));
        /* Set the required bits in cr4 too (currently X86_CR4_VMXE). */
        val = pCtx->cr4 | (uint32_t)pVM->hwaccm.s.vmx.msr.vmx_cr4_fixed0;
        switch(pVM->hwaccm.s.enmShadowMode)
        {
        case PGMMODE_REAL:
        case PGMMODE_PROTECTED:     /* Protected mode, no paging. */
            AssertFailed();
            return VERR_PGM_UNSUPPORTED_HOST_PAGING_MODE;

        case PGMMODE_32_BIT:        /* 32-bit paging. */
            break;

        case PGMMODE_PAE:           /* PAE paging. */
        case PGMMODE_PAE_NX:        /* PAE paging with NX enabled. */
            /** @todo use normal 32 bits paging */
            val |= X86_CR4_PAE;
            break;

        case PGMMODE_AMD64:         /* 64-bit AMD paging (long mode). */
        case PGMMODE_AMD64_NX:      /* 64-bit AMD paging (long mode) with NX enabled. */
            AssertFailed();
            return VERR_PGM_UNSUPPORTED_HOST_PAGING_MODE;

        default:                   /* shut up gcc */
            AssertFailed();
            return VERR_PGM_UNSUPPORTED_HOST_PAGING_MODE;
        }
        rc |= VMXWriteVMCS(VMX_VMCS_GUEST_CR4,              val);
        Log2(("Guest CR4 %08x\n", val));
        /* CR4 flags owned by the host; if the guests attempts to change them, then
         * the VM will exit.
         */
        val =   X86_CR4_PAE
              | X86_CR4_PGE
              | X86_CR4_PSE
              | X86_CR4_VMXE;
        pVM->hwaccm.s.vmx.cr4_mask = val;

        rc |= VMXWriteVMCS(VMX_VMCS_CTRL_CR4_MASK, val);
        Log2(("Guest CR4-mask %08x\n", val));
        AssertRC(rc);
    }

    if (pVM->hwaccm.s.fContextUseFlags & HWACCM_CHANGED_GUEST_CR3)
    {
        /* Save our shadow CR3 register. */
        val = PGMGetHyperCR3(pVM);
        rc = VMXWriteVMCS(VMX_VMCS_GUEST_CR3, val);
        AssertRC(rc);
    }

    /* Debug registers. */
    if (pVM->hwaccm.s.fContextUseFlags & HWACCM_CHANGED_GUEST_DEBUG)
    {
        /** @todo DR0-6 */
        val  = pCtx->dr7;
        val &= ~(BIT(11) | BIT(12) | BIT(14) | BIT(15));    /* must be zero */
        val |= 0x400;                                       /* must be one */
#ifdef VBOX_STRICT
        val = 0x400;
#endif
        rc |= VMXWriteVMCS(VMX_VMCS_GUEST_DR7,              val);
        AssertRC(rc);

        /* IA32_DEBUGCTL MSR. */
        rc  = VMXWriteVMCS(VMX_VMCS_GUEST_DEBUGCTL_FULL,    0);
        rc |= VMXWriteVMCS(VMX_VMCS_GUEST_DEBUGCTL_HIGH,    0);
        AssertRC(rc);

        /** @todo */
        rc |= VMXWriteVMCS(VMX_VMCS_GUEST_DEBUG_EXCEPTIONS,         0);
        AssertRC(rc);
    }

    /* EIP, ESP and EFLAGS */
    rc  = VMXWriteVMCS(VMX_VMCS_GUEST_RIP,              pCtx->eip);
    rc |= VMXWriteVMCS(VMX_VMCS_GUEST_RSP,              pCtx->esp);
    AssertRC(rc);

    /* Bits 22-31, 15, 5 & 3 must be zero. Bit 1 must be 1. */
    val  = pCtx->eflags.u32;
    val &= VMX_EFLAGS_RESERVED_0;
    val |= VMX_EFLAGS_RESERVED_1;
    rc   = VMXWriteVMCS(VMX_VMCS_GUEST_RFLAGS,           val);
    AssertRC(rc);

    /** TSC offset. */
    uint64_t u64TSCOffset = TMCpuTickGetOffset(pVM);

#if HC_ARCH_BITS == 64
    rc  = VMXWriteVMCS(VMX_VMCS_CTRL_TSC_OFFSET_FULL, u64TSCOffset);
#else
    rc  = VMXWriteVMCS(VMX_VMCS_CTRL_TSC_OFFSET_FULL, (uint32_t)u64TSCOffset);
    rc |= VMXWriteVMCS(VMX_VMCS_CTRL_TSC_OFFSET_HIGH, (uint32_t)(u64TSCOffset >> 32ULL));
#endif
    AssertRC(rc);

    /* Done. */
    pVM->hwaccm.s.fContextUseFlags &= ~HWACCM_CHANGED_ALL_GUEST;

    return rc;
}

/**
 * Runs guest code in a VMX VM.
 *
 * @note NEVER EVER turn on interrupts here. Due to our illegal entry into the kernel, it might mess things up. (XP kernel traps have been frequently observed)
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 * @param   pCtx        Guest context
 */
HWACCMR0DECL(int) VMXR0RunGuestCode(PVM pVM, CPUMCTX *pCtx)
{
    int         rc = VINF_SUCCESS;
    RTCCUINTREG val, valShadow;
    RTCCUINTREG exitReason, instrError, cbInstr;
    RTGCUINTPTR exitQualification;
    RTGCUINTPTR intInfo = 0; /* shut up buggy gcc 4 */
    RTGCUINTPTR errCode, instrInfo, uInterruptState;

    Log2(("\nE"));

    STAM_PROFILE_ADV_START(&pVM->hwaccm.s.StatEntry, x);

#ifdef VBOX_STRICT
    rc = VMXReadVMCS(VMX_VMCS_CTRL_PIN_EXEC_CONTROLS, &val);
    AssertRC(rc);
    Log2(("VMX_VMCS_CTRL_PIN_EXEC_CONTROLS = %08x\n", val));

    /* allowed zero */
    if ((val & (pVM->hwaccm.s.vmx.msr.vmx_pin_ctls & 0xFFFFFFFF)) != (pVM->hwaccm.s.vmx.msr.vmx_pin_ctls & 0xFFFFFFFF))
    {
        Log(("Invalid VMX_VMCS_CTRL_PIN_EXEC_CONTROLS: zero\n"));
    }
    /* allowed one */
    if ((val & ~(pVM->hwaccm.s.vmx.msr.vmx_pin_ctls >> 32ULL)) != 0)
    {
        Log(("Invalid VMX_VMCS_CTRL_PIN_EXEC_CONTROLS: one\n"));
    }

    rc = VMXReadVMCS(VMX_VMCS_CTRL_PROC_EXEC_CONTROLS, &val);
    AssertRC(rc);
    Log2(("VMX_VMCS_CTRL_PROC_EXEC_CONTROLS = %08x\n", val));

    /* allowed zero */
    if ((val & (pVM->hwaccm.s.vmx.msr.vmx_proc_ctls & 0xFFFFFFFF)) != (pVM->hwaccm.s.vmx.msr.vmx_proc_ctls & 0xFFFFFFFF))
    {
        Log(("Invalid VMX_VMCS_CTRL_PROC_EXEC_CONTROLS: zero\n"));
    }
    /* allowed one */
    if ((val & ~(pVM->hwaccm.s.vmx.msr.vmx_proc_ctls >> 32ULL)) != 0)
    {
        Log(("Invalid VMX_VMCS_CTRL_PROC_EXEC_CONTROLS: one\n"));
    }

    rc = VMXReadVMCS(VMX_VMCS_CTRL_ENTRY_CONTROLS, &val);
    AssertRC(rc);
    Log2(("VMX_VMCS_CTRL_ENTRY_CONTROLS = %08x\n", val));

    /* allowed zero */
    if ((val & (pVM->hwaccm.s.vmx.msr.vmx_entry & 0xFFFFFFFF)) != (pVM->hwaccm.s.vmx.msr.vmx_entry & 0xFFFFFFFF))
    {
        Log(("Invalid VMX_VMCS_CTRL_ENTRY_CONTROLS: zero\n"));
    }
    /* allowed one */
    if ((val & ~(pVM->hwaccm.s.vmx.msr.vmx_entry >> 32ULL)) != 0)
    {
        Log(("Invalid VMX_VMCS_CTRL_ENTRY_CONTROLS: one\n"));
    }

    rc = VMXReadVMCS(VMX_VMCS_CTRL_EXIT_CONTROLS, &val);
    AssertRC(rc);
    Log2(("VMX_VMCS_CTRL_EXIT_CONTROLS = %08x\n", val));

    /* allowed zero */
    if ((val & (pVM->hwaccm.s.vmx.msr.vmx_exit & 0xFFFFFFFF)) != (pVM->hwaccm.s.vmx.msr.vmx_exit & 0xFFFFFFFF))
    {
        Log(("Invalid VMX_VMCS_CTRL_EXIT_CONTROLS: zero\n"));
    }
    /* allowed one */
    if ((val & ~(pVM->hwaccm.s.vmx.msr.vmx_exit >> 32ULL)) != 0)
    {
        Log(("Invalid VMX_VMCS_CTRL_EXIT_CONTROLS: one\n"));
    }
#endif

#if 0
    /*
     * Check if debug registers are armed.
     */
    uint32_t u32DR7 = ASMGetDR7();
    if (u32DR7 & X86_DR7_ENABLED_MASK)
    {
        pVM->cpum.s.fUseFlags |= CPUM_USE_DEBUG_REGS_HOST;
    }
    else
        pVM->cpum.s.fUseFlags &= ~CPUM_USE_DEBUG_REGS_HOST;
#endif

    /* We can jump to this point to resume execution after determining that a VM-exit is innocent.
     */
ResumeExecution:

    /* Check for irq inhibition due to instruction fusing (sti, mov ss). */
    if (VM_FF_ISSET(pVM, VM_FF_INHIBIT_INTERRUPTS))
    {
        Log(("VM_FF_INHIBIT_INTERRUPTS at %VGv successor %VGv\n", pCtx->eip, EMGetInhibitInterruptsPC(pVM)));
        if (pCtx->eip != EMGetInhibitInterruptsPC(pVM))
        {
            /** @note we intentionally don't clear VM_FF_INHIBIT_INTERRUPTS here.
             *  Before we are able to execute this instruction in raw mode (iret to guest code) an external interrupt might
             *  force a world switch again. Possibly allowing a guest interrupt to be dispatched in the process. This could
             *  break the guest. Sounds very unlikely, but such timing sensitive problem are not as rare as you might think.
             */
            VM_FF_CLEAR(pVM, VM_FF_INHIBIT_INTERRUPTS);
            /* Irq inhibition is no longer active; clear the corresponding VMX state. */
            rc = VMXWriteVMCS(VMX_VMCS_GUEST_INTERRUPTIBILITY_STATE,   0);
            AssertRC(rc);
        }
    }
    else
    {
        /* Irq inhibition is no longer active; clear the corresponding VMX state. */
        rc = VMXWriteVMCS(VMX_VMCS_GUEST_INTERRUPTIBILITY_STATE,   0);
        AssertRC(rc);
    }

    /* Check for pending actions that force us to go back to ring 3. */
    if (VM_FF_ISPENDING(pVM, VM_FF_TO_R3 | VM_FF_TIMER))
    {
        VM_FF_CLEAR(pVM, VM_FF_TO_R3);
        STAM_COUNTER_INC(&pVM->hwaccm.s.StatSwitchToR3);
        STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatEntry, x);
        rc = VINF_EM_RAW_TO_R3;
        goto end;
    }
    /* Pending request packets might contain actions that need immediate attention, such as pending hardware interrupts. */
    if (VM_FF_ISPENDING(pVM, VM_FF_REQUEST))
    {
        STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatEntry, x);
        rc = VINF_EM_PENDING_REQUEST;
        goto end;
    }

    /* When external interrupts are pending, we should exit the VM when IF is set. */
    /** @note *after* VM_FF_INHIBIT_INTERRUPTS check!!! */
    rc = VMXR0CheckPendingInterrupt(pVM, pCtx);
    if (VBOX_FAILURE(rc))
    {
        STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatEntry, x);
        goto end;
    }

    /** @todo check timers?? */

    /* Save the host state first. */
    rc  = VMXR0SaveHostState(pVM);
    if (rc != VINF_SUCCESS)
    {
        STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatEntry, x);
        goto end;
    }
    /* Load the guest state */
    rc = VMXR0LoadGuestState(pVM, pCtx);
    if (rc != VINF_SUCCESS)
    {
        STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatEntry, x);
        goto end;
    }

    /* Non-register state Guest Context */
    /** @todo change me according to cpu state */
    rc = VMXWriteVMCS(VMX_VMCS_GUEST_ACTIVITY_STATE,           VMX_CMS_GUEST_ACTIVITY_ACTIVE);
    AssertRC(rc);

    STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatEntry, x);

    /* Manual save and restore:
     * - General purpose registers except RIP, RSP
     *
     * Trashed:
     * - CR2 (we don't care)
     * - LDTR (reset to 0)
     * - DRx (presumably not changed at all)
     * - DR7 (reset to 0x400)
     * - EFLAGS (reset to BIT(1); not relevant)
     *
     */

    /* All done! Let's start VM execution. */
    STAM_PROFILE_ADV_START(&pVM->hwaccm.s.StatInGC, x);
    if (pVM->hwaccm.s.vmx.fResumeVM == false)
    {
        rc = VMXStartVM(pCtx);
    }
    else
    {
        rc = VMXResumeVM(pCtx);
    }

    /* In case we execute a goto ResumeExecution later on. */
    pVM->hwaccm.s.vmx.fResumeVM = true;

    /**
     * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
     * IMPORTANT: WE CAN'T DO ANY LOGGING OR OPERATIONS THAT CAN DO A LONGJMP BACK TO RING 3 *BEFORE* WE'VE SYNCED BACK (MOST OF) THE GUEST STATE
     * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
     */

    STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatInGC, x);
    STAM_PROFILE_ADV_START(&pVM->hwaccm.s.StatExit, x);

    switch (rc)
    {
    case VINF_SUCCESS:
        break;

    case VERR_VMX_INVALID_VMXON_PTR:
        AssertFailed();
        goto end;

    case VERR_VMX_UNABLE_TO_START_VM:
    case VERR_VMX_UNABLE_TO_RESUME_VM:
    {
#ifdef VBOX_STRICT
        int      rc1;

        rc1  = VMXReadVMCS(VMX_VMCS_RO_EXIT_REASON, &exitReason);
        rc1 |= VMXReadVMCS(VMX_VMCS_RO_VM_INSTR_ERROR, &instrError);
        AssertRC(rc1);
        if (rc1 == VINF_SUCCESS)
        {
            RTGDTR    gdtr;
            PVBOXDESC pDesc;

            ASMGetGDTR(&gdtr);
            VMXWriteVMCS(VMX_VMCS_HOST_GDTR_BASE, gdtr.pGdt);

            Log(("Unable to start/resume VM for reason: %x. Instruction error %x\n", (uint32_t)exitReason, (uint32_t)instrError));
            Log(("Current stack %08x\n", &rc1));

            VMXReadVMCS(VMX_VMCS_HOST_CR0, &val);
            Log(("VMX_VMCS_HOST_CR0 %08x\n", val));

            VMXReadVMCS(VMX_VMCS_HOST_CR3, &val);
            Log(("VMX_VMCS_HOST_CR3 %08x\n", val));

            VMXReadVMCS(VMX_VMCS_HOST_CR4, &val);
            Log(("VMX_VMCS_HOST_CR4 %08x\n", val));

            VMXReadVMCS(VMX_VMCS_HOST_FIELD_CS, &val);
            Log(("VMX_VMCS_HOST_FIELD_CS %08x\n", val));
            if (val < gdtr.cbGdt)
            {
                pDesc  = &((PVBOXDESC)gdtr.pGdt)[val >> X86_SEL_SHIFT];
                HWACCMR0DumpDescriptor(pDesc, val, "CS: ");
            }

            VMXReadVMCS(VMX_VMCS_HOST_FIELD_DS, &val);
            Log(("VMX_VMCS_HOST_FIELD_DS %08x\n", val));
            if (val < gdtr.cbGdt)
            {
                pDesc  = &((PVBOXDESC)gdtr.pGdt)[val >> X86_SEL_SHIFT];
                HWACCMR0DumpDescriptor(pDesc, val, "DS: ");
            }

            VMXReadVMCS(VMX_VMCS_HOST_FIELD_ES, &val);
            Log(("VMX_VMCS_HOST_FIELD_ES %08x\n", val));
            if (val < gdtr.cbGdt)
            {
                pDesc  = &((PVBOXDESC)gdtr.pGdt)[val >> X86_SEL_SHIFT];
                HWACCMR0DumpDescriptor(pDesc, val, "ES: ");
            }

            VMXReadVMCS(VMX_VMCS_HOST_FIELD_FS, &val);
            Log(("VMX_VMCS_HOST_FIELD_FS %08x\n", val));
            if (val < gdtr.cbGdt)
            {
                pDesc  = &((PVBOXDESC)gdtr.pGdt)[val >> X86_SEL_SHIFT];
                HWACCMR0DumpDescriptor(pDesc, val, "FS: ");
            }

            VMXReadVMCS(VMX_VMCS_HOST_FIELD_GS, &val);
            Log(("VMX_VMCS_HOST_FIELD_GS %08x\n", val));
            if (val < gdtr.cbGdt)
            {
                pDesc  = &((PVBOXDESC)gdtr.pGdt)[val >> X86_SEL_SHIFT];
                HWACCMR0DumpDescriptor(pDesc, val, "GS: ");
            }

            VMXReadVMCS(VMX_VMCS_HOST_FIELD_SS, &val);
            Log(("VMX_VMCS_HOST_FIELD_SS %08x\n", val));
            if (val < gdtr.cbGdt)
            {
                pDesc  = &((PVBOXDESC)gdtr.pGdt)[val >> X86_SEL_SHIFT];
                HWACCMR0DumpDescriptor(pDesc, val, "SS: ");
            }

            VMXReadVMCS(VMX_VMCS_HOST_FIELD_TR, &val);
            Log(("VMX_VMCS_HOST_FIELD_TR %08x\n", val));
            if (val < gdtr.cbGdt)
            {
                pDesc  = &((PVBOXDESC)gdtr.pGdt)[val >> X86_SEL_SHIFT];
                HWACCMR0DumpDescriptor(pDesc, val, "TR: ");
            }

            VMXReadVMCS(VMX_VMCS_HOST_TR_BASE, &val);
            Log(("VMX_VMCS_HOST_TR_BASE %VGv\n", val));

            VMXReadVMCS(VMX_VMCS_HOST_GDTR_BASE, &val);
            Log(("VMX_VMCS_HOST_GDTR_BASE %VGv\n", val));
            VMXReadVMCS(VMX_VMCS_HOST_IDTR_BASE, &val);
            Log(("VMX_VMCS_HOST_IDTR_BASE %VGv\n", val));

            VMXReadVMCS(VMX_VMCS_HOST_SYSENTER_CS, &val);
            Log(("VMX_VMCS_HOST_SYSENTER_CS  %08x\n", val));

            VMXReadVMCS(VMX_VMCS_HOST_SYSENTER_EIP, &val);
            Log(("VMX_VMCS_HOST_SYSENTER_EIP %VGv\n", val));

            VMXReadVMCS(VMX_VMCS_HOST_SYSENTER_ESP, &val);
            Log(("VMX_VMCS_HOST_SYSENTER_ESP %VGv\n", val));

            VMXReadVMCS(VMX_VMCS_HOST_RSP, &val);
            Log(("VMX_VMCS_HOST_RSP %VGv\n", val));
            VMXReadVMCS(VMX_VMCS_HOST_RIP, &val);
            Log(("VMX_VMCS_HOST_RIP %VGv\n", val));
        }
#endif /* VBOX_STRICT */
        goto end;
    }

    default:
        /* impossible */
        AssertFailed();
        goto end;
    }
    /* Success. Query the guest state and figure out what has happened. */

    /* Investigate why there was a VM-exit. */
    rc  = VMXReadVMCS(VMX_VMCS_RO_EXIT_REASON, &exitReason);
    STAM_COUNTER_INC(&pVM->hwaccm.s.pStatExitReason[exitReason & MASK_EXITREASON_STAT]);

    exitReason &= 0xffff;   /* bit 0-15 contain the exit code. */
    rc |= VMXReadVMCS(VMX_VMCS_RO_VM_INSTR_ERROR, &instrError);
    rc |= VMXReadVMCS(VMX_VMCS_RO_EXIT_INSTR_LENGTH, &cbInstr);
    rc |= VMXReadVMCS(VMX_VMCS_RO_EXIT_INTERRUPTION_INFO, &val);
    intInfo   = val;
    rc |= VMXReadVMCS(VMX_VMCS_RO_EXIT_INTERRUPTION_ERRCODE, &val);
    errCode   = val;    /* might not be valid; depends on VMX_EXIT_INTERRUPTION_INFO_ERROR_CODE_IS_VALID. */
    rc |= VMXReadVMCS(VMX_VMCS_RO_EXIT_INSTR_INFO, &val);
    instrInfo = val;
    rc |= VMXReadVMCS(VMX_VMCS_RO_EXIT_QUALIFICATION, &val);
    exitQualification = val;
    AssertRC(rc);

    /* Take care of instruction fusing (sti, mov ss) */
    rc |= VMXReadVMCS(VMX_VMCS_GUEST_INTERRUPTIBILITY_STATE, &val);
    uInterruptState = val;
    if (uInterruptState != 0)
    {
        Assert(uInterruptState <= 2);    /* only sti & mov ss */
        Log(("uInterruptState %x eip=%VGv\n", uInterruptState, pCtx->eip));
        EMSetInhibitInterruptsPC(pVM, pCtx->eip);
    }
    else
        VM_FF_CLEAR(pVM, VM_FF_INHIBIT_INTERRUPTS);

    /* Let's first sync back eip, esp, and eflags. */
    rc = VMXReadVMCS(VMX_VMCS_GUEST_RIP,              &val);
    AssertRC(rc);
    pCtx->eip               = val;
    rc = VMXReadVMCS(VMX_VMCS_GUEST_RSP,              &val);
    AssertRC(rc);
    pCtx->esp               = val;
    rc = VMXReadVMCS(VMX_VMCS_GUEST_RFLAGS,           &val);
    AssertRC(rc);
    pCtx->eflags.u32        = val;

    /* Control registers. */
    VMXReadVMCS(VMX_VMCS_CTRL_CR0_READ_SHADOW,   &valShadow);
    VMXReadVMCS(VMX_VMCS_GUEST_CR0,              &val);
    val = (valShadow & pVM->hwaccm.s.vmx.cr0_mask) | (val & ~pVM->hwaccm.s.vmx.cr0_mask);
    CPUMSetGuestCR0(pVM, val);

    VMXReadVMCS(VMX_VMCS_CTRL_CR4_READ_SHADOW,   &valShadow);
    VMXReadVMCS(VMX_VMCS_GUEST_CR4,              &val);
    val = (valShadow & pVM->hwaccm.s.vmx.cr4_mask) | (val & ~pVM->hwaccm.s.vmx.cr4_mask);
    CPUMSetGuestCR4(pVM, val);

    CPUMSetGuestCR2(pVM, ASMGetCR2());

    VMXReadVMCS(VMX_VMCS_GUEST_DR7,              &val);
    CPUMSetGuestDR7(pVM, val);

    /* Guest CPU context: ES, CS, SS, DS, FS, GS. */
    VMX_READ_SELREG(ES, es);
    VMX_READ_SELREG(SS, ss);
    VMX_READ_SELREG(CS, cs);
    VMX_READ_SELREG(DS, ds);
    VMX_READ_SELREG(FS, fs);
    VMX_READ_SELREG(GS, gs);

    /** @note NOW IT'S SAFE FOR LOGGING! */
    Log2(("Raw exit reason %08x\n", exitReason));

    /* Check if an injected event was interrupted prematurely. */
    rc = VMXReadVMCS(VMX_VMCS_RO_IDT_INFO,            &val);
    AssertRC(rc);
    pVM->hwaccm.s.Event.intInfo = VMX_VMCS_CTRL_ENTRY_IRQ_INFO_FROM_EXIT_INT_INFO(val);
    if (    VMX_EXIT_INTERRUPTION_INFO_VALID(pVM->hwaccm.s.Event.intInfo)
        &&  VMX_EXIT_INTERRUPTION_INFO_TYPE(pVM->hwaccm.s.Event.intInfo) != VMX_EXIT_INTERRUPTION_INFO_TYPE_SW)
    {
        Log(("Pending inject %VX64 at %08x exit=%08x intInfo=%08x exitQualification=%08x\n", pVM->hwaccm.s.Event.intInfo, pCtx->eip, exitReason, intInfo, exitQualification));
        pVM->hwaccm.s.Event.fPending = true;
        /* Error code present? */
        if (VMX_EXIT_INTERRUPTION_INFO_ERROR_CODE_IS_VALID(pVM->hwaccm.s.Event.intInfo))
        {
            rc = VMXReadVMCS(VMX_VMCS_RO_IDT_ERRCODE, &val);
            AssertRC(rc);
            pVM->hwaccm.s.Event.errCode  = val;
        }
        else
            pVM->hwaccm.s.Event.errCode  = 0;
    }

#ifdef VBOX_STRICT
    if (exitReason == VMX_EXIT_ERR_INVALID_GUEST_STATE)
        HWACCMDumpRegs(pCtx);
#endif

    Log2(("E%d", exitReason));
    Log2(("Exit reason %d, exitQualification %08x\n", exitReason, exitQualification));
    Log2(("instrInfo=%d instrError=%d instr length=%d\n", instrInfo, instrError, cbInstr));
    Log2(("Interruption error code %d\n", errCode));
    Log2(("IntInfo = %08x\n", intInfo));
    Log2(("New EIP=%VGv\n", pCtx->eip));

    /* Some cases don't need a complete resync of the guest CPU state; handle them here. */
    switch (exitReason)
    {
    case VMX_EXIT_EXCEPTION:            /* 0 Exception or non-maskable interrupt (NMI). */
    case VMX_EXIT_EXTERNAL_IRQ:         /* 1 External interrupt. */
    {
        uint32_t vector = VMX_EXIT_INTERRUPTION_INFO_VECTOR(intInfo);

        if (!VMX_EXIT_INTERRUPTION_INFO_VALID(intInfo))
        {
            Assert(exitReason == VMX_EXIT_EXTERNAL_IRQ);
            /* External interrupt; leave to allow it to be dispatched again. */
            rc = VINF_EM_RAW_INTERRUPT;
            break;
        }
        switch (VMX_EXIT_INTERRUPTION_INFO_TYPE(intInfo))
        {
        case VMX_EXIT_INTERRUPTION_INFO_TYPE_NMI:   /* Non-maskable interrupt. */
            /* External interrupt; leave to allow it to be dispatched again. */
            rc = VINF_EM_RAW_INTERRUPT;
            break;

        case VMX_EXIT_INTERRUPTION_INFO_TYPE_EXT:   /* External hardware interrupt. */
            AssertFailed(); /* can't come here; fails the first check. */
            break;

        case VMX_EXIT_INTERRUPTION_INFO_TYPE_SWEXCPT:   /* Software exception. (#BP or #OF) */
            Assert(vector == 3 || vector == 4);
            /* no break */
        case VMX_EXIT_INTERRUPTION_INFO_TYPE_HWEXCPT:   /* Hardware exception. */
            Log2(("Hardware/software interrupt %d\n", vector));
            switch (vector)
            {
            case X86_XCPT_NM:
            {
                uint32_t oldCR0;

                Log(("#NM fault at %VGv error code %x\n", pCtx->eip, errCode));

                /** @todo don't intercept #NM exceptions anymore when we've activated the guest FPU state. */
                oldCR0 = ASMGetCR0();
                /* If we sync the FPU/XMM state on-demand, then we can continue execution as if nothing has happened. */
                rc = CPUMHandleLazyFPU(pVM);
                if (rc == VINF_SUCCESS)
                {
                    Assert(CPUMIsGuestFPUStateActive(pVM));

                    /* CPUMHandleLazyFPU could have changed CR0; restore it. */
                    ASMSetCR0(oldCR0);

                    STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitShadowNM);

                    /* Continue execution. */
                    STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatExit, x);
                    pVM->hwaccm.s.fContextUseFlags |= HWACCM_CHANGED_GUEST_CR0;

                    goto ResumeExecution;
                }

                Log(("Forward #NM fault to the guest\n"));
                STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitGuestNM);
                rc = VMXR0InjectEvent(pVM, pCtx, VMX_VMCS_CTRL_ENTRY_IRQ_INFO_FROM_EXIT_INT_INFO(intInfo), cbInstr, 0);
                AssertRC(rc);
                STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatExit, x);
                goto ResumeExecution;
            }

            case X86_XCPT_PF: /* Page fault */
            {
                Log2(("Page fault at %VGv error code %x\n", exitQualification ,errCode));
                /* Exit qualification contains the linear address of the page fault. */
                TRPMAssertTrap(pVM, X86_XCPT_PF, TRPM_TRAP);
                TRPMSetErrorCode(pVM, errCode);
                TRPMSetFaultAddress(pVM, exitQualification);

                /* Forward it to our trap handler first, in case our shadow pages are out of sync. */
                rc = PGMTrap0eHandler(pVM, errCode, CPUMCTX2CORE(pCtx), (RTGCPTR)exitQualification);
                Log2(("PGMTrap0eHandler %VGv returned %Vrc\n", pCtx->eip, rc));
                if (rc == VINF_SUCCESS)
                {   /* We've successfully synced our shadow pages, so let's just continue execution. */
                    Log2(("Shadow page fault at %VGv cr2=%VGv error code %x\n", pCtx->eip, exitQualification ,errCode));
                    STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitShadowPF);

                    TRPMResetTrap(pVM);

                    STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatExit, x);
                    goto ResumeExecution;
                }
                else
                if (rc == VINF_EM_RAW_GUEST_TRAP)
                {   /* A genuine pagefault.
                     * Forward the trap to the guest by injecting the exception and resuming execution.
                     */
                    Log2(("Forward page fault to the guest\n"));
                    STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitGuestPF);
                    /* The error code might have been changed. */
                    errCode = TRPMGetErrorCode(pVM);

                    TRPMResetTrap(pVM);

                    /* Now we must update CR2. */
                    pCtx->cr2 = exitQualification;
                    rc = VMXR0InjectEvent(pVM, pCtx, VMX_VMCS_CTRL_ENTRY_IRQ_INFO_FROM_EXIT_INT_INFO(intInfo), cbInstr, errCode);
                    AssertRC(rc);

                    STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatExit, x);
                    goto ResumeExecution;
                }
#ifdef VBOX_STRICT
                if (rc != VINF_EM_RAW_EMULATE_INSTR)
                    Log(("PGMTrap0eHandler failed with %d\n", rc));
#endif
                /* Need to go back to the recompiler to emulate the instruction. */
                TRPMResetTrap(pVM);
                break;
            }

            case X86_XCPT_MF: /* Floating point exception. */
            {
                STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitGuestMF);
                if (!(pCtx->cr0 & X86_CR0_NE))
                {
                    /* old style FPU error reporting needs some extra work. */
                    /** @todo don't fall back to the recompiler, but do it manually. */
                    rc = VINF_EM_RAW_EMULATE_INSTR;
                    break;
                }
                Log(("Trap %x at %VGv\n", vector, pCtx->eip));
                rc = VMXR0InjectEvent(pVM, pCtx, VMX_VMCS_CTRL_ENTRY_IRQ_INFO_FROM_EXIT_INT_INFO(intInfo), cbInstr, errCode);
                AssertRC(rc);

                STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatExit, x);
                goto ResumeExecution;
            }

#ifdef VBOX_STRICT
            case X86_XCPT_GP:   /* General protection failure exception.*/
            case X86_XCPT_UD:   /* Unknown opcode exception. */
            case X86_XCPT_DE:   /* Debug exception. */
            case X86_XCPT_SS:   /* Stack segment exception. */
            case X86_XCPT_NP:   /* Segment not present exception. */
            {
                switch(vector)
                {
                case X86_XCPT_DE:
                    STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitGuestDE);
                    break;
                case X86_XCPT_UD:
                    STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitGuestUD);
                    break;
                case X86_XCPT_SS:
                    STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitGuestSS);
                    break;
                case X86_XCPT_NP:
                    STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitGuestNP);
                    break;
                case X86_XCPT_GP:
                    STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitGuestGP);
                    break;
                }

                Log(("Trap %x at %VGv\n", vector, pCtx->eip));
                rc = VMXR0InjectEvent(pVM, pCtx, VMX_VMCS_CTRL_ENTRY_IRQ_INFO_FROM_EXIT_INT_INFO(intInfo), cbInstr, errCode);
                AssertRC(rc);

                STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatExit, x);
                goto ResumeExecution;
            }
#endif
            default:
                AssertMsgFailed(("Unexpected vm-exit caused by exception %x\n", vector));
                rc = VERR_EM_INTERNAL_ERROR;
                break;
            } /* switch (vector) */

            break;

        default:
            rc = VERR_EM_INTERNAL_ERROR;
            AssertFailed();
            break;
        }

        break;
    }

    case VMX_EXIT_IRQ_WINDOW:           /* 7 Interrupt window. */
        /* Clear VM-exit on IF=1 change. */
        Log2(("VMX_EXIT_IRQ_WINDOW %VGv\n", pCtx->eip));
        rc = VMXWriteVMCS(VMX_VMCS_CTRL_PROC_EXEC_CONTROLS, pVM->hwaccm.s.vmx.proc_ctls);
        AssertRC(rc);
        STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitIrqWindow);
        goto ResumeExecution;   /* we check for pending guest interrupts there */

    case VMX_EXIT_INVD:                 /* 13 Guest software attempted to execute INVD. */
        STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitInvd);
        /* Skip instruction and continue directly. */
        pCtx->eip += cbInstr;
        /* Continue execution.*/
        STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatExit, x);
        goto ResumeExecution;

    case VMX_EXIT_CPUID:                /* 10 Guest software attempted to execute CPUID. */
    {
        Log2(("VMX: Cpuid %x\n", pCtx->eax));
        STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitCpuid);
        rc = EMInterpretCpuId(pVM, CPUMCTX2CORE(pCtx));
        if (rc == VINF_SUCCESS)
        {
            /* Update EIP and continue execution. */
            pCtx->eip += cbInstr;
            STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatExit, x);
            goto ResumeExecution;
        }
        AssertMsgFailed(("EMU: cpuid failed with %Vrc\n", rc));
        rc = VINF_EM_RAW_EMULATE_INSTR;
        break;
    }

    case VMX_EXIT_INVPG:                /* 14 Guest software attempted to execute INVPG. */
    {
        Log2(("VMX: invlpg\n"));
        STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitInvpg);
        rc = EMInterpretInvlpg(pVM, CPUMCTX2CORE(pCtx), exitQualification);
        if (rc == VINF_SUCCESS)
        {
            /* Update EIP and continue execution. */
            pCtx->eip += cbInstr;
            STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatExit, x);
            goto ResumeExecution;
        }
        AssertMsgFailed(("EMU: invlpg %VGv failed with %Vrc\n", exitQualification, rc));
        rc = VINF_EM_RAW_EMULATE_INSTR;
        break;
    }

    case VMX_EXIT_CRX_MOVE:             /* 28 Control-register accesses. */
    {
        switch (VMX_EXIT_QUALIFICATION_CRX_ACCESS(exitQualification))
        {
        case VMX_EXIT_QUALIFICATION_CRX_ACCESS_WRITE:
            Log2(("VMX: %VGv mov cr%d, x\n", pCtx->eip, VMX_EXIT_QUALIFICATION_CRX_REGISTER(exitQualification)));
            STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitCRxWrite);
            rc = EMInterpretCRxWrite(pVM, CPUMCTX2CORE(pCtx),
                                     VMX_EXIT_QUALIFICATION_CRX_REGISTER(exitQualification),
                                     VMX_EXIT_QUALIFICATION_CRX_GENREG(exitQualification));

            switch (VMX_EXIT_QUALIFICATION_CRX_REGISTER(exitQualification))
            {
            case 0:
                pVM->hwaccm.s.fContextUseFlags |= HWACCM_CHANGED_GUEST_CR0;
                break;
            case 2:
                break;
            case 3:
                pVM->hwaccm.s.fContextUseFlags |= HWACCM_CHANGED_GUEST_CR3;
                break;
            case 4:
                pVM->hwaccm.s.fContextUseFlags |= HWACCM_CHANGED_GUEST_CR4;
                break;
            default:
                AssertFailed();
            }
            /* Check if a sync operation is pending. */
            if (    rc == VINF_SUCCESS /* don't bother if we are going to ring 3 anyway */
                &&  VM_FF_ISPENDING(pVM, VM_FF_PGM_SYNC_CR3 | VM_FF_PGM_SYNC_CR3_NON_GLOBAL))
            {
                rc = PGMSyncCR3(pVM, CPUMGetGuestCR0(pVM), CPUMGetGuestCR3(pVM), CPUMGetGuestCR4(pVM), VM_FF_ISSET(pVM, VM_FF_PGM_SYNC_CR3));
                AssertRC(rc);
            }
            break;

        case VMX_EXIT_QUALIFICATION_CRX_ACCESS_READ:
            Log2(("VMX: mov x, crx\n"));
            STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitCRxRead);
            rc = EMInterpretCRxRead(pVM, CPUMCTX2CORE(pCtx),
                                    VMX_EXIT_QUALIFICATION_CRX_GENREG(exitQualification),
                                    VMX_EXIT_QUALIFICATION_CRX_REGISTER(exitQualification));
            break;

        case VMX_EXIT_QUALIFICATION_CRX_ACCESS_CLTS:
            Log2(("VMX: clts\n"));
            STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitCLTS);
            rc = EMInterpretCLTS(pVM);
            pVM->hwaccm.s.fContextUseFlags |= HWACCM_CHANGED_GUEST_CR0;
            break;

        case VMX_EXIT_QUALIFICATION_CRX_ACCESS_LMSW:
            Log2(("VMX: lmsw %x\n", VMX_EXIT_QUALIFICATION_CRX_LMSW_DATA(exitQualification)));
            STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitLMSW);
            rc = EMInterpretLMSW(pVM, VMX_EXIT_QUALIFICATION_CRX_LMSW_DATA(exitQualification));
            pVM->hwaccm.s.fContextUseFlags |= HWACCM_CHANGED_GUEST_CR0;
            break;
        }

        /* Update EIP if no error occurred. */
        if (VBOX_SUCCESS(rc))
            pCtx->eip += cbInstr;

        if (rc == VINF_SUCCESS)
        {
            /* Only resume if successful. */
            STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatExit, x);
            goto ResumeExecution;
        }
        Assert(rc == VERR_EM_INTERPRETER || rc == VINF_PGM_CHANGE_MODE || rc == VINF_PGM_SYNC_CR3);
        if (rc == VERR_EM_INTERPRETER)
            rc = VINF_EM_RAW_EMULATE_INSTR;
        break;
    }

    case VMX_EXIT_DRX_MOVE:             /* 29 Debug-register accesses. */
    {
        /** @todo clear VMX_VMCS_CTRL_PROC_EXEC_CONTROLS_MOV_DR_EXIT after the first time and restore drx registers afterwards */
        if (VMX_EXIT_QUALIFICATION_DRX_DIRECTION(exitQualification) == VMX_EXIT_QUALIFICATION_DRX_DIRECTION_WRITE)
        {
            Log2(("VMX: mov drx%d, genreg%d\n", VMX_EXIT_QUALIFICATION_DRX_REGISTER(exitQualification), VMX_EXIT_QUALIFICATION_DRX_GENREG(exitQualification)));
            STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitDRxWrite);
            rc = EMInterpretDRxWrite(pVM, CPUMCTX2CORE(pCtx),
                                     VMX_EXIT_QUALIFICATION_DRX_REGISTER(exitQualification),
                                     VMX_EXIT_QUALIFICATION_DRX_GENREG(exitQualification));
            Log2(("DR7=%08x\n", pCtx->dr7));
        }
        else
        {
            Log2(("VMX: mov x, drx\n"));
            STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitDRxRead);
            rc = EMInterpretDRxRead(pVM, CPUMCTX2CORE(pCtx),
                                    VMX_EXIT_QUALIFICATION_DRX_GENREG(exitQualification),
                                    VMX_EXIT_QUALIFICATION_DRX_REGISTER(exitQualification));
        }
        /* Update EIP if no error occurred. */
        if (VBOX_SUCCESS(rc))
            pCtx->eip += cbInstr;

        if (rc == VINF_SUCCESS)
        {
            /* Only resume if successful. */
            STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatExit, x);
            goto ResumeExecution;
        }
        Assert(rc == VERR_EM_INTERPRETER);
        rc = VINF_EM_RAW_EMULATE_INSTR;
        break;
    }

    /** @note We'll get a #GP if the IO instruction isn't allowed (IOPL or TSS bitmap); no need to double check. */
    case VMX_EXIT_PORT_IO:              /* 30 I/O instruction. */
    {
        uint32_t uIOWidth = VMX_EXIT_QUALIFICATION_IO_WIDTH(exitQualification);
        uint32_t uPort;

        /** @todo necessary to make the distinction? */
        if (VMX_EXIT_QUALIFICATION_IO_ENCODING(exitQualification) == VMX_EXIT_QUALIFICATION_IO_ENCODING_DX)
        {
            uPort = pCtx->edx & 0xffff;
        }
        else
            uPort = VMX_EXIT_QUALIFICATION_IO_PORT(exitQualification);  /* Immediate encoding. */

        /* First simple in and out instructions. */
        /** @todo str & rep */
        if (    !VMX_EXIT_QUALIFICATION_IO_STRING(exitQualification)
            &&  !VMX_EXIT_QUALIFICATION_IO_REP(exitQualification)
            /* paranoid checks ahead */
            &&  uIOWidth != 2
            &&  uIOWidth < 4
           )
        {
            uint32_t cbSize  = aIOSize[uIOWidth];
            uint32_t uAndVal = aIOOpAnd[uIOWidth];

            if (VMX_EXIT_QUALIFICATION_IO_DIRECTION(exitQualification) == VMX_EXIT_QUALIFICATION_IO_DIRECTION_OUT)
            {
                STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitIOWrite);
                rc = IOMIOPortWrite(pVM, uPort, pCtx->eax & uAndVal, cbSize);
            }
            else
            {
                uint32_t u32Val = 0;

                STAM_COUNTER_INC(&pVM->hwaccm.s.StatExitIORead);
                rc = IOMIOPortRead(pVM, uPort, &u32Val, cbSize);
                if (rc == VINF_SUCCESS)
                {
                    /* Write back to the EAX register. */
                    pCtx->eax = (pCtx->eax & ~uAndVal) | (u32Val & uAndVal);
                }
            }
            if (rc == VINF_SUCCESS)
            {
                /* Update EIP and continue execution. */
                pCtx->eip += cbInstr;
                STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatExit, x);
                goto ResumeExecution;
            }
            Assert(rc == VINF_IOM_HC_IOPORT_READ || rc == VINF_IOM_HC_IOPORT_WRITE);
            rc = (VMX_EXIT_QUALIFICATION_IO_DIRECTION(exitQualification) == VMX_EXIT_QUALIFICATION_IO_DIRECTION_OUT)
                        ? VINF_IOM_HC_IOPORT_WRITE : VINF_IOM_HC_IOPORT_READ;
        }
        else
            rc = VINF_IOM_HC_IOPORT_READWRITE;

        break;
    }

    default:
        /* The rest is handled after syncing the entire CPU state. */
        break;
    }

    /* Remaining guest CPU context: TR, IDTR, GDTR, LDTR. */
    VMX_READ_SELREG(LDTR, ldtr);
    VMX_READ_SELREG(TR, tr);

    VMXReadVMCS(VMX_VMCS_GUEST_GDTR_LIMIT,       &val);
    pCtx->gdtr.cbGdt        = val;
    VMXReadVMCS(VMX_VMCS_GUEST_GDTR_BASE,        &val);
    pCtx->gdtr.pGdt         = val;

    VMXReadVMCS(VMX_VMCS_GUEST_IDTR_LIMIT,       &val);
    pCtx->idtr.cbIdt        = val;
    VMXReadVMCS(VMX_VMCS_GUEST_IDTR_BASE,        &val);
    pCtx->idtr.pIdt         = val;

    /*
     * System MSRs
     */
    VMXReadVMCS(VMX_VMCS_GUEST_SYSENTER_CS,      &val);
    pCtx->SysEnter.cs       = val;
    VMXReadVMCS(VMX_VMCS_GUEST_SYSENTER_EIP,     &val);
    pCtx->SysEnter.eip      = val;
    VMXReadVMCS(VMX_VMCS_GUEST_SYSENTER_ESP,     &val);
    pCtx->SysEnter.esp      = val;

    /* Signal changes for the recompiler. */
    CPUMSetChangedFlags(pVM, CPUM_CHANGED_SYSENTER_MSR | CPUM_CHANGED_LDTR | CPUM_CHANGED_GDTR | CPUM_CHANGED_IDTR | CPUM_CHANGED_TR | CPUM_CHANGED_HIDDEN_SEL_REGS);

    /* Investigate why there was a VM-exit. (part 2) */
    switch (exitReason)
    {
    case VMX_EXIT_EXCEPTION:            /* 0 Exception or non-maskable interrupt (NMI). */
    case VMX_EXIT_EXTERNAL_IRQ:         /* 1 External interrupt. */
        /* Already handled above. */
        break;

    case VMX_EXIT_TRIPLE_FAULT:         /* 2 Triple fault. */
        rc = VINF_EM_RESET;             /* Triple fault equals a reset. */
        break;

    case VMX_EXIT_INIT_SIGNAL:          /* 3 INIT signal. */
    case VMX_EXIT_SIPI:                 /* 4 Start-up IPI (SIPI). */
        rc = VINF_EM_RAW_INTERRUPT;
        AssertFailed();                 /* Can't happen. Yet. */
        break;

    case VMX_EXIT_IO_SMI_IRQ:           /* 5 I/O system-management interrupt (SMI). */
    case VMX_EXIT_SMI_IRQ:              /* 6 Other SMI. */
        rc = VINF_EM_RAW_INTERRUPT;
        AssertFailed();                 /* Can't happen afaik. */
        break;

    case VMX_EXIT_TASK_SWITCH:          /* 9 Task switch. */
        rc = VINF_EM_RAW_RING_SWITCH_INT;
        break;

    case VMX_EXIT_HLT:                  /* 12 Guest software attempted to execute HLT. */
        /** Check if external interrupts are pending; if so, don't switch back. */
        if (VM_FF_ISPENDING(pVM, (VM_FF_INTERRUPT_APIC|VM_FF_INTERRUPT_PIC)))
        {
            pCtx->eip++;    /* skip hlt */
            goto ResumeExecution;
        }

        rc = VINF_EM_RAW_EMULATE_INSTR_HLT;
        break;

    case VMX_EXIT_RDTSC:                /* 16 Guest software attempted to execute RDTSC. */
        rc = VERR_EM_INTERNAL_ERROR;
        AssertFailed(); /* we don't let it fault. */
        break;

    case VMX_EXIT_RSM:                  /* 17 Guest software attempted to execute RSM in SMM. */
        AssertFailed(); /* can't happen. */
        rc = VINF_EM_RAW_EXCEPTION_PRIVILEGED;
        break;

    case VMX_EXIT_VMCALL:               /* 18 Guest software executed VMCALL. */
    case VMX_EXIT_VMCLEAR:              /* 19 Guest software executed VMCLEAR. */
    case VMX_EXIT_VMLAUNCH:             /* 20 Guest software executed VMLAUNCH. */
    case VMX_EXIT_VMPTRLD:              /* 21 Guest software executed VMPTRLD. */
    case VMX_EXIT_VMPTRST:              /* 22 Guest software executed VMPTRST. */
    case VMX_EXIT_VMREAD:               /* 23 Guest software executed VMREAD. */
    case VMX_EXIT_VMRESUME:             /* 24 Guest software executed VMRESUME. */
    case VMX_EXIT_VMWRITE:              /* 25 Guest software executed VMWRITE. */
    case VMX_EXIT_VMXOFF:               /* 26 Guest software executed VMXOFF. */
    case VMX_EXIT_VMXON:                /* 27 Guest software executed VMXON. */
        /** @todo inject #UD immediately */
        rc = VINF_EM_RAW_EXCEPTION_PRIVILEGED;
        break;

    case VMX_EXIT_CPUID:                /* 10 Guest software attempted to execute CPUID. */
    case VMX_EXIT_INVPG:                /* 14 Guest software attempted to execute INVPG. */
    case VMX_EXIT_CRX_MOVE:             /* 28 Control-register accesses. */
    case VMX_EXIT_DRX_MOVE:             /* 29 Debug-register accesses. */
    case VMX_EXIT_PORT_IO:              /* 30 I/O instruction. */
        /* already handled above */
        AssertMsg(rc == VINF_PGM_CHANGE_MODE || rc == VINF_EM_RAW_INTERRUPT || rc == VINF_EM_RAW_EMULATE_INSTR || rc == VINF_PGM_SYNC_CR3 || rc == VINF_IOM_HC_IOPORT_READ || rc == VINF_IOM_HC_IOPORT_WRITE || rc == VINF_IOM_HC_IOPORT_READWRITE, ("rc = %d\n", rc));
        break;

    case VMX_EXIT_RDPMC:                /* 15 Guest software attempted to execute RDPMC. */
    case VMX_EXIT_RDMSR:                /* 31 RDMSR. Guest software attempted to execute RDMSR. */
    case VMX_EXIT_WRMSR:                /* 32 WRMSR. Guest software attempted to execute WRMSR. */
    case VMX_EXIT_MWAIT:                /* 36 Guest software executed MWAIT. */
    case VMX_EXIT_MONITOR:              /* 39 Guest software attempted to execute MONITOR. */
    case VMX_EXIT_PAUSE:                /* 40 Guest software attempted to execute PAUSE. */
        rc = VINF_EM_RAW_EXCEPTION_PRIVILEGED;
        break;

    case VMX_EXIT_IRQ_WINDOW:           /* 7 Interrupt window. */
        Assert(rc == VINF_EM_RAW_INTERRUPT);
        break;

    case VMX_EXIT_TPR:                  /* 43 TPR below threshold. Guest software executed MOV to CR8. */
    case VMX_EXIT_ERR_INVALID_GUEST_STATE:  /* 33 VM-entry failure due to invalid guest state. */
    case VMX_EXIT_ERR_MSR_LOAD:         /* 34 VM-entry failure due to MSR loading. */
    case VMX_EXIT_ERR_MACHINE_CHECK:    /* 41 VM-entry failure due to machine-check. */
    default:
        rc = VERR_EM_INTERNAL_ERROR;
        AssertMsgFailed(("Unexpected exit code %d\n", exitReason));                 /* Can't happen. */
        break;

    }
end:

    /* If we executed vmlaunch/vmresume and an external irq was pending, then we don't have to do a full sync the next time. */
    if (    exitReason == VMX_EXIT_EXTERNAL_IRQ
        &&  !VMX_EXIT_INTERRUPTION_INFO_VALID(intInfo))
    {
        STAM_COUNTER_INC(&pVM->hwaccm.s.StatPendingHostIrq);
        /* On the next entry we'll only sync the host context. */
        pVM->hwaccm.s.fContextUseFlags |= HWACCM_CHANGED_HOST_CONTEXT;
    }
    else
    {
        /* On the next entry we'll sync everything. */
        /** @todo we can do better than this */
        pVM->hwaccm.s.fContextUseFlags |= HWACCM_CHANGED_ALL;
    }

    STAM_PROFILE_ADV_STOP(&pVM->hwaccm.s.StatExit, x);
    Log2(("X"));
    return rc;
}


/**
 * Enable VMX
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
HWACCMR0DECL(int) VMXR0Enable(PVM pVM)
{
    Assert(pVM->hwaccm.s.vmx.fSupported);

    /* Make sure the VMX instructions don't cause #UD faults. */
    ASMSetCR4(ASMGetCR4() | X86_CR4_VMXE);

    /* Enter VMX Root Mode */
    int rc = VMXEnable(pVM->hwaccm.s.vmx.pVMXONPhys);
    if (VBOX_FAILURE(rc))
        return rc;

    /* Activate the VM Control Structure. */
    rc = VMXActivateVMCS(pVM->hwaccm.s.vmx.pVMCSPhys);
    if (VBOX_FAILURE(rc))
    {
        /* Leave VMX Root Mode. */
        VMXDisable();
        return rc;
    }
    pVM->hwaccm.s.vmx.fResumeVM = false;
    return VINF_SUCCESS;
}


/**
 * Disable VMX
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
HWACCMR0DECL(int) VMXR0Disable(PVM pVM)
{
    Assert(pVM->hwaccm.s.vmx.fSupported);

    /* Clear VM Control Structure. Marking it inactive, clearing implementation specific data and writing back VMCS data to memory. */
    int rc = VMXClearVMCS(pVM->hwaccm.s.vmx.pVMCSPhys);
    AssertRC(rc);

    /* Leave VMX Root Mode. */
    VMXDisable();

    return VINF_SUCCESS;
}

