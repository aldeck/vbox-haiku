/** @file
 *
 * VirtualBox COM: logging macros and function definitions
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
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef ____H_LOGGING
#define ____H_LOGGING

/** @def LOG_GROUP_MAIN_OVERRIDE
 *  Define this macro to point to the desired log group before including
 *  the |Logging.h| header if you want to use a group other than LOG_GROUP_MAIN
 *  for logging from within Main source files.
 *
 *  @example #define LOG_GROUP_MAIN_OVERRIDE LOG_GROUP_HGCM
 */

/*
 *  We might be including the VBox logging subsystem before
 *  including this header file, so reset the logging group.
 */
#ifdef LOG_GROUP
# undef LOG_GROUP
#endif
#ifdef LOG_GROUP_MAIN_OVERRIDE
# define LOG_GROUP LOG_GROUP_MAIN_OVERRIDE
#else
# define LOG_GROUP LOG_GROUP_MAIN
#endif

/* Ensure log macros are enabled if release logging is requested */
#if defined (VBOX_MAIN_RELEASE_LOG) && !defined (DEBUG)
# ifndef LOG_ENABLED
#  define LOG_ENABLED
# endif
#endif

#include <VBox/log.h>
#include <iprt/assert.h>

/** @deprecated Please use LogFlowThisFunc instead! */
#define LogFlowMember(m)     \
    do { LogFlow (("{%p} ", this)); LogFlow (m); } while (0)

/** @def MyLogIt
 * Copy of LogIt that works even when logging is completely disabled (e.g. in
 * release builds) and doesn't interefere with the default release logger
 * instance (which is already in use by the VM process).
 *
 * @warning Logging using MyLog* is intended only as a temporary mean to debug
 *          release builds (e.g. in case if the error is not reproducible with
 *          the debug builds)! Any MyLog* usage must be removed from the sources
 *          after the error has been fixed.
 */
#if defined(RT_ARCH_AMD64) || defined(LOG_USE_C99)
# define _MyLogRemoveParentheseis(...)               __VA_ARGS__
# define _MyLogIt(pvInst, fFlags, iGroup, ...)       RTLogLoggerEx((PRTLOGGER)pvInst, fFlags, iGroup, __VA_ARGS__)
# define MyLogIt(pvInst, fFlags, iGroup, fmtargs)    _MyLogIt(pvInst, fFlags, iGroup, _MyLogRemoveParentheseis fmtargs)
#else
# define MyLogIt(pvInst, fFlags, iGroup, fmtargs) \
    do \
    { \
        register PRTLOGGER LogIt_pLogger = (PRTLOGGER)(pvInst) ? (PRTLOGGER)(pvInst) : RTLogDefaultInstance(); \
        if (LogIt_pLogger) \
        { \
            register unsigned LogIt_fFlags = LogIt_pLogger->afGroups[(unsigned)(iGroup) < LogIt_pLogger->cGroups ? (unsigned)(iGroup) : 0]; \
            if ((LogIt_fFlags & ((fFlags) | RTLOGGRPFLAGS_ENABLED)) == ((fFlags) | RTLOGGRPFLAGS_ENABLED)) \
                LogIt_pLogger->pfnLogger fmtargs; \
        } \
    } while (0)
#endif

/** @def MyLog
 * Equivalent to LogFlow but uses MyLogIt instead of LogIt.
 *
 * @warning Logging using MyLog* is intended only as a temporary mean to debug
 *          release builds (e.g. in case if the error is not reproducible with
 *          the debug builds)! Any MyLog* usage must be removed from the sources
 *          after the error has been fixed.
 */
#define MyLog(a)            MyLogIt(LOG_INSTANCE, RTLOGGRPFLAGS_FLOW, LOG_GROUP, a)

#endif // ____H_LOGGING
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
