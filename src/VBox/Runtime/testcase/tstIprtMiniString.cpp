/* $Id$ */
/** @file
 * IPRT Testcase - iprt::MiniString.
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/cpp/ministring.h>

#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/uni.h>


static void test1Hlp1(const char *pszExpect, const char *pszFormat, ...)
{
#if 0
    va_list va;
    va_start(va, pszFormat);
    iprt::MiniString strTst(pszFormat, va);
    va_end(va);
    RTTESTI_CHECK_MSG(strTst.equals(pszExpect),  ("strTst='%s' expected='%s'\n",  strTst.c_str(), pszExpect));
#endif
}

static void test1(RTTEST hTest)
{
    RTTestSub(hTest, "Basics");

#define CHECK(expr) RTTESTI_CHECK(expr)
#define CHECK_DUMP(expr, value) \
    do { \
        if (!(expr)) \
            RTTestFailed(hTest, "%d: FAILED %s, got \"%s\"", __LINE__, #expr, value); \
    } while (0)

#define CHECK_DUMP_I(expr) \
    do { \
        if (!(expr)) \
            RTTestFailed(hTest, "%d: FAILED %s, got \"%d\"", __LINE__, #expr, expr); \
    } while (0)
#define CHECK_EQUAL(Str, szExpect) \
    do { \
        if (!(Str).equals(szExpect)) \
            RTTestIFailed("line %u: expected \"%s\" got \"%s\"", __LINE__, szExpect, (Str).c_str()); \
    } while (0)

    iprt::MiniString empty;
    CHECK(empty.length() == 0);
    CHECK(empty.capacity() == 0);

    iprt::MiniString sixbytes("12345");
    CHECK(sixbytes.length() == 5);
    CHECK(sixbytes.capacity() == 6);

    sixbytes.append(iprt::MiniString("678"));
    CHECK(sixbytes.length() == 8);
    CHECK(sixbytes.capacity() >= 9);

    sixbytes.append("9a");
    CHECK(sixbytes.length() == 10);
    CHECK(sixbytes.capacity() >= 11);

    char *psz = sixbytes.mutableRaw();
        // 123456789a
        //       ^
        // 0123456
    psz[6] = '\0';
    sixbytes.jolt();
    CHECK(sixbytes.length() == 6);
    CHECK(sixbytes.capacity() == 7);

    iprt::MiniString morebytes("tobereplaced");
    morebytes = "newstring ";
    morebytes.append(sixbytes);

    CHECK_DUMP(morebytes == "newstring 123456", morebytes.c_str());

    iprt::MiniString third(morebytes);
    third.reserve(100 * 1024);      // 100 KB
    CHECK_DUMP(third == "newstring 123456", morebytes.c_str() );
    CHECK(third.capacity() == 100 * 1024);
    CHECK(third.length() == morebytes.length());          // must not have changed

    iprt::MiniString copy1(morebytes);
    iprt::MiniString copy2 = morebytes;
    CHECK(copy1 == copy2);

    copy1 = NULL;
    CHECK(copy1.length() == 0);

    copy1 = "";
    CHECK(copy1.length() == 0);

    CHECK(iprt::MiniString("abc") <  iprt::MiniString("def"));
    CHECK(iprt::MiniString("") <  iprt::MiniString("def"));
    CHECK(iprt::MiniString("abc") > iprt::MiniString(""));
    CHECK(iprt::MiniString("abc") != iprt::MiniString("def"));
    CHECK_DUMP_I(iprt::MiniString("def") > iprt::MiniString("abc"));
    CHECK(iprt::MiniString("abc") == iprt::MiniString("abc"));
    CHECK(iprt::MiniString("").compare("") == 0);
    CHECK(iprt::MiniString("").compare(NULL) == 0);
    CHECK(iprt::MiniString("").compare("a") < 0);
    CHECK(iprt::MiniString("a").compare("") > 0);
    CHECK(iprt::MiniString("a").compare(NULL) > 0);

    CHECK(iprt::MiniString("abc") <  "def");
    CHECK(iprt::MiniString("abc") != "def");
    CHECK_DUMP_I(iprt::MiniString("def") > "abc");
    CHECK(iprt::MiniString("abc") == "abc");

    CHECK(iprt::MiniString("abc").equals("abc"));
    CHECK(!iprt::MiniString("abc").equals("def"));
    CHECK(iprt::MiniString("abc").equalsIgnoreCase("Abc"));
    CHECK(iprt::MiniString("abc").equalsIgnoreCase("ABc"));
    CHECK(iprt::MiniString("abc").equalsIgnoreCase("ABC"));
    CHECK(!iprt::MiniString("abc").equalsIgnoreCase("dBC"));
    CHECK(iprt::MiniString("").equals(""));
    CHECK(iprt::MiniString("").equals(NULL));
    CHECK(!iprt::MiniString("").equals("a"));
    CHECK(!iprt::MiniString("a").equals(""));
    CHECK(!iprt::MiniString("a").equals(NULL));
    CHECK(iprt::MiniString("").equalsIgnoreCase(""));
    CHECK(iprt::MiniString("").equalsIgnoreCase(NULL));
    CHECK(!iprt::MiniString("").equalsIgnoreCase("a"));
    CHECK(!iprt::MiniString("a").equalsIgnoreCase(""));

    copy2.setNull();
    for (int i = 0; i < 100; ++i)
    {
        copy2.reserve(50);      // should be ignored after 50 loops
        copy2.append("1");
    }
    CHECK(copy2.length() == 100);

    copy2.setNull();
    for (int i = 0; i < 100; ++i)
    {
        copy2.reserve(50);      // should be ignored after 50 loops
        copy2.append('1');
    }
    CHECK(copy2.length() == 100);

    /* printf */
    iprt::MiniString StrFmt;
    CHECK(StrFmt.printf("%s-%s-%d", "abc", "def", 42).equals("abc-def-42"));
    test1Hlp1("abc-42-def", "%s-%d-%s", "abc", 42, "def");
    test1Hlp1("", "");
    test1Hlp1("1", "1");
    test1Hlp1("foobar", "%s", "foobar");

    /* substring constructors */
    iprt::MiniString SubStr1("", (size_t)0);
    CHECK_EQUAL(SubStr1, "");

    iprt::MiniString SubStr2("abcdef", 2);
    CHECK_EQUAL(SubStr2, "ab");

    iprt::MiniString SubStr3("abcdef", 1);
    CHECK_EQUAL(SubStr3, "a");

    iprt::MiniString SubStr4("abcdef", 6);
    CHECK_EQUAL(SubStr4, "abcdef");

    iprt::MiniString SubStr5("abcdef", 7);
    CHECK_EQUAL(SubStr5, "abcdef");


    iprt::MiniString SubStrBase("abcdef");

    iprt::MiniString SubStr10(SubStrBase, 0);
    CHECK_EQUAL(SubStr10, "abcdef");

    iprt::MiniString SubStr11(SubStrBase, 1);
    CHECK_EQUAL(SubStr11, "bcdef");

    iprt::MiniString SubStr12(SubStrBase, 1, 1);
    CHECK_EQUAL(SubStr12, "b");

    iprt::MiniString SubStr13(SubStrBase, 2, 3);
    CHECK_EQUAL(SubStr13, "cde");

    iprt::MiniString SubStr14(SubStrBase, 2, 4);
    CHECK_EQUAL(SubStr14, "cdef");

    iprt::MiniString SubStr15(SubStrBase, 2, 5);
    CHECK_EQUAL(SubStr15, "cdef");

    /* substr() and substrCP() functions */
    iprt::MiniString strTest("");
    CHECK_EQUAL(strTest.substr(0), "");
    CHECK_EQUAL(strTest.substrCP(0), "");
    CHECK_EQUAL(strTest.substr(1), "");
    CHECK_EQUAL(strTest.substrCP(1), "");

    /* now let's have some non-ASCII to chew on */
    strTest = "abcdefßäbcdef";
            // 13 codepoints, but 15 bytes (excluding null terminator);
            // "ß" and "ä" consume two bytes each
    CHECK_EQUAL(strTest.substr(0),   strTest.c_str());
    CHECK_EQUAL(strTest.substrCP(0), strTest.c_str());

    CHECK_EQUAL(strTest.substr(2),   "cdefßäbcdef");
    CHECK_EQUAL(strTest.substrCP(2), "cdefßäbcdef");

    CHECK_EQUAL(strTest.substr(2, 2),   "cd");
    CHECK_EQUAL(strTest.substrCP(2, 2), "cd");

    CHECK_EQUAL(strTest.substr(6),   "ßäbcdef");
    CHECK_EQUAL(strTest.substrCP(6), "ßäbcdef");

    CHECK_EQUAL(strTest.substr(6, 2),   "ß");           // UTF-8 "ß" consumes two bytes
    CHECK_EQUAL(strTest.substrCP(6, 1), "ß");

    CHECK_EQUAL(strTest.substr(8),   "äbcdef");         // UTF-8 "ß" consumes two bytes
    CHECK_EQUAL(strTest.substrCP(7), "äbcdef");

    CHECK_EQUAL(strTest.substr(8, 3),   "äb");          // UTF-8 "ä" consumes two bytes
    CHECK_EQUAL(strTest.substrCP(7, 2), "äb");

    CHECK_EQUAL(strTest.substr(14, 1),   "f");
    CHECK_EQUAL(strTest.substrCP(12, 1), "f");

    CHECK_EQUAL(strTest.substr(15, 1),   "");
    CHECK_EQUAL(strTest.substrCP(13, 1), "");

    CHECK_EQUAL(strTest.substr(16, 1),   "");
    CHECK_EQUAL(strTest.substrCP(15, 1), "");

    /* and check cooperation with find() */
    size_t pos = strTest.find("ß");
    CHECK_EQUAL(strTest.substr(pos), "ßäbcdef");

    /* split */
    iprt::list<iprt::MiniString> spList1 = iprt::MiniString("##abcdef##abcdef####abcdef##").split("##", iprt::MiniString::RemoveEmptyParts);
    RTTESTI_CHECK(spList1.size() == 3);
    for (size_t i = 0; i < spList1.size(); ++i)
        RTTESTI_CHECK(spList1.at(i) == "abcdef");
    iprt::list<iprt::MiniString> spList2 = iprt::MiniString("##abcdef##abcdef####abcdef##").split("##", iprt::MiniString::KeepEmptyParts);
    RTTESTI_CHECK_RETV(spList2.size() == 5);
    RTTESTI_CHECK(spList2.at(0) == "");
    RTTESTI_CHECK(spList2.at(1) == "abcdef");
    RTTESTI_CHECK(spList2.at(2) == "abcdef");
    RTTESTI_CHECK(spList2.at(3) == "");
    RTTESTI_CHECK(spList2.at(4) == "abcdef");
    iprt::list<iprt::MiniString> spList3 = iprt::MiniString().split("##", iprt::MiniString::KeepEmptyParts);
    RTTESTI_CHECK(spList3.size() == 0);
    iprt::list<iprt::MiniString> spList4 = iprt::MiniString().split("");
    RTTESTI_CHECK(spList4.size() == 0);
    iprt::list<iprt::MiniString> spList5 = iprt::MiniString("abcdef").split("");
    RTTESTI_CHECK_RETV(spList5.size() == 1);
    RTTESTI_CHECK(spList5.at(0) == "abcdef");

    /* join */
    iprt::list<iprt::MiniString> jnList;
    strTest = iprt::MiniString::join(jnList);
    RTTESTI_CHECK(strTest == "");
    strTest = iprt::MiniString::join(jnList, "##");
    RTTESTI_CHECK(strTest == "");
    for (size_t i = 0; i < 5; ++i)
        jnList.append("abcdef");
    strTest = iprt::MiniString::join(jnList);
    RTTESTI_CHECK(strTest == "abcdefabcdefabcdefabcdefabcdef");
    strTest = iprt::MiniString::join(jnList, "##");
    RTTESTI_CHECK(strTest == "abcdef##abcdef##abcdef##abcdef##abcdef");

    /* special constructor and assignment arguments */
    iprt::MiniString StrCtor1("");
    RTTESTI_CHECK(StrCtor1.isEmpty());
    RTTESTI_CHECK(StrCtor1.length() == 0);

    iprt::MiniString StrCtor2(NULL);
    RTTESTI_CHECK(StrCtor2.isEmpty());
    RTTESTI_CHECK(StrCtor2.length() == 0);

    iprt::MiniString StrCtor1d(StrCtor1);
    RTTESTI_CHECK(StrCtor1d.isEmpty());
    RTTESTI_CHECK(StrCtor1d.length() == 0);

    iprt::MiniString StrCtor2d(StrCtor2);
    RTTESTI_CHECK(StrCtor2d.isEmpty());
    RTTESTI_CHECK(StrCtor2d.length() == 0);

    for (unsigned i = 0; i < 2; i++)
    {
        iprt::MiniString StrAssign;
        if (i) StrAssign = "abcdef";
        StrAssign = (char *)NULL;
        RTTESTI_CHECK(StrAssign.isEmpty());
        RTTESTI_CHECK(StrAssign.length() == 0);

        if (i) StrAssign = "abcdef";
        StrAssign = "";
        RTTESTI_CHECK(StrAssign.isEmpty());
        RTTESTI_CHECK(StrAssign.length() == 0);

        if (i) StrAssign = "abcdef";
        StrAssign = StrCtor1;
        RTTESTI_CHECK(StrAssign.isEmpty());
        RTTESTI_CHECK(StrAssign.length() == 0);

        if (i) StrAssign = "abcdef";
        StrAssign = StrCtor2;
        RTTESTI_CHECK(StrAssign.isEmpty());
        RTTESTI_CHECK(StrAssign.length() == 0);
    }

#undef CHECK
#undef CHECK_DUMP
#undef CHECK_DUMP_I
#undef CHECK_EQUAL
}


static int mymemcmp(const char *psz1, const char *psz2, size_t cch)
{
    for (size_t off = 0; off < cch; off++)
        if (psz1[off] != psz2[off])
        {
            RTTestIFailed("off=%#x  psz1=%.*Rhxs  psz2=%.*Rhxs\n", off,
                          RT_MIN(cch - off, 8), &psz1[off],
                          RT_MIN(cch - off, 8), &psz2[off]);
            return psz1[off] > psz2[off] ? 1 : -1;
        }
    return 0;
}

static void test2(RTTEST hTest)
{
    RTTestSub(hTest, "UTF-8 upper/lower encoding assumption");

#define CHECK_EQUAL(str1, str2) \
    do \
    { \
        RTTESTI_CHECK(strlen((str1).c_str()) == (str1).length()); \
        RTTESTI_CHECK((str1).length() == (str2).length()); \
        RTTESTI_CHECK(mymemcmp((str1).c_str(), (str2).c_str(), (str2).length() + 1) == 0); \
    } while (0)

    iprt::MiniString strTmp;
    char szDst[16];

    /* Collect all upper and lower case code points. */
    iprt::MiniString strLower("");
    strLower.reserve(_4M);

    iprt::MiniString strUpper("");
    strUpper.reserve(_4M);

    for (RTUNICP uc = 1; uc <= 0x10fffd; uc++)
    {
        if (RTUniCpIsLower(uc))
        {
            RTTESTI_CHECK_MSG(uc < 0xd800 || (uc > 0xdfff && uc != 0xfffe && uc != 0xffff), ("%#x\n", uc));
            strLower.appendCodePoint(uc);
        }
        if (RTUniCpIsUpper(uc))
        {
            RTTESTI_CHECK_MSG(uc < 0xd800 || (uc > 0xdfff && uc != 0xfffe && uc != 0xffff), ("%#x\n", uc));
            strUpper.appendCodePoint(uc);
        }
    }
    RTTESTI_CHECK(strlen(strLower.c_str()) == strLower.length());
    RTTESTI_CHECK(strlen(strUpper.c_str()) == strUpper.length());

    /* Fold each code point in the lower case string and check that it encodes
       into the same or less number of bytes. */
    size_t      cch    = 0;
    const char *pszCur = strLower.c_str();
    iprt::MiniString    strUpper2("");
    strUpper2.reserve(strLower.length() + 64);
    for (;;)
    {
        RTUNICP             ucLower;
        const char * const  pszPrev   = pszCur;
        RTTESTI_CHECK_RC_BREAK(RTStrGetCpEx(&pszCur, &ucLower), VINF_SUCCESS);
        size_t const        cchSrc    = pszCur - pszPrev;
        if (!ucLower)
            break;

        RTUNICP const       ucUpper   = RTUniCpToUpper(ucLower);
        const char         *pszDstEnd = RTStrPutCp(szDst, ucUpper);
        size_t const        cchDst    = pszDstEnd - &szDst[0];
        RTTESTI_CHECK_MSG(cchSrc >= cchDst,
                          ("ucLower=%#x %u bytes;  ucUpper=%#x %u bytes\n",
                           ucLower, cchSrc, ucUpper, cchDst));
        cch += cchDst;
        strUpper2.appendCodePoint(ucUpper);

        /* roundtrip stability */
        RTUNICP const       ucUpper2  = RTUniCpToUpper(ucUpper);
        RTTESTI_CHECK_MSG(ucUpper2 == ucUpper, ("ucUpper2=%#x ucUpper=%#x\n", ucUpper2, ucUpper));

        RTUNICP const       ucLower2  = RTUniCpToLower(ucUpper);
        RTUNICP const       ucUpper3  = RTUniCpToUpper(ucLower2);
        RTTESTI_CHECK_MSG(ucUpper3 == ucUpper, ("ucUpper3=%#x ucUpper=%#x\n", ucUpper3, ucUpper));

        pszDstEnd = RTStrPutCp(szDst, ucLower2);
        size_t const        cchLower2 = pszDstEnd - &szDst[0];
        RTTESTI_CHECK_MSG(cchDst == cchLower2,
                          ("ucLower2=%#x %u bytes;  ucUpper=%#x %u bytes\n",
                           ucLower2, cchLower2, ucUpper, cchDst));
    }
    RTTESTI_CHECK(strlen(strUpper2.c_str()) == strUpper2.length());
    RTTESTI_CHECK_MSG(cch == strUpper2.length(), ("cch=%u length()=%u\n", cch, strUpper2.length()));

    /* the toUpper method shall do the same thing. */
    strTmp = strLower;      CHECK_EQUAL(strTmp, strLower);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);

    /* Ditto for the upper case string. */
    cch    = 0;
    pszCur = strUpper.c_str();
    iprt::MiniString    strLower2("");
    strLower2.reserve(strUpper.length() + 64);
    for (;;)
    {
        RTUNICP             ucUpper;
        const char * const  pszPrev   = pszCur;
        RTTESTI_CHECK_RC_BREAK(RTStrGetCpEx(&pszCur, &ucUpper), VINF_SUCCESS);
        size_t const        cchSrc    = pszCur - pszPrev;
        if (!ucUpper)
            break;

        RTUNICP const       ucLower   = RTUniCpToLower(ucUpper);
        const char         *pszDstEnd = RTStrPutCp(szDst, ucLower);
        size_t const        cchDst    = pszDstEnd - &szDst[0];
        RTTESTI_CHECK_MSG(cchSrc >= cchDst,
                          ("ucUpper=%#x %u bytes;  ucLower=%#x %u bytes\n",
                           ucUpper, cchSrc, ucLower, cchDst));

        cch += cchDst;
        strLower2.appendCodePoint(ucLower);

        /* roundtrip stability */
        RTUNICP const       ucLower2  = RTUniCpToLower(ucLower);
        RTTESTI_CHECK_MSG(ucLower2 == ucLower, ("ucLower2=%#x ucLower=%#x\n", ucLower2, ucLower));

        RTUNICP const       ucUpper2  = RTUniCpToUpper(ucLower);
        RTUNICP const       ucLower3  = RTUniCpToLower(ucUpper2);
        RTTESTI_CHECK_MSG(ucLower3 == ucLower, ("ucLower3=%#x ucLower=%#x\n", ucLower3, ucLower));

        pszDstEnd = RTStrPutCp(szDst, ucUpper2);
        size_t const        cchUpper2 = pszDstEnd - &szDst[0];
        RTTESTI_CHECK_MSG(cchDst == cchUpper2,
                          ("ucUpper2=%#x %u bytes;  ucLower=%#x %u bytes\n",
                           ucUpper2, cchUpper2, ucLower, cchDst));
    }
    RTTESTI_CHECK(strlen(strLower2.c_str()) == strLower2.length());
    RTTESTI_CHECK_MSG(cch == strLower2.length(), ("cch=%u length()=%u\n", cch, strLower2.length()));

    strTmp = strUpper;      CHECK_EQUAL(strTmp, strUpper);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);

    /* Checks of folding stability when nothing shall change. */
    strTmp = strUpper;      CHECK_EQUAL(strTmp, strUpper);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper);

    strTmp = strUpper2;     CHECK_EQUAL(strTmp, strUpper2);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);

    strTmp = strLower;      CHECK_EQUAL(strTmp, strLower);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower);

    strTmp = strLower2;     CHECK_EQUAL(strTmp, strLower2);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);

    /* Check folding stability for roundtrips. */
    strTmp = strUpper;      CHECK_EQUAL(strTmp, strUpper);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);
    strTmp.toUpper();
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);
    strTmp.toUpper();
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);

    strTmp = strLower;      CHECK_EQUAL(strTmp, strLower);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);
    strTmp.toLower();
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);
    strTmp.toLower();
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);
}


int main()
{
    RTTEST      hTest;
    RTEXITCODE  rcExit = RTTestInitAndCreate("tstIprtMiniString", &hTest);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        RTTestBanner(hTest);

        test1(hTest);
        test2(hTest);

        rcExit = RTTestSummaryAndDestroy(hTest);
    }
    return rcExit;
}

