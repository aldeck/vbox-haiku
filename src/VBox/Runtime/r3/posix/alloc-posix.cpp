/* $Id$ */
/** @file
 * IPRT - Memory Allocation, POSIX.
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
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
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/param.h>
#include <iprt/err.h>
#include <iprt/string.h>

#include <stdlib.h>
#ifndef RT_OS_FREEBSD /* Deprecated on FreeBSD */
# include <malloc.h>
#endif
#include <errno.h>
#include <sys/mman.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#if !defined(RT_USE_MMAP_EXEC) && (defined(RT_OS_LINUX))
# define RT_USE_MMAP_EXEC
#endif

#if !defined(RT_USE_MMAP_PAGE) && 0 /** @todo mmap is too slow for full scale EF setup. */
# define RT_USE_MMAP_PAGE
#endif


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
#ifdef RT_USE_MMAP_EXEC
/**
 * RTMemExecAlloc() header used when using mmap for allocating the memory.
 */
typedef struct RTMEMEXECHDR
{
    /** Magic number (RTMEMEXECHDR_MAGIC). */
    size_t      uMagic;
    /** The size we requested from mmap. */
    size_t      cb;
# if ARCH_BITS == 32
    uint32_t    Alignment[2];
# endif
} RTMEMEXECHDR, *PRTMEMEXECHDR;

/** Magic for RTMEMEXECHDR. */
# define RTMEMEXECHDR_MAGIC (~(size_t)0xfeedbabe)

#endif  /* RT_USE_MMAP_EXEC */



RTDECL(void *) RTMemExecAllocTag(size_t cb, const char *pszTag) RT_NO_THROW
{
    AssertMsg(cb, ("Allocating ZERO bytes is really not a good idea! Good luck with the next assertion!\n"));

#ifdef RT_USE_MMAP_EXEC
    /*
     * Use mmap to get low memory.
     */
    size_t cbAlloc = RT_ALIGN_Z(cb + sizeof(RTMEMEXECHDR), PAGE_SIZE);
    void *pv = mmap(NULL, cbAlloc, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS
# if defined(RT_ARCH_AMD64) && defined(MAP_32BIT)
                    | MAP_32BIT
# endif
                    , -1, 0);
    AssertMsgReturn(pv != MAP_FAILED, ("errno=%d cb=%#zx\n", errno, cb), NULL);
    PRTMEMEXECHDR pHdr = (PRTMEMEXECHDR)pv;
    pHdr->uMagic = RTMEMEXECHDR_MAGIC;
    pHdr->cb = cbAlloc;
    pv = pHdr + 1;

#else
    /*
     * Allocate first.
     */
    cb = RT_ALIGN_Z(cb, 32);
    void *pv = NULL;
    int rc = posix_memalign(&pv, 32, cb);
    AssertMsg(!rc && pv, ("posix_memalign(%zd) failed!!! rc=%d\n", cb, rc));
    if (pv && !rc)
    {
        /*
         * Add PROT_EXEC flag to the page.
         *
         * This is in violation of the SuS where I think it saith that mprotect() shall
         * only be used with mmap()'ed memory. Works on linux and OS/2 LIBC v0.6.
         */
        memset(pv, 0xcc, cb);
        void   *pvProt = (void *)((uintptr_t)pv & ~PAGE_OFFSET_MASK);
        size_t  cbProt = ((uintptr_t)pv & PAGE_OFFSET_MASK) + cb;
        cbProt = RT_ALIGN_Z(cbProt, PAGE_SIZE);
        rc = mprotect(pvProt, cbProt, PROT_READ | PROT_WRITE | PROT_EXEC);
        if (rc)
        {
            AssertMsgFailed(("mprotect(%p, %#zx,,) -> rc=%d, errno=%d\n", pvProt, cbProt, rc, errno));
            free(pv);
            pv = NULL;
        }
    }
#endif
    return pv;
}


RTDECL(void)    RTMemExecFree(void *pv) RT_NO_THROW
{
    if (pv)
    {
#ifdef RT_USE_MMAP_EXEC
        PRTMEMEXECHDR pHdr = (PRTMEMEXECHDR)pv - 1;
        AssertMsgReturnVoid(RT_ALIGN_P(pHdr, PAGE_SIZE) == pHdr, ("pHdr=%p pv=%p\n", pHdr, pv));
        AssertMsgReturnVoid(pHdr->uMagic == RTMEMEXECHDR_MAGIC, ("pHdr=%p(uMagic=%#zx) pv=%p\n", pHdr, pHdr->uMagic, pv));
        int rc = munmap(pHdr, pHdr->cb);
        AssertMsg(!rc, ("munmap -> %d errno=%d\n", rc, errno)); NOREF(rc);
#else
        free(pv);
#endif
    }
}


RTDECL(void *) RTMemPageAllocTag(size_t cb, const char *pszTag) RT_NO_THROW
{
#ifdef RT_USE_MMAP_PAGE
    size_t  cbAligned = RT_ALIGN_Z(cb, PAGE_SIZE);
    void   *pv = mmap(NULL, cbAligned, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    AssertMsgReturn(pv != MAP_FAILED, ("errno=%d cb=%#zx\n", errno, cb), NULL);
    return pv;

#else
# if defined(RT_OS_FREEBSD) /** @todo huh? we're using posix_memalign in the next function... */
    void *pv;
    int rc = posix_memalign(&pv, PAGE_SIZE, RT_ALIGN_Z(cb, PAGE_SIZE));
    if (!rc)
        return pv;
    return NULL;
# else /* !RT_OS_FREEBSD */
    return memalign(PAGE_SIZE, cb);
# endif
#endif
}


RTDECL(void *) RTMemPageAllocZTag(size_t cb, const char *pszTag) RT_NO_THROW
{
#ifdef RT_USE_MMAP_PAGE
    size_t  cbAligned = RT_ALIGN_Z(cb, PAGE_SIZE);
    void   *pv = mmap(NULL, cbAligned, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    AssertMsgReturn(pv != MAP_FAILED, ("errno=%d cb=%#zx\n", errno, cb), NULL);
    return pv;

#else
    void *pv;
    int rc = posix_memalign(&pv, PAGE_SIZE, RT_ALIGN_Z(cb, PAGE_SIZE));
    if (!rc)
    {
        RT_BZERO(pv, RT_ALIGN_Z(cb, PAGE_SIZE));
        return pv;
    }
    return NULL;
#endif
}


RTDECL(void) RTMemPageFree(void *pv, size_t cb) RT_NO_THROW
{
    if (pv)
    {
        Assert(!((uintptr_t)pv & PAGE_OFFSET_MASK));

#ifdef RT_USE_MMAP_PAGE
        size_t cbAligned = RT_ALIGN_Z(cb, PAGE_SIZE);
        int rc = munmap(pv, cbAligned);
        AssertMsg(!rc, ("munmap(%p, %#zx) -> %d errno=%d\n", pv, cbAligned, rc, errno)); NOREF(rc);
#else
        free(pv);
#endif
    }
}


RTDECL(int) RTMemProtect(void *pv, size_t cb, unsigned fProtect) RT_NO_THROW
{
    /*
     * Validate input.
     */
    if (cb == 0)
    {
        AssertMsgFailed(("!cb\n"));
        return VERR_INVALID_PARAMETER;
    }
    if (fProtect & ~(RTMEM_PROT_NONE | RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC))
    {
        AssertMsgFailed(("fProtect=%#x\n", fProtect));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Convert the flags.
     */
    int fProt;
#if     RTMEM_PROT_NONE  == PROT_NONE \
    &&  RTMEM_PROT_READ  == PROT_READ \
    &&  RTMEM_PROT_WRITE == PROT_WRITE \
    &&  RTMEM_PROT_EXEC  == PROT_EXEC
    fProt = fProtect;
#else
    Assert(!RTMEM_PROT_NONE);
    if (!fProtect)
        fProt = PROT_NONE;
    else
    {
        fProt = 0;
        if (fProtect & RTMEM_PROT_READ)
            fProt |= PROT_READ;
        if (fProtect & RTMEM_PROT_WRITE)
            fProt |= PROT_WRITE;
        if (fProtect & RTMEM_PROT_EXEC)
            fProt |= PROT_EXEC;
    }
#endif

    /*
     * Align the request.
     */
    cb += (uintptr_t)pv & PAGE_OFFSET_MASK;
    pv = (void *)((uintptr_t)pv & ~PAGE_OFFSET_MASK);

    /*
     * Change the page attributes.
     */
    int rc = mprotect(pv, cb, fProt);
    if (!rc)
        return rc;
    return RTErrConvertFromErrno(errno);
}
