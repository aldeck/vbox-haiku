/* $Id$ */
/** @file
 * GVMM - Global VM Manager.
 */

/*
 * Copyright (C) 2007 innotek GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/** @page pg_GVMM   GVMM - The Global VM Manager
 *
 * The Global VM Manager lives in ring-0. It's main function at the moment
 * is to manage a list of all running VMs, keep a ring-0 only structure (GVM)
 * for each of them, and assign them unique identifiers (so GMM can track
 * page owners). The idea for the future is to add an idle priority kernel
 * thread that can take care of tasks like page sharing.
 *
 * The GVMM will create a ring-0 object for each VM when it's registered,
 * this is both for session cleanup purposes and for having a point where
 * it's possible to implement usage polices later (in SUPR0ObjRegister).
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_GVMM
#include <VBox/gvmm.h>
#include "GVMMR0Internal.h"
#include <VBox/gvm.h>
#include <VBox/vm.h>
#include <VBox/err.h>
#include <iprt/alloc.h>
#include <iprt/semaphore.h>
#include <iprt/time.h>
#include <VBox/log.h>
#include <iprt/thread.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/

/**
 * Global VM handle.
 */
typedef struct GVMHANDLE
{
    /** The index of the next handle in the list (free or used). (0 is nil.) */
    uint16_t volatile   iNext;
    /** Our own index / handle value. */
    uint16_t            iSelf;
    /** The pointer to the ring-0 only (aka global) VM structure. */
    PGVM                pGVM;
    /** The ring-0 mapping of the shared VM instance data. */
    PVM                 pVM;
    /** The virtual machine object. */
    void               *pvObj;
    /** The session this VM is associated with. */
    PSUPDRVSESSION      pSession;
    /** The ring-0 handle of the EMT thread.
     * This is used for assertions and similar cases where we need to find the VM handle. */
    RTNATIVETHREAD      hEMT;
} GVMHANDLE;
/** Pointer to a global VM handle. */
typedef GVMHANDLE *PGVMHANDLE;

/**
 * The GVMM instance data.
 */
typedef struct GVMM
{
    /** Eyecatcher / magic. */
    uint32_t            u32Magic;
    /** The index of the head of the free handle chain. (0 is nil.) */
    uint16_t volatile   iFreeHead;
    /** The index of the head of the active handle chain. (0 is nil.) */
    uint16_t volatile   iUsedHead;
    /** The number of VMs. */
    uint16_t volatile   cVMs;
//    /** The number of halted EMT threads. */
//    uint16_t volatile   cHaltedEMTs;
    /** The lock used to serialize VM creation, destruction and associated events that
     * isn't performance critical. Owners may acquire the list lock. */
    RTSEMFASTMUTEX      CreateDestroyLock;
    /** The lock used to serialize used list updates and accesses.
     * This indirectly includes scheduling since the scheduler will have to walk the
     * used list to examin running VMs. Owners may not acquire any other locks. */
    RTSEMFASTMUTEX      UsedLock;
    /** The handle array.
     * The size of this array defines the maximum number of currently running VMs.
     * The first entry is unused as it represents the NIL handle. */
    GVMHANDLE           aHandles[128];

    /** The number of VMs that means we no longer considers ourselves along on a CPU/Core.
     * @gcfgm   /GVMM/cVMsMeansCompany 32-bit  0..UINT32_MAX
     */
    uint32_t            cVMsMeansCompany;
    /** The minimum sleep time for when we're alone, in nano seconds.
     * @gcfgm   /GVMM/MinSleepAlone     32-bit  0..100000000
     */
    uint32_t            nsMinSleepAlone;
    /** The minimum sleep time for when we've got company, in nano seconds.
     * @gcfgm   /GVMM/MinSleepCompany   32-bit  0..100000000
     */
    uint32_t            nsMinSleepCompany;
    /** The limit for the first round of early wakeups, given in nano seconds.
     * @gcfgm   /GVMM/EarlyWakeUp1      32-bit  0..100000000
     */
    uint32_t            nsEarlyWakeUp1;
    /** The limit for the second round of early wakeups, given in nano seconds.
     * @gcfgm   /GVMM/EarlyWakeUp2      32-bit  0..100000000
     */
    uint32_t            nsEarlyWakeUp2;
} GVMM;
/** Pointer to the GVMM instance data. */
typedef GVMM *PGVMM;

/** The GVMM::u32Magic value (Charlie Haden). */
#define GVMM_MAGIC      0x19370806



/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Pointer to the GVMM instance data.
 * (Just my general dislike for global variables.) */
static PGVMM g_pGVMM = NULL;

/** Macro for obtaining and validating the g_pGVMM pointer.
 * On failure it will return from the invoking function with the specified return value.
 *
 * @param   pGVMM   The name of the pGVMM variable.
 * @param   rc      The return value on failure. Use VERR_INTERNAL_ERROR for
 *                  VBox status codes.
 */
#define GVMM_GET_VALID_INSTANCE(pGVMM, rc) \
    do { \
        (pGVMM) = g_pGVMM;\
        AssertPtrReturn((pGVMM), (rc)); \
        AssertMsgReturn((pGVMM)->u32Magic == GVMM_MAGIC, ("%p - %#x\n", (pGVMM), (pGVMM)->u32Magic), (rc)); \
    } while (0)

/** Macro for obtaining and validating the g_pGVMM pointer, void function variant.
 * On failure it will return from the invoking function.
 *
 * @param   pGVMM   The name of the pGVMM variable.
 */
#define GVMM_GET_VALID_INSTANCE_VOID(pGVMM) \
    do { \
        (pGVMM) = g_pGVMM;\
        AssertPtrReturnVoid((pGVMM)); \
        AssertMsgReturnVoid((pGVMM)->u32Magic == GVMM_MAGIC, ("%p - %#x\n", (pGVMM), (pGVMM)->u32Magic)); \
    } while (0)


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static void gvmmR0InitPerVMData(PGVM pGVM);
static DECLCALLBACK(void) gvmmR0HandleObjDestructor(void *pvObj, void *pvGVMM, void *pvHandle);
static int gvmmR0ByVM(PVM pVM, PGVM *ppGVM, PGVMM *ppGVMM, bool fTakeUsedLock);
static int gvmmR0ByVMAndEMT(PVM pVM, PGVM *ppGVM, PGVMM *ppGVMM);


/**
 * Initializes the GVMM.
 *
 * This is called while owninng the loader sempahore (see supdrvIOCtl_LdrLoad()).
 *
 * @returns VBox status code.
 */
GVMMR0DECL(int) GVMMR0Init(void)
{
    LogFlow(("GVMMR0Init:\n"));

    /*
     * Allocate and initialize the instance data.
     */
    PGVMM pGVMM = (PGVMM)RTMemAllocZ(sizeof(*pGVMM));
    if (!pGVMM)
        return VERR_NO_MEMORY;
    int rc = RTSemFastMutexCreate(&pGVMM->CreateDestroyLock);
    if (RT_SUCCESS(rc))
    {
        rc = RTSemFastMutexCreate(&pGVMM->UsedLock);
        if (RT_SUCCESS(rc))
        {
            pGVMM->u32Magic = GVMM_MAGIC;
            pGVMM->iUsedHead = 0;
            pGVMM->iFreeHead = 1;

            /* the nil handle */
            pGVMM->aHandles[0].iSelf = 0;
            pGVMM->aHandles[0].iNext = 0;

            /* the tail */
            unsigned i = RT_ELEMENTS(pGVMM->aHandles) - 1;
            pGVMM->aHandles[i].iSelf = i;
            pGVMM->aHandles[i].iNext = 0; /* nil */

            /* the rest */
            while (i-- > 1)
            {
                pGVMM->aHandles[i].iSelf = i;
                pGVMM->aHandles[i].iNext = i + 1;
            }

            /* The default configuration values. */
            pGVMM->cVMsMeansCompany  = 1;                           /** @todo should be adjusted to relative to the cpu count or something... */
            pGVMM->nsMinSleepAlone   = 750000 /* ns (0.750 ms) */;  /** @todo this should be adjusted to be 75% (or something) of the scheduler granularity... */
            pGVMM->nsMinSleepCompany =  15000 /* ns (0.015 ms) */;
            pGVMM->nsEarlyWakeUp1    =  25000 /* ns (0.025 ms) */;
            pGVMM->nsEarlyWakeUp2    =  50000 /* ns (0.050 ms) */;

            g_pGVMM = pGVMM;
            LogFlow(("GVMMR0Init: pGVMM=%p\n", pGVMM));
            return VINF_SUCCESS;
        }

        RTSemFastMutexDestroy(pGVMM->CreateDestroyLock);
    }

    RTMemFree(pGVMM);
    return rc;
}


/**
 * Terminates the GVM.
 *
 * This is called while owning the loader semaphore (see supdrvLdrFree()).
 * And unless something is wrong, there should be absolutely no VMs
 * registered at this point.
 */
GVMMR0DECL(void) GVMMR0Term(void)
{
    LogFlow(("GVMMR0Term:\n"));

    PGVMM pGVMM = g_pGVMM;
    g_pGVMM = NULL;
    if (RT_UNLIKELY(!VALID_PTR(pGVMM)))
    {
        SUPR0Printf("GVMMR0Term: pGVMM=%p\n", pGVMM);
        return;
    }

    pGVMM->u32Magic++;

    RTSemFastMutexDestroy(pGVMM->UsedLock);
    pGVMM->UsedLock = NIL_RTSEMFASTMUTEX;
    RTSemFastMutexDestroy(pGVMM->CreateDestroyLock);
    pGVMM->CreateDestroyLock = NIL_RTSEMFASTMUTEX;

    pGVMM->iFreeHead = 0;
    if (pGVMM->iUsedHead)
    {
        SUPR0Printf("GVMMR0Term: iUsedHead=%#x! (cVMs=%#x)\n", pGVMM->iUsedHead, pGVMM->cVMs);
        pGVMM->iUsedHead = 0;
    }

    RTMemFree(pGVMM);
}


/**
 * A quick hack for setting global config values.
 *
 * @returns VBox status code.
 *
 * @param   pSession    The session handle. Used for authentication.
 * @param   pszName     The variable name.
 * @param   u64Value    The new value.
 */
GVMMR0DECL(int) GVMMR0SetConfig(PSUPDRVSESSION pSession, const char *pszName, uint64_t u64Value)
{
    /*
     * Validate input.
     */
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_INTERNAL_ERROR);
    AssertPtrReturn(pSession, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);

    /*
     * String switch time!
     */
    if (strncmp(pszName, "/GVMM/", sizeof("/GVMM/") - 1))
        return VERR_CFGM_VALUE_NOT_FOUND; /* borrow status codes from CFGM... */
    int rc = VINF_SUCCESS;
    pszName += sizeof("/GVMM/") - 1;
    if (!strcmp(pszName, "cVMsMeansCompany"))
    {
        if (u64Value <= UINT32_MAX)
            pGVMM->cVMsMeansCompany = u64Value;
        else
            rc = VERR_OUT_OF_RANGE;
    }
    else if (!strcmp(pszName, "MinSleepAlone"))
    {
        if (u64Value <= 100000000)
            pGVMM->nsMinSleepAlone = u64Value;
        else
            rc = VERR_OUT_OF_RANGE;
    }
    else if (!strcmp(pszName, "MinSleepCompany"))
    {
        if (u64Value <= 100000000)
            pGVMM->nsMinSleepCompany = u64Value;
        else
            rc = VERR_OUT_OF_RANGE;
    }
    else if (!strcmp(pszName, "EarlyWakeUp1"))
    {
        if (u64Value <= 100000000)
            pGVMM->nsEarlyWakeUp1 = u64Value;
        else
            rc = VERR_OUT_OF_RANGE;
    }
    else if (!strcmp(pszName, "EarlyWakeUp2"))
    {
        if (u64Value <= 100000000)
            pGVMM->nsEarlyWakeUp2 = u64Value;
        else
            rc = VERR_OUT_OF_RANGE;
    }
    else
        rc = VERR_CFGM_VALUE_NOT_FOUND;
    return rc;
}


/**
 * A quick hack for getting global config values.
 *
 * @returns VBox status code.
 *
 * @param   pSession    The session handle. Used for authentication.
 * @param   pszName     The variable name.
 * @param   u64Value    The new value.
 */
GVMMR0DECL(int) GVMMR0QueryConfig(PSUPDRVSESSION pSession, const char *pszName, uint64_t *pu64Value)
{
    /*
     * Validate input.
     */
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_INTERNAL_ERROR);
    AssertPtrReturn(pSession, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrReturn(pu64Value, VERR_INVALID_POINTER);

    /*
     * String switch time!
     */
    if (strncmp(pszName, "/GVMM/", sizeof("/GVMM/") - 1))
        return VERR_CFGM_VALUE_NOT_FOUND; /* borrow status codes from CFGM... */
    int rc = VINF_SUCCESS;
    pszName += sizeof("/GVMM/") - 1;
    if (!strcmp(pszName, "cVMsMeansCompany"))
        *pu64Value = pGVMM->cVMsMeansCompany;
    else if (!strcmp(pszName, "MinSleepAlone"))
        *pu64Value = pGVMM->nsMinSleepAlone;
    else if (!strcmp(pszName, "MinSleepCompany"))
        *pu64Value = pGVMM->nsMinSleepCompany;
    else if (!strcmp(pszName, "EarlyWakeUp1"))
        *pu64Value = pGVMM->nsEarlyWakeUp1;
    else if (!strcmp(pszName, "EarlyWakeUp2"))
        *pu64Value = pGVMM->nsEarlyWakeUp2;
    else
        rc = VERR_CFGM_VALUE_NOT_FOUND;
    return rc;
}


/**
 * Request wrapper for the GVMMR0CreateVM API.
 *
 * @returns VBox status code.
 * @param   pReq        The request buffer.
 */
GVMMR0DECL(int) GVMMR0CreateVMReq(PGVMMCREATEVMREQ pReq)
{
    /*
     * Validate the request.
     */
    if (!VALID_PTR(pReq))
        return VERR_INVALID_POINTER;
    if (pReq->Hdr.cbReq != sizeof(*pReq))
        return VERR_INVALID_PARAMETER;
    if (!VALID_PTR(pReq->pSession))
        return VERR_INVALID_POINTER;

    /*
     * Execute it.
     */
    PVM pVM;
    pReq->pVMR0 = NULL;
    pReq->pVMR3 = NIL_RTR3PTR;
    int rc = GVMMR0CreateVM(pReq->pSession, &pVM);
    if (RT_SUCCESS(rc))
    {
        pReq->pVMR0 = pVM;
        pReq->pVMR3 = pVM->pVMR3;
    }
    return rc;
}


/**
 * Allocates the VM structure and registers it with GVM.
 *
 * @returns VBox status code.
 * @param   pSession    The support driver session.
 * @param   ppVM        Where to store the pointer to the VM structure.
 *
 * @thread  Any thread.
 */
GVMMR0DECL(int) GVMMR0CreateVM(PSUPDRVSESSION pSession, PVM *ppVM)
{
    LogFlow(("GVMMR0CreateVM: pSession=%p\n", pSession));
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_INTERNAL_ERROR);

    AssertPtrReturn(ppVM, VERR_INVALID_POINTER);
    *ppVM = NULL;

    AssertReturn(RTThreadNativeSelf() != NIL_RTNATIVETHREAD, VERR_INTERNAL_ERROR);

    /*
     * The whole allocation process is protected by the lock.
     */
    int rc = RTSemFastMutexRequest(pGVMM->CreateDestroyLock);
    AssertRCReturn(rc, rc);

    /*
     * Allocate a handle first so we don't waste resources unnecessarily.
     */
    uint16_t iHandle = pGVMM->iFreeHead;
    if (iHandle)
    {
        PGVMHANDLE pHandle = &pGVMM->aHandles[iHandle];

        /* consistency checks, a bit paranoid as always. */
        if (    !pHandle->pVM
            &&  !pHandle->pGVM
            &&  !pHandle->pvObj
            &&  pHandle->iSelf == iHandle)
        {
            pHandle->pvObj = SUPR0ObjRegister(pSession, SUPDRVOBJTYPE_VM, gvmmR0HandleObjDestructor, pGVMM, pHandle);
            if (pHandle->pvObj)
            {
                /*
                 * Move the handle from the free to used list and perform permission checks.
                 */
                rc = RTSemFastMutexRequest(pGVMM->UsedLock);
                AssertRC(rc);

                pGVMM->iFreeHead = pHandle->iNext;
                pHandle->iNext = pGVMM->iUsedHead;
                pGVMM->iUsedHead = iHandle;
                pGVMM->cVMs++;

                pHandle->pVM = NULL;
                pHandle->pGVM = NULL;
                pHandle->pSession = pSession;
                pHandle->hEMT = NIL_RTNATIVETHREAD;

                RTSemFastMutexRelease(pGVMM->UsedLock);

                rc = SUPR0ObjVerifyAccess(pHandle->pvObj, pSession, NULL);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Allocate the global VM structure (GVM) and initialize it.
                     */
                    PGVM pGVM = (PGVM)RTMemAllocZ(sizeof(*pGVM));
                    if (pGVM)
                    {
                        pGVM->u32Magic = GVM_MAGIC;
                        pGVM->hSelf = iHandle;
                        pGVM->hEMT = NIL_RTNATIVETHREAD;
                        pGVM->pVM = NULL;

                        gvmmR0InitPerVMData(pGVM);
                        /* GMMR0InitPerVMData(pGVM); - later */

                        /*
                         * Allocate the shared VM structure and associated page array.
                         */
                        const size_t cPages = RT_ALIGN(sizeof(VM), PAGE_SIZE) >> PAGE_SHIFT;
                        rc = RTR0MemObjAllocLow(&pGVM->gvmm.s.VMMemObj, cPages << PAGE_SHIFT, false /* fExecutable */);
                        if (RT_SUCCESS(rc))
                        {
                            PVM pVM = (PVM)RTR0MemObjAddress(pGVM->gvmm.s.VMMemObj); AssertPtr(pVM);
                            memset(pVM, 0, cPages << PAGE_SHIFT);
                            pVM->enmVMState = VMSTATE_CREATING;
                            pVM->pVMR0 = pVM;
                            pVM->pSession = pSession;
                            pVM->hSelf = iHandle;

                            rc = RTR0MemObjAllocPage(&pGVM->gvmm.s.VMPagesMemObj, cPages * sizeof(SUPPAGE), false /* fExecutable */);
                            if (RT_SUCCESS(rc))
                            {
                                PSUPPAGE paPages = (PSUPPAGE)RTR0MemObjAddress(pGVM->gvmm.s.VMPagesMemObj); AssertPtr(paPages);
                                for (size_t iPage = 0; iPage < cPages; iPage++)
                                {
                                    paPages[iPage].uReserved = 0;
                                    paPages[iPage].Phys = RTR0MemObjGetPagePhysAddr(pGVM->gvmm.s.VMMemObj, iPage);
                                    Assert(paPages[iPage].Phys != NIL_RTHCPHYS);
                                }

                                /*
                                 * Map them into ring-3.
                                 */
                                rc = RTR0MemObjMapUser(&pGVM->gvmm.s.VMMapObj, pGVM->gvmm.s.VMMemObj, (RTR3PTR)-1, 0,
                                                       RTMEM_PROT_READ | RTMEM_PROT_WRITE, NIL_RTR0PROCESS);
                                if (RT_SUCCESS(rc))
                                {
                                    pVM->pVMR3 = RTR0MemObjAddressR3(pGVM->gvmm.s.VMMapObj);
                                    AssertPtr((void *)pVM->pVMR3);

                                    rc = RTR0MemObjMapUser(&pGVM->gvmm.s.VMPagesMapObj, pGVM->gvmm.s.VMPagesMemObj, (RTR3PTR)-1, 0,
                                                           RTMEM_PROT_READ | RTMEM_PROT_WRITE, NIL_RTR0PROCESS);
                                    if (RT_SUCCESS(rc))
                                    {
                                        pVM->paVMPagesR3 = RTR0MemObjAddressR3(pGVM->gvmm.s.VMPagesMapObj);
                                        AssertPtr((void *)pVM->paVMPagesR3);

                                        /* complete the handle - take the UsedLock sem just to be careful. */
                                        rc = RTSemFastMutexRequest(pGVMM->UsedLock);
                                        AssertRC(rc);

                                        pHandle->pVM = pVM;
                                        pHandle->pGVM = pGVM;
                                        pGVM->pVM = pVM;


                                        RTSemFastMutexRelease(pGVMM->UsedLock);
                                        RTSemFastMutexRelease(pGVMM->CreateDestroyLock);

                                        *ppVM = pVM;
                                        Log(("GVMMR0CreateVM: pVM=%p pVMR3=%p pGVM=%p hGVM=%d\n", pVM, pVM->pVMR3, pGVM, iHandle));
                                        return VINF_SUCCESS;
                                    }

                                    RTR0MemObjFree(pGVM->gvmm.s.VMMapObj, false /* fFreeMappings */);
                                    pGVM->gvmm.s.VMMapObj = NIL_RTR0MEMOBJ;
                                }
                                RTR0MemObjFree(pGVM->gvmm.s.VMPagesMemObj, false /* fFreeMappings */);
                                pGVM->gvmm.s.VMPagesMemObj = NIL_RTR0MEMOBJ;
                            }
                            RTR0MemObjFree(pGVM->gvmm.s.VMMemObj, false /* fFreeMappings */);
                            pGVM->gvmm.s.VMMemObj = NIL_RTR0MEMOBJ;
                        }
                    }
                }
                /* else: The user wasn't permitted to create this VM. */

                /*
                 * The handle will be freed by gvmmR0HandleObjDestructor as we release the
                 * object reference here. A little extra mess because of non-recursive lock.
                 */
                void *pvObj = pHandle->pvObj;
                pHandle->pvObj = NULL;
                RTSemFastMutexRelease(pGVMM->CreateDestroyLock);

                SUPR0ObjRelease(pvObj, pSession);

                SUPR0Printf("GVMMR0CreateVM: failed, rc=%d\n", rc);
                return rc;
            }

            rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_INTERNAL_ERROR;
    }
    else
        rc = VERR_GVM_TOO_MANY_VMS;

    RTSemFastMutexRelease(pGVMM->CreateDestroyLock);
    return rc;
}


/**
 * Initializes the per VM data belonging to GVMM.
 *
 * @param   pGVM        Pointer to the global VM structure.
 */
static void gvmmR0InitPerVMData(PGVM pGVM)
{
    AssertCompile(RT_SIZEOFMEMB(GVM,gvmm.s) <= RT_SIZEOFMEMB(GVM,gvmm.padding));
    Assert(RT_SIZEOFMEMB(GVM,gvmm.s) <= RT_SIZEOFMEMB(GVM,gvmm.padding));
    pGVM->gvmm.s.VMMemObj = NIL_RTR0MEMOBJ;
    pGVM->gvmm.s.VMMapObj = NIL_RTR0MEMOBJ;
    pGVM->gvmm.s.VMPagesMemObj = NIL_RTR0MEMOBJ;
    pGVM->gvmm.s.VMPagesMapObj = NIL_RTR0MEMOBJ;
    pGVM->gvmm.s.HaltEventMulti = NIL_RTSEMEVENTMULTI;
}


/**
 * Associates an EMT thread with a VM.
 *
 * This is called early during the ring-0 VM initialization so assertions later in
 * the process can be handled gracefully.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The VM instance data (aka handle), ring-0 mapping of course.
 * @thread  EMT.
 */
GVMMR0DECL(int) GVMMR0AssociateEMTWithVM(PVM pVM)
{
    LogFlow(("GVMMR0AssociateEMTWithVM: pVM=%p\n", pVM));
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_INTERNAL_ERROR);

    /*
     * Validate the VM structure, state and handle.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertReturn(!((uintptr_t)pVM & PAGE_OFFSET_MASK), VERR_INVALID_POINTER);
    AssertMsgReturn(pVM->enmVMState == VMSTATE_CREATING, ("%d\n", pVM->enmVMState), VERR_WRONG_ORDER);

    RTNATIVETHREAD hEMT = RTThreadNativeSelf();
    AssertReturn(hEMT != NIL_RTNATIVETHREAD, VERR_NOT_SUPPORTED);

    const uint16_t hGVM = pVM->hSelf;
    AssertReturn(hGVM != NIL_GVM_HANDLE, VERR_INVALID_HANDLE);
    AssertReturn(hGVM < RT_ELEMENTS(pGVMM->aHandles), VERR_INVALID_HANDLE);

    PGVMHANDLE pHandle = &pGVMM->aHandles[hGVM];
    AssertReturn(pHandle->pVM == pVM, VERR_NOT_OWNER);

    /*
     * Take the lock, validate the handle and update the structure members.
     */
    int rc = RTSemFastMutexRequest(pGVMM->CreateDestroyLock);
    AssertRCReturn(rc, rc);
    rc = RTSemFastMutexRequest(pGVMM->UsedLock);
    AssertRC(rc);

    if (    pHandle->pVM == pVM
        &&  VALID_PTR(pHandle->pvObj)
        &&  VALID_PTR(pHandle->pSession)
        &&  VALID_PTR(pHandle->pGVM)
        &&  pHandle->pGVM->u32Magic == GVM_MAGIC)
    {
        pHandle->hEMT = hEMT;
        pHandle->pGVM->hEMT = hEMT;
    }
    else
        rc = VERR_INTERNAL_ERROR;

    RTSemFastMutexRelease(pGVMM->UsedLock);
    RTSemFastMutexRelease(pGVMM->CreateDestroyLock);
    LogFlow(("GVMMR0AssociateEMTWithVM: returns %Vrc (hEMT=%RTnthrd)\n", rc, hEMT));
    return rc;
}


/**
 * Does the VM initialization.
 *
 * @returns VBox status code.
 * @param   pVM         Pointer to the shared VM structure.
 */
GVMMR0DECL(int) GVMMR0InitVM(PVM pVM)
{
    LogFlow(("GVMMR0InitVM: pVM=%p\n", pVM));

    /*
     * Validate the VM structure, state and handle.
     */
    PGVM pGVM;
    PGVMM pGVMM;
    int rc = gvmmR0ByVMAndEMT(pVM, &pGVM, &pGVMM);
    if (RT_SUCCESS(rc))
    {
        if (pGVM->gvmm.s.HaltEventMulti == NIL_RTSEMEVENTMULTI)
        {
            rc = RTSemEventMultiCreate(&pGVM->gvmm.s.HaltEventMulti);
            if (RT_FAILURE(rc))
                pGVM->gvmm.s.HaltEventMulti = NIL_RTSEMEVENTMULTI;
        }
        else
            rc = VERR_WRONG_ORDER;
    }

    LogFlow(("GVMMR0InitVM: returns %Rrc\n", rc));
    return rc;
}


/**
 * Disassociates the EMT thread from a VM.
 *
 * This is called last in the ring-0 VM termination. After this point anyone is
 * allowed to destroy the VM. Ideally, we should associate the VM with the thread
 * that's going to call GVMMR0DestroyVM for optimal security, but that's impractical
 * at present.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The VM instance data (aka handle), ring-0 mapping of course.
 * @thread  EMT.
 */
GVMMR0DECL(int) GVMMR0DisassociateEMTFromVM(PVM pVM)
{
    LogFlow(("GVMMR0DisassociateEMTFromVM: pVM=%p\n", pVM));
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_INTERNAL_ERROR);

    /*
     * Validate the VM structure, state and handle.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertReturn(!((uintptr_t)pVM & PAGE_OFFSET_MASK), VERR_INVALID_POINTER);
    AssertMsgReturn(pVM->enmVMState >= VMSTATE_CREATING && pVM->enmVMState <= VMSTATE_DESTROYING, ("%d\n", pVM->enmVMState), VERR_WRONG_ORDER);

    RTNATIVETHREAD hEMT = RTThreadNativeSelf();
    AssertReturn(hEMT != NIL_RTNATIVETHREAD, VERR_NOT_SUPPORTED);

    const uint16_t hGVM = pVM->hSelf;
    AssertReturn(hGVM != NIL_GVM_HANDLE, VERR_INVALID_HANDLE);
    AssertReturn(hGVM < RT_ELEMENTS(pGVMM->aHandles), VERR_INVALID_HANDLE);

    PGVMHANDLE pHandle = &pGVMM->aHandles[hGVM];
    AssertReturn(pHandle->pVM == pVM, VERR_NOT_OWNER);

    /*
     * Take the lock, validate the handle and update the structure members.
     */
    int rc = RTSemFastMutexRequest(pGVMM->CreateDestroyLock);
    AssertRCReturn(rc, rc);
    rc = RTSemFastMutexRequest(pGVMM->UsedLock);
    AssertRC(rc);

    if (    VALID_PTR(pHandle->pvObj)
        &&  VALID_PTR(pHandle->pSession)
        &&  VALID_PTR(pHandle->pGVM)
        &&  pHandle->pGVM->u32Magic == GVM_MAGIC)
    {
        if (    pHandle->pVM == pVM
            &&  pHandle->hEMT == hEMT)
        {
            pHandle->hEMT = NIL_RTNATIVETHREAD;
            pHandle->pGVM->hEMT = NIL_RTNATIVETHREAD;
        }
        else
            rc = VERR_NOT_OWNER;
    }
    else
        rc = VERR_INVALID_HANDLE;

    RTSemFastMutexRelease(pGVMM->UsedLock);
    RTSemFastMutexRelease(pGVMM->CreateDestroyLock);
    LogFlow(("GVMMR0DisassociateEMTFromVM: returns %Vrc (hEMT=%RTnthrd)\n", rc, hEMT));
    return rc;
}


/**
 * Destroys the VM, freeing all associated resources (the ring-0 ones anyway).
 *
 * This is call from the vmR3DestroyFinalBit and from a error path in VMR3Create,
 * and the caller is not the EMT thread, unfortunately. For security reasons, it
 * would've been nice if the caller was actually the EMT thread or that we somehow
 * could've associated the calling thread with the VM up front.
 *
 * @returns VBox status code.
 * @param   pVM         Where to store the pointer to the VM structure.
 *
 * @thread  EMT if it's associated with the VM, otherwise any thread.
 */
GVMMR0DECL(int) GVMMR0DestroyVM(PVM pVM)
{
    LogFlow(("GVMMR0DestroyVM: pVM=%p\n", pVM));
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_INTERNAL_ERROR);


    /*
     * Validate the VM structure, state and caller.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertReturn(!((uintptr_t)pVM & PAGE_OFFSET_MASK), VERR_INVALID_POINTER);
    AssertMsgReturn(pVM->enmVMState >= VMSTATE_CREATING && pVM->enmVMState <= VMSTATE_TERMINATED, ("%d\n", pVM->enmVMState), VERR_WRONG_ORDER);

    uint32_t hGVM = pVM->hSelf;
    AssertReturn(hGVM != NIL_GVM_HANDLE, VERR_INVALID_HANDLE);
    AssertReturn(hGVM < RT_ELEMENTS(pGVMM->aHandles), VERR_INVALID_HANDLE);

    PGVMHANDLE pHandle = &pGVMM->aHandles[hGVM];
    AssertReturn(pHandle->pVM == pVM, VERR_NOT_OWNER);

    RTNATIVETHREAD hSelf = RTThreadNativeSelf();
    AssertReturn(pHandle->hEMT == hSelf || pHandle->hEMT == NIL_RTNATIVETHREAD, VERR_NOT_OWNER);

    /*
     * Lookup the handle and destroy the object.
     * Since the lock isn't recursive and we'll have to leave it before dereferencing the
     * object, we take some precautions against racing callers just in case...
     */
    int rc = RTSemFastMutexRequest(pGVMM->CreateDestroyLock);
    AssertRC(rc);

    /* be careful here because we might theoretically be racing someone else cleaning up. */
    if (    pHandle->pVM == pVM
        &&  (   pHandle->hEMT == hSelf
             || pHandle->hEMT == NIL_RTNATIVETHREAD)
        &&  VALID_PTR(pHandle->pvObj)
        &&  VALID_PTR(pHandle->pSession)
        &&  VALID_PTR(pHandle->pGVM)
        &&  pHandle->pGVM->u32Magic == GVM_MAGIC)
    {
        void *pvObj = pHandle->pvObj;
        pHandle->pvObj = NULL;
        RTSemFastMutexRelease(pGVMM->CreateDestroyLock);

        SUPR0ObjRelease(pvObj, pHandle->pSession);
    }
    else
    {
        SUPR0Printf("GVMMR0DestroyVM: pHandle=%p:{.pVM=%p, hEMT=%p, .pvObj=%p} pVM=%p hSelf=%p\n",
                    pHandle, pHandle->pVM, pHandle->hEMT, pHandle->pvObj, pVM, hSelf);
        RTSemFastMutexRelease(pGVMM->CreateDestroyLock);
        rc = VERR_INTERNAL_ERROR;
    }

    return rc;
}


/**
 * Handle destructor.
 *
 * @param   pvGVMM       The GVM instance pointer.
 * @param   pvHandle    The handle pointer.
 */
static DECLCALLBACK(void) gvmmR0HandleObjDestructor(void *pvObj, void *pvGVMM, void *pvHandle)
{
    LogFlow(("gvmmR0HandleObjDestructor: %p %p %p\n", pvObj, pvGVMM, pvHandle));

    /*
     * Some quick, paranoid, input validation.
     */
    PGVMHANDLE pHandle = (PGVMHANDLE)pvHandle;
    AssertPtr(pHandle);
    PGVMM pGVMM = (PGVMM)pvGVMM;
    Assert(pGVMM == g_pGVMM);
    const uint16_t iHandle = pHandle - &pGVMM->aHandles[0];
    if (    !iHandle
        ||  iHandle >= RT_ELEMENTS(pGVMM->aHandles)
        ||  iHandle != pHandle->iSelf)
    {
        SUPR0Printf("GVM: handle %d is out of range or corrupt (iSelf=%d)!\n", iHandle, pHandle->iSelf);
        return;
    }

    int rc = RTSemFastMutexRequest(pGVMM->CreateDestroyLock);
    AssertRC(rc);
    rc = RTSemFastMutexRequest(pGVMM->UsedLock);
    AssertRC(rc);

    /*
     * This is a tad slow but a doubly linked list is too much hazzle.
     */
    if (RT_UNLIKELY(pHandle->iNext >= RT_ELEMENTS(pGVMM->aHandles)))
    {
        SUPR0Printf("GVM: used list index %d is out of range!\n", pHandle->iNext);
        RTSemFastMutexRelease(pGVMM->UsedLock);
        RTSemFastMutexRelease(pGVMM->CreateDestroyLock);
        return;
    }

    if (pGVMM->iUsedHead == iHandle)
        pGVMM->iUsedHead = pHandle->iNext;
    else
    {
        uint16_t iPrev = pGVMM->iUsedHead;
        int c = RT_ELEMENTS(pGVMM->aHandles) + 2;
        while (!iPrev)
        {
            if (RT_UNLIKELY(iPrev >= RT_ELEMENTS(pGVMM->aHandles)))
            {
                SUPR0Printf("GVM: used list index %d is out of range!\n");
                RTSemFastMutexRelease(pGVMM->UsedLock);
                RTSemFastMutexRelease(pGVMM->CreateDestroyLock);
                return;
            }
            if (RT_UNLIKELY(c-- <= 0))
            {
                iPrev = 0;
                break;
            }

            if (pGVMM->aHandles[iPrev].iNext == iHandle)
                break;
            iPrev = pGVMM->aHandles[iPrev].iNext;
        }
        if (!iPrev)
        {
            SUPR0Printf("GVM: can't find the handle previous previous of %d!\n", pHandle->iSelf);
            RTSemFastMutexRelease(pGVMM->UsedLock);
            RTSemFastMutexRelease(pGVMM->CreateDestroyLock);
            return;
        }

        pGVMM->aHandles[iPrev].iNext = pHandle->iNext;
    }
    pHandle->iNext = 0;
    pGVMM->cVMs--;

    RTSemFastMutexRelease(pGVMM->UsedLock);

    /*
     * Do the global cleanup round.
     */
    PGVM pGVM = pHandle->pGVM;
    if (    VALID_PTR(pGVM)
        &&  pGVM->u32Magic == GVM_MAGIC)
    {
        /// @todo GMMR0CleanupVM(pGVM);

        /*
         * Do the GVMM cleanup - must be done last.
         */
        /* The VM and VM pages mappings/allocations. */
        if (pGVM->gvmm.s.VMPagesMapObj != NIL_RTR0MEMOBJ)
        {
            rc = RTR0MemObjFree(pGVM->gvmm.s.VMPagesMapObj, false /* fFreeMappings */); AssertRC(rc);
            pGVM->gvmm.s.VMPagesMapObj = NIL_RTR0MEMOBJ;
        }

        if (pGVM->gvmm.s.VMMapObj != NIL_RTR0MEMOBJ)
        {
            rc = RTR0MemObjFree(pGVM->gvmm.s.VMMapObj, false /* fFreeMappings */); AssertRC(rc);
            pGVM->gvmm.s.VMMapObj = NIL_RTR0MEMOBJ;
        }

        if (pGVM->gvmm.s.VMPagesMemObj != NIL_RTR0MEMOBJ)
        {
            rc = RTR0MemObjFree(pGVM->gvmm.s.VMPagesMemObj, false /* fFreeMappings */); AssertRC(rc);
            pGVM->gvmm.s.VMPagesMemObj = NIL_RTR0MEMOBJ;
        }

        if (pGVM->gvmm.s.VMMemObj != NIL_RTR0MEMOBJ)
        {
            rc = RTR0MemObjFree(pGVM->gvmm.s.VMMemObj, false /* fFreeMappings */); AssertRC(rc);
            pGVM->gvmm.s.VMMemObj = NIL_RTR0MEMOBJ;
        }

        /* the GVM structure itself. */
        pGVM->u32Magic++;
        RTMemFree(pGVM);
    }
    /* else: GVMMR0CreateVM cleanup.  */

    /*
     * Free the handle.
     * Reacquire the UsedLock here to since we're updating handle fields.
     */
    rc = RTSemFastMutexRequest(pGVMM->UsedLock);
    AssertRC(rc);

    pHandle->iNext = pGVMM->iFreeHead;
    pGVMM->iFreeHead = iHandle;
    ASMAtomicXchgPtr((void * volatile *)&pHandle->pGVM, NULL);
    ASMAtomicXchgPtr((void * volatile *)&pHandle->pVM, NULL);
    ASMAtomicXchgPtr((void * volatile *)&pHandle->pvObj, NULL);
    ASMAtomicXchgPtr((void * volatile *)&pHandle->pSession, NULL);
    ASMAtomicXchgSize(&pHandle->hEMT, NIL_RTNATIVETHREAD);

    RTSemFastMutexRelease(pGVMM->UsedLock);
    RTSemFastMutexRelease(pGVMM->CreateDestroyLock);
    LogFlow(("gvmmR0HandleObjDestructor: returns\n"));
}


/**
 * Lookup a GVM structure by its handle.
 *
 * @returns The GVM pointer on success, NULL on failure.
 * @param   hGVM    The global VM handle. Asserts on bad handle.
 */
GVMMR0DECL(PGVM) GVMMR0ByHandle(uint32_t hGVM)
{
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, NULL);

    /*
     * Validate.
     */
    AssertReturn(hGVM != NIL_GVM_HANDLE, NULL);
    AssertReturn(hGVM < RT_ELEMENTS(pGVMM->aHandles), NULL);

    /*
     * Look it up.
     */
    PGVMHANDLE pHandle = &pGVMM->aHandles[hGVM];
    AssertPtrReturn(pHandle->pVM, NULL);
    AssertPtrReturn(pHandle->pvObj, NULL);
    PGVM pGVM = pHandle->pGVM;
    AssertPtrReturn(pGVM, NULL);
    AssertReturn(pGVM->pVM == pHandle->pVM, NULL);

    return pHandle->pGVM;
}


/**
 * Lookup a GVM structure by the shared VM structure.
 *
 * @returns VBox status code.
 * @param   pVM             The shared VM structure (the ring-0 mapping).
 * @param   ppGVM           Where to store the GVM pointer.
 * @param   ppGVMM          Where to store the pointer to the GVMM instance data.
 * @param   fTakeUsedLock   Whether to take the used lock or not.
 *                          Be very careful if not taking the lock as it's possible that
 *                          the VM will disappear then.
 *
 * @remark  This will not assert on an invalid pVM but try return sliently.
 */
static int gvmmR0ByVM(PVM pVM, PGVM *ppGVM, PGVMM *ppGVMM, bool fTakeUsedLock)
{
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_INTERNAL_ERROR);

    /*
     * Validate.
     */
    if (RT_UNLIKELY(    !VALID_PTR(pVM)
                    ||  ((uintptr_t)pVM & PAGE_OFFSET_MASK)))
        return VERR_INVALID_POINTER;
    if (RT_UNLIKELY(    pVM->enmVMState < VMSTATE_CREATING
                    ||  pVM->enmVMState >= VMSTATE_TERMINATED))
        return VERR_INVALID_POINTER;

    uint16_t hGVM = pVM->hSelf;
    if (RT_UNLIKELY(    hGVM == NIL_GVM_HANDLE
                    ||  hGVM >= RT_ELEMENTS(pGVMM->aHandles)))
        return VERR_INVALID_HANDLE;

    /*
     * Look it up.
     */
    PGVMHANDLE pHandle = &pGVMM->aHandles[hGVM];
    PGVM pGVM;
    if (fTakeUsedLock)
    {
        int rc = RTSemFastMutexRequest(pGVMM->UsedLock);
        AssertRCReturn(rc, rc);

        pGVM = pHandle->pGVM;
        if (RT_UNLIKELY(    pHandle->pVM != pVM
                        ||  !VALID_PTR(pHandle->pvObj)
                        ||  !VALID_PTR(pGVM)
                        ||  pGVM->pVM != pVM))
        {
            RTSemFastMutexRelease(pGVMM->UsedLock);
            return VERR_INVALID_HANDLE;
        }
    }
    else
    {
        if (RT_UNLIKELY(pHandle->pVM != pVM))
            return VERR_INVALID_HANDLE;
        if (RT_UNLIKELY(!VALID_PTR(pHandle->pvObj)))
            return VERR_INVALID_HANDLE;

        pGVM = pHandle->pGVM;
        if (!RT_UNLIKELY(!VALID_PTR(pGVM)))
            return VERR_INVALID_HANDLE;
        if (!RT_UNLIKELY(pGVM->pVM != pVM))
            return VERR_INVALID_HANDLE;
    }

    *ppGVM = pGVM;
    *ppGVMM = pGVMM;
    return VINF_SUCCESS;
}


/**
 * Lookup a GVM structure by the shared VM structure.
 *
 * @returns The GVM pointer on success, NULL on failure.
 * @param   pVM     The shared VM structure (the ring-0 mapping).
 */
GVMMR0DECL(PGVM) GVMMR0ByVM(PVM pVM)
{
    PGVMM pGVMM;
    PGVM pGVM;
    int rc = gvmmR0ByVM(pVM, &pGVM, &pGVMM, false /* fTakeUsedLock */);
    if (RT_SUCCESS(rc))
        return pGVM;
    AssertRC(rc);
    return NULL;
}


/**
 * Lookup a GVM structure by the shared VM structure
 * and ensuring that the caller is the EMT thread.
 *
 * @returns VBox status code.
 * @param   pVM         The shared VM structure (the ring-0 mapping).
 * @param   ppGVM       Where to store the GVM pointer.
 * @param   ppGVMM      Where to store the pointer to the GVMM instance data.
 * @thread  EMT
 *
 * @remark  This will assert in failure paths.
 */
static int gvmmR0ByVMAndEMT(PVM pVM, PGVM *ppGVM, PGVMM *ppGVMM)
{
    PGVMM pGVMM;
    GVMM_GET_VALID_INSTANCE(pGVMM, VERR_INTERNAL_ERROR);

    /*
     * Validate.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertReturn(!((uintptr_t)pVM & PAGE_OFFSET_MASK), VERR_INVALID_POINTER);

    uint16_t hGVM = pVM->hSelf;
    AssertReturn(hGVM != NIL_GVM_HANDLE, VERR_INVALID_HANDLE);
    AssertReturn(hGVM < RT_ELEMENTS(pGVMM->aHandles), VERR_INVALID_HANDLE);

    /*
     * Look it up.
     */
    PGVMHANDLE pHandle = &pGVMM->aHandles[hGVM];
    RTNATIVETHREAD hAllegedEMT = RTThreadNativeSelf();
    AssertMsgReturn(pHandle->hEMT == hAllegedEMT, ("hEMT %x hAllegedEMT %x\n", pHandle->hEMT, hAllegedEMT), VERR_NOT_OWNER);
    AssertReturn(pHandle->pVM == pVM, VERR_NOT_OWNER);
    AssertPtrReturn(pHandle->pvObj, VERR_INTERNAL_ERROR);

    PGVM pGVM = pHandle->pGVM;
    AssertPtrReturn(pGVM, VERR_INTERNAL_ERROR);
    AssertReturn(pGVM->pVM == pVM, VERR_INTERNAL_ERROR);
    AssertReturn(pGVM->hEMT == hAllegedEMT, VERR_INTERNAL_ERROR);

    *ppGVM = pGVM;
    *ppGVMM = pGVMM;
    return VINF_SUCCESS;
}


/**
 * Lookup a GVM structure by the shared VM structure
 * and ensuring that the caller is the EMT thread.
 *
 * @returns VBox status code.
 * @param   pVM         The shared VM structure (the ring-0 mapping).
 * @param   ppGVM       Where to store the GVM pointer.
 * @thread  EMT
 */
GVMMR0DECL(int) GVMMR0ByVMAndEMT(PVM pVM, PGVM *ppGVM)
{
    AssertPtrReturn(ppGVM, VERR_INVALID_POINTER);
    PGVMM pGVMM;
    return gvmmR0ByVMAndEMT(pVM, ppGVM, &pGVMM);
}


/**
 * Lookup a VM by its global handle.
 *
 * @returns The VM handle on success, NULL on failure.
 * @param   hGVM    The global VM handle. Asserts on bad handle.
 */
GVMMR0DECL(PVM) GVMMR0GetVMByHandle(uint32_t hGVM)
{
    PGVM pGVM = GVMMR0ByHandle(hGVM);
    return pGVM ? pGVM->pVM : NULL;
}


/**
 * Looks up the VM belonging to the specified EMT thread.
 *
 * This is used by the assertion machinery in VMMR0.cpp to avoid causing
 * unnecessary kernel panics when the EMT thread hits an assertion. The
 * call may or not be an EMT thread.
 *
 * @returns The VM handle on success, NULL on failure.
 * @param   hEMT    The native thread handle of the EMT.
 *                  NIL_RTNATIVETHREAD means the current thread
 */
GVMMR0DECL(PVM) GVMMR0GetVMByEMT(RTNATIVETHREAD hEMT)
{
    /*
     * No Assertions here as we're usually called in a AssertMsgN or
     * RTAssert* context.
     */
    PGVMM pGVMM = g_pGVMM;
    if (    !VALID_PTR(pGVMM)
        ||  pGVMM->u32Magic != GVMM_MAGIC)
        return NULL;

    if (hEMT == NIL_RTNATIVETHREAD)
        hEMT = RTThreadNativeSelf();

    /*
     * Search the handles in a linear fashion as we don't dare take the lock (assert).
     */
    for (unsigned i = 1; i < RT_ELEMENTS(pGVMM->aHandles); i++)
        if (    pGVMM->aHandles[i].hEMT == hEMT
            &&  pGVMM->aHandles[i].iSelf == i
            &&  VALID_PTR(pGVMM->aHandles[i].pvObj)
            &&  VALID_PTR(pGVMM->aHandles[i].pVM))
            return pGVMM->aHandles[i].pVM;

    return NULL;
}


/**
 * This is will wake up expired and soon-to-be expired VMs.
 *
 * @returns Number of VMs that has been woken up.
 * @param   pGVMM       Pointer to the GVMM instance data.
 * @param   u64Now      The current time.
 */
static unsigned gvmmR0SchedDoWakeUps(PGVMM pGVMM, uint64_t u64Now)
{
    /*
     * The first pass will wake up VMs which has actually expired
     * and look for VMs that should be woken up in the 2nd and 3rd passes.
     */
    unsigned cWoken = 0;
    unsigned cHalted = 0;
    unsigned cTodo2nd = 0;
    unsigned cTodo3rd = 0;
    for (unsigned i = pGVMM->iUsedHead;
         i != NIL_GVM_HANDLE && i < RT_ELEMENTS(pGVMM->aHandles);
         i = pGVMM->aHandles[i].iNext)
    {
        PGVM pCurGVM = pGVMM->aHandles[i].pGVM;
        if (    VALID_PTR(pCurGVM)
            &&  pCurGVM->u32Magic == GVM_MAGIC)
        {
            uint64_t u64 = pCurGVM->gvmm.s.u64HaltExpire;
            if (u64)
            {
                if (u64 <= u64Now)
                {
                    if (ASMAtomicXchgU64(&pCurGVM->gvmm.s.u64HaltExpire, 0))
                    {
                        int rc = RTSemEventMultiSignal(pCurGVM->gvmm.s.HaltEventMulti);
                        AssertRC(rc);
                        cWoken++;
                    }
                }
                else
                {
                    cHalted++;
                    if (u64 <= u64Now + pGVMM->nsEarlyWakeUp1)
                        cTodo2nd++;
                    else if (u64 <= u64Now + pGVMM->nsEarlyWakeUp2)
                        cTodo3rd++;
                }
            }
        }
    }

    if (cTodo2nd)
    {
        for (unsigned i = pGVMM->iUsedHead;
             i != NIL_GVM_HANDLE && i < RT_ELEMENTS(pGVMM->aHandles);
             i = pGVMM->aHandles[i].iNext)
        {
            PGVM pCurGVM = pGVMM->aHandles[i].pGVM;
            if (    VALID_PTR(pCurGVM)
                &&  pCurGVM->u32Magic == GVM_MAGIC
                &&  pCurGVM->gvmm.s.u64HaltExpire
                &&  pCurGVM->gvmm.s.u64HaltExpire <= u64Now + pGVMM->nsEarlyWakeUp1)
            {
                if (ASMAtomicXchgU64(&pCurGVM->gvmm.s.u64HaltExpire, 0))
                {
                    int rc = RTSemEventMultiSignal(pCurGVM->gvmm.s.HaltEventMulti);
                    AssertRC(rc);
                    cWoken++;
                }
            }
        }
    }

    if (cTodo3rd)
    {
        for (unsigned i = pGVMM->iUsedHead;
             i != NIL_GVM_HANDLE && i < RT_ELEMENTS(pGVMM->aHandles);
             i = pGVMM->aHandles[i].iNext)
        {
            PGVM pCurGVM = pGVMM->aHandles[i].pGVM;
            if (    VALID_PTR(pCurGVM)
                &&  pCurGVM->u32Magic == GVM_MAGIC
                &&  pCurGVM->gvmm.s.u64HaltExpire
                &&  pCurGVM->gvmm.s.u64HaltExpire <= u64Now + pGVMM->nsEarlyWakeUp2)
            {
                if (ASMAtomicXchgU64(&pCurGVM->gvmm.s.u64HaltExpire, 0))
                {
                    int rc = RTSemEventMultiSignal(pCurGVM->gvmm.s.HaltEventMulti);
                    AssertRC(rc);
                    cWoken++;
                }
            }
        }
    }

    return cWoken;
}


/**
 * Halt the EMT thread.
 *
 * @returns VINF_SUCCESS normal wakeup (timeout or kicked by other thread).
 *          VERR_INTERRUPTED if a signal was scheduled for the thread.
 * @param   pVM                 Pointer to the shared VM structure.
 * @param   u64ExpireGipTime    The time for the sleep to expire expressed as GIP time.
 * @thread  EMT.
 */
GVMMR0DECL(int) GVMMR0SchedHalt(PVM pVM, uint64_t u64ExpireGipTime)
{
    LogFlow(("GVMMR0DisassociateEMTFromVM: pVM=%p\n", pVM));

    /*
     * Validate the VM structure, state and handle.
     */
    PGVMM pGVMM;
    PGVM pGVM;
    int rc = gvmmR0ByVMAndEMT(pVM, &pGVM, &pGVMM);
    if (RT_FAILURE(rc))
        return rc;
    pGVM->gvmm.s.StatsSched.cHaltCalls++;

    Assert(!pGVM->gvmm.s.u64HaltExpire);

    /*
     * Take the UsedList semaphore, get the current time
     * and check if anyone needs waking up.
     * Interrupts must NOT be disabled at this point because we ask for GIP time!
     */
    rc = RTSemFastMutexRequest(pGVMM->UsedLock);
    AssertRC(rc);

    pGVM->gvmm.s.iCpuEmt = ASMGetApicId();

    Assert(ASMGetFlags() & X86_EFL_IF);
    const uint64_t u64Now = RTTimeNanoTS(); /* (GIP time) */
    pGVM->gvmm.s.StatsSched.cHaltWakeUps += gvmmR0SchedDoWakeUps(pGVMM, u64Now);

    /*
     * Go to sleep if we must...
     */
    if (    u64Now < u64ExpireGipTime
        &&  u64ExpireGipTime - u64Now > (pGVMM->cVMs > pGVMM->cVMsMeansCompany
                                         ? pGVMM->nsMinSleepCompany
                                         : pGVMM->nsMinSleepAlone))
    {
        pGVM->gvmm.s.StatsSched.cHaltBlocking++;
        ASMAtomicXchgU64(&pGVM->gvmm.s.u64HaltExpire, u64ExpireGipTime);
        RTSemFastMutexRelease(pGVMM->UsedLock);

        uint32_t cMillies = (u64ExpireGipTime - u64Now) / 1000000;
        rc = RTSemEventMultiWaitNoResume(pGVM->gvmm.s.HaltEventMulti, cMillies ? cMillies : 1);
        ASMAtomicXchgU64(&pGVM->gvmm.s.u64HaltExpire, 0);
        if (rc == VERR_TIMEOUT)
        {
            pGVM->gvmm.s.StatsSched.cHaltTimeouts++;
            rc = VINF_SUCCESS;
        }
    }
    else
    {
        pGVM->gvmm.s.StatsSched.cHaltNotBlocking++;
        RTSemFastMutexRelease(pGVMM->UsedLock);
    }

    /* Make sure false wake up calls (gvmmR0SchedDoWakeUps) cause us to spin. */
    RTSemEventMultiReset(pGVM->gvmm.s.HaltEventMulti);

    return rc;
}


/**
 * Wakes up the halted EMT thread so it can service a pending request.
 *
 * @returns VINF_SUCCESS if not yielded.
 *          VINF_GVM_NOT_BLOCKED if the EMT thread wasn't blocked.
 * @param   pVM                 Pointer to the shared VM structure.
 * @thread  Any but EMT.
 */
GVMMR0DECL(int) GVMMR0SchedWakeUp(PVM pVM)
{
    /*
     * Validate input and take the UsedLock.
     */
    PGVM pGVM;
    PGVMM pGVMM;
    int rc = gvmmR0ByVM(pVM, &pGVM, &pGVMM, true /* fTakeUsedLock */);
    if (RT_SUCCESS(rc))
    {
        pGVM->gvmm.s.StatsSched.cWakeUpCalls++;

        /*
         * Signal the semaphore regardless of whether it's current blocked on it.
         *
         * The reason for this is that there is absolutely no way we can be 100%
         * certain that it isn't *about* go to go to sleep on it and just got
         * delayed a bit en route. So, we will always signal the semaphore when
         * the it is flagged as halted in the VMM.
         */
        if (pGVM->gvmm.s.u64HaltExpire)
        {
            rc = VINF_SUCCESS;
            ASMAtomicXchgU64(&pGVM->gvmm.s.u64HaltExpire, 0);
        }
        else
        {
            rc = VINF_GVM_NOT_BLOCKED;
            pGVM->gvmm.s.StatsSched.cWakeUpNotHalted++;
        }

        int rc2 = RTSemEventMultiSignal(pGVM->gvmm.s.HaltEventMulti);
        AssertRC(rc2);

        /*
         * While we're here, do a round of scheduling.
         */
        Assert(ASMGetFlags() & X86_EFL_IF);
        const uint64_t u64Now = RTTimeNanoTS(); /* (GIP time) */
        pGVM->gvmm.s.StatsSched.cWakeUpWakeUps += gvmmR0SchedDoWakeUps(pGVMM, u64Now);


        rc2 = RTSemFastMutexRelease(pGVMM->UsedLock);
        AssertRC(rc2);
    }

    LogFlow(("GVMMR0SchedWakeUp: returns %Rrc\n", rc));
    return rc;
}


/**
 * Poll the schedule to see if someone else should get a chance to run.
 *
 * This is a bit hackish and will not work too well if the machine is
 * under heavy load from non-VM processes.
 *
 * @returns VINF_SUCCESS if not yielded.
 *          VINF_GVM_YIELDED if an attempt to switch to a different VM task was made.
 * @param   pVM                 Pointer to the shared VM structure.
 * @param   u64ExpireGipTime    The time for the sleep to expire expressed as GIP time.
 * @param   fYield              Whether to yield or not.
 *                              This is for when we're spinning in the halt loop.
 * @thread  EMT.
 */
GVMMR0DECL(int) GVMMR0SchedPoll(PVM pVM, bool fYield)
{
    /*
     * Validate input.
     */
    PGVM pGVM;
    PGVMM pGVMM;
    int rc = gvmmR0ByVMAndEMT(pVM, &pGVM, &pGVMM);
    if (RT_SUCCESS(rc))
    {
        rc = RTSemFastMutexRequest(pGVMM->UsedLock);
        AssertRC(rc);
        pGVM->gvmm.s.StatsSched.cPollCalls++;

        Assert(ASMGetFlags() & X86_EFL_IF);
        const uint64_t u64Now = RTTimeNanoTS(); /* (GIP time) */

        if (!fYield)
            pGVM->gvmm.s.StatsSched.cPollWakeUps += gvmmR0SchedDoWakeUps(pGVMM, u64Now);
        else
        {
            /** @todo implement this... */
            rc = VERR_NOT_IMPLEMENTED;
        }

        RTSemFastMutexRelease(pGVMM->UsedLock);
    }

    LogFlow(("GVMMR0SchedWakeUp: returns %Rrc\n", rc));
    return rc;
}



/**
 * Retrieves the GVMM statistics visible to the caller.
 *
 * @returns VBox status code.
 *
 * @param   pStats      Where to put the statistics.
 * @param   pSession    The current session.
 * @param   pVM         The VM to obtain statistics for. Optional.
 */
GVMMR0DECL(int) GVMMR0QueryStatistics(PGVMMSTATS pStats, PSUPDRVSESSION pSession, PVM pVM)
{
    LogFlow(("GVMMR0QueryStatistics: pStats=%p pSession=%p pVM=%p\n", pStats, pSession, pVM));

    /*
     * Validate input.
     */
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pStats, VERR_INVALID_POINTER);
    pStats->cVMs = 0; /* (crash before taking the sem...) */

    /*
     * Take the lock and get the VM statistics.
     */
    PGVMM pGVMM;
    if (pVM)
    {
        PGVM pGVM;
        int rc = gvmmR0ByVM(pVM, &pGVM, &pGVMM, true /*fTakeUsedLock*/);
        if (RT_FAILURE(rc))
            return rc;
        pStats->SchedVM = pGVM->gvmm.s.StatsSched;
    }
    else
    {
        GVMM_GET_VALID_INSTANCE(pGVMM, VERR_INTERNAL_ERROR);
        memset(&pStats->SchedVM, 0, sizeof(pStats->SchedVM));

        int rc = RTSemFastMutexRequest(pGVMM->UsedLock);
        AssertRCReturn(rc, rc);
    }

    /*
     * Enumerate the VMs and add the ones visibile to the statistics.
     */
    pStats->cVMs = 0;
    memset(&pStats->SchedSum, 0, sizeof(pStats->SchedSum));

    for (unsigned i = pGVMM->iUsedHead;
         i != NIL_GVM_HANDLE && i < RT_ELEMENTS(pGVMM->aHandles);
         i = pGVMM->aHandles[i].iNext)
    {
        PGVM pGVM = pGVMM->aHandles[i].pGVM;
        void *pvObj = pGVMM->aHandles[i].pvObj;
        if (    VALID_PTR(pvObj)
            &&  VALID_PTR(pGVM)
            &&  pGVM->u32Magic == GVM_MAGIC
            &&  RT_SUCCESS(SUPR0ObjVerifyAccess(pvObj, pSession, NULL)))
        {
            pStats->cVMs++;

            pStats->SchedSum.cHaltCalls        += pGVM->gvmm.s.StatsSched.cHaltCalls;
            pStats->SchedSum.cHaltBlocking     += pGVM->gvmm.s.StatsSched.cHaltBlocking;
            pStats->SchedSum.cHaltTimeouts     += pGVM->gvmm.s.StatsSched.cHaltTimeouts;
            pStats->SchedSum.cHaltNotBlocking  += pGVM->gvmm.s.StatsSched.cHaltNotBlocking;
            pStats->SchedSum.cHaltWakeUps      += pGVM->gvmm.s.StatsSched.cHaltWakeUps;

            pStats->SchedSum.cWakeUpCalls      += pGVM->gvmm.s.StatsSched.cWakeUpCalls;
            pStats->SchedSum.cWakeUpNotHalted  += pGVM->gvmm.s.StatsSched.cWakeUpNotHalted;
            pStats->SchedSum.cWakeUpWakeUps    += pGVM->gvmm.s.StatsSched.cWakeUpWakeUps;

            pStats->SchedSum.cPollCalls        += pGVM->gvmm.s.StatsSched.cPollCalls;
            pStats->SchedSum.cPollHalts        += pGVM->gvmm.s.StatsSched.cPollHalts;
            pStats->SchedSum.cPollWakeUps      += pGVM->gvmm.s.StatsSched.cPollWakeUps;
        }
    }

    RTSemFastMutexRelease(pGVMM->UsedLock);

    return VINF_SUCCESS;
}


/**
 * VMMR0 request wrapper for GVMMR0QueryStatistics.
 *
 * @returns see GVMMR0QueryStatistics.
 * @param   pVM             Pointer to the shared VM structure. Optional.
 * @param   pReq            The request packet.
 */
GVMMR0DECL(int) GVMMR0QueryStatisticsReq(PVM pVM, PGVMMQUERYSTATISTICSSREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GVMMR0QueryStatistics(&pReq->Stats, pReq->pSession, pVM);
}


/**
 * Resets the specified GVMM statistics.
 *
 * @returns VBox status code.
 *
 * @param   pStats      Which statistics to reset, that is, non-zero fields indicates which to reset.
 * @param   pSession    The current session.
 * @param   pVM         The VM to reset statistics for. Optional.
 */
GVMMR0DECL(int) GVMMR0ResetStatistics(PCGVMMSTATS pStats, PSUPDRVSESSION pSession, PVM pVM)
{
    LogFlow(("GVMMR0ResetStatistics: pStats=%p pSession=%p pVM=%p\n", pStats, pSession, pVM));

    /*
     * Validate input.
     */
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pStats, VERR_INVALID_POINTER);

    /*
     * Take the lock and get the VM statistics.
     */
    PGVMM pGVMM;
    if (pVM)
    {
        PGVM pGVM;
        int rc = gvmmR0ByVM(pVM, &pGVM, &pGVMM, true /*fTakeUsedLock*/);
        if (RT_FAILURE(rc))
            return rc;
#       define MAYBE_RESET_FIELD(field) \
            do { if (pStats->SchedVM. field ) { pGVM->gvmm.s.StatsSched. field = 0; } } while (0)
        MAYBE_RESET_FIELD(cHaltCalls);
        MAYBE_RESET_FIELD(cHaltBlocking);
        MAYBE_RESET_FIELD(cHaltTimeouts);
        MAYBE_RESET_FIELD(cHaltNotBlocking);
        MAYBE_RESET_FIELD(cHaltWakeUps);
        MAYBE_RESET_FIELD(cWakeUpCalls);
        MAYBE_RESET_FIELD(cWakeUpNotHalted);
        MAYBE_RESET_FIELD(cWakeUpWakeUps);
        MAYBE_RESET_FIELD(cPollCalls);
        MAYBE_RESET_FIELD(cPollHalts);
        MAYBE_RESET_FIELD(cPollWakeUps);
#       undef MAYBE_RESET_FIELD
    }
    else
    {
        GVMM_GET_VALID_INSTANCE(pGVMM, VERR_INTERNAL_ERROR);

        int rc = RTSemFastMutexRequest(pGVMM->UsedLock);
        AssertRCReturn(rc, rc);
    }

    /*
     * Enumerate the VMs and add the ones visibile to the statistics.
     */
    if (ASMMemIsAll8(&pStats->SchedSum, sizeof(pStats->SchedSum), 0))
    {
        for (unsigned i = pGVMM->iUsedHead;
             i != NIL_GVM_HANDLE && i < RT_ELEMENTS(pGVMM->aHandles);
             i = pGVMM->aHandles[i].iNext)
        {
            PGVM pGVM = pGVMM->aHandles[i].pGVM;
            void *pvObj = pGVMM->aHandles[i].pvObj;
            if (    VALID_PTR(pvObj)
                &&  VALID_PTR(pGVM)
                &&  pGVM->u32Magic == GVM_MAGIC
                &&  RT_SUCCESS(SUPR0ObjVerifyAccess(pvObj, pSession, NULL)))
            {
#               define MAYBE_RESET_FIELD(field) \
                    do { if (pStats->SchedSum. field ) { pGVM->gvmm.s.StatsSched. field = 0; } } while (0)
                MAYBE_RESET_FIELD(cHaltCalls);
                MAYBE_RESET_FIELD(cHaltBlocking);
                MAYBE_RESET_FIELD(cHaltTimeouts);
                MAYBE_RESET_FIELD(cHaltNotBlocking);
                MAYBE_RESET_FIELD(cHaltWakeUps);
                MAYBE_RESET_FIELD(cWakeUpCalls);
                MAYBE_RESET_FIELD(cWakeUpNotHalted);
                MAYBE_RESET_FIELD(cWakeUpWakeUps);
                MAYBE_RESET_FIELD(cPollCalls);
                MAYBE_RESET_FIELD(cPollHalts);
                MAYBE_RESET_FIELD(cPollWakeUps);
#               undef MAYBE_RESET_FIELD
            }
        }
    }

    RTSemFastMutexRelease(pGVMM->UsedLock);

    return VINF_SUCCESS;
}


/**
 * VMMR0 request wrapper for GVMMR0ResetStatistics.
 *
 * @returns see GVMMR0ResetStatistics.
 * @param   pVM             Pointer to the shared VM structure. Optional.
 * @param   pReq            The request packet.
 */
GVMMR0DECL(int) GVMMR0ResetStatisticsReq(PVM pVM, PGVMMRESETSTATISTICSSREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GVMMR0ResetStatistics(&pReq->Stats, pReq->pSession, pVM);
}

