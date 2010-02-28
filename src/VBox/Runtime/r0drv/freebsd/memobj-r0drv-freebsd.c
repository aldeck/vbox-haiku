/* $Id$ */
/** @file
 * IPRT - Ring-0 Memory Objects, FreeBSD.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "the-freebsd-kernel.h"

#include <iprt/memobj.h>
#include <iprt/mem.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/param.h>
#include <iprt/process.h>
#include "internal/memobj.h"


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * The FreeBSD version of the memory object structure.
 */
typedef struct RTR0MEMOBJFREEBSD
{
    /** The core structure. */
    RTR0MEMOBJINTERNAL  Core;
    /** Type dependent data */
    union
    {
        /** Everything not physical */
        struct
        {
            /** The VM object associated with the allocation. */
            vm_object_t         pObject;
        } NonPhys;
        /** Physical contiguous/non-contiguous memory */
        struct
        {
            /** Number of allocated pages */
            uint32_t            cPages;
            /** Array of allocated pages. */
            vm_page_t           apPages[1];
        } Phys;
    } u;
} RTR0MEMOBJFREEBSD, *PRTR0MEMOBJFREEBSD;


MALLOC_DEFINE(M_IPRTMOBJ, "iprtmobj", "IPRT - R0MemObj");

/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/


int rtR0MemObjNativeFree(RTR0MEMOBJ pMem)
{
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)pMem;
    int rc;

    switch (pMemFreeBSD->Core.enmType)
    {
        case RTR0MEMOBJTYPE_CONT:
            contigfree(pMemFreeBSD->Core.pv, pMemFreeBSD->Core.cb, M_IPRTMOBJ);
            break;

        case RTR0MEMOBJTYPE_PAGE:
            if (pMemFreeBSD->u.NonPhys.pObject)
            {
                rc = vm_map_remove(kernel_map,
                                   (vm_offset_t)pMemFreeBSD->Core.pv,
                                   (vm_offset_t)pMemFreeBSD->Core.pv + pMemFreeBSD->Core.cb);
                AssertMsg(rc == KERN_SUCCESS, ("%#x", rc));
            }
            else
            {
                contigfree(pMemFreeBSD->Core.pv, pMemFreeBSD->Core.cb, M_IPRTMOBJ);
                rc = vm_map_remove(kernel_map,
                                   (vm_offset_t)pMemFreeBSD->Core.pv,
                                   (vm_offset_t)pMemFreeBSD->Core.pv + pMemFreeBSD->Core.cb);
                AssertMsg(rc == KERN_SUCCESS, ("%#x", rc));
            }
            break;

        case RTR0MEMOBJTYPE_LOCK:
        {
            int fFlags = VM_MAP_WIRE_NOHOLES;
            vm_map_t pMap = kernel_map;

            if (pMemFreeBSD->Core.u.Lock.R0Process != NIL_RTR0PROCESS)
            {
                pMap = &((struct proc *)pMemFreeBSD->Core.u.Lock.R0Process)->p_vmspace->vm_map;
                fFlags |= VM_MAP_WIRE_USER;
            }
            else
                fFlags |= VM_MAP_WIRE_SYSTEM;

            rc = vm_map_unwire(pMap,
                               (vm_offset_t)pMemFreeBSD->Core.pv,
                               (vm_offset_t)pMemFreeBSD->Core.pv + pMemFreeBSD->Core.cb,
                               fFlags);
            AssertMsg(rc == KERN_SUCCESS, ("%#x", rc));
            break;
        }

        case RTR0MEMOBJTYPE_RES_VIRT:
        {
            vm_map_t pMap = kernel_map;
            if (pMemFreeBSD->Core.u.Lock.R0Process != NIL_RTR0PROCESS)
                pMap = &((struct proc *)pMemFreeBSD->Core.u.Lock.R0Process)->p_vmspace->vm_map;
            rc = vm_map_remove(pMap,
                               (vm_offset_t)pMemFreeBSD->Core.pv,
                               (vm_offset_t)pMemFreeBSD->Core.pv + pMemFreeBSD->Core.cb);
            AssertMsg(rc == KERN_SUCCESS, ("%#x", rc));
            break;
        }

        case RTR0MEMOBJTYPE_MAPPING:
        {
            vm_map_t pMap = kernel_map;

            if (pMemFreeBSD->Core.u.Mapping.R0Process != NIL_RTR0PROCESS)
                pMap = &((struct proc *)pMemFreeBSD->Core.u.Mapping.R0Process)->p_vmspace->vm_map;

            rc = vm_map_remove(pMap,
                               (vm_offset_t)pMemFreeBSD->Core.pv,
                               (vm_offset_t)pMemFreeBSD->Core.pv + pMemFreeBSD->Core.cb);
            AssertMsg(rc == KERN_SUCCESS, ("%#x", rc));
            break;
        }

        case RTR0MEMOBJTYPE_PHYS:
        case RTR0MEMOBJTYPE_PHYS_NC:
        {
            vm_page_lock_queues();
            for (uint32_t iPage = 0; iPage < pMemFreeBSD->u.Phys.cPages; iPage++)
            {
                vm_page_t pPage = pMemFreeBSD->u.Phys.apPages[iPage];
                pPage->wire_count--;
                vm_page_free_toq(pPage);
            }
            vm_page_unlock_queues();
            break;
        }

        /* unused: */
        case RTR0MEMOBJTYPE_LOW:
        default:
            AssertMsgFailed(("enmType=%d\n", pMemFreeBSD->Core.enmType));
            return VERR_INTERNAL_ERROR;
    }

    return VINF_SUCCESS;
}


int rtR0MemObjNativeAllocPage(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable)
{
    int rc;
    size_t cPages = cb >> PAGE_SHIFT;

    /* create the object. */
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(*pMemFreeBSD), RTR0MEMOBJTYPE_PAGE, NULL, cb);
    if (!pMemFreeBSD)
        return VERR_NO_MEMORY;

    pMemFreeBSD->u.NonPhys.pObject = vm_object_allocate(OBJT_DEFAULT, cPages);
    if (pMemFreeBSD->u.NonPhys.pObject)
    {
        vm_offset_t MapAddress = vm_map_min(kernel_map);
        rc = vm_map_find(kernel_map,                    /* map */
                         pMemFreeBSD->u.NonPhys.pObject, /* object */
                         0,                             /* offset */
                         &MapAddress,                   /* addr (IN/OUT) */
                         cb,                            /* length */
                         TRUE,                          /* find_space */
                         fExecutable                    /* protection */
                         ? VM_PROT_ALL
                         : VM_PROT_RW,
                         VM_PROT_ALL,                   /* max(_prot) */
                         FALSE);                        /* cow (copy-on-write) */
        if (rc == KERN_SUCCESS)
        {
            vm_offset_t AddressDst = MapAddress;

            rc = VINF_SUCCESS;

            VM_OBJECT_LOCK(pMemFreeBSD->u.NonPhys.pObject);
            for (size_t iPage = 0; iPage < cPages; iPage++)
            {
                vm_pindex_t PageIndex = OFF_TO_IDX(AddressDst);
                vm_page_t   pPage;

                pPage = vm_page_alloc(pMemFreeBSD->u.NonPhys.pObject, PageIndex,
                                      VM_ALLOC_NOBUSY | VM_ALLOC_SYSTEM |
                                      VM_ALLOC_WIRED);

#if __FreeBSD_version >= 800000 /** @todo Find exact version number */
                /* Fixes crashes during VM termination on FreeBSD8-CURRENT amd64
                 * with kernel debugging enabled. */
                vm_page_set_valid(pPage, 0, PAGE_SIZE);
#endif

                if (pPage)
                {
                    vm_page_lock_queues();
                    vm_page_wire(pPage);
                    vm_page_unlock_queues();
                    /* Put the page into the page table now. */
#if __FreeBSD_version >= 701105
                    pmap_enter(kernel_map->pmap, AddressDst, VM_PROT_NONE, pPage,
                               fExecutable
                               ? VM_PROT_ALL
                               : VM_PROT_RW,
                               TRUE);
#else
                    pmap_enter(kernel_map->pmap, AddressDst, pPage,
                               fExecutable
                               ? VM_PROT_ALL
                               : VM_PROT_RW,
                               TRUE);
#endif
                }
                else
                {
                    /*
                     * Allocation failed. vm_map_remove will remove any
                     * page already alocated.
                     */
                    rc = VERR_NO_MEMORY;
                    break;
                }
                AddressDst += PAGE_SIZE;
            }
            VM_OBJECT_UNLOCK(pMemFreeBSD->u.NonPhys.pObject);

            if (rc == VINF_SUCCESS)
            {
                pMemFreeBSD->Core.pv = (void *)MapAddress;
                *ppMem = &pMemFreeBSD->Core;
                return VINF_SUCCESS;
            }

            vm_map_remove(kernel_map,
                          MapAddress,
                          MapAddress + cb);
        }
        rc = VERR_NO_MEMORY; /** @todo fix translation (borrow from darwin) */
    }
    else
        rc = VERR_NO_MEMORY;

    rtR0MemObjDelete(&pMemFreeBSD->Core);
    return rc;
}


int rtR0MemObjNativeAllocLow(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable)
{
    /*
     * Try a Alloc first and see if we get luck, if not try contigmalloc.
     * Might wish to try find our own pages or something later if this
     * turns into a problemspot on AMD64 boxes.
     */
    int rc = rtR0MemObjNativeAllocPage(ppMem, cb, fExecutable);
    if (RT_SUCCESS(rc))
    {
        size_t iPage = cb >> PAGE_SHIFT;
        while (iPage-- > 0)
            if (rtR0MemObjNativeGetPagePhysAddr(*ppMem, iPage) > (_4G - PAGE_SIZE))
            {
                RTR0MemObjFree(*ppMem, false);
                *ppMem = NULL;
                rc = VERR_NO_MEMORY;
                break;
            }
    }
    if (RT_FAILURE(rc))
        rc = rtR0MemObjNativeAllocCont(ppMem, cb, fExecutable);
    return rc;
}


int rtR0MemObjNativeAllocCont(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable)
{
    /* create the object. */
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(*pMemFreeBSD), RTR0MEMOBJTYPE_CONT, NULL, cb);
    if (!pMemFreeBSD)
        return VERR_NO_MEMORY;

    /* do the allocation. */
    pMemFreeBSD->Core.pv = contigmalloc(cb,                   /* size */
                                        M_IPRTMOBJ,           /* type */
                                        M_NOWAIT | M_ZERO,    /* flags */
                                        0,                    /* lowest physical address*/
                                        _4G-1,                /* highest physical address */
                                        PAGE_SIZE,            /* alignment. */
                                        0);                   /* boundrary */
    if (pMemFreeBSD->Core.pv)
    {
        pMemFreeBSD->Core.u.Cont.Phys = vtophys(pMemFreeBSD->Core.pv);
        *ppMem = &pMemFreeBSD->Core;
        return VINF_SUCCESS;
    }

    NOREF(fExecutable);
    rtR0MemObjDelete(&pMemFreeBSD->Core);
    return VERR_NO_MEMORY;
}

static int rtR0MemObjFreeBSDAllocPhysPages(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJTYPE enmType,
                                           size_t cb,
                                           RTHCPHYS PhysHighest, size_t uAlignment,
                                           bool fContiguous)
{
    int        rc = VINF_SUCCESS;
    uint32_t   cPages = cb >> PAGE_SHIFT;
    vm_paddr_t VmPhysAddrHigh;

    /* create the object. */
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(RT_OFFSETOF(RTR0MEMOBJFREEBSD, u.Phys.apPages[cPages]), enmType, NULL, cb);
    if (!pMemFreeBSD)
        return VERR_NO_MEMORY;

    pMemFreeBSD->u.Phys.cPages = cPages;

    /*
     * For now allocate contiguous pages
     * if there is an upper limit or
     * the alignment is not on a page boundary.
     */
    if (PhysHighest != NIL_RTHCPHYS)
    {
        VmPhysAddrHigh = PhysHighest;
        fContiguous = true;
    }
    else
        VmPhysAddrHigh = ~(vm_paddr_t)0;

    if (uAlignment != PAGE_SIZE)
        fContiguous = true;

    mtx_lock(&vm_page_queue_free_mtx);
    if (fContiguous)
    {
        vm_page_t pPage = vm_phys_alloc_contig(cPages, 0, VmPhysAddrHigh, uAlignment, 0);

        if (pPage)
            for (uint32_t iPage = 0; iPage < cPages; iPage++)
            {
                pPage[iPage].flags     &= ~PG_FREE;
                pPage[iPage].wire_count = 1;
                atomic_add_int(&cnt.v_wire_count, 1);
                pMemFreeBSD->u.Phys.apPages[iPage] = &pPage[iPage];
            }
        else
            rc = VERR_NO_MEMORY;
    }
    else
    {
        /* Allocate page by page */
        for (uint32_t iPage = 0; iPage < cPages; iPage++)
        {
            vm_page_t pPage = vm_phys_alloc_pages(VM_FREEPOOL_DEFAULT, 0);

            if (!pPage)
            {
                vm_page_lock_queues();
                while (iPage-- > 0)
                {
                    pMemFreeBSD->u.Phys.apPages[iPage]->wire_count--;
                    vm_page_free_toq(pMemFreeBSD->u.Phys.apPages[iPage]);
                }
                vm_page_unlock_queues();

                rc = VERR_NO_MEMORY;
                break;
            }

            pPage->flags     &= ~PG_FREE;
            pPage->valid      = VM_PAGE_BITS_ALL;
            pPage->wire_count = 1;
            atomic_add_int(&cnt.v_wire_count, 1);
            pMemFreeBSD->u.Phys.apPages[iPage] = pPage;
        }
    }
    mtx_unlock(&vm_page_queue_free_mtx);

    if (RT_FAILURE(rc))
        rtR0MemObjDelete(&pMemFreeBSD->Core);
    else
    {
        if (enmType == RTR0MEMOBJTYPE_PHYS)
        {
            pMemFreeBSD->Core.u.Phys.PhysBase = VM_PAGE_TO_PHYS(pMemFreeBSD->u.Phys.apPages[0]);
            pMemFreeBSD->Core.u.Phys.fAllocated = true;
        }

        *ppMem = &pMemFreeBSD->Core;
    }

    return rc;
}

int rtR0MemObjNativeAllocPhys(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest, size_t uAlignment)
{
    return rtR0MemObjFreeBSDAllocPhysPages(ppMem, RTR0MEMOBJTYPE_PHYS, cb, PhysHighest, uAlignment, true);
}


int rtR0MemObjNativeAllocPhysNC(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest)
{
    return rtR0MemObjFreeBSDAllocPhysPages(ppMem, RTR0MEMOBJTYPE_PHYS_NC, cb, PhysHighest, PAGE_SIZE, false);
}


int rtR0MemObjNativeEnterPhys(PPRTR0MEMOBJINTERNAL ppMem, RTHCPHYS Phys, size_t cb)
{
    /* create the object. */
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(*pMemFreeBSD), RTR0MEMOBJTYPE_PHYS, NULL, cb);
    if (!pMemFreeBSD)
        return VERR_NO_MEMORY;

    /* there is no allocation here, it needs to be mapped somewhere first. */
    pMemFreeBSD->Core.u.Phys.fAllocated = false;
    pMemFreeBSD->Core.u.Phys.PhysBase = Phys;
    *ppMem = &pMemFreeBSD->Core;
    return VINF_SUCCESS;
}


int rtR0MemObjNativeLockUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3Ptr, size_t cb, uint32_t fAccess, RTR0PROCESS R0Process)
{
    int rc;
    NOREF(fAccess);

    /* create the object. */
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(*pMemFreeBSD), RTR0MEMOBJTYPE_LOCK, (void *)R3Ptr, cb);
    if (!pMemFreeBSD)
        return VERR_NO_MEMORY;

    /*
     * We could've used vslock here, but we don't wish to be subject to
     * resource usage restrictions, so we'll call vm_map_wire directly.
     */
    rc = vm_map_wire(&((struct proc *)R0Process)->p_vmspace->vm_map, /* the map */
                     (vm_offset_t)R3Ptr,                             /* start */
                     (vm_offset_t)R3Ptr + cb,                        /* end */
                     VM_MAP_WIRE_USER | VM_MAP_WIRE_NOHOLES);        /* flags */
    if (rc == KERN_SUCCESS)
    {
        pMemFreeBSD->Core.u.Lock.R0Process = R0Process;
        *ppMem = &pMemFreeBSD->Core;
        return VINF_SUCCESS;
    }
    rtR0MemObjDelete(&pMemFreeBSD->Core);
    return VERR_NO_MEMORY;/** @todo fix mach -> vbox error conversion for freebsd. */
}


int rtR0MemObjNativeLockKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pv, size_t cb, uint32_t fAccess)
{
    int rc;
    NOREF(fAccess);

    /* create the object. */
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(*pMemFreeBSD), RTR0MEMOBJTYPE_LOCK, pv, cb);
    if (!pMemFreeBSD)
        return VERR_NO_MEMORY;

    /* lock the memory */
    rc = vm_map_wire(kernel_map,                                    /* the map */
                     (vm_offset_t)pv,                               /* start */
                     (vm_offset_t)pv + cb,                          /* end */
                     VM_MAP_WIRE_SYSTEM | VM_MAP_WIRE_NOHOLES);     /* flags - SYSTEM? */
    if (rc == KERN_SUCCESS)
    {
        pMemFreeBSD->Core.u.Lock.R0Process = NIL_RTR0PROCESS;
        *ppMem = &pMemFreeBSD->Core;
        return VINF_SUCCESS;
    }
    rtR0MemObjDelete(&pMemFreeBSD->Core);
    return VERR_NO_MEMORY;/** @todo fix mach -> vbox error conversion for freebsd. */
}


/**
 * Worker for the two virtual address space reservers.
 *
 * We're leaning on the examples provided by mmap and vm_mmap in vm_mmap.c here.
 */
static int rtR0MemObjNativeReserveInMap(PPRTR0MEMOBJINTERNAL ppMem, void *pvFixed, size_t cb, size_t uAlignment, RTR0PROCESS R0Process, vm_map_t pMap)
{
    int rc;

    /*
     * The pvFixed address range must be within the VM space when specified.
     */
    if (pvFixed != (void *)-1
        && (    (vm_offset_t)pvFixed      < vm_map_min(pMap)
            ||  (vm_offset_t)pvFixed + cb > vm_map_max(pMap)))
        return VERR_INVALID_PARAMETER;

    /*
     * Check that the specified alignment is supported.
     */
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;

    /*
     * Create the object.
     */
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(*pMemFreeBSD), RTR0MEMOBJTYPE_RES_VIRT, NULL, cb);
    if (!pMemFreeBSD)
        return VERR_NO_MEMORY;

    /*
     * Allocate an empty VM object and map it into the requested map.
     */
    pMemFreeBSD->u.NonPhys.pObject = vm_object_allocate(OBJT_DEFAULT, cb >> PAGE_SHIFT);
    if (pMemFreeBSD->u.NonPhys.pObject)
    {
        vm_offset_t MapAddress = pvFixed != (void *)-1
                               ? (vm_offset_t)pvFixed
                               : vm_map_min(pMap);
        if (pvFixed != (void *)-1)
            vm_map_remove(pMap,
                          MapAddress,
                          MapAddress + cb);

        rc = vm_map_find(pMap,                          /* map */
                         pMemFreeBSD->u.NonPhys.pObject, /* object */
                         0,                             /* offset */
                         &MapAddress,                   /* addr (IN/OUT) */
                         cb,                            /* length */
                         pvFixed == (void *)-1,         /* find_space */
                         VM_PROT_NONE,                  /* protection */
                         VM_PROT_ALL,                   /* max(_prot) ?? */
                         0);                            /* cow (copy-on-write) */
        if (rc == KERN_SUCCESS)
        {
            if (R0Process != NIL_RTR0PROCESS)
            {
                rc = vm_map_inherit(pMap,
                                    MapAddress,
                                    MapAddress + cb,
                                    VM_INHERIT_SHARE);
                AssertMsg(rc == KERN_SUCCESS, ("%#x\n", rc));
            }
            pMemFreeBSD->Core.pv = (void *)MapAddress;
            pMemFreeBSD->Core.u.ResVirt.R0Process = R0Process;
            *ppMem = &pMemFreeBSD->Core;
            return VINF_SUCCESS;
        }
        vm_object_deallocate(pMemFreeBSD->u.NonPhys.pObject);
        rc = VERR_NO_MEMORY; /** @todo fix translation (borrow from darwin) */
    }
    else
        rc = VERR_NO_MEMORY;
    rtR0MemObjDelete(&pMemFreeBSD->Core);
    return rc;

}

int rtR0MemObjNativeReserveKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pvFixed, size_t cb, size_t uAlignment)
{
    return rtR0MemObjNativeReserveInMap(ppMem, pvFixed, cb, uAlignment, NIL_RTR0PROCESS, kernel_map);
}


int rtR0MemObjNativeReserveUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3PtrFixed, size_t cb, size_t uAlignment, RTR0PROCESS R0Process)
{
    return rtR0MemObjNativeReserveInMap(ppMem, (void *)R3PtrFixed, cb, uAlignment, R0Process,
                                        &((struct proc *)R0Process)->p_vmspace->vm_map);
}


int rtR0MemObjNativeMapKernel(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, void *pvFixed, size_t uAlignment,
                              unsigned fProt, size_t offSub, size_t cbSub)
{
    AssertMsgReturn(!offSub && !cbSub, ("%#x %#x\n", offSub, cbSub), VERR_NOT_SUPPORTED);
    AssertMsgReturn(pvFixed == (void *)-1, ("%p\n", pvFixed), VERR_NOT_SUPPORTED);

    /*
     * Check that the specified alignment is supported.
     */
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;



/* Phys: see pmap_mapdev in i386/i386/pmap.c (http://fxr.watson.org/fxr/source/i386/i386/pmap.c?v=RELENG62#L2860) */

#if 0
/** @todo finish the implementation. */

    int rc;
    void *pvR0 = NULL;
    PRTR0MEMOBJFREEBSD pMemToMapOs2 = (PRTR0MEMOBJFREEBSD)pMemToMap;
    switch (pMemToMapOs2->Core.enmType)
    {
        /*
         * These has kernel mappings.
         */
        case RTR0MEMOBJTYPE_PAGE:
        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_CONT:
            pvR0 = pMemToMapOs2->Core.pv;
            break;

        case RTR0MEMOBJTYPE_PHYS_NC:
        case RTR0MEMOBJTYPE_PHYS:
            pvR0 = pMemToMapOs2->Core.pv;
            if (!pvR0)
            {
                /* no ring-0 mapping, so allocate a mapping in the process. */
                AssertMsgReturn(uAlignment == PAGE_SIZE, ("%#zx\n", uAlignment), VERR_NOT_SUPPORTED);
                AssertMsgReturn(fProt & RTMEM_PROT_WRITE, ("%#x\n", fProt), VERR_NOT_SUPPORTED);
                Assert(!pMemToMapOs2->Core.u.Phys.fAllocated);
                ULONG ulPhys = pMemToMapOs2->Core.u.Phys.PhysBase;
                rc = KernVMAlloc(pMemToMapOs2->Core.cb, VMDHA_PHYS, &pvR0, (PPVOID)&ulPhys, NULL);
                if (rc)
                    return RTErrConvertFromOS2(rc);
                pMemToMapOs2->Core.pv = pvR0;
            }
            break;

        case RTR0MEMOBJTYPE_LOCK:
            if (pMemToMapOs2->Core.u.Lock.R0Process != NIL_RTR0PROCESS)
                return VERR_NOT_SUPPORTED; /** @todo implement this... */
            pvR0 = pMemToMapOs2->Core.pv;
            break;

        case RTR0MEMOBJTYPE_RES_VIRT:
        case RTR0MEMOBJTYPE_MAPPING:
        default:
            AssertMsgFailed(("enmType=%d\n", pMemToMapOs2->Core.enmType));
            return VERR_INTERNAL_ERROR;
    }

    /*
     * Create a dummy mapping object for it.
     *
     * All mappings are read/write/execute in OS/2 and there isn't
     * any cache options, so sharing is ok. And the main memory object
     * isn't actually freed until all the mappings have been freed up
     * (reference counting).
     */
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(RT_OFFSETOF(RTR0MEMOBJOS2, Lock), RTR0MEMOBJTYPE_MAPPING, pvR0, pMemToMapOs2->Core.cb);
    if (pMemFreeBSD)
    {
        pMemFreeBSD->Core.u.Mapping.R0Process = NIL_RTR0PROCESS;
        *ppMem = &pMemFreeBSD->Core;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
#endif
    return VERR_NOT_IMPLEMENTED;
}


/* see http://markmail.org/message/udhq33tefgtyfozs */
int rtR0MemObjNativeMapUser(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, RTR3PTR R3PtrFixed, size_t uAlignment, unsigned fProt, RTR0PROCESS R0Process)
{
    /*
     * Check for unsupported stuff.
     */
    AssertMsgReturn(R0Process == RTR0ProcHandleSelf(), ("%p != %p\n", R0Process, RTR0ProcHandleSelf()), VERR_NOT_SUPPORTED);
    AssertMsgReturn(R3PtrFixed == (RTR3PTR)-1, ("%p\n", R3PtrFixed), VERR_NOT_SUPPORTED);
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;

    int                rc;
    PRTR0MEMOBJFREEBSD pMemToMapFreeBSD = (PRTR0MEMOBJFREEBSD)pMemToMap;
    struct proc       *pProc            = (struct proc *)R0Process;
    struct vm_map     *pProcMap         = &pProc->p_vmspace->vm_map;

    /* calc protection */
    vm_prot_t       ProtectionFlags = 0;
    if ((fProt & RTMEM_PROT_NONE) == RTMEM_PROT_NONE)
        ProtectionFlags = VM_PROT_NONE;
    if ((fProt & RTMEM_PROT_READ) == RTMEM_PROT_READ)
        ProtectionFlags |= VM_PROT_READ;
    if ((fProt & RTMEM_PROT_WRITE) == RTMEM_PROT_WRITE)
        ProtectionFlags |= VM_PROT_WRITE;
    if ((fProt & RTMEM_PROT_EXEC) == RTMEM_PROT_EXEC)
        ProtectionFlags |= VM_PROT_EXECUTE;

    /* calc mapping address */
    PROC_LOCK(pProc);
    vm_offset_t AddrR3 = round_page((vm_offset_t)pProc->p_vmspace->vm_daddr + lim_max(pProc, RLIMIT_DATA));
    PROC_UNLOCK(pProc);

    vm_object_t pObjectNew = vm_object_allocate(OBJT_PHYS, pMemToMap->cb >> PAGE_SHIFT);
    if (!RT_UNLIKELY(pObjectNew))
        return VERR_NO_MEMORY;

    /* Insert the object in the map. */
    rc = vm_map_find(pProcMap,              /* Map to insert the object in */
                     pObjectNew,            /* Object to map */
                     0,                     /* Start offset in the object */
                     &AddrR3,               /* Start address IN/OUT */
                     pMemToMap->cb,         /* Size of the mapping */
                     TRUE,                  /* Whether a suitable address should be searched for first */
                     ProtectionFlags,       /* protection flags */
                     VM_PROT_ALL,           /* Maximum protection flags */
                     0);                    /* Copy on write */

    /* Map the memory page by page into the destination map. */
    if (rc == KERN_SUCCESS)
    {
        size_t         cPages;
        vm_offset_t    AddrToMap    = (vm_offset_t)pMemToMap->pv;
        pmap_t         pPhysicalMap = pProcMap->pmap;
        vm_offset_t    AddrR3Dst    = AddrR3;

        if (   pMemToMap->enmType == RTR0MEMOBJTYPE_PHYS
            || pMemToMap->enmType == RTR0MEMOBJTYPE_PHYS_NC)
            cPages = pMemToMapFreeBSD->u.Phys.cPages;
        else
            cPages = pMemToMap->cb >> PAGE_SHIFT;

        /* Insert the memory page by page into the mapping. */
        for (uint32_t iPage = 0; iPage < cPages; iPage++)
        {
            vm_page_t pPage;

            if (   pMemToMap->enmType == RTR0MEMOBJTYPE_PHYS
                || pMemToMap->enmType == RTR0MEMOBJTYPE_PHYS_NC)
                pPage = pMemToMapFreeBSD->u.Phys.apPages[iPage];
            else
            {
                pPage = PHYS_TO_VM_PAGE(vtophys(AddrToMap));
                AddrToMap += PAGE_SIZE;
            }

#if __FreeBSD_version >= 701105
            pmap_enter(pPhysicalMap, AddrR3Dst, VM_PROT_NONE, pPage, ProtectionFlags, TRUE);
#else
            pmap_enter(pPhysicalMap, AddrR3Dst, pPage, ProtectionFlags, TRUE);
#endif
            AddrR3Dst += PAGE_SIZE;
        }
    }
    else
        vm_object_deallocate(pObjectNew);

    if (rc == KERN_SUCCESS)
    {
        /*
         * Create a mapping object for it.
         */
        PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(RTR0MEMOBJFREEBSD),
                                                                           RTR0MEMOBJTYPE_MAPPING,
                                                                           (void *)AddrR3,
                                                                           pMemToMap->cb);
        if (pMemFreeBSD)
        {
            Assert((vm_offset_t)pMemFreeBSD->Core.pv == AddrR3);
            pMemFreeBSD->Core.u.Mapping.R0Process = R0Process;
            *ppMem = &pMemFreeBSD->Core;
            return VINF_SUCCESS;
        }

        rc = vm_map_remove(pProcMap, ((vm_offset_t)AddrR3), ((vm_offset_t)AddrR3) + pMemToMap->cb);
        AssertMsg(rc == KERN_SUCCESS, ("Deleting mapping failed\n"));
    }

    return VERR_NO_MEMORY;
}


int rtR0MemObjNativeProtect(PRTR0MEMOBJINTERNAL pMem, size_t offSub, size_t cbSub, uint32_t fProt)
{
    NOREF(pMem);
    NOREF(offSub);
    NOREF(cbSub);
    NOREF(fProt);
    return VERR_NOT_SUPPORTED;
}


RTHCPHYS rtR0MemObjNativeGetPagePhysAddr(PRTR0MEMOBJINTERNAL pMem, size_t iPage)
{
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)pMem;

    switch (pMemFreeBSD->Core.enmType)
    {
        case RTR0MEMOBJTYPE_LOCK:
        {
            if (    pMemFreeBSD->Core.u.Lock.R0Process != NIL_RTR0PROCESS
                &&  pMemFreeBSD->Core.u.Lock.R0Process != (RTR0PROCESS)curproc)
            {
                /* later */
                return NIL_RTHCPHYS;
            }

            vm_offset_t pb = (vm_offset_t)pMemFreeBSD->Core.pv + (iPage << PAGE_SHIFT);

            struct proc    *pProc     = (struct proc *)pMemFreeBSD->Core.u.Lock.R0Process;
            struct vm_map  *pProcMap  = &pProc->p_vmspace->vm_map;
            pmap_t pPhysicalMap       = pProcMap->pmap;

            return pmap_extract(pPhysicalMap, pb);
        }

        case RTR0MEMOBJTYPE_PAGE:
        {
            vm_offset_t pb = (vm_offset_t)pMemFreeBSD->Core.pv + (iPage << PAGE_SHIFT);
            return vtophys(pb);
        }

        case RTR0MEMOBJTYPE_MAPPING:
        {
            vm_offset_t pb = (vm_offset_t)pMemFreeBSD->Core.pv + (iPage << PAGE_SHIFT);

            if (pMemFreeBSD->Core.u.Mapping.R0Process != NIL_RTR0PROCESS)
            {
                struct proc    *pProc     = (struct proc *)pMemFreeBSD->Core.u.Mapping.R0Process;
                struct vm_map  *pProcMap  = &pProc->p_vmspace->vm_map;
                pmap_t pPhysicalMap       = pProcMap->pmap;

                return pmap_extract(pPhysicalMap, pb);
            }
            return vtophys(pb);
        }

        case RTR0MEMOBJTYPE_CONT:
            return pMemFreeBSD->Core.u.Cont.Phys + (iPage << PAGE_SHIFT);

        case RTR0MEMOBJTYPE_PHYS:
            return pMemFreeBSD->Core.u.Phys.PhysBase + (iPage << PAGE_SHIFT);

        case RTR0MEMOBJTYPE_PHYS_NC:
        {
            RTHCPHYS PhysAddr = NIL_RTHCPHYS;

            if (iPage < pMemFreeBSD->u.Phys.cPages)
                PhysAddr = VM_PAGE_TO_PHYS(pMemFreeBSD->u.Phys.apPages[iPage]);

            return PhysAddr;
        }
        case RTR0MEMOBJTYPE_RES_VIRT:
        case RTR0MEMOBJTYPE_LOW:
        default:
            return NIL_RTHCPHYS;
    }
}

