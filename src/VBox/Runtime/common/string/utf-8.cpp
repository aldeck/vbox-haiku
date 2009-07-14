/* $Id$ */
/** @file
 * IPRT - UTF-8 Decoding.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/string.h>
#include "internal/iprt.h"

#include <iprt/uni.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include "internal/string.h"



/**
 * Get get length in code points of a UTF-8 encoded string.
 * The string is validated while doing this.
 *
 * @returns IPRT status code.
 * @param   psz             Pointer to the UTF-8 string.
 * @param   cch             The max length of the string. (btw cch = cb)
 *                          Use RTSTR_MAX if all of the string is to be examined.
 * @param   pcuc            Where to store the length in unicode code points.
 * @param   pcchActual      Where to store the actual size of the UTF-8 string
 *                          on success (cch = cb again). Optional.
 */
static int rtUtf8Length(const char *psz, size_t cch, size_t *pcuc, size_t *pcchActual)
{
    const unsigned char *puch = (const unsigned char *)psz;
    size_t cCodePoints = 0;
    while (cch > 0)
    {
        const unsigned char uch = *puch;
        if (!uch)
            break;
        if (uch & RT_BIT(7))
        {
            /* figure sequence length and validate the first byte */
            unsigned cb;
            if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5))) == (RT_BIT(7) | RT_BIT(6)))
                cb = 2;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5)))
                cb = 3;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4)))
                cb = 4;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3)))
                cb = 5;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2) | RT_BIT(1))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2)))
                cb = 6;
            else
            {
                RTStrAssertMsgFailed(("Invalid UTF-8 first byte: %.*Rhxs\n", RT_MIN(cch, 10), puch));
                return VERR_INVALID_UTF8_ENCODING;
            }

            /* check length */
            if (cb > cch)
            {
                RTStrAssertMsgFailed(("Invalid UTF-8 length: cb=%d cch=%d (%.*Rhxs)\n", cb, cch, RT_MIN(cch, 10), puch));
                return VERR_INVALID_UTF8_ENCODING;
            }

            /* validate the rest */
            switch (cb)
            {
                case 6:
                    RTStrAssertMsgReturn((puch[5] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("6/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                case 5:
                    RTStrAssertMsgReturn((puch[4] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("5/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                case 4:
                    RTStrAssertMsgReturn((puch[3] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("4/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                case 3:
                    RTStrAssertMsgReturn((puch[2] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("3/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                case 2:
                    RTStrAssertMsgReturn((puch[1] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("2/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                   break;
            }

            /* validate the code point. */
            RTUNICP uc;
            switch (cb)
            {
                case 6:
                    uc =            (puch[5] & 0x3f)
                        | ((RTUNICP)(puch[4] & 0x3f) << 6)
                        | ((RTUNICP)(puch[3] & 0x3f) << 12)
                        | ((RTUNICP)(puch[2] & 0x3f) << 18)
                        | ((RTUNICP)(puch[1] & 0x3f) << 24)
                        | ((RTUNICP)(uch     & 0x01) << 30);
                    RTStrAssertMsgReturn(uc >= 0x04000000 && uc <= 0x7fffffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    break;
                case 5:
                    uc =            (puch[4] & 0x3f)
                        | ((RTUNICP)(puch[3] & 0x3f) << 6)
                        | ((RTUNICP)(puch[2] & 0x3f) << 12)
                        | ((RTUNICP)(puch[1] & 0x3f) << 18)
                        | ((RTUNICP)(uch     & 0x03) << 24);
                    RTStrAssertMsgReturn(uc >= 0x00200000 && uc <= 0x03ffffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    break;
                case 4:
                    uc =            (puch[3] & 0x3f)
                        | ((RTUNICP)(puch[2] & 0x3f) << 6)
                        | ((RTUNICP)(puch[1] & 0x3f) << 12)
                        | ((RTUNICP)(uch     & 0x07) << 18);
                    RTStrAssertMsgReturn(uc >= 0x00010000 && uc <= 0x001fffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    break;
                case 3:
                    uc =            (puch[2] & 0x3f)
                        | ((RTUNICP)(puch[1] & 0x3f) << 6)
                        | ((RTUNICP)(uch     & 0x0f) << 12);
                    RTStrAssertMsgReturn(uc >= 0x00000800 && uc <= 0x0000fffd,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch),
                                         uc == 0xffff || uc == 0xfffe ? VERR_CODE_POINT_ENDIAN_INDICATOR : VERR_INVALID_UTF8_ENCODING);
                    RTStrAssertMsgReturn(uc < 0xd800 || uc > 0xdfff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_CODE_POINT_SURROGATE);
                    break;
                case 2:
                    uc =            (puch[1] & 0x3f)
                        | ((RTUNICP)(uch     & 0x1f) << 6);
                    RTStrAssertMsgReturn(uc >= 0x00000080 && uc <= 0x000007ff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    break;
            }

            /* advance */
            cch -= cb;
            puch += cb;
        }
        else
        {
             /* one ASCII byte */
            puch++;
            cch--;
        }
        cCodePoints++;
    }

    /* done */
    *pcuc = cCodePoints;
    if (pcchActual)
        *pcchActual = puch - (unsigned char const *)psz;
    return VINF_SUCCESS;
}


/**
 * Decodes and UTF-8 string into an array of unicode code point.
 *
 * Since we know the input is valid, we do *not* perform encoding or length checks.
 *
 * @returns iprt status code.
 * @param   psz     The UTF-8 string to recode. This is a valid encoding.
 * @param   cch     The number of chars (the type char, so bytes if you like) to process of the UTF-8 string.
 *                  The recoding will stop when cch or '\\0' is reached. Pass RTSTR_MAX to process up to '\\0'.
 * @param   paCps   Where to store the code points array.
 * @param   cCps    The number of RTUNICP items the paCps buffer can hold, excluding the terminator ('\\0').
 * @param   pcCps   Where to store the actual number of decoded code points. This excludes the terminator.
 */
static int rtUtf8Decode(const char *psz, size_t cch, PRTUNICP paCps, size_t cCps, size_t *pcCps)
{
    int                     rc = VINF_SUCCESS;
    const unsigned char    *puch = (const unsigned char *)psz;
    const PRTUNICP          pCpEnd = paCps + cCps;
    PRTUNICP                pCp = paCps;
    Assert(pCpEnd >= pCp);
    while (cch > 0)
    {
        /* read the next char and check for terminator. */
        const unsigned char uch = *puch;
        if (!uch)
            break;

        /* check for output overflow */
        if (pCp >= pCpEnd)
        {
            rc = VERR_BUFFER_OVERFLOW;
            break;
        }

        /* decode and recode the code point */
        if (!(uch & RT_BIT(7)))
        {
            *pCp++ = uch;
            puch++;
            cch--;
        }
#ifdef RT_STRICT
        else if (!(uch & RT_BIT(6)))
            AssertMsgFailed(("Internal error!\n"));
#endif
        else if (!(uch & RT_BIT(5)))
        {
            *pCp++ = (puch[1] & 0x3f)
                   | ((uint16_t)(uch     & 0x1f) << 6);
            puch += 2;
            cch -= 2;
        }
        else if (!(uch & RT_BIT(4)))
        {
            *pCp++ = (puch[2] & 0x3f)
                   | ((uint16_t)(puch[1] & 0x3f) << 6)
                   | ((uint16_t)(uch     & 0x0f) << 12);
            puch += 3;
            cch -= 3;
        }
        else if (!(uch & RT_BIT(3)))
        {
            *pCp++ = (puch[3] & 0x3f)
                   | ((RTUNICP)(puch[2] & 0x3f) << 6)
                   | ((RTUNICP)(puch[1] & 0x3f) << 12)
                   | ((RTUNICP)(uch     & 0x07) << 18);
            puch += 4;
            cch -= 4;
        }
        else if (!(uch & RT_BIT(2)))
        {
            *pCp++ = (puch[4] & 0x3f)
                   | ((RTUNICP)(puch[3] & 0x3f) << 6)
                   | ((RTUNICP)(puch[2] & 0x3f) << 12)
                   | ((RTUNICP)(puch[1] & 0x3f) << 18)
                   | ((RTUNICP)(uch     & 0x03) << 24);
            puch += 5;
            cch -= 6;
        }
        else
        {
            Assert(!(uch & RT_BIT(1)));
            *pCp++ = (puch[5] & 0x3f)
                   | ((RTUNICP)(puch[4] & 0x3f) << 6)
                   | ((RTUNICP)(puch[3] & 0x3f) << 12)
                   | ((RTUNICP)(puch[2] & 0x3f) << 18)
                   | ((RTUNICP)(puch[1] & 0x3f) << 24)
                   | ((RTUNICP)(uch     & 0x01) << 30);
            puch += 6;
            cch -= 6;
        }
    }

    /* done */
    *pCp = 0;
    *pcCps = pCp - paCps;
    return rc;
}


RTDECL(size_t) RTStrUniLen(const char *psz)
{
    size_t cCodePoints;
    int rc = rtUtf8Length(psz, RTSTR_MAX, &cCodePoints, NULL);
    return RT_SUCCESS(rc) ? cCodePoints : 0;
}
RT_EXPORT_SYMBOL(RTStrUniLen);


RTDECL(int) RTStrUniLenEx(const char *psz, size_t cch, size_t *pcCps)
{
    size_t cCodePoints;
    int rc = rtUtf8Length(psz, cch, &cCodePoints, NULL);
    if (pcCps)
        *pcCps = RT_SUCCESS(rc) ? cCodePoints : 0;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrUniLenEx);


RTDECL(int) RTStrValidateEncoding(const char *psz)
{
    return RTStrValidateEncodingEx(psz, RTSTR_MAX, 0);
}
RT_EXPORT_SYMBOL(RTStrValidateEncoding);


RTDECL(int) RTStrValidateEncodingEx(const char *psz, size_t cch, uint32_t fFlags)
{
    AssertReturn(!(fFlags & ~(RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED)), VERR_INVALID_PARAMETER);
    AssertPtr(psz);

    /*
     * Use rtUtf8Length for the job.
     */
    size_t cchActual;
    size_t cCpsIgnored;
    int rc = rtUtf8Length(psz, cch, &cCpsIgnored, &cchActual);
    if (RT_SUCCESS(rc))
    {
        if (    (fFlags & RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED)
            &&  cchActual >= cch)
            rc = VERR_BUFFER_OVERFLOW;
    }
    return rc;


    return RTStrUniLenEx(psz, cch, &cCpsIgnored);
}
RT_EXPORT_SYMBOL(RTStrValidateEncodingEx);


RTDECL(bool) RTStrIsValidEncoding(const char *psz)
{
    int rc = RTStrValidateEncodingEx(psz, RTSTR_MAX, 0);
    return RT_SUCCESS(rc);
}
RT_EXPORT_SYMBOL(RTStrIsValidEncoding);


RTDECL(int) RTStrToUni(const char *pszString, PRTUNICP *ppaCps)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(pszString));
    Assert(VALID_PTR(ppaCps));
    *ppaCps = NULL;

    /*
     * Validate the UTF-8 input and count its code points.
     */
    size_t cCps;
    int rc = rtUtf8Length(pszString, RTSTR_MAX, &cCps, NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate buffer.
         */
        PRTUNICP paCps = (PRTUNICP)RTMemAlloc((cCps + 1) * sizeof(RTUNICP));
        if (paCps)
        {
            /*
             * Decode the string.
             */
            rc = rtUtf8Decode(pszString, RTSTR_MAX, paCps, cCps, &cCps);
            if (RT_SUCCESS(rc))
            {
                *ppaCps = paCps;
                return rc;
            }
            RTMemFree(paCps);
        }
        else
            rc = VERR_NO_CODE_POINT_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUni);


RTDECL(int)  RTStrToUniEx(const char *pszString, size_t cchString, PRTUNICP *ppaCps, size_t cCps, size_t *pcCps)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(pszString));
    Assert(VALID_PTR(ppaCps));
    Assert(!pcCps || VALID_PTR(pcCps));

    /*
     * Validate the UTF-8 input and count the code points.
     */
    size_t cCpsResult;
    int rc = rtUtf8Length(pszString, cchString, &cCpsResult, NULL);
    if (RT_SUCCESS(rc))
    {
        if (pcCps)
            *pcCps = cCpsResult;

        /*
         * Check buffer size / Allocate buffer.
         */
        bool fShouldFree;
        PRTUNICP paCpsResult;
        if (cCps > 0 && *ppaCps)
        {
            fShouldFree = false;
            if (cCps <= cCpsResult)
                return VERR_BUFFER_OVERFLOW;
            paCpsResult = *ppaCps;
        }
        else
        {
            *ppaCps = NULL;
            fShouldFree = true;
            cCps = RT_MAX(cCpsResult + 1, cCps);
            paCpsResult = (PRTUNICP)RTMemAlloc(cCps * sizeof(RTUNICP));
        }
        if (paCpsResult)
        {
            /*
             * Encode the UTF-16 string.
             */
            rc = rtUtf8Decode(pszString, cchString, paCpsResult, cCps - 1, &cCpsResult);
            if (RT_SUCCESS(rc))
            {
                *ppaCps = paCpsResult;
                return rc;
            }
            if (fShouldFree)
                RTMemFree(paCpsResult);
        }
        else
            rc = VERR_NO_CODE_POINT_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUniEx);


/**
 * Calculates the UTF-16 length of a string, validating the encoding while doing so.
 *
 * @returns IPRT status code.
 * @param   psz     Pointer to the UTF-8 string.
 * @param   cch     The max length of the string. (btw cch = cb)
 *                  Use RTSTR_MAX if all of the string is to be examined.s
 * @param   pcwc    Where to store the length of the UTF-16 string as a number of RTUTF16 characters.
 */
static int rtUtf8CalcUtf16Length(const char *psz, size_t cch, size_t *pcwc)
{
    const unsigned char *puch = (const unsigned char *)psz;
    size_t cwc = 0;
    while (cch > 0)
    {
        const unsigned char uch = *puch;
        if (!uch)
            break;
        if (!(uch & RT_BIT(7)))
        {
             /* one ASCII byte */
            cwc++;
            puch++;
            cch--;
        }
        else
        {
            /* figure sequence length and validate the first byte */
            unsigned cb;
            if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5))) == (RT_BIT(7) | RT_BIT(6)))
                cb = 2;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5)))
                cb = 3;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4)))
                cb = 4;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3)))
                cb = 5;
            else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2) | RT_BIT(1))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3) | RT_BIT(2)))
                cb = 6;
            else
            {
                RTStrAssertMsgFailed(("Invalid UTF-8 first byte: %.*Rhxs\n", RT_MIN(cch, 10), puch));
                return VERR_INVALID_UTF8_ENCODING;
            }

            /* check length */
            if (cb > cch)
            {
                RTStrAssertMsgFailed(("Invalid UTF-8 length: cb=%d cch=%d (%.*Rhxs)\n", cb, cch, RT_MIN(cch, 10), puch));
                return VERR_INVALID_UTF8_ENCODING;
            }

            /* validate the rest */
            switch (cb)
            {
                case 6:
                    RTStrAssertMsgReturn((puch[5] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("6/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                case 5:
                    RTStrAssertMsgReturn((puch[4] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("5/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                case 4:
                    RTStrAssertMsgReturn((puch[3] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("4/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                case 3:
                    RTStrAssertMsgReturn((puch[2] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("3/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                case 2:
                    RTStrAssertMsgReturn((puch[1] & (RT_BIT(7) | RT_BIT(6))) == RT_BIT(7), ("2/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                   break;
            }

            /* validate the code point. */
            RTUNICP uc;
            switch (cb)
            {
                case 6:
                    uc =            (puch[5] & 0x3f)
                        | ((RTUNICP)(puch[4] & 0x3f) << 6)
                        | ((RTUNICP)(puch[3] & 0x3f) << 12)
                        | ((RTUNICP)(puch[2] & 0x3f) << 18)
                        | ((RTUNICP)(puch[1] & 0x3f) << 24)
                        | ((RTUNICP)(uch     & 0x01) << 30);
                    RTStrAssertMsgReturn(uc >= 0x04000000 && uc <= 0x7fffffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RTStrAssertMsgFailed(("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch));
                    return VERR_CANT_RECODE_AS_UTF16;
                case 5:
                    uc =            (puch[4] & 0x3f)
                        | ((RTUNICP)(puch[3] & 0x3f) << 6)
                        | ((RTUNICP)(puch[2] & 0x3f) << 12)
                        | ((RTUNICP)(puch[1] & 0x3f) << 18)
                        | ((RTUNICP)(uch     & 0x03) << 24);
                    RTStrAssertMsgReturn(uc >= 0x00200000 && uc <= 0x03ffffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RTStrAssertMsgFailed(("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch));
                    return VERR_CANT_RECODE_AS_UTF16;
                case 4:
                    uc =            (puch[3] & 0x3f)
                        | ((RTUNICP)(puch[2] & 0x3f) << 6)
                        | ((RTUNICP)(puch[1] & 0x3f) << 12)
                        | ((RTUNICP)(uch     & 0x07) << 18);
                    RTStrAssertMsgReturn(uc >= 0x00010000 && uc <= 0x001fffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    RTStrAssertMsgReturn(uc <= 0x0010ffff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_CANT_RECODE_AS_UTF16);
                    cwc++;
                    break;
                case 3:
                    uc =            (puch[2] & 0x3f)
                        | ((RTUNICP)(puch[1] & 0x3f) << 6)
                        | ((RTUNICP)(uch     & 0x0f) << 12);
                    RTStrAssertMsgReturn(uc >= 0x00000800 && uc <= 0x0000fffd,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch),
                                         uc == 0xffff || uc == 0xfffe ? VERR_CODE_POINT_ENDIAN_INDICATOR : VERR_INVALID_UTF8_ENCODING);
                    RTStrAssertMsgReturn(uc < 0xd800 || uc > 0xdfff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_CODE_POINT_SURROGATE);
                    break;
                case 2:
                    uc =            (puch[1] & 0x3f)
                        | ((RTUNICP)(uch     & 0x1f) << 6);
                    RTStrAssertMsgReturn(uc >= 0x00000080 && uc <= 0x000007ff,
                                         ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, cch), puch), VERR_INVALID_UTF8_ENCODING);
                    break;
            }

            /* advance */
            cch -= cb;
            puch += cb;
            cwc++;
        }
    }

    /* done */
    *pcwc = cwc;
    return VINF_SUCCESS;
}


/**
 * Recodes a valid UTF-8 string as UTF-16.
 *
 * Since we know the input is valid, we do *not* perform encoding or length checks.
 *
 * @returns iprt status code.
 * @param   psz     The UTF-8 string to recode. This is a valid encoding.
 * @param   cch     The number of chars (the type char, so bytes if you like) to process of the UTF-8 string.
 *                  The recoding will stop when cch or '\\0' is reached. Pass RTSTR_MAX to process up to '\\0'.
 * @param   pwsz    Where to store the UTF-16 string.
 * @param   cwc     The number of RTUTF16 items the pwsz buffer can hold, excluding the terminator ('\\0').
 * @param   pcwc    Where to store the actual number of RTUTF16 items encoded into the UTF-16. This excludes the terminator.
 */
static int rtUtf8RecodeAsUtf16(const char *psz, size_t cch, PRTUTF16 pwsz, size_t cwc, size_t *pcwc)
{
    int                     rc = VINF_SUCCESS;
    const unsigned char    *puch = (const unsigned char *)psz;
    const PRTUTF16          pwszEnd = pwsz + cwc;
    PRTUTF16                pwc = pwsz;
    Assert(pwszEnd >= pwc);
    while (cch > 0)
    {
        /* read the next char and check for terminator. */
        const unsigned char uch = *puch;
        if (!uch)
            break;

        /* check for output overflow */
        if (pwc >= pwszEnd)
        {
            rc = VERR_BUFFER_OVERFLOW;
            break;
        }

        /* decode and recode the code point */
        if (!(uch & RT_BIT(7)))
        {
            *pwc++ = uch;
            puch++;
            cch--;
        }
        else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5))) == (RT_BIT(7) | RT_BIT(6)))
        {
            uint16_t uc = (puch[1] & 0x3f)
                    | ((uint16_t)(uch     & 0x1f) << 6);
            *pwc++ = uc;
            puch += 2;
            cch -= 2;
        }
        else if ((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5)))
        {
            uint16_t uc = (puch[2] & 0x3f)
                    | ((uint16_t)(puch[1] & 0x3f) << 6)
                    | ((uint16_t)(uch     & 0x0f) << 12);
            *pwc++ = uc;
            puch += 3;
            cch -= 3;
        }
        else
        {
            /* generate surrugate pair */
            Assert((uch & (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4) | RT_BIT(3))) == (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4)));
            RTUNICP uc =           (puch[3] & 0x3f)
                       | ((RTUNICP)(puch[2] & 0x3f) << 6)
                       | ((RTUNICP)(puch[1] & 0x3f) << 12)
                       | ((RTUNICP)(uch     & 0x07) << 18);
            if (pwc + 1 >= pwszEnd)
            {
                rc = VERR_BUFFER_OVERFLOW;
                break;
            }
            uc -= 0x10000;
            *pwc++ = 0xd800 | (uc >> 10);
            *pwc++ = 0xdc00 | (uc & 0x3ff);
            puch += 4;
            cch -= 4;
        }
    }

    /* done */
    *pwc = '\0';
    *pcwc = pwc - pwsz;
    return rc;
}


RTDECL(int) RTStrToUtf16(const char *pszString, PRTUTF16 *ppwszString)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(ppwszString));
    Assert(VALID_PTR(pszString));
    *ppwszString = NULL;

    /*
     * Validate the UTF-8 input and calculate the length of the UTF-16 string.
     */
    size_t cwc;
    int rc = rtUtf8CalcUtf16Length(pszString, RTSTR_MAX, &cwc);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate buffer.
         */
        PRTUTF16 pwsz = (PRTUTF16)RTMemAlloc((cwc + 1) * sizeof(RTUTF16));
        if (pwsz)
        {
            /*
             * Encode the UTF-16 string.
             */
            rc = rtUtf8RecodeAsUtf16(pszString, RTSTR_MAX, pwsz, cwc, &cwc);
            if (RT_SUCCESS(rc))
            {
                *ppwszString = pwsz;
                return rc;
            }
            RTMemFree(pwsz);
        }
        else
            rc = VERR_NO_UTF16_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUtf16);


RTDECL(int)  RTStrToUtf16Ex(const char *pszString, size_t cchString, PRTUTF16 *ppwsz, size_t cwc, size_t *pcwc)
{
    /*
     * Validate input.
     */
    Assert(VALID_PTR(pszString));
    Assert(VALID_PTR(ppwsz));
    Assert(!pcwc || VALID_PTR(pcwc));

    /*
     * Validate the UTF-8 input and calculate the length of the UTF-16 string.
     */
    size_t cwcResult;
    int rc = rtUtf8CalcUtf16Length(pszString, cchString, &cwcResult);
    if (RT_SUCCESS(rc))
    {
        if (pcwc)
            *pcwc = cwcResult;

        /*
         * Check buffer size / Allocate buffer.
         */
        bool fShouldFree;
        PRTUTF16 pwszResult;
        if (cwc > 0 && *ppwsz)
        {
            fShouldFree = false;
            if (cwc <= cwcResult)
                return VERR_BUFFER_OVERFLOW;
            pwszResult = *ppwsz;
        }
        else
        {
            *ppwsz = NULL;
            fShouldFree = true;
            cwc = RT_MAX(cwcResult + 1, cwc);
            pwszResult = (PRTUTF16)RTMemAlloc(cwc * sizeof(RTUTF16));
        }
        if (pwszResult)
        {
            /*
             * Encode the UTF-16 string.
             */
            rc = rtUtf8RecodeAsUtf16(pszString, cchString, pwszResult, cwc - 1, &cwcResult);
            if (RT_SUCCESS(rc))
            {
                *ppwsz = pwszResult;
                return rc;
            }
            if (fShouldFree)
                RTMemFree(pwszResult);
        }
        else
            rc = VERR_NO_UTF16_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTStrToUtf16Ex);


RTDECL(size_t) RTStrCalcUtf16Len(const char *psz)
{
    size_t cwc;
    int rc = rtUtf8CalcUtf16Length(psz, RTSTR_MAX, &cwc);
    return RT_SUCCESS(rc) ? cwc : 0;
}
RT_EXPORT_SYMBOL(RTStrCalcUtf16Len);


RTDECL(int) RTStrCalcUtf16LenEx(const char *psz, size_t cch, size_t *pcwc)
{
    size_t cwc;
    int rc = rtUtf8CalcUtf16Length(psz, cch, &cwc);
    if (pcwc)
        *pcwc = RT_SUCCESS(rc) ? cwc : ~(size_t)0;
    return rc;
}
RT_EXPORT_SYMBOL(RTStrCalcUtf16LenEx);


/**
 * Handle invalid encodings passed to RTStrGetCp() and RTStrGetCpEx().
 * @returns rc
 * @param   ppsz        The pointer to the string position point.
 * @param   pCp         Where to store RTUNICP_INVALID.
 * @param   rc          The iprt error code.
 */
static int rtStrGetCpExFailure(const char **ppsz, PRTUNICP pCp, int rc)
{
    /*
     * Try find a valid encoding.
     */
    (*ppsz)++; /** @todo code this! */
    *pCp = RTUNICP_INVALID;
    return rc;
}


RTDECL(RTUNICP) RTStrGetCpInternal(const char *psz)
{
    RTUNICP Cp;
    RTStrGetCpExInternal(&psz, &Cp);
    return Cp;
}
RT_EXPORT_SYMBOL(RTStrGetCpInternal);


RTDECL(int) RTStrGetCpExInternal(const char **ppsz, PRTUNICP pCp)
{
    const unsigned char *puch = (const unsigned char *)*ppsz;
    const unsigned char uch = *puch;
    RTUNICP             uc;

    /* ASCII ? */
    if (!(uch & RT_BIT(7)))
    {
        uc = uch;
        puch++;
    }
    else if (uch & RT_BIT(6))
    {
        /* figure the length and validate the first octet. */
        unsigned cb;
        if (!(uch & RT_BIT(5)))
            cb = 2;
        else if (!(uch & RT_BIT(4)))
            cb = 3;
        else if (!(uch & RT_BIT(3)))
            cb = 4;
        else if (!(uch & RT_BIT(2)))
            cb = 5;
        else if (!(uch & RT_BIT(1)))
            cb = 6;
        else
        {
            RTStrAssertMsgFailed(("Invalid UTF-8 first byte: %.*Rhxs\n", RT_MIN(strlen((char *)puch), 10), puch));
            return rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING);
        }

        /* validate the rest */
        switch (cb)
        {
            case 6:
                RTStrAssertMsgReturn((puch[5] & 0xc0) == 0x80, ("6/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
            case 5:
                RTStrAssertMsgReturn((puch[4] & 0xc0) == 0x80, ("5/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
            case 4:
                RTStrAssertMsgReturn((puch[3] & 0xc0) == 0x80, ("4/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
            case 3:
                RTStrAssertMsgReturn((puch[2] & 0xc0) == 0x80, ("3/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
            case 2:
                RTStrAssertMsgReturn((puch[1] & 0xc0) == 0x80, ("2/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
               break;
        }

        /* get and validate the code point. */
        switch (cb)
        {
            case 6:
                uc =            (puch[5] & 0x3f)
                    | ((RTUNICP)(puch[4] & 0x3f) << 6)
                    | ((RTUNICP)(puch[3] & 0x3f) << 12)
                    | ((RTUNICP)(puch[2] & 0x3f) << 18)
                    | ((RTUNICP)(puch[1] & 0x3f) << 24)
                    | ((RTUNICP)(uch     & 0x01) << 30);
                RTStrAssertMsgReturn(uc >= 0x04000000 && uc <= 0x7fffffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            case 5:
                uc =            (puch[4] & 0x3f)
                    | ((RTUNICP)(puch[3] & 0x3f) << 6)
                    | ((RTUNICP)(puch[2] & 0x3f) << 12)
                    | ((RTUNICP)(puch[1] & 0x3f) << 18)
                    | ((RTUNICP)(uch     & 0x03) << 24);
                RTStrAssertMsgReturn(uc >= 0x00200000 && uc <= 0x03ffffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            case 4:
                uc =            (puch[3] & 0x3f)
                    | ((RTUNICP)(puch[2] & 0x3f) << 6)
                    | ((RTUNICP)(puch[1] & 0x3f) << 12)
                    | ((RTUNICP)(uch     & 0x07) << 18);
                RTStrAssertMsgReturn(uc >= 0x00010000 && uc <= 0x001fffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            case 3:
                uc =            (puch[2] & 0x3f)
                    | ((RTUNICP)(puch[1] & 0x3f) << 6)
                    | ((RTUNICP)(uch     & 0x0f) << 12);
                RTStrAssertMsgReturn(uc >= 0x00000800 && uc <= 0x0000fffd,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, uc == 0xffff || uc == 0xfffe ? VERR_CODE_POINT_ENDIAN_INDICATOR : VERR_INVALID_UTF8_ENCODING));
                RTStrAssertMsgReturn(uc < 0xd800 || uc > 0xdfff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_CODE_POINT_SURROGATE));
                break;
            case 2:
                uc =            (puch[1] & 0x3f)
                    | ((RTUNICP)(uch     & 0x1f) << 6);
                RTStrAssertMsgReturn(uc >= 0x00000080 && uc <= 0x000007ff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            default: /* impossible, but GCC is bitching. */
                uc = RTUNICP_INVALID;
                break;
        }
        puch += cb;
    }
    else
    {
        /* 6th bit is always set. */
        RTStrAssertMsgFailed(("Invalid UTF-8 first byte: %.*Rhxs\n", RT_MIN(strlen((char *)puch), 10), puch));
        return rtStrGetCpExFailure(ppsz, pCp, VERR_INVALID_UTF8_ENCODING);
    }
    *pCp = uc;
    *ppsz = (const char *)puch;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTStrGetCpExInternal);


/**
 * Handle invalid encodings passed to RTStrGetCpNEx().
 * @returns rc
 * @param   ppsz        The pointer to the string position point.
 * @param   pcch        Pointer to the string length.
 * @param   pCp         Where to store RTUNICP_INVALID.
 * @param   rc          The iprt error code.
 */
static int rtStrGetCpNExFailure(const char **ppsz, size_t *pcch, PRTUNICP pCp, int rc)
{
    /*
     * Try find a valid encoding.
     */
    (*ppsz)++; /** @todo code this! */
    (*pcch)--;
    *pCp = RTUNICP_INVALID;
    return rc;
}


RTDECL(int) RTStrGetCpNExInternal(const char **ppsz, size_t *pcch, PRTUNICP pCp)
{
    const unsigned char *puch = (const unsigned char *)*ppsz;
    const unsigned char uch = *puch;
    size_t              cch = *pcch;
    RTUNICP             uc;

    if (cch == 0)
    {
        *pCp = RTUNICP_INVALID;
        return VERR_END_OF_STRING;
    }

    /* ASCII ? */
    if (!(uch & RT_BIT(7)))
    {
        uc = uch;
        puch++;
        cch--;
    }
    else if (uch & RT_BIT(6))
    {
        /* figure the length and validate the first octet. */
        unsigned cb;
        if (!(uch & RT_BIT(5)))
            cb = 2;
        else if (!(uch & RT_BIT(4)))
            cb = 3;
        else if (!(uch & RT_BIT(3)))
            cb = 4;
        else if (!(uch & RT_BIT(2)))
            cb = 5;
        else if (!(uch & RT_BIT(1)))
            cb = 6;
        else
        {
            RTStrAssertMsgFailed(("Invalid UTF-8 first byte: %.*Rhxs\n", RT_MIN(strlen((char *)puch), 10), puch));
            return rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING);
        }

        if (cb > cch)
            return rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING);

        /* validate the rest */
        switch (cb)
        {
            case 6:
                RTStrAssertMsgReturn((puch[5] & 0xc0) == 0x80, ("6/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
            case 5:
                RTStrAssertMsgReturn((puch[4] & 0xc0) == 0x80, ("5/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
            case 4:
                RTStrAssertMsgReturn((puch[3] & 0xc0) == 0x80, ("4/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
            case 3:
                RTStrAssertMsgReturn((puch[2] & 0xc0) == 0x80, ("3/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
            case 2:
                RTStrAssertMsgReturn((puch[1] & 0xc0) == 0x80, ("2/%u: %.*Rhxs\n", cb, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
               break;
        }

        /* get and validate the code point. */
        switch (cb)
        {
            case 6:
                uc =            (puch[5] & 0x3f)
                    | ((RTUNICP)(puch[4] & 0x3f) << 6)
                    | ((RTUNICP)(puch[3] & 0x3f) << 12)
                    | ((RTUNICP)(puch[2] & 0x3f) << 18)
                    | ((RTUNICP)(puch[1] & 0x3f) << 24)
                    | ((RTUNICP)(uch     & 0x01) << 30);
                RTStrAssertMsgReturn(uc >= 0x04000000 && uc <= 0x7fffffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            case 5:
                uc =            (puch[4] & 0x3f)
                    | ((RTUNICP)(puch[3] & 0x3f) << 6)
                    | ((RTUNICP)(puch[2] & 0x3f) << 12)
                    | ((RTUNICP)(puch[1] & 0x3f) << 18)
                    | ((RTUNICP)(uch     & 0x03) << 24);
                RTStrAssertMsgReturn(uc >= 0x00200000 && uc <= 0x03ffffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            case 4:
                uc =            (puch[3] & 0x3f)
                    | ((RTUNICP)(puch[2] & 0x3f) << 6)
                    | ((RTUNICP)(puch[1] & 0x3f) << 12)
                    | ((RTUNICP)(uch     & 0x07) << 18);
                RTStrAssertMsgReturn(uc >= 0x00010000 && uc <= 0x001fffff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            case 3:
                uc =            (puch[2] & 0x3f)
                    | ((RTUNICP)(puch[1] & 0x3f) << 6)
                    | ((RTUNICP)(uch     & 0x0f) << 12);
                RTStrAssertMsgReturn(uc >= 0x00000800 && uc <= 0x0000fffd,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, uc == 0xffff || uc == 0xfffe ? VERR_CODE_POINT_ENDIAN_INDICATOR : VERR_INVALID_UTF8_ENCODING));
                RTStrAssertMsgReturn(uc < 0xd800 || uc > 0xdfff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_CODE_POINT_SURROGATE));
                break;
            case 2:
                uc =            (puch[1] & 0x3f)
                    | ((RTUNICP)(uch     & 0x1f) << 6);
                RTStrAssertMsgReturn(uc >= 0x00000080 && uc <= 0x000007ff,
                                     ("%u: cp=%#010RX32: %.*Rhxs\n", cb, uc, RT_MIN(cb + 10, strlen((char *)puch)), puch),
                                     rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING));
                break;
            default: /* impossible, but GCC is bitching. */
                uc = RTUNICP_INVALID;
                break;
        }
        puch += cb;
        cch  -= cb;
    }
    else
    {
        /* 6th bit is always set. */
        RTStrAssertMsgFailed(("Invalid UTF-8 first byte: %.*Rhxs\n", RT_MIN(strlen((char *)puch), 10), puch));
        return rtStrGetCpNExFailure(ppsz, pcch, pCp, VERR_INVALID_UTF8_ENCODING);
    }
    *pCp = uc;
    *ppsz = (const char *)puch;
    (*pcch) = cch;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTStrGetCpNExInternal);


RTDECL(char *) RTStrPutCpInternal(char *psz, RTUNICP uc)
{
    unsigned char *puch = (unsigned char *)psz;
    if (uc < 0x80)
        *puch++ = (unsigned char )uc;
    else if (uc < 0x00000800)
    {
        *puch++ = 0xc0 | (uc >> 6);
        *puch++ = 0x80 | (uc & 0x3f);
    }
    else if (uc < 0x00010000)
    {
        if (   uc < 0x0000d8000
             || (   uc > 0x0000dfff
                 && uc < 0x0000fffe))
        {
            *puch++ = 0xe0 | (uc >> 12);
            *puch++ = 0x80 | ((uc >> 6) & 0x3f);
            *puch++ = 0x80 | (uc & 0x3f);
        }
        else
        {
            AssertMsgFailed(("Invalid code point U+%05x!\n", uc));
            *puch++ = 0x7f;
        }
    }
    else if (uc < 0x00200000)
    {
        *puch++ = 0xf0 | (uc >> 18);
        *puch++ = 0x80 | ((uc >> 12) & 0x3f);
        *puch++ = 0x80 | ((uc >> 6) & 0x3f);
        *puch++ = 0x80 | (uc & 0x3f);
    }
    else if (uc < 0x04000000)
    {
        *puch++ = 0xf1 | (uc >> 24);
        *puch++ = 0x80 | ((uc >> 18) & 0x3f);
        *puch++ = 0x80 | ((uc >> 12) & 0x3f);
        *puch++ = 0x80 | ((uc >> 6) & 0x3f);
        *puch++ = 0x80 | (uc & 0x3f);
    }
    else if (uc <= 0x7fffffff)
    {
        *puch++ = 0xf3 | (uc >> 30);
        *puch++ = 0x80 | ((uc >> 24) & 0x3f);
        *puch++ = 0x80 | ((uc >> 18) & 0x3f);
        *puch++ = 0x80 | ((uc >> 12) & 0x3f);
        *puch++ = 0x80 | ((uc >> 6) & 0x3f);
        *puch++ = 0x80 | (uc & 0x3f);
    }
    else
    {
        AssertMsgFailed(("Invalid code point U+%08x!\n", uc));
        *puch++ = 0x7f;
    }

    return (char *)puch;
}
RT_EXPORT_SYMBOL(RTStrPutCpInternal);


RTDECL(char *) RTStrPrevCp(const char *pszStart, const char *psz)
{
    if (pszStart < psz)
    {
        /* simple char? */
        const unsigned char *puch = (const unsigned char *)psz;
        unsigned uch = *--puch;
        if (!(uch & RT_BIT(7)))
            return (char *)puch;
        RTStrAssertMsgReturn(!(uch & RT_BIT(6)), ("uch=%#x\n", uch), (char *)pszStart);

        /* two or more. */
        uint32_t uMask = 0xffffffc0;
        while (     (const unsigned char *)pszStart < puch
               &&   !(uMask & 1))
        {
            unsigned uch = *--puch;
            if ((uch & 0xc0) != 0x80)
            {
                RTStrAssertMsgReturn((uch & (uMask >> 1)) == (uMask & 0xff),
                                     ("Invalid UTF-8 encoding: %.*Rhxs puch=%p psz=%p\n", psz - (char *)puch, puch, psz),
                                     (char *)pszStart);
                return (char *)puch;
            }
            uMask >>= 1;
        }
        RTStrAssertMsgFailed(("Invalid UTF-8 encoding: %.*Rhxs puch=%p psz=%p\n", psz - (char *)puch, puch, psz));
    }
    return (char *)pszStart;
}
RT_EXPORT_SYMBOL(RTStrPrevCp);


/**
 * Performs a case sensitive string compare between two UTF-8 strings.
 *
 * Encoding errors are ignored by the current implementation. So, the only
 * difference between this and the CRT strcmp function is the handling of
 * NULL arguments.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   psz1        First UTF-8 string. Null is allowed.
 * @param   psz2        Second UTF-8 string. Null is allowed.
 */
RTDECL(int) RTStrCmp(const char *psz1, const char *psz2)
{
    if (psz1 == psz2)
        return 0;
    if (!psz1)
        return -1;
    if (!psz2)
        return 1;

    return strcmp(psz1, psz2);
}
RT_EXPORT_SYMBOL(RTStrCmp);


/**
 * Performs a case sensitive string compare between two UTF-8 strings, given
 * a maximum string length.
 *
 * Encoding errors are ignored by the current implementation. So, the only
 * difference between this and the CRT strncmp function is the handling of
 * NULL arguments.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   psz1        First UTF-8 string. Null is allowed.
 * @param   psz2        Second UTF-8 string. Null is allowed.
 * @param   cchMax      The maximum string length
 */
RTDECL(int) RTStrNCmp(const char *psz1, const char *psz2, size_t cchMax)
{
    if (psz1 == psz2)
        return 0;
    if (!psz1)
        return -1;
    if (!psz2)
        return 1;

    return strncmp(psz1, psz2, cchMax);
}
RT_EXPORT_SYMBOL(RTStrNCmp);


/**
 * Performs a case insensitive string compare between two UTF-8 strings.
 *
 * This is a simplified compare, as only the simplified lower/upper case folding
 * specified by the unicode specs are used. It does not consider character pairs
 * as they are used in some languages, just simple upper & lower case compares.
 *
 * The result is the difference between the mismatching codepoints after they
 * both have been lower cased.
 *
 * If the string encoding is invalid the function will assert (strict builds)
 * and use RTStrCmp for the remainder of the string.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   psz1        First UTF-8 string. Null is allowed.
 * @param   psz2        Second UTF-8 string. Null is allowed.
 */
RTDECL(int) RTStrICmp(const char *psz1, const char *psz2)
{
    if (psz1 == psz2)
        return 0;
    if (!psz1)
        return -1;
    if (!psz2)
        return 1;

    const char *pszStart1 = psz1;
    for (;;)
    {
        /* Get the codepoints */
        RTUNICP cp1;
        int rc = RTStrGetCpEx(&psz1, &cp1);
        if (RT_FAILURE(rc))
        {
            AssertRC(rc);
            psz1--;
            break;
        }

        RTUNICP cp2;
        rc = RTStrGetCpEx(&psz2, &cp2);
        if (RT_FAILURE(rc))
        {
            AssertRC(rc);
            psz2--;
            psz1 = RTStrPrevCp(pszStart1, psz1);
            break;
        }

        /* compare */
        int iDiff = cp1 - cp2;
        if (iDiff)
        {
            iDiff = RTUniCpToUpper(cp1) != RTUniCpToUpper(cp2);
            if (iDiff)
            {
                iDiff = RTUniCpToLower(cp1) - RTUniCpToLower(cp2); /* lower case diff last! */
                if (iDiff)
                    return iDiff;
            }
        }

        /* hit the terminator? */
        if (!cp1)
            return 0;
    }

    /* Hit some bad encoding, continue in case insensitive mode. */
    return RTStrCmp(psz1, psz2);
}
RT_EXPORT_SYMBOL(RTStrICmp);


/**
 * Performs a case insensitive string compare between two UTF-8 strings, given a
 * maximum string length.
 *
 * This is a simplified compare, as only the simplified lower/upper case folding
 * specified by the unicode specs are used. It does not consider character pairs
 * as they are used in some languages, just simple upper & lower case compares.
 *
 * The result is the difference between the mismatching codepoints after they
 * both have been lower cased.
 *
 * If the string encoding is invalid the function will assert (strict builds)
 * and use RTStrCmp for the remainder of the string.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   psz1        First UTF-8 string. Null is allowed.
 * @param   psz2        Second UTF-8 string. Null is allowed.
 * @param   cchMax      Maximum string length
 */
RTDECL(int) RTStrNICmp(const char *psz1, const char *psz2, size_t cchMax)
{
    if (cchMax == 0)
        return 0;
    if (psz1 == psz2)
        return 0;
    if (!psz1)
        return -1;
    if (!psz2)
        return 1;

    for (;;)
    {
        /* Get the codepoints */
        RTUNICP cp1;
        size_t cchMax2 = cchMax;
        int rc = RTStrGetCpNEx(&psz1, &cchMax, &cp1);
        if (RT_FAILURE(rc))
        {
            AssertRC(rc);
            psz1--;
            cchMax++;
            break;
        }

        RTUNICP cp2;
        rc = RTStrGetCpNEx(&psz2, &cchMax2, &cp2);
        if (RT_FAILURE(rc))
        {
            AssertRC(rc);
            psz2--;
            psz1 -= (cchMax - cchMax2 + 1);  /* This can't overflow, can it? */
            cchMax = cchMax2 + 1;
            break;
        }

        /* compare */
        int iDiff = cp1 - cp2;
        if (iDiff)
        {
            iDiff = RTUniCpToUpper(cp1) != RTUniCpToUpper(cp2);
            if (iDiff)
            {
                iDiff = RTUniCpToLower(cp1) - RTUniCpToLower(cp2); /* lower case diff last! */
                if (iDiff)
                    return iDiff;
            }
        }

        /* hit the terminator? */
        if (!cp1 || cchMax == 0)
            return 0;
    }

    /* Hit some bad encoding, continue in case insensitive mode. */
    return RTStrNCmp(psz1, psz2, cchMax);
}
RT_EXPORT_SYMBOL(RTStrNICmp);


RTDECL(char *) RTStrStr(const char *pszHaystack, const char *pszNeedle)
{
    /* Any NULL strings means NULL return. (In the RTStrCmp tradition.) */
    if (!pszHaystack)
        return NULL;
    if (!pszNeedle)
        return NULL;

    /* The rest is CRT. */
    return (char *)strstr(pszHaystack, pszNeedle);
}
RT_EXPORT_SYMBOL(RTStrStr);


RTDECL(char *) RTStrIStr(const char *pszHaystack, const char *pszNeedle)
{
    /* Any NULL strings means NULL return. (In the RTStrCmp tradition.) */
    if (!pszHaystack)
        return NULL;
    if (!pszNeedle)
        return NULL;

    /* The empty string matches everything. */
    if (!*pszNeedle)
        return (char *)pszHaystack;

    /*
     * The search strategy is to pick out the first char of the needle, fold it,
     * and match it against the haystack code point by code point. When encountering
     * a matching code point we use RTStrNICmp for the remainder (if any) of the needle.
     */
    const char * const pszNeedleStart = pszNeedle;
    RTUNICP Cp0;
    RTStrGetCpEx(&pszNeedle, &Cp0);     /* pszNeedle is advanced one code point. */
    size_t const    cchNeedle   = strlen(pszNeedle);
    size_t const    cchNeedleCp0= pszNeedle - pszNeedleStart;
    RTUNICP const   Cp0Lower    = RTUniCpToLower(Cp0);
    RTUNICP const   Cp0Upper    = RTUniCpToUpper(Cp0);
    if (    Cp0Lower == Cp0Upper
        &&  Cp0Lower == Cp0)
    {
        /* Cp0 is not a case sensitive char. */
        for (;;)
        {
            RTUNICP Cp;
            RTStrGetCpEx(&pszHaystack, &Cp);
            if (!Cp)
                break;
            if (    Cp == Cp0
                &&  !RTStrNICmp(pszHaystack, pszNeedle, cchNeedle))
                return (char *)pszHaystack - cchNeedleCp0;
        }
    }
    else if (   Cp0Lower == Cp0
             || Cp0Upper != Cp0)
    {
        /* Cp0 is case sensitive */
        for (;;)
        {
            RTUNICP Cp;
            RTStrGetCpEx(&pszHaystack, &Cp);
            if (!Cp)
                break;
            if (    (   Cp == Cp0Upper
                     || Cp == Cp0Lower)
                &&  !RTStrNICmp(pszHaystack, pszNeedle, cchNeedle))
                return (char *)pszHaystack - cchNeedleCp0;
        }
    }
    else
    {
        /* Cp0 is case sensitive and folds to two difference chars. (paranoia) */
        for (;;)
        {
            RTUNICP Cp;
            RTStrGetCpEx(&pszHaystack, &Cp);
            if (!Cp)
                break;
            if (    (   Cp == Cp0
                     || Cp == Cp0Upper
                     || Cp == Cp0Lower)
                &&  !RTStrNICmp(pszHaystack, pszNeedle, cchNeedle))
                return (char *)pszHaystack - cchNeedleCp0;
        }
    }


    return NULL;
}
RT_EXPORT_SYMBOL(RTStrIStr);


RTDECL(char *) RTStrToLower(char *psz)
{
    /*
     * Loop the code points in the string, converting them one by one.
     * ASSUMES that the code points for upper and lower case are encoded
     *         with the exact same length.
     */
    /** @todo Handled bad encodings correctly+quietly, remove assumption,
     *        optimize. */
    char *pszCur = psz;
    while (*pszCur)
    {
        RTUNICP cp = RTStrGetCp(pszCur);
        cp = RTUniCpToLower(cp);
        pszCur = RTStrPutCp(pszCur, cp);
    }
    return psz;
}
RT_EXPORT_SYMBOL(RTStrToLower);


RTDECL(char *) RTStrToUpper(char *psz)
{
    /*
     * Loop the code points in the string, converting them one by one.
     * ASSUMES that the code points for upper and lower case are encoded
     *         with the exact same length.
     */
    /** @todo Handled bad encodings correctly+quietly, remove assumption,
     *        optimize. */
    char *pszCur = psz;
    while(*pszCur)
    {
        RTUNICP cp = RTStrGetCp(pszCur);
        cp = RTUniCpToUpper(cp);
        pszCur = RTStrPutCp(pszCur, cp);
    }
    return psz;
}
RT_EXPORT_SYMBOL(RTStrToUpper);

