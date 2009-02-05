/** @file
 * MS COM / XPCOM Abstraction Layer:
 * Legacy error printing macros for COM/XPCOM (used with testcases).
 *
 * NOTE: to lighten the load on the compilers, please use the shared
 * functions in errorprint2.h for new code.
 */

/*
 * Copyright (C) 2006-2009 Sun Microsystems, Inc.
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

#ifndef ___VBox_com_errorprint_legacy_h
#define ___VBox_com_errorprint_legacy_h

#include <iprt/assert.h>

/*
 * A section of helpful macros for error output
 */

/**
 *  Prints a line describing the given COM result code.
 *  Used by command line tools or for debugging.
 */
#define PRINT_RC_MESSAGE(rc) \
    do { \
        RTPrintf ("[!] Primary RC  = %Rhra\n", rc); \
        Log (("[!] Primary RC  = %Rhra\n", rc)); \
    } while (0)

/**
 *  Prints the extended error information.
 *  Used by command line tools or for debugging.
 *
 *  @param info com::ErrorInfo instance
 */
#define PRINT_ERROR_INFO(info) \
    do { \
        RTPrintf ("[!] Full error info present: %RTbool, basic error info present: %RTbool\n", \
                  info.isFullAvailable(), info.isBasicAvailable()); \
        Log (("[!] Full error info present: %RTbool, basic error info present: %RTbool\n", \
              info.isFullAvailable(), info.isBasicAvailable())); \
        if (info.isFullAvailable() || info.isBasicAvailable()) { \
            RTPrintf ("[!] Result Code = %Rhra\n", info.getResultCode()); \
            RTPrintf ("[!] Text        = %ls\n", info.getText().raw()); \
            RTPrintf ("[!] Component   = %ls, Interface: %ls, {%RTuuid}\n", \
                      info.getComponent().raw(), info.getInterfaceName().raw(), \
                      info.getInterfaceID().raw()); \
            RTPrintf ("[!] Callee      = %ls, {%RTuuid}\n", \
                      info.getCalleeName().raw(), info.getCalleeIID().raw()); \
            Log (("[!] Result Code = %Rhra\n", info.getResultCode())); \
            Log (("[!] Text        = %ls\n", info.getText().raw())); \
            Log (("[!] Component   = %ls, Interface: %ls, {%RTuuid}\n", \
                  info.getComponent().raw(), info.getInterfaceName().raw(), \
                  info.getInterfaceID().raw())); \
            Log (("[!] Callee      = %ls, {%RTuuid}\n", \
                  info.getCalleeName().raw(), info.getCalleeIID().raw())); \
        } \
    } while (0)

/**
 *  Calls the given interface method and then checks if the return value
 *  (COM result code) indicates a failure. If so, prints the failed
 *  function/line/file and the description of the result code.
 *
 *  Used by command line tools or for debugging and assumes the |HRESULT rc|
 *  variable is accessible for assigning in the current scope.
 */
#define CHECK_RC(method) \
    do { \
        rc = method; \
        if (FAILED (rc)) { \
            RTPrintf ("[!] FAILED calling " #method " at line %d!\n", __LINE__); \
            Log (("[!] FAILED calling " #method " at line %d!\n", __LINE__)); \
            PRINT_RC_MESSAGE(rc); \
        } \
    } while (0)

/**
 *  Does the same as CHECK_RC(), but executes the |return rc| statement on
 *  failure.
 */
#define CHECK_RC_RET(method) \
    do { CHECK_RC (method); if (FAILED (rc)) return rc; } while (0)

/**
 *  Does the same as CHECK_RC(), but executes the |break| statement on
 *  failure.
 */
#define CHECK_RC_BREAK(method) \
    if (1) { CHECK_RC (method); if (FAILED (rc)) break; } else do {} while (0)

/**
 *  Calls the given method of the given interface and then checks if the return
 *  value (COM result code) indicates a failure. If so, prints the failed
 *  function/line/file, the description of the result code and attempts to
 *  query the extended error information on the current thread (using
 *  com::ErrorInfo) if the interface reports that it supports error information.
 *
 *  Used by command line tools or for debugging and assumes the |HRESULT rc|
 *  variable is accessible for assigning in the current scope.
 */
#define CHECK_ERROR(iface, method) \
    do \
    { \
        CHECK_RC(iface->method); \
        if (FAILED(rc)) { \
            com::ErrorInfo info (iface); \
            PRINT_ERROR_INFO (info); \
        } \
    } while (0)

/**
 *  Does the same as CHECK_ERROR(), but executes the |return ret| statement on
 *  failure.
 */
#define CHECK_ERROR_RET(iface, method, ret) \
    do { CHECK_ERROR (iface, method); if (FAILED (rc)) return (ret); } while (0)

/**
 *  Does the same as CHECK_ERROR(), but executes the |break| statement on
 *  failure.
 */
#if defined(__GNUC__)
 #define CHECK_ERROR_BREAK(iface, method) \
    ({ CHECK_ERROR (iface, method); if (FAILED (rc)) break; })
#else
 #define CHECK_ERROR_BREAK(iface, method) \
    if (1) { CHECK_ERROR (iface, method); if (FAILED (rc)) break; } else do {} while (0)
#endif

#define CHECK_ERROR_NOCALL() \
    do { \
        com::ErrorInfo info; \
        PRINT_ERROR_INFO (info); \
    } while (0)

/**
 *  Does the same as CHECK_ERROR(), but doesn't need the interface pointer
 *  because doesn't do a check whether the interface supports error info or not.
 */
#define CHECK_ERROR_NI(method) \
    do { \
        CHECK_RC (method); \
        if (FAILED (rc)) { \
            com::ErrorInfo info; \
            PRINT_ERROR_INFO (info); \
        } \
    } while (0)

/**
 *  Does the same as CHECK_ERROR_NI(), but executes the |return rc| statement
 *  on failure.
 */
#define CHECK_ERROR_NI_RET(method) \
    do { CHECK_ERROR_NI (method); if (FAILED (rc)) return rc; } while (0)

/**
 *  Does the same as CHECK_ERROR_NI(), but executes the |break| statement
 *  on failure.
 */
#define CHECK_ERROR_NI_BREAK(method) \
    if (1) { CHECK_ERROR_NI (method); if (FAILED (rc)) break; } else do {} while (0)


/**
 *  Asserts the given expression is true. When the expression is false, prints
 *  a line containing the failed function/line/file; otherwise does nothing.
 */
#define ASSERT(expr) \
    do { \
        if (!(expr)) \
        { \
            RTPrintf ("[!] ASSERTION FAILED at line %d: %s\n", __LINE__, #expr); \
            Log (("[!] ASSERTION FAILED at line %d: %s\n", __LINE__, #expr)); \
        } \
    } while (0)

/**
 *  Does the same as ASSERT(), but executes the |return ret| statement if the
 *  expression to assert is false.
 */
#define ASSERT_RET(expr, ret) \
    do { ASSERT (expr); if (!(expr)) return (ret); } while (0)

/**
 *  Does the same as ASSERT(), but executes the |break| statement if the
 *  expression to assert is false.
 */
#define ASSERT_BREAK(expr) \
    if (1) { ASSERT (expr); if (!(expr)) break; } else do {} while (0)


#endif

