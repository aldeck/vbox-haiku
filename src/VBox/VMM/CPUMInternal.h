/* $Id$ */
/** @file
 * CPUM - Internal header file.
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

#ifndef __CPUMInternal_h__
#define __CPUMInternal_h__

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/x86.h>


#if !defined(IN_CPUM_R3) && !defined(IN_CPUM_R0) && !defined(IN_CPUM_GC)
# error "Not in CPUM! This is an internal header!"
#endif


/** @defgroup grp_cpum_int   Internals
 * @ingroup grp_cpum
 * @internal
 * @{
 */

/** Flags and types for CPUM fault handlers
 * @{ */
/** Type: Load DS */
#define CPUM_HANDLER_DS                 1
/** Type: Load ES */
#define CPUM_HANDLER_ES                 2
/** Type: Load FS */
#define CPUM_HANDLER_FS                 3
/** Type: Load GS */
#define CPUM_HANDLER_GS                 4
/** Type: IRET */
#define CPUM_HANDLER_IRET               5
/** Type mask. */
#define CPUM_HANDLER_TYPEMASK           0xff
/** If set EBP points to the CPUMCTXCORE that's being used. */
#define CPUM_HANDLER_CTXCORE_IN_EBP     BIT(31)
/** @} */


/** Use flags (CPUM::fUseFlags).
 * (Don't forget to sync this with CPUMInternal.mac!)
 * @{ */
/** Used the FPU, SSE or such stuff. */
#define CPUM_USED_FPU                   BIT(0)
/** Used the FPU, SSE or such stuff since last we were in REM.
 * REM syncing is clearing this, lazy FPU is setting it. */
#define CPUM_USED_FPU_SINCE_REM         BIT(1)
/** Host OS is using SYSENTER and we must NULL the CS. */
#define CPUM_USE_SYSENTER               BIT(2)
/** Host OS is using SYSENTER and we must NULL the CS. */
#define CPUM_USE_SYSCALL                BIT(3)
/** Debug registers are used by host and must be disabled. */
#define CPUM_USE_DEBUG_REGS_HOST        BIT(4)
/** Enabled use of debug registers in guest context. */
#define CPUM_USE_DEBUG_REGS             BIT(5)
/** @} */


/**
 * The save host CPU state.
 */
typedef struct CPUMHOSTCTX
{
    /** FPU state. (16-byte alignment)
     * @remark On x86, the format isn't necessarily X86FXSTATE (not important). */
    X86FXSTATE      fpu;

#if HC_ARCH_BITS == 32
    /** General purpose register, selectors, flags and more
     * @{ */
    //uint32_t        eax; - scratch
    uint32_t        ebx;
    //uint32_t        ecx; - scratch
    //uint32_t        edx; - scratch
    uint32_t        edi;
    uint32_t        esi;
    uint32_t        ebp;
    /* lss pair */
    uint32_t        esp;
    RTSEL           ss;
    RTSEL           ssPadding;
    RTSEL           gs;
    RTSEL           gsPadding;
    RTSEL           fs;
    RTSEL           fsPadding;
    RTSEL           es;
    RTSEL           esPadding;
    RTSEL           ds;
    RTSEL           dsPadding;
    RTSEL           cs;
    RTSEL           csPadding;
    X86EFLAGS       eflags;
    //uint32_t        eip; - scratch
    /** @} */

    /** Control registers.
     * @{ */
    uint32_t        cr0;
    //uint32_t        cr2; - scratch
    uint32_t        cr3;
    uint32_t        cr4;
    /** @} */

    /** Debug registers.
     * @{ */
    uint32_t        dr0;
    uint32_t        dr1;
    uint32_t        dr2;
    uint32_t        dr3;
    uint32_t        dr6;
    uint32_t        dr7;
    /** @} */

    /** Global Descriptor Table register. */
    VBOXGDTR        gdtr;
    uint16_t        gdtrPadding;
    /** Interrupt Descriptor Table register. */
    VBOXIDTR        idtr;
    uint16_t        idtrPadding;
    /** The task register. */
    RTSEL           ldtr;
    RTSEL           ldtrPadding;
    /** The task register. */
    RTSEL           tr;
    RTSEL           trPadding;
    uint32_t        SysEnterPadding;

    /** The sysenter msr registers.
     * This member is not used by the hypervisor context. */
    CPUMSYSENTER    SysEnter;

    /* padding to get 32byte aligned size */
    uint8_t         auPadding[24];

#elif HC_ARCH_BITS == 64
    /** General purpose register ++
     * { */
    //uint64_t        rax; - scratch
    uint64_t        rbx;
    //uint64_t        rcx; - scratch
    //uint64_t        rdx; - scratch
    uint64_t        rdi;
    uint64_t        rsi;
    uint64_t        rbp;
    uint64_t        rsp;
    //uint64_t        r8; - scratch
    //uint64_t        r9; - scratch
    uint64_t        r10;
    uint64_t        r11;
    uint64_t        r12;
    uint64_t        r13;
    uint64_t        r14;
    uint64_t        r15;
    //uint64_t        rip; - scratch
    uint64_t        rflags;
    /** @} */

    /** Selector registers
     * @{ */
    RTSEL           ss;
    RTSEL           ssPadding;
    RTSEL           gs;
    RTSEL           gsPadding;
    RTSEL           fs;
    RTSEL           fsPadding;
    RTSEL           es;
    RTSEL           esPadding;
    RTSEL           ds;
    RTSEL           dsPadding;
    RTSEL           cs;
    RTSEL           csPadding;
    /** @} */

    /** Control registers.
     * @{ */
    uint64_t        cr0;
    //uint64_t        cr2; - scratch
    uint64_t        cr3;
    uint64_t        cr4;
    uint64_t        cr8;
    /** @} */

    /** Debug registers.
     * @{ */
    uint64_t        dr0;
    uint64_t        dr1;
    uint64_t        dr2;
    uint64_t        dr3;
    uint64_t        dr6;
    uint64_t        dr7;
    /** @} */

    /** Global Descriptor Table register. */
    uint8_t         gdtr[10]; //X86GDTR64
    uint16_t        gdtrPadding;
    /** Interrupt Descriptor Table register. */
    uint8_t         idtr[10]; //X86IDTR64
    uint16_t        idtrPadding;
    /** The task register. */
    RTSEL           ldtr;
    RTSEL           ldtrPadding;
    /** The task register. */
    RTSEL           tr;
    RTSEL           trPadding;

    /** MSRs
     * @{ */
    CPUMSYSENTER    SysEnter;
    uint64_t        FSbase;
    uint64_t        GSbase;
    uint64_t        efer;
    /** @} */

    /* padding to get 32byte aligned size */
    uint8_t         auPadding[8];
#else
# error HC_ARCH_BITS not defined
#endif 
} CPUMHOSTCTX, *PCPUMHOSTCTX;


/**
 * Converts a CPUM pointer into a VM pointer.
 * @returns Pointer to the VM structure the CPUM is part of.
 * @param   pCPUM   Pointer to CPUM instance data.
 */
#define CPUM2VM(pCPUM)  ( (PVM)((char*)pCPUM - pCPUM->offVM) )


/**
 * CPUM Data (part of VM)
 */
#pragma pack(1)
typedef struct CPUM
{
    /** Offset to the VM structure. */
    RTUINT          offVM;
    /** Pointer to CPU structure in GC. */
    GCPTRTYPE(struct CPUM *) pCPUMGC;
    /** Pointer to CPU structure in HC. */
    HCPTRTYPE(struct CPUM *) pCPUMHC;

    /** Force 32byte alignment of the next member. */
    uint32_t        padding[4 + (HC_ARCH_BITS == 32)];

    /**
     * Saved host context. Only valid while inside GC.
     * Must be aligned on 16 byte boundrary.
     */
    CPUMHOSTCTX     Host;

    /**
     * Hypervisor context.
     * Must be aligned on 16 byte boundrary.
     */
    CPUMCTX         Hyper;

    /**
     * Guest context.
     * Must be aligned on 16 byte boundrary.
     */
    CPUMCTX         Guest;


    /** Pointer to the current hypervisor core context - HCPtr. */
    HCPTRTYPE(PCPUMCTXCORE) pHyperCoreHC;
    /** Pointer to the current hypervisor core context - GCPtr. */
    GCPTRTYPE(PCPUMCTXCORE) pHyperCoreGC;

    /** Use flags.
     * These flags indicates both what is to be used and what have been used.
     */
    uint32_t        fUseFlags;

    /** Changed flags.
     * These flags indicates to REM (and others) which important guest
     * registers which has been changed since last time the flags were cleared.
     * See the CPUM_CHANGED_* defines for what we keep track of.
     */
    uint32_t        fChanged;

    /** Hidden selector registers state.
     *  Valid (hw accelerated raw mode) or not (normal raw mode)
     */
    uint32_t        fValidHiddenSelRegs;

    /** Host CPU Features - ECX */
    struct
    {
        /** edx part */
        X86CPUIDFEATEDX     edx;
        /** ecx part */
        X86CPUIDFEATECX     ecx;
    }   CPUFeatures;

    /** CR4 mask */
    struct
    {
        uint32_t AndMask;
        uint32_t OrMask;
    } CR4;

    /** Have we entered rawmode? */
    bool                    fRawEntered;
    uint8_t                 abPadding[3 + (HC_ARCH_BITS == 32) * 4];

    /** The standard set of CpuId leafs. */
    CPUMCPUID               aGuestCpuIdStd[5];
    /** The extended set of CpuId leafs. */
    CPUMCPUID               aGuestCpuIdExt[10];
    /** The default set of CpuId leafs. */
    CPUMCPUID               GuestCpuIdDef;

    /**
     * Guest context on raw mode entry.
     * This a debug feature.
     */
    CPUMCTX         GuestEntry;
} CPUM, *PCPUM;
#pragma pack()

#ifdef IN_RING3

#endif

__BEGIN_DECLS

DECLASM(int) CPUMHandleLazyFPUAsm(PCPUM pCPUM);
DECLASM(int) CPUMRestoreHostFPUStateAsm(PCPUM pCPUM);

__END_DECLS

/** @} */

#endif
