/* $Id$ */
/** @file
 * IPRT - Initialization & Termination, R0 Driver, Solaris.
 */

/*
 * Copyright (C) 2006-2007 Oracle Corporation
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
#include "the-solaris-kernel.h"
#include "internal/iprt.h"

#include <iprt/err.h>
#include <iprt/asm.h>
#include "internal/initterm.h"


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Indicates that the spl routines (and therefore a bunch of other ones too)
 * will set EFLAGS::IF and break code that disables interrupts.  */
bool g_frtSolarisSplSetsEIF = false;


int rtR0InitNative(void)
{
    /*
     * Initialize vbi (keeping it separate for now)
     */
    int rc = vbi_init();
    if (!rc)
    {
        /*
         * Detech whether spl*() is preserving the interrupt flag or not.
         * This is a problem on S10.
         */
        RTCCUINTREG uOldFlags = ASMIntDisableFlags();
        int iOld = splr(DISP_LEVEL);
        if (ASMIntAreEnabled())
            g_frtSolarisSplSetsEIF = true;
        splx(iOld);
        if (ASMIntAreEnabled())
            g_frtSolarisSplSetsEIF = true;
        ASMSetFlags(uOldFlags);

        return VINF_SUCCESS;
    }
    cmn_err(CE_NOTE, "vbi_init failed. rc=%d\n", rc);
    return VERR_GENERAL_FAILURE;
}


void rtR0TermNative(void)
{
}

