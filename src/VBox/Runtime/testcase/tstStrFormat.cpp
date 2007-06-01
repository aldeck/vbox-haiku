/* $Id$ */
/** @file
 * innotek Portable Runtime Testcase - String formatting.
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
#include <iprt/string.h>
#include <iprt/runtime.h>
#include <iprt/uuid.h>
#include <iprt/string.h>
#include <iprt/stream.h>

int main()
{
    RTR3Init();

    int         cErrors = 0;
    uint32_t    u32 = 0x010;
    uint64_t    u64 = 0x100;
    char        szStr[120];

    /* simple */
    size_t cch = RTStrPrintf(szStr, sizeof(szStr), "u32=%d u64=%lld u64=%#llx", u32, u64, u64);
    if (strcmp(szStr, "u32=16 u64=256 u64=0x100"))
    {
        RTPrintf("error: '%s'\n"
               "wanted 'u32=16 u64=256 u64=0x100'\n", szStr);
        cErrors++;
    }

    /* just big. */
    u64 = UINT64_C(0x7070605040302010);
    cch = RTStrPrintf(szStr, sizeof(szStr), "u64=%#llx 42=%d u64=%lld 42=%d", u64, 42, u64, 42);
    if (strcmp(szStr, "u64=0x7070605040302010 42=42 u64=8102081627430068240 42=42"))
    {
        RTPrintf("error: '%s'\n"
                 "wanted 'u64=0x8070605040302010 42=42 u64=8102081627430068240 42=42'\n", szStr);
        RTPrintf("%d\n", (int)(u64 % 10));
        cErrors++;
    }

    /* huge and negative. */
    u64 = UINT64_C(0x8070605040302010);
    cch = RTStrPrintf(szStr, sizeof(szStr), "u64=%#llx 42=%d u64=%llu 42=%d u64=%lld 42=%d", u64, 42, u64, 42, u64, 42);
    /* Not sure if this is the correct decimal representation... But both */
    if (strcmp(szStr, "u64=0x8070605040302010 42=42 u64=9255003132036915216 42=42 u64=-9191740941672636400 42=42"))
    {
        RTPrintf("error: '%s'\n"
                 "wanted 'u64=0x8070605040302010 42=42 u64=9255003132036915216 42=42 u64=-9191740941672636400 42=42'\n", szStr);
        RTPrintf("%d\n", (int)(u64 % 10));
        cErrors++;
    }

    /* 64-bit value bug. */
    u64 = 0xa0000000;
    cch = RTStrPrintf(szStr, sizeof(szStr), "u64=%#llx 42=%d u64=%lld 42=%d", u64, 42, u64, 42);
    if (strcmp(szStr, "u64=0xa0000000 42=42 u64=2684354560 42=42"))
    {
        RTPrintf("error: '%s'\n"
                 "wanted 'u64=0xa0000000 42=42 u64=2684354560 42=42'\n", szStr);
        cErrors++;
    }

    /* uuid */
    RTUUID Uuid;
    RTUuidCreate(&Uuid);
    char szCorrect[RTUUID_STR_LENGTH];
    RTUuidToStr(&Uuid, szCorrect, sizeof(szCorrect));
    cch = RTStrPrintf(szStr, sizeof(szStr), "%Vuuid", &Uuid);
    if (strcmp(szStr, szCorrect))
    {
        RTPrintf("error:    '%s'\n"
                 "expected: '%s'\n",
                 szStr, szCorrect);
        cErrors++;
    }

    /* allocation */
    char *psz = (char *)~0;
    int cch2 = RTStrAPrintf(&psz, "Hey there! %s%s", "This is a test", "!");
    if (cch2 < 0)
    {
        RTPrintf("error: RTStrAPrintf failed, cch2=%d\n", cch2);
        cErrors++;
    }
    else if (strcmp(psz, "Hey there! This is a test!"))
    {
        RTPrintf("error: RTStrAPrintf failed\n"
                 "got   : '%s'\n"
                 "wanted: 'Hey there! This is a test!'\n",
               psz);
        cErrors++;
    }
    else if ((int)strlen(psz) != cch2)
    {
        RTPrintf("error: RTStrAPrintf failed, cch2 == %d expected %u\n", cch2, strlen(psz));
        cErrors++;
    }
    RTStrFree(psz);

#define CHECK42(fmt, arg, out) \
    do { \
        cch = RTStrPrintf(szStr, sizeof(szStr), fmt " 42=%d " fmt " 42=%d", arg, 42, arg, 42); \
        if (strcmp(szStr, out " 42=42 " out " 42=42")) \
        { \
            RTPrintf("error(%d): format '%s'\n" \
                     "    output: '%s'\n"  \
                     "    wanted: '%s'\n", \
                     __LINE__, fmt, szStr, out " 42=42 " out " 42=42"); \
            cErrors++; \
        } \
        else if (cch != sizeof(out " 42=42 " out " 42=42") - 1) \
        { \
            RTPrintf("error(%d): Invalid length %d returned, expected %u!\n", \
                     __LINE__, cch, sizeof(out " 42=42 " out " 42=42") - 1); \
            cErrors++; \
        } \
    } while (0)

    /*
     * Runtime extensions.
     */
    CHECK42("%RGi", (RTGCINT)127, "127");
    CHECK42("%RGi", (RTGCINT)-586589, "-586589");

    CHECK42("%RGp", (RTGCPHYS)0x44505045, "44505045");
    CHECK42("%RGp", ~(RTGCPHYS)0, "ffffffff");

    CHECK42("%RGu", (RTGCUINT)586589, "586589");
    CHECK42("%RGu", (RTGCUINT)1, "1");
    CHECK42("%RGu", (RTGCUINT)3000000000U, "3000000000");

    CHECK42("%RGv", (RTGCUINTPTR)0, "00000000");
    CHECK42("%RGv", ~(RTGCUINTPTR)0, "ffffffff");
    CHECK42("%RGv", (RTGCUINTPTR)0x84342134, "84342134");

    CHECK42("%RGx", (RTGCUINT)0x234, "234");
    CHECK42("%RGx", (RTGCUINT)0xffffffff, "ffffffff");

    CHECK42("%RHi", (RTHCINT)127, "127");
    CHECK42("%RHi", (RTHCINT)-586589, "-586589");

    CHECK42("%RHp", (RTHCPHYS)0x0000000044505045, "0000000044505045");
    CHECK42("%RHp", ~(RTHCPHYS)0, "ffffffffffffffff");

    CHECK42("%RHu", (RTHCUINT)586589, "586589");
    CHECK42("%RHu", (RTHCUINT)1, "1");
    CHECK42("%RHu", (RTHCUINT)3000000000U, "3000000000");

    if (sizeof(void*) == 8)
    {
        CHECK42("%RHv", (RTHCUINTPTR)0, "0000000000000000");
        CHECK42("%RHv", ~(RTHCUINTPTR)0, "ffffffffffffffff");
        CHECK42("%RHv", (RTHCUINTPTR)0x84342134, "0000000084342134");
    }
    else
    {
        CHECK42("%RHv", (RTHCUINTPTR)0, "00000000");
        CHECK42("%RHv", ~(RTHCUINTPTR)0, "ffffffff");
        CHECK42("%RHv", (RTHCUINTPTR)0x84342134, "84342134");
    }

    CHECK42("%RHx", (RTHCUINT)0x234, "234");
    CHECK42("%RHx", (RTHCUINT)0xffffffff, "ffffffff");

    CHECK42("%RI16", (int16_t)1, "1");
    CHECK42("%RI16", (int16_t)-16384, "-16384");

    CHECK42("%RI32", (int32_t)1123, "1123");
    CHECK42("%RI32", (int32_t)-86596, "-86596");

    CHECK42("%RI64", (int64_t)112345987345LL, "112345987345");
    CHECK42("%RI64", (int64_t)-8659643985723459LL, "-8659643985723459");

    CHECK42("%RI8", (int8_t)1, "1");
    CHECK42("%RI8", (int8_t)-128, "-128");

    CHECK42("%RTfile", (RTFILE)127, "127");
    CHECK42("%RTfile", (RTFILE)12341234, "12341234");

    CHECK42("%RTfmode", (RTFMODE)0x123403, "00123403");

    CHECK42("%RTfoff", (RTFOFF)12342312, "12342312");
    CHECK42("%RTfoff", (RTFOFF)-123123123, "-123123123");
    CHECK42("%RTfoff", (RTFOFF)858694596874568LL, "858694596874568");

    RTFAR16 fp16;
    fp16.off = 0x34ff;
    fp16.sel = 0x0160;
    CHECK42("%RTfp16", fp16, "0160:34ff");

    RTFAR32 fp32;
    fp32.off = 0xff094030;
    fp32.sel = 0x0168;
    CHECK42("%RTfp32", fp32, "0168:ff094030");

    RTFAR64 fp64;
    fp64.off = 0xffff003401293487ULL;
    fp64.sel = 0x0ff8;
    CHECK42("%RTfp64", fp64, "0ff8:ffff003401293487");
    fp64.off = 0x0;
    fp64.sel = 0x0;
    CHECK42("%RTfp64", fp64, "0000:0000000000000000");

    CHECK42("%RTgid", (RTGID)-1, "-1");
    CHECK42("%RTgid", (RTGID)1004, "1004");

    CHECK42("%RTino", (RTINODE)0, "0000000000000000");
    CHECK42("%RTino", (RTINODE)0x123412341324ULL, "0000123412341324");

    CHECK42("%RTint", (RTINT)127, "127");
    CHECK42("%RTint", (RTINT)-586589, "-586589");
    CHECK42("%RTint", (RTINT)-23498723, "-23498723");

    CHECK42("%RTiop", (RTIOPORT)0x3c4, "03c4");
    CHECK42("%RTiop", (RTIOPORT)0xffff, "ffff");

    CHECK42("%RTproc", (RTPROCESS)0xffffff, "00ffffff");
    CHECK42("%RTproc", (RTPROCESS)0x43455443, "43455443");

    if (sizeof(RTUINTPTR) == 8)
    {
        CHECK42("%RTptr", (RTUINTPTR)0, "0000000000000000");
        CHECK42("%RTptr", ~(RTUINTPTR)0, "ffffffffffffffff");
        CHECK42("%RTptr", (RTUINTPTR)0x84342134, "0000000084342134");
    }
    else
    {
        CHECK42("%RTptr", (RTUINTPTR)0, "00000000");
        CHECK42("%RTptr", ~(RTUINTPTR)0, "ffffffff");
        CHECK42("%RTptr", (RTUINTPTR)0x84342134, "84342134");
    }

    if (sizeof(RTUINTREG) == 8)
    {
        CHECK42("%RTreg", (RTUINTREG)0, "0000000000000000");
        CHECK42("%RTreg", ~(RTUINTREG)0, "ffffffffffffffff");
        CHECK42("%RTreg", (RTUINTREG)0x84342134, "0000000084342134");
        CHECK42("%RTreg", (RTUINTREG)0x23484342134ULL, "0000023484342134");
    }
    else
    {
        CHECK42("%RTreg", (RTUINTREG)0, "00000000");
        CHECK42("%RTreg", ~(RTUINTREG)0, "ffffffff");
        CHECK42("%RTreg", (RTUINTREG)0x84342134, "84342134");
    }

    CHECK42("%RTsel", (RTSEL)0x543, "0543");
    CHECK42("%RTsel", (RTSEL)0xf8f8, "f8f8");

    if (sizeof(RTSEMEVENT) == 8)
    {
        CHECK42("%RTsem", (RTSEMEVENT)0, "0000000000000000");
        CHECK42("%RTsem", (RTSEMEVENT)0x23484342134ULL, "0000023484342134");
    }
    else
    {
        CHECK42("%RTsem", (RTSEMEVENT)0, "00000000");
        CHECK42("%RTsem", (RTSEMEVENT)0x84342134, "84342134");
    }

    CHECK42("%RTsock", (RTSOCKET)12234, "12234");
    CHECK42("%RTsock", (RTSOCKET)584854543, "584854543");

    if (sizeof(RTTHREAD) == 8)
    {
        CHECK42("%RTthrd", (RTTHREAD)0, "0000000000000000");
        CHECK42("%RTthrd", (RTTHREAD)~(uintptr_t)0, "ffffffffffffffff");
        CHECK42("%RTthrd", (RTTHREAD)0x63484342134ULL, "0000063484342134");
    }
    else
    {
        CHECK42("%RTthrd", (RTTHREAD)0, "00000000");
        CHECK42("%RTthrd", (RTTHREAD)~(uintptr_t)0, "ffffffff");
        CHECK42("%RTthrd", (RTTHREAD)0x54342134, "54342134");
    }

    CHECK42("%RTuid", (RTUID)-2, "-2");
    CHECK42("%RTuid", (RTUID)90344, "90344");

    CHECK42("%RTuint", (RTGCUINT)584589, "584589");
    CHECK42("%RTuint", (RTGCUINT)3, "3");
    CHECK42("%RTuint", (RTGCUINT)2400000000U, "2400000000");

    RTUuidCreate(&Uuid);
    RTUuidToStr(&Uuid, szCorrect, sizeof(szCorrect));
    cch = RTStrPrintf(szStr, sizeof(szStr), "%RTuuid", &Uuid);
    if (strcmp(szStr, szCorrect))
    {
        RTPrintf("error:    '%s'\n"
                 "expected: '%s'\n",
                 szStr, szCorrect);
        cErrors++;
    }

    CHECK42("%RTxint", (RTGCUINT)0x2345, "2345");
    CHECK42("%RTxint", (RTGCUINT)0xffff8fff, "ffff8fff");

    CHECK42("%RU16", (uint16_t)7, "7");
    CHECK42("%RU16", (uint16_t)46384, "46384");

    CHECK42("%RU32", (uint32_t)1123, "1123");
    CHECK42("%RU32", (uint32_t)86596, "86596");

    CHECK42("%RU64", (uint64_t)112345987345ULL, "112345987345");
    CHECK42("%RU64", (uint64_t)8659643985723459ULL, "8659643985723459");

    CHECK42("%RU8", (uint8_t)1, "1");
    CHECK42("%RU8", (uint8_t)254, "254");
    CHECK42("%RU8", 256, "0");

    CHECK42("%RX16", (uint16_t)0x7, "7");
    CHECK42("%RX16", 0x46384, "6384");

    CHECK42("%RX32", (uint32_t)0x1123, "1123");
    CHECK42("%RX32", (uint32_t)0x49939493, "49939493");

    CHECK42("%RX64", (uint64_t)0x348734, "348734");
    CHECK42("%RX64", (uint64_t)0x12312312312343fULL, "12312312312343f");

    CHECK42("%RX8", (uint8_t)1, "1");
    CHECK42("%RX8", (uint8_t)0xff, "ff");
    CHECK42("%RX8", 0x100, "0");

#define CHECKSTR(Correct) \
    if (strcmp(szStr, Correct)) \
    { \
        RTPrintf("error:    '%s'\n" \
                 "expected: '%s'\n", szStr, Correct); \
        cErrors++; \
    }

    /*
     * String formatting.
     */
//            0         1         2         3         4         5         6         7
//            0....5....0....5....0....5....0....5....0....5....0....5....0....5....0
    cch = RTStrPrintf(szStr, sizeof(szStr), "%-10s %-30s %s", "cmd", "args", "description");
    CHECKSTR("cmd        args                           description");

    cch = RTStrPrintf(szStr, sizeof(szStr), "%-10s %-30s %s", "cmd", "", "description");
    CHECKSTR("cmd                                       description");


    cch = RTStrPrintf(szStr, sizeof(szStr),  "%*s", 0, "");
    CHECKSTR("");

    /* automatic conversions. */
    static RTUNICP s_usz1[] = { 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', 0 }; //assumes ascii.
    static RTUTF16 s_wsz1[] = { 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', 0 }; //assumes ascii.

    cch = RTStrPrintf(szStr, sizeof(szStr), "%ls", s_wsz1);
    CHECKSTR("hello world");
    cch = RTStrPrintf(szStr, sizeof(szStr), "%Ls", s_usz1);
    CHECKSTR("hello world");

    cch = RTStrPrintf(szStr, sizeof(szStr), "%.5ls", s_wsz1);
    CHECKSTR("hello");
    cch = RTStrPrintf(szStr, sizeof(szStr), "%.5Ls", s_usz1);
    CHECKSTR("hello");

#if 0
    static RTUNICP s_usz2[] = { 0xc5, 0xc6, 0xf8, 0 };
    static RTUTF16 s_wsz2[] = { 0xc5, 0xc6, 0xf8, 0 };
    static char    s_sz2[]  = { 0xc5, 0xc6, 0xf8, 0 };///@todo multibyte tests.

    cch = RTStrPrintf(szStr, sizeof(szStr), "%ls", s_wsz2);
    CHECKSTR(s_sz2);
    cch = RTStrPrintf(szStr, sizeof(szStr), "%Ls", s_usz2);
    CHECKSTR(s_sz2);
#endif


    /*
     * Summarize and exit.
     */
    if (!cErrors)
        RTPrintf("tstStrFormat: SUCCESS\n");
    else
        RTPrintf("tstStrFormat: FAILED - %d errors\n", cErrors);
    return !!cErrors;
}

