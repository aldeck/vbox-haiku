/* $Id$ */
/** @file
 * innotek Portable Runtime - Initialization & Termination, R0 Driver, Linux.
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
#include "the-linux-kernel.h"
#include <iprt/err.h>
#include <iprt/assert.h>
#include "internal/initterm.h"


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
#ifdef __AMD64__
/* in alloc-r0drv0-linux.c */
extern void rtR0MemExecCleanup(void);
#endif 


int rtR0InitNative(void)
{
    return VINF_SUCCESS;
}


void rtR0TermNative(void)
{
#ifdef __AMD64__
    rtR0MemExecCleanup();
#endif 
}

