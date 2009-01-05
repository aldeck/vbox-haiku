/* $Id$ */
/** @file
 * IPRT Testcase - Test various path functions.
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
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/param.h>


#define CHECK_RC(method) \
    do { \
        rc = method; \
        if (RT_FAILURE(rc)) \
        { \
            cErrors++; \
            RTPrintf("\ntstPath: FAILED calling " #method " at line %d: rc=%Rrc\n", __LINE__, rc); \
        } \
    } while (0)

int main()
{
    /*
     * Init RT.
     */
    int rc;
    int cErrors = 0;
    CHECK_RC(RTR3Init());
    if (RT_FAILURE(rc))
        return 1;

    /*
     * RTPathProgram, RTPathUserHome and RTProcGetExecutableName.
     */
    char szPath[RTPATH_MAX];
    CHECK_RC(RTPathProgram(szPath, sizeof(szPath)));
    if (RT_SUCCESS(rc))
        RTPrintf("Program={%s}\n", szPath);
    CHECK_RC(RTPathUserHome(szPath, sizeof(szPath)));
    if (RT_SUCCESS(rc))
        RTPrintf("UserHome={%s}\n", szPath);
    if (RTProcGetExecutableName(szPath, sizeof(szPath)) == szPath)
        RTPrintf("ExecutableName={%s}\n", szPath);
    else
    {
        RTPrintf("tstPath: FAILED - RTProcGetExecutableName\n");
        cErrors++;
    }


    /*
     * RTPathAbsEx
     */
    RTPrintf("tstPath: TESTING RTPathAbsEx()\n");
    static const struct
    {
        const char *pcszInputBase;
        const char *pcszInputPath;
        int rc;
        const char *pcszOutput;
    }
    s_aRTPathAbsExTests[] =
    {
#if defined (RT_OS_OS2) || defined (RT_OS_WINDOWS)
    { NULL, "", VERR_INVALID_PARAMETER, NULL },
    { NULL, ".", VINF_SUCCESS, "%p" },
    { NULL, "\\", VINF_SUCCESS, "%d\\" },
    { NULL, "\\..", VINF_SUCCESS, "%d\\" },
    { NULL, "/absolute/..", VINF_SUCCESS, "%d\\" },
    { NULL, "/absolute\\\\../..", VINF_SUCCESS, "%d\\" },
    { NULL, "/absolute//../path\\", VINF_SUCCESS, "%d\\path" },
    { NULL, "/absolute/../../path", VINF_SUCCESS, "%d\\path" },
    { NULL, "relative/../dir\\.\\.\\.\\file.txt", VINF_SUCCESS, "%p\\dir\\file.txt" },
    { NULL, "\\data\\", VINF_SUCCESS, "%d\\data" },
    { "relative_base/dir\\", "\\from_root", VINF_SUCCESS, "%d\\from_root" },
    { "relative_base/dir/", "relative_also", VINF_SUCCESS, "%p\\relative_base\\dir\\relative_also" },
#else
    { NULL, "", VERR_INVALID_PARAMETER, NULL },
    { NULL, ".", VINF_SUCCESS, "%p" },
    { NULL, "/", VINF_SUCCESS, "/" },
    { NULL, "/..", VINF_SUCCESS, "/" },
    { NULL, "/absolute/..", VINF_SUCCESS, "/" },
    { NULL, "/absolute\\\\../..", VINF_SUCCESS, "/" },
    { NULL, "/absolute//../path/", VINF_SUCCESS, "/path" },
    { NULL, "/absolute/../../path", VINF_SUCCESS, "/path" },
    { NULL, "relative/../dir/./././file.txt", VINF_SUCCESS, "%p/dir/file.txt" },
    { NULL, "relative/../dir\\.\\.\\.\\file.txt", VINF_SUCCESS, "%p/dir\\.\\.\\.\\file.txt" },  /* linux-specific */
    { NULL, "/data/", VINF_SUCCESS, "/data" },
    { "relative_base/dir/", "/from_root", VINF_SUCCESS, "/from_root" },
    { "relative_base/dir/", "relative_also", VINF_SUCCESS, "%p/relative_base/dir/relative_also" },
#endif
#if defined (RT_OS_OS2) || defined (RT_OS_WINDOWS)
    { NULL, "C:\\", VINF_SUCCESS, "C:\\" },
    { "C:\\", "..", VINF_SUCCESS, "C:\\" },
    { "C:\\temp", "..", VINF_SUCCESS, "C:\\" },
    { "C:\\VirtualBox/Machines", "..\\VirtualBox.xml", VINF_SUCCESS, "C:\\VirtualBox\\VirtualBox.xml" },
    { "C:\\MustDie", "\\from_root/dir/..", VINF_SUCCESS, "C:\\from_root" },
    { "C:\\temp", "D:\\data", VINF_SUCCESS, "D:\\data" },
    { NULL, "\\\\server\\..\\share", VINF_SUCCESS, "\\\\server\\..\\share" /* kind of strange */ },
    { NULL, "\\\\server/", VINF_SUCCESS, "\\\\server" },
    { NULL, "\\\\", VINF_SUCCESS, "\\\\" },
    { NULL, "\\\\\\something", VINF_SUCCESS, "\\\\\\something" /* kind of strange */ },
    { "\\\\server\\share_as_base", "/from_root", VINF_SUCCESS, "\\\\server\\from_root" },
    { "\\\\just_server", "/from_root", VINF_SUCCESS, "\\\\just_server\\from_root" },
    { "\\\\server\\share_as_base", "relative\\data", VINF_SUCCESS, "\\\\server\\share_as_base\\relative\\data" },
    { "base", "\\\\?\\UNC\\relative/edwef/..", VINF_SUCCESS, "\\\\?\\UNC\\relative" },
    { "\\\\?\\UNC\\base", "/from_root", VERR_INVALID_NAME, NULL },
#else
    { "/temp", "..", VINF_SUCCESS, "/" },
    { "/VirtualBox/Machines", "../VirtualBox.xml", VINF_SUCCESS, "/VirtualBox/VirtualBox.xml" },
    { "/MustDie", "/from_root/dir/..", VINF_SUCCESS, "/from_root" },
    { "\\temp", "\\data", VINF_SUCCESS, "%p/\\temp/\\data" },
#endif
    };

    for (unsigned i = 0; i < RT_ELEMENTS(s_aRTPathAbsExTests); ++ i)
    {
        rc = RTPathAbsEx(s_aRTPathAbsExTests[i].pcszInputBase,
                         s_aRTPathAbsExTests[i].pcszInputPath,
                         szPath, sizeof(szPath));
        if (rc != s_aRTPathAbsExTests[i].rc)
        {
            RTPrintf("tstPath: RTPathAbsEx unexpected result code!\n"
                     "   input base: '%s'\n"
                     "   input path: '%s'\n"
                     "       output: '%s'\n"
                     "           rc: %Rrc\n"
                     "  expected rc: %Rrc\n",
                     s_aRTPathAbsExTests[i].pcszInputBase,
                     s_aRTPathAbsExTests[i].pcszInputPath,
                     szPath, rc,
                     s_aRTPathAbsExTests[i].rc);
            cErrors++;
            continue;
        }

        char szTmp[RTPATH_MAX];
        char *pszExpected = NULL;
        if (s_aRTPathAbsExTests[i].pcszOutput != NULL)
        {
            if (s_aRTPathAbsExTests[i].pcszOutput[0] == '%')
            {
                rc = RTPathGetCurrent(szTmp, sizeof(szTmp));
                if (RT_FAILURE(rc))
                {
                    RTPrintf("tstPath: RTPathGetCurrent failed with rc=%Rrc!\n", rc);
                    cErrors++;
                    break;
                }

                pszExpected = szTmp;

                if (s_aRTPathAbsExTests[i].pcszOutput[1] == 'p')
                {
                    size_t cch = strlen(szTmp);
                    if (cch + strlen(s_aRTPathAbsExTests[i].pcszOutput) - 2 <= sizeof(szTmp))
                        strcpy(szTmp + cch, s_aRTPathAbsExTests[i].pcszOutput + 2);
                }
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
                else if (s_aRTPathAbsExTests[i].pcszOutput[1] == 'd')
                {
                    if (2 + strlen(s_aRTPathAbsExTests[i].pcszOutput) - 2 <= sizeof(szTmp))
                        strcpy(szTmp + 2, s_aRTPathAbsExTests[i].pcszOutput + 2);
                }
#endif
            }
            else
            {
                strcpy(szTmp, s_aRTPathAbsExTests[i].pcszOutput);
                pszExpected = szTmp;
            }

            if (strcmp(szPath, pszExpected))
            {
                RTPrintf("tstPath: RTPathAbsEx failed!\n"
                         "   input base: '%s'\n"
                         "   input path: '%s'\n"
                         "       output: '%s'\n"
                         "     expected: '%s'\n",
                         s_aRTPathAbsExTests[i].pcszInputBase,
                         s_aRTPathAbsExTests[i].pcszInputPath,
                         szPath,
                         s_aRTPathAbsExTests[i].pcszOutput);
                cErrors++;
            }
        }
    }

    /*
     * RTPathStripFilename
     */
    RTPrintf("tstPath: RTPathStripFilename...\n");
    static const char *s_apszStripFilenameTests[] =
    {
        "/usr/include///",              "/usr/include//",
        "/usr/include/",                "/usr/include",
        "/usr/include",                 "/usr",
        "/usr",                         "/",
        "usr",                          ".",
#if defined (RT_OS_OS2) || defined (RT_OS_WINDOWS)
        "c:/windows",                   "c:/",
        "c:/",                          "c:/",
        "D:",                           "D:",
        "C:\\OS2\\DLLS",                "C:\\OS2",
#endif
    };
    for (unsigned i = 0; i < RT_ELEMENTS(s_apszStripFilenameTests); i += 2)
    {
        const char *pszInput  = s_apszStripFilenameTests[i];
        const char *pszExpect = s_apszStripFilenameTests[i + 1];
        char szPath[RTPATH_MAX];
        strcpy(szPath, pszInput);
        RTPathStripFilename(szPath);
        if (strcmp(szPath, pszExpect))
        {
            RTPrintf("tstPath: RTPathStripFilename failed!\n"
                     "   input: '%s'\n"
                     "  output: '%s'\n"
                     "expected: '%s'\n",
                     pszInput, szPath, pszExpect);
            cErrors++;
        }
    }

    /*
     * Summary.
     */
    if (!cErrors)
        RTPrintf("tstPath: SUCCESS\n");
    else
        RTPrintf("tstPath: FAILURE %d errors\n", cErrors);
    return !!cErrors;
}

