/* $Id$ */
/** @file
 * IPRT - Assertion Workers, Ring-0 Drivers, OS/2.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/string.h>
#include <iprt/stdarg.h>

#include <VBox/log.h>

#include "internal/assert.h"


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** The last assert message. (in DATA16) */
extern char g_szRTAssertMsg[2048];
/** The length of the last assert message. (in DATA16) */
extern size_t g_cchRTAssertMsg;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static DECLCALLBACK(size_t) rtR0Os2AssertOutputCB(void *pvArg, const char *pachChars, size_t cbChars);


void rtR0AssertNativeMsg1(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
#if defined(DEBUG_bird)
    RTLogComPrintf("\n!!Assertion Failed!!\n"
                   "Expression: %s\n"
                   "Location  : %s(%d) %s\n",
                   pszExpr, pszFile, uLine, pszFunction);
#endif

    g_cchRTAssertMsg = RTStrPrintf(g_szRTAssertMsg, sizeof(g_szRTAssertMsg),
                                   "\r\n!!Assertion Failed!!\r\n"
                                   "Expression: %s\r\n"
                                   "Location  : %s(%d) %s\r\n",
                                   pszExpr, pszFile, uLine, pszFunction);
}


void rtR0AssertNativeMsg2V(const char *pszFormat, va_list va)
{
#if defined(DEBUG_bird)
    va_list vaCopy;
    va_copy(vaCopy, va);
    RTLogComPrintfV(pszFormat, vaCopy);
    va_end(vaCopy);
#endif

    va_start(va, pszFormat);
    size_t cch = g_cchRTAssertMsg;
    char *pch = &g_szRTAssertMsg[cch];
    cch += RTStrFormatV(rtR0Os2AssertOutputCB, &pch, NULL, NULL, pszFormat, va);
    g_cchRTAssertMsg = cch;
    va_end(va);
}


/**
 * Output callback.
 *
 * @returns number of bytes written.
 * @param   pvArg       Pointer to a char pointer with the current output position.
 * @param   pachChars   Pointer to an array of utf-8 characters.
 * @param   cbChars     Number of bytes in the character array pointed to by pachChars.
 */
static DECLCALLBACK(size_t) rtR0Os2AssertOutputCB(void *pvArg, const char *pachChars, size_t cbChars)
{
    char **ppch = (char **)pvArg;
    char *pch = *ppch;

    while (cbChars-- > 0)
    {
        const char ch = *pachChars++;
        if (ch == '\r')
            continue;
        if (ch == '\n')
        {
            if (pch + 1 >= &g_szRTAssertMsg[sizeof(g_szRTAssertMsg)])
                break;
            *pch++ = '\r';
        }
        if (pch + 1 >= &g_szRTAssertMsg[sizeof(g_szRTAssertMsg)])
            break;
        *pch++ = ch;
    }
    *pch = '\0';

    size_t cbWritten = pch - *ppch;
    *ppch = pch;
    return cbWritten;
}

