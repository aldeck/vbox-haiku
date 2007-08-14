/* $Id$ */
/** @file
 * innotek Portable Runtime - Ring-0 Memory Objects, NT.
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
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "the-nt-kernel.h"

#include <iprt/memobj.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/process.h>
#include "internal/memobj.h"


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** Maximum number of bytes we try to lock down in one go.
 * This is supposed to have a limit right below 256MB, but this appears
 * to actually be much lower. The values here have been determined experimentally.
 */
#ifdef RT_ARCH_X86
# define MAX_LOCK_MEM_SIZE   (32*1024*1024) /* 32MB */
#endif
#ifdef RT_ARCH_AMD64
# define MAX_LOCK_MEM_SIZE   (24*1024*1024) /* 24MB */
#endif


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * The NT version of the memory object structure.
 */
typedef struct RTR0MEMOBJNT
{
    /** The core structure. */
    RTR0MEMOBJINTERNAL  Core;
    /** The number of PMDLs (memory descriptor lists) in the array. */
    unsigned            cMdls;
    /** Array of MDL pointers. (variable size) */
    PMDL                apMdls[1];
} RTR0MEMOBJNT, *PRTR0MEMOBJNT;


int rtR0MemObjNativeFree(RTR0MEMOBJ pMem)
{
    PRTR0MEMOBJNT pMemNt = (PRTR0MEMOBJNT)pMem;

    /*
     * Release any memory that we've allocated or locked.
     */
    switch (pMemNt->Core.enmType)
    {
        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_PAGE:
            break;

        case RTR0MEMOBJTYPE_CONT:
            break;

        case RTR0MEMOBJTYPE_LOCK:
            for (unsigned i = 0; i < pMemNt->cMdls; i++)
                MmUnlockPages(pMemNt->apMdl[i]);
            break;

        case RTR0MEMOBJTYPE_PHYS:
            Assert(!pMemNt->Core.u.Phys.fAllocated);
            break;

        case RTR0MEMOBJTYPE_RES_VIRT:
            AssertMsgFailed(("RTR0MEMOBJTYPE_RES_VIRT\n"));
            return VERR_INTERNAL_ERROR;
            break;

        case RTR0MEMOBJTYPE_MAPPING:
            /* nothing to do here. */
            break;

        default:
            AssertMsgFailed(("enmType=%d\n", pMemNt->Core.enmType));
            return VERR_INTERNAL_ERROR;
    }

    /*
     * Free any MDLs.
     */
    for (unsigned i = 0; i < pMemNt->cMdls; i++)
    {
        MmUnlockPages(pMemNt->apMdl[i]);
        IoFreeMdl(pMemNt->u.locked.papMdl[i]);
    }
    return VINF_SUCCESS;
}


int rtR0MemObjNativeAllocPage(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable)
{
    /*
     * Try allocate the memory and create it's IOMemoryDescriptor first.
     */
    int rc = VERR_NO_PAGE_MEMORY;
    AssertCompile(sizeof(IOPhysicalAddress) == 4);
    void *pv = IOMallocAligned(cb, PAGE_SIZE);
    if (pv)
    {
        IOMemoryDescriptor *pMemDesc = IOMemoryDescriptor::withAddress((vm_address_t)pv, cb, kIODirectionInOut, kernel_task);
        if (pMemDesc)
        {
            /*
             * Create the IPRT memory object.
             */
            PRTR0MEMOBJNT pMemNt = (PRTR0MEMOBJNT)rtR0MemObjNew(sizeof(*pMemNt), RTR0MEMOBJTYPE_PAGE, pv, cb);
            if (pMemNt)
            {
                pMemNt->pMemDesc = pMemDesc;
                *ppMem = &pMemNt->Core;
                return VINF_SUCCESS;
            }

            rc = VERR_NO_MEMORY;
            pMemDesc->release();
        }
        else
            rc = VERR_MEMOBJ_INIT_FAILED;
        IOFreeAligned(pv, cb);
    }
    return rc;
}


int rtR0MemObjNativeAllocLow(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable)
{
#if 1
    /*
     * Allocating 128KB for the low page pool can bit a bit exhausting on the kernel,
     * it frequnetly causes the entire box to lock up on startup.
     *
     * So, try allocate the memory using IOMallocAligned first and if we get any high
     * physical memory we'll release it and fall back on IOMAllocContiguous.
     */
    int rc = VERR_NO_PAGE_MEMORY;
    AssertCompile(sizeof(IOPhysicalAddress) == 4);
    void *pv = IOMallocAligned(cb, PAGE_SIZE);
    if (pv)
    {
        IOMemoryDescriptor *pMemDesc = IOMemoryDescriptor::withAddress((vm_address_t)pv, cb, kIODirectionInOut, kernel_task);
        if (pMemDesc)
        {
            /*
             * Check if it's all below 4GB.
             */
            for (IOByteCount off = 0; off < cb; off += PAGE_SIZE)
            {
                addr64_t Addr = pMemDesc->getPhysicalSegment64(off, NULL);
                if (Addr > (uint32_t)(_4G - PAGE_SIZE))
                {
                    /* Ok, we failed, fall back on contiguous allocation. */
                    pMemDesc->release();
                    IOFreeAligned(pv, cb);
                    return rtR0MemObjNativeAllocCont(ppMem, cb, fExecutable);
                }
            }

            /*
             * Create the IPRT memory object.
             */
            PRTR0MEMOBJNT pMemNt = (PRTR0MEMOBJNT)rtR0MemObjNew(sizeof(*pMemNt), RTR0MEMOBJTYPE_LOW, pv, cb);
            if (pMemNt)
            {
                pMemNt->pMemDesc = pMemDesc;
                *ppMem = &pMemNt->Core;
                return VINF_SUCCESS;
            }

            rc = VERR_NO_MEMORY;
            pMemDesc->release();
        }
        else
            rc = VERR_MEMOBJ_INIT_FAILED;
        IOFreeAligned(pv, cb);
    }
    return rc;

#else

    /*
     * IOMallocContiguous is the most suitable API.
     */
    return rtR0MemObjNativeAllocCont(ppMem, cb, fExecutable);
#endif
}


int rtR0MemObjNativeAllocCont(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable)
{
    /*
     * Try allocate the memory and create it's IOMemoryDescriptor first.
     */
    int rc = VERR_NO_CONT_MEMORY;
    AssertCompile(sizeof(IOPhysicalAddress) == 4);
    void *pv = IOMallocContiguous(cb, PAGE_SIZE, NULL);
    if (pv)
    {
        IOMemoryDescriptor *pMemDesc = IOMemoryDescriptor::withAddress((vm_address_t)pv, cb, kIODirectionInOut, kernel_task);
        if (pMemDesc)
        {
            /* a bit of useful paranoia. */
            addr64_t PhysAddr = pMemDesc->getPhysicalSegment64(0, NULL);
            Assert(PhysAddr == pMemDesc->getPhysicalAddress());
            if (    PhysAddr > 0
                &&  PhysAddr <= _4G
                &&  PhysAddr + cb <= _4G)
            {
                /*
                 * Create the IPRT memory object.
                 */
                PRTR0MEMOBJNT pMemNt = (PRTR0MEMOBJNT)rtR0MemObjNew(sizeof(*pMemNt), RTR0MEMOBJTYPE_CONT, pv, cb);
                if (pMemNt)
                {
                    pMemNt->Core.u.Cont.Phys = PhysAddr;
                    pMemNt->pMemDesc = pMemDesc;
                    *ppMem = &pMemNt->Core;
                    return VINF_SUCCESS;
                }

                rc = VERR_NO_MEMORY;
            }
            else
            {
                AssertMsgFailed(("PhysAddr=%llx\n", (unsigned long long)PhysAddr));
                rc = VERR_INTERNAL_ERROR;
            }
            pMemDesc->release();
        }
        else
            rc = VERR_MEMOBJ_INIT_FAILED;
        IOFreeContiguous(pv, cb);
    }
    return rc;
}


int rtR0MemObjNativeAllocPhys(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest)
{
#if 0 /* turned out IOMallocPhysical isn't exported yet. sigh. */
    /*
     * Try allocate the memory and create it's IOMemoryDescriptor first.
     * Note that IOMallocPhysical is not working correctly (it's ignoring the mask).
     */

    /* first calc the mask (in the hope that it'll be used) */
    IOPhysicalAddress PhysMask = ~(IOPhysicalAddress)PAGE_OFFSET_MASK;
    if (PhysHighest != NIL_RTHCPHYS)
    {
        PhysMask = ~(IOPhysicalAddress)0;
        while (PhysMask > PhysHighest)
            PhysMask >>= 1;
        AssertReturn(PhysMask + 1 < cb, VERR_INVALID_PARAMETER);
        PhysMask &= ~(IOPhysicalAddress)PAGE_OFFSET_MASK;
    }

    /* try allocate physical memory. */
    int rc = VERR_NO_PHYS_MEMORY;
    mach_vm_address_t PhysAddr64 = IOMallocPhysical(cb, PhysMask);
    if (PhysAddr64)
    {
        IOPhysicalAddress PhysAddr = PhysAddr64;
        if (    PhysAddr == PhysAddr64
            &&  PhysAddr < PhysHighest
            &&  PhysAddr + cb <= PhysHighest)
        {
            /* create a descriptor. */
            IOMemoryDescriptor *pMemDesc = IOMemoryDescriptor::withPhysicalAddress(PhysAddr, cb, kIODirectionInOut);
            if (pMemDesc)
            {
                Assert(PhysAddr == pMemDesc->getPhysicalAddress());

                /*
                 * Create the IPRT memory object.
                 */
                PRTR0MEMOBJNT pMemNt = (PRTR0MEMOBJNT)rtR0MemObjNew(sizeof(*pMemNt), RTR0MEMOBJTYPE_PHYS, NULL, cb);
                if (pMemNt)
                {
                    pMemNt->Core.u.Phys.PhysBase = PhysAddr;
                    pMemNt->Core.u.Phys.fAllocated = true;
                    pMemNt->pMemDesc = pMemDesc;
                    *ppMem = &pMemNt->Core;
                    return VINF_SUCCESS;
                }

                rc = VERR_NO_MEMORY;
                pMemDesc->release();
            }
            else
                rc = VERR_MEMOBJ_INIT_FAILED;
        }
        else
        {
            AssertMsgFailed(("PhysAddr=%#llx PhysAddr64=%#llx PhysHigest=%#llx\n", (unsigned long long)PhysAddr,
                             (unsigned long long)PhysAddr64, (unsigned long long)PhysHighest));
            rc = VERR_INTERNAL_ERROR;
        }

        IOFreePhysical(PhysAddr64, cb);
    }

    /*
     * Just in case IOMallocContiguous doesn't work right, we can try fall back
     * on a contiguous allcation.
     */
    if (rc == VERR_INTERNAL_ERROR || rc == VERR_NO_PHYS_MEMORY)
    {
        int rc2 = rtR0MemObjNativeAllocCont(ppMem, cb, false);
        if (RT_SUCCESS(rc2))
            rc = rc2;
    }

    return rc;

#else

    return rtR0MemObjNativeAllocCont(ppMem, cb, false);
#endif
}


int rtR0MemObjNativeAllocPhysNC(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest)
{
    /** @todo rtR0MemObjNativeAllocPhys / darwin. */
    return rtR0MemObjNativeAllocPhys(ppMem, cb, PhysHighest);
}


int rtR0MemObjNativeEnterPhys(PPRTR0MEMOBJINTERNAL ppMem, RTHCPHYS Phys, size_t cb)
{
    /*
     * Validate the address range and create a descriptor for it.
     */
    int rc = VERR_ADDRESS_TOO_BIG;
    IOPhysicalAddress PhysAddr = Phys;
    if (PhysAddr == Phys)
    {
        IOMemoryDescriptor *pMemDesc = IOMemoryDescriptor::withPhysicalAddress(PhysAddr, cb, kIODirectionInOut);
        if (pMemDesc)
        {
            Assert(PhysAddr == pMemDesc->getPhysicalAddress());

            /*
             * Create the IPRT memory object.
             */
            PRTR0MEMOBJNT pMemNt = (PRTR0MEMOBJNT)rtR0MemObjNew(sizeof(*pMemNt), RTR0MEMOBJTYPE_PHYS, NULL, cb);
            if (pMemNt)
            {
                pMemNt->Core.u.Phys.PhysBase = PhysAddr;
                pMemNt->Core.u.Phys.fAllocated = false;
                pMemNt->pMemDesc = pMemDesc;
                *ppMem = &pMemNt->Core;
                return VINF_SUCCESS;
            }

            rc = VERR_NO_MEMORY;
            pMemDesc->release();
        }
    }
    else
        AssertMsgFailed(("%#llx\n", (unsigned long long)Phys));
    return rc;
}


/**
 * Internal worker for locking down pages.
 *
 * @return IPRT status code.
 *
 * @param ppMem     Where to store the memory object pointer.
 * @param pv        First page.
 * @param cb        Number of bytes.
 * @param Task      The task \a pv and \a cb refers to.
 */
static int rtR0MemObjNativeLock(PPRTR0MEMOBJINTERNAL ppMem, void *pv, size_t cb, task_t Task)
{
#ifdef USE_VM_MAP_WIRE
    vm_map_t Map = get_task_map(Task);
    Assert(Map);

    /*
     * First try lock the memory.
     */
    int rc = VERR_LOCK_FAILED;
    kern_return_t kr = vm_map_wire(get_task_map(Task),
                                   (vm_map_offset_t)pv,
                                   (vm_map_offset_t)pv + cb,
                                   VM_PROT_DEFAULT,
                                   0 /* not user */);
    if (kr == KERN_SUCCESS)
    {
        /*
         * Create the IPRT memory object.
         */
        PRTR0MEMOBJNT pMemNt = (PRTR0MEMOBJNT)rtR0MemObjNew(sizeof(*pMemNt), RTR0MEMOBJTYPE_LOCK, pv, cb);
        if (pMemNt)
        {
            pMemNt->Core.u.Lock.R0Process = (RTR0PROCESS)Task;
            *ppMem = &pMemNt->Core;
            return VINF_SUCCESS;
        }

        kr = vm_map_unwire(get_task_map(Task), (vm_map_offset_t)pv, (vm_map_offset_t)pv + cb, 0 /* not user */);
        Assert(kr == KERN_SUCCESS);
        rc = VERR_NO_MEMORY;
    }

#else

    /*
     * Create a descriptor and try lock it (prepare).
     */
    int rc = VERR_MEMOBJ_INIT_FAILED;
    IOMemoryDescriptor *pMemDesc = IOMemoryDescriptor::withAddress((vm_address_t)pv, cb, kIODirectionInOut, Task);
    if (pMemDesc)
    {
        IOReturn IORet = pMemDesc->prepare(kIODirectionInOut);
        if (IORet == kIOReturnSuccess)
        {
            /*
             * Create the IPRT memory object.
             */
            PRTR0MEMOBJNT pMemNt = (PRTR0MEMOBJNT)rtR0MemObjNew(sizeof(*pMemNt), RTR0MEMOBJTYPE_LOCK, pv, cb);
            if (pMemNt)
            {
                pMemNt->Core.u.Lock.R0Process = (RTR0PROCESS)Task;
                pMemNt->pMemDesc = pMemDesc;
                *ppMem = &pMemNt->Core;
                return VINF_SUCCESS;
            }

            pMemDesc->complete();
            rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_LOCK_FAILED;
        pMemDesc->release();
    }
#endif
    return rc;
}


int rtR0MemObjNativeLockUser(PPRTR0MEMOBJINTERNAL ppMem, void *pv, size_t cb, RTR0PROCESS R0Process)
{
    return rtR0MemObjNativeLock(ppMem, pv, cb, (task_t)R0Process);
}


int rtR0MemObjNativeLockKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pv, size_t cb)
{
    return rtR0MemObjNativeLock(ppMem, pv, cb, kernel_task);
}


int rtR0MemObjNativeReserveKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pvFixed, size_t cb, size_t uAlignment)
{
    return VERR_NOT_IMPLEMENTED;
}


int rtR0MemObjNativeReserveUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3PtrFixed, size_t cb, size_t uAlignment, RTR0PROCESS R0Process)
{
    return VERR_NOT_IMPLEMENTED;
}


int rtR0MemObjNativeMapKernel(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, void *pvFixed, size_t uAlignment, unsigned fProt)
{
    /*
     * Must have a memory descriptor.
     */
    int rc = VERR_INVALID_PARAMETER;
    PRTR0MEMOBJNT pMemToMapDarwin = (PRTR0MEMOBJNT)pMemToMap;
    if (pMemToMapDarwin->pMemDesc)
    {
        IOMemoryMap *pMemMap = pMemToMapDarwin->pMemDesc->map(kernel_task, kIOMapAnywhere,
                                                              kIOMapAnywhere | kIOMapDefaultCache);
        if (pMemMap)
        {
            IOVirtualAddress VirtAddr = pMemMap->getVirtualAddress();
            void *pv = (void *)(uintptr_t)VirtAddr;
            if ((uintptr_t)pv == VirtAddr)
            {
                /*
                 * Create the IPRT memory object.
                 */
                PRTR0MEMOBJNT pMemNt = (PRTR0MEMOBJNT)rtR0MemObjNew(sizeof(*pMemNt), RTR0MEMOBJTYPE_MAPPING,
                                                                                pv, pMemToMapDarwin->Core.cb);
                if (pMemNt)
                {
                    pMemNt->Core.u.Mapping.R0Process = NIL_RTR0PROCESS;
                    pMemNt->pMemMap = pMemMap;
                    *ppMem = &pMemNt->Core;
                    return VINF_SUCCESS;
                }

                rc = VERR_NO_MEMORY;
            }
            else
                rc = VERR_ADDRESS_TOO_BIG;
            pMemMap->release();
        }
        else
            rc = VERR_MAP_FAILED;
    }
    return rc;
}


int rtR0MemObjNativeMapUser(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, RTR3PTR R3PtrFixed, size_t uAlignment, unsigned fProt, RTR0PROCESS R0Process)
{
    /*
     * Must have a memory descriptor.
     */
    int rc = VERR_INVALID_PARAMETER;
    PRTR0MEMOBJNT pMemToMapDarwin = (PRTR0MEMOBJNT)pMemToMap;
    if (pMemToMapDarwin->pMemDesc)
    {
        IOMemoryMap *pMemMap = pMemToMapDarwin->pMemDesc->map((task_t)R0Process, kIOMapAnywhere,
                                                              kIOMapAnywhere | kIOMapDefaultCache);
        if (pMemMap)
        {
            IOVirtualAddress VirtAddr = pMemMap->getVirtualAddress();
            void *pv = (void *)(uintptr_t)VirtAddr;
            if ((uintptr_t)pv == VirtAddr)
            {
                /*
                 * Create the IPRT memory object.
                 */
                PRTR0MEMOBJNT pMemNt = (PRTR0MEMOBJNT)rtR0MemObjNew(sizeof(*pMemNt), RTR0MEMOBJTYPE_MAPPING,
                                                                                pv, pMemToMapDarwin->Core.cb);
                if (pMemNt)
                {
                    pMemNt->Core.u.Mapping.R0Process = R0Process;
                    pMemNt->pMemMap = pMemMap;
                    *ppMem = &pMemNt->Core;
                    return VINF_SUCCESS;
                }

                rc = VERR_NO_MEMORY;
            }
            else
                rc = VERR_ADDRESS_TOO_BIG;
            pMemMap->release();
        }
        else
            rc = VERR_MAP_FAILED;
    }
    return rc;
}


RTHCPHYS rtR0MemObjNativeGetPagePhysAddr(PRTR0MEMOBJINTERNAL pMem, unsigned iPage)
{
    RTHCPHYS            PhysAddr;
    PRTR0MEMOBJNT   pMemNt = (PRTR0MEMOBJNT)pMem;

#ifdef USE_VM_MAP_WIRE
    /*
     * Locked memory doesn't have a memory descriptor and
     * needs to be handled differently.
     */
    if (pMemNt->Core.enmType == RTR0MEMOBJTYPE_LOCK)
    {
        ppnum_t PgNo;
        if (pMemNt->Core.u.Lock.R0Process == NIL_RTR0PROCESS)
            PgNo = pmap_find_phys(kernel_pmap, (uintptr_t)pMemNt->Core.pv + iPage * PAGE_SIZE);
        else
        {
            /*
             * From what I can tell, Apple seems to have locked up the all the
             * available interfaces that could help us obtain the pmap_t of a task
             * or vm_map_t.

             * So, we'll have to figure out where in the vm_map_t  structure it is
             * and read it our selves. ASSUMING that kernel_pmap is pointed to by
             * kernel_map->pmap, we scan kernel_map to locate the structure offset.
             * Not nice, but it will hopefully do the job in a reliable manner...
             *
             * (get_task_pmap, get_map_pmap or vm_map_pmap is what we really need btw.)
             */
            static int s_offPmap = -1;
            if (RT_UNLIKELY(s_offPmap == -1))
            {
                pmap_t const *p = (pmap_t *)kernel_map;
                pmap_t const * const pEnd = p + 64;
                for (; p < pEnd; p++)
                    if (*p == kernel_pmap)
                    {
                        s_offPmap = (uintptr_t)p - (uintptr_t)kernel_map;
                        break;
                    }
                AssertReturn(s_offPmap >= 0, NIL_RTHCPHYS);
            }
            pmap_t Pmap = *(pmap_t *)((uintptr_t)get_task_map((task_t)pMemNt->Core.u.Lock.R0Process) + s_offPmap);
            PgNo = pmap_find_phys(Pmap, (uintptr_t)pMemNt->Core.pv + iPage * PAGE_SIZE);
        }

        AssertReturn(PgNo, NIL_RTHCPHYS);
        PhysAddr = (RTHCPHYS)PgNo << PAGE_SHIFT;
        Assert((PhysAddr >> PAGE_SHIFT) == PgNo);
    }
    else
#endif /* USE_VM_MAP_WIRE */
    {
        /*
         * Get the memory descriptor.
         */
        IOMemoryDescriptor *pMemDesc = pMemNt->pMemDesc;
        if (!pMemDesc)
            pMemDesc = pMemNt->pMemMap->getMemoryDescriptor();
        AssertReturn(pMemDesc, NIL_RTHCPHYS);

        /*
         * If we've got a memory descriptor, use getPhysicalSegment64().
         */
        addr64_t Addr = pMemDesc->getPhysicalSegment64(iPage * PAGE_SIZE, NULL);
        AssertMsgReturn(Addr, ("iPage=%u\n", iPage), NIL_RTHCPHYS);
        PhysAddr = Addr;
        AssertMsgReturn(PhysAddr == Addr, ("PhysAddr=%VHp Addr=%RX64\n", PhysAddr, (uint64_t)Addr), NIL_RTHCPHYS);
    }

    return PhysAddr;
}

