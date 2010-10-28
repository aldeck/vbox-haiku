/* $Id$ */
/** @file
 * VBoxNetFlt - Network Filter Driver (Host), Windows Specific Code. Integration with IntNet/NetFlt
 */

/*
 * Copyright (C) 2008 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
/*
 * Based in part on Microsoft DDK sample code for Ndis Intermediate Miniport passthru driver sample.
 * Copyright (c) 1993-1999, Microsoft Corporation
 */

#ifndef ___VBoxNetFlt_win_h___
#define ___VBoxNetFlt_win_h___

/*
 * globals
 */

/** global lock */
extern NDIS_SPIN_LOCK     g_GlobalLock;

extern UINT g_fPacketDontLoopBack;
extern UINT g_fPacketIsLoopedBack;

/*
 * Debug Print API
 */
#ifdef DEBUG

#define DBGPRINT(Fmt) DbgPrint Fmt

#else /* if DBG */

#define DBGPRINT(Fmt)

#endif /* if DBG */

/** get the PVBOXNETFLTINS from PADAPT */
#define PADAPT_2_PVBOXNETFLTINS(_pAdapt)  ( (PVBOXNETFLTINS)((uint8_t *)(_pAdapt) - RT_OFFSETOF(VBOXNETFLTINS, u.s.IfAdaptor)) )
/** get the PADAPT from PVBOXNETFLTINS */
#define PVBOXNETFLTINS_2_PADAPT(_pNetFlt) ( &(_pNetFlt)->u.s.IfAdaptor )


DECLHIDDEN(NTSTATUS) vboxNetFltWinPtDispatch(IN PDEVICE_OBJECT pDeviceObject, IN PIRP pIrp);
DECLHIDDEN(VOID) vboxNetFltWinUnload(IN PDRIVER_OBJECT DriverObject);

#ifndef VBOXNETADP
# if !defined(VBOX_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)
DECLHIDDEN(bool) vboxNetFltWinMatchPackets(PNDIS_PACKET pPacket1, PNDIS_PACKET pPacket2, const INT cbMatch);
DECLHIDDEN(bool) vboxNetFltWinMatchPacketAndSG(PNDIS_PACKET pPacket, PINTNETSG pSG, const INT cbMatch);
# endif
#endif

/*************************
 * packet queue API      *
 *************************/


#define LIST_ENTRY_2_PACKET_INFO(pListEntry) \
    ( (PPACKET_INFO)((uint8_t *)(pListEntry) - RT_OFFSETOF(PACKET_INFO, ListEntry)) )

#if !defined(VBOX_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)

#define VBOX_SLE_2_SEND_RSVD(_pEntry) \
    ( (PSEND_RSVD)((uint8_t *)(_pEntry) - RT_OFFSETOF(SEND_RSVD, ListEntry)) )

#define VBOX_SLE_2_SENDPACKET(_pEntry) \
    ( (PNDIS_PACKET)((uint8_t *)(VBOX_SLE_2_SEND_RSVD(_pEntry)) - RT_OFFSETOF(NDIS_PACKET, ProtocolReserved)) )

#endif
/**
 * enqueus the packet info to the tail of the queue
 */
DECLINLINE(void) vboxNetFltWinQuEnqueueTail(PPACKET_QUEUE pQueue, PPACKET_INFO pPacketInfo)
{
    InsertTailList(pQueue, &pPacketInfo->ListEntry);
}

DECLINLINE(void) vboxNetFltWinQuEnqueueHead(PPACKET_QUEUE pQueue, PPACKET_INFO pPacketInfo)
{
    Assert(pPacketInfo->pPool);
    InsertHeadList(pQueue, &pPacketInfo->ListEntry);
}

/**
 * enqueus the packet info to the tail of the queue
 */
DECLINLINE(void) vboxNetFltWinQuInterlockedEnqueueTail(PINTERLOCKED_PACKET_QUEUE pQueue, PPACKET_INFO pPacketInfo)
{
    Assert(pPacketInfo->pPool);
    NdisAcquireSpinLock(&pQueue->Lock);
    vboxNetFltWinQuEnqueueTail(&pQueue->Queue, pPacketInfo);
    NdisReleaseSpinLock(&pQueue->Lock);
}

DECLINLINE(void) vboxNetFltWinQuInterlockedEnqueueHead(PINTERLOCKED_PACKET_QUEUE pQueue, PPACKET_INFO pPacketInfo)
{
    NdisAcquireSpinLock(&pQueue->Lock);
    vboxNetFltWinQuEnqueueHead(&pQueue->Queue, pPacketInfo);
    NdisReleaseSpinLock(&pQueue->Lock);
}

/**
 * dequeus the packet info from the head of the queue
 */
DECLINLINE(PPACKET_INFO) vboxNetFltWinQuDequeueHead(PPACKET_QUEUE pQueue)
{
    PLIST_ENTRY pListEntry = RemoveHeadList(pQueue);
    if(pListEntry != pQueue)
    {
        PPACKET_INFO pInfo = LIST_ENTRY_2_PACKET_INFO(pListEntry);
        Assert(pInfo->pPool);
        return pInfo;
    }
    return NULL;
}

DECLINLINE(PPACKET_INFO) vboxNetFltWinQuDequeueTail(PPACKET_QUEUE pQueue)
{
    PLIST_ENTRY pListEntry = RemoveTailList(pQueue);
    if(pListEntry != pQueue)
    {
        PPACKET_INFO pInfo = LIST_ENTRY_2_PACKET_INFO(pListEntry);
        Assert(pInfo->pPool);
        return pInfo;
    }
    return NULL;
}

DECLINLINE(PPACKET_INFO) vboxNetFltWinQuInterlockedDequeueHead(PINTERLOCKED_PACKET_QUEUE pInterlockedQueue)
{
    PPACKET_INFO pInfo;
    NdisAcquireSpinLock(&pInterlockedQueue->Lock);
    pInfo = vboxNetFltWinQuDequeueHead(&pInterlockedQueue->Queue);
    NdisReleaseSpinLock(&pInterlockedQueue->Lock);
    return pInfo;
}

DECLINLINE(PPACKET_INFO) vboxNetFltWinQuInterlockedDequeueTail(PINTERLOCKED_PACKET_QUEUE pInterlockedQueue)
{
    PPACKET_INFO pInfo;
    NdisAcquireSpinLock(&pInterlockedQueue->Lock);
    pInfo = vboxNetFltWinQuDequeueTail(&pInterlockedQueue->Queue);
    NdisReleaseSpinLock(&pInterlockedQueue->Lock);
    return pInfo;
}

DECLINLINE(void) vboxNetFltWinQuDequeue(PPACKET_INFO pInfo)
{
    RemoveEntryList(&pInfo->ListEntry);
}

DECLINLINE(void) vboxNetFltWinQuInterlockedDequeue(PINTERLOCKED_PACKET_QUEUE pInterlockedQueue, PPACKET_INFO pInfo)
{
    NdisAcquireSpinLock(&pInterlockedQueue->Lock);
    vboxNetFltWinQuDequeue(pInfo);
    NdisReleaseSpinLock(&pInterlockedQueue->Lock);
}

/**
 * allocates the packet info from the pool
 */
DECLINLINE(PPACKET_INFO) vboxNetFltWinPpAllocPacketInfo(PPACKET_INFO_POOL pPool)
{
    return vboxNetFltWinQuInterlockedDequeueHead(&pPool->Queue);
}

/**
 * returns the packet info to the pool
 */
DECLINLINE(void) vboxNetFltWinPpFreePacketInfo(PPACKET_INFO pInfo)
{
    PPACKET_INFO_POOL pPool = pInfo->pPool;
    vboxNetFltWinQuInterlockedEnqueueHead(&pPool->Queue, pInfo);
}

/** initializes the packet queue */
#define INIT_PACKET_QUEUE(_pQueue) InitializeListHead((_pQueue))

/** initializes the packet queue */
#define INIT_INTERLOCKED_PACKET_QUEUE(_pQueue) \
    { \
        INIT_PACKET_QUEUE(&(_pQueue)->Queue); \
        NdisAllocateSpinLock(&(_pQueue)->Lock); \
    }

/** delete the packet queue */
#define FINI_INTERLOCKED_PACKET_QUEUE(_pQueue) NdisFreeSpinLock(&(_pQueue)->Lock)

/** returns the packet the packet info contains */
#define GET_PACKET_FROM_INFO(_pPacketInfo) (ASMAtomicUoReadPtr((void * volatile *)&(_pPacketInfo)->pPacket))

/** assignes the packet to the packet info */
#define SET_PACKET_TO_INFO(_pPacketInfo, _pPacket) (ASMAtomicUoWritePtr(&(_pPacketInfo)->pPacket, (_pPacket)))

/** returns the flags the packet info contains */
#define GET_FLAGS_FROM_INFO(_pPacketInfo) (ASMAtomicUoReadU32((volatile uint32_t *)&(_pPacketInfo)->fFlags))

/** sets flags to the packet info */
#define SET_FLAGS_TO_INFO(_pPacketInfo, _fFlags) (ASMAtomicUoWriteU32((volatile uint32_t *)&(_pPacketInfo)->fFlags, (_fFlags)))

#ifdef VBOXNETFLT_NO_PACKET_QUEUE
DECLHIDDEN(bool) vboxNetFltWinPostIntnet(PVBOXNETFLTINS pInstance, PVOID pvPacket, const UINT fFlags);
#else
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinQuEnqueuePacket(PVBOXNETFLTINS pInstance, PVOID pPacket, const UINT fPacketFlags);

#ifndef VBOX_NETFLT_ONDEMAND_BIND
DECLHIDDEN(void) vboxNetFltWinQuFiniPacketQueue(PVBOXNETFLTINS pInstance);

DECLHIDDEN(NTSTATUS) vboxNetFltWinQuInitPacketQueue(PVBOXNETFLTINS pInstance);
#endif

#endif /* #ifndef VBOXNETFLT_NO_PACKET_QUEUE */


#ifndef VBOXNETADP
/**
 * searches the list entry in a single-linked list
 */
DECLINLINE(bool) vboxNetFltWinSearchListEntry(PSINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry2Search, bool bRemove)
{
    PSINGLE_LIST_ENTRY pHead = &pList->Head;
    PSINGLE_LIST_ENTRY pCur;
    PSINGLE_LIST_ENTRY pPrev;
    for(pCur = pHead->Next, pPrev = pHead; pCur; pPrev = pCur, pCur = pCur->Next)
    {
        if(pEntry2Search == pCur)
        {
            if(bRemove)
            {
                pPrev->Next = pCur->Next;
                if(pCur == pList->pTail)
                {
                    pList->pTail = pPrev;
                }
            }
            return true;
        }
    }
    return false;
}

#if !defined(VBOX_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)

DECLINLINE(PNDIS_PACKET) vboxNetFltWinSearchPacket(PSINGLE_LIST pList, PNDIS_PACKET pPacket2Search, int cbMatch, bool bRemove)
{
    PSINGLE_LIST_ENTRY pHead = &pList->Head;
    PSINGLE_LIST_ENTRY pCur;
    PSINGLE_LIST_ENTRY pPrev;
    PNDIS_PACKET pCurPacket;
    for(pCur = pHead->Next, pPrev = pHead; pCur; pPrev = pCur, pCur = pCur->Next)
    {
        pCurPacket = VBOX_SLE_2_SENDPACKET(pCur);
        if(pCurPacket == pPacket2Search || vboxNetFltWinMatchPackets(pPacket2Search, pCurPacket, cbMatch))
        {
            if(bRemove)
            {
                pPrev->Next = pCur->Next;
                if(pCur == pList->pTail)
                {
                    pList->pTail = pPrev;
                }
            }
            return pCurPacket;
        }
    }
    return NULL;
}

DECLINLINE(PNDIS_PACKET) vboxNetFltWinSearchPacketBySG(PSINGLE_LIST pList, PINTNETSG pSG, int cbMatch, bool bRemove)
{
    PSINGLE_LIST_ENTRY pHead = &pList->Head;
    PSINGLE_LIST_ENTRY pCur;
    PSINGLE_LIST_ENTRY pPrev;
    PNDIS_PACKET pCurPacket;
    for(pCur = pHead->Next, pPrev = pHead; pCur; pPrev = pCur, pCur = pCur->Next)
    {
        pCurPacket = VBOX_SLE_2_SENDPACKET(pCur);
        if(vboxNetFltWinMatchPacketAndSG(pCurPacket, pSG, cbMatch))
        {
            if(bRemove)
            {
                pPrev->Next = pCur->Next;
                if(pCur == pList->pTail)
                {
                    pList->pTail = pPrev;
                }
            }
            return pCurPacket;
        }
    }
    return NULL;
}

#endif /* #if !defined(VBOX_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS) */

DECLINLINE(bool) vboxNetFltWinSListIsEmpty(PSINGLE_LIST pList)
{
    return !pList->Head.Next;
}

DECLINLINE(void) vboxNetFltWinPutTail(PSINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry)
{
    pList->pTail->Next = pEntry;
    pList->pTail = pEntry;
    pEntry->Next = NULL;
}

DECLINLINE(void) vboxNetFltWinPutHead(PSINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry)
{
    pEntry->Next = pList->Head.Next;
    pList->Head.Next = pEntry;
    if(!pEntry->Next)
        pList->pTail = pEntry;
}

DECLINLINE(PSINGLE_LIST_ENTRY) vboxNetFltWinGetHead(PSINGLE_LIST pList)
{
    PSINGLE_LIST_ENTRY pEntry = pList->Head.Next;
    if(pEntry && pEntry == pList->pTail)
    {
        pList->Head.Next = NULL;
        pList->pTail = &pList->Head;
    }
    return pEntry;
}

DECLINLINE(bool) vboxNetFltWinInterlockedSearchListEntry(PINTERLOCKED_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry2Search, bool bRemove)
{
    bool bFound;
    NdisAcquireSpinLock(&pList->Lock);
    bFound = vboxNetFltWinSearchListEntry(&pList->List, pEntry2Search, bRemove);
    NdisReleaseSpinLock(&pList->Lock);
    return bFound;
}

#if !defined(VBOX_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)

DECLINLINE(PNDIS_PACKET) vboxNetFltWinInterlockedSearchPacket(PINTERLOCKED_SINGLE_LIST pList, PNDIS_PACKET pPacket2Search, int cbMatch, bool bRemove)
{
    PNDIS_PACKET pFound;
    NdisAcquireSpinLock(&pList->Lock);
    pFound = vboxNetFltWinSearchPacket(&pList->List, pPacket2Search, cbMatch, bRemove);
    NdisReleaseSpinLock(&pList->Lock);
    return pFound;
}

DECLINLINE(PNDIS_PACKET) vboxNetFltWinInterlockedSearchPacketBySG(PINTERLOCKED_SINGLE_LIST pList, PINTNETSG pSG, int cbMatch, bool bRemove)
{
    PNDIS_PACKET pFound;
    NdisAcquireSpinLock(&pList->Lock);
    pFound = vboxNetFltWinSearchPacketBySG(&pList->List, pSG, cbMatch, bRemove);
    NdisReleaseSpinLock(&pList->Lock);
    return pFound;
}
#endif /* #if !defined(VBOX_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS) */

DECLINLINE(void) vboxNetFltWinInterlockedPutTail(PINTERLOCKED_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry)
{
    NdisAcquireSpinLock(&pList->Lock);
    vboxNetFltWinPutTail(&pList->List, pEntry);
    NdisReleaseSpinLock(&pList->Lock);
}

DECLINLINE(void) vboxNetFltWinInterlockedPutHead(PINTERLOCKED_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry)
{
    NdisAcquireSpinLock(&pList->Lock);
    vboxNetFltWinPutHead(&pList->List, pEntry);
    NdisReleaseSpinLock(&pList->Lock);
}

DECLINLINE(PSINGLE_LIST_ENTRY) vboxNetFltWinInterlockedGetHead(PINTERLOCKED_SINGLE_LIST pList)
{
    PSINGLE_LIST_ENTRY pEntry;
    NdisAcquireSpinLock(&pList->Lock);
    pEntry = vboxNetFltWinGetHead(&pList->List);
    NdisReleaseSpinLock(&pList->Lock);
    return pEntry;
}

# if defined(DEBUG_NETFLT_PACKETS) || !defined(VBOX_LOOPBACK_USEFLAGS)
DECLINLINE(void) vboxNetFltWinLbPutSendPacket(PADAPT pAdapt, PNDIS_PACKET pPacket, bool bFromIntNet)
{
    PSEND_RSVD pSrv = (PSEND_RSVD)pPacket->ProtocolReserved;
    pSrv->bFromIntNet = bFromIntNet;
    vboxNetFltWinInterlockedPutHead(&pAdapt->SendPacketQueue, &pSrv->ListEntry);
}

DECLINLINE(bool) vboxNetFltWinLbIsFromIntNet(PNDIS_PACKET pPacket)
{
    PSEND_RSVD pSrv = (PSEND_RSVD)pPacket->ProtocolReserved;
    return pSrv->bFromIntNet;
}

DECLINLINE(PNDIS_PACKET) vboxNetFltWinLbSearchLoopBack(PADAPT pAdapt, PNDIS_PACKET pPacket, bool bRemove)
{
    return vboxNetFltWinInterlockedSearchPacket(&pAdapt->SendPacketQueue, pPacket, VBOXNETFLT_PACKETMATCH_LENGTH, bRemove);
}

DECLINLINE(PNDIS_PACKET) vboxNetFltWinLbSearchLoopBackBySG(PADAPT pAdapt, PINTNETSG pSG, bool bRemove)
{
    return vboxNetFltWinInterlockedSearchPacketBySG(&pAdapt->SendPacketQueue, pSG, VBOXNETFLT_PACKETMATCH_LENGTH, bRemove);
}

DECLINLINE(bool) vboxNetFltWinLbRemoveSendPacket(PADAPT pAdapt, PNDIS_PACKET pPacket)
{
    PSEND_RSVD pSrv = (PSEND_RSVD)pPacket->ProtocolReserved;
    bool bRet = vboxNetFltWinInterlockedSearchListEntry(&pAdapt->SendPacketQueue, &pSrv->ListEntry, true);
#ifdef DEBUG_misha
    PVBOXNETFLTINS pNetFlt = PADAPT_2_PVBOXNETFLTINS(pAdapt);
    Assert(bRet == (pNetFlt->enmTrunkState == INTNETTRUNKIFSTATE_ACTIVE));
#endif
    return bRet;
}

# endif

#endif

#ifdef DEBUG_misha
DECLHIDDEN(bool) vboxNetFltWinCheckMACs(PNDIS_PACKET pPacket, PRTMAC pDst, PRTMAC pSrc);
DECLHIDDEN(bool) vboxNetFltWinCheckMACsSG(PINTNETSG pSG, PRTMAC pDst, PRTMAC pSrc);
extern RTMAC g_vboxNetFltWinVerifyMACBroadcast;
extern RTMAC g_vboxNetFltWinVerifyMACGuest;

# define VBOXNETFLT_LBVERIFY(_pnf, _p) \
    do { \
        Assert(!vboxNetFltWinCheckMACs(_p, NULL, &g_vboxNetFltWinVerifyMACGuest)); \
        Assert(!vboxNetFltWinCheckMACs(_p, NULL, &(_pnf)->u.s.MacAddr)); \
    } while(0)

# define VBOXNETFLT_LBVERIFYSG(_pnf, _p) \
    do { \
        Assert(!vboxNetFltWinCheckMACsSG(_p, NULL, &g_vboxNetFltWinVerifyMACGuest)); \
        Assert(!vboxNetFltWinCheckMACsSG(_p, NULL, &(_pnf)->u.s.MacAddr)); \
    } while(0)

#else
# define VBOXNETFLT_LBVERIFY(_pnf, _p) do{}while(0)
# define VBOXNETFLT_LBVERIFYSG(_pnf, _p) do{}while(0)
#endif

/** initializes the list */
#define INIT_SINGLE_LIST(_pList) \
    { \
        (_pList)->Head.Next = NULL; \
        (_pList)->pTail = &(_pList)->Head; \
    }

/** initializes the list */
#define INIT_INTERLOCKED_SINGLE_LIST(_pList) \
    do { \
        INIT_SINGLE_LIST(&(_pList)->List); \
        NdisAllocateSpinLock(&(_pList)->Lock); \
    } while(0)

/** delete the packet queue */
#define FINI_INTERLOCKED_SINGLE_LIST(_pList) \
    do { \
        Assert(vboxNetFltWinSListIsEmpty(&(_pList)->List)); \
        NdisFreeSpinLock(&(_pList)->Lock) \
    } while(0)


/** obtains the PTRANSFERDATA_RSVD given a single list entry it contains */
//#define PT_SLE_2_TRANSFERDATA_RSVD(_pl) \
//    ( (PTRANSFERDATA_RSVD)((uint8_t *)(_pl) - RT_OFFSETOF(TRANSFERDATA_RSVD, ListEntry)))

///** obtains the ndis packet given a single list entry assuming it is stored in ProtocolReserved field of the packet */
//#define PT_SLE_2_NDIS_PACKET(_pl) \
//   ( (PNDIS_PACKET)((uint8_t *)PT_SLE_2_TRANSFERDATA_RSVD(_pl) - RT_OFFSETOF(NDIS_PACKET, ProtocolReserved)))

/**************************************************************************
 * PADAPT, PVBOXNETFLTINS reference/dereference (i.e. retain/release) API *
 **************************************************************************/

DECLHIDDEN(void) vboxNetFltWinWaitDereference(PADAPT_DEVICE pState);

DECLINLINE(void) vboxNetFltWinReferenceModeNetFlt(PVBOXNETFLTINS pIns)
{
    ASMAtomicIncU32((volatile uint32_t *)&pIns->u.s.cModeNetFltRefs);
}

DECLINLINE(void) vboxNetFltWinReferenceModePassThru(PVBOXNETFLTINS pIns)
{
    ASMAtomicIncU32((volatile uint32_t *)&pIns->u.s.cModePassThruRefs);
}

DECLINLINE(void) vboxNetFltWinIncReferenceModeNetFlt(PVBOXNETFLTINS pIns, uint32_t v)
{
    ASMAtomicAddU32((volatile uint32_t *)&pIns->u.s.cModeNetFltRefs, v);
}

DECLINLINE(void) vboxNetFltWinIncReferenceModePassThru(PVBOXNETFLTINS pIns, uint32_t v)
{
    ASMAtomicAddU32((volatile uint32_t *)&pIns->u.s.cModePassThruRefs, v);
}

DECLINLINE(void) vboxNetFltWinDereferenceModeNetFlt(PVBOXNETFLTINS pIns)
{
    ASMAtomicDecU32((volatile uint32_t *)&pIns->u.s.cModeNetFltRefs);
}

DECLINLINE(void) vboxNetFltWinDereferenceModePassThru(PVBOXNETFLTINS pIns)
{
    ASMAtomicDecU32((volatile uint32_t *)&pIns->u.s.cModePassThruRefs);
}

DECLINLINE(void) vboxNetFltWinDecReferenceModeNetFlt(PVBOXNETFLTINS pIns, uint32_t v)
{
    Assert(v);
    ASMAtomicAddU32((volatile uint32_t *)&pIns->u.s.cModeNetFltRefs, (uint32_t)(-((int32_t)v)));
}

DECLINLINE(void) vboxNetFltWinDecReferenceModePassThru(PVBOXNETFLTINS pIns, uint32_t v)
{
    Assert(v);
    ASMAtomicAddU32((volatile uint32_t *)&pIns->u.s.cModePassThruRefs, (uint32_t)(-((int32_t)v)));
}

DECLINLINE(void) vboxNetFltWinSetPowerState(PADAPT_DEVICE pState, NDIS_DEVICE_POWER_STATE State)
{
    ASMAtomicUoWriteU32((volatile uint32_t *)&pState->PowerState, State);
}

DECLINLINE(NDIS_DEVICE_POWER_STATE) vboxNetFltWinGetPowerState(PADAPT_DEVICE pState)
{
    return (NDIS_DEVICE_POWER_STATE)ASMAtomicUoReadU32((volatile uint32_t *)&pState->PowerState);
}

DECLINLINE(void) vboxNetFltWinSetOpState(PADAPT_DEVICE pState, VBOXNETDEVOPSTATE State)
{
    ASMAtomicUoWriteU32((volatile uint32_t *)&pState->OpState, State);
}

DECLINLINE(VBOXNETDEVOPSTATE) vboxNetFltWinGetOpState(PADAPT_DEVICE pState)
{
    return (VBOXNETDEVOPSTATE)ASMAtomicUoReadU32((volatile uint32_t *)&pState->OpState);
}

DECLINLINE(bool) vboxNetFltWinDoReferenceDevice(PADAPT pAdapt, PADAPT_DEVICE pState)
{
    if (vboxNetFltWinGetPowerState(pState) == NdisDeviceStateD0 && vboxNetFltWinGetOpState(pState) == kVBoxNetDevOpState_Initialized)
    {
        /** @todo r=bird: Since this is a volatile member, why don't you declare it as
         *        such and save yourself all the casting? */
        ASMAtomicIncU32((uint32_t volatile *)&pState->cReferences);
        return true;
    }
    return false;
}

#ifndef VBOXNETADP
DECLINLINE(bool) vboxNetFltWinDoReferenceDevices(PADAPT pAdapt, PADAPT_DEVICE pState1, PADAPT_DEVICE pState2)
{
    if (vboxNetFltWinGetPowerState(pState1) == NdisDeviceStateD0
            && vboxNetFltWinGetOpState(pState1) == kVBoxNetDevOpState_Initialized
            && vboxNetFltWinGetPowerState(pState2) == NdisDeviceStateD0
            && vboxNetFltWinGetOpState(pState2) == kVBoxNetDevOpState_Initialized)
    {
        ASMAtomicIncU32((uint32_t volatile *)&pState1->cReferences);
        ASMAtomicIncU32((uint32_t volatile *)&pState2->cReferences);
        return true;
    }
    return false;
}
#endif

DECLINLINE(void) vboxNetFltWinDereferenceDevice(PADAPT pAdapt, PADAPT_DEVICE pState)
{
/*    NdisAcquireSpinLock(&pAdapt->Lock); */
    ASMAtomicDecU32((uint32_t volatile *)&pState->cReferences);
    /** @todo r=bird: Add comment explaining why these cannot hit 0 or why
     *        reference are counted  */
/*    NdisReleaseSpinLock(&pAdapt->Lock); */
}

#ifndef VBOXNETADP
DECLINLINE(void) vboxNetFltWinDereferenceDevices(PADAPT pAdapt, PADAPT_DEVICE pState1, PADAPT_DEVICE pState2)
{
/*    NdisAcquireSpinLock(&pAdapt->Lock); */
    ASMAtomicDecU32((uint32_t volatile *)&pState1->cReferences);
    ASMAtomicDecU32((uint32_t volatile *)&pState2->cReferences);
/*    NdisReleaseSpinLock(&pAdapt->Lock); */
}
#endif

DECLINLINE(void) vboxNetFltWinDecReferenceDevice(PADAPT pAdapt, PADAPT_DEVICE pState, uint32_t v)
{
    Assert(v);
    ASMAtomicAddU32((uint32_t volatile *)&pState->cReferences, (uint32_t)(-((int32_t)v)));
}

#ifndef VBOXNETADP
DECLINLINE(void) vboxNetFltWinDecReferenceDevices(PADAPT pAdapt, PADAPT_DEVICE pState1, PADAPT_DEVICE pState2, uint32_t v)
{
    ASMAtomicAddU32((uint32_t volatile *)&pState1->cReferences, (uint32_t)(-((int32_t)v)));
    ASMAtomicAddU32((uint32_t volatile *)&pState2->cReferences, (uint32_t)(-((int32_t)v)));
}
#endif

DECLINLINE(bool) vboxNetFltWinDoIncReferenceDevice(PADAPT pAdapt, PADAPT_DEVICE pState, uint32_t v)
{
    Assert(v);
    if (vboxNetFltWinGetPowerState(pState) == NdisDeviceStateD0 && vboxNetFltWinGetOpState(pState) == kVBoxNetDevOpState_Initialized)
    {
        ASMAtomicAddU32((uint32_t volatile *)&pState->cReferences, v);
        return true;
    }
    return false;
}

#ifndef VBOXNETADP
DECLINLINE(bool) vboxNetFltWinDoIncReferenceDevices(PADAPT pAdapt, PADAPT_DEVICE pState1, PADAPT_DEVICE pState2, uint32_t v)
{
    if (vboxNetFltWinGetPowerState(pState1) == NdisDeviceStateD0
            && vboxNetFltWinGetOpState(pState1) == kVBoxNetDevOpState_Initialized
            && vboxNetFltWinGetPowerState(pState2) == NdisDeviceStateD0
            && vboxNetFltWinGetOpState(pState2) == kVBoxNetDevOpState_Initialized)
    {
        ASMAtomicAddU32((uint32_t volatile *)&pState1->cReferences, v);
        ASMAtomicAddU32((uint32_t volatile *)&pState2->cReferences, v);
        return true;
    }
    return false;
}
#endif

#ifdef VBOX_NETFLT_ONDEMAND_BIND
DECLINLINE(PVBOXNETFLTINS) vboxNetFltWinReferenceAdaptNetFltFromAdapt(PADAPT pAdapt)
{
    RTSPINLOCKTMP Tmp = RTSPINLOCKTMP_INITIALIZER;
    PVBOXNETFLTINS pNetFlt;

    pNetFlt = PADAPT_2_PVBOXNETFLTINS(pAdapt);

    RTSpinlockAcquireNoInts((pNetFlt)->hSpinlock, &Tmp);
    if(pNetFlt->enmTrunkState != INTNETTRUNKIFSTATE_ACTIVE)
    {
        RTSpinlockReleaseNoInts((pNetFlt)->hSpinlock, &Tmp);
        return NULL;
    }

    if(!vboxNetFltWinDoReferenceDevice(pAdapt, &pAdapt->PTState))
    {
        RTSpinlockReleaseNoInts((pNetFlt)->hSpinlock, &Tmp);
        return NULL;
    }

    vboxNetFltRetain((pNetFlt), true /* fBusy */);

    RTSpinlockReleaseNoInts((pNetFlt)->hSpinlock, &Tmp);

    return pNetFlt;
}
#else
DECLINLINE(bool) vboxNetFltWinReferenceAdaptNetFlt(PVBOXNETFLTINS pNetFlt, PADAPT pAdapt, bool * pbNetFltActive)
{
    RTSPINLOCKTMP Tmp = RTSPINLOCKTMP_INITIALIZER;

    RTSpinlockAcquireNoInts((pNetFlt)->hSpinlock, &Tmp);
#ifndef VBOXNETADP
    if(!vboxNetFltWinDoReferenceDevices(pAdapt, &pAdapt->MPState, &pAdapt->PTState))
#else
    if(!vboxNetFltWinDoReferenceDevice(pAdapt, &pAdapt->MPState))
#endif
    {
        RTSpinlockReleaseNoInts((pNetFlt)->hSpinlock, &Tmp);
        *pbNetFltActive = false;
        return false;
    }

    if(pNetFlt->enmTrunkState != INTNETTRUNKIFSTATE_ACTIVE)
    {
        vboxNetFltWinReferenceModePassThru(pNetFlt);
        RTSpinlockReleaseNoInts((pNetFlt)->hSpinlock, &Tmp);
        *pbNetFltActive = false;
        return true;
    }

    vboxNetFltRetain((pNetFlt), true /* fBusy */);
    vboxNetFltWinReferenceModeNetFlt(pNetFlt);
    RTSpinlockReleaseNoInts((pNetFlt)->hSpinlock, &Tmp);

    *pbNetFltActive = true;
    return true;
}
#endif

#ifdef VBOX_NETFLT_ONDEMAND_BIND
DECLINLINE(PVBOXNETFLTINS) vboxNetFltWinIncReferenceAdaptNetFltFromAdapt(PADAPT pAdapt, uint32_t v)
{
    RTSPINLOCKTMP Tmp = RTSPINLOCKTMP_INITIALIZER;
    PVBOXNETFLTINS pNetFlt;
    uint32_t i;

    Assert(v);
    if(!v)
    {
        return NULL;
    }

    pNetFlt = PADAPT_2_PVBOXNETFLTINS(pAdapt);

    RTSpinlockAcquireNoInts((pNetFlt)->hSpinlock, &Tmp);
    if(pNetFlt->enmTrunkState != INTNETTRUNKIFSTATE_ACTIVE)
    {
        RTSpinlockReleaseNoInts((pNetFlt)->hSpinlock, &Tmp);
        return NULL;
    }

    if(!vboxNetFltWinDoIncReferenceDevice(pAdapt, &pAdapt->PTState, v))
    {
        RTSpinlockReleaseNoInts((pNetFlt)->hSpinlock, &Tmp);
        return NULL;
    }

    vboxNetFltRetain((pNetFlt), true /* fBusy */);

    RTSpinlockReleaseNoInts((pNetFlt)->hSpinlock, &Tmp);

    /* we have marked it as busy, so can do the res references outside the lock */
    for(i = 0; i < v-1; i++)
    {
        vboxNetFltRetain((pNetFlt), true /* fBusy */);
    }

    return pNetFlt;
}
#else
DECLINLINE(bool) vboxNetFltWinIncReferenceAdaptNetFlt(PVBOXNETFLTINS pNetFlt, PADAPT pAdapt, uint32_t v, bool *pbNetFltActive)
{
    RTSPINLOCKTMP Tmp = RTSPINLOCKTMP_INITIALIZER;
    uint32_t i;

    Assert(v);
    if(!v)
    {
        *pbNetFltActive = false;
        return false;
    }

    RTSpinlockAcquireNoInts((pNetFlt)->hSpinlock, &Tmp);
#ifndef VBOXNETADP
    if(!vboxNetFltWinDoIncReferenceDevices(pAdapt, &pAdapt->MPState, &pAdapt->PTState, v))
#else
    if(!vboxNetFltWinDoIncReferenceDevice(pAdapt, &pAdapt->MPState, v))
#endif
    {
        RTSpinlockReleaseNoInts(pNetFlt->hSpinlock, &Tmp);
        *pbNetFltActive = false;
        return false;
    }

    if(pNetFlt->enmTrunkState != INTNETTRUNKIFSTATE_ACTIVE)
    {
        vboxNetFltWinIncReferenceModePassThru(pNetFlt, v);

        RTSpinlockReleaseNoInts((pNetFlt)->hSpinlock, &Tmp);
        *pbNetFltActive = false;
        return true;
    }

    vboxNetFltRetain(pNetFlt, true /* fBusy */);

    vboxNetFltWinIncReferenceModeNetFlt(pNetFlt, v);

    RTSpinlockReleaseNoInts(pNetFlt->hSpinlock, &Tmp);

    /* we have marked it as busy, so can do the res references outside the lock */
    for(i = 0; i < v-1; i++)
    {
        vboxNetFltRetain(pNetFlt, true /* fBusy */);
    }

    *pbNetFltActive = true;

    return true;
}

#endif

DECLINLINE(void) vboxNetFltWinDecReferenceNetFlt(PVBOXNETFLTINS pNetFlt, uint32_t n)
{
    uint32_t i;
    for(i = 0; i < n; i++)
    {
        vboxNetFltRelease(pNetFlt, true);
    }

    vboxNetFltWinDecReferenceModeNetFlt(pNetFlt, n);
}

DECLINLINE(void) vboxNetFltWinDereferenceNetFlt(PVBOXNETFLTINS pNetFlt)
{
    vboxNetFltRelease(pNetFlt, true);

    vboxNetFltWinDereferenceModeNetFlt(pNetFlt);
}

DECLINLINE(void) vboxNetFltWinDecReferenceAdapt(PADAPT pAdapt, uint32_t v)
{
#ifdef VBOX_NETFLT_ONDEMAND_BIND
    vboxNetFltWinDecReferenceDevice(pAdapt, &pAdapt->PTState, v);
#elif defined(VBOXNETADP)
    vboxNetFltWinDecReferenceDevice(pAdapt, &pAdapt->MPState, v);
#else
    vboxNetFltWinDecReferenceDevices(pAdapt, &pAdapt->MPState, &pAdapt->PTState, v);
#endif
}

DECLINLINE(void) vboxNetFltWinDereferenceAdapt(PADAPT pAdapt)
{
#ifdef VBOX_NETFLT_ONDEMAND_BIND
    vboxNetFltWinDereferenceDevice(pAdapt, &pAdapt->PTState);
#elif defined(VBOXNETADP)
    vboxNetFltWinDereferenceDevice(pAdapt, &pAdapt->MPState);
#else
    vboxNetFltWinDereferenceDevices(pAdapt, &pAdapt->MPState, &pAdapt->PTState);
#endif
}

DECLINLINE(bool) vboxNetFltWinIncReferenceAdapt(PADAPT pAdapt, uint32_t v)
{
    PVBOXNETFLTINS pNetFlt = PADAPT_2_PVBOXNETFLTINS(pAdapt);
    RTSPINLOCKTMP Tmp = RTSPINLOCKTMP_INITIALIZER;

    Assert(v);
    if(!v)
    {
        return false;
    }

    RTSpinlockAcquireNoInts(pNetFlt->hSpinlock, &Tmp);
#ifdef VBOX_NETFLT_ONDEMAND_BIND
    if(vboxNetFltWinDoIncReferenceDevice(pAdapt, &pAdapt->PTState))
#elif defined(VBOXNETADP)
    if(vboxNetFltWinDoIncReferenceDevice(pAdapt, &pAdapt->MPState, v))
#else
    if(vboxNetFltWinDoIncReferenceDevices(pAdapt, &pAdapt->MPState, &pAdapt->PTState, v))
#endif
    {
        RTSpinlockReleaseNoInts(pNetFlt->hSpinlock, &Tmp);
        return true;
    }

    RTSpinlockReleaseNoInts(pNetFlt->hSpinlock, &Tmp);
    return false;
}

DECLINLINE(bool) vboxNetFltWinReferenceAdapt(PADAPT pAdapt)
{
    PVBOXNETFLTINS pNetFlt = PADAPT_2_PVBOXNETFLTINS(pAdapt);
    RTSPINLOCKTMP Tmp = RTSPINLOCKTMP_INITIALIZER;
    RTSpinlockAcquireNoInts(pNetFlt->hSpinlock, &Tmp);
#ifdef VBOX_NETFLT_ONDEMAND_BIND
    if(vboxNetFltWinDoReferenceDevice(pAdapt, &pAdapt->PTState))
#elif defined(VBOXNETADP)
    if(vboxNetFltWinDoReferenceDevice(pAdapt, &pAdapt->MPState))
#else
    if(vboxNetFltWinDoReferenceDevices(pAdapt, &pAdapt->MPState, &pAdapt->PTState))
#endif
    {
        RTSpinlockReleaseNoInts(pNetFlt->hSpinlock, &Tmp);
        return true;
    }

    RTSpinlockReleaseNoInts(pNetFlt->hSpinlock, &Tmp);
    return false;
}

/***********************************************
 * methods for accessing the network card info *
 ***********************************************/

DECLHIDDEN(NDIS_STATUS) vboxNetFltWinGetMacAddress(PADAPT pAdapt, PRTMAC pMac);
DECLHIDDEN(bool) vboxNetFltWinIsPromiscuous(PADAPT pAdapt);
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinSetPromiscuous(PADAPT pAdapt, bool bYes);
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinQueryPhysicalMedium(PADAPT pAdapt, NDIS_PHYSICAL_MEDIUM * pMedium);

/*********************
 * mem alloc API     *
 *********************/

DECLHIDDEN(NDIS_STATUS) vboxNetFltWinMemAlloc(PVOID* ppMemBuf, UINT cbLength);

DECLHIDDEN(void) vboxNetFltWinMemFree(PVOID pMemBuf);

/* convenience method used which allocates and initializes the PINTNETSG containing one
 * segment referring the buffer of size cbBufSize
 * the allocated PINTNETSG should be freed with the vboxNetFltWinMemFree.
 *
 * This is used when our ProtocolReceive callback is called and we have to return the indicated NDIS_PACKET
 * on a callback exit. This is why we allocate the PINTNETSG and put the packet info there and enqueue it
 * for the packet queue */
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinAllocSG(UINT cbBufSize, PINTNETSG *ppSG);

/************************
 * PADAPT init/fini API *
 ************************/

#if defined(VBOX_NETFLT_ONDEMAND_BIND)
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtInitBind(PADAPT pAdapt);
#elif defined(VBOXNETADP)
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtInitBind(PADAPT *ppAdapt, NDIS_HANDLE hMiniportAdapter, PNDIS_STRING pBindToMiniportName /* actually this is our miniport name*/, NDIS_HANDLE hWrapperConfigurationContext);
#else
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtInitBind(PADAPT *ppAdapt, PNDIS_STRING pOurMiniportName, PNDIS_STRING pBindToMiniportName);
#endif

#ifdef VBOX_NETFLT_ONDEMAND_BIND
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtAllocInitPADAPT(PADAPT pAdapt);
#else
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtAllocInitPADAPT(PADAPT *ppAdapt, PNDIS_STRING pOurMiniportName, PNDIS_STRING pBindToMiniportName);
#endif

DECLHIDDEN(VOID) vboxNetFltWinPtFiniPADAPT(PADAPT pAdapt);
#ifndef VBOXNETADP
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtInitPADAPT(IN  PADAPT pAdapt, IN PNDIS_STRING pOurDeviceName);
#else
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtInitPADAPT(IN  PADAPT pAdapt);
#endif

/************************************
 * Execute Job at passive level API *
 ************************************/

typedef VOID (*JOB_ROUTINE) (PVOID pContext);

DECLHIDDEN(VOID) vboxNetFltWinJobSynchExecAtPassive(JOB_ROUTINE pRoutine, PVOID pContext);

/*******************************
 * Ndis Packets processing API *
 *******************************/

#ifndef NDIS_PACKET_FIRST_NDIS_BUFFER
#define NDIS_PACKET_FIRST_NDIS_BUFFER(_Packet)      ((_Packet)->Private.Head)
#endif

#ifndef NDIS_PACKET_LAST_NDIS_BUFFER
#define NDIS_PACKET_LAST_NDIS_BUFFER(_Packet)       ((_Packet)->Private.Tail)
#endif

#ifndef NDIS_PACKET_VALID_COUNTS
#define NDIS_PACKET_VALID_COUNTS(_Packet)           ((_Packet)->Private.ValidCounts)
#endif


DECLHIDDEN(PNDIS_PACKET) vboxNetFltWinNdisPacketFromSG(PADAPT pAdapt, PINTNETSG pSG, PVOID pBufToFree, bool bToWire, bool bCopyMemory);

DECLHIDDEN(void) vboxNetFltWinFreeSGNdisPacket(PNDIS_PACKET pPacket, bool bFreeMem);

#ifdef DEBUG_NETFLT_PACKETS
#define DBG_CHECK_PACKETS(_p1, _p2) \
    {   \
        bool _b = vboxNetFltWinMatchPackets(_p1, _p2, -1);  \
        Assert(_b);  \
    }

#define DBG_CHECK_PACKET_AND_SG(_p, _sg) \
    {   \
        bool _b = vboxNetFltWinMatchPacketAndSG(_p, _sg, -1);  \
        Assert(_b);  \
    }

#define DBG_CHECK_SGS(_sg1, _sg2) \
    {   \
        bool _b = vboxNetFltWinMatchSGs(_sg1, _sg2, -1);  \
        Assert(_b);  \
    }

#else
#define DBG_CHECK_PACKETS(_p1, _p2)
#define DBG_CHECK_PACKET_AND_SG(_p, _sg)
#define DBG_CHECK_SGS(_sg1, _sg2)
#endif

/**
 * Ndis loops back broadcast packets posted to the wire by IntNet
 * This routine is used in the mechanism of preventing this looping
 *
 * @param pAdapt
 * @param pPacket
 * @param bOnRecv true is we are receiving the packet from the wire
 * false otherwise (i.e. the packet is from the host)
 *
 * @return true if the packet is a looped back one, false otherwise
 */
#ifdef VBOX_LOOPBACK_USEFLAGS
DECLINLINE(bool) vboxNetFltWinIsLoopedBackPacket(PNDIS_PACKET pPacket)
{
    return (NdisGetPacketFlags(pPacket) & g_fPacketIsLoopedBack) == g_fPacketIsLoopedBack;
}
#endif

/**************************************************************
 * utility methods for ndis packet creation/initialization    *
 **************************************************************/

#define VBOXNETFLT_OOB_INIT(_p) \
    { \
        NdisZeroMemory(NDIS_OOB_DATA_FROM_PACKET(_p), sizeof(NDIS_PACKET_OOB_DATA)); \
        NDIS_SET_PACKET_HEADER_SIZE(_p, ETH_HEADER_SIZE); \
    }

#if !defined(VBOX_NETFLT_ONDEMAND_BIND) && !defined(VBOXNETADP)

DECLINLINE(NDIS_STATUS) vboxNetFltWinCopyPacketInfoOnRecv(PNDIS_PACKET pDstPacket, PNDIS_PACKET pSrcPacket)
{
    NDIS_STATUS fStatus;

    /*
     * Get the original packet (it could be the same packet as the one
     * received or a different one based on the number of layered miniports
     * below) and set it on the indicated packet so the OOB data is visible
     * correctly to protocols above us.
     */
    NDIS_SET_ORIGINAL_PACKET(pDstPacket, NDIS_GET_ORIGINAL_PACKET(pSrcPacket));

     /*
     * Set Packet Flags
     */
    NdisGetPacketFlags(pDstPacket) = NdisGetPacketFlags(pSrcPacket);

    fStatus = NDIS_GET_PACKET_STATUS(pSrcPacket);

    NDIS_SET_PACKET_STATUS(pDstPacket, fStatus);
    NDIS_SET_PACKET_HEADER_SIZE(pDstPacket, NDIS_GET_PACKET_HEADER_SIZE(pSrcPacket));

    return fStatus;
}

DECLINLINE(void) vboxNetFltWinCopyPacketInfoOnSend(PNDIS_PACKET pDstPacket, PNDIS_PACKET pSrcPacket)
{
    PVOID               pMediaSpecificInfo = NULL;
    UINT                fMediaSpecificInfoSize = 0;

    NdisGetPacketFlags(pDstPacket) = NdisGetPacketFlags(pSrcPacket);

#ifdef WIN9X
    /*
     * Work around the fact that NDIS does not initialize this
     * to FALSE on Win9x.
     */
    NDIS_PACKET_VALID_COUNTS(pDstPacket) = FALSE;
#endif /* WIN9X */

    /*
     * Copy the OOB data from the original packet to the new
     * packet.
     */
    NdisMoveMemory(NDIS_OOB_DATA_FROM_PACKET(pDstPacket),
                NDIS_OOB_DATA_FROM_PACKET(pSrcPacket),
                sizeof(NDIS_PACKET_OOB_DATA));
    /*
     * Copy relevant parts of the per packet info into the new packet
     */
#ifndef WIN9X
    NdisIMCopySendPerPacketInfo(pDstPacket, pSrcPacket);
#endif

    /*
     * Copy the Media specific information
     */
    NDIS_GET_PACKET_MEDIA_SPECIFIC_INFO(pSrcPacket,
                                        &pMediaSpecificInfo,
                                        &fMediaSpecificInfoSize);

    if (pMediaSpecificInfo || fMediaSpecificInfoSize)
    {
        NDIS_SET_PACKET_MEDIA_SPECIFIC_INFO(pDstPacket,
                                            pMediaSpecificInfo,
                                            fMediaSpecificInfoSize);
    }
}

DECLHIDDEN(NDIS_STATUS)
vboxNetFltWinPrepareSendPacket(
    IN PADAPT             pAdapt,
    IN PNDIS_PACKET           pPacket,
    OUT PNDIS_PACKET         *ppMyPacket
    /*, IN bool bNetFltActive*/
    );


DECLHIDDEN(NDIS_STATUS)
vboxNetFltWinPrepareRecvPacket(
    IN PADAPT            pAdapt,
    IN PNDIS_PACKET           pPacket,
    OUT PNDIS_PACKET        *ppMyPacket,
    IN bool bDpr
    );
#endif

DECLHIDDEN(void) vboxNetFltWinSleep(ULONG milis);

#define MACS_EQUAL(_m1, _m2) \
    ((_m1).au16[0] == (_m2).au16[0] \
        && (_m1).au16[1] == (_m2).au16[1] \
        && (_m1).au16[2] == (_m2).au16[2])


DECLHIDDEN(NDIS_STATUS) vboxNetFltWinDetachFromInterface(PADAPT pAdapt, bool bOnUnbind);
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinCopyString(PNDIS_STRING pDst, PNDIS_STRING pSrc);


/**
 * Sets the enmState member atomically.
 *
 * Used for all updates.
 *
 * @param   pThis           The instance.
 * @param   enmNewState     The new value.
 */
DECLINLINE(void) vboxNetFltWinSetAdaptState(PADAPT pAdapt, VBOXADAPTSTATE enmNewState)
{
    ASMAtomicWriteU32((uint32_t volatile *)&pAdapt->enmState, enmNewState);
}

/**
 * Gets the enmState member atomically.
 *
 * Used for all reads.
 *
 * @returns The enmState value.
 * @param   pThis           The instance.
 */
DECLINLINE(VBOXADAPTSTATE) vboxNetFltWinGetAdaptState(PADAPT pAdapt)
{
    return (VBOXADAPTSTATE)ASMAtomicUoReadU32((uint32_t volatile *)&pAdapt->enmState);
}


#ifndef VBOXNETADP
# define VBOXNETFLT_PROMISCUOUS_SUPPORTED(_pAdapt) \
    (!PADAPT_2_PVBOXNETFLTINS(_pAdapt)->fDisablePromiscuous)
//    (!((_pAdapt)->PhMedium == NdisPhysicalMediumWirelessWan \
//    || (_pAdapt)->PhMedium == NdisPhysicalMediumWirelessLan \
//    || (_pAdapt)->PhMedium == NdisPhysicalMediumNative802_11 \
//    || (_pAdapt)->PhMedium == NdisPhysicalMediumBluetooth \
//    /*|| (_pAdapt)->PhMedium == NdisPhysicalMediumWiMax */ \
//    ))
#else
# define STATISTIC_INCREASE(_s) ASMAtomicIncU32((uint32_t volatile *)&(_s));

DECLHIDDEN(void) vboxNetFltWinGenerateMACAddress(RTMAC *pMac);
DECLHIDDEN(int) vboxNetFltWinMAC2NdisString(RTMAC *pMac, PNDIS_STRING pNdisString);
DECLHIDDEN(int) vboxNetFltWinMACFromNdisString(RTMAC *pMac, PNDIS_STRING pNdisString);

#endif
#endif
