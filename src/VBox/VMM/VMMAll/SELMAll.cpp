/* $Id$ */
/** @file
 * SELM All contexts.
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
#define LOG_GROUP LOG_GROUP_SELM
#include <VBox/selm.h>
#include <VBox/stam.h>
#include <VBox/mm.h>
#include "SELMInternal.h"
#include <VBox/vm.h>
#include <VBox/x86.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <iprt/assert.h>
#include <VBox/log.h>
#include <VBox/pgm.h>



/**
 * Converts a GC selector based address to a flat address.
 *
 * No limit checks are done. Use the SELMToFlat*() or SELMValidate*() functions
 * for that.
 *
 * @returns Flat address.
 * @param   pVM     VM Handle.
 * @param   Sel     Selector part.
 * @param   Addr    Address part.
 */
static RTGCPTR selmToFlat(PVM pVM, RTSEL Sel, RTGCPTR Addr)
{
    Assert(!CPUMAreHiddenSelRegsValid(pVM));

    /** @todo check the limit. */
    VBOXDESC    Desc;
    if (!(Sel & X86_SEL_LDT))
        Desc = pVM->selm.s.CTXSUFF(paGdt)[Sel >> X86_SEL_SHIFT];
    else
    {
        /** @todo handle LDT pages not present! */
        #ifdef IN_GC
        PVBOXDESC    paLDT = (PVBOXDESC)((char *)pVM->selm.s.GCPtrLdt + pVM->selm.s.offLdtHyper);
        #else
        PVBOXDESC    paLDT = (PVBOXDESC)((char *)pVM->selm.s.HCPtrLdt + pVM->selm.s.offLdtHyper);
        #endif
        Desc = paLDT[Sel >> X86_SEL_SHIFT];
    }

    return (RTGCPTR)( (RTGCUINTPTR)Addr
                       + (   (Desc.Gen.u8BaseHigh2 << 24)
                          |  (Desc.Gen.u8BaseHigh1 << 16)
                          |   Desc.Gen.u16BaseLow));
}


/**
 * Converts a GC selector based address to a flat address.
 *
 * No limit checks are done. Use the SELMToFlat*() or SELMValidate*() functions
 * for that.
 *
 * @returns Flat address.
 * @param   pVM     VM Handle.
 * @param   Sel     Selector part.
 * @param   Addr    Address part.
 */
SELMDECL(RTGCPTR) SELMToFlat(PVM pVM, RTSEL Sel, CPUMSELREGHID *pHiddenSel, RTGCPTR Addr)
{
    if (!CPUMAreHiddenSelRegsValid(pVM))
        return selmToFlat(pVM, Sel, Addr);

    return (RTGCPTR)(pHiddenSel->u32Base + (RTGCUINTPTR)Addr);
}


/**
 * Converts a GC selector based address to a flat address.
 *
 * Some basic checking is done, but not all kinds yet.
 *
 * @returns VBox status
 * @param   pVM     VM Handle.
 * @param   Sel     Selector part.
 * @param   Addr    Address part.
 * @param   fFlags  SELMTOFLAT_FLAGS_*
 *                  GDT entires are valid.
 * @param   ppvGC   Where to store the GC flat address.
 * @param   pcb     Where to store the bytes from *ppvGC which can be accessed according to
 *                  the selector. NULL is allowed.
 */
SELMDECL(int) SELMToFlatEx(PVM pVM, RTSEL Sel, RTGCPTR Addr, unsigned fFlags, PRTGCPTR ppvGC, uint32_t *pcb)
{
    /*
     * Deal with real mode first.
     */
    if (CPUMIsGuestInRealMode(pVM)
        /** @todo  || (fFlags & SELMTOFLAT_FLAGS_V86)*/)
    {
        if (pcb)
            *pcb = 0x10000 - ((RTGCUINTPTR)Addr & 0xffff);
        if (ppvGC)
        {
            RTGCUINTPTR uFlat = ((RTGCUINTPTR)Addr & 0xffff) + (Sel << 4);
            *ppvGC = (RTGCPTR)uFlat;
        }
        return VINF_SUCCESS;
    }

    Assert(!CPUMAreHiddenSelRegsValid(pVM));

    VBOXDESC    Desc;
    if (!(Sel & X86_SEL_LDT))
    {
        if (   !(fFlags & SELMTOFLAT_FLAGS_HYPER)
            && (unsigned)(Sel & X86_SEL_MASK) >= pVM->selm.s.GuestGdtr.cbGdt)
            return VERR_INVALID_SELECTOR;
        Desc = pVM->selm.s.CTXSUFF(paGdt)[Sel >> X86_SEL_SHIFT];
    }
    else
    {
        if ((unsigned)(Sel & X86_SEL_MASK) >= pVM->selm.s.cbLdtLimit)
            return VERR_INVALID_SELECTOR;

        /** @todo handle LDT page(s) not present! */
        #ifdef IN_GC
        PVBOXDESC    paLDT = (PVBOXDESC)((char *)pVM->selm.s.GCPtrLdt + pVM->selm.s.offLdtHyper);
        #else
        PVBOXDESC    paLDT = (PVBOXDESC)((char *)pVM->selm.s.HCPtrLdt + pVM->selm.s.offLdtHyper);
        #endif
        Desc = paLDT[Sel >> X86_SEL_SHIFT];
    }

    /*
     * Check if present.
     */
    if (Desc.Gen.u1Present)
    {
        /* calc limit. */
        uint32_t    u32Limit = Desc.Gen.u4LimitHigh << 16 | Desc.Gen.u16LimitLow;
        if (Desc.Gen.u1Granularity)
            u32Limit = (u32Limit << PAGE_SHIFT) | PAGE_OFFSET_MASK;

        /* calc address assuming straight stuff. */
        RTGCPTR pvFlat = (RTGCPTR)(  (RTGCUINTPTR)Addr
                                       + (   (Desc.Gen.u8BaseHigh2 << 24)
                                          |  (Desc.Gen.u8BaseHigh1 << 16)
                                          |   Desc.Gen.u16BaseLow )
                                          );
        /*
         * Type check.
         */
        #define BOTH(a, b) ((a << 16) | b)
        switch (BOTH(Desc.Gen.u1DescType, Desc.Gen.u4Type))
        {

            /** Read only selector type. */
            case BOTH(1,X86_SEL_TYPE_RO):
            case BOTH(1,X86_SEL_TYPE_RO_ACC):
            case BOTH(1,X86_SEL_TYPE_RW):
            case BOTH(1,X86_SEL_TYPE_RW_ACC):
            case BOTH(1,X86_SEL_TYPE_EO):
            case BOTH(1,X86_SEL_TYPE_EO_ACC):
            case BOTH(1,X86_SEL_TYPE_ER):
            case BOTH(1,X86_SEL_TYPE_ER_ACC):
                if (!(fFlags & SELMTOFLAT_FLAGS_NO_PL))
                {
                    /** @todo fix this mess */
                }
                /* check limit. */
                if ((RTGCUINTPTR)Addr > u32Limit)
                    return VERR_OUT_OF_SELECTOR_BOUNDS;
                /* ok */
                if (ppvGC)
                    *ppvGC = pvFlat;
                if (pcb)
                    *pcb = u32Limit - (uint32_t)Addr + 1;
                return VINF_SUCCESS;
            case BOTH(1,X86_SEL_TYPE_EO_CONF):
            case BOTH(1,X86_SEL_TYPE_EO_CONF_ACC):
            case BOTH(1,X86_SEL_TYPE_ER_CONF):
            case BOTH(1,X86_SEL_TYPE_ER_CONF_ACC):
                if (!(fFlags & SELMTOFLAT_FLAGS_NO_PL))
                {
                    /** @todo fix this mess */
                }
                /* check limit. */
                if ((RTGCUINTPTR)Addr > u32Limit)
                    return VERR_OUT_OF_SELECTOR_BOUNDS;
                /* ok */
                if (ppvGC)
                    *ppvGC = pvFlat;
                if (pcb)
                    *pcb = u32Limit - (uint32_t)Addr + 1;
                return VINF_SUCCESS;

            case BOTH(1,X86_SEL_TYPE_RO_DOWN):
            case BOTH(1,X86_SEL_TYPE_RO_DOWN_ACC):
            case BOTH(1,X86_SEL_TYPE_RW_DOWN):
            case BOTH(1,X86_SEL_TYPE_RW_DOWN_ACC):
                if (!(fFlags & SELMTOFLAT_FLAGS_NO_PL))
                {
                    /** @todo fix this mess */
                }
                /* check limit. */
                /** @todo check the manual on this!!! */
                if ((RTGCUINTPTR)(Desc.Gen.u1Granularity ? 0xffffffff : 0xfffff) - (RTGCUINTPTR)Addr > u32Limit)
                    return VERR_OUT_OF_SELECTOR_BOUNDS;

                /* ok */
                if (ppvGC)
                    *ppvGC = pvFlat;
                if (pcb)
                    *pcb = (Desc.Gen.u1Granularity ? 0xffffffff : 0xfffff) - (RTGCUINTPTR)Addr + 1;
                return VINF_SUCCESS;

            case BOTH(0,X86_SEL_TYPE_SYS_286_TSS_AVAIL):
            case BOTH(0,X86_SEL_TYPE_SYS_LDT):
            case BOTH(0,X86_SEL_TYPE_SYS_286_TSS_BUSY):
            case BOTH(0,X86_SEL_TYPE_SYS_286_CALL_GATE):
            case BOTH(0,X86_SEL_TYPE_SYS_TASK_GATE):
            case BOTH(0,X86_SEL_TYPE_SYS_286_INT_GATE):
            case BOTH(0,X86_SEL_TYPE_SYS_286_TRAP_GATE):
            case BOTH(0,X86_SEL_TYPE_SYS_386_TSS_AVAIL):
            case BOTH(0,X86_SEL_TYPE_SYS_386_TSS_BUSY):
            case BOTH(0,X86_SEL_TYPE_SYS_386_CALL_GATE):
            case BOTH(0,X86_SEL_TYPE_SYS_386_INT_GATE):
            case BOTH(0,X86_SEL_TYPE_SYS_386_TRAP_GATE):
                if (!(fFlags & SELMTOFLAT_FLAGS_NO_PL))
                {
                    /** @todo fix this mess */
                }
                /* check limit. */
                if ((RTGCUINTPTR)Addr > u32Limit)
                    return VERR_OUT_OF_SELECTOR_BOUNDS;
                /* ok */
                if (ppvGC)
                    *ppvGC = pvFlat;
                if (pcb)
                    *pcb = 0xffffffff - (RTGCUINTPTR)pvFlat + 1; /* Depends on the type.. fixme if we care. */
                return VINF_SUCCESS;

            default:
                return VERR_INVALID_SELECTOR;

        }
        #undef BOTH
    }
    return VERR_SELECTOR_NOT_PRESENT;
}


/**
 * Validates and converts a GC selector based code address to a flat address.
 *
 * @returns Flat address.
 * @param   pVM     VM Handle.
 * @param   SelCPL  Current privilege level. Get this from SS - CS might be conforming!
 *                  A full selector can be passed, we'll only use the RPL part.
 * @param   SelCS   Selector part.
 * @param   Addr    Address part.
 * @param   ppvFlat Where to store the flat address.
 */
static int selmValidateAndConvertCSAddr(PVM pVM, RTSEL SelCPL, RTSEL SelCS, RTGCPTR Addr, PRTGCPTR ppvFlat)
{
    Assert(!CPUMAreHiddenSelRegsValid(pVM));

    /** @todo validate limit! */
    VBOXDESC    Desc;
    if (!(SelCS & X86_SEL_LDT))
        Desc = pVM->selm.s.CTXSUFF(paGdt)[SelCS >> X86_SEL_SHIFT];
    else
    {
        /** @todo handle LDT page(s) not present! */
        #ifdef IN_GC
        PVBOXDESC    paLDT = (PVBOXDESC)((char *)pVM->selm.s.GCPtrLdt + pVM->selm.s.offLdtHyper);
        #else
        PVBOXDESC    paLDT = (PVBOXDESC)((char *)pVM->selm.s.HCPtrLdt + pVM->selm.s.offLdtHyper);
        #endif
        Desc = paLDT[SelCS >> X86_SEL_SHIFT];
    }

    /*
     * Check if present.
     */
    if (Desc.Gen.u1Present)
    {
        /*
         * Type check.
         */
        if (    Desc.Gen.u1DescType == 1
            &&  (Desc.Gen.u4Type & X86_SEL_TYPE_CODE))
        {
            /*
             * Check level.
             */
            unsigned uLevel = RT_MAX(SelCPL & X86_SEL_RPL, SelCS & X86_SEL_RPL);
            if (    !(Desc.Gen.u4Type & X86_SEL_TYPE_CONF)
                ?   uLevel <= Desc.Gen.u2Dpl
                :   uLevel >= Desc.Gen.u2Dpl /* hope I got this right now... */
                    )
            {
                /*
                 * Limit check.
                 */
                uint32_t    u32Limit = Desc.Gen.u4LimitHigh << 16 | Desc.Gen.u16LimitLow;
                if (Desc.Gen.u1Granularity)
                    u32Limit = (u32Limit << PAGE_SHIFT) | PAGE_OFFSET_MASK;
                if ((RTGCUINTPTR)Addr <= u32Limit)
                {
                    if (ppvFlat)
                        *ppvFlat = (RTGCPTR)(  (RTGCUINTPTR)Addr
                                               + (   (Desc.Gen.u8BaseHigh2 << 24)
                                                  |  (Desc.Gen.u8BaseHigh1 << 16)
                                                  |   Desc.Gen.u16BaseLow)
                                                 );
                    return VINF_SUCCESS;
                }
                return VERR_OUT_OF_SELECTOR_BOUNDS;
            }
            return VERR_INVALID_RPL;
        }
        return VERR_NOT_CODE_SELECTOR;
    }
    return VERR_SELECTOR_NOT_PRESENT;
}


/**
 * Validates and converts a GC selector based code address to a flat address.
 *
 * @returns Flat address.
 * @param   pVM          VM Handle.
 * @param   SelCPL       Current privilege level. Get this from SS - CS might be conforming!
 *                       A full selector can be passed, we'll only use the RPL part.
 * @param   SelCS        Selector part.
 * @param   pHiddenSel   The hidden CS selector register.
 * @param   Addr         Address part.
 * @param   ppvFlat      Where to store the flat address.
 */
SELMDECL(int) SELMValidateAndConvertCSAddr(PVM pVM, RTSEL SelCPL, RTSEL SelCS, CPUMSELREGHID *pHiddenCSSel, RTGCPTR Addr, PRTGCPTR ppvFlat)
{
    if (!CPUMAreHiddenSelRegsValid(pVM))
        return selmValidateAndConvertCSAddr(pVM, SelCPL, SelCS, Addr, ppvFlat);

    /*
     * Check if present.
     */
    if (pHiddenCSSel->Attr.n.u1Present)
    {
        /*
         * Type check.
         */
        if (     pHiddenCSSel->Attr.n.u1DescType == 1
            &&  (pHiddenCSSel->Attr.n.u4Type & X86_SEL_TYPE_CODE))
        {
            /*
             * Check level.
             */
            unsigned uLevel = RT_MAX(SelCPL & X86_SEL_RPL, SelCS & X86_SEL_RPL);
            if (    !(pHiddenCSSel->Attr.n.u4Type & X86_SEL_TYPE_CONF)
                ?   uLevel <= pHiddenCSSel->Attr.n.u2Dpl
                :   uLevel >= pHiddenCSSel->Attr.n.u2Dpl /* hope I got this right now... */
                    )
            {
                /*
                 * Limit check.
                 */
                uint32_t    u32Limit = pHiddenCSSel->u32Limit;
                /** @todo correct with hidden limit value?? */
                if (pHiddenCSSel->Attr.n.u1Granularity)
                    u32Limit = (u32Limit << PAGE_SHIFT) | PAGE_OFFSET_MASK;
                if ((RTGCUINTPTR)Addr <= u32Limit)
                {
                    if (ppvFlat)
                        *ppvFlat = (RTGCPTR)(  (RTGCUINTPTR)Addr + pHiddenCSSel->u32Base );

                    return VINF_SUCCESS;
                }
                return VERR_OUT_OF_SELECTOR_BOUNDS;
            }
            return VERR_INVALID_RPL;
        }
        return VERR_NOT_CODE_SELECTOR;
    }
    return VERR_SELECTOR_NOT_PRESENT;
}


/**
 * Checks if a selector is 32-bit or 16-bit.
 *
 * @returns True if it is 32-bit.
 * @returns False if it is 16-bit.
 * @param   pVM     VM Handle.
 * @param   Sel     The selector.
 */
static bool selmIsSelector32Bit(PVM pVM, RTSEL Sel)
{
    Assert(!CPUMAreHiddenSelRegsValid(pVM));

    /** @todo validate limit! */
    VBOXDESC Desc;
    if (!(Sel & X86_SEL_LDT))
        Desc = pVM->selm.s.CTXSUFF(paGdt)[Sel >> X86_SEL_SHIFT];
    else
    {
        /** @todo handle LDT page(s) not present! */
        PVBOXDESC   paLDT = (PVBOXDESC)((char *)pVM->selm.s.CTXMID(,PtrLdt) + pVM->selm.s.offLdtHyper);
        Desc = paLDT[Sel >> X86_SEL_SHIFT];
    }
    return Desc.Gen.u1DefBig;
}


/**
 * Checks if a selector is 32-bit or 16-bit.
 *
 * @returns True if it is 32-bit.
 * @returns False if it is 16-bit.
 * @param   pVM        VM Handle.
 * @param   Sel        The selector.
 * @param   pHiddenSel The hidden selector register.
 */
SELMDECL(bool) SELMIsSelector32Bit(PVM pVM, RTSEL Sel, CPUMSELREGHID *pHiddenSel)
{
    if (!CPUMAreHiddenSelRegsValid(pVM))
        return selmIsSelector32Bit(pVM, Sel);

    return pHiddenSel->Attr.n.u1DefBig;
}


/**
 * Returns Hypervisor's Trap 08 (\#DF) selector.
 *
 * @returns Hypervisor's Trap 08 (\#DF) selector.
 * @param   pVM     VM Handle.
 */
SELMDECL(RTSEL) SELMGetTrap8Selector(PVM pVM)
{
    return pVM->selm.s.SelTSSTrap08;
}


/**
 * Sets EIP of Hypervisor's Trap 08 (\#DF) TSS.
 *
 * @param   pVM     VM Handle.
 * @param   u32EIP  EIP of Trap 08 handler.
 */
SELMDECL(void) SELMSetTrap8EIP(PVM pVM, uint32_t u32EIP)
{
    pVM->selm.s.TssTrap08.eip = u32EIP;
}


/**
 * Sets ss:esp for ring1 in main Hypervisor's TSS.
 *
 * @param   pVM     VM Handle.
 * @param   ss      Ring1 SS register value.
 * @param   esp     Ring1 ESP register value.
 */
SELMDECL(void) SELMSetRing1Stack(PVM pVM, uint32_t ss, uint32_t esp)
{
    pVM->selm.s.Tss.ss1  = ss;
    pVM->selm.s.Tss.esp1 = esp;
}


/**
 * Gets ss:esp for ring1 in main Hypervisor's TSS.
 *
 * @returns VBox status code.
 * @param   pVM     VM Handle.
 * @param   pSS     Ring1 SS register value.
 * @param   pEsp    Ring1 ESP register value.
 */
SELMDECL(int) SELMGetRing1Stack(PVM pVM, uint32_t *pSS, uint32_t *pEsp)
{
    if (pVM->selm.s.fSyncTSSRing0Stack)
    {
        GCPTRTYPE(uint8_t *)GCPtrTss = (GCPTRTYPE(uint8_t *))pVM->selm.s.GCPtrGuestTss;
        int     rc;
        VBOXTSS tss;

        Assert(pVM->selm.s.GCPtrGuestTss && pVM->selm.s.cbMonitoredGuestTss);

#ifdef IN_GC
        bool    fTriedAlready = false;

tryagain:
        rc  = MMGCRamRead(pVM, &tss.ss0,  GCPtrTss + RT_OFFSETOF(VBOXTSS, ss0), sizeof(tss.ss0));
        rc |= MMGCRamRead(pVM, &tss.esp0, GCPtrTss + RT_OFFSETOF(VBOXTSS, esp0), sizeof(tss.esp0));
  #ifdef DEBUG
        rc |= MMGCRamRead(pVM, &tss.offIoBitmap, GCPtrTss + RT_OFFSETOF(VBOXTSS, offIoBitmap), sizeof(tss.offIoBitmap));
  #endif

        if (VBOX_FAILURE(rc))
        {
            if (!fTriedAlready)
            {
                /* Shadow page might be out of sync. Sync and try again */
                /** @todo might cross page boundary */
                fTriedAlready = true;
                rc = PGMPrefetchPage(pVM, GCPtrTss); 
                if (rc != VINF_SUCCESS)
                    return rc;

                goto tryagain;
            }
            else
            {
                AssertMsgFailed(("Unable to read TSS structure at %08X\n", GCPtrTss));
                return rc;
            }
        }
#else /* IN_GC */
        /* Reading too much. Could be cheaper than two seperate calls though. */
        rc = PGMPhysReadGCPtr(pVM, &tss, GCPtrTss, sizeof(VBOXTSS));
        if (VBOX_FAILURE(rc))
        {
            AssertReleaseMsgFailed(("Unable to read TSS structure at %08X\n", GCPtrTss));
            return rc;
        }
#endif /* IN_GC */

#ifdef DEBUG
        uint32_t ssr0  = pVM->selm.s.Tss.ss1;
        uint32_t espr0 = pVM->selm.s.Tss.esp1;
        ssr0 &= ~1;

        if (ssr0 != tss.ss0 || espr0 != tss.esp0)
            Log(("SELMGetRing1Stack: Updating TSS ring 0 stack to %04X:%08X\n", tss.ss0, tss.esp0));

        Log(("offIoBitmap=%#x\n", tss.offIoBitmap));
#endif
        /* Update our TSS structure for the guest's ring 1 stack */
        SELMSetRing1Stack(pVM, tss.ss0 | 1, tss.esp0);
        pVM->selm.s.fSyncTSSRing0Stack = false;
    }

    *pSS  = pVM->selm.s.Tss.ss1;
    *pEsp = pVM->selm.s.Tss.esp1;

    return VINF_SUCCESS;
}


/**
 * Returns Guest TSS pointer
 *
 * @param   pVM     VM Handle.
 */
SELMDECL(RTGCPTR) SELMGetGuestTSS(PVM pVM)
{
    return (RTGCPTR)pVM->selm.s.GCPtrGuestTss;
}


/**
 * Validates a CS selector.
 *
 * @returns VBox status code.
 * @param   pSelInfo    Pointer to the selector information for the CS selector.
 * @param   SelCPL      The selector defining the CPL (SS).
 */
SELMDECL(int) SELMSelInfoValidateCS(PCSELMSELINFO pSelInfo, RTSEL SelCPL)
{
    /*
     * Check if present.
     */
    if (pSelInfo->Raw.Gen.u1Present)
    {
        /*
         * Type check.
         */
        if (    pSelInfo->Raw.Gen.u1DescType == 1
            &&  (pSelInfo->Raw.Gen.u4Type & X86_SEL_TYPE_CODE))
        {
            /*
             * Check level.
             */
            unsigned uLevel = RT_MAX(SelCPL & X86_SEL_RPL, pSelInfo->Sel & X86_SEL_RPL);
            if (    !(pSelInfo->Raw.Gen.u4Type & X86_SEL_TYPE_CONF)
                ?   uLevel <= pSelInfo->Raw.Gen.u2Dpl
                :   uLevel >= pSelInfo->Raw.Gen.u2Dpl /* hope I got this right now... */
                    )
                return VINF_SUCCESS;
            return VERR_INVALID_RPL;
        }
        return VERR_NOT_CODE_SELECTOR;
    }
    return VERR_SELECTOR_NOT_PRESENT;
}


/**
 * Gets the hypervisor code selector (CS).
 * @returns CS selector.
 * @param   pVM     The VM handle.
 */
SELMDECL(RTSEL) SELMGetHyperCS(PVM pVM)
{
    return pVM->selm.s.SelCS;
}


/**
 * Gets the 64-mode hypervisor code selector (CS64).
 * @returns CS selector.
 * @param   pVM     The VM handle.
 */
SELMDECL(RTSEL) SELMGetHyperCS64(PVM pVM)
{
    return pVM->selm.s.SelCS64;
}


/**
 * Gets the hypervisor data selector (DS).
 * @returns DS selector.
 * @param   pVM     The VM handle.
 */
SELMDECL(RTSEL) SELMGetHyperDS(PVM pVM)
{
    return pVM->selm.s.SelDS;
}


/**
 * Gets the hypervisor TSS selector.
 * @returns TSS selector.
 * @param   pVM     The VM handle.
 */
SELMDECL(RTSEL) SELMGetHyperTSS(PVM pVM)
{
    return pVM->selm.s.SelTSS;
}


/**
 * Gets the hypervisor TSS Trap 8 selector.
 * @returns TSS Trap 8 selector.
 * @param   pVM     The VM handle.
 */
SELMDECL(RTSEL) SELMGetHyperTSSTrap08(PVM pVM)
{
    return pVM->selm.s.SelTSSTrap08;
}


/**
 * Gets the address for the hypervisor GDT.
 *
 * @returns The GDT address.
 * @param   pVM     The VM handle.
 * @remark  This is intended only for very special use, like in the world
 *          switchers. Don't exploit this API!
 */
SELMDECL(RTGCPTR) SELMGetHyperGDT(PVM pVM)
{
    /*
     * Always convert this from the HC pointer since. We're can be
     * called before the first relocation and have to work correctly
     * without having dependencies on the relocation order.
     */
    return MMHyperHC2GC(pVM, pVM->selm.s.paGdtHC);
}


/**
 * Gets info about the current TSS.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if we've got a TSS loaded.
 * @retval  VERR_SELM_NO_TSS if we haven't got a TSS (rather unlikely).
 *
 * @param   pVM                 The VM handle.
 * @param   pGCPtrTss           Where to store the TSS address.
 * @param   pcbTss              Where to store the TSS size limit.
 * @param   pfCanHaveIOBitmap   Where to store the can-have-I/O-bitmap indicator. (optional)
 */
SELMDECL(int) SELMGetTSSInfo(PVM pVM, PRTGCUINTPTR pGCPtrTss, PRTGCUINTPTR pcbTss, bool *pfCanHaveIOBitmap)
{
    if (!CPUMAreHiddenSelRegsValid(pVM))
    {
        /*
         * Do we have a valid TSS?
         */
        if (    pVM->selm.s.GCSelTss == (RTSEL)~0
            || !pVM->selm.s.fGuestTss32Bit)
            return VERR_SELM_NO_TSS;

        /*
         * Fill in return values.
         */
        *pGCPtrTss = (RTGCUINTPTR)pVM->selm.s.GCPtrGuestTss;
        *pcbTss = pVM->selm.s.cbGuestTss;
        if (pfCanHaveIOBitmap)
            *pfCanHaveIOBitmap = pVM->selm.s.fGuestTss32Bit;
    }
    else
    {
        CPUMSELREGHID *pHiddenTRReg;

        pHiddenTRReg = CPUMGetGuestTRHid(pVM);

        *pGCPtrTss = pHiddenTRReg->u32Base;
        *pcbTss    = pHiddenTRReg->u32Limit;

        if (pfCanHaveIOBitmap)
            *pfCanHaveIOBitmap =  pHiddenTRReg->Attr.n.u4Type == X86_SEL_TYPE_SYS_386_TSS_AVAIL
                               || pHiddenTRReg->Attr.n.u4Type == X86_SEL_TYPE_SYS_386_TSS_BUSY;
    }
    return VINF_SUCCESS;
}
