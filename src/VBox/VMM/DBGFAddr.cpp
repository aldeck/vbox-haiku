/* $Id$ */
/** @file
 * VMM DBGF - Debugger Facility, Mixed Address Methods.
 */

/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
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
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/dbgf.h>
#include <VBox/selm.h>
#include "DBGFInternal.h"
#include <VBox/vm.h>
#include <VBox/mm.h>
#include <VBox/err.h>
#include <VBox/log.h>



/**
 * Checks if an address is in the HMA or not.
 * @returns true if it's inside the HMA.
 * @returns flase if it's not inside the HMA.
 * @param   pVM         The VM handle.
 * @param   FlatPtr     The address in question.
 */
DECLINLINE(bool) dbgfR3IsHMA(PVM pVM, RTGCUINTPTR FlatPtr)
{
    return MMHyperIsInsideArea(pVM, FlatPtr);
}


/**
 * Creates a mixed address from a Sel:off pair.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 * @param   pAddress    Where to store the mixed address.
 * @param   Sel         The selector part.
 * @param   off         The offset part.
 */
DBGFR3DECL(int) DBGFR3AddrFromSelOff(PVM pVM, PDBGFADDRESS pAddress, RTSEL Sel, RTUINTPTR off)
{
    pAddress->Sel = Sel;
    pAddress->off = off;
    if (Sel != DBGF_SEL_FLAT)
    {
        SELMSELINFO SelInfo;
        int rc = SELMR3GetSelectorInfo(pVM, Sel, &SelInfo);
        if (VBOX_FAILURE(rc))
            return rc;
        if (off > SelInfo.cbLimit)
            return VERR_OUT_OF_SELECTOR_BOUNDS;
        pAddress->FlatPtr = SelInfo.GCPtrBase + off;
        /** @todo fix this flat selector test! */
        if (    !SelInfo.GCPtrBase
            &&  SelInfo.Raw.Gen.u1Granularity
            &&  SelInfo.Raw.Gen.u1DefBig)
            pAddress->fFlags = DBGFADDRESS_FLAGS_FLAT;
        else if (SelInfo.cbLimit <= 0xffff)
            pAddress->fFlags = DBGFADDRESS_FLAGS_FAR16;
        else if (SelInfo.cbLimit <= 0xffffffff)
            pAddress->fFlags = DBGFADDRESS_FLAGS_FAR32;
        else
            pAddress->fFlags = DBGFADDRESS_FLAGS_FAR64;
    }
    else
    {
        pAddress->FlatPtr = off;
        pAddress->fFlags = DBGFADDRESS_FLAGS_FLAT;
    }
    pAddress->fFlags |= DBGFADDRESS_FLAGS_VALID;
    if (dbgfR3IsHMA(pVM, pAddress->FlatPtr))
        pAddress->fFlags |= DBGFADDRESS_FLAGS_HMA;

    return VINF_SUCCESS;
}


/**
 * Creates a mixed address from a flat address.
 *
 * @param   pVM         The VM handle.
 * @param   pAddress    Where to store the mixed address.
 * @param   FlatPtr     The flat pointer.
 */
DBGFR3DECL(void) DBGFR3AddrFromFlat(PVM pVM, PDBGFADDRESS pAddress, RTGCUINTPTR FlatPtr)
{
    pAddress->Sel     = DBGF_SEL_FLAT;
    pAddress->off     = FlatPtr;
    pAddress->FlatPtr = FlatPtr;
    pAddress->fFlags  = DBGFADDRESS_FLAGS_FLAT | DBGFADDRESS_FLAGS_VALID;
    if (dbgfR3IsHMA(pVM, pAddress->FlatPtr))
        pAddress->fFlags |= DBGFADDRESS_FLAGS_HMA;
}


/**
 * Checks if the specified address is valid (checks the structure pointer too).
 *
 * @returns true if valid.
 * @returns false if invalid.
 * @param   pVM         The VM handle.
 * @param   pAddress    The address to validate.
 */
DBGFR3DECL(bool) DBGFR3AddrIsValid(PVM pVM, PCDBGFADDRESS pAddress)
{
    if (!VALID_PTR(pAddress))
        return false;
    if (!DBGFADDRESS_IS_VALID(pAddress))
        return false;
    /* more? */
    return true;
}
