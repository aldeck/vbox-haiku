/* $Id$ */
/** @file
 * DBGF - Debugger Facility, Memory Methods.
 */

/*
 * Copyright (C) 2007 Sun Microsystems, Inc.
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/dbgf.h>
#include <VBox/pgm.h>
#include <VBox/selm.h>
#include <VBox/hwaccm.h>
#include "DBGFInternal.h"
#include <VBox/vm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/mm.h>



/**
 * Scan guest memory for an exact byte string.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 * @param   idCpu       The ID of the CPU context to search in.
 * @param   pAddress    Where to store the mixed address.
 * @param   pcbRange    The number of bytes to scan. Passed as a pointer because
 *                      it may be 64-bit.
 * @param   pabNeedle   What to search for - exact search.
 * @param   cbNeedle    Size of the search byte string.
 * @param   pHitAddress Where to put the address of the first hit.
 */
static DECLCALLBACK(int) dbgfR3MemScan(PVM pVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, PCRTGCUINTPTR pcbRange,
                                       const uint8_t *pabNeedle, size_t cbNeedle, PDBGFADDRESS pHitAddress)
{
    Assert(idCpu == VMMGetCpuId(pVM));

    /*
     * Validate the input we use, PGM does the rest.
     */
    RTGCUINTPTR cbRange = *pcbRange;
    if (!DBGFR3AddrIsValid(pVM, pAddress))
        return VERR_INVALID_POINTER;
    if (!VALID_PTR(pHitAddress))
        return VERR_INVALID_POINTER;
    if (DBGFADDRESS_IS_HMA(pAddress))
        return VERR_INVALID_POINTER;

    /*
     * Select DBGF worker by addressing mode.
     */
    int     rc;
    PVMCPU  pVCpu   = VMMGetCpuById(pVM, idCpu);
    PGMMODE enmMode = PGMGetGuestMode(pVCpu);
    if (    enmMode == PGMMODE_REAL
        ||  enmMode == PGMMODE_PROTECTED
        ||  DBGFADDRESS_IS_PHYS(pAddress)
        )
    {
        RTGCPHYS PhysHit;
        rc = PGMR3DbgScanPhysical(pVM, pAddress->FlatPtr, cbRange, pabNeedle, cbNeedle, &PhysHit);
        if (RT_SUCCESS(rc))
            DBGFR3AddrFromPhys(pVM, pHitAddress, PhysHit);
    }
    else
    {
#if GC_ARCH_BITS > 32
        if (    (   pAddress->FlatPtr >= _4G
                 || pAddress->FlatPtr + cbRange > _4G)
            &&  enmMode != PGMMODE_AMD64
            &&  enmMode != PGMMODE_AMD64_NX)
            return VERR_DBGF_MEM_NOT_FOUND;
#endif
        RTGCUINTPTR GCPtrHit;
        rc = PGMR3DbgScanVirtual(pVM, pVCpu, pAddress->FlatPtr, cbRange, pabNeedle, cbNeedle, &GCPtrHit);
        if (RT_SUCCESS(rc))
            DBGFR3AddrFromFlat(pVM, pHitAddress, GCPtrHit);
    }

    return rc;
}


/**
 * Scan guest memory for an exact byte string.
 *
 * @returns VBox status codes:
 * @retval  VINF_SUCCESS and *pGCPtrHit on success.
 * @retval  VERR_DBGF_MEM_NOT_FOUND if not found.
 * @retval  VERR_INVALID_POINTER if any of the pointer arguments are invalid.
 * @retval  VERR_INVALID_ARGUMENT if any other arguments are invalid.
 *
 * @param   pVM         The VM handle.
 * @param   idCpu       The ID of the CPU context to search in.
 * @param   pAddress    Where to store the mixed address.
 * @param   cbRange     The number of bytes to scan.
 * @param   pabNeedle   What to search for - exact search.
 * @param   cbNeedle    Size of the search byte string.
 * @param   pHitAddress Where to put the address of the first hit.
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3MemScan(PVM pVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, RTGCUINTPTR cbRange, const uint8_t *pabNeedle, size_t cbNeedle, PDBGFADDRESS pHitAddress)
{
    AssertReturn(idCpu < pVM->cCPUs, VERR_INVALID_PARAMETER);

    PVMREQ pReq;
    int rc = VMR3ReqCall(pVM, idCpu, &pReq, RT_INDEFINITE_WAIT,
                         (PFNRT)dbgfR3MemScan, 7, pVM, idCpu, pAddress, &cbRange, pabNeedle, cbNeedle, pHitAddress);
    if (RT_SUCCESS(rc))
        rc = pReq->iStatus;
    VMR3ReqFree(pReq);

    return rc;
}


/**
 * Read guest memory.
 *
 * @returns VBox status code.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   pAddress        Where to start reading.
 * @param   pvBuf           Where to store the data we've read.
 * @param   cbRead          The number of bytes to read.
 */
static DECLCALLBACK(int) dbgfR3MemRead(PVM pVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, void *pvBuf, size_t cbRead)
{
    Assert(idCpu == VMMGetCpuId(pVM));

    /*
     * Validate the input we use, PGM does the rest.
     */
    if (!DBGFR3AddrIsValid(pVM, pAddress))
        return VERR_INVALID_POINTER;
    if (!VALID_PTR(pvBuf))
        return VERR_INVALID_POINTER;

    /*
     * HMA is special
     */
    int rc;
    if (DBGFADDRESS_IS_HMA(pAddress))
    {
        if (DBGFADDRESS_IS_PHYS(pAddress))
            rc = VERR_INVALID_POINTER;
        else
            rc = MMR3HyperReadGCVirt(pVM, pvBuf, pAddress->FlatPtr, cbRead);
    }
    else
    {
        /*
         * Select DBGF worker by addressing mode.
         */
        PVMCPU  pVCpu   = VMMGetCpuById(pVM, idCpu);
        PGMMODE enmMode = PGMGetGuestMode(pVCpu);
        if (    enmMode == PGMMODE_REAL
            ||  enmMode == PGMMODE_PROTECTED
            ||  DBGFADDRESS_IS_PHYS(pAddress) )
            rc = PGMPhysSimpleReadGCPhys(pVM, pvBuf, pAddress->FlatPtr, cbRead);
        else
        {
#if GC_ARCH_BITS > 32
            if (    (   pAddress->FlatPtr >= _4G
                     || pAddress->FlatPtr + cbRead > _4G)
                &&  enmMode != PGMMODE_AMD64
                &&  enmMode != PGMMODE_AMD64_NX)
                return VERR_PAGE_TABLE_NOT_PRESENT;
#endif
            rc = PGMPhysSimpleReadGCPtr(pVCpu, pvBuf, pAddress->FlatPtr, cbRead);
        }
    }
    return rc;
}


/**
 * Read guest memory.
 *
 * @returns VBox status code.
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           The ID of the source CPU context (for the address).
 * @param   pAddress        Where to start reading.
 * @param   pvBuf           Where to store the data we've read.
 * @param   cbRead          The number of bytes to read.
 */
VMMR3DECL(int) DBGFR3MemRead(PVM pVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, void *pvBuf, size_t cbRead)
{
    AssertReturn(idCpu < pVM->cCPUs, VERR_INVALID_PARAMETER);
    if ((pAddress->fFlags & DBGFADDRESS_FLAGS_TYPE_MASK) == DBGFADDRESS_FLAGS_RING0)
    {
        AssertCompile(sizeof(RTHCUINTPTR) <= sizeof(pAddress->FlatPtr));
        return VMMR3ReadR0Stack(pVM, idCpu, (RTHCUINTPTR)pAddress->FlatPtr, pvBuf, cbRead);
    }
    else
    {
        PVMREQ pReq;
        int rc = VMR3ReqCallU(pVM->pUVM, idCpu, &pReq, RT_INDEFINITE_WAIT, 0,
                              (PFNRT)dbgfR3MemRead, 5, pVM, idCpu, pAddress, pvBuf, cbRead);
        if (RT_SUCCESS(rc))
            rc = pReq->iStatus;
        VMR3ReqFree(pReq);
        return rc;
    }
}


/**
 * Read a zero terminated string from guest memory.
 *
 * @returns VBox status code.
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           The ID of the source CPU context (for the address).
 * @param   pAddress        Where to start reading.
 * @param   pszBuf          Where to store the string.
 * @param   cchBuf          The size of the buffer.
 */
static DECLCALLBACK(int) dbgfR3MemReadString(PVM pVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, char *pszBuf, size_t cchBuf)
{
    /*
     * Validate the input we use, PGM does the rest.
     */
    if (!DBGFR3AddrIsValid(pVM, pAddress))
        return VERR_INVALID_POINTER;
    if (!VALID_PTR(pszBuf))
        return VERR_INVALID_POINTER;

    /*
     * Let dbgfR3MemRead do the job.
     */
    int rc = dbgfR3MemRead(pVM, idCpu, pAddress, pszBuf, cchBuf);

    /*
     * Make sure the result is terminated and that overflow is signaled.
     * This may look a bit reckless with the rc but, it should be fine.
     */
    if (!memchr(pszBuf, '\0', cchBuf))
    {
        pszBuf[cchBuf - 1] = '\0';
        rc = VINF_BUFFER_OVERFLOW;
    }
    /*
     * Handle partial reads (not perfect).
     */
    else if (RT_FAILURE(rc))
    {
        if (pszBuf[0])
            rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Read a zero terminated string from guest memory.
 *
 * @returns VBox status code.
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           The ID of the source CPU context (for the address).
 * @param   pAddress        Where to start reading.
 * @param   pszBuf          Where to store the string.
 * @param   cchBuf          The size of the buffer.
 */
VMMR3DECL(int) DBGFR3MemReadString(PVM pVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, char *pszBuf, size_t cchBuf)
{
    /*
     * Validate and zero output.
     */
    if (!VALID_PTR(pszBuf))
        return VERR_INVALID_POINTER;
    if (cchBuf <= 0)
        return VERR_INVALID_PARAMETER;
    memset(pszBuf, 0, cchBuf);
    AssertReturn(idCpu < pVM->cCPUs, VERR_INVALID_PARAMETER);

    /*
     * Pass it on to the EMT.
     */
    PVMREQ pReq;
    int rc = VMR3ReqCallU(pVM->pUVM, idCpu, &pReq, RT_INDEFINITE_WAIT, 0,
                          (PFNRT)dbgfR3MemReadString, 5, pVM, idCpu, pAddress, pszBuf, cchBuf);
    if (RT_SUCCESS(rc))
        rc = pReq->iStatus;
    VMR3ReqFree(pReq);

    return rc;
}


/**
 * Writes guest memory.
 *
 * @returns VBox status code.
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           The ID of the target CPU context (for the address).
 * @param   pAddress        Where to start writing.
 * @param   pvBuf           The data to write.
 * @param   cbRead          The number of bytes to write.
 */
static DECLCALLBACK(int) dbgfR3MemWrite(PVM pVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, void const *pvBuf, size_t cbWrite)
{
    /*
     * Validate the input we use, PGM does the rest.
     */
    if (!DBGFR3AddrIsValid(pVM, pAddress))
        return VERR_INVALID_POINTER;
    if (!VALID_PTR(pvBuf))
        return VERR_INVALID_POINTER;

    /*
     * HMA is always special.
     */
    int rc;
    if (DBGFADDRESS_IS_HMA(pAddress))
    {
        /** @todo write to HMA. */
        rc = VERR_ACCESS_DENIED;
    }
    else
    {
        /*
         * Select PGM function by addressing mode.
         */
        PVMCPU  pVCpu   = VMMGetCpuById(pVM, idCpu);
        PGMMODE enmMode = PGMGetGuestMode(pVCpu);
        if (    enmMode == PGMMODE_REAL
            ||  enmMode == PGMMODE_PROTECTED
            ||  DBGFADDRESS_IS_PHYS(pAddress) )
            rc = PGMPhysSimpleWriteGCPhys(pVM, pAddress->FlatPtr, pvBuf, cbWrite);
        else
        {
#if GC_ARCH_BITS > 32
            if (    (   pAddress->FlatPtr >= _4G
                     || pAddress->FlatPtr + cbWrite > _4G)
                &&  enmMode != PGMMODE_AMD64
                &&  enmMode != PGMMODE_AMD64_NX)
                return VERR_PAGE_TABLE_NOT_PRESENT;
#endif
            rc = PGMPhysSimpleWriteGCPtr(pVCpu, pAddress->FlatPtr, pvBuf, cbWrite);
        }
    }
    return rc;
}


/**
 * Read guest memory.
 *
 * @returns VBox status code.
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           The ID of the target CPU context (for the address).
 * @param   pAddress        Where to start writing.
 * @param   pvBuf           The data to write.
 * @param   cbRead          The number of bytes to write.
 */
VMMR3DECL(int) DBGFR3MemWrite(PVM pVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, void const *pvBuf, size_t cbWrite)
{
    AssertReturn(idCpu < pVM->cCPUs, VERR_INVALID_PARAMETER);

    PVMREQ pReq;
    int rc = VMR3ReqCallU(pVM->pUVM, idCpu, &pReq, RT_INDEFINITE_WAIT, 0,
                          (PFNRT)dbgfR3MemWrite, 5, pVM, idCpu, pAddress, pvBuf, cbWrite);
    if (RT_SUCCESS(rc))
        rc = pReq->iStatus;
    VMR3ReqFree(pReq);

    return rc;
}


/**
 * Worker for DBGFR3SelQueryInfo that calls into SELM.
 */
static DECLCALLBACK(int) dbgfR3SelQueryInfo(PVM pVM, VMCPUID idCpu, RTSEL Sel, uint32_t fFlags, PDBGFSELINFO pSelInfo)
{
    /*
     * Make the query.
     */
    int rc;
    if (!(fFlags & DBGFSELQI_FLAGS_DT_GUEST))
    {
        PVMCPU pVCpu = VMMGetCpuById(pVM, idCpu);
        VMCPU_ASSERT_EMT(pVCpu);
        rc = SELMR3GetSelectorInfo(pVM, pVCpu, Sel, pSelInfo);
    }
    else
    {
        if (HWACCMIsEnabled(pVM))
            rc = VERR_INVALID_STATE;
        else
            rc = SELMR3GetShadowSelectorInfo(pVM, Sel, pSelInfo);
    }
    return rc;
}


/**
 * Gets information about a selector.
 *
 * Intended for the debugger mostly and will prefer the guest
 * descriptor tables over the shadow ones.
 *
 * @returns VBox status code, the following are the common ones.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INVALID_SELECTOR if the selector isn't fully inside the
 *          descriptor table.
 * @retval  VERR_SELECTOR_NOT_PRESENT if the LDT is invalid or not present. This
 *          is not returned if the selector itself isn't present, you have to
 *          check that for yourself (see DBGFSELINFO::fFlags).
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT or VERR_PAGE_NOT_PRESENT if the
 *          pagetable or page backing the selector table wasn't present.
 *
 * @param   pVM         VM handle.
 * @param   idCpu       The ID of the virtual CPU context.
 * @param   Sel         The selector to get info about.
 * @param   fFlags      Flags, see DBGFQSEL_FLAGS_*.
 * @param   pSelInfo    Where to store the information. This will always be
 *                      updated.
 *
 * @remarks This is a wrapper around SELMR3GetSelectorInfo and
 *          SELMR3GetShadowSelectorInfo.
 */
VMMR3DECL(int) DBGFR3SelQueryInfo(PVM pVM, VMCPUID idCpu, RTSEL Sel, uint32_t fFlags, PDBGFSELINFO pSelInfo)
{
    AssertReturn(idCpu < pVM->cCPUs, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~(DBGFSELQI_FLAGS_DT_GUEST | DBGFSELQI_FLAGS_DT_SHADOW)), VERR_INVALID_PARAMETER);

    /* Clear the return data here on this thread. */
    memset(pSelInfo, 0, sizeof(*pSelInfo));

    /*
     * Dispatch the request to a worker running on the target CPU.
     */
    PVMREQ pReq;
    int rc = VMR3ReqCallU(pVM->pUVM, idCpu, &pReq, RT_INDEFINITE_WAIT, 0,
                          (PFNRT)dbgfR3SelQueryInfo, 5, pVM, idCpu, Sel, fFlags, pSelInfo);
    if (RT_SUCCESS(rc))
        rc = pReq->iStatus;
    VMR3ReqFree(pReq);

    return rc;
}


/**
 * Validates a CS selector.
 *
 * @returns VBox status code.
 * @param   pSelInfo    Pointer to the selector information for the CS selector.
 * @param   SelCPL      The selector defining the CPL (SS).
 */
VMMDECL(int) DBGFR3SelInfoValidateCS(PCDBGFSELINFO pSelInfo, RTSEL SelCPL)
{
    /*
     * Check if present.
     */
    if (pSelInfo->u.Raw.Gen.u1Present)
    {
        /*
         * Type check.
         */
        if (    pSelInfo->u.Raw.Gen.u1DescType == 1
            &&  (pSelInfo->u.Raw.Gen.u4Type & X86_SEL_TYPE_CODE))
        {
            /*
             * Check level.
             */
            unsigned uLevel = RT_MAX(SelCPL & X86_SEL_RPL, pSelInfo->Sel & X86_SEL_RPL);
            if (    !(pSelInfo->u.Raw.Gen.u4Type & X86_SEL_TYPE_CONF)
                ?   uLevel <= pSelInfo->u.Raw.Gen.u2Dpl
                :   uLevel >= pSelInfo->u.Raw.Gen.u2Dpl /* hope I got this right now... */
                    )
                return VINF_SUCCESS;
            return VERR_INVALID_RPL;
        }
        return VERR_NOT_CODE_SELECTOR;
    }
    return VERR_SELECTOR_NOT_PRESENT;
}


