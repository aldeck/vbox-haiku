/* $Id$ */
/** @file
 * InnoTek Portable Runtime Testcase - inline assembly.
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
#include <iprt/asm.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/runtime.h>
#include <iprt/param.h>


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Global error count. */
static unsigned g_cErrors;


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#define CHECKVAL(val, expect, fmt) \
    do \
    { \
        if ((val) != (expect)) \
        { \
            g_cErrors++; \
            RTPrintf("%s, %d: " #val ": expected " fmt " got " fmt "\n", __FUNCTION__, __LINE__, (expect), (val)); \
        } \
    } while (0)

#define CHECKOP(op, expect, fmt, type) \
    do \
    { \
        type val = op; \
        if (val != (type)(expect)) \
        { \
            g_cErrors++; \
            RTPrintf("%s, %d: " #op ": expected " fmt " got " fmt "\n", __FUNCTION__, __LINE__, (type)(expect), val); \
        } \
    } while (0)


#ifndef PIC
const char *getCacheAss(unsigned u)
{
    if (u == 0)
        return "res0  ";
    if (u == 1)
        return "direct";
    if (u >= 256)
        return "???";

    char *pszRet;
    RTStrAPrintf(&pszRet, "%d way", u);     /* intentional leak! */
    return pszRet;
}


const char *getL2CacheAss(unsigned u)
{
    switch (u)
    {
        case 0:  return "off   ";
        case 1:  return "direct";
        case 2:  return "2 way ";
        case 3:  return "res3  ";
        case 4:  return "4 way ";
        case 5:  return "res5  ";
        case 6:  return "8 way ";
        case 7:  return "res7  ";
        case 8:  return "16 way";
        case 9:  return "res9  ";
        case 10: return "res10 ";
        case 11: return "res11 ";
        case 12: return "res12 ";
        case 13: return "res13 ";
        case 14: return "res14 ";
        case 15: return "fully ";
        default:
            return "????";
    }
}


/**
 * Test and dump all possible info from the CPUID instruction.
 *
 * @remark  Bits shared with the libc cpuid.c program. This all written by me, so no worries.
 * @todo transform the dumping into a generic runtime function. We'll need it for logging!
 */
void tstASMCpuId(void)
{
    unsigned    iBit;
    uint32_t    uEAX, uEBX, uECX, uEDX;
    if (!ASMHasCpuId())
    {
        RTPrintf("tstInlineAsm: warning! CPU doesn't support CPUID\n");
        return;
    }

    /*
     * Try the 0 function and use that for checking the ASMCpuId_* variants.
     */
    ASMCpuId(0, &uEAX, &uEBX, &uECX, &uEDX);

    uint32_t u32 = ASMCpuId_ECX(0);
    CHECKVAL(u32, uECX, "%x");

    u32 = ASMCpuId_EDX(0);
    CHECKVAL(u32, uEDX, "%x");

    uint32_t uECX2 = uECX - 1;
    uint32_t uEDX2 = uEDX - 1;
    ASMCpuId_ECX_EDX(0, &uECX2, &uEDX2);

    CHECKVAL(uECX2, uECX, "%x");
    CHECKVAL(uEDX2, uEDX, "%x");

    /*
     * Done testing, dump the information.
     */
    RTPrintf("tstInlineAsm: CPUID Dump\n");
    ASMCpuId(0, &uEAX, &uEBX, &uECX, &uEDX);
    const uint32_t cFunctions = uEAX;

    /* raw dump */
    RTPrintf("\n"
             "         RAW Standard CPUIDs\n"
             "Function  eax      ebx      ecx      edx\n");
    for (unsigned iStd = 0; iStd <= cFunctions + 3; iStd++)
    {
        ASMCpuId(iStd, &uEAX, &uEBX, &uECX, &uEDX);
        RTPrintf("%08x  %08x %08x %08x %08x%s\n",
                 iStd, uEAX, uEBX, uECX, uEDX, iStd <= cFunctions ? "" : "*");
    }

    /*
     * Understandable output
     */
    ASMCpuId(0, &uEAX, &uEBX, &uECX, &uEDX);
    RTPrintf("Name:                            %.04s%.04s%.04s\n"
             "Support:                         0-%u\n",
             &uEBX, &uEDX, &uECX, uEAX);

    /*
     * Get Features.
     */
    if (cFunctions >= 1)
    {
        ASMCpuId(1, &uEAX, &uEBX, &uECX, &uEDX);
        RTPrintf("Family:                          %d  \tExtended: %d \tEffectiv: %d\n"
                 "Model:                           %d  \tExtended: %d \tEffectiv: %d\n"
                 "Stepping:                        %d\n"
                 "APIC ID:                         %#04x\n"
                 "Logical CPUs:                    %d\n"
                 "CLFLUSH Size:                    %d\n"
                 "Brand ID:                        %#04x\n",
                 (uEAX >> 8) & 0xf, (uEAX >> 20) & 0x7f, ((uEAX >> 8) & 0xf) + (((uEAX >> 8) & 0xf) == 0xf ? (uEAX >> 20) & 0x7f : 0),
                 (uEAX >> 4) & 0xf, (uEAX >> 16) & 0x0f, ((uEAX >> 4) & 0xf) | (((uEAX >> 4) & 0xf) == 0xf ? (uEAX >> 16) & 0x0f : 0),
                 (uEAX >> 0) & 0xf,
                 (uEBX >> 24) & 0xff,
                 (uEBX >> 16) & 0xff,
                 (uEBX >>  8) & 0xff,
                 (uEBX >>  0) & 0xff);

        RTPrintf("Features EDX:                   ");
        if (uEDX & BIT(0))   RTPrintf(" FPU");
        if (uEDX & BIT(1))   RTPrintf(" VME");
        if (uEDX & BIT(2))   RTPrintf(" DE");
        if (uEDX & BIT(3))   RTPrintf(" PSE");
        if (uEDX & BIT(4))   RTPrintf(" TSC");
        if (uEDX & BIT(5))   RTPrintf(" MSR");
        if (uEDX & BIT(6))   RTPrintf(" PAE");
        if (uEDX & BIT(7))   RTPrintf(" MCE");
        if (uEDX & BIT(8))   RTPrintf(" CX8");
        if (uEDX & BIT(9))   RTPrintf(" APIC");
        if (uEDX & BIT(10))  RTPrintf(" 10");
        if (uEDX & BIT(11))  RTPrintf(" SEP");
        if (uEDX & BIT(12))  RTPrintf(" MTRR");
        if (uEDX & BIT(13))  RTPrintf(" PGE");
        if (uEDX & BIT(14))  RTPrintf(" MCA");
        if (uEDX & BIT(15))  RTPrintf(" CMOV");
        if (uEDX & BIT(16))  RTPrintf(" PAT");
        if (uEDX & BIT(17))  RTPrintf(" PSE36");
        if (uEDX & BIT(18))  RTPrintf(" PSN");
        if (uEDX & BIT(19))  RTPrintf(" CLFSH");
        if (uEDX & BIT(20))  RTPrintf(" 20");
        if (uEDX & BIT(21))  RTPrintf(" DS");
        if (uEDX & BIT(22))  RTPrintf(" ACPI");
        if (uEDX & BIT(23))  RTPrintf(" MMX");
        if (uEDX & BIT(24))  RTPrintf(" FXSR");
        if (uEDX & BIT(25))  RTPrintf(" SSE");
        if (uEDX & BIT(26))  RTPrintf(" SSE2");
        if (uEDX & BIT(27))  RTPrintf(" SS");
        if (uEDX & BIT(28))  RTPrintf(" HTT");
        if (uEDX & BIT(29))  RTPrintf(" 29");
        if (uEDX & BIT(30))  RTPrintf(" 30");
        if (uEDX & BIT(31))  RTPrintf(" 31");
        RTPrintf("\n");

        /** @todo check intel docs. */
        RTPrintf("Features ECX:                   ");
        if (uECX & BIT(0))   RTPrintf(" SSE3");
        for (iBit = 1; iBit < 13; iBit++)
            if (uECX & BIT(iBit))
                RTPrintf(" %d", iBit);
        if (uECX & BIT(13))  RTPrintf(" CX16");
        for (iBit = 14; iBit < 32; iBit++)
            if (uECX & BIT(iBit))
                RTPrintf(" %d", iBit);
        RTPrintf("\n");
    }

    /*
     * Extended.
     * Implemented after AMD specs.
     */
    /** @todo check out the intel specs. */
    ASMCpuId(0x80000000, &uEAX, &uEBX, &uECX, &uEDX);
    if (!uEAX && !uEBX && !uECX && !uEDX)
    {
        RTPrintf("No extended CPUID info? Check the manual on how to detect this...\n");
        return;
    }
    const uint32_t cExtFunctions = uEAX | 0x80000000;

    /* raw dump */
    RTPrintf("\n"
             "         RAW Extended CPUIDs\n"
             "Function  eax      ebx      ecx      edx\n");
    for (unsigned iExt = 0x80000000; iExt <= cExtFunctions + 3; iExt++)
    {
        ASMCpuId(iExt, &uEAX, &uEBX, &uECX, &uEDX);
        RTPrintf("%08x  %08x %08x %08x %08x%s\n",
                 iExt, uEAX, uEBX, uECX, uEDX, iExt <= cExtFunctions ? "" : "*");
    }

    /*
     * Understandable output
     */
    ASMCpuId(0x80000000, &uEAX, &uEBX, &uECX, &uEDX);
    RTPrintf("Ext Name:                        %.4s%.4s%.4s\n"
             "Ext Supports:                    0x80000000-%#010x\n",
             &uEBX, &uEDX, &uECX, uEAX);

    if (cExtFunctions >= 0x80000001)
    {
        ASMCpuId(0x80000001, &uEAX, &uEBX, &uECX, &uEDX);
        RTPrintf("Family:                          %d  \tExtended: %d \tEffectiv: %d\n"
                 "Model:                           %d  \tExtended: %d \tEffectiv: %d\n"
                 "Stepping:                        %d\n"
                 "Brand ID:                        %#05x\n",
                 (uEAX >> 8) & 0xf, (uEAX >> 20) & 0x7f, ((uEAX >> 8) & 0xf) + (((uEAX >> 8) & 0xf) == 0xf ? (uEAX >> 20) & 0x7f : 0),
                 (uEAX >> 4) & 0xf, (uEAX >> 16) & 0x0f, ((uEAX >> 4) & 0xf) | (((uEAX >> 4) & 0xf) == 0xf ? (uEAX >> 16) & 0x0f : 0),
                 (uEAX >> 0) & 0xf,
                 uEBX & 0xfff);

        RTPrintf("Features EDX:                   ");
        if (uEDX & BIT(0))   RTPrintf(" FPU");
        if (uEDX & BIT(1))   RTPrintf(" VME");
        if (uEDX & BIT(2))   RTPrintf(" DE");
        if (uEDX & BIT(3))   RTPrintf(" PSE");
        if (uEDX & BIT(4))   RTPrintf(" TSC");
        if (uEDX & BIT(5))   RTPrintf(" MSR");
        if (uEDX & BIT(6))   RTPrintf(" PAE");
        if (uEDX & BIT(7))   RTPrintf(" MCE");
        if (uEDX & BIT(8))   RTPrintf(" CX8");
        if (uEDX & BIT(9))   RTPrintf(" APIC");
        if (uEDX & BIT(10))  RTPrintf(" 10");
        if (uEDX & BIT(11))  RTPrintf(" SCR");
        if (uEDX & BIT(12))  RTPrintf(" MTRR");
        if (uEDX & BIT(13))  RTPrintf(" PGE");
        if (uEDX & BIT(14))  RTPrintf(" MCA");
        if (uEDX & BIT(15))  RTPrintf(" CMOV");
        if (uEDX & BIT(16))  RTPrintf(" PAT");
        if (uEDX & BIT(17))  RTPrintf(" PSE36");
        if (uEDX & BIT(18))  RTPrintf(" 18");
        if (uEDX & BIT(19))  RTPrintf(" 19");
        if (uEDX & BIT(20))  RTPrintf(" NX");
        if (uEDX & BIT(21))  RTPrintf(" 21");
        if (uEDX & BIT(22))  RTPrintf(" ExtMMX");
        if (uEDX & BIT(23))  RTPrintf(" MMX");
        if (uEDX & BIT(24))  RTPrintf(" FXSR");
        if (uEDX & BIT(25))  RTPrintf(" FastFXSR");
        if (uEDX & BIT(26))  RTPrintf(" 26");
        if (uEDX & BIT(27))  RTPrintf(" RDTSCP");
        if (uEDX & BIT(28))  RTPrintf(" 29");
        if (uEDX & BIT(29))  RTPrintf(" LongMode");
        if (uEDX & BIT(30))  RTPrintf(" Ext3DNow");
        if (uEDX & BIT(31))  RTPrintf(" 3DNow");
        RTPrintf("\n");

        /** @todo Check intel docs. */
        RTPrintf("Features ECX:                   ");
        if (uECX & BIT(0))   RTPrintf(" LAHF/SAHF");
        if (uECX & BIT(1))   RTPrintf(" CMPL");
        if (uECX & BIT(2))   RTPrintf(" 2");
        if (uECX & BIT(3))   RTPrintf(" 3");
        if (uECX & BIT(4))   RTPrintf(" CR8L");
        for (iBit = 5; iBit < 32; iBit++)
            if (uECX & BIT(iBit))
                RTPrintf(" %d", iBit);
        RTPrintf("\n");
    }

     char szString[4*4*3+1] = {0};
     if (cExtFunctions >= 0x80000002)
         ASMCpuId(0x80000002, &szString[0  + 0], &szString[0  + 4], &szString[0  + 8], &szString[0  + 12]);
     if (cExtFunctions >= 0x80000003)
         ASMCpuId(0x80000003, &szString[16 + 0], &szString[16 + 4], &szString[16 + 8], &szString[16 + 12]);
     if (cExtFunctions >= 0x80000004)
         ASMCpuId(0x80000004, &szString[32 + 0], &szString[32 + 4], &szString[32 + 8], &szString[32 + 12]);
     if (cExtFunctions >= 0x80000002)
         RTPrintf("Full Name:                       %s\n", szString);

     if (cExtFunctions >= 0x80000005)
     {
         ASMCpuId(0x80000005, &uEAX, &uEBX, &uECX, &uEDX);
         RTPrintf("TLB 2/4M Instr/Uni:              %s %3d entries\n"
                  "TLB 2/4M Data:                   %s %3d entries\n",
                  getCacheAss((uEAX >>  8) & 0xff), (uEAX >>  0) & 0xff,
                  getCacheAss((uEAX >> 24) & 0xff), (uEAX >> 16) & 0xff);
         RTPrintf("TLB 4K Instr/Uni:                %s %3d entries\n"
                  "TLB 4K Data:                     %s %3d entries\n",
                  getCacheAss((uEBX >>  8) & 0xff), (uEBX >>  0) & 0xff,
                  getCacheAss((uEBX >> 24) & 0xff), (uEBX >> 16) & 0xff);
         RTPrintf("L1 Instr Cache Line Size:        %d bytes\n"
                  "L1 Instr Cache Lines Per Tag:    %d\n"
                  "L1 Instr Cache Associativity:    %s\n"
                  "L1 Instr Cache Size:             %d KB\n",
                  (uEDX >> 0) & 0xff,
                  (uEDX >> 8) & 0xff,
                  getCacheAss((uEDX >> 16) & 0xff),
                  (uEDX >> 24) & 0xff);
         RTPrintf("L1 Data Cache Line Size:         %d bytes\n"
                  "L1 Data Cache Lines Per Tag:     %d\n"
                  "L1 Data Cache Associativity:     %s\n"
                  "L1 Data Cache Size:              %d KB\n",
                  (uECX >> 0) & 0xff,
                  (uECX >> 8) & 0xff,
                  getCacheAss((uECX >> 16) & 0xff),
                  (uECX >> 24) & 0xff);
     }

     if (cExtFunctions >= 0x80000006)
     {
         ASMCpuId(0x80000006, &uEAX, &uEBX, &uECX, &uEDX);
         RTPrintf("L2 TLB 2/4M Instr/Uni:           %s %4d entries\n"
                  "L2 TLB 2/4M Data:                %s %4d entries\n",
                  getL2CacheAss((uEAX >> 12) & 0xf),  (uEAX >>  0) & 0xfff,
                  getL2CacheAss((uEAX >> 28) & 0xf),  (uEAX >> 16) & 0xfff);
         RTPrintf("L2 TLB 4K Instr/Uni:             %s %4d entries\n"
                  "L2 TLB 4K Data:                  %s %4d entries\n",
                  getL2CacheAss((uEBX >> 12) & 0xf),  (uEBX >>  0) & 0xfff,
                  getL2CacheAss((uEBX >> 28) & 0xf),  (uEBX >> 16) & 0xfff);
         RTPrintf("L2 Cache Line Size:              %d bytes\n"
                  "L2 Cache Lines Per Tag:          %d\n"
                  "L2 Cache Associativity:          %s\n"
                  "L2 Cache Size:                   %d KB\n",
                  (uEDX >> 0) & 0xff,
                  (uEDX >> 8) & 0xf,
                  getL2CacheAss((uEDX >> 12) & 0xf),
                  (uEDX >> 16) & 0xffff);
     }

     if (cExtFunctions >= 0x80000007)
     {
         ASMCpuId(0x80000007, &uEAX, &uEBX, &uECX, &uEDX);
         RTPrintf("APM Features:                   ");
         if (uEDX & BIT(0))   RTPrintf(" TS");
         if (uEDX & BIT(1))   RTPrintf(" FID");
         if (uEDX & BIT(2))   RTPrintf(" VID");
         if (uEDX & BIT(3))   RTPrintf(" TTP");
         if (uEDX & BIT(4))   RTPrintf(" TM");
         if (uEDX & BIT(5))   RTPrintf(" STC");
         for (iBit = 6; iBit < 32; iBit++)
             if (uEDX & BIT(iBit))
                 RTPrintf(" %d", iBit);
         RTPrintf("\n");
     }

     if (cExtFunctions >= 0x80000008)
     {
         ASMCpuId(0x80000008, &uEAX, &uEBX, &uECX, &uEDX);
         RTPrintf("Physical Address Width:          %d bits\n"
                  "Virtual Address Width:           %d bits\n",
                  (uEAX >> 0) & 0xff,
                  (uEAX >> 8) & 0xff);
         RTPrintf("Physical Core Count:             %d\n",
                  (uECX >> 0) & 0xff);
     }
}
#endif /* !PIC */


static void tstASMAtomicXchgU8(void)
{
    struct
    {
        uint8_t u8Dummy0;
        uint8_t u8;
        uint8_t u8Dummy1;
    } s;

    s.u8 = 0;
    s.u8Dummy0 = s.u8Dummy1 = 0x42;
    CHECKOP(ASMAtomicXchgU8(&s.u8, 1), 0, "%#x", uint8_t);
    CHECKVAL(s.u8, 1, "%#x");

    CHECKOP(ASMAtomicXchgU8(&s.u8, 0), 1, "%#x", uint8_t);
    CHECKVAL(s.u8, 0, "%#x");

    CHECKOP(ASMAtomicXchgU8(&s.u8, 0xff), 0, "%#x", uint8_t);
    CHECKVAL(s.u8, 0xff, "%#x");

    CHECKOP(ASMAtomicXchgU8(&s.u8, 0x87), 0xffff, "%#x", uint8_t);
    CHECKVAL(s.u8, 0x87, "%#x");
    CHECKVAL(s.u8Dummy0, 0x42, "%#x");
    CHECKVAL(s.u8Dummy1, 0x42, "%#x");
}


static void tstASMAtomicXchgU16(void)
{
    struct
    {
        uint16_t u16Dummy0;
        uint16_t u16;
        uint16_t u16Dummy1;
    } s;

    s.u16 = 0;
    s.u16Dummy0 = s.u16Dummy1 = 0x1234;
    CHECKOP(ASMAtomicXchgU16(&s.u16, 1), 0, "%#x", uint16_t);
    CHECKVAL(s.u16, 1, "%#x");

    CHECKOP(ASMAtomicXchgU16(&s.u16, 0), 1, "%#x", uint16_t);
    CHECKVAL(s.u16, 0, "%#x");

    CHECKOP(ASMAtomicXchgU16(&s.u16, 0xffff), 0, "%#x", uint16_t);
    CHECKVAL(s.u16, 0xffff, "%#x");

    CHECKOP(ASMAtomicXchgU16(&s.u16, 0x8765), 0xffff, "%#x", uint16_t);
    CHECKVAL(s.u16, 0x8765, "%#x");
    CHECKVAL(s.u16Dummy0, 0x1234, "%#x");
    CHECKVAL(s.u16Dummy1, 0x1234, "%#x");
}


static void tstASMAtomicXchgU32(void)
{
    struct
    {
        uint32_t u32Dummy0;
        uint32_t u32;
        uint32_t u32Dummy1;
    } s;

    s.u32 = 0;
    s.u32Dummy0 = s.u32Dummy1 = 0x11223344;

    CHECKOP(ASMAtomicXchgU32(&s.u32, 1), 0, "%#x", uint32_t);
    CHECKVAL(s.u32, 1, "%#x");

    CHECKOP(ASMAtomicXchgU32(&s.u32, 0), 1, "%#x", uint32_t);
    CHECKVAL(s.u32, 0, "%#x");

    CHECKOP(ASMAtomicXchgU32(&s.u32, ~0U), 0, "%#x", uint32_t);
    CHECKVAL(s.u32, ~0U, "%#x");

    CHECKOP(ASMAtomicXchgU32(&s.u32, 0x87654321), ~0U, "%#x", uint32_t);
    CHECKVAL(s.u32, 0x87654321, "%#x");

    CHECKVAL(s.u32Dummy0, 0x11223344, "%#x");
    CHECKVAL(s.u32Dummy1, 0x11223344, "%#x");
}


static void tstASMAtomicXchgU64(void)
{
    struct
    {
        uint64_t u64Dummy0;
        uint64_t u64;
        uint64_t u64Dummy1;
    } s;

    s.u64 = 0;
    s.u64Dummy0 = s.u64Dummy1 = 0x1122334455667788ULL;

    CHECKOP(ASMAtomicXchgU64(&s.u64, 1), 0ULL, "%#llx", uint64_t);
    CHECKVAL(s.u64, 1ULL, "%#llx");

    CHECKOP(ASMAtomicXchgU64(&s.u64, 0), 1ULL, "%#llx", uint64_t);
    CHECKVAL(s.u64, 0ULL, "%#llx");

    CHECKOP(ASMAtomicXchgU64(&s.u64, ~0ULL), 0ULL, "%#llx", uint64_t);
    CHECKVAL(s.u64, ~0ULL, "%#llx");

    CHECKOP(ASMAtomicXchgU64(&s.u64, 0xfedcba0987654321ULL),  ~0ULL, "%#llx", uint64_t);
    CHECKVAL(s.u64, 0xfedcba0987654321ULL, "%#llx");

    CHECKVAL(s.u64Dummy0, 0x1122334455667788ULL, "%#x");
    CHECKVAL(s.u64Dummy1, 0x1122334455667788ULL, "%#x");
}


#ifdef __amd64__
static void tstASMAtomicXchgU128(void)
{
    struct
    {
        RTUINT128U  u128Dummy0;
        RTUINT128U  u128;
        RTUINT128U  u128Dummy1;
    } s;
    RTUINT128U u128Ret;
    RTUINT128U u128Arg;


    s.u128Dummy0.s.Lo = s.u128Dummy0.s.Hi = 0x1122334455667788;
    s.u128.s.Lo = 0;
    s.u128.s.Hi = 0;
    s.u128Dummy1 = s.u128Dummy0;

    u128Arg.s.Lo = 1;
    u128Arg.s.Hi = 0;
    u128Ret.u = ASMAtomicXchgU128(&s.u128.u, u128Arg.u);
    CHECKVAL(u128Ret.s.Lo, 0ULL, "%#llx");
    CHECKVAL(u128Ret.s.Hi, 0ULL, "%#llx");
    CHECKVAL(s.u128.s.Lo, 1ULL, "%#llx");
    CHECKVAL(s.u128.s.Hi, 0ULL, "%#llx");

    u128Arg.s.Lo = 0;
    u128Arg.s.Hi = 0;
    u128Ret.u = ASMAtomicXchgU128(&s.u128.u, u128Arg.u);
    CHECKVAL(u128Ret.s.Lo, 1ULL, "%#llx");
    CHECKVAL(u128Ret.s.Hi, 0ULL, "%#llx");
    CHECKVAL(s.u128.s.Lo, 0ULL, "%#llx");
    CHECKVAL(s.u128.s.Hi, 0ULL, "%#llx");

    u128Arg.s.Lo = ~0ULL;
    u128Arg.s.Hi = ~0ULL;
    u128Ret.u = ASMAtomicXchgU128(&s.u128.u, u128Arg.u);
    CHECKVAL(u128Ret.s.Lo, 0ULL, "%#llx");
    CHECKVAL(u128Ret.s.Hi, 0ULL, "%#llx");
    CHECKVAL(s.u128.s.Lo, ~0ULL, "%#llx");
    CHECKVAL(s.u128.s.Hi, ~0ULL, "%#llx");


    u128Arg.s.Lo = 0xfedcba0987654321ULL;
    u128Arg.s.Hi = 0x8897a6b5c4d3e2f1ULL;
    u128Ret.u = ASMAtomicXchgU128(&s.u128.u, u128Arg.u);
    CHECKVAL(u128Ret.s.Lo, ~0ULL, "%#llx");
    CHECKVAL(u128Ret.s.Hi, ~0ULL, "%#llx");
    CHECKVAL(s.u128.s.Lo, 0xfedcba0987654321ULL, "%#llx");
    CHECKVAL(s.u128.s.Hi, 0x8897a6b5c4d3e2f1ULL, "%#llx");

    CHECKVAL(s.u128Dummy0.s.Lo, 0x1122334455667788, "%#llx");
    CHECKVAL(s.u128Dummy0.s.Hi, 0x1122334455667788, "%#llx");
    CHECKVAL(s.u128Dummy1.s.Lo, 0x1122334455667788, "%#llx");
    CHECKVAL(s.u128Dummy1.s.Hi, 0x1122334455667788, "%#llx");
}
#endif


static void tstASMAtomicXchgPtr(void)
{
    void *pv = NULL;

    CHECKOP(ASMAtomicXchgPtr(&pv, (void *)(~(uintptr_t)0)), NULL, "%p", void *);
    CHECKVAL(pv, (void *)(~(uintptr_t)0), "%p");

    CHECKOP(ASMAtomicXchgPtr(&pv, (void *)0x87654321), (void *)(~(uintptr_t)0), "%p", void *);
    CHECKVAL(pv, (void *)0x87654321, "%p");

    CHECKOP(ASMAtomicXchgPtr(&pv, NULL), (void *)0x87654321, "%p", void *);
    CHECKVAL(pv, NULL, "%p");
}


static void tstASMAtomicCmpXchgU32(void)
{
    uint32_t u32 = 0xffffffff;

    CHECKOP(ASMAtomicCmpXchgU32(&u32, 0, 0), false, "%d", bool);
    CHECKVAL(u32, 0xffffffff, "%x");

    CHECKOP(ASMAtomicCmpXchgU32(&u32, 0, 0xffffffff), true, "%d", bool);
    CHECKVAL(u32, 0, "%x");

    CHECKOP(ASMAtomicCmpXchgU32(&u32, 0x8008efd, 0xffffffff), false, "%d", bool);
    CHECKVAL(u32, 0, "%x");

    CHECKOP(ASMAtomicCmpXchgU32(&u32, 0x8008efd, 0), true, "%d", bool);
    CHECKVAL(u32, 0x8008efd, "%x");
}


static void tstASMAtomicCmpXchgU64(void)
{
    uint64_t u64 = 0xffffffffffffffULL;

    CHECKOP(ASMAtomicCmpXchgU64(&u64, 0, 0), false, "%d", bool);
    CHECKVAL(u64, 0xffffffffffffffULL, "%x");

    CHECKOP(ASMAtomicCmpXchgU64(&u64, 0, 0xffffffffffffffULL), true, "%d", bool);
    CHECKVAL(u64, 0, "%x");

    CHECKOP(ASMAtomicCmpXchgU64(&u64, 0x80040008008efdULL, 0xffffffff), false, "%d", bool);
    CHECKVAL(u64, 0, "%x");

    CHECKOP(ASMAtomicCmpXchgU64(&u64, 0x80040008008efdULL, 0xffffffff00000000ULL), false, "%d", bool);
    CHECKVAL(u64, 0, "%x");

    CHECKOP(ASMAtomicCmpXchgU64(&u64, 0x80040008008efdULL, 0), true, "%d", bool);
    CHECKVAL(u64, 0x80040008008efdULL, "%x");
}


static void tstASMAtomicReadU64(void)
{
    uint64_t u64 = 0;

    CHECKOP(ASMAtomicReadU64(&u64), 0ULL, "%#llx", uint64_t);
    CHECKVAL(u64, 0ULL, "%#llx");

    u64 = ~0ULL;
    CHECKOP(ASMAtomicReadU64(&u64), ~0ULL, "%#llx", uint64_t);
    CHECKVAL(u64, ~0ULL, "%#llx");

    u64 = 0xfedcba0987654321ULL;
    CHECKOP(ASMAtomicReadU64(&u64), 0xfedcba0987654321ULL, "%#llx", uint64_t);
    CHECKVAL(u64, 0xfedcba0987654321ULL, "%#llx");
}


static void tstASMAtomicDecIncS32(void)
{
    int32_t i32Rc;
    int32_t i32 = 10;
#define MYCHECK(op, rc) \
    do { \
        i32Rc = op; \
        if (i32Rc != (rc)) \
        { \
            RTPrintf("%s, %d: FAILURE: %s -> %d expected %d\n", __FUNCTION__, __LINE__, #op, i32Rc, rc); \
            g_cErrors++; \
        } \
        if (i32 != (rc)) \
        { \
            RTPrintf("%s, %d: FAILURE: %s => i32=%d expected %d\n", __FUNCTION__, __LINE__, #op, i32, rc); \
            g_cErrors++; \
        } \
    } while (0)
    MYCHECK(ASMAtomicDecS32(&i32), 9);
    MYCHECK(ASMAtomicDecS32(&i32), 8);
    MYCHECK(ASMAtomicDecS32(&i32), 7);
    MYCHECK(ASMAtomicDecS32(&i32), 6);
    MYCHECK(ASMAtomicDecS32(&i32), 5);
    MYCHECK(ASMAtomicDecS32(&i32), 4);
    MYCHECK(ASMAtomicDecS32(&i32), 3);
    MYCHECK(ASMAtomicDecS32(&i32), 2);
    MYCHECK(ASMAtomicDecS32(&i32), 1);
    MYCHECK(ASMAtomicDecS32(&i32), 0);
    MYCHECK(ASMAtomicDecS32(&i32), -1);
    MYCHECK(ASMAtomicDecS32(&i32), -2);
    MYCHECK(ASMAtomicIncS32(&i32), -1);
    MYCHECK(ASMAtomicIncS32(&i32), 0);
    MYCHECK(ASMAtomicIncS32(&i32), 1);
    MYCHECK(ASMAtomicIncS32(&i32), 2);
    MYCHECK(ASMAtomicIncS32(&i32), 3);
    MYCHECK(ASMAtomicDecS32(&i32), 2);
    MYCHECK(ASMAtomicIncS32(&i32), 3);
    MYCHECK(ASMAtomicDecS32(&i32), 2);
    MYCHECK(ASMAtomicIncS32(&i32), 3);
#undef MYCHECK

}


static void tstASMAtomicAndOrU32(void)
{
    uint32_t u32 = 0xffffffff;

    ASMAtomicOrU32(&u32, 0xffffffff);
    CHECKVAL(u32, 0xffffffff, "%x");

    ASMAtomicAndU32(&u32, 0xffffffff);
    CHECKVAL(u32, 0xffffffff, "%x");

    ASMAtomicAndU32(&u32, 0x8f8f8f8f);
    CHECKVAL(u32, 0x8f8f8f8f, "%x");

    ASMAtomicOrU32(&u32, 0x70707070);
    CHECKVAL(u32, 0xffffffff, "%x");

    ASMAtomicAndU32(&u32, 1);
    CHECKVAL(u32, 1, "%x");

    ASMAtomicOrU32(&u32, 0x80000000);
    CHECKVAL(u32, 0x80000001, "%x");

    ASMAtomicAndU32(&u32, 0x80000000);
    CHECKVAL(u32, 0x80000000, "%x");

    ASMAtomicAndU32(&u32, 0);
    CHECKVAL(u32, 0, "%x");

    ASMAtomicOrU32(&u32, 0x42424242);
    CHECKVAL(u32, 0x42424242, "%x");
}


void tstASMMemZeroPage(void)
{
    struct
    {
        uint64_t    u64Magic1;
        uint8_t     abPage[PAGE_SIZE];
        uint64_t    u64Magic2;
    } Buf1, Buf2, Buf3;

    Buf1.u64Magic1 = UINT64_C(0xffffffffffffffff);
    memset(Buf1.abPage, 0x55, sizeof(Buf1.abPage));
    Buf1.u64Magic2 = UINT64_C(0xffffffffffffffff);
    Buf2.u64Magic1 = UINT64_C(0xffffffffffffffff);
    memset(Buf2.abPage, 0x77, sizeof(Buf2.abPage));
    Buf2.u64Magic2 = UINT64_C(0xffffffffffffffff);
    Buf3.u64Magic1 = UINT64_C(0xffffffffffffffff);
    memset(Buf3.abPage, 0x99, sizeof(Buf3.abPage));
    Buf3.u64Magic2 = UINT64_C(0xffffffffffffffff);
    ASMMemZeroPage(Buf1.abPage);
    ASMMemZeroPage(Buf2.abPage);
    ASMMemZeroPage(Buf3.abPage);
    if (    Buf1.u64Magic1 != UINT64_C(0xffffffffffffffff)
        ||  Buf1.u64Magic2 != UINT64_C(0xffffffffffffffff)
        ||  Buf1.u64Magic1 != UINT64_C(0xffffffffffffffff)
        ||  Buf1.u64Magic2 != UINT64_C(0xffffffffffffffff)
        ||  Buf2.u64Magic1 != UINT64_C(0xffffffffffffffff)
        ||  Buf2.u64Magic2 != UINT64_C(0xffffffffffffffff))
    {
        RTPrintf("tstInlineAsm: ASMMemZeroPage violated one/both magic(s)!\n");
        g_cErrors++;
    }
    for (unsigned i = 0; i < sizeof(Buf1.abPage); i++)
        if (Buf1.abPage[i])
        {
            RTPrintf("tstInlineAsm: ASMMemZeroPage didn't clear byte at offset %#x!\n", i);
            g_cErrors++;
        }
    for (unsigned i = 0; i < sizeof(Buf1.abPage); i++)
        if (Buf1.abPage[i])
        {
            RTPrintf("tstInlineAsm: ASMMemZeroPage didn't clear byte at offset %#x!\n", i);
            g_cErrors++;
        }
    for (unsigned i = 0; i < sizeof(Buf2.abPage); i++)
        if (Buf2.abPage[i])
        {
            RTPrintf("tstInlineAsm: ASMMemZeroPage didn't clear byte at offset %#x!\n", i);
            g_cErrors++;
        }
}


void tstASMMath(void)
{
    uint64_t u64 = ASMMult2xU32RetU64(UINT32_C(0x80000000), UINT32_C(0x10000000));
    CHECKVAL(u64, UINT64_C(0x0800000000000000), "%#018RX64");

    uint32_t u32 = ASMDivU64ByU32RetU32(UINT64_C(0x0800000000000000), UINT32_C(0x10000000));
    CHECKVAL(u32, UINT32_C(0x80000000), "%#010RX32");
}


int main(int argc, char *argv[])
{
    RTR3Init();
    RTPrintf("tstInlineAsm: TESTING\n");

    /*
     * Execute the tests.
     */
#ifndef PIC
    tstASMCpuId();
#endif
    tstASMAtomicXchgU8();
    tstASMAtomicXchgU16();
    tstASMAtomicXchgU32();
    tstASMAtomicXchgU64();
#ifdef __amd64__
    tstASMAtomicXchgU128();
#endif
    tstASMAtomicXchgPtr();
    tstASMAtomicCmpXchgU32();
    tstASMAtomicCmpXchgU64();
    tstASMAtomicReadU64();
    tstASMAtomicDecIncS32();
    tstASMAtomicAndOrU32();
    tstASMMemZeroPage();
    tstASMMath();

    /*
     * Show the result.
     */
    if (!g_cErrors)
        RTPrintf("tstInlineAsm: SUCCESS\n", g_cErrors);
    else
        RTPrintf("tstInlineAsm: FAILURE - %d errors\n", g_cErrors);
    return !!g_cErrors;
}

