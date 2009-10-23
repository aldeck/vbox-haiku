/* $Id$ */
/** @file
 * IPRT - Internal Header for the NT Ring-0 Driver Code.
 */

/*
 * Copyright (C) 2008 Sun Microsystems, Inc.
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

#ifndef ___internal_r0drv_h
#define ___internal_r0drv_h

#include <iprt/cpuset.h>

RT_C_DECLS_BEGIN

/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
typedef ULONG (__stdcall *PFNMYEXSETTIMERRESOLUTION)(ULONG, BOOLEAN);
typedef VOID (__stdcall *PFNMYKEFLUSHQUEUEDDPCS)(VOID);
typedef VOID (__stdcall *PFNRTKESETSYSTEMAFFINITYTHREAD)(KAFFINITY);

/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
extern RTCPUSET                     g_rtMpNtCpuSet;
extern PFNMYEXSETTIMERRESOLUTION    g_pfnrtNtExSetTimerResolution;
extern PFNMYKEFLUSHQUEUEDDPCS       g_pfnrtNtKeFlushQueuedDpcs;
extern PFNRTKESETSYSTEMAFFINITYTHREAD g_pfnrtKeSetSystemAffinityThread;
extern uint32_t                     g_offrtNtPbQuantumEnd;
extern uint32_t                     g_cbrtNtPbQuantumEnd;
extern uint32_t                     g_offrtNtPbDpcQueueDepth;

RT_C_DECLS_END

#endif


