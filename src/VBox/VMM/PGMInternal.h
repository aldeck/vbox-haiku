/* $Id$ */
/** @file
 * PGM - Internal header file.
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

#ifndef __PGMInternal_h__
#define __PGMInternal_h__

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/err.h>
#include <VBox/stam.h>
#include <VBox/param.h>
#include <VBox/vmm.h>
#include <VBox/mm.h>
#include <VBox/pdm.h>
#include <iprt/avl.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>

#if !defined(IN_PGM_R3) && !defined(IN_PGM_R0) && !defined(IN_PGM_GC)
# error "Not in PGM! This is an internal header!"
#endif


/** @defgroup grp_pgm_int   Internals
 * @ingroup grp_pgm
 * @internal
 * @{
 */


/** @name PGM Compile Time Config
 * @{
 */

/**
 * Solve page is out of sync issues inside Guest Context (in PGMGC.cpp).
 * Comment it if it will break something.
 */
#define PGM_OUT_OF_SYNC_IN_GC

/**
 * Virtualize the dirty bit
 * This also makes a half-hearted attempt at the accessed bit. For full
 * accessed bit virtualization define PGM_SYNC_ACCESSED_BIT.
 */
#define PGM_SYNC_DIRTY_BIT

/**
 * Fully virtualize the accessed bit.
 * @remark This requires SYNC_DIRTY_ACCESSED_BITS to be defined!
 */
#define PGM_SYNC_ACCESSED_BIT

/**
 * Check and skip global PDEs for non-global flushes
 */
#define PGM_SKIP_GLOBAL_PAGEDIRS_ON_NONGLOBAL_FLUSH

/**
 * Sync N pages instead of a whole page table
 */
#define PGM_SYNC_N_PAGES

/**
 * Number of pages to sync during a page fault
 *
 * When PGMPOOL_WITH_GCPHYS_TRACKING is enabled using high values here
 * causes a lot of unnecessary extents and also is slower than taking more \#PFs.
 */
#define PGM_SYNC_NR_PAGES         8

/**
 * Number of PGMPhysRead/Write cache entries (must be <= sizeof(uint64_t))
 */
#define PGM_MAX_PHYSCACHE_ENTRIES 64
#define PGM_MAX_PHYSCACHE_ENTRIES_MASK (PGM_MAX_PHYSCACHE_ENTRIES-1)

/**
 * Enable caching of PGMR3PhysRead/WriteByte/Word/Dword
 */
#define PGM_PHYSMEMACCESS_CACHING

/*
 * Assert Sanity.
 */
#if defined(PGM_SYNC_ACCESSED_BIT) && !defined(PGM_SYNC_DIRTY_BIT)
# error "PGM_SYNC_ACCESSED_BIT requires PGM_SYNC_DIRTY_BIT!"
#endif

/** @def PGMPOOL_WITH_CACHE
 * Enable agressive caching using the page pool.
 *
 * This requires PGMPOOL_WITH_USER_TRACKING and PGMPOOL_WITH_MONITORING.
 */
#define PGMPOOL_WITH_CACHE

/** @def PGMPOOL_WITH_MIXED_PT_CR3
 * When defined, we'll deal with 'uncachable' pages.
 */
#ifdef PGMPOOL_WITH_CACHE
# define PGMPOOL_WITH_MIXED_PT_CR3
#endif

/** @def PGMPOOL_WITH_MONITORING
 * Monitor the guest pages which are shadowed.
 * When this is enabled, PGMPOOL_WITH_CACHE or PGMPOOL_WITH_GCPHYS_TRACKING must
 * be enabled as well.
 * @remark doesn't really work without caching now. (Mixed PT/CR3 change.)
 */
#ifdef PGMPOOL_WITH_CACHE
# define PGMPOOL_WITH_MONITORING
#endif

/** @def PGMPOOL_WITH_GCPHYS_TRACKING
 * Tracking the of shadow pages mapping guest physical pages.
 *
 * This is very expensive, the current cache prototype is trying to figure out
 * whether it will be acceptable with an agressive caching policy.
 */
#if defined(PGMPOOL_WITH_CACHE) || defined(PGMPOOL_WITH_MONITORING)
# define PGMPOOL_WITH_GCPHYS_TRACKING
#endif

/** @def PGMPOOL_WITH_USER_TRACKING
 * Tracking users of shadow pages. This is required for the linking of shadow page
 * tables and physical guest addresses.
 */
#if defined(PGMPOOL_WITH_GCPHYS_TRACKING) || defined(PGMPOOL_WITH_CACHE) || defined(PGMPOOL_WITH_MONITORING)
# define PGMPOOL_WITH_USER_TRACKING
#endif

/** @def PGMPOOL_CFG_MAX_GROW
 * The maximum number of pages to add to the pool in one go.
 */
#define PGMPOOL_CFG_MAX_GROW    (_256K >> PAGE_SHIFT)

/** @def VBOX_STRICT_PGM_HANDLER_VIRTUAL
 * Enables some extra assertions for virtual handlers (mainly phys2virt related).
 */
#ifdef VBOX_STRICT
# define VBOX_STRICT_PGM_HANDLER_VIRTUAL
#endif
/** @} */


/** @name PDPTR and PML4 flags.
 * These are placed in the three bits available for system programs in
 * the PDPTR and PML4 entries.
 * @{ */
/** The entry is a permanent one and it's must always be present.
 * Never free such an entry. */
#define PGM_PLXFLAGS_PERMANENT      BIT64(10)
/** @} */

/** @name Page directory flags.
 * These are placed in the three bits available for system programs in
 * the page directory entries.
 * @{ */
/** Mapping (hypervisor allocated pagetable). */
#define PGM_PDFLAGS_MAPPING         BIT64(10)
/** Made read-only to facilitate dirty bit tracking. */
#define PGM_PDFLAGS_TRACK_DIRTY     BIT64(11)
/** @} */

/** @name Page flags.
 * These are placed in the three bits available for system programs in
 * the page entries.
 * @{ */
/** Made read-only to facilitate dirty bit tracking. */
#define PGM_PTFLAGS_TRACK_DIRTY     BIT64(9)

#ifndef PGM_PTFLAGS_CSAM_VALIDATED
/** Scanned and approved by CSAM (tm).
 * NOTE: Must be identical to the one defined in CSAMInternal.h!!
 * @todo Move PGM_PTFLAGS_* and PGM_PDFLAGS_* to VBox/pgm.h. */
#define PGM_PTFLAGS_CSAM_VALIDATED  BIT64(11)
#endif
/** @} */

/** @name Defines used to indicate the shadow and guest paging in the templates.
 * @{ */
#define PGM_TYPE_REAL       1
#define PGM_TYPE_PROT       2
#define PGM_TYPE_32BIT      3
#define PGM_TYPE_PAE        4
#define PGM_TYPE_AMD64      5
/** @} */

/** @name Defines used to check if the guest is using paging
 * @{ */
#define PGM_WITH_PAGING(a)  (a == PGM_TYPE_32BIT || a == PGM_TYPE_PAE || a == PGM_TYPE_AMD64)
/** @} */

/** @def PGM_HCPHYS_2_PTR
 * Maps a HC physical page pool address to a virtual address.
 *
 * @returns VBox status code.
 * @param   pVM     The VM handle.
 * @param   HCPhys  The HC physical address to map to a virtual one.
 * @param   ppv     Where to store the virtual address. No need to cast this.
 *
 * @remark  In GC this uses PGMGCDynMapHCPage(), so it will consume of the
 *          small page window employeed by that function. Be careful.
 * @remark  There is no need to assert on the result.
 */
#ifdef IN_GC
# define PGM_HCPHYS_2_PTR(pVM, HCPhys, ppv) PGMGCDynMapHCPage(pVM, HCPhys, (void **)(ppv))
#else
# define PGM_HCPHYS_2_PTR(pVM, HCPhys, ppv) MMPagePhys2PageEx(pVM, HCPhys, (void **)(ppv))
#endif

/** @def PGM_GCPHYS_2_PTR
 * Maps a GC physical page address to a virtual address.
 *
 * @returns VBox status code.
 * @param   pVM     The VM handle.
 * @param   GCPhys  The GC physical address to map to a virtual one.
 * @param   ppv     Where to store the virtual address. No need to cast this.
 *
 * @remark  In GC this uses PGMGCDynMapGCPage(), so it will consume of the
 *          small page window employeed by that function. Be careful.
 * @remark  There is no need to assert on the result.
 */
#ifdef IN_GC
# define PGM_GCPHYS_2_PTR(pVM, GCPhys, ppv) PGMGCDynMapGCPage(pVM, GCPhys, (void **)(ppv))
#else
# define PGM_GCPHYS_2_PTR(pVM, GCPhys, ppv) PGMPhysGCPhys2HCPtr(pVM, GCPhys, 1 /* one page only */, (void **)(ppv)) /** @todo this isn't asserting, use PGMRamGCPhys2HCPtr! */
#endif

/** @def PGM_GCPHYS_2_PTR_EX
 * Maps a unaligned GC physical page address to a virtual address.
 *
 * @returns VBox status code.
 * @param   pVM     The VM handle.
 * @param   GCPhys  The GC physical address to map to a virtual one.
 * @param   ppv     Where to store the virtual address. No need to cast this.
 *
 * @remark  In GC this uses PGMGCDynMapGCPage(), so it will consume of the
 *          small page window employeed by that function. Be careful.
 * @remark  There is no need to assert on the result.
 */
#ifdef IN_GC
# define PGM_GCPHYS_2_PTR_EX(pVM, GCPhys, ppv) PGMGCDynMapGCPageEx(pVM, GCPhys, (void **)(ppv))
#else
# define PGM_GCPHYS_2_PTR_EX(pVM, GCPhys, ppv) PGMPhysGCPhys2HCPtr(pVM, GCPhys, 1 /* one page only */, (void **)(ppv)) /** @todo this isn't asserting, use PGMRamGCPhys2HCPtr! */
#endif

/** @def PGM_INVL_PG
 * Invalidates a page when in GC does nothing in HC.
 *
 * @param   GCVirt      The virtual address of the page to invalidate.
 */
#ifdef IN_GC
# define PGM_INVL_PG(GCVirt)        ASMInvalidatePage((void *)(GCVirt))
#else
# define PGM_INVL_PG(GCVirt)        ((void)0)
#endif

/** @def PGM_INVL_BIG_PG
 * Invalidates a 4MB page directory entry when in GC does nothing in HC.
 *
 * @param   GCVirt      The virtual address within the page directory to invalidate.
 */
#ifdef IN_GC
# define PGM_INVL_BIG_PG(GCVirt)    ASMReloadCR3()
#else
# define PGM_INVL_BIG_PG(GCVirt)    ((void)0)
#endif

/** @def PGM_INVL_GUEST_TLBS()
 * Invalidates all guest TLBs.
 */
#ifdef IN_GC
# define PGM_INVL_GUEST_TLBS()      ASMReloadCR3()
#else
# define PGM_INVL_GUEST_TLBS()      ((void)0)
#endif


/**
 * Structure for tracking GC Mappings.
 *
 * This structure is used by linked list in both GC and HC.
 */
typedef struct PGMMAPPING
{
    /** Pointer to next entry. */
    R3PTRTYPE(struct PGMMAPPING *)  pNextR3;
    /** Pointer to next entry. */
    GCPTRTYPE(struct PGMMAPPING *)  pNextGC;
    /** Pointer to next entry. */
    R0PTRTYPE(struct PGMMAPPING *)  pNextR0;
    /** Start Virtual address. */
    RTGCUINTPTR                     GCPtr;
    /** Last Virtual address (inclusive). */
    RTGCUINTPTR                     GCPtrLast;
    /** Range size (bytes). */
    RTGCUINTPTR                     cb;
    /** Pointer to relocation callback function. */
    R3PTRTYPE(PFNPGMRELOCATE)       pfnRelocate;
    /** User argument to the callback. */
    R3PTRTYPE(void *)               pvUser;
    /** Mapping description / name. For easing debugging. */
    R3PTRTYPE(const char *)         pszDesc;
    /** Number of page tables. */
    RTUINT                          cPTs;
#if HC_ARCH_BITS != GC_ARCH_BITS
    RTUINT                          uPadding0; /**< Alignment padding. */
#endif
    /** Array of page table mapping data. Each entry
     * describes one page table. The array can be longer
     * than the declared length.
     */
    struct
    {
        /** The HC physical address of the page table. */
        RTHCPHYS                HCPhysPT;
        /** The HC physical address of the first PAE page table. */
        RTHCPHYS                HCPhysPaePT0;
        /** The HC physical address of the second PAE page table. */
        RTHCPHYS                HCPhysPaePT1;
        /** The HC virtual address of the 32-bit page table. */
        R3PTRTYPE(PVBOXPT)      pPTR3;
        /** The HC virtual address of the two PAE page table. (i.e 1024 entries instead of 512) */
        R3PTRTYPE(PX86PTPAE)    paPaePTsR3;
        /** The GC virtual address of the 32-bit page table. */
        GCPTRTYPE(PVBOXPT)      pPTGC;
        /** The GC virtual address of the two PAE page table. */
        GCPTRTYPE(PX86PTPAE)    paPaePTsGC;
        /** The GC virtual address of the 32-bit page table. */
        R0PTRTYPE(PVBOXPT)      pPTR0;
        /** The GC virtual address of the two PAE page table. */
        R0PTRTYPE(PX86PTPAE)    paPaePTsR0;
    } aPTs[1];
} PGMMAPPING;
/** Pointer to structure for tracking GC Mappings. */
typedef struct PGMMAPPING *PPGMMAPPING;


/**
 * Physical page access handler structure.
 *
 * This is used to keep track of physical address ranges
 * which are being monitored in some kind of way.
 */
typedef struct PGMPHYSHANDLER
{
    AVLROGCPHYSNODECORE                 Core;
    /** Alignment padding. */
    uint32_t                            u32Padding;
    /** Access type. */
    PGMPHYSHANDLERTYPE                  enmType;
    /** Number of pages to update. */
    uint32_t                            cPages;
    /** Pointer to R3 callback function. */
    R3PTRTYPE(PFNPGMR3PHYSHANDLER)      pfnHandlerR3;
    /** User argument for R3 handlers. */
    R3PTRTYPE(void *)                   pvUserR3;
    /** Pointer to R0 callback function. */
    R0PTRTYPE(PFNPGMR0PHYSHANDLER)      pfnHandlerR0;
    /** User argument for R0 handlers. */
    R0PTRTYPE(void *)                   pvUserR0;
    /** Pointer to GC callback function. */
    GCPTRTYPE(PFNPGMGCPHYSHANDLER)      pfnHandlerGC;
    /** User argument for GC handlers. */
    GCPTRTYPE(void *)                   pvUserGC;
    /** Description / Name. For easing debugging. */
    R3PTRTYPE(const char *)             pszDesc;
#ifdef VBOX_WITH_STATISTICS
    /** Profiling of this handler. */
    STAMPROFILE                         Stat;
#endif
} PGMPHYSHANDLER;
/** Pointer to a physical page access handler structure. */
typedef PGMPHYSHANDLER *PPGMPHYSHANDLER;


/**
 * Cache node for the physical addresses covered by a virtual handler.
 */
typedef struct PGMPHYS2VIRTHANDLER
{
    /** Core node for the tree based on physical ranges. */
    AVLROGCPHYSNODECORE                 Core;
    /** Offset from this struct to the PGMVIRTHANDLER structure. */
    RTGCINTPTR                          offVirtHandler;
    /** Offset of the next alias relativer to this one.
     * Bit 0 is used for indicating whether we're in the tree.
     * Bit 1 is used for indicating that we're the head node.
     */
    int32_t                             offNextAlias;
} PGMPHYS2VIRTHANDLER;
/** Pointer to a phys to virtual handler structure. */
typedef PGMPHYS2VIRTHANDLER *PPGMPHYS2VIRTHANDLER;

/** The bit in PGMPHYS2VIRTHANDLER::offNextAlias used to indicate that the
 * node is in the tree. */
#define PGMPHYS2VIRTHANDLER_IN_TREE     BIT(0)
/** The bit in PGMPHYS2VIRTHANDLER::offNextAlias used to indicate that the
 * node is in the head of an alias chain.
 * The PGMPHYS2VIRTHANDLER_IN_TREE is always set if this bit is set. */
#define PGMPHYS2VIRTHANDLER_IS_HEAD     BIT(1)
/** The mask to apply to PGMPHYS2VIRTHANDLER::offNextAlias to get the offset. */
#define PGMPHYS2VIRTHANDLER_OFF_MASK    (~(int32_t)3)


/**
 * Virtual page access handler structure.
 *
 * This is used to keep track of virtual address ranges
 * which are being monitored in some kind of way.
 */
typedef struct PGMVIRTHANDLER
{
    /** Core node for the tree based on virtual ranges. */
    AVLROGCPTRNODECORE                  Core;
    /** Number of cache pages. */
    uint32_t                            u32Padding;
    /** Access type. */
    PGMVIRTHANDLERTYPE                  enmType;
    /** Number of cache pages. */
    uint32_t                            cPages;

/** @todo The next two members are redundant. It adds some readability though. */
    /** Start of the range. */
    RTGCPTR                             GCPtr;
    /** End of the range (exclusive). */
    RTGCPTR                             GCPtrLast;
    /** Size of the range (in bytes). */
    RTGCUINTPTR                         cb;
    /** Pointer to the GC callback function. */
    GCPTRTYPE(PFNPGMGCVIRTHANDLER)      pfnHandlerGC;
    /** Pointer to the HC callback function for invalidation. */
    HCPTRTYPE(PFNPGMHCVIRTINVALIDATE)   pfnInvalidateHC;
    /** Pointer to the HC callback function. */
    HCPTRTYPE(PFNPGMHCVIRTHANDLER)      pfnHandlerHC;
    /** Description / Name. For easing debugging. */
    HCPTRTYPE(const char *)             pszDesc;
#ifdef VBOX_WITH_STATISTICS
    /** Profiling of this handler. */
    STAMPROFILE                         Stat;
#endif
    /** Array of cached physical addresses for the monitored ranged.  */
    PGMPHYS2VIRTHANDLER                 aPhysToVirt[HC_ARCH_BITS == 32 ? 1 : 2];
} PGMVIRTHANDLER;
/** Pointer to a virtual page access handler structure. */
typedef PGMVIRTHANDLER *PPGMVIRTHANDLER;


/**
 * Ram range for GC Phys to HC Phys conversion.
 *
 * Can be used for HC Virt to GC Phys and HC Virt to HC Phys
 * conversions too, but we'll let MM handle that for now.
 *
 * This structure is used by linked lists in both GC and HC.
 */
typedef struct PGMRAMRANGE
{
    /** Pointer to the next RAM range - for HC. */
    HCPTRTYPE(struct PGMRAMRANGE *)     pNextHC;
    /** Pointer to the next RAM range - for GC. */
    GCPTRTYPE(struct PGMRAMRANGE *)     pNextGC;
    /** Start of the range. Page aligned. */
    RTGCPHYS                            GCPhys;
    /** Last address in the range (inclusive). Page aligned (-1). */
    RTGCPHYS                            GCPhysLast;
    /** Size of the range. (Page aligned of course). */
    RTGCPHYS                            cb;
    /** MM_RAM_* flags */
    uint32_t                            fFlags;

    /** HC virtual lookup ranges for chunks. Currently only used with MM_RAM_FLAGS_DYNAMIC_ALLOC ranges. */
    GCPTRTYPE(PRTHCPTR)                 pavHCChunkGC;
    /** HC virtual lookup ranges for chunks. Currently only used with MM_RAM_FLAGS_DYNAMIC_ALLOC ranges. */
    HCPTRTYPE(PRTHCPTR)                 pavHCChunkHC;

    /** Start of the HC mapping of the range.
     * For pure MMIO and dynamically allocated ranges this is NULL, while for all ranges this is a valid pointer. */
    HCPTRTYPE(void *)                   pvHC;

    /** Array of the flags and HC physical addresses corresponding to the range.
     * The index is the page number in the range. The size is cb >> PAGE_SHIFT.
     *
     * The 12 lower bits of the physical address are flags and must be masked
     * off to get the correct physical address.
     *
     * For pure MMIO ranges only the flags are valid.
     */
    RTHCPHYS                            aHCPhys[1];
} PGMRAMRANGE;
/** Pointer to Ram range for GC Phys to HC Phys conversion. */
typedef PGMRAMRANGE *PPGMRAMRANGE;

/** Return hc ptr corresponding to the ram range and physical offset */
#define PGMRAMRANGE_GETHCPTR(pRam, off) \
    (pRam->fFlags & MM_RAM_FLAGS_DYNAMIC_ALLOC) ? (RTHCPTR)((RTHCUINTPTR)CTXSUFF(pRam->pavHCChunk)[(off >> PGM_DYNAMIC_CHUNK_SHIFT)] + (off & PGM_DYNAMIC_CHUNK_OFFSET_MASK))  \
                                                : (RTHCPTR)((RTHCUINTPTR)pRam->pvHC + off);

/** @todo r=bird: fix typename. */
/**
 * PGMPhysRead/Write cache entry
 */
typedef struct PGMPHYSCACHE_ENTRY
{
    /** HC pointer to physical page */
    R3PTRTYPE(uint8_t *)            pbHC;
    /** GC Physical address for cache entry */
    RTGCPHYS                        GCPhys;
#if HC_ARCH_BITS == 64 && GC_ARCH_BITS == 32
    RTGCPHYS                        u32Padding0; /**< alignment padding. */
#endif
} PGMPHYSCACHE_ENTRY;

/**
 * PGMPhysRead/Write cache to reduce REM memory access overhead
 */
typedef struct PGMPHYSCACHE
{
    /** Bitmap of valid cache entries */
    uint64_t                        aEntries;
    /** Cache entries */
    PGMPHYSCACHE_ENTRY              Entry[PGM_MAX_PHYSCACHE_ENTRIES];
} PGMPHYSCACHE;


/** @name PGM Pool Indexes.
 * Aka. the unique shadow page identifier.
 * @{ */
/** NIL page pool IDX. */
#define NIL_PGMPOOL_IDX         0
/** The first normal index. */
#define PGMPOOL_IDX_FIRST_SPECIAL 1
/** Page directory (32-bit root). */
#define PGMPOOL_IDX_PD          1
/** The extended PAE page directory (2048 entries, works as root currently). */
#define PGMPOOL_IDX_PAE_PD      2
/** Page Directory Pointer Table (PAE root, not currently used). */
#define PGMPOOL_IDX_PDPTR       3
/** Page Map Level-4 (64-bit root). */
#define PGMPOOL_IDX_PML4        4
/** The first normal index. */
#define PGMPOOL_IDX_FIRST       5
/** The last valid index. (inclusive, 14 bits) */
#define PGMPOOL_IDX_LAST        0x3fff
/** @} */

/** The NIL index for the parent chain. */
#define NIL_PGMPOOL_USER_INDEX  ((uint16_t)0xffff)

/**
 * Node in the chain linking a shadowed page to it's parent (user).
 */
#pragma pack(1)
typedef struct PGMPOOLUSER
{
    /** The index to the next item in the chain. NIL_PGMPOOL_USER_INDEX is no next. */
    uint16_t            iNext;
    /** The user page index. */
    uint16_t            iUser;
    /** Index into the user table. */
    uint16_t            iUserTable;
} PGMPOOLUSER, *PPGMPOOLUSER;
typedef const PGMPOOLUSER *PCPGMPOOLUSER;
#pragma pack()


/** The NIL index for the phys ext chain. */
#define NIL_PGMPOOL_PHYSEXT_INDEX  ((uint16_t)0xffff)

/**
 * Node in the chain of physical cross reference extents.
 */
#pragma pack(1)
typedef struct PGMPOOLPHYSEXT
{
    /** The index to the next item in the chain. NIL_PGMPOOL_PHYSEXT_INDEX is no next. */
    uint16_t            iNext;
    /** The user page index. */
    uint16_t            aidx[3];
} PGMPOOLPHYSEXT, *PPGMPOOLPHYSEXT;
typedef const PGMPOOLPHYSEXT *PCPGMPOOLPHYSEXT;
#pragma pack()


/**
 * The kind of page that's being shadowed.
 */
typedef enum PGMPOOLKIND
{
    /** The virtual invalid 0 entry. */
    PGMPOOLKIND_INVALID = 0,
    /** The entry is free (=unused). */
    PGMPOOLKIND_FREE,

    /** Shw: 32-bit page table; Gst: no paging  */
    PGMPOOLKIND_32BIT_PT_FOR_PHYS,
    /** Shw: 32-bit page table; Gst: 32-bit page table.  */
    PGMPOOLKIND_32BIT_PT_FOR_32BIT_PT,
    /** Shw: 32-bit page table; Gst: 4MB page.  */
    PGMPOOLKIND_32BIT_PT_FOR_32BIT_4MB,
    /** Shw: PAE page table; Gst: no paging  */
    PGMPOOLKIND_PAE_PT_FOR_PHYS,
    /** Shw: PAE page table;    Gst: 32-bit page table. */
    PGMPOOLKIND_PAE_PT_FOR_32BIT_PT,
    /** Shw: PAE page table;    Gst: Half of a 4MB page.  */
    PGMPOOLKIND_PAE_PT_FOR_32BIT_4MB,
    /** Shw: PAE page table;    Gst: PAE page table. */
    PGMPOOLKIND_PAE_PT_FOR_PAE_PT,
    /** Shw: PAE page table;    Gst: 2MB page.  */
    PGMPOOLKIND_PAE_PT_FOR_PAE_2MB,

    /** Shw: PAE page directory;    Gst: 32-bit page directory. */
    PGMPOOLKIND_PAE_PD_FOR_32BIT_PD,
    /** Shw: PAE page directory;    Gst: PAE page directory. */
    PGMPOOLKIND_PAE_PD_FOR_PAE_PD,

    /** Shw: 64-bit page directory pointer table;   Gst: 64-bit page directory pointer table. */
    PGMPOOLKIND_64BIT_PDPTR_FOR_64BIT_PDPTR,

    /** Shw: Root 32-bit page directory. */
    PGMPOOLKIND_ROOT_32BIT_PD,
    /** Shw: Root PAE page directory */
    PGMPOOLKIND_ROOT_PAE_PD,
    /** Shw: Root PAE page directory pointer table (legacy, 4 entries). */
    PGMPOOLKIND_ROOT_PDPTR,
    /** Shw: Root page map level-4 table. */
    PGMPOOLKIND_ROOT_PML4,

    /** The last valid entry. */
    PGMPOOLKIND_LAST = PGMPOOLKIND_ROOT_PML4
} PGMPOOLKIND;


/**
 * The tracking data for a page in the pool.
 */
typedef struct PGMPOOLPAGE
{
    /** AVL node code with the (HC) physical address of this page. */
    AVLOHCPHYSNODECORE  Core;
    /** Pointer to the HC mapping of the page. */
    HCPTRTYPE(void *)   pvPageHC;
    /** The guest physical address. */
    RTGCPHYS            GCPhys;
    /** The kind of page we're shadowing. (This is really a PGMPOOLKIND enum.) */
    uint8_t             enmKind;
    uint8_t             bPadding;
    /** The index of this page. */
    uint16_t            idx;
    /** The next entry in the list this page currently resides in.
     * It's either in the free list or in the GCPhys hash. */
    uint16_t            iNext;
#ifdef PGMPOOL_WITH_USER_TRACKING
    /** Head of the user chain. NIL_PGMPOOL_USER_INDEX if not currently in use. */
    uint16_t            iUserHead;
    /** The number of present entries. */
    uint16_t            cPresent;
    /** The first entry in the table which is present. */
    uint16_t            iFirstPresent;
#endif
#ifdef PGMPOOL_WITH_MONITORING
    /** The number of modifications to the monitored page. */
    uint16_t            cModifications;
    /** The next modified page. NIL_PGMPOOL_IDX if tail. */
    uint16_t            iModifiedNext;
    /** The previous modified page. NIL_PGMPOOL_IDX if head. */
    uint16_t            iModifiedPrev;
    /** The next page sharing access handler. NIL_PGMPOOL_IDX if tail. */
    uint16_t            iMonitoredNext;
    /** The previous page sharing access handler. NIL_PGMPOOL_IDX if head. */
    uint16_t            iMonitoredPrev;
#endif
#ifdef PGMPOOL_WITH_CACHE
    /** The next page in the age list. */
    uint16_t            iAgeNext;
    /** The previous page in the age list. */
    uint16_t            iAgePrev;
/** @todo add more from PGMCache.h when merging with it. */
#endif /* PGMPOOL_WITH_CACHE */
    /** Used to indicate that the page is zeroed. */
    bool                fZeroed;
    /** Used to indicate that a PT has non-global entries. */
    bool                fSeenNonGlobal;
    /** Used to indicate that we're monitoring writes to the guest page. */
    bool                fMonitored;
    /** Used to indicate that the page is in the cache (e.g. in the GCPhys hash).
     * (All pages are in the age list.) */
    bool                fCached;
    /** This is used by the R3 access handlers when invoked by an async thread.
     * It's a hack required because of REMR3NotifyHandlerPhysicalDeregister. */
    bool volatile       fReusedFlushPending;
    /** Used to indicate that the guest is mapping the page is also used as a CR3.
     * In these cases the access handler acts differently and will check
     * for mapping conflicts like the normal CR3 handler.
     * @todo When we change the CR3 shadowing to use pool pages, this flag can be
     *       replaced by a list of pages which share access handler.
     */
    bool                fCR3Mix;
#if HC_ARCH_BITS == 64 || GC_ARCH_BITS == 64
    bool                Alignment[4];   /**< Align the structure size on a 64-bit boundrary. */
#endif 
} PGMPOOLPAGE, *PPGMPOOLPAGE, **PPPGMPOOLPAGE;


#ifdef PGMPOOL_WITH_CACHE
/** The hash table size. */
# define PGMPOOL_HASH_SIZE      0x40
/** The hash function. */
# define PGMPOOL_HASH(GCPhys)   ( ((GCPhys) >> PAGE_SHIFT) & (PGMPOOL_HASH_SIZE - 1) )
#endif


/**
 * The shadow page pool instance data.
 *
 * It's all one big allocation made at init time, except for the
 * pages that is. The user nodes follows immediatly after the
 * page structures.
 */
typedef struct PGMPOOL
{
    /** The VM handle - HC Ptr. */
    HCPTRTYPE(PVM)  pVMHC;
    /** The VM handle - GC Ptr. */
    GCPTRTYPE(PVM)  pVMGC;
    /** The max pool size. This includes the special IDs.  */
    uint16_t        cMaxPages;
    /** The current pool size. */
    uint16_t        cCurPages;
    /** The head of the free page list. */
    uint16_t        iFreeHead;
    /* Padding. */
    uint16_t        u16Padding;
#ifdef PGMPOOL_WITH_USER_TRACKING
    /** Head of the chain of free user nodes. */
    uint16_t        iUserFreeHead;
    /** The number of user nodes we've allocated. */
    uint16_t        cMaxUsers;
    /** The number of present page table entries in the entire pool. */
    uint32_t        cPresent;
    /** Pointer to the array of user nodes - GC pointer. */
    GCPTRTYPE(PPGMPOOLUSER) paUsersGC;
    /** Pointer to the array of user nodes - HC pointer. */
    HCPTRTYPE(PPGMPOOLUSER) paUsersHC;
#endif /* PGMPOOL_WITH_USER_TRACKING */
#ifdef PGMPOOL_WITH_GCPHYS_TRACKING
    /** Head of the chain of free phys ext nodes. */
    uint16_t        iPhysExtFreeHead;
    /** The number of user nodes we've allocated. */
    uint16_t        cMaxPhysExts;
    /** Pointer to the array of physical xref extent - GC pointer. */
    GCPTRTYPE(PPGMPOOLPHYSEXT) paPhysExtsGC;
    /** Pointer to the array of physical xref extent nodes - HC pointer. */
    HCPTRTYPE(PPGMPOOLPHYSEXT) paPhysExtsHC;
#endif /* PGMPOOL_WITH_GCPHYS_TRACKING */
#ifdef PGMPOOL_WITH_CACHE
    /** Hash table for GCPhys addresses. */
    uint16_t        aiHash[PGMPOOL_HASH_SIZE];
    /** The head of the age list. */
    uint16_t        iAgeHead;
    /** The tail of the age list. */
    uint16_t        iAgeTail;
    /** Set if the cache is enabled. */
    bool            fCacheEnabled;
#endif /* PGMPOOL_WITH_CACHE */
#ifdef PGMPOOL_WITH_MONITORING
    /** Head of the list of modified pages. */
    uint16_t        iModifiedHead;
    /** The current number of modified pages. */
    uint16_t        cModifiedPages;
    /** Access handler, GC. */
    GCPTRTYPE(PFNPGMGCPHYSHANDLER)  pfnAccessHandlerGC;
    /** Access handler, R0. */
    R0PTRTYPE(PFNPGMR0PHYSHANDLER)  pfnAccessHandlerR0;
    /** Access handler, R3. */
    R3PTRTYPE(PFNPGMR3PHYSHANDLER)  pfnAccessHandlerR3;
    /** The access handler description (HC ptr). */
    R3PTRTYPE(const char *)         pszAccessHandler;
#endif /* PGMPOOL_WITH_MONITORING */
    /** The number of pages currently in use. */
    uint16_t        cUsedPages;
#ifdef VBOX_WITH_STATISTICS
    /** The high wather mark for cUsedPages. */
    uint16_t        cUsedPagesHigh;
    uint32_t        Alignment1;         /**< Align the next member on a 64-bit boundrary. */
    /** Profiling pgmPoolAlloc(). */
    STAMPROFILEADV  StatAlloc;
    /** Profiling pgmPoolClearAll(). */
    STAMPROFILE     StatClearAll;
    /** Profiling pgmPoolFlushAllInt(). */
    STAMPROFILE     StatFlushAllInt;
    /** Profiling pgmPoolFlushPage(). */
    STAMPROFILE     StatFlushPage;
    /** Profiling pgmPoolFree(). */
    STAMPROFILE     StatFree;
    /** Profiling time spent zeroing pages. */
    STAMPROFILE     StatZeroPage;
# ifdef PGMPOOL_WITH_USER_TRACKING
    /** Profiling of pgmPoolTrackDeref. */
    STAMPROFILE     StatTrackDeref;
    /** Profiling pgmTrackFlushGCPhysPT. */
    STAMPROFILE     StatTrackFlushGCPhysPT;
    /** Profiling pgmTrackFlushGCPhysPTs. */
    STAMPROFILE     StatTrackFlushGCPhysPTs;
    /** Profiling pgmTrackFlushGCPhysPTsSlow. */
    STAMPROFILE     StatTrackFlushGCPhysPTsSlow;
    /** Number of times we've been out of user records. */
    STAMCOUNTER     StatTrackFreeUpOneUser;
# endif
# ifdef PGMPOOL_WITH_GCPHYS_TRACKING
    /** Profiling deref activity related tracking GC physical pages. */
    STAMPROFILE     StatTrackDerefGCPhys;
    /** Number of linear searches for a HCPhys in the ram ranges. */
    STAMCOUNTER     StatTrackLinearRamSearches;
    /** The number of failing pgmPoolTrackPhysExtAlloc calls. */
    STAMCOUNTER     StamTrackPhysExtAllocFailures;
# endif
# ifdef PGMPOOL_WITH_MONITORING
    /** Profiling the GC PT access handler. */
    STAMPROFILE     StatMonitorGC;
    /** Times we've failed interpreting the instruction. */
    STAMCOUNTER     StatMonitorGCEmulateInstr;
    /** Profiling the pgmPoolFlushPage calls made from the GC PT access handler. */
    STAMPROFILE     StatMonitorGCFlushPage;
    /** Times we've detected fork(). */
    STAMCOUNTER     StatMonitorGCFork;
    /** Profiling the GC access we've handled (except REP STOSD). */
    STAMPROFILE     StatMonitorGCHandled;
    /** Times we've failed interpreting a patch code instruction. */
    STAMCOUNTER     StatMonitorGCIntrFailPatch1;
    /** Times we've failed interpreting a patch code instruction during flushing. */
    STAMCOUNTER     StatMonitorGCIntrFailPatch2;
    /** The number of times we've seen rep prefixes we can't handle. */
    STAMCOUNTER     StatMonitorGCRepPrefix;
    /** Profiling the REP STOSD cases we've handled. */
    STAMPROFILE     StatMonitorGCRepStosd;

    /** Profiling the HC PT access handler. */
    STAMPROFILE     StatMonitorHC;
    /** Times we've failed interpreting the instruction. */
    STAMCOUNTER     StatMonitorHCEmulateInstr;
    /** Profiling the pgmPoolFlushPage calls made from the HC PT access handler. */
    STAMPROFILE     StatMonitorHCFlushPage;
    /** Times we've detected fork(). */
    STAMCOUNTER     StatMonitorHCFork;
    /** Profiling the HC access we've handled (except REP STOSD). */
    STAMPROFILE     StatMonitorHCHandled;
    /** The number of times we've seen rep prefixes we can't handle. */
    STAMCOUNTER     StatMonitorHCRepPrefix;
    /** Profiling the REP STOSD cases we've handled. */
    STAMPROFILE     StatMonitorHCRepStosd;
    /** The number of times we're called in an async thread an need to flush. */
    STAMCOUNTER     StatMonitorHCAsync;
    /** The high wather mark for cModifiedPages. */
    uint16_t        cModifiedPagesHigh;
    uint16_t        Alignment2[3];      /**< Align the next member on a 64-bit boundrary. */
# endif
# ifdef PGMPOOL_WITH_CACHE
    /** The number of cache hits. */
    STAMCOUNTER     StatCacheHits;
    /** The number of cache misses. */
    STAMCOUNTER     StatCacheMisses;
    /** The number of times we've got a conflict of 'kind' in the cache. */
    STAMCOUNTER     StatCacheKindMismatches;
    /** Number of times we've been out of pages. */
    STAMCOUNTER     StatCacheFreeUpOne;
    /** The number of cacheable allocations. */
    STAMCOUNTER     StatCacheCacheable;
    /** The number of uncacheable allocations. */
    STAMCOUNTER     StatCacheUncacheable;
# endif
#elif HC_ARCH_BITS == 64 && GC_ARCH_BITS == 32
    uint32_t        Alignment1;         /**< Align the next member on a 64-bit boundrary. */
#endif
    /** The AVL tree for looking up a page by its HC physical address. */
    AVLOHCPHYSTREE  HCPhysTree;
    uint32_t        Alignment3;         /**< Align the next member on a 64-bit boundrary. */
    /** Array of pages. (cMaxPages in length)
     * The Id is the index into thist array.
     */
    PGMPOOLPAGE     aPages[PGMPOOL_IDX_FIRST];
} PGMPOOL, *PPGMPOOL, **PPPGMPOOL;


/** @def PGMPOOL_PAGE_2_PTR
 * Maps a pool page pool into the current context.
 *
 * @returns VBox status code.
 * @param   pVM     The VM handle.
 * @param   pPage   The pool page.
 *
 * @remark  In HC this uses PGMGCDynMapHCPage(), so it will consume of the
 *          small page window employeed by that function. Be careful.
 * @remark  There is no need to assert on the result.
 */
#ifdef IN_GC
# define PGMPOOL_PAGE_2_PTR(pVM, pPage)    pgmGCPoolMapPage((pVM), (pPage))
#else
# define PGMPOOL_PAGE_2_PTR(pVM, pPage)    ((pPage)->pvPageHC)
#endif


/**
 * Trees are using self relative offsets as pointers.
 * So, all its data, including the root pointer, must be in the heap for HC and GC
 * to have the same layout.
 */
typedef struct PGMTREES
{
    /** Physical access handlers (AVL range+offsetptr tree). */
    AVLROGCPHYSTREE                 PhysHandlers;
    /** Virtual access handlers (AVL range + GC ptr tree). */
    AVLROGCPTRTREE                  VirtHandlers;
    /** Virtual access handlers (Phys range AVL range + offsetptr tree). */
    AVLROGCPHYSTREE                 PhysToVirtHandlers;
    uint32_t auPadding[1];
} PGMTREES;
/** Pointer to PGM trees. */
typedef PGMTREES *PPGMTREES;


/** @name Paging mode macros
 * @{ */
#ifdef IN_GC
# define PGM_CTX(a,b)                   a##GC##b
# define PGM_CTX_STR(a,b)               a "GC" b
# define PGM_CTX_DECL(type)             PGMGCDECL(type)
#else
# ifdef IN_RING3
#  define PGM_CTX(a,b)                   a##R3##b
#  define PGM_CTX_STR(a,b)               a "R3" b
#  define PGM_CTX_DECL(type)             DECLCALLBACK(type)
# else
#  define PGM_CTX(a,b)                   a##R0##b
#  define PGM_CTX_STR(a,b)               a "R0" b
#  define PGM_CTX_DECL(type)             PGMDECL(type)
# endif
#endif

#define PGM_GST_NAME_REAL(name)         PGM_CTX(pgm,GstReal##name)
#define PGM_GST_NAME_GC_REAL_STR(name)  "pgmGCGstReal" #name
#define PGM_GST_NAME_R0_REAL_STR(name)  "pgmR0GstReal" #name
#define PGM_GST_NAME_PROT(name)         PGM_CTX(pgm,GstProt##name)
#define PGM_GST_NAME_GC_PROT_STR(name)  "pgmGCGstProt" #name
#define PGM_GST_NAME_R0_PROT_STR(name)  "pgmR0GstProt" #name
#define PGM_GST_NAME_32BIT(name)        PGM_CTX(pgm,Gst32Bit##name)
#define PGM_GST_NAME_GC_32BIT_STR(name) "pgmGCGst32Bit" #name
#define PGM_GST_NAME_R0_32BIT_STR(name) "pgmR0Gst32Bit" #name
#define PGM_GST_NAME_PAE(name)          PGM_CTX(pgm,GstPAE##name)
#define PGM_GST_NAME_GC_PAE_STR(name)   "pgmGCGstPAE" #name
#define PGM_GST_NAME_R0_PAE_STR(name)   "pgmR0GstPAE" #name
#define PGM_GST_NAME_AMD64(name)        PGM_CTX(pgm,GstAMD64##name)
#define PGM_GST_NAME_GC_AMD64_STR(name) "pgmGCGstAMD64" #name
#define PGM_GST_NAME_R0_AMD64_STR(name) "pgmR0GstAMD64" #name
#define PGM_GST_PFN(name, pVM)          ((pVM)->pgm.s.PGM_CTX(pfn,Gst##name))
#define PGM_GST_DECL(type, name)        PGM_CTX_DECL(type) PGM_GST_NAME(name)

#define PGM_SHW_NAME_32BIT(name)        PGM_CTX(pgm,Shw32Bit##name)
#define PGM_SHW_NAME_GC_32BIT_STR(name) "pgmGCShw32Bit" #name
#define PGM_SHW_NAME_R0_32BIT_STR(name) "pgmR0Shw32Bit" #name
#define PGM_SHW_NAME_PAE(name)          PGM_CTX(pgm,ShwPAE##name)
#define PGM_SHW_NAME_GC_PAE_STR(name)   "pgmGCShwPAE" #name
#define PGM_SHW_NAME_R0_PAE_STR(name)   "pgmR0ShwPAE" #name
#define PGM_SHW_NAME_AMD64(name)        PGM_CTX(pgm,ShwAMD64##name)
#define PGM_SHW_NAME_GC_AMD64_STR(name) "pgmGCShwAMD64" #name
#define PGM_SHW_NAME_R0_AMD64_STR(name) "pgmR0ShwAMD64" #name
#define PGM_SHW_DECL(type, name)        PGM_CTX_DECL(type) PGM_SHW_NAME(name)
#define PGM_SHW_PFN(name, pVM)          ((pVM)->pgm.s.PGM_CTX(pfn,Shw##name))

/*                   Shw_Gst */
#define PGM_BTH_NAME_32BIT_REAL(name)   PGM_CTX(pgm,Bth32BitReal##name)
#define PGM_BTH_NAME_32BIT_PROT(name)   PGM_CTX(pgm,Bth32BitProt##name)
#define PGM_BTH_NAME_32BIT_32BIT(name)  PGM_CTX(pgm,Bth32Bit32Bit##name)
#define PGM_BTH_NAME_PAE_REAL(name)     PGM_CTX(pgm,BthPAEReal##name)
#define PGM_BTH_NAME_PAE_PROT(name)     PGM_CTX(pgm,BthPAEProt##name)
#define PGM_BTH_NAME_PAE_32BIT(name)    PGM_CTX(pgm,BthPAE32Bit##name)
#define PGM_BTH_NAME_PAE_PAE(name)      PGM_CTX(pgm,BthPAEPAE##name)
#define PGM_BTH_NAME_AMD64_REAL(name)   PGM_CTX(pgm,BthAMD64Real##name)
#define PGM_BTH_NAME_AMD64_PROT(name)   PGM_CTX(pgm,BthAMD64Prot##name)
#define PGM_BTH_NAME_AMD64_AMD64(name)  PGM_CTX(pgm,BthAMD64AMD64##name)
#define PGM_BTH_NAME_GC_32BIT_REAL_STR(name)   "pgmGCBth32BitReal" #name
#define PGM_BTH_NAME_GC_32BIT_PROT_STR(name)   "pgmGCBth32BitProt" #name
#define PGM_BTH_NAME_GC_32BIT_32BIT_STR(name)  "pgmGCBth32Bit32Bit" #name
#define PGM_BTH_NAME_GC_PAE_REAL_STR(name)     "pgmGCBthPAEReal" #name
#define PGM_BTH_NAME_GC_PAE_PROT_STR(name)     "pgmGCBthPAEProt" #name
#define PGM_BTH_NAME_GC_PAE_32BIT_STR(name)    "pgmGCBthPAE32Bit" #name
#define PGM_BTH_NAME_GC_PAE_PAE_STR(name)      "pgmGCBthPAEPAE" #name
#define PGM_BTH_NAME_GC_AMD64_REAL_STR(name)   "pgmGCBthAMD64Real" #name
#define PGM_BTH_NAME_GC_AMD64_PROT_STR(name)   "pgmGCBthAMD64Prot" #name
#define PGM_BTH_NAME_GC_AMD64_AMD64_STR(name)  "pgmGCBthAMD64AMD64" #name
#define PGM_BTH_NAME_R0_32BIT_REAL_STR(name)   "pgmR0Bth32BitReal" #name
#define PGM_BTH_NAME_R0_32BIT_PROT_STR(name)   "pgmR0Bth32BitProt" #name
#define PGM_BTH_NAME_R0_32BIT_32BIT_STR(name)  "pgmR0Bth32Bit32Bit" #name
#define PGM_BTH_NAME_R0_PAE_REAL_STR(name)     "pgmR0BthPAEReal" #name
#define PGM_BTH_NAME_R0_PAE_PROT_STR(name)     "pgmR0BthPAEProt" #name
#define PGM_BTH_NAME_R0_PAE_32BIT_STR(name)    "pgmR0BthPAE32Bit" #name
#define PGM_BTH_NAME_R0_PAE_PAE_STR(name)      "pgmR0BthPAEPAE" #name
#define PGM_BTH_NAME_R0_AMD64_REAL_STR(name)   "pgmR0BthAMD64Real" #name
#define PGM_BTH_NAME_R0_AMD64_PROT_STR(name)   "pgmR0BthAMD64Prot" #name
#define PGM_BTH_NAME_R0_AMD64_AMD64_STR(name)  "pgmR0BthAMD64AMD64" #name
#define PGM_BTH_DECL(type, name)        PGM_CTX_DECL(type) PGM_BTH_NAME(name)
#define PGM_BTH_PFN(name, pVM)          ((pVM)->pgm.s.PGM_CTX(pfn,Bth##name))
/** @} */

/**
 * Data for each paging mode.
 */
typedef struct PGMMODEDATA
{
    /** The guest mode type. */
    uint32_t    uGstType;
    /** The shadow mode type. */
    uint32_t    uShwType;

    /** @name Function pointers for Shadow paging.
     * @{
     */
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwRelocate,(PVM pVM, RTGCUINTPTR offDelta));
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwExit,(PVM pVM));
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwGetPage,(PVM pVM, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTHCPHYS pHCPhys));
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwModifyPage,(PVM pVM, RTGCUINTPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask));
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwGetPDEByIndex,(PVM pVM, uint32_t iPD, PX86PDEPAE pPde));
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwSetPDEByIndex,(PVM pVM, uint32_t iPD, X86PDEPAE Pde));
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwModifyPDEByIndex,(PVM pVM, uint32_t iPD, uint64_t fFlags, uint64_t fMask));

    DECLGCCALLBACKMEMBER(int,  pfnGCShwGetPage,(PVM pVM, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTHCPHYS pHCPhys));
    DECLGCCALLBACKMEMBER(int,  pfnGCShwModifyPage,(PVM pVM, RTGCUINTPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask));
    DECLGCCALLBACKMEMBER(int,  pfnGCShwGetPDEByIndex,(PVM pVM, uint32_t iPD, PX86PDEPAE pPde));
    DECLGCCALLBACKMEMBER(int,  pfnGCShwSetPDEByIndex,(PVM pVM, uint32_t iPD, X86PDEPAE Pde));
    DECLGCCALLBACKMEMBER(int,  pfnGCShwModifyPDEByIndex,(PVM pVM, uint32_t iPD, uint64_t fFlags, uint64_t fMask));

    DECLR0CALLBACKMEMBER(int,  pfnR0ShwGetPage,(PVM pVM, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTHCPHYS pHCPhys));
    DECLR0CALLBACKMEMBER(int,  pfnR0ShwModifyPage,(PVM pVM, RTGCUINTPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask));
    DECLR0CALLBACKMEMBER(int,  pfnR0ShwGetPDEByIndex,(PVM pVM, uint32_t iPD, PX86PDEPAE pPde));
    DECLR0CALLBACKMEMBER(int,  pfnR0ShwSetPDEByIndex,(PVM pVM, uint32_t iPD, X86PDEPAE Pde));
    DECLR0CALLBACKMEMBER(int,  pfnR0ShwModifyPDEByIndex,(PVM pVM, uint32_t iPD, uint64_t fFlags, uint64_t fMask));
    /** @} */

    /** @name Function pointers for Guest paging.
     * @{
     */
    DECLR3CALLBACKMEMBER(int,  pfnR3GstRelocate,(PVM pVM, RTGCUINTPTR offDelta));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstExit,(PVM pVM));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstGetPage,(PVM pVM, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTGCPHYS pGCPhys));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstModifyPage,(PVM pVM, RTGCUINTPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstGetPDE,(PVM pVM, RTGCUINTPTR GCPtr, PX86PDEPAE pPde));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstMonitorCR3,(PVM pVM, RTGCPHYS GCPhysCR3));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstUnmonitorCR3,(PVM pVM));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstMapCR3,(PVM pVM, RTGCPHYS GCPhysCR3));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstUnmapCR3,(PVM pVM));
    HCPTRTYPE(PFNPGMR3PHYSHANDLER)  pfnHCGstWriteHandlerCR3;
    HCPTRTYPE(const char *)         pszHCGstWriteHandlerCR3;

    DECLGCCALLBACKMEMBER(int,  pfnGCGstGetPage,(PVM pVM, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTGCPHYS pGCPhys));
    DECLGCCALLBACKMEMBER(int,  pfnGCGstModifyPage,(PVM pVM, RTGCUINTPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask));
    DECLGCCALLBACKMEMBER(int,  pfnGCGstGetPDE,(PVM pVM, RTGCUINTPTR GCPtr, PX86PDEPAE pPde));
    DECLGCCALLBACKMEMBER(int,  pfnGCGstMonitorCR3,(PVM pVM, RTGCPHYS GCPhysCR3));
    DECLGCCALLBACKMEMBER(int,  pfnGCGstUnmonitorCR3,(PVM pVM));
    DECLGCCALLBACKMEMBER(int,  pfnGCGstMapCR3,(PVM pVM, RTGCPHYS GCPhysCR3));
    DECLGCCALLBACKMEMBER(int,  pfnGCGstUnmapCR3,(PVM pVM));
    GCPTRTYPE(PFNPGMGCPHYSHANDLER)  pfnGCGstWriteHandlerCR3;

    DECLR0CALLBACKMEMBER(int,  pfnR0GstGetPage,(PVM pVM, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTGCPHYS pGCPhys));
    DECLR0CALLBACKMEMBER(int,  pfnR0GstModifyPage,(PVM pVM, RTGCUINTPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask));
    DECLR0CALLBACKMEMBER(int,  pfnR0GstGetPDE,(PVM pVM, RTGCUINTPTR GCPtr, PX86PDEPAE pPde));
    DECLR0CALLBACKMEMBER(int,  pfnR0GstMonitorCR3,(PVM pVM, RTGCPHYS GCPhysCR3));
    DECLR0CALLBACKMEMBER(int,  pfnR0GstUnmonitorCR3,(PVM pVM));
    DECLR0CALLBACKMEMBER(int,  pfnR0GstMapCR3,(PVM pVM, RTGCPHYS GCPhysCR3));
    DECLR0CALLBACKMEMBER(int,  pfnR0GstUnmapCR3,(PVM pVM));
    R0PTRTYPE(PFNPGMR0PHYSHANDLER)  pfnR0GstWriteHandlerCR3;
    /** @} */

    /** @name Function pointers for Both Shadow and Guest paging.
     * @{
     */
    DECLR3CALLBACKMEMBER(int,       pfnR3BthRelocate,(PVM pVM, RTGCUINTPTR offDelta));
    DECLR3CALLBACKMEMBER(int,       pfnR3BthTrap0eHandler,(PVM pVM, RTGCUINT uErr, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault));
    DECLR3CALLBACKMEMBER(int,       pfnR3BthInvalidatePage,(PVM pVM, RTGCPTR GCPtrPage));
    DECLR3CALLBACKMEMBER(int,       pfnR3BthSyncCR3,(PVM pVM, uint32_t cr0, uint32_t cr3, uint32_t cr4, bool fGlobal));
    DECLR3CALLBACKMEMBER(int,       pfnR3BthSyncPage,(PVM pVM, VBOXPDE PdeSrc, RTGCUINTPTR GCPtrPage, unsigned cPages, unsigned uError));
    DECLR3CALLBACKMEMBER(int,       pfnR3BthPrefetchPage,(PVM pVM, RTGCUINTPTR GCPtrPage));
    DECLR3CALLBACKMEMBER(int,       pfnR3BthVerifyAccessSyncPage,(PVM pVM, RTGCUINTPTR GCPtrPage, unsigned fFlags, unsigned uError));
#ifdef VBOX_STRICT
    DECLR3CALLBACKMEMBER(unsigned,  pfnR3BthAssertCR3,(PVM pVM, uint32_t cr3, uint32_t cr4, RTGCUINTPTR GCPtr, RTGCUINTPTR cb));
#endif

    DECLGCCALLBACKMEMBER(int,       pfnGCBthTrap0eHandler,(PVM pVM, RTGCUINT uErr, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault));
    DECLGCCALLBACKMEMBER(int,       pfnGCBthInvalidatePage,(PVM pVM, RTGCPTR GCPtrPage));
    DECLGCCALLBACKMEMBER(int,       pfnGCBthSyncCR3,(PVM pVM, uint32_t cr0, uint32_t cr3, uint32_t cr4, bool fGlobal));
    DECLGCCALLBACKMEMBER(int,       pfnGCBthSyncPage,(PVM pVM, VBOXPDE PdeSrc, RTGCUINTPTR GCPtrPage, unsigned cPages, unsigned uError));
    DECLGCCALLBACKMEMBER(int,       pfnGCBthPrefetchPage,(PVM pVM, RTGCUINTPTR GCPtrPage));
    DECLGCCALLBACKMEMBER(int,       pfnGCBthVerifyAccessSyncPage,(PVM pVM, RTGCUINTPTR GCPtrPage, unsigned fFlags, unsigned uError));
#ifdef VBOX_STRICT
    DECLGCCALLBACKMEMBER(unsigned,  pfnGCBthAssertCR3,(PVM pVM, uint32_t cr3, uint32_t cr4, RTGCUINTPTR GCPtr, RTGCUINTPTR cb));
#endif

    DECLR0CALLBACKMEMBER(int,       pfnR0BthTrap0eHandler,(PVM pVM, RTGCUINT uErr, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault));
    DECLR0CALLBACKMEMBER(int,       pfnR0BthInvalidatePage,(PVM pVM, RTGCPTR GCPtrPage));
    DECLR0CALLBACKMEMBER(int,       pfnR0BthSyncCR3,(PVM pVM, uint32_t cr0, uint32_t cr3, uint32_t cr4, bool fGlobal));
    DECLR0CALLBACKMEMBER(int,       pfnR0BthSyncPage,(PVM pVM, VBOXPDE PdeSrc, RTGCUINTPTR GCPtrPage, unsigned cPages, unsigned uError));
    DECLR0CALLBACKMEMBER(int,       pfnR0BthPrefetchPage,(PVM pVM, RTGCUINTPTR GCPtrPage));
    DECLR0CALLBACKMEMBER(int,       pfnR0BthVerifyAccessSyncPage,(PVM pVM, RTGCUINTPTR GCPtrPage, unsigned fFlags, unsigned uError));
#ifdef VBOX_STRICT
    DECLR0CALLBACKMEMBER(unsigned,  pfnR0BthAssertCR3,(PVM pVM, uint32_t cr3, uint32_t cr4, RTGCUINTPTR GCPtr, RTGCUINTPTR cb));
#endif
    /** @} */
} PGMMODEDATA, *PPGMMODEDATA;



/**
 * Converts a PGM pointer into a VM pointer.
 * @returns Pointer to the VM structure the PGM is part of.
 * @param   pPGM   Pointer to PGM instance data.
 */
#define PGM2VM(pPGM)  ( (PVM)((char*)pPGM - pPGM->offVM) )

/**
 * PGM Data (part of VM)
 */
typedef struct PGM
{
    /** Offset to the VM structure. */
    RTINT                       offVM;

    /*
     * This will be redefined at least two more times before we're done, I'm sure.
     * The current code is only to get on with the coding.
     *   - 2004-06-10: initial version, bird.
     *   - 2004-07-02: 1st time, bird.
     *   - 2004-10-18: 2nd time, bird.
     *   - 2005-07-xx: 3rd time, bird.
     */

    /** Pointer to the page table entries for the dynamic page mapping area - GCPtr. */
    GCPTRTYPE(PX86PTE)          paDynPageMap32BitPTEsGC;
    /** Pointer to the page table entries for the dynamic page mapping area - GCPtr. */
    GCPTRTYPE(PX86PTEPAE)       paDynPageMapPaePTEsGC;

    /** The host paging mode. (This is what SUPLib reports.) */
    SUPPAGINGMODE               enmHostMode;
    /** The shadow paging mode. */
    PGMMODE                     enmShadowMode;
    /** The guest paging mode. */
    PGMMODE                     enmGuestMode;

    /** The current physical address representing in the guest CR3 register. */
    RTGCPHYS                    GCPhysCR3;
    /** Pointer to the 5 page CR3 content mapping.
     * The first page is always the CR3 (in some form) while the 4 other pages
     * are used of the PDs in PAE mode. */
    RTGCPTR                     GCPtrCR3Mapping;
    /** The physical address of the currently monitored guest CR3 page.
     * When this value is NIL_RTGCPHYS no page is being monitored. */
    RTGCPHYS                    GCPhysGstCR3Monitored;
#if HC_ARCH_BITS == 64 || GC_ARCH_BITS == 64
    RTGCPHYS                    GCPhysPadding0; /**< alignment padding. */
#endif

    /** @name 32-bit Guest Paging.
     * @{ */
    /** The guest's page directory, HC pointer. */
    HCPTRTYPE(PVBOXPD)          pGuestPDHC;
    /** The guest's page directory, static GC mapping. */
    GCPTRTYPE(PVBOXPD)          pGuestPDGC;
    /** @} */

    /** @name PAE Guest Paging.
     * @{ */
    /** The guest's page directory pointer table, static GC mapping. */
    GCPTRTYPE(PX86PDPTR)        pGstPaePDPTRGC;
    /** The guest's page directory pointer table, HC pointer. */
    HCPTRTYPE(PX86PDPTR)        pGstPaePDPTRHC;
    /** The guest's page directories, HC pointers.
     * These are individual pointers and doesn't have to be adjecent.
     * These doesn't have to be update to date - use pgmGstGetPaePD() to access them. */
    HCPTRTYPE(PX86PDPAE)        apGstPaePDsHC[4];
    /** The guest's page directories, static GC mapping.
     * Unlike the HC array the first entry can be accessed as a 2048 entry PD.
     * These doesn't have to be update to date - use pgmGstGetPaePD() to access them. */
    GCPTRTYPE(PX86PDPAE)        apGstPaePDsGC[4];
    /** The physical addresses of the guest page directories (PAE) pointed to by apGstPagePDsHC/GC. */
    RTGCPHYS                    aGCPhysGstPaePDs[4];
    /** The physical addresses of the monitored guest page directories (PAE). */
    RTGCPHYS                    aGCPhysGstPaePDsMonitored[4];
    /** @} */


    /** @name 32-bit Shadow Paging
     * @{ */
    /** The 32-Bit PD - HC Ptr. */
    HCPTRTYPE(PX86PD)           pHC32BitPD;
    /** The 32-Bit PD - GC Ptr. */
    GCPTRTYPE(PX86PD)           pGC32BitPD;
#if HC_ARCH_BITS == 64 && GC_ARCH_BITS == 32
    uint32_t                    u32Padding1; /**< alignment padding. */
#endif
    /** The Physical Address (HC) of the 32-Bit PD. */
    RTHCPHYS                    HCPhys32BitPD;
    /** @} */

    /** @name PAE Shadow Paging
     * @{ */
    /** The four PDs for the low 4GB - HC Ptr.
     * Even though these are 4 pointers, what they point at is a single table.
     * Thus, it's possible to walk the 2048 entries starting where apHCPaePDs[0] points. */
    HCPTRTYPE(PX86PDPAE)        apHCPaePDs[4];
    /** The four PDs for the low 4GB - GC Ptr.
     * Same kind of mapping as apHCPaePDs. */
    GCPTRTYPE(PX86PDPAE)        apGCPaePDs[4];
    /** The Physical Address (HC) of the four PDs for the low 4GB.
     * These are *NOT* 4 contiguous pages. */
    RTHCPHYS                    aHCPhysPaePDs[4];
    /** The PAE PDPTR - HC Ptr. */
    HCPTRTYPE(PX86PDPTR)        pHCPaePDPTR;
    /** The Physical Address (HC) of the PAE PDPTR. */
    RTHCPHYS                    HCPhysPaePDPTR;
    /** The PAE PDPTR - GC Ptr. */
    GCPTRTYPE(PX86PDPTR)        pGCPaePDPTR;
    /** @} */

    /** @name AMD64 Shadow Paging
     * Extends PAE Paging.
     * @{ */
    /** The Page Map Level 4 table - HC Ptr. */
    GCPTRTYPE(PX86PML4)         pGCPaePML4;
    /** The Page Map Level 4 table - GC Ptr. */
    HCPTRTYPE(PX86PML4)         pHCPaePML4;
    /** The Physical Address (HC) of the Page Map Level 4 table. */
    RTHCPHYS                    HCPhysPaePML4;
    /** @}*/

    /** @name Function pointers for Shadow paging.
     * @{
     */
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwRelocate,(PVM pVM, RTGCUINTPTR offDelta));
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwExit,(PVM pVM));
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwGetPage,(PVM pVM, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTHCPHYS pHCPhys));
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwModifyPage,(PVM pVM, RTGCUINTPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask));
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwGetPDEByIndex,(PVM pVM, uint32_t iPD, PX86PDEPAE pPde));
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwSetPDEByIndex,(PVM pVM, uint32_t iPD, X86PDEPAE Pde));
    DECLR3CALLBACKMEMBER(int,  pfnR3ShwModifyPDEByIndex,(PVM pVM, uint32_t iPD, uint64_t fFlags, uint64_t fMask));

    DECLGCCALLBACKMEMBER(int,  pfnGCShwGetPage,(PVM pVM, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTHCPHYS pHCPhys));
    DECLGCCALLBACKMEMBER(int,  pfnGCShwModifyPage,(PVM pVM, RTGCUINTPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask));
    DECLGCCALLBACKMEMBER(int,  pfnGCShwGetPDEByIndex,(PVM pVM, uint32_t iPD, PX86PDEPAE pPde));
    DECLGCCALLBACKMEMBER(int,  pfnGCShwSetPDEByIndex,(PVM pVM, uint32_t iPD, X86PDEPAE Pde));
    DECLGCCALLBACKMEMBER(int,  pfnGCShwModifyPDEByIndex,(PVM pVM, uint32_t iPD, uint64_t fFlags, uint64_t fMask));
#if GC_ARCH_BITS == 32 && HC_ARCH_BITS == 64
    RTGCPTR                    alignment0; /**< structure size alignment. */
#endif 

    DECLR0CALLBACKMEMBER(int,  pfnR0ShwGetPage,(PVM pVM, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTHCPHYS pHCPhys));
    DECLR0CALLBACKMEMBER(int,  pfnR0ShwModifyPage,(PVM pVM, RTGCUINTPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask));
    DECLR0CALLBACKMEMBER(int,  pfnR0ShwGetPDEByIndex,(PVM pVM, uint32_t iPD, PX86PDEPAE pPde));
    DECLR0CALLBACKMEMBER(int,  pfnR0ShwSetPDEByIndex,(PVM pVM, uint32_t iPD, X86PDEPAE Pde));
    DECLR0CALLBACKMEMBER(int,  pfnR0ShwModifyPDEByIndex,(PVM pVM, uint32_t iPD, uint64_t fFlags, uint64_t fMask));

    /** @} */

    /** @name Function pointers for Guest paging.
     * @{
     */
    DECLR3CALLBACKMEMBER(int,  pfnR3GstRelocate,(PVM pVM, RTGCUINTPTR offDelta));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstExit,(PVM pVM));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstGetPage,(PVM pVM, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTGCPHYS pGCPhys));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstModifyPage,(PVM pVM, RTGCUINTPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstGetPDE,(PVM pVM, RTGCUINTPTR GCPtr, PX86PDEPAE pPde));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstMonitorCR3,(PVM pVM, RTGCPHYS GCPhysCR3));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstUnmonitorCR3,(PVM pVM));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstMapCR3,(PVM pVM, RTGCPHYS GCPhysCR3));
    DECLR3CALLBACKMEMBER(int,  pfnR3GstUnmapCR3,(PVM pVM));
    HCPTRTYPE(PFNPGMR3PHYSHANDLER)  pfnHCGstWriteHandlerCR3;
    HCPTRTYPE(const char *)         pszHCGstWriteHandlerCR3;

    DECLGCCALLBACKMEMBER(int,  pfnGCGstGetPage,(PVM pVM, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTGCPHYS pGCPhys));
    DECLGCCALLBACKMEMBER(int,  pfnGCGstModifyPage,(PVM pVM, RTGCUINTPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask));
    DECLGCCALLBACKMEMBER(int,  pfnGCGstGetPDE,(PVM pVM, RTGCUINTPTR GCPtr, PX86PDEPAE pPde));
    DECLGCCALLBACKMEMBER(int,  pfnGCGstMonitorCR3,(PVM pVM, RTGCPHYS GCPhysCR3));
    DECLGCCALLBACKMEMBER(int,  pfnGCGstUnmonitorCR3,(PVM pVM));
    DECLGCCALLBACKMEMBER(int,  pfnGCGstMapCR3,(PVM pVM, RTGCPHYS GCPhysCR3));
    DECLGCCALLBACKMEMBER(int,  pfnGCGstUnmapCR3,(PVM pVM));
    GCPTRTYPE(PFNPGMGCPHYSHANDLER)  pfnGCGstWriteHandlerCR3;

    DECLR0CALLBACKMEMBER(int,  pfnR0GstGetPage,(PVM pVM, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTGCPHYS pGCPhys));
    DECLR0CALLBACKMEMBER(int,  pfnR0GstModifyPage,(PVM pVM, RTGCUINTPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask));
    DECLR0CALLBACKMEMBER(int,  pfnR0GstGetPDE,(PVM pVM, RTGCUINTPTR GCPtr, PX86PDEPAE pPde));
    DECLR0CALLBACKMEMBER(int,  pfnR0GstMonitorCR3,(PVM pVM, RTGCPHYS GCPhysCR3));
    DECLR0CALLBACKMEMBER(int,  pfnR0GstUnmonitorCR3,(PVM pVM));
    DECLR0CALLBACKMEMBER(int,  pfnR0GstMapCR3,(PVM pVM, RTGCPHYS GCPhysCR3));
    DECLR0CALLBACKMEMBER(int,  pfnR0GstUnmapCR3,(PVM pVM));
    R0PTRTYPE(PFNPGMR0PHYSHANDLER)  pfnR0GstWriteHandlerCR3;
    /** @} */

    /** @name Function pointers for Both Shadow and Guest paging.
     * @{
     */
    DECLR3CALLBACKMEMBER(int,       pfnR3BthRelocate,(PVM pVM, RTGCUINTPTR offDelta));
    DECLR3CALLBACKMEMBER(int,       pfnR3BthTrap0eHandler,(PVM pVM, RTGCUINT uErr, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault));
    DECLR3CALLBACKMEMBER(int,       pfnR3BthInvalidatePage,(PVM pVM, RTGCPTR GCPtrPage));
    DECLR3CALLBACKMEMBER(int,       pfnR3BthSyncCR3,(PVM pVM, uint32_t cr0, uint32_t cr3, uint32_t cr4, bool fGlobal));
    DECLR3CALLBACKMEMBER(int,       pfnR3BthSyncPage,(PVM pVM, VBOXPDE PdeSrc, RTGCUINTPTR GCPtrPage, unsigned cPages, unsigned uError));
    DECLR3CALLBACKMEMBER(int,       pfnR3BthPrefetchPage,(PVM pVM, RTGCUINTPTR GCPtrPage));
    DECLR3CALLBACKMEMBER(int,       pfnR3BthVerifyAccessSyncPage,(PVM pVM, RTGCUINTPTR GCPtrPage, unsigned fFlags, unsigned uError));
    DECLR3CALLBACKMEMBER(unsigned,  pfnR3BthAssertCR3,(PVM pVM, uint32_t cr3, uint32_t cr4, RTGCUINTPTR GCPtr, RTGCUINTPTR cb));

    DECLR0CALLBACKMEMBER(int,       pfnR0BthTrap0eHandler,(PVM pVM, RTGCUINT uErr, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault));
    DECLR0CALLBACKMEMBER(int,       pfnR0BthInvalidatePage,(PVM pVM, RTGCPTR GCPtrPage));
    DECLR0CALLBACKMEMBER(int,       pfnR0BthSyncCR3,(PVM pVM, uint32_t cr0, uint32_t cr3, uint32_t cr4, bool fGlobal));
    DECLR0CALLBACKMEMBER(int,       pfnR0BthSyncPage,(PVM pVM, VBOXPDE PdeSrc, RTGCUINTPTR GCPtrPage, unsigned cPages, unsigned uError));
    DECLR0CALLBACKMEMBER(int,       pfnR0BthPrefetchPage,(PVM pVM, RTGCUINTPTR GCPtrPage));
    DECLR0CALLBACKMEMBER(int,       pfnR0BthVerifyAccessSyncPage,(PVM pVM, RTGCUINTPTR GCPtrPage, unsigned fFlags, unsigned uError));
    DECLR0CALLBACKMEMBER(unsigned,  pfnR0BthAssertCR3,(PVM pVM, uint32_t cr3, uint32_t cr4, RTGCUINTPTR GCPtr, RTGCUINTPTR cb));

    DECLGCCALLBACKMEMBER(int,       pfnGCBthTrap0eHandler,(PVM pVM, RTGCUINT uErr, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault));
    DECLGCCALLBACKMEMBER(int,       pfnGCBthInvalidatePage,(PVM pVM, RTGCPTR GCPtrPage));
    DECLGCCALLBACKMEMBER(int,       pfnGCBthSyncCR3,(PVM pVM, uint32_t cr0, uint32_t cr3, uint32_t cr4, bool fGlobal));
    DECLGCCALLBACKMEMBER(int,       pfnGCBthSyncPage,(PVM pVM, VBOXPDE PdeSrc, RTGCUINTPTR GCPtrPage, unsigned cPages, unsigned uError));
    DECLGCCALLBACKMEMBER(int,       pfnGCBthPrefetchPage,(PVM pVM, RTGCUINTPTR GCPtrPage));
    DECLGCCALLBACKMEMBER(int,       pfnGCBthVerifyAccessSyncPage,(PVM pVM, RTGCUINTPTR GCPtrPage, unsigned fFlags, unsigned uError));
    DECLGCCALLBACKMEMBER(unsigned,  pfnGCBthAssertCR3,(PVM pVM, uint32_t cr3, uint32_t cr4, RTGCUINTPTR GCPtr, RTGCUINTPTR cb));
#if GC_ARCH_BITS == 32 && HC_ARCH_BITS == 64
    RTGCPTR                         alignment2; /**< structure size alignment. */
#endif 
    /** @} */

    /** Pointer to SHW+GST mode data (function pointers).
     * The index into this table is made up from */
    R3PTRTYPE(PPGMMODEDATA)         paModeData;


    /** Pointer to the list of RAM ranges (Phys GC -> Phys HC conversion) - for HC.
     * This is sorted by physical address and contains no overlaps.
     * The memory locks and other conversions are managed by MM at the moment.
     */
    HCPTRTYPE(PPGMRAMRANGE)         pRamRangesHC;
    /** Pointer to the list of RAM ranges (Phys GC -> Phys HC conversion) - for GC.
     * This is sorted by physical address and contains no overlaps.
     * The memory locks and other conversions are managed by MM at the moment.
     */
    GCPTRTYPE(PPGMRAMRANGE)         pRamRangesGC;
    /** The configured RAM size. */
    RTUINT                          cbRamSize;

    /** PGM offset based trees - HC Ptr. */
    HCPTRTYPE(PPGMTREES)            pTreesHC;
    /** PGM offset based trees - GC Ptr. */
    GCPTRTYPE(PPGMTREES)            pTreesGC;

    /** Linked list of GC mappings - for GC.
     * The list is sorted ascending on address.
     */
    GCPTRTYPE(PPGMMAPPING)          pMappingsGC;
    /** Linked list of GC mappings - for HC.
     * The list is sorted ascending on address.
     */
    R3PTRTYPE(PPGMMAPPING)          pMappingsR3;
    /** Linked list of GC mappings - for R0.
     * The list is sorted ascending on address.
     */
    R0PTRTYPE(PPGMMAPPING)          pMappingsR0;

    /** If set no conflict checks are required.  (boolean) */
    bool                            fMappingsFixed;
    /** If set, then no mappings are put into the shadow page table. (boolean) */
    bool                            fDisableMappings;
    /** Size of fixed mapping */
    uint32_t                        cbMappingFixed;
    /** Base address (GC) of fixed mapping */
    RTGCPTR                         GCPtrMappingFixed;
#if HC_ARCH_BITS == 64 && GC_ARCH_BITS == 32
    uint32_t                        u32Padding0; /**< alignment padding. */
#endif


    /** @name Intermediate Context
     * @{ */
    /** Pointer to the intermediate page directory - Normal. */
    HCPTRTYPE(PX86PD)               pInterPD;
    /** Pointer to the intermedate page tables - Normal.
     * There are two page tables, one for the identity mapping and one for
     * the host context mapping (of the core code). */
    HCPTRTYPE(PX86PT)               apInterPTs[2];
    /** Pointer to the intermedate page tables - PAE. */
    HCPTRTYPE(PX86PTPAE)            apInterPaePTs[2];
    /** Pointer to the intermedate page directory - PAE. */
    HCPTRTYPE(PX86PDPAE)            apInterPaePDs[4];
    /** Pointer to the intermedate page directory - PAE. */
    HCPTRTYPE(PX86PDPTR)            pInterPaePDPTR;
    /** Pointer to the intermedate page-map level 4 - AMD64. */
    HCPTRTYPE(PX86PML4)             pInterPaePML4;
    /** Pointer to the intermedate page directory - AMD64. */
    HCPTRTYPE(PX86PDPTR)            pInterPaePDPTR64;
    /** The Physical Address (HC) of the intermediate Page Directory - Normal. */
    RTHCPHYS                        HCPhysInterPD;
    /** The Physical Address (HC) of the intermediate Page Directory Pointer Table - PAE. */
    RTHCPHYS                        HCPhysInterPaePDPTR;
    /** The Physical Address (HC) of the intermediate Page Map Level 4 table - AMD64. */
    RTHCPHYS                        HCPhysInterPaePML4;
    /** @} */

    /** Base address of the dynamic page mapping area.
     * The array is MM_HYPER_DYNAMIC_SIZE bytes big.
     */
    GCPTRTYPE(uint8_t *)            pbDynPageMapBaseGC;
    /** The index of the last entry used in the dynamic page mapping area. */
    RTUINT                          iDynPageMapLast;
    /** Cache containing the last entries in the dynamic page mapping area.
     * The cache size is covering half of the mapping area. */
    RTHCPHYS                        aHCPhysDynPageMapCache[MM_HYPER_DYNAMIC_SIZE >> (PAGE_SHIFT + 1)];

    /** A20 gate mask.
     * Our current approach to A20 emulation is to let REM do it and don't bother
     * anywhere else. The interesting Guests will be operating with it enabled anyway.
     * But whould need arrise, we'll subject physical addresses to this mask. */
    RTGCPHYS                        GCPhysA20Mask;
    /** A20 gate state - boolean! */
    RTUINT                          fA20Enabled;

    /** What needs syncing (PGM_SYNC_*).
     * This is used to queue operations for PGMSyncCR3, PGMInvalidatePage,
     * PGMFlushTLB, and PGMR3Load. */
    RTUINT                          fSyncFlags;

#if HC_ARCH_BITS == 64 && GC_ARCH_BITS == 32
    RTUINT                          uPadding3; /**< alignment padding. */
#endif
    /** PGM critical section.
     * This protects the physical & virtual access handlers, ram ranges,
     * and the page flag updating (some of it anyway).
     */
    PDMCRITSECT                     CritSect;

    /** Shadow Page Pool - HC Ptr. */
    HCPTRTYPE(PPGMPOOL)             pPoolHC;
    /** Shadow Page Pool - GC Ptr. */
    GCPTRTYPE(PPGMPOOL)             pPoolGC;

    /** Flush the cache on the next access. */
    bool                            fPhysCacheFlushPending;
/** @todo r=bird: Fix member names!*/
    /** PGMPhysRead cache */
    PGMPHYSCACHE                    pgmphysreadcache;
    /** PGMPhysWrite cache */
    PGMPHYSCACHE                    pgmphyswritecache;

    /** @name Release Statistics
     * @{ */
    /** The number of times the guest has switched mode since last reset or statistics reset. */
    STAMCOUNTER                     cGuestModeChanges;
    /** @} */

#ifdef VBOX_WITH_STATISTICS
    /** GC: Which statistic this \#PF should be attributed to. */
    GCPTRTYPE(PSTAMPROFILE) pStatTrap0eAttributionGC;
    RTGCPTR                 padding0;
    /** HC: Which statistic this \#PF should be attributed to. */
    HCPTRTYPE(PSTAMPROFILE) pStatTrap0eAttributionHC;
    RTHCPTR                 padding1;
    STAMPROFILE     StatGCTrap0e;                       /**< GC: PGMGCTrap0eHandler() profiling. */
    STAMPROFILE     StatTrap0eCSAM;                     /**< Profiling of the Trap0eHandler body when the cause is CSAM. */
    STAMPROFILE     StatTrap0eDirtyAndAccessedBits;     /**< Profiling of the Trap0eHandler body when the cause is dirty and/or accessed bit emulation. */
    STAMPROFILE     StatTrap0eGuestTrap;                /**< Profiling of the Trap0eHandler body when the cause is a guest trap. */
    STAMPROFILE     StatTrap0eHndPhys;                  /**< Profiling of the Trap0eHandler body when the cause is a physical handler. */
    STAMPROFILE     StatTrap0eHndVirt;                  /**< Profiling of the Trap0eHandler body when the cause is a virtual handler. */
    STAMPROFILE     StatTrap0eHndUnhandled;             /**< Profiling of the Trap0eHandler body when the cause is access outside the monitored areas of a monitored page. */
    STAMPROFILE     StatTrap0eMisc;                     /**< Profiling of the Trap0eHandler body when the cause is not known. */
    STAMPROFILE     StatTrap0eOutOfSync;                /**< Profiling of the Trap0eHandler body when the cause is an out-of-sync page. */
    STAMPROFILE     StatTrap0eOutOfSyncHndPhys;         /**< Profiling of the Trap0eHandler body when the cause is an out-of-sync physical handler page. */
    STAMPROFILE     StatTrap0eOutOfSyncHndVirt;         /**< Profiling of the Trap0eHandler body when the cause is an out-of-sync virtual handler page. */
    STAMPROFILE     StatTrap0eOutOfSyncObsHnd;          /**< Profiling of the Trap0eHandler body when the cause is an obsolete handler page. */
    STAMPROFILE     StatTrap0eSyncPT;                   /**< Profiling of the Trap0eHandler body when the cause is lazy syncing of a PT. */

    STAMCOUNTER     StatTrap0eMapHandler;               /**< Number of traps due to access handlers in mappings. */
    STAMCOUNTER     StatGCTrap0eConflicts;              /**< GC: The number of times \#PF was caused by an undetected conflict. */

    STAMCOUNTER     StatGCTrap0eUSNotPresentRead;
    STAMCOUNTER     StatGCTrap0eUSNotPresentWrite;
    STAMCOUNTER     StatGCTrap0eUSWrite;
    STAMCOUNTER     StatGCTrap0eUSReserved;
    STAMCOUNTER     StatGCTrap0eUSRead;

    STAMCOUNTER     StatGCTrap0eSVNotPresentRead;
    STAMCOUNTER     StatGCTrap0eSVNotPresentWrite;
    STAMCOUNTER     StatGCTrap0eSVWrite;
    STAMCOUNTER     StatGCTrap0eSVReserved;

    STAMCOUNTER     StatGCTrap0eUnhandled;
    STAMCOUNTER     StatGCTrap0eMap;

    /** GC: PGMSyncPT() profiling. */
    STAMPROFILE     StatGCSyncPT;
    /** GC: The number of times PGMSyncPT() needed to allocate page tables. */
    STAMCOUNTER     StatGCSyncPTAlloc;
    /** GC: The number of times PGMSyncPT() detected conflicts. */
    STAMCOUNTER     StatGCSyncPTConflict;
    /** GC: The number of times PGMSyncPT() failed. */
    STAMCOUNTER     StatGCSyncPTFailed;
    /** GC: PGMGCInvalidatePage() profiling. */
    STAMPROFILE     StatGCInvalidatePage;
    /** GC: The number of times PGMGCInvalidatePage() was called for a 4KB page. */
    STAMCOUNTER     StatGCInvalidatePage4KBPages;
    /** GC: The number of times PGMGCInvalidatePage() was called for a 4MB page. */
    STAMCOUNTER     StatGCInvalidatePage4MBPages;
    /** GC: The number of times PGMGCInvalidatePage() skipped a 4MB page. */
    STAMCOUNTER     StatGCInvalidatePage4MBPagesSkip;
    /** GC: The number of times PGMGCInvalidatePage() was called for a not accessed page directory. */
    STAMCOUNTER     StatGCInvalidatePagePDNAs;
    /** GC: The number of times PGMGCInvalidatePage() was called for a not present page directory. */
    STAMCOUNTER     StatGCInvalidatePagePDNPs;
    /** GC: The number of times PGMGCInvalidatePage() was called for a page directory containing mappings (no conflict). */
    STAMCOUNTER     StatGCInvalidatePagePDMappings;
    /** GC: The number of times PGMGCInvalidatePage() was called for an out of sync page directory. */
    STAMCOUNTER     StatGCInvalidatePagePDOutOfSync;
    /** HC: The number of times PGMGCInvalidatePage() was skipped due to not present shw or pending pending SyncCR3. */
    STAMCOUNTER     StatGCInvalidatePageSkipped;
    /** GC: The number of times user page is out of sync was detected in GC. */
    STAMCOUNTER     StatGCPageOutOfSyncUser;
    /** GC: The number of times supervisor page is out of sync was detected in GC. */
    STAMCOUNTER     StatGCPageOutOfSyncSupervisor;
    /** GC: The number of dynamic page mapping cache hits */
    STAMCOUNTER     StatDynMapCacheMisses;
    /** GC: The number of dynamic page mapping cache misses */
    STAMCOUNTER     StatDynMapCacheHits;
    /** GC: The number of times pgmGCGuestPDWriteHandler() was successfully called. */
    STAMCOUNTER     StatGCGuestCR3WriteHandled;
    /** GC: The number of times pgmGCGuestPDWriteHandler() was called and we had to fall back to the recompiler. */
    STAMCOUNTER     StatGCGuestCR3WriteUnhandled;
    /** GC: The number of times pgmGCGuestPDWriteHandler() was called and a conflict was detected. */
    STAMCOUNTER     StatGCGuestCR3WriteConflict;
    /** GC: Number of out-of-sync handled pages. */
    STAMCOUNTER     StatHandlersOutOfSync;
    /** GC: Number of traps due to physical access handlers. */
    STAMCOUNTER     StatHandlersPhysical;
    /** GC: Number of traps due to virtual access handlers. */
    STAMCOUNTER     StatHandlersVirtual;
    /** GC: Number of traps due to virtual access handlers found by physical address. */
    STAMCOUNTER     StatHandlersVirtualByPhys;
    /** GC: Number of traps due to virtual access handlers found by virtual address (without proper physical flags). */
    STAMCOUNTER     StatHandlersVirtualUnmarked;
    /** GC: Number of traps due to access outside range of monitored page(s). */
    STAMCOUNTER     StatHandlersUnhandled;

    /** GC: The number of times pgmGCGuestROMWriteHandler() was successfully called. */
    STAMCOUNTER     StatGCGuestROMWriteHandled;
    /** GC: The number of times pgmGCGuestROMWriteHandler() was called and we had to fall back to the recompiler */
    STAMCOUNTER     StatGCGuestROMWriteUnhandled;

    /** HC: PGMR3InvalidatePage() profiling. */
    STAMPROFILE     StatHCInvalidatePage;
    /** HC: The number of times PGMR3InvalidatePage() was called for a 4KB page. */
    STAMCOUNTER     StatHCInvalidatePage4KBPages;
    /** HC: The number of times PGMR3InvalidatePage() was called for a 4MB page. */
    STAMCOUNTER     StatHCInvalidatePage4MBPages;
    /** HC: The number of times PGMR3InvalidatePage() skipped a 4MB page. */
    STAMCOUNTER     StatHCInvalidatePage4MBPagesSkip;
    /** HC: The number of times PGMR3InvalidatePage() was called for a not accessed page directory. */
    STAMCOUNTER     StatHCInvalidatePagePDNAs;
    /** HC: The number of times PGMR3InvalidatePage() was called for a not present page directory. */
    STAMCOUNTER     StatHCInvalidatePagePDNPs;
    /** HC: The number of times PGMR3InvalidatePage() was called for a page directory containing mappings (no conflict). */
    STAMCOUNTER     StatHCInvalidatePagePDMappings;
    /** HC: The number of times PGMGCInvalidatePage() was called for an out of sync page directory. */
    STAMCOUNTER     StatHCInvalidatePagePDOutOfSync;
    /** HC: The number of times PGMR3InvalidatePage() was skipped due to not present shw or pending pending SyncCR3. */
    STAMCOUNTER     StatHCInvalidatePageSkipped;
    /** HC: PGMR3SyncPT() profiling. */
    STAMPROFILE     StatHCSyncPT;
    /** HC: pgmr3SyncPTResolveConflict() profiling (includes the entire relocation). */
    STAMPROFILE     StatHCResolveConflict;
    /** HC: Number of times PGMR3CheckMappingConflicts() detected a conflict. */
    STAMCOUNTER     StatHCDetectedConflicts;
    /** HC: The total number of times pgmHCGuestPDWriteHandler() was called. */
    STAMCOUNTER     StatHCGuestPDWrite;
    /** HC: The number of times pgmHCGuestPDWriteHandler() detected a conflict */
    STAMCOUNTER     StatHCGuestPDWriteConflict;

    /** HC: The number of pages marked not present for accessed bit emulation. */
    STAMCOUNTER     StatHCAccessedPage;
    /** HC: The number of pages marked read-only for dirty bit tracking. */
    STAMCOUNTER     StatHCDirtyPage;
    /** HC: The number of pages marked read-only for dirty bit tracking. */
    STAMCOUNTER     StatHCDirtyPageBig;
    /** HC: The number of traps generated for dirty bit tracking. */
    STAMCOUNTER     StatHCDirtyPageTrap;
    /** HC: The number of pages already dirty or readonly. */
    STAMCOUNTER     StatHCDirtyPageSkipped;

    /** GC: The number of pages marked not present for accessed bit emulation. */
    STAMCOUNTER     StatGCAccessedPage;
    /** GC: The number of pages marked read-only for dirty bit tracking. */
    STAMCOUNTER     StatGCDirtyPage;
    /** GC: The number of pages marked read-only for dirty bit tracking. */
    STAMCOUNTER     StatGCDirtyPageBig;
    /** GC: The number of traps generated for dirty bit tracking. */
    STAMCOUNTER     StatGCDirtyPageTrap;
    /** GC: The number of pages already dirty or readonly. */
    STAMCOUNTER     StatGCDirtyPageSkipped;
    /** GC: The number of pages marked dirty because of write accesses. */
    STAMCOUNTER     StatGCDirtiedPage;
    /** GC: The number of pages already marked dirty because of write accesses. */
    STAMCOUNTER     StatGCPageAlreadyDirty;
    /** GC: The number of real pages faults during dirty bit tracking. */
    STAMCOUNTER     StatGCDirtyTrackRealPF;

    /** GC: Profiling of the PGMTrackDirtyBit() body */
    STAMPROFILE     StatGCDirtyBitTracking;
    /** HC: Profiling of the PGMTrackDirtyBit() body */
    STAMPROFILE     StatHCDirtyBitTracking;

    /** GC: Profiling of the PGMGstModifyPage() body */
    STAMPROFILE     StatGCGstModifyPage;
    /** HC: Profiling of the PGMGstModifyPage() body */
    STAMPROFILE     StatHCGstModifyPage;

    /** GC: The number of time we've marked a PD not present from SyncPage to virtualize the accessed bit. */
    STAMCOUNTER     StatGCSyncPagePDNAs;
    /** GC: The number of time we've encountered an out-of-sync PD in SyncPage. */
    STAMCOUNTER     StatGCSyncPagePDOutOfSync;
    /** HC: The number of time we've marked a PD not present from SyncPage to virtualize the accessed bit. */
    STAMCOUNTER     StatHCSyncPagePDNAs;
    /** HC: The number of time we've encountered an out-of-sync PD in SyncPage. */
    STAMCOUNTER     StatHCSyncPagePDOutOfSync;

    STAMCOUNTER StatSynPT4kGC;
    STAMCOUNTER StatSynPT4kHC;
    STAMCOUNTER StatSynPT4MGC;
    STAMCOUNTER StatSynPT4MHC;

    /** Profiling of the PGMFlushTLB() body. */
    STAMPROFILE StatFlushTLB;
    /** The number of times PGMFlushTLB was called with a new CR3, non-global. (switch) */
    STAMCOUNTER StatFlushTLBNewCR3;
    /** The number of times PGMFlushTLB was called with a new CR3, global. (switch) */
    STAMCOUNTER StatFlushTLBNewCR3Global;
    /** The number of times PGMFlushTLB was called with the same CR3, non-global. (flush) */
    STAMCOUNTER StatFlushTLBSameCR3;
    /** The number of times PGMFlushTLB was called with the same CR3, global. (flush) */
    STAMCOUNTER StatFlushTLBSameCR3Global;

    STAMPROFILE StatGCSyncCR3;                      /**< GC: PGMSyncCR3() profiling. */
    STAMPROFILE StatGCSyncCR3Handlers;              /**< GC: Profiling of the PGMSyncCR3() update handler section. */
    STAMPROFILE StatGCSyncCR3HandlerVirtualReset;   /**< GC: Profiling of the virtual handler resets. */
    STAMPROFILE StatGCSyncCR3HandlerVirtualUpdate;  /**< GC: Profiling of the virtual handler updates. */
    STAMCOUNTER StatGCSyncCR3Global;                /**< GC: The number of global CR3 syncs. */
    STAMCOUNTER StatGCSyncCR3NotGlobal;             /**< GC: The number of non-global CR3 syncs. */
    STAMCOUNTER StatGCSyncCR3DstFreed;              /**< GC: The number of times we've had to free a shadow entry. */
    STAMCOUNTER StatGCSyncCR3DstFreedSrcNP;         /**< GC: The number of times we've had to free a shadow entry for which the source entry was not present. */
    STAMCOUNTER StatGCSyncCR3DstNotPresent;         /**< GC: The number of times we've encountered a not present shadow entry for a present guest entry. */
    STAMCOUNTER StatGCSyncCR3DstSkippedGlobalPD;    /**< GC: The number of times a global page directory wasn't flushed. */
    STAMCOUNTER StatGCSyncCR3DstSkippedGlobalPT;    /**< GC: The number of times a page table with only global entries wasn't flushed. */
    STAMCOUNTER StatGCSyncCR3DstCacheHit;           /**< GC: The number of times we got some kind of cache hit on a page table. */

    STAMPROFILE StatHCSyncCR3;                      /**< HC: PGMSyncCR3() profiling. */
    STAMPROFILE StatHCSyncCR3Handlers;              /**< HC: Profiling of the PGMSyncCR3() update handler section. */
    STAMPROFILE StatHCSyncCR3HandlerVirtualReset;   /**< HC: Profiling of the virtual handler resets. */
    STAMPROFILE StatHCSyncCR3HandlerVirtualUpdate;  /**< HC: Profiling of the virtual handler updates. */
    STAMCOUNTER StatHCSyncCR3Global;                /**< HC: The number of global CR3 syncs. */
    STAMCOUNTER StatHCSyncCR3NotGlobal;             /**< HC: The number of non-global CR3 syncs. */
    STAMCOUNTER StatHCSyncCR3DstFreed;              /**< HC: The number of times we've had to free a shadow entry. */
    STAMCOUNTER StatHCSyncCR3DstFreedSrcNP;         /**< HC: The number of times we've had to free a shadow entry for which the source entry was not present. */
    STAMCOUNTER StatHCSyncCR3DstNotPresent;         /**< HC: The number of times we've encountered a not present shadow entry for a present guest entry. */
    STAMCOUNTER StatHCSyncCR3DstSkippedGlobalPD;    /**< HC: The number of times a global page directory wasn't flushed. */
    STAMCOUNTER StatHCSyncCR3DstSkippedGlobalPT;    /**< HC: The number of times a page table with only global entries wasn't flushed. */
    STAMCOUNTER StatHCSyncCR3DstCacheHit;           /**< HC: The number of times we got some kind of cache hit on a page table. */

    /** GC: Profiling of pgmHandlerVirtualFindByPhysAddr. */
    STAMPROFILE StatVirtHandleSearchByPhysGC;
    /** HC: Profiling of pgmHandlerVirtualFindByPhysAddr. */
    STAMPROFILE StatVirtHandleSearchByPhysHC;
    /** HC: The number of times PGMR3HandlerPhysicalReset is called. */
    STAMCOUNTER StatHandlePhysicalReset;

    STAMPROFILE StatCheckPageFault;
    STAMPROFILE StatLazySyncPT;
    STAMPROFILE StatMapping;
    STAMPROFILE StatOutOfSync;
    STAMPROFILE StatHandlers;
    STAMPROFILE StatEIPHandlers;
    STAMPROFILE StatHCPrefetch;

# ifdef PGMPOOL_WITH_GCPHYS_TRACKING
    /** The number of first time shadowings. */
    STAMCOUNTER StatTrackVirgin;
    /** The number of times switching to cRef2, i.e. the page is being shadowed by two PTs. */
    STAMCOUNTER StatTrackAliased;
    /** The number of times we're tracking using cRef2. */
    STAMCOUNTER StatTrackAliasedMany;
    /** The number of times we're hitting pages which has overflowed cRef2. */
    STAMCOUNTER StatTrackAliasedLots;
    /** The number of times the extent list grows to long. */
    STAMCOUNTER StatTrackOverflows;
    /** Profiling of SyncPageWorkerTrackDeref (expensive). */
    STAMPROFILE StatTrackDeref;
# endif

    /** Allocated mbs of guest ram */
    STAMCOUNTER     StatDynRamTotal;
    /** Nr of pgmr3PhysGrowRange calls. */
    STAMCOUNTER     StatDynRamGrow;

    STAMCOUNTER StatGCTrap0ePD[X86_PG_ENTRIES];
    STAMCOUNTER StatGCSyncPtPD[X86_PG_ENTRIES];
    STAMCOUNTER StatGCSyncPagePD[X86_PG_ENTRIES];
#endif
} PGM, *PPGM;


/** @name PGM::fSyncFlags Flags
 * @{
 */
/** Updates the MM_RAM_FLAGS_VIRTUAL_HANDLER page bit. */
#define PGM_SYNC_UPDATE_PAGE_BIT_VIRTUAL        BIT(0)
/** Always sync CR3. */
#define PGM_SYNC_ALWAYS                         BIT(1)
/** Check monitoring on next CR3 (re)load and invalidate page. */
#define PGM_SYNC_MONITOR_CR3                    BIT(2)
/** Clear the page pool (a light weight flush). */
#define PGM_SYNC_CLEAR_PGM_POOL                 BIT(8)
/** @} */


__BEGIN_DECLS

PGMGCDECL(int)  pgmGCGuestPDWriteHandler(PVM pVM, RTGCUINT uErrorCode, PCPUMCTXCORE pRegFrame, void *pvFault, RTGCPHYS GCPhysFault, void *pvUser);
PGMDECL(int)    pgmGuestROMWriteHandler(PVM pVM, RTGCUINT uErrorCode, PCPUMCTXCORE pRegFrame, void *pvFault, RTGCPHYS GCPhysFault, void *pvUser);
PGMGCDECL(int)  pgmCachePTWriteGC(PVM pVM, RTGCUINT uErrorCode, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault, RTGCPHYS GCPhysFault, void *pvUser);
int             pgmR3SyncPTResolveConflict(PVM pVM, PPGMMAPPING pMapping, PVBOXPD pPDSrc, int iPDOld);
PPGMMAPPING     pgmGetMapping(PVM pVM, RTGCPTR GCPtr);
void            pgmR3MapRelocate(PVM pVM, PPGMMAPPING pMapping, int iPDOld, int iPDNew);
int             pgmR3ChangeMode(PVM pVM, PGMMODE enmGuestMode);
int             pgmLock(PVM pVM);
void            pgmUnlock(PVM pVM);

void            pgmR3HandlerPhysicalUpdateAll(PVM pVM);
int             pgmHandlerVirtualFindByPhysAddr(PVM pVM, RTGCPHYS GCPhys, PPGMVIRTHANDLER *ppVirt, unsigned *piPage);
DECLCALLBACK(int) pgmHandlerVirtualResetOne(PAVLROGCPTRNODECORE pNode, void *pvUser);
#ifdef VBOX_STRICT
void            pgmHandlerVirtualDumpPhysPages(PVM pVM);
#else
# define pgmHandlerVirtualDumpPhysPages(a) do { } while (0)
#endif
DECLCALLBACK(void) pgmR3InfoHandlers(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);


#ifdef IN_RING3
int             pgmr3PhysGrowRange(PVM pVM, RTGCPHYS GCPhys);

int             pgmR3PoolInit(PVM pVM);
void            pgmR3PoolRelocate(PVM pVM);
void            pgmR3PoolReset(PVM pVM);

#endif
#ifdef IN_GC
void           *pgmGCPoolMapPage(PVM pVM, PPGMPOOLPAGE pPage);
#endif
int             pgmPoolAlloc(PVM pVM, RTGCPHYS GCPhys, PGMPOOLKIND enmKind, uint16_t iUser, uint16_t iUserTable, PPPGMPOOLPAGE ppPage);
PPGMPOOLPAGE    pgmPoolGetPageByHCPhys(PVM pVM, RTHCPHYS HCPhys);
void            pgmPoolFree(PVM pVM, RTHCPHYS HCPhys, uint16_t iUser, uint16_t iUserTable);
void            pgmPoolFreeByPage(PPGMPOOL pPool, PPGMPOOLPAGE pPage, uint16_t iUser, uint16_t iUserTable);
int             pgmPoolFlushPage(PPGMPOOL pPool, PPGMPOOLPAGE pPage);
void            pgmPoolFlushAll(PVM pVM);
void            pgmPoolClearAll(PVM pVM);
void            pgmPoolTrackFlushGCPhysPT(PVM pVM, PRTHCPHYS pHCPhys, uint16_t iShw, uint16_t cRefs);
void            pgmPoolTrackFlushGCPhysPTs(PVM pVM, PRTHCPHYS pHCPhys, uint16_t iPhysExt);
int             pgmPoolTrackFlushGCPhysPTsSlow(PVM pVM, PRTHCPHYS pHCPhys);
PPGMPOOLPHYSEXT pgmPoolTrackPhysExtAlloc(PVM pVM, uint16_t *piPhysExt);
void            pgmPoolTrackPhysExtFree(PVM pVM, uint16_t iPhysExt);
void            pgmPoolTrackPhysExtFreeList(PVM pVM, uint16_t iPhysExt);
uint16_t        pgmPoolTrackPhysExtAddref(PVM pVM, uint16_t u16, uint16_t iShwPT);
void            pgmPoolTrackPhysExtDerefGCPhys(PPGMPOOL pPool, PPGMPOOLPAGE pPage, PRTHCPHYS pHCPhys);
#ifdef PGMPOOL_WITH_MONITORING
# ifdef IN_RING3
void            pgmPoolMonitorChainChanging(PPGMPOOL pPool, PPGMPOOLPAGE pPage, RTGCPHYS GCPhysFault, RTHCPTR pvAddress, PDISCPUSTATE pCpu);
# else
void            pgmPoolMonitorChainChanging(PPGMPOOL pPool, PPGMPOOLPAGE pPage, RTGCPHYS GCPhysFault, RTGCPTR pvAddress, PDISCPUSTATE pCpu);
# endif
int             pgmPoolMonitorChainFlush(PPGMPOOL pPool, PPGMPOOLPAGE pPage);
void            pgmPoolMonitorModifiedInsert(PPGMPOOL pPool, PPGMPOOLPAGE pPage);
void            pgmPoolMonitorModifiedClearAll(PVM pVM);
int             pgmPoolMonitorMonitorCR3(PPGMPOOL pPool, uint16_t idxRoot, RTGCPHYS GCPhysCR3);
int             pgmPoolMonitorUnmonitorCR3(PPGMPOOL pPool, uint16_t idxRoot);
#endif

__END_DECLS


/**
 * Convert GC Phys to HC Phys.
 *
 * @returns VBox status.
 * @param   pPGM        PGM handle.
 * @param   GCPhys      The GC physical address.
 * @param   pHCPhys     Where to store the corresponding HC physical address.
 */
DECLINLINE(int) PGMRamGCPhys2HCPhys(PPGM pPGM, RTGCPHYS GCPhys, PRTHCPHYS pHCPhys)
{
    /*
     * Walk range list.
     */
    PPGMRAMRANGE pRam = CTXSUFF(pPGM->pRamRanges);
    while (pRam)
    {
        RTGCPHYS off = GCPhys - pRam->GCPhys;
        if (off < pRam->cb)
        {
            unsigned iPage = off >> PAGE_SHIFT;
            /* Physical chunk in dynamically allocated range not present? */
            if (RT_UNLIKELY(!(pRam->aHCPhys[iPage] & X86_PTE_PAE_PG_MASK)))
            {
#ifdef IN_RING3
                int rc = pgmr3PhysGrowRange(PGM2VM(pPGM), GCPhys);
#else
                int rc = CTXALLMID(VMM, CallHost)(PGM2VM(pPGM), VMMCALLHOST_PGM_RAM_GROW_RANGE, GCPhys);
#endif
                if (rc != VINF_SUCCESS)
                    return rc;
            }
            *pHCPhys = (pRam->aHCPhys[iPage] & X86_PTE_PAE_PG_MASK) | (off & PAGE_OFFSET_MASK);
            return VINF_SUCCESS;
        }

        pRam = CTXSUFF(pRam->pNext);
    }
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}


/**
 * Convert GC Phys to HC Virt.
 *
 * @returns VBox status.
 * @param   pPGM        PGM handle.
 * @param   GCPhys      The GC physical address.
 * @param   pHCPtr      Where to store the corresponding HC virtual address.
 */
DECLINLINE(int) PGMRamGCPhys2HCPtr(PPGM pPGM, RTGCPHYS GCPhys, PRTHCPTR pHCPtr)
{
    /*
     * Walk range list.
     */
    PPGMRAMRANGE pRam = CTXSUFF(pPGM->pRamRanges);
    while (pRam)
    {
        RTGCPHYS off = GCPhys - pRam->GCPhys;
        if (off < pRam->cb)
        {
            if (pRam->fFlags & MM_RAM_FLAGS_DYNAMIC_ALLOC)
            {
                unsigned idx = (off >> PGM_DYNAMIC_CHUNK_SHIFT);
                /* Physical chunk in dynamically allocated range not present? */
                if (RT_UNLIKELY(!CTXSUFF(pRam->pavHCChunk)[idx]))
                {
#ifdef IN_RING3
                    int rc = pgmr3PhysGrowRange(PGM2VM(pPGM), GCPhys);
#else
                    int rc = CTXALLMID(VMM, CallHost)(PGM2VM(pPGM), VMMCALLHOST_PGM_RAM_GROW_RANGE, GCPhys);
#endif
                    if (rc != VINF_SUCCESS)
                        return rc;
                }
                *pHCPtr = (RTHCPTR)((RTHCUINTPTR)CTXSUFF(pRam->pavHCChunk)[idx] + (off & PGM_DYNAMIC_CHUNK_OFFSET_MASK));
                return VINF_SUCCESS;
            }
            if (pRam->pvHC)
            {
                *pHCPtr = (RTHCPTR)((RTHCUINTPTR)pRam->pvHC + off);
                return VINF_SUCCESS;
            }
            return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
        }

        pRam = CTXSUFF(pRam->pNext);
    }
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}


/**
 * Convert GC Phys to HC Virt.
 *
 * @returns VBox status.
 * @param   PVM         VM handle.
 * @param   pRam        Ram range
 * @param   GCPhys      The GC physical address.
 * @param   pHCPtr      Where to store the corresponding HC virtual address.
 */
DECLINLINE(int) PGMRamGCPhys2HCPtr(PVM pVM, PPGMRAMRANGE pRam, RTGCPHYS GCPhys, PRTHCPTR pHCPtr)
{
    RTGCPHYS off = GCPhys - pRam->GCPhys;
    Assert(off < pRam->cb);

    if (pRam->fFlags & MM_RAM_FLAGS_DYNAMIC_ALLOC)
    {
        unsigned idx = (off >> PGM_DYNAMIC_CHUNK_SHIFT);
        /* Physical chunk in dynamically allocated range not present? */
        if (RT_UNLIKELY(!CTXSUFF(pRam->pavHCChunk)[idx]))
        {
#ifdef IN_RING3
            int rc = pgmr3PhysGrowRange(pVM, GCPhys);
#else
            int rc = CTXALLMID(VMM, CallHost)(pVM, VMMCALLHOST_PGM_RAM_GROW_RANGE, GCPhys);
#endif
            if (rc != VINF_SUCCESS)
                return rc;
        }
        *pHCPtr = (RTHCPTR)((RTHCUINTPTR)CTXSUFF(pRam->pavHCChunk)[idx] + (off & PGM_DYNAMIC_CHUNK_OFFSET_MASK));
        return VINF_SUCCESS;
    }
    if (pRam->pvHC)
    {
        *pHCPtr = (RTHCPTR)((RTHCUINTPTR)pRam->pvHC + off);
        return VINF_SUCCESS;
    }
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}


/**
 * Convert GC Phys to HC Virt and HC Phys.
 *
 * @returns VBox status.
 * @param   pPGM        PGM handle.
 * @param   GCPhys      The GC physical address.
 * @param   pHCPtr      Where to store the corresponding HC virtual address.
 * @param   pHCPhys     Where to store the HC Physical address and its flags.
 */
DECLINLINE(int) PGMRamGCPhys2HCPtrAndHCPhysWithFlags(PPGM pPGM, RTGCPHYS GCPhys, PRTHCPTR pHCPtr, PRTHCPHYS pHCPhys)
{
    /*
     * Walk range list.
     */
    PPGMRAMRANGE pRam = CTXSUFF(pPGM->pRamRanges);
    while (pRam)
    {
        RTGCPHYS off = GCPhys - pRam->GCPhys;
        if (off < pRam->cb)
        {
            unsigned iPage = off >> PAGE_SHIFT;
            /* Physical chunk in dynamically allocated range not present? */
            if (RT_UNLIKELY(!(pRam->aHCPhys[iPage] & X86_PTE_PAE_PG_MASK)))
            {
#ifdef IN_RING3
                int rc = pgmr3PhysGrowRange(PGM2VM(pPGM), GCPhys);
#else
                int rc = CTXALLMID(VMM, CallHost)(PGM2VM(pPGM), VMMCALLHOST_PGM_RAM_GROW_RANGE, GCPhys);
#endif
                if (rc != VINF_SUCCESS)
                    return rc;
            }
            *pHCPhys = pRam->aHCPhys[iPage];

            if (pRam->fFlags & MM_RAM_FLAGS_DYNAMIC_ALLOC)
            {
                unsigned idx = (off >> PGM_DYNAMIC_CHUNK_SHIFT);
                *pHCPtr = (RTHCPTR)((RTHCUINTPTR)CTXSUFF(pRam->pavHCChunk)[idx] + (off & PGM_DYNAMIC_CHUNK_OFFSET_MASK));
                return VINF_SUCCESS;
            }
            if (pRam->pvHC)
            {
                *pHCPtr = (RTHCPTR)((RTHCUINTPTR)pRam->pvHC + off);
                return VINF_SUCCESS;
            }
            *pHCPtr = 0;
            return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
        }

        pRam = CTXSUFF(pRam->pNext);
    }
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}


/**
 * Convert GC Phys page to a page entry pointer.
 *
 * This is used by code which may have to update the flags.
 *
 * @returns VBox status.
 * @param   pPGM        PGM handle.
 * @param   GCPhys      The GC physical address.
 * @param   ppHCPhys    Where to store the pointer to the page entry.
 */
DECLINLINE(int) PGMRamGCPhys2PagePtr(PPGM pPGM, RTGCPHYS GCPhys, PRTHCPHYS *ppHCPhys)
{
    /*
     * Walk range list.
     */
    PPGMRAMRANGE pRam = CTXSUFF(pPGM->pRamRanges);
    while (pRam)
    {
        RTGCPHYS off = GCPhys - pRam->GCPhys;
        if (off < pRam->cb)
        {
            unsigned iPage = off >> PAGE_SHIFT;
            /* Physical chunk in dynamically allocated range not present? */
            if (RT_UNLIKELY(!(pRam->aHCPhys[iPage] & X86_PTE_PAE_PG_MASK)))
            {
#ifdef IN_RING3
                int rc = pgmr3PhysGrowRange(PGM2VM(pPGM), GCPhys);
#else
                int rc = CTXALLMID(VMM, CallHost)(PGM2VM(pPGM), VMMCALLHOST_PGM_RAM_GROW_RANGE, GCPhys);
#endif
                if (rc != VINF_SUCCESS)
                    return rc;
            }
            *ppHCPhys = &pRam->aHCPhys[iPage];
            return VINF_SUCCESS;
        }

        pRam = CTXSUFF(pRam->pNext);
    }
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}


/**
 * Convert GC Phys page to HC Phys page and flags.
 *
 * @returns VBox status.
 * @param   pPGM        PGM handle.
 * @param   GCPhys      The GC physical address.
 * @param   pHCPhys     Where to store the corresponding HC physical address of the page
 *                      and the page flags.
 */
DECLINLINE(int) PGMRamGCPhys2HCPhysWithFlags(PPGM pPGM, RTGCPHYS GCPhys, PRTHCPHYS pHCPhys)
{
    /*
     * Walk range list.
     */
    PPGMRAMRANGE pRam = CTXSUFF(pPGM->pRamRanges);
    while (pRam)
    {
        RTGCPHYS off = GCPhys - pRam->GCPhys;
        if (off < pRam->cb)
        {
            unsigned iPage = off >> PAGE_SHIFT;
            /* Physical chunk in dynamically allocated range not present? */
            if (RT_UNLIKELY(!(pRam->aHCPhys[iPage] & X86_PTE_PAE_PG_MASK)))
            {
#ifdef IN_RING3
                int rc = pgmr3PhysGrowRange(PGM2VM(pPGM), GCPhys);
#else
                int rc = CTXALLMID(VMM, CallHost)(PGM2VM(pPGM), VMMCALLHOST_PGM_RAM_GROW_RANGE, GCPhys);
#endif
                if (rc != VINF_SUCCESS)
                    return rc;
            }
            *pHCPhys = pRam->aHCPhys[iPage];
            return VINF_SUCCESS;
        }

        pRam = CTXSUFF(pRam->pNext);
    }
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}


/**
 * Clears flags associated with a RAM address.
 *
 * @returns VBox status code.
 * @param   pPGM        PGM handle.
 * @param   GCPhys      Guest context physical address.
 * @param   fFlags      fFlags to clear. (Bits 0-11.)
 */
DECLINLINE(int) PGMRamFlagsClearByGCPhys(PPGM pPGM, RTGCPHYS GCPhys, unsigned fFlags)
{
    /*
     * Walk range list.
     */
    PPGMRAMRANGE pRam = CTXSUFF(pPGM->pRamRanges);
    while (pRam)
    {
        RTGCPHYS off = GCPhys - pRam->GCPhys;
        if (off < pRam->cb)
        {
            unsigned iPage = off >> PAGE_SHIFT;
            /* Physical chunk in dynamically allocated range not present? */
            if (RT_UNLIKELY(!(pRam->aHCPhys[iPage] & X86_PTE_PAE_PG_MASK)))
            {
#ifdef IN_RING3
                int rc = pgmr3PhysGrowRange(PGM2VM(pPGM), GCPhys);
#else
                int rc = CTXALLMID(VMM, CallHost)(PGM2VM(pPGM), VMMCALLHOST_PGM_RAM_GROW_RANGE, GCPhys);
#endif
                if (rc != VINF_SUCCESS)
                    return rc;
            }
            fFlags &= ~X86_PTE_PAE_PG_MASK;
            pRam->aHCPhys[iPage] &= ~(RTHCPHYS)fFlags;
            return VINF_SUCCESS;
        }

        pRam = CTXSUFF(pRam->pNext);
    }
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}


/**
 * Clears flags associated with a RAM address.
 *
 * @returns VBox status code.
 * @param   pPGM        PGM handle.
 * @param   GCPhys      Guest context physical address.
 * @param   fFlags      fFlags to clear. (Bits 0-11.)
 * @param   ppRamHint   Where to read and store the ram list hint.
 *                      The caller initializes this to NULL before the call.
 */
DECLINLINE(int) PGMRamFlagsClearByGCPhysWithHint(PPGM pPGM, RTGCPHYS GCPhys, unsigned fFlags, PPGMRAMRANGE *ppRamHint)
{
    /*
     * Check the hint.
     */
    PPGMRAMRANGE pRam = *ppRamHint;
    if (pRam)
    {
        RTGCPHYS off = GCPhys - pRam->GCPhys;
        if (off < pRam->cb)
        {
            unsigned iPage = off >> PAGE_SHIFT;
            /* Physical chunk in dynamically allocated range not present? */
            if (RT_UNLIKELY(!(pRam->aHCPhys[iPage] & X86_PTE_PAE_PG_MASK)))
            {
#ifdef IN_RING3
                int rc = pgmr3PhysGrowRange(PGM2VM(pPGM), GCPhys);
#else
                int rc = CTXALLMID(VMM, CallHost)(PGM2VM(pPGM), VMMCALLHOST_PGM_RAM_GROW_RANGE, GCPhys);
#endif
                if (rc != VINF_SUCCESS)
                    return rc;
            }
            fFlags &= ~X86_PTE_PAE_PG_MASK;
            pRam->aHCPhys[iPage] &= ~(RTHCPHYS)fFlags;
            return VINF_SUCCESS;
        }
    }

    /*
     * Walk range list.
     */
    pRam = CTXSUFF(pPGM->pRamRanges);
    while (pRam)
    {
        RTGCPHYS off = GCPhys - pRam->GCPhys;
        if (off < pRam->cb)
        {
            unsigned iPage = off >> PAGE_SHIFT;
            /* Physical chunk in dynamically allocated range not present? */
            if (RT_UNLIKELY(!(pRam->aHCPhys[iPage] & X86_PTE_PAE_PG_MASK)))
            {
#ifdef IN_RING3
                int rc = pgmr3PhysGrowRange(PGM2VM(pPGM), GCPhys);
#else
                int rc = CTXALLMID(VMM, CallHost)(PGM2VM(pPGM), VMMCALLHOST_PGM_RAM_GROW_RANGE, GCPhys);
#endif
                if (rc != VINF_SUCCESS)
                    return rc;
            }
            fFlags &= ~X86_PTE_PAE_PG_MASK;
            pRam->aHCPhys[iPage] &= ~(RTHCPHYS)fFlags;
            *ppRamHint = pRam;
            return VINF_SUCCESS;
        }

        pRam = CTXSUFF(pRam->pNext);
    }
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}

/**
 * Sets (bitwise OR) flags associated with a RAM address.
 *
 * @returns VBox status code.
 * @param   pPGM        PGM handle.
 * @param   GCPhys      Guest context physical address.
 * @param   fFlags      fFlags to set clear. (Bits 0-11.)
 */
DECLINLINE(int) PGMRamFlagsSetByGCPhys(PPGM pPGM, RTGCPHYS GCPhys, unsigned fFlags)
{
    /*
     * Walk range list.
     */
    PPGMRAMRANGE pRam = CTXSUFF(pPGM->pRamRanges);
    while (pRam)
    {
        RTGCPHYS off = GCPhys - pRam->GCPhys;
        if (off < pRam->cb)
        {
            unsigned iPage = off >> PAGE_SHIFT;
            /* Physical chunk in dynamically allocated range not present? */
            if (RT_UNLIKELY(!(pRam->aHCPhys[iPage] & X86_PTE_PAE_PG_MASK)))
            {
#ifdef IN_RING3
                int rc = pgmr3PhysGrowRange(PGM2VM(pPGM), GCPhys);
#else
                int rc = CTXALLMID(VMM, CallHost)(PGM2VM(pPGM), VMMCALLHOST_PGM_RAM_GROW_RANGE, GCPhys);
#endif
                if (rc != VINF_SUCCESS)
                    return rc;
            }
            fFlags &= ~X86_PTE_PAE_PG_MASK;
            pRam->aHCPhys[iPage] |= fFlags;
            return VINF_SUCCESS;
        }

        pRam = CTXSUFF(pRam->pNext);
    }
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}

/**
 * Sets (bitwise OR) flags associated with a RAM address.
 *
 * @returns VBox status code.
 * @param   pPGM        PGM handle.
 * @param   GCPhys      Guest context physical address.
 * @param   fFlags      fFlags to set clear. (Bits 0-11.)
 * @param   ppRamHint   Where to read and store the ram list hint.
 *                      The caller initializes this to NULL before the call.
 */
DECLINLINE(int) PGMRamFlagsSetByGCPhysWithHint(PPGM pPGM, RTGCPHYS GCPhys, unsigned fFlags, PPGMRAMRANGE *ppRamHint)
{
    /*
     * Check the hint.
     */
    PPGMRAMRANGE pRam = *ppRamHint;
    if (pRam)
    {
        RTGCPHYS off = GCPhys - pRam->GCPhys;
        if (off < pRam->cb)
        {
            unsigned iPage = off >> PAGE_SHIFT;
            /* Physical chunk in dynamically allocated range not present? */
            if (RT_UNLIKELY(!(pRam->aHCPhys[iPage] & X86_PTE_PAE_PG_MASK)))
            {
#ifdef IN_RING3
                int rc = pgmr3PhysGrowRange(PGM2VM(pPGM), GCPhys);
#else
                int rc = CTXALLMID(VMM, CallHost)(PGM2VM(pPGM), VMMCALLHOST_PGM_RAM_GROW_RANGE, GCPhys);
#endif
                if (rc != VINF_SUCCESS)
                    return rc;
            }
            fFlags &= ~X86_PTE_PAE_PG_MASK;
            pRam->aHCPhys[iPage] |= fFlags;
            return VINF_SUCCESS;
        }
    }

    /*
     * Walk range list.
     */
    pRam = CTXSUFF(pPGM->pRamRanges);
    while (pRam)
    {
        RTGCPHYS off = GCPhys - pRam->GCPhys;
        if (off < pRam->cb)
        {
            unsigned iPage = off >> PAGE_SHIFT;
            /* Physical chunk in dynamically allocated range not present? */
            if (RT_UNLIKELY(!(pRam->aHCPhys[iPage] & X86_PTE_PAE_PG_MASK)))
            {
#ifdef IN_RING3
                int rc = pgmr3PhysGrowRange(PGM2VM(pPGM), GCPhys);
#else
                int rc = CTXALLMID(VMM, CallHost)(PGM2VM(pPGM), VMMCALLHOST_PGM_RAM_GROW_RANGE, GCPhys);
#endif
                if (rc != VINF_SUCCESS)
                    return rc;
            }
            fFlags &= ~X86_PTE_PAE_PG_MASK;
            pRam->aHCPhys[iPage] |= fFlags;
            *ppRamHint = pRam;
            return VINF_SUCCESS;
        }

        pRam = CTXSUFF(pRam->pNext);
    }
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}


/**
 * Gets the page directory for the specified address.
 *
 * @returns Pointer to the page directory in question.
 * @returns NULL if the page directory is not present or on an invalid page.
 * @param   pPGM        Pointer to the PGM instance data.
 * @param   GCPtr       The address.
 */
DECLINLINE(PX86PDPAE) pgmGstGetPaePD(PPGM pPGM, RTGCUINTPTR GCPtr)
{
    const unsigned iPdPtr = GCPtr >> X86_PDPTR_SHIFT;
    if (CTXSUFF(pPGM->pGstPaePDPTR)->a[iPdPtr].n.u1Present)
    {
        if ((CTXSUFF(pPGM->pGstPaePDPTR)->a[iPdPtr].u & X86_PDPE_PG_MASK) == pPGM->aGCPhysGstPaePDs[iPdPtr])
            return CTXSUFF(pPGM->apGstPaePDs)[iPdPtr];

        /* cache is out-of-sync. */
        PX86PDPAE pPD;
        int rc = PGM_GCPHYS_2_PTR(PGM2VM(pPGM), CTXSUFF(pPGM->pGstPaePDPTR)->a[iPdPtr].u & X86_PDPE_PG_MASK, &pPD);
        if (VBOX_SUCCESS(rc))
            return pPD;
        AssertMsgFailed(("Impossible! rc=%d PDPE=%#llx\n", rc, CTXSUFF(pPGM->pGstPaePDPTR)->a[iPdPtr].u));
        /* returning NIL_RTGCPHYS is ok if we assume it's just an invalid page of some kind emualted as all 0s. */
    }
    return NULL;
}


/**
 * Gets the page directory entry for the specified address.
 *
 * @returns Pointer to the page directory entry in question.
 * @returns NULL if the page directory is not present or on an invalid page.
 * @param   pPGM        Pointer to the PGM instance data.
 * @param   GCPtr       The address.
 */
DECLINLINE(PX86PDEPAE) pgmGstGetPaePDEPtr(PPGM pPGM, RTGCUINTPTR GCPtr)
{
    const unsigned iPdPtr = GCPtr >> X86_PDPTR_SHIFT;
    if (CTXSUFF(pPGM->pGstPaePDPTR)->a[iPdPtr].n.u1Present)
    {
        const unsigned iPD = (GCPtr >> X86_PD_PAE_SHIFT) & X86_PD_PAE_MASK;
        if ((CTXSUFF(pPGM->pGstPaePDPTR)->a[iPdPtr].u & X86_PDPE_PG_MASK) == pPGM->aGCPhysGstPaePDs[iPdPtr])
            return &CTXSUFF(pPGM->apGstPaePDs)[iPdPtr]->a[iPD];

        /* cache is out-of-sync. */
        PX86PDPAE pPD;
        int rc = PGM_GCPHYS_2_PTR(PGM2VM(pPGM), CTXSUFF(pPGM->pGstPaePDPTR)->a[iPdPtr].u & X86_PDPE_PG_MASK, &pPD);
        if (VBOX_SUCCESS(rc))
            return &pPD->a[iPD];
        AssertMsgFailed(("Impossible! rc=%d PDPE=%#llx\n", rc, CTXSUFF(pPGM->pGstPaePDPTR)->a[iPdPtr].u));
        /* returning NIL_RTGCPHYS is ok if we assume it's just an invalid page or something which we'll emulate as all 0s. */
    }
    return NULL;
}


/**
 * Gets the page directory entry for the specified address.
 *
 * @returns The page directory entry in question.
 * @returns A non-present entry if the page directory is not present or on an invalid page.
 * @param   pPGM        Pointer to the PGM instance data.
 * @param   GCPtr       The address.
 */
DECLINLINE(uint64_t) pgmGstGetPaePDE(PPGM pPGM, RTGCUINTPTR GCPtr)
{
    const unsigned iPdPtr = GCPtr >> X86_PDPTR_SHIFT;
    if (CTXSUFF(pPGM->pGstPaePDPTR)->a[iPdPtr].n.u1Present)
    {
        const unsigned iPD = (GCPtr >> X86_PD_PAE_SHIFT) & X86_PD_PAE_MASK;
        if ((CTXSUFF(pPGM->pGstPaePDPTR)->a[iPdPtr].u & X86_PDPE_PG_MASK) == pPGM->aGCPhysGstPaePDs[iPdPtr])
            return CTXSUFF(pPGM->apGstPaePDs)[iPdPtr]->a[iPD].u;

        /* cache is out-of-sync. */
        PX86PDPAE pPD;
        int rc = PGM_GCPHYS_2_PTR(PGM2VM(pPGM), CTXSUFF(pPGM->pGstPaePDPTR)->a[iPdPtr].u & X86_PDPE_PG_MASK, &pPD);
        if (VBOX_SUCCESS(rc))
            return pPD->a[iPD].u;
        AssertMsgFailed(("Impossible! rc=%d PDPE=%#llx\n", rc, CTXSUFF(pPGM->pGstPaePDPTR)->a[iPdPtr].u));
    }
    return 0;
}


/**
 * Checks if any of the specified page flags are set for the given page.
 *
 * @returns true if any of the flags are set.
 * @returns false if all the flags are clear.
 * @param   pPGM        PGM handle.
 * @param   GCPhys      The GC physical address.
 * @param   fFlags      The flags to check for.
 */
DECLINLINE(bool) PGMRamTestFlags(PPGM pPGM, RTGCPHYS GCPhys, uint64_t fFlags)
{
    /*
     * Walk range list.
     */
    for (PPGMRAMRANGE pRam = CTXSUFF(pPGM->pRamRanges);
         pRam;
         pRam = CTXSUFF(pRam->pNext))
    {
        RTGCPHYS off = GCPhys - pRam->GCPhys;
        if (off < pRam->cb)
            return (pRam->aHCPhys[off >> PAGE_SHIFT] & fFlags) != 0;
    }
    return false;
}


/**
 * Gets the ram flags for a handler.
 *
 * @returns The ram flags.
 * @param   pCur        The physical handler in question.
 */
DECLINLINE(unsigned) pgmHandlerPhysicalCalcFlags(PPGMPHYSHANDLER pCur)
{
    switch (pCur->enmType)
    {
        case PGMPHYSHANDLERTYPE_PHYSICAL:
            return MM_RAM_FLAGS_PHYSICAL_HANDLER;

        case PGMPHYSHANDLERTYPE_PHYSICAL_WRITE:
            return MM_RAM_FLAGS_PHYSICAL_HANDLER | MM_RAM_FLAGS_PHYSICAL_WRITE;

        case PGMPHYSHANDLERTYPE_MMIO:
        case PGMPHYSHANDLERTYPE_PHYSICAL_ALL:
            return MM_RAM_FLAGS_PHYSICAL_HANDLER | MM_RAM_FLAGS_PHYSICAL_ALL;

        default:
            AssertFatalMsgFailed(("Invalid type %d\n", pCur->enmType));
    }
}


/**
 * Clears one physical page of a virtual handler
 *
 * @param   pPGM    Pointer to the PGM instance.
 * @param   pCur    Virtual handler structure
 * @param   iPage   Physical page index
 */
DECLINLINE(void) pgmHandlerVirtualClearPage(PPGM pPGM, PPGMVIRTHANDLER pCur, unsigned iPage)
{
    const PPGMPHYS2VIRTHANDLER pPhys2Virt = &pCur->aPhysToVirt[iPage];

    /*
     * Remove the node from the tree (it's supposed to be in the tree if we get here!).
     */
#ifdef VBOX_STRICT_PGM_HANDLER_VIRTUAL
    AssertReleaseMsg(pPhys2Virt->offNextAlias & PGMPHYS2VIRTHANDLER_IN_TREE,
                     ("pPhys2Virt=%p:{.Core.Key=%VGp, .Core.KeyLast=%VGp, .offVirtHandler=%#RX32, .offNextAlias=%#RX32}\n",
                      pPhys2Virt, pPhys2Virt->Core.Key, pPhys2Virt->Core.KeyLast, pPhys2Virt->offVirtHandler, pPhys2Virt->offNextAlias));
#endif
    if (pPhys2Virt->offNextAlias & PGMPHYS2VIRTHANDLER_IS_HEAD)
    {
        /* We're the head of the alias chain. */
        PPGMPHYS2VIRTHANDLER pRemove = (PPGMPHYS2VIRTHANDLER)RTAvlroGCPhysRemove(&pPGM->CTXSUFF(pTrees)->PhysToVirtHandlers, pPhys2Virt->Core.Key); NOREF(pRemove);
#ifdef VBOX_STRICT_PGM_HANDLER_VIRTUAL
        AssertReleaseMsg(pRemove != NULL,
                         ("pPhys2Virt=%p:{.Core.Key=%VGp, .Core.KeyLast=%VGp, .offVirtHandler=%#RX32, .offNextAlias=%#RX32}\n",
                          pPhys2Virt, pPhys2Virt->Core.Key, pPhys2Virt->Core.KeyLast, pPhys2Virt->offVirtHandler, pPhys2Virt->offNextAlias));
        AssertReleaseMsg(pRemove == pPhys2Virt,
                         ("wanted: pPhys2Virt=%p:{.Core.Key=%VGp, .Core.KeyLast=%VGp, .offVirtHandler=%#RX32, .offNextAlias=%#RX32}\n"
                          "   got:    pRemove=%p:{.Core.Key=%VGp, .Core.KeyLast=%VGp, .offVirtHandler=%#RX32, .offNextAlias=%#RX32}\n",
                          pPhys2Virt, pPhys2Virt->Core.Key, pPhys2Virt->Core.KeyLast, pPhys2Virt->offVirtHandler, pPhys2Virt->offNextAlias,
                          pRemove, pRemove->Core.Key, pRemove->Core.KeyLast, pRemove->offVirtHandler, pRemove->offNextAlias));
#endif
        if (pPhys2Virt->offNextAlias & PGMPHYS2VIRTHANDLER_OFF_MASK)
        {
            /* Insert the next list in the alias chain into the tree. */
            PPGMPHYS2VIRTHANDLER pNext = (PPGMPHYS2VIRTHANDLER)((intptr_t)pPhys2Virt + (pPhys2Virt->offNextAlias & PGMPHYS2VIRTHANDLER_OFF_MASK));
#ifdef VBOX_STRICT_PGM_HANDLER_VIRTUAL
            AssertReleaseMsg(pNext->offNextAlias & PGMPHYS2VIRTHANDLER_IN_TREE,
                             ("pNext=%p:{.Core.Key=%VGp, .Core.KeyLast=%VGp, .offVirtHandler=%#RX32, .offNextAlias=%#RX32}\n",
                             pNext, pNext->Core.Key, pNext->Core.KeyLast, pNext->offVirtHandler, pNext->offNextAlias));
#endif
            pNext->offNextAlias |= PGMPHYS2VIRTHANDLER_IS_HEAD;
            bool fRc = RTAvlroGCPhysInsert(&pPGM->CTXSUFF(pTrees)->PhysToVirtHandlers, &pNext->Core);
            AssertRelease(fRc);
        }
    }
    else
    {
        /* Locate the previous node in the alias chain. */
        PPGMPHYS2VIRTHANDLER pPrev = (PPGMPHYS2VIRTHANDLER)RTAvlroGCPhysGet(&pPGM->CTXSUFF(pTrees)->PhysToVirtHandlers, pPhys2Virt->Core.Key);
#ifdef VBOX_STRICT_PGM_HANDLER_VIRTUAL
        AssertReleaseMsg(pPrev != pPhys2Virt,
                         ("pPhys2Virt=%p:{.Core.Key=%VGp, .Core.KeyLast=%VGp, .offVirtHandler=%#RX32, .offNextAlias=%#RX32} pPrev=%p\n",
                          pPhys2Virt, pPhys2Virt->Core.Key, pPhys2Virt->Core.KeyLast, pPhys2Virt->offVirtHandler, pPhys2Virt->offNextAlias, pPrev));
#endif
        for (;;)
        {
            PPGMPHYS2VIRTHANDLER pNext = (PPGMPHYS2VIRTHANDLER)((intptr_t)pPrev + (pPrev->offNextAlias & PGMPHYS2VIRTHANDLER_OFF_MASK));
            if (pNext == pPhys2Virt)
            {
                /* unlink. */
                LogFlow(("pgmHandlerVirtualClearPage: removed %p:{.offNextAlias=%#RX32} from alias chain. prev %p:{.offNextAlias=%#RX32} [%VGp-%VGp]\n",
                         pPhys2Virt, pPhys2Virt->offNextAlias, pPrev, pPrev->offNextAlias, pPhys2Virt->Core.Key, pPhys2Virt->Core.KeyLast));
                if (!(pPhys2Virt->offNextAlias & PGMPHYS2VIRTHANDLER_OFF_MASK))
                    pPrev->offNextAlias &= ~PGMPHYS2VIRTHANDLER_OFF_MASK;
                else
                {
                    PPGMPHYS2VIRTHANDLER pNewNext = (PPGMPHYS2VIRTHANDLER)((intptr_t)pPhys2Virt + (pPhys2Virt->offNextAlias & PGMPHYS2VIRTHANDLER_OFF_MASK));
                    pPrev->offNextAlias = ((intptr_t)pNewNext - (intptr_t)pPrev)
                                        | (pPrev->offNextAlias & ~PGMPHYS2VIRTHANDLER_OFF_MASK);
                }
                break;
            }

            /* next */
            if (pNext == pPrev)
            {
#ifdef VBOX_STRICT_PGM_HANDLER_VIRTUAL
                AssertReleaseMsg(pNext != pPrev,
                                 ("pPhys2Virt=%p:{.Core.Key=%VGp, .Core.KeyLast=%VGp, .offVirtHandler=%#RX32, .offNextAlias=%#RX32} pPrev=%p\n",
                                  pPhys2Virt, pPhys2Virt->Core.Key, pPhys2Virt->Core.KeyLast, pPhys2Virt->offVirtHandler, pPhys2Virt->offNextAlias, pPrev));
#endif
                break;
            }
            pPrev = pNext;
        }
    }
    Log2(("PHYS2VIRT: Removing %VGp-%VGp %#RX32 %s\n",
          pPhys2Virt->Core.Key, pPhys2Virt->Core.KeyLast, pPhys2Virt->offNextAlias, HCSTRING(pCur->pszDesc)));
    pPhys2Virt->offNextAlias = 0;
    pPhys2Virt->Core.KeyLast = NIL_RTGCPHYS; /* require reinsert */

    /*
     * Clear the ram flags for this page.
     */
    int rc = PGMRamFlagsClearByGCPhys(pPGM, pPhys2Virt->Core.Key,
                                      MM_RAM_FLAGS_VIRTUAL_HANDLER | MM_RAM_FLAGS_VIRTUAL_ALL | MM_RAM_FLAGS_VIRTUAL_WRITE);
    AssertRC(rc);
}


/**
 * Internal worker for finding a 'in-use' shadow page give by it's physical address.
 *
 * @returns Pointer to the shadow page structure.
 * @param   pPool       The pool.
 * @param   HCPhys      The HC physical address of the shadow page.
 */
DECLINLINE(PPGMPOOLPAGE) pgmPoolGetPage(PPGMPOOL pPool, RTHCPHYS HCPhys)
{
    /*
     * Look up the page.
     */
    PPGMPOOLPAGE pPage = (PPGMPOOLPAGE)RTAvloHCPhysGet(&pPool->HCPhysTree, HCPhys & X86_PTE_PAE_PG_MASK);
    AssertFatalMsg(pPage && pPage->enmKind != PGMPOOLKIND_FREE, ("HCPhys=%VHp pPage=%p type=%d\n", HCPhys, pPage, (pPage) ? pPage->enmKind : 0));
    return pPage;
}


/**
 * Internal worker for finding a 'in-use' shadow page give by it's physical address.
 *
 * @returns Pointer to the shadow page structure.
 * @param   pPool       The pool.
 * @param   idx         The pool page index.
 */
DECLINLINE(PPGMPOOLPAGE) pgmPoolGetPageByIdx(PPGMPOOL pPool, unsigned idx)
{
    AssertFatalMsg(idx >= PGMPOOL_IDX_FIRST && idx < pPool->cCurPages, ("idx=%d\n", idx));
    return &pPool->aPages[idx];
}


#ifdef PGMPOOL_WITH_GCPHYS_TRACKING
/**
 * Clear references to guest physical memory.
 *
 * @param   pPool       The pool.
 * @param   pPage       The page.
 * @param   pHCPhys     Pointer to the aHCPhys entry in the ram range.
 */
DECLINLINE(void) pgmTrackDerefGCPhys(PPGMPOOL pPool, PPGMPOOLPAGE pPage, PRTHCPHYS pHCPhys)
{
    /*
     * Just deal with the simple case here.
     */
#ifdef LOG_ENABLED
    const RTHCPHYS HCPhysOrg = *pHCPhys;
#endif
    const unsigned cRefs = *pHCPhys >> MM_RAM_FLAGS_CREFS_SHIFT;
    if (cRefs == 1)
    {
        Assert(pPage->idx == ((*pHCPhys >> MM_RAM_FLAGS_IDX_SHIFT) & MM_RAM_FLAGS_IDX_MASK));
        *pHCPhys = *pHCPhys & MM_RAM_FLAGS_NO_REFS_MASK;
    }
    else
        pgmPoolTrackPhysExtDerefGCPhys(pPool, pPage, pHCPhys);
    LogFlow(("pgmTrackDerefGCPhys: *pHCPhys=%RHp -> %RHp\n", HCPhysOrg, *pHCPhys));
}
#endif


#ifdef PGMPOOL_WITH_CACHE
/**
 * Moves the page to the head of the age list.
 *
 * This is done when the cached page is used in one way or another.
 *
 * @param   pPool       The pool.
 * @param   pPage       The cached page.
 * @todo inline in PGMInternal.h!
 */
DECLINLINE(void) pgmPoolCacheUsed(PPGMPOOL pPool, PPGMPOOLPAGE pPage)
{
    /*
     * Move to the head of the age list.
     */
    if (pPage->iAgePrev != NIL_PGMPOOL_IDX)
    {
        /* unlink */
        pPool->aPages[pPage->iAgePrev].iAgeNext = pPage->iAgeNext;
        if (pPage->iAgeNext != NIL_PGMPOOL_IDX)
            pPool->aPages[pPage->iAgeNext].iAgePrev = pPage->iAgePrev;
        else
            pPool->iAgeTail = pPage->iAgePrev;

        /* insert at head */
        pPage->iAgePrev = NIL_PGMPOOL_IDX;
        pPage->iAgeNext = pPool->iAgeHead;
        Assert(pPage->iAgeNext != NIL_PGMPOOL_IDX); /* we would've already been head then */
        pPool->iAgeHead = pPage->idx;
        pPool->aPages[pPage->iAgeNext].iAgePrev = pPage->idx;
    }
}
#endif /* PGMPOOL_WITH_CACHE */

/**
 * Tells if mappings are to be put into the shadow page table or not
 *
 * @returns boolean result
 * @param   pVM         VM handle.
 */

DECLINLINE(bool) pgmMapAreMappingsEnabled(PPGM pPGM)
{
    return !pPGM->fDisableMappings;
}

/** @} */

#endif
