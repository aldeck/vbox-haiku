/* $Id$ */
/** @file
 * PGM - Page Manager and Monitor, Physical Memory Addressing.
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


/**
 * Read physical memory. (one byte/word/dword)
 *
 * This API respects access handlers and MMIO. Use PGMPhysReadGCPhys() if you
 * want to ignore those.
 *
 * @param   pVM             VM Handle.
 * @param   GCPhys          Physical address start reading from.
 */
PGMDECL(PGMPHYS_DATATYPE) PGMPHYSFN_READNAME(PVM pVM, RTGCPHYS GCPhys)
{
    uint32_t iCacheIndex;

    Assert(VM_IS_EMT(pVM));

#ifdef PGM_PHYSMEMACCESS_CACHING
    if (pVM->pgm.s.fPhysCacheFlushPending)
    {
        pVM->pgm.s.pgmphysreadcache.aEntries  = 0;   /* flush cache; physical or virtual handlers have changed */
        pVM->pgm.s.pgmphyswritecache.aEntries = 0;   /* flush cache; physical or virtual handlers have changed */
        pVM->pgm.s.fPhysCacheFlushPending = false;
    }
    else
    if (    ASMBitTest(&pVM->pgm.s.pgmphysreadcache.aEntries, (iCacheIndex = ((GCPhys >> PAGE_SHIFT) & PGM_MAX_PHYSCACHE_ENTRIES_MASK)))
        &&  pVM->pgm.s.pgmphysreadcache.Entry[iCacheIndex].GCPhys == PAGE_ADDRESS(GCPhys) /** @todo r=bird: wrong macro. You don't want an uintptr_t! */
#if PGMPHYS_DATASIZE != 1
        &&  PAGE_ADDRESS(GCPhys) == PAGE_ADDRESS(GCPhys + sizeof(PGMPHYS_DATATYPE) - 1) /** (GCPhys & PAGE_OFFSET_MASK) <= PAGE_SIZE - sizeof(PGMPHYS_DATATYPE) */
#endif
       )
    {
        RTGCPHYS off = GCPhys - pVM->pgm.s.pgmphysreadcache.Entry[iCacheIndex].GCPhys;
        return *(PGMPHYS_DATATYPE *)(pVM->pgm.s.pgmphysreadcache.Entry[iCacheIndex].pbHC + off);
    }
#endif /* PGM_PHYSMEMACCESS_CACHING */
    PGMPHYS_DATATYPE val;

    PGMPhysRead(pVM, GCPhys, &val, sizeof(val));
    return val;
}


/**
 * Write to physical memory. (one byte/word/dword)
 *
 * This API respects access handlers and MMIO. Use PGMPhysReadGCPhys() if you
 * want to ignore those.
 *
 * @param   pVM             VM Handle.
 * @param   GCPhys          Physical address to write to.
 * @param   val             What to write.
 */
PGMDECL(void) PGMPHYSFN_WRITENAME(PVM pVM, RTGCPHYS GCPhys, PGMPHYS_DATATYPE val)
{
    uint32_t iCacheIndex;

    Assert(VM_IS_EMT(pVM));

#ifdef PGM_PHYSMEMACCESS_CACHING
    if (pVM->pgm.s.fPhysCacheFlushPending)
    {
        pVM->pgm.s.pgmphysreadcache.aEntries  = 0;   /* flush cache; physical or virtual handlers have changed */
        pVM->pgm.s.pgmphyswritecache.aEntries = 0;   /* flush cache; physical or virtual handlers have changed */
        pVM->pgm.s.fPhysCacheFlushPending = false;
    }
    else
    if (    ASMBitTest(&pVM->pgm.s.pgmphyswritecache.aEntries, (iCacheIndex = ((GCPhys >> PAGE_SHIFT) & PGM_MAX_PHYSCACHE_ENTRIES_MASK)))
        &&  pVM->pgm.s.pgmphyswritecache.Entry[iCacheIndex].GCPhys == PAGE_ADDRESS(GCPhys)
#if PGMPHYS_DATASIZE != 1
        &&  PAGE_ADDRESS(GCPhys) == PAGE_ADDRESS(GCPhys + sizeof(val) - 1)
#endif
       )
    {
        RTGCPHYS off = GCPhys - pVM->pgm.s.pgmphyswritecache.Entry[iCacheIndex].GCPhys;
        *(PGMPHYS_DATATYPE *)(pVM->pgm.s.pgmphyswritecache.Entry[iCacheIndex].pbHC + off) = val;
        return;
    }
#endif /* PGM_PHYSMEMACCESS_CACHING */
    return PGMPhysWrite(pVM, GCPhys, &val, sizeof(val));
}

#undef PGMPHYSFN_READNAME
#undef PGMPHYSFN_WRITENAME
#undef PGMPHYS_DATATYPE
#undef PGMPHYS_DATASIZE

