/* $Id$ */
/** @file
 * IPRT - Internal RTProc header.
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

#ifndef ___internal_process_h
#define ___internal_process_h

#include <iprt/process.h>
#include <iprt/param.h>

RT_C_DECLS_BEGIN

extern RTPROCESS        g_ProcessSelf;
extern RTPROCPRIORITY   g_enmProcessPriority;
extern char             g_szrtProcExePath[RTPATH_MAX];
extern size_t           g_cchrtProcExePath;
extern size_t           g_cchrtProcDir;
extern size_t           g_offrtProcName;

/**
 * Validates and sets the process priority.
 * This will check that all rtThreadNativeSetPriority() will success for all the
 * thread types when applied to the current thread.
 *
 * @returns iprt status code.
 * @param   enmPriority     The priority to validate and set.
 * @remark  Located in sched.
 */
int rtProcNativeSetPriority(RTPROCPRIORITY enmPriority);

/**
 * Determines the full path to the executable image.
 *
 * This is called by rtR3Init.
 *
 * @returns IPRT status code.
 *
 * @param   pszPath     Pointer to the g_szrtProcExePath buffer.
 * @param   cchPath     The size of the buffer.
 */
DECLHIDDEN(int) rtProcInitExePath(char *pszPath, size_t cchPath);

RT_C_DECLS_END

#endif

