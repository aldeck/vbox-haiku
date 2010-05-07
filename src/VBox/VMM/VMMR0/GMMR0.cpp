/* $Id$ */
/** @file
 * GMM - Global Memory Manager.
 */

/*
 * Copyright (C) 2007 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/** @page pg_gmm    GMM - The Global Memory Manager
 *
 * As the name indicates, this component is responsible for global memory
 * management. Currently only guest RAM is allocated from the GMM, but this
 * may change to include shadow page tables and other bits later.
 *
 * Guest RAM is managed as individual pages, but allocated from the host OS
 * in chunks for reasons of portability / efficiency. To minimize the memory
 * footprint all tracking structure must be as small as possible without
 * unnecessary performance penalties.
 *
 * The allocation chunks has fixed sized, the size defined at compile time
 * by the #GMM_CHUNK_SIZE \#define.
 *
 * Each chunk is given an unquie ID. Each page also has a unique ID. The
 * relation ship between the two IDs is:
 * @code
 *  GMM_CHUNK_SHIFT = log2(GMM_CHUNK_SIZE / PAGE_SIZE);
 *  idPage = (idChunk << GMM_CHUNK_SHIFT) | iPage;
 * @endcode
 * Where iPage is the index of the page within the chunk. This ID scheme
 * permits for efficient chunk and page lookup, but it relies on the chunk size
 * to be set at compile time. The chunks are organized in an AVL tree with their
 * IDs being the keys.
 *
 * The physical address of each page in an allocation chunk is maintained by
 * the #RTR0MEMOBJ and obtained using #RTR0MemObjGetPagePhysAddr. There is no
 * need to duplicate this information (it'll cost 8-bytes per page if we did).
 *
 * So what do we need to track per page? Most importantly we need to know
 * which state the page is in:
 *   - Private - Allocated for (eventually) backing one particular VM page.
 *   - Shared  - Readonly page that is used by one or more VMs and treated
 *               as COW by PGM.
 *   - Free    - Not used by anyone.
 *
 * For the page replacement operations (sharing, defragmenting and freeing)
 * to be somewhat efficient, private pages needs to be associated with a
 * particular page in a particular VM.
 *
 * Tracking the usage of shared pages is impractical and expensive, so we'll
 * settle for a reference counting system instead.
 *
 * Free pages will be chained on LIFOs
 *
 * On 64-bit systems we will use a 64-bit bitfield per page, while on 32-bit
 * systems a 32-bit bitfield will have to suffice because of address space
 * limitations. The #GMMPAGE structure shows the details.
 *
 *
 * @section sec_gmm_alloc_strat Page Allocation Strategy
 *
 * The strategy for allocating pages has to take fragmentation and shared
 * pages into account, or we may end up with with 2000 chunks with only
 * a few pages in each. Shared pages cannot easily be reallocated because
 * of the inaccurate usage accounting (see above). Private pages can be
 * reallocated by a defragmentation thread in the same manner that sharing
 * is done.
 *
 * The first approach is to manage the free pages in two sets depending on
 * whether they are mainly for the allocation of shared or private pages.
 * In the initial implementation there will be almost no possibility for
 * mixing shared and private pages in the same chunk (only if we're really
 * stressed on memory), but when we implement forking of VMs and have to
 * deal with lots of COW pages it'll start getting kind of interesting.
 *
 * The sets are lists of chunks with approximately the same number of
 * free pages. Say the chunk size is 1MB, meaning 256 pages, and a set
 * consists of 16 lists. So, the first list will contain the chunks with
 * 1-7 free pages, the second covers 8-15, and so on. The chunks will be
 * moved between the lists as pages are freed up or allocated.
 *
 *
 * @section sec_gmm_costs       Costs
 *
 * The per page cost in kernel space is 32-bit plus whatever RTR0MEMOBJ
 * entails. In addition there is the chunk cost of approximately
 * (sizeof(RT0MEMOBJ) + sizof(CHUNK)) / 2^CHUNK_SHIFT bytes per page.
 *
 * On Windows the per page #RTR0MEMOBJ cost is 32-bit on 32-bit windows
 * and 64-bit on 64-bit windows (a PFN_NUMBER in the MDL). So, 64-bit per page.
 * The cost on Linux is identical, but here it's because of sizeof(struct page *).
 *
 *
 * @section sec_gmm_legacy      Legacy Mode for Non-Tier-1 Platforms
 *
 * In legacy mode the page source is locked user pages and not
 * #RTR0MemObjAllocPhysNC, this means that a page can only be allocated
 * by the VM that locked it. We will make no attempt at implementing
 * page sharing on these systems, just do enough to make it all work.
 *
 *
 * @subsection sub_gmm_locking  Serializing
 *
 * One simple fast mutex will be employed in the initial implementation, not
 * two as metioned in @ref subsec_pgmPhys_Serializing.
 *
 * @see @ref subsec_pgmPhys_Serializing
 *
 *
 * @section sec_gmm_overcommit  Memory Over-Commitment Management
 *
 * The GVM will have to do the system wide memory over-commitment
 * management. My current ideas are:
 *      - Per VM oc policy that indicates how much to initially commit
 *        to it and what to do in a out-of-memory situation.
 *      - Prevent overtaxing the host.
 *
 * There are some challenges here, the main ones are configurability and
 * security. Should we for instance permit anyone to request 100% memory
 * commitment? Who should be allowed to do runtime adjustments of the
 * config. And how to prevent these settings from being lost when the last
 * VM process exits? The solution is probably to have an optional root
 * daemon the will keep VMMR0.r0 in memory and enable the security measures.
 *
 *
 *
 * @section sec_gmm_numa  NUMA
 *
 * NUMA considerations will be designed and implemented a bit later.
 *
 * The preliminary guesses is that we will have to try allocate memory as
 * close as possible to the CPUs the VM is executed on (EMT and additional CPU
 * threads). Which means it's mostly about allocation and sharing policies.
 * Both the scheduler and allocator interface will to supply some NUMA info
 * and we'll need to have a way to calc access costs.
 *
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_GMM
#include <VBox/gmm.h>
#include "GMMR0Internal.h"
#include <VBox/gvm.h>
#include <VBox/log.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <iprt/avl.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/** Pointer to set of free chunks.  */
typedef struct GMMCHUNKFREESET *PGMMCHUNKFREESET;

/** Pointer to a GMM allocation chunk. */
typedef struct GMMCHUNK *PGMMCHUNK;

/**
 * The per-page tracking structure employed by the GMM.
 *
 * On 32-bit hosts we'll some trickery is necessary to compress all
 * the information into 32-bits. When the fSharedFree member is set,
 * the 30th bit decides whether it's a free page or not.
 *
 * Because of the different layout on 32-bit and 64-bit hosts, macros
 * are used to get and set some of the data.
 */
typedef union GMMPAGE
{
#if HC_ARCH_BITS == 64
    /** Unsigned integer view. */
    uint64_t u;

    /** The common view. */
    struct GMMPAGECOMMON
    {
        uint32_t    uStuff1 : 32;
        uint32_t    uStuff2 : 30;
        /** The page state. */
        uint32_t    u2State : 2;
    } Common;

    /** The view of a private page. */
    struct GMMPAGEPRIVATE
    {
        /** The guest page frame number. (Max addressable: 2 ^ 44 - 16) */
        uint32_t    pfn;
        /** The GVM handle. (64K VMs) */
        uint32_t    hGVM : 16;
        /** Reserved. */
        uint32_t    u16Reserved : 14;
        /** The page state. */
        uint32_t    u2State : 2;
    } Private;

    /** The view of a shared page. */
    struct GMMPAGESHARED
    {
        /** The host page frame number. (Max addressable: 2 ^ 44 - 16) */
        uint32_t    pfn;
        /** The reference count (64K VMs). */
        uint32_t    cRefs : 16;
        /** Reserved. Checksum or something? Two hGVMs for forking? */
        uint32_t    u14Reserved : 14;
        /** The page state. */
        uint32_t    u2State : 2;
    } Shared;

    /** The view of a free page. */
    struct GMMPAGEFREE
    {
        /** The index of the next page in the free list. UINT16_MAX is NIL. */
        uint16_t    iNext;
        /** Reserved. Checksum or something? */
        uint16_t    u16Reserved0;
        /** Reserved. Checksum or something? */
        uint32_t    u30Reserved1 : 30;
        /** The page state. */
        uint32_t    u2State : 2;
    } Free;

#else /* 32-bit */
    /** Unsigned integer view. */
    uint32_t u;

    /** The common view. */
    struct GMMPAGECOMMON
    {
        uint32_t    uStuff : 30;
        /** The page state. */
        uint32_t    u2State : 2;
    } Common;

    /** The view of a private page. */
    struct GMMPAGEPRIVATE
    {
        /** The guest page frame number. (Max addressable: 2 ^ 36) */
        uint32_t    pfn : 24;
        /** The GVM handle. (127 VMs) */
        uint32_t    hGVM : 7;
        /** The top page state bit, MBZ. */
        uint32_t    fZero : 1;
    } Private;

    /** The view of a shared page. */
    struct GMMPAGESHARED
    {
        /** The reference count. */
        uint32_t    cRefs : 30;
        /** The page state. */
        uint32_t    u2State : 2;
    } Shared;

    /** The view of a free page. */
    struct GMMPAGEFREE
    {
        /** The index of the next page in the free list. UINT16_MAX is NIL. */
        uint32_t    iNext : 16;
        /** Reserved. Checksum or something? */
        uint32_t    u14Reserved : 14;
        /** The page state. */
        uint32_t    u2State : 2;
    } Free;
#endif
} GMMPAGE;
AssertCompileSize(GMMPAGE, sizeof(RTHCUINTPTR));
/** Pointer to a GMMPAGE. */
typedef GMMPAGE *PGMMPAGE;


/** @name The Page States.
 * @{ */
/** A private page. */
#define GMM_PAGE_STATE_PRIVATE          0
/** A private page - alternative value used on the 32-bit implemenation.
 * This will never be used on 64-bit hosts. */
#define GMM_PAGE_STATE_PRIVATE_32       1
/** A shared page. */
#define GMM_PAGE_STATE_SHARED           2
/** A free page. */
#define GMM_PAGE_STATE_FREE             3
/** @} */


/** @def GMM_PAGE_IS_PRIVATE
 *
 * @returns true if private, false if not.
 * @param   pPage       The GMM page.
 */
#if HC_ARCH_BITS == 64
# define GMM_PAGE_IS_PRIVATE(pPage) ( (pPage)->Common.u2State == GMM_PAGE_STATE_PRIVATE )
#else
# define GMM_PAGE_IS_PRIVATE(pPage) ( (pPage)->Private.fZero == 0 )
#endif

/** @def GMM_PAGE_IS_SHARED
 *
 * @returns true if shared, false if not.
 * @param   pPage       The GMM page.
 */
#define GMM_PAGE_IS_SHARED(pPage)   ( (pPage)->Common.u2State == GMM_PAGE_STATE_SHARED )

/** @def GMM_PAGE_IS_FREE
 *
 * @returns true if free, false if not.
 * @param   pPage       The GMM page.
 */
#define GMM_PAGE_IS_FREE(pPage)     ( (pPage)->Common.u2State == GMM_PAGE_STATE_FREE )

/** @def GMM_PAGE_PFN_LAST
 * The last valid guest pfn range.
 * @remark Some of the values outside the range has special meaning,
 *         see GMM_PAGE_PFN_UNSHAREABLE.
 */
#if HC_ARCH_BITS == 64
# define GMM_PAGE_PFN_LAST           UINT32_C(0xfffffff0)
#else
# define GMM_PAGE_PFN_LAST           UINT32_C(0x00fffff0)
#endif
AssertCompile(GMM_PAGE_PFN_LAST        == (GMM_GCPHYS_LAST >> PAGE_SHIFT));

/** @def GMM_PAGE_PFN_UNSHAREABLE
 * Indicates that this page isn't used for normal guest memory and thus isn't shareable.
 */
#if HC_ARCH_BITS == 64
# define GMM_PAGE_PFN_UNSHAREABLE   UINT32_C(0xfffffff1)
#else
# define GMM_PAGE_PFN_UNSHAREABLE   UINT32_C(0x00fffff1)
#endif
AssertCompile(GMM_PAGE_PFN_UNSHAREABLE == (GMM_GCPHYS_UNSHAREABLE >> PAGE_SHIFT));


/**
 * A GMM allocation chunk ring-3 mapping record.
 *
 * This should really be associated with a session and not a VM, but
 * it's simpler to associated with a VM and cleanup with the VM object
 * is destroyed.
 */
typedef struct GMMCHUNKMAP
{
    /** The mapping object. */
    RTR0MEMOBJ      MapObj;
    /** The VM owning the mapping. */
    PGVM            pGVM;
} GMMCHUNKMAP;
/** Pointer to a GMM allocation chunk mapping. */
typedef struct GMMCHUNKMAP *PGMMCHUNKMAP;

typedef enum GMMCHUNKTYPE
{
    GMMCHUNKTYPE_INVALID        = 0,
    GMMCHUNKTYPE_NON_CONTINUOUS = 1,      /* 4 kb pages */
    GMMCHUNKTYPE_CONTINUOUS     = 2,      /* one 2 MB continuous physical range. */
    GMMCHUNKTYPE_32BIT_HACK     = 0x7fffffff
} GMMCHUNKTYPE;


/**
 * A GMM allocation chunk.
 */
typedef struct GMMCHUNK
{
    /** The AVL node core.
     * The Key is the chunk ID. */
    AVLU32NODECORE      Core;
    /** The memory object.
     * Either from RTR0MemObjAllocPhysNC or RTR0MemObjLockUser depending on
     * what the host can dish up with. */
    RTR0MEMOBJ          MemObj;
    /** Pointer to the next chunk in the free list. */
    PGMMCHUNK           pFreeNext;
    /** Pointer to the previous chunk in the free list. */
    PGMMCHUNK           pFreePrev;
    /** Pointer to the free set this chunk belongs to. NULL for
     * chunks with no free pages. */
    PGMMCHUNKFREESET    pSet;
    /** Pointer to an array of mappings. */
    PGMMCHUNKMAP        paMappings;
    /** The number of mappings. */
    uint16_t            cMappings;
    /** The head of the list of free pages. UINT16_MAX is the NIL value. */
    uint16_t            iFreeHead;
    /** The number of free pages. */
    uint16_t            cFree;
    /** The GVM handle of the VM that first allocated pages from this chunk, this
     * is used as a preference when there are several chunks to choose from.
     * When in bound memory mode this isn't a preference any longer. */
    uint16_t            hGVM;
    /** The number of private pages. */
    uint16_t            cPrivate;
    /** The number of shared pages. */
    uint16_t            cShared;
    /** Chunk type */
    GMMCHUNKTYPE        enmType;
    /** The pages. */
    GMMPAGE             aPages[GMM_CHUNK_SIZE >> PAGE_SHIFT];
} GMMCHUNK;


/**
 * An allocation chunk TLB entry.
 */
typedef struct GMMCHUNKTLBE
{
    /** The chunk id. */
    uint32_t        idChunk;
    /** Pointer to the chunk. */
    PGMMCHUNK       pChunk;
} GMMCHUNKTLBE;
/** Pointer to an allocation chunk TLB entry. */
typedef GMMCHUNKTLBE *PGMMCHUNKTLBE;


/** The number of entries tin the allocation chunk TLB. */
#define GMM_CHUNKTLB_ENTRIES        32
/** Gets the TLB entry index for the given Chunk ID. */
#define GMM_CHUNKTLB_IDX(idChunk)   ( (idChunk) & (GMM_CHUNKTLB_ENTRIES - 1) )

/**
 * An allocation chunk TLB.
 */
typedef struct GMMCHUNKTLB
{
    /** The TLB entries. */
    GMMCHUNKTLBE    aEntries[GMM_CHUNKTLB_ENTRIES];
} GMMCHUNKTLB;
/** Pointer to an allocation chunk TLB. */
typedef GMMCHUNKTLB *PGMMCHUNKTLB;


/** The GMMCHUNK::cFree shift count. */
#define GMM_CHUNK_FREE_SET_SHIFT    4
/** The GMMCHUNK::cFree mask for use when considering relinking a chunk. */
#define GMM_CHUNK_FREE_SET_MASK     15
/** The number of lists in set. */
#define GMM_CHUNK_FREE_SET_LISTS    (GMM_CHUNK_NUM_PAGES >> GMM_CHUNK_FREE_SET_SHIFT)

/**
 * A set of free chunks.
 */
typedef struct GMMCHUNKFREESET
{
    /** The number of free pages in the set. */
    uint64_t        cFreePages;
    /** Chunks ordered by increasing number of free pages. */
    PGMMCHUNK       apLists[GMM_CHUNK_FREE_SET_LISTS];
} GMMCHUNKFREESET;


/**
 * The GMM instance data.
 */
typedef struct GMM
{
    /** Magic / eye catcher. GMM_MAGIC */
    uint32_t            u32Magic;
    /** The fast mutex protecting the GMM.
     * More fine grained locking can be implemented later if necessary. */
    RTSEMFASTMUTEX      Mtx;
    /** The chunk tree. */
    PAVLU32NODECORE     pChunks;
    /** The chunk TLB. */
    GMMCHUNKTLB         ChunkTLB;
    /** The private free set. */
    GMMCHUNKFREESET     Private;
    /** The shared free set. */
    GMMCHUNKFREESET     Shared;

    /** Shared module tree (global). */
    /** todo seperate trees for distinctly different guest OSes. */
    PAVLGCPTRNODECORE   pGlobalSharedModuleTree;

    /** The maximum number of pages we're allowed to allocate.
     * @gcfgm   64-bit GMM/MaxPages Direct.
     * @gcfgm   32-bit GMM/PctPages Relative to the number of host pages. */
    uint64_t            cMaxPages;
    /** The number of pages that has been reserved.
     * The deal is that cReservedPages - cOverCommittedPages <= cMaxPages. */
    uint64_t            cReservedPages;
    /** The number of pages that we have over-committed in reservations. */
    uint64_t            cOverCommittedPages;
    /** The number of actually allocated (committed if you like) pages. */
    uint64_t            cAllocatedPages;
    /** The number of pages that are shared. A subset of cAllocatedPages. */
    uint64_t            cSharedPages;
    /** The number of pages that are shared that has been left behind by
     * VMs not doing proper cleanups. */
    uint64_t            cLeftBehindSharedPages;
    /** The number of allocation chunks.
     * (The number of pages we've allocated from the host can be derived from this.) */
    uint32_t            cChunks;
    /** The number of current ballooned pages. */
    uint64_t            cBalloonedPages;

    /** The legacy allocation mode indicator.
     * This is determined at initialization time. */
    bool                fLegacyAllocationMode;
    /** The bound memory mode indicator.
     * When set, the memory will be bound to a specific VM and never
     * shared. This is always set if fLegacyAllocationMode is set.
     * (Also determined at initialization time.) */
    bool                fBoundMemoryMode;
    /** The number of registered VMs. */
    uint16_t            cRegisteredVMs;

    /** The previous allocated Chunk ID.
     * Used as a hint to avoid scanning the whole bitmap. */
    uint32_t            idChunkPrev;
    /** Chunk ID allocation bitmap.
     * Bits of allocated IDs are set, free ones are clear.
     * The NIL id (0) is marked allocated. */
    uint32_t            bmChunkId[(GMM_CHUNKID_LAST + 1 + 31) / 32];
} GMM;
/** Pointer to the GMM instance. */
typedef GMM *PGMM;

/** The value of GMM::u32Magic (Katsuhiro Otomo). */
#define GMM_MAGIC       0x19540414


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Pointer to the GMM instance data. */
static PGMM g_pGMM = NULL;

/** Macro for obtaining and validating the g_pGMM pointer.
 * On failure it will return from the invoking function with the specified return value.
 *
 * @param   pGMM    The name of the pGMM variable.
 * @param   rc      The return value on failure. Use VERR_INTERNAL_ERROR for
 *                  VBox status codes.
 */
#define GMM_GET_VALID_INSTANCE(pGMM, rc) \
    do { \
        (pGMM) = g_pGMM; \
        AssertPtrReturn((pGMM), (rc)); \
        AssertMsgReturn((pGMM)->u32Magic == GMM_MAGIC, ("%p - %#x\n", (pGMM), (pGMM)->u32Magic), (rc)); \
    } while (0)

/** Macro for obtaining and validating the g_pGMM pointer, void function variant.
 * On failure it will return from the invoking function.
 *
 * @param   pGMM    The name of the pGMM variable.
 */
#define GMM_GET_VALID_INSTANCE_VOID(pGMM) \
    do { \
        (pGMM) = g_pGMM; \
        AssertPtrReturnVoid((pGMM)); \
        AssertMsgReturnVoid((pGMM)->u32Magic == GMM_MAGIC, ("%p - %#x\n", (pGMM), (pGMM)->u32Magic)); \
    } while (0)


/** @def GMM_CHECK_SANITY_UPON_ENTERING
 * Checks the sanity of the GMM instance data before making changes.
 *
 * This is macro is a stub by default and must be enabled manually in the code.
 *
 * @returns true if sane, false if not.
 * @param   pGMM    The name of the pGMM variable.
 */
#if defined(VBOX_STRICT) && 0
# define GMM_CHECK_SANITY_UPON_ENTERING(pGMM)   (gmmR0SanityCheck((pGMM), __PRETTY_FUNCTION__, __LINE__) == 0)
#else
# define GMM_CHECK_SANITY_UPON_ENTERING(pGMM)   (true)
#endif

/** @def GMM_CHECK_SANITY_UPON_LEAVING
 * Checks the sanity of the GMM instance data after making changes.
 *
 * This is macro is a stub by default and must be enabled manually in the code.
 *
 * @returns true if sane, false if not.
 * @param   pGMM    The name of the pGMM variable.
 */
#if defined(VBOX_STRICT) && 0
# define GMM_CHECK_SANITY_UPON_LEAVING(pGMM)    (gmmR0SanityCheck((pGMM), __PRETTY_FUNCTION__, __LINE__) == 0)
#else
# define GMM_CHECK_SANITY_UPON_LEAVING(pGMM)    (true)
#endif

/** @def GMM_CHECK_SANITY_IN_LOOPS
 * Checks the sanity of the GMM instance in the allocation loops.
 *
 * This is macro is a stub by default and must be enabled manually in the code.
 *
 * @returns true if sane, false if not.
 * @param   pGMM    The name of the pGMM variable.
 */
#if defined(VBOX_STRICT) && 0
# define GMM_CHECK_SANITY_IN_LOOPS(pGMM)        (gmmR0SanityCheck((pGMM), __PRETTY_FUNCTION__, __LINE__) == 0)
#else
# define GMM_CHECK_SANITY_IN_LOOPS(pGMM)        (true)
#endif


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static DECLCALLBACK(int) gmmR0TermDestroyChunk(PAVLU32NODECORE pNode, void *pvGMM);
static DECLCALLBACK(int) gmmR0CleanupVMScanChunk(PAVLU32NODECORE pNode, void *pvGMM);
static DECLCALLBACK(int) gmmR0CleanupSharedModule(PAVLGCPTRNODECORE pNode, void *pvGVM);
/*static*/ DECLCALLBACK(int) gmmR0CleanupVMDestroyChunk(PAVLU32NODECORE pNode, void *pvGVM);
DECLINLINE(void) gmmR0LinkChunk(PGMMCHUNK pChunk, PGMMCHUNKFREESET pSet);
DECLINLINE(void) gmmR0UnlinkChunk(PGMMCHUNK pChunk);
static uint32_t gmmR0SanityCheck(PGMM pGMM, const char *pszFunction, unsigned uLineNo);
static void gmmR0FreeChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk);
static void gmmR0FreeSharedPage(PGMM pGMM, uint32_t idPage, PGMMPAGE pPage);
static int gmmR0UnmapChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk);



/**
 * Initializes the GMM component.
 *
 * This is called when the VMMR0.r0 module is loaded and protected by the
 * loader semaphore.
 *
 * @returns VBox status code.
 */
GMMR0DECL(int) GMMR0Init(void)
{
    LogFlow(("GMMInit:\n"));

    /*
     * Allocate the instance data and the lock(s).
     */
    PGMM pGMM = (PGMM)RTMemAllocZ(sizeof(*pGMM));
    if (!pGMM)
        return VERR_NO_MEMORY;
    pGMM->u32Magic = GMM_MAGIC;
    for (unsigned i = 0; i < RT_ELEMENTS(pGMM->ChunkTLB.aEntries); i++)
        pGMM->ChunkTLB.aEntries[i].idChunk = NIL_GMM_CHUNKID;
    ASMBitSet(&pGMM->bmChunkId[0], NIL_GMM_CHUNKID);

    int rc = RTSemFastMutexCreate(&pGMM->Mtx);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check and see if RTR0MemObjAllocPhysNC works.
         */
#if 0 /* later, see #3170. */
        RTR0MEMOBJ MemObj;
        rc = RTR0MemObjAllocPhysNC(&MemObj, _64K, NIL_RTHCPHYS);
        if (RT_SUCCESS(rc))
        {
            rc = RTR0MemObjFree(MemObj, true);
            AssertRC(rc);
        }
        else if (rc == VERR_NOT_SUPPORTED)
            pGMM->fLegacyAllocationMode = pGMM->fBoundMemoryMode = true;
        else
            SUPR0Printf("GMMR0Init: RTR0MemObjAllocPhysNC(,64K,Any) -> %d!\n", rc);
#else
# if defined(RT_OS_WINDOWS) || defined(RT_OS_SOLARIS) || defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)
        pGMM->fLegacyAllocationMode = false;
#  if ARCH_BITS == 32
        /* Don't reuse possibly partial chunks because of the virtual address space limitation. */
        pGMM->fBoundMemoryMode = true;
#  else
        pGMM->fBoundMemoryMode = false;
#  endif
# else
        pGMM->fLegacyAllocationMode = true;
        pGMM->fBoundMemoryMode = true;
# endif
#endif

        /*
         * Query system page count and guess a reasonable cMaxPages value.
         */
        pGMM->cMaxPages = UINT32_MAX; /** @todo IPRT function for query ram size and such. */

        g_pGMM = pGMM;
        LogFlow(("GMMInit: pGMM=%p fLegacyAllocationMode=%RTbool fBoundMemoryMode=%RTbool\n", pGMM, pGMM->fLegacyAllocationMode, pGMM->fBoundMemoryMode));
        return VINF_SUCCESS;
    }

    RTMemFree(pGMM);
    SUPR0Printf("GMMR0Init: failed! rc=%d\n", rc);
    return rc;
}


/**
 * Terminates the GMM component.
 */
GMMR0DECL(void) GMMR0Term(void)
{
    LogFlow(("GMMTerm:\n"));

    /*
     * Take care / be paranoid...
     */
    PGMM pGMM = g_pGMM;
    if (!VALID_PTR(pGMM))
        return;
    if (pGMM->u32Magic != GMM_MAGIC)
    {
        SUPR0Printf("GMMR0Term: u32Magic=%#x\n", pGMM->u32Magic);
        return;
    }

    /*
     * Undo what init did and free all the resources we've acquired.
     */
    /* Destroy the fundamentals. */
    g_pGMM = NULL;
    pGMM->u32Magic++;
    RTSemFastMutexDestroy(pGMM->Mtx);
    pGMM->Mtx = NIL_RTSEMFASTMUTEX;

    /* free any chunks still hanging around. */
    RTAvlU32Destroy(&pGMM->pChunks, gmmR0TermDestroyChunk, pGMM);

    /* finally the instance data itself. */
    RTMemFree(pGMM);
    LogFlow(("GMMTerm: done\n"));
}


/**
 * RTAvlU32Destroy callback.
 *
 * @returns 0
 * @param   pNode   The node to destroy.
 * @param   pvGMM   The GMM handle.
 */
static DECLCALLBACK(int) gmmR0TermDestroyChunk(PAVLU32NODECORE pNode, void *pvGMM)
{
    PGMMCHUNK pChunk = (PGMMCHUNK)pNode;

    if (pChunk->cFree != (GMM_CHUNK_SIZE >> PAGE_SHIFT))
        SUPR0Printf("GMMR0Term: %p/%#x: cFree=%d cPrivate=%d cShared=%d cMappings=%d\n", pChunk,
                    pChunk->Core.Key, pChunk->cFree, pChunk->cPrivate, pChunk->cShared, pChunk->cMappings);

    int rc = RTR0MemObjFree(pChunk->MemObj, true /* fFreeMappings */);
    if (RT_FAILURE(rc))
    {
        SUPR0Printf("GMMR0Term: %p/%#x: RTRMemObjFree(%p,true) -> %d (cMappings=%d)\n", pChunk,
                    pChunk->Core.Key, pChunk->MemObj, rc, pChunk->cMappings);
        AssertRC(rc);
    }
    pChunk->MemObj = NIL_RTR0MEMOBJ;

    RTMemFree(pChunk->paMappings);
    pChunk->paMappings = NULL;

    RTMemFree(pChunk);
    NOREF(pvGMM);
    return 0;
}


/**
 * Initializes the per-VM data for the GMM.
 *
 * This is called from within the GVMM lock (from GVMMR0CreateVM)
 * and should only initialize the data members so GMMR0CleanupVM
 * can deal with them. We reserve no memory or anything here,
 * that's done later in GMMR0InitVM.
 *
 * @param   pGVM    Pointer to the Global VM structure.
 */
GMMR0DECL(void) GMMR0InitPerVMData(PGVM pGVM)
{
    AssertCompile(RT_SIZEOFMEMB(GVM,gmm.s) <= RT_SIZEOFMEMB(GVM,gmm.padding));

    pGVM->gmm.s.enmPolicy = GMMOCPOLICY_INVALID;
    pGVM->gmm.s.enmPriority = GMMPRIORITY_INVALID;
    pGVM->gmm.s.fMayAllocate = false;
}


/**
 * Cleans up when a VM is terminating.
 *
 * @param   pGVM    Pointer to the Global VM structure.
 */
GMMR0DECL(void) GMMR0CleanupVM(PGVM pGVM)
{
    LogFlow(("GMMR0CleanupVM: pGVM=%p:{.pVM=%p, .hSelf=%#x}\n", pGVM, pGVM->pVM, pGVM->hSelf));

    PGMM pGMM;
    GMM_GET_VALID_INSTANCE_VOID(pGMM);

    int rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    GMM_CHECK_SANITY_UPON_ENTERING(pGMM);

#ifdef VBOX_WITH_PAGE_SHARING
    /* Clean up all registered shared modules. */
    RTAvlGCPtrDestroy(&pGVM->gmm.s.pSharedModuleTree, gmmR0CleanupSharedModule, pGVM);
#endif

    /*
     * The policy is 'INVALID' until the initial reservation
     * request has been serviced.
     */
    if (    pGVM->gmm.s.enmPolicy > GMMOCPOLICY_INVALID
        &&  pGVM->gmm.s.enmPolicy < GMMOCPOLICY_END)
    {
        /*
         * If it's the last VM around, we can skip walking all the chunk looking
         * for the pages owned by this VM and instead flush the whole shebang.
         *
         * This takes care of the eventuality that a VM has left shared page
         * references behind (shouldn't happen of course, but you never know).
         */
        Assert(pGMM->cRegisteredVMs);
        pGMM->cRegisteredVMs--;
#if 0 /* disabled so it won't hide bugs. */
        if (!pGMM->cRegisteredVMs)
        {
            RTAvlU32Destroy(&pGMM->pChunks, gmmR0CleanupVMDestroyChunk, pGMM);

            for (unsigned i = 0; i < RT_ELEMENTS(pGMM->ChunkTLB.aEntries); i++)
            {
                pGMM->ChunkTLB.aEntries[i].idChunk = NIL_GMM_CHUNKID;
                pGMM->ChunkTLB.aEntries[i].pChunk = NULL;
            }

            memset(&pGMM->Private, 0, sizeof(pGMM->Private));
            memset(&pGMM->Shared, 0, sizeof(pGMM->Shared));

            memset(&pGMM->bmChunkId[0], 0, sizeof(pGMM->bmChunkId));
            ASMBitSet(&pGMM->bmChunkId[0], NIL_GMM_CHUNKID);

            pGMM->cReservedPages = 0;
            pGMM->cOverCommittedPages = 0;
            pGMM->cAllocatedPages = 0;
            pGMM->cSharedPages = 0;
            pGMM->cLeftBehindSharedPages = 0;
            pGMM->cChunks = 0;
            pGMM->cBalloonedPages = 0;
        }
        else
#endif
        {
            /*
             * Walk the entire pool looking for pages that belongs to this VM
             * and left over mappings. (This'll only catch private pages, shared
             * pages will be 'left behind'.)
             */
            uint64_t cPrivatePages = pGVM->gmm.s.cPrivatePages; /* save */
            RTAvlU32DoWithAll(&pGMM->pChunks, true /* fFromLeft */, gmmR0CleanupVMScanChunk, pGVM);
            if (pGVM->gmm.s.cPrivatePages)
                SUPR0Printf("GMMR0CleanupVM: hGVM=%#x has %#x private pages that cannot be found!\n", pGVM->hSelf, pGVM->gmm.s.cPrivatePages);
            pGMM->cAllocatedPages -= cPrivatePages;

            /* free empty chunks. */
            if (cPrivatePages)
            {
                PGMMCHUNK pCur = pGMM->Private.apLists[RT_ELEMENTS(pGMM->Private.apLists) - 1];
                while (pCur)
                {
                    PGMMCHUNK pNext = pCur->pFreeNext;
                    if (    pCur->cFree == GMM_CHUNK_NUM_PAGES
                        &&  (   !pGMM->fBoundMemoryMode
                             || pCur->hGVM == pGVM->hSelf))
                        gmmR0FreeChunk(pGMM, pGVM, pCur);
                    pCur = pNext;
                }
            }

            /* account for shared pages that weren't freed. */
            if (pGVM->gmm.s.cSharedPages)
            {
                Assert(pGMM->cSharedPages >= pGVM->gmm.s.cSharedPages);
                SUPR0Printf("GMMR0CleanupVM: hGVM=%#x left %#x shared pages behind!\n", pGVM->hSelf, pGVM->gmm.s.cSharedPages);
                pGMM->cLeftBehindSharedPages += pGVM->gmm.s.cSharedPages;
            }

            /*
             * Update the over-commitment management statistics.
             */
            pGMM->cReservedPages -= pGVM->gmm.s.Reserved.cBasePages
                                  + pGVM->gmm.s.Reserved.cFixedPages
                                  + pGVM->gmm.s.Reserved.cShadowPages;
            switch (pGVM->gmm.s.enmPolicy)
            {
                case GMMOCPOLICY_NO_OC:
                    break;
                default:
                    /** @todo Update GMM->cOverCommittedPages */
                    break;
            }
        }
    }

    /* zap the GVM data. */
    pGVM->gmm.s.enmPolicy = GMMOCPOLICY_INVALID;
    pGVM->gmm.s.enmPriority = GMMPRIORITY_INVALID;
    pGVM->gmm.s.fMayAllocate = false;

    GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    RTSemFastMutexRelease(pGMM->Mtx);

    LogFlow(("GMMR0CleanupVM: returns\n"));
}


/**
 * RTAvlU32DoWithAll callback.
 *
 * @returns 0
 * @param   pNode   The node to search.
 * @param   pvGVM   Pointer to the shared VM structure.
 */
static DECLCALLBACK(int) gmmR0CleanupVMScanChunk(PAVLU32NODECORE pNode, void *pvGVM)
{
    PGMMCHUNK pChunk = (PGMMCHUNK)pNode;
    PGVM pGVM = (PGVM)pvGVM;

    /*
     * Look for pages belonging to the VM.
     * (Perform some internal checks while we're scanning.)
     */
#ifndef VBOX_STRICT
    if (pChunk->cFree != (GMM_CHUNK_SIZE >> PAGE_SHIFT))
#endif
    {
        unsigned cPrivate = 0;
        unsigned cShared = 0;
        unsigned cFree = 0;

        gmmR0UnlinkChunk(pChunk);       /* avoiding cFreePages updates. */

        uint16_t hGVM = pGVM->hSelf;
        unsigned iPage = (GMM_CHUNK_SIZE >> PAGE_SHIFT);
        while (iPage-- > 0)
            if (GMM_PAGE_IS_PRIVATE(&pChunk->aPages[iPage]))
            {
                if (pChunk->aPages[iPage].Private.hGVM == hGVM)
                {
                    /*
                     * Free the page.
                     *
                     * The reason for not using gmmR0FreePrivatePage here is that we
                     * must *not* cause the chunk to be freed from under us - we're in
                     * an AVL tree walk here.
                     */
                    pChunk->aPages[iPage].u = 0;
                    pChunk->aPages[iPage].Free.iNext = pChunk->iFreeHead;
                    pChunk->aPages[iPage].Free.u2State = GMM_PAGE_STATE_FREE;
                    pChunk->iFreeHead = iPage;
                    pChunk->cPrivate--;
                    pChunk->cFree++;
                    pGVM->gmm.s.cPrivatePages--;
                    cFree++;
                }
                else
                    cPrivate++;
            }
            else if (GMM_PAGE_IS_FREE(&pChunk->aPages[iPage]))
                cFree++;
            else
                cShared++;

        gmmR0LinkChunk(pChunk, pChunk->cShared ? &g_pGMM->Shared : &g_pGMM->Private);

        /*
         * Did it add up?
         */
        if (RT_UNLIKELY(    pChunk->cFree != cFree
                        ||  pChunk->cPrivate != cPrivate
                        ||  pChunk->cShared != cShared))
        {
            SUPR0Printf("gmmR0CleanupVMScanChunk: Chunk %p/%#x has bogus stats - free=%d/%d private=%d/%d shared=%d/%d\n",
                        pChunk->cFree, cFree, pChunk->cPrivate, cPrivate, pChunk->cShared, cShared);
            pChunk->cFree = cFree;
            pChunk->cPrivate = cPrivate;
            pChunk->cShared = cShared;
        }
    }

    /*
     * Look for the mapping belonging to the terminating VM.
     */
    for (unsigned i = 0; i < pChunk->cMappings; i++)
        if (pChunk->paMappings[i].pGVM == pGVM)
        {
            RTR0MEMOBJ MemObj = pChunk->paMappings[i].MapObj;

            pChunk->cMappings--;
            if (i < pChunk->cMappings)
                 pChunk->paMappings[i] = pChunk->paMappings[pChunk->cMappings];
            pChunk->paMappings[pChunk->cMappings].pGVM = NULL;
            pChunk->paMappings[pChunk->cMappings].MapObj = NIL_RTR0MEMOBJ;

            int rc = RTR0MemObjFree(MemObj, false /* fFreeMappings (NA) */);
            if (RT_FAILURE(rc))
            {
                SUPR0Printf("gmmR0CleanupVMScanChunk: %p/%#x: mapping #%x: RTRMemObjFree(%p,false) -> %d \n",
                            pChunk, pChunk->Core.Key, i, MemObj, rc);
                AssertRC(rc);
            }
            break;
        }

    /*
     * If not in bound memory mode, we should reset the hGVM field
     * if it has our handle in it.
     */
    if (pChunk->hGVM == pGVM->hSelf)
    {
        if (!g_pGMM->fBoundMemoryMode)
            pChunk->hGVM = NIL_GVM_HANDLE;
        else if (pChunk->cFree != GMM_CHUNK_NUM_PAGES)
        {
            SUPR0Printf("gmmR0CleanupVMScanChunk: %p/%#x: cFree=%#x - it should be 0 in bound mode!\n",
                        pChunk, pChunk->Core.Key, pChunk->cFree);
            AssertMsgFailed(("%p/%#x: cFree=%#x - it should be 0 in bound mode!\n", pChunk, pChunk->Core.Key, pChunk->cFree));

            gmmR0UnlinkChunk(pChunk);
            pChunk->cFree = GMM_CHUNK_NUM_PAGES;
            gmmR0LinkChunk(pChunk, pChunk->cShared ? &g_pGMM->Shared : &g_pGMM->Private);
        }
    }

    return 0;
}


/**
 * RTAvlU32Destroy callback for GMMR0CleanupVM.
 *
 * @returns 0
 * @param   pNode   The node (allocation chunk) to destroy.
 * @param   pvGVM   Pointer to the shared VM structure.
 */
/*static*/ DECLCALLBACK(int) gmmR0CleanupVMDestroyChunk(PAVLU32NODECORE pNode, void *pvGVM)
{
    PGMMCHUNK pChunk = (PGMMCHUNK)pNode;
    PGVM pGVM = (PGVM)pvGVM;

    for (unsigned i = 0; i < pChunk->cMappings; i++)
    {
        if (pChunk->paMappings[i].pGVM != pGVM)
            SUPR0Printf("gmmR0CleanupVMDestroyChunk: %p/%#x: mapping #%x: pGVM=%p exepcted %p\n", pChunk,
                        pChunk->Core.Key, i, pChunk->paMappings[i].pGVM, pGVM);
        int rc = RTR0MemObjFree(pChunk->paMappings[i].MapObj, false /* fFreeMappings (NA) */);
        if (RT_FAILURE(rc))
        {
            SUPR0Printf("gmmR0CleanupVMDestroyChunk: %p/%#x: mapping #%x: RTRMemObjFree(%p,false) -> %d \n", pChunk,
                        pChunk->Core.Key, i, pChunk->paMappings[i].MapObj, rc);
            AssertRC(rc);
        }
    }

    int rc = RTR0MemObjFree(pChunk->MemObj, true /* fFreeMappings */);
    if (RT_FAILURE(rc))
    {
        SUPR0Printf("gmmR0CleanupVMDestroyChunk: %p/%#x: RTRMemObjFree(%p,true) -> %d (cMappings=%d)\n", pChunk,
                    pChunk->Core.Key, pChunk->MemObj, rc, pChunk->cMappings);
        AssertRC(rc);
    }
    pChunk->MemObj = NIL_RTR0MEMOBJ;

    RTMemFree(pChunk->paMappings);
    pChunk->paMappings = NULL;

    RTMemFree(pChunk);
    return 0;
}


/**
 * The initial resource reservations.
 *
 * This will make memory reservations according to policy and priority. If there aren't
 * sufficient resources available to sustain the VM this function will fail and all
 * future allocations requests will fail as well.
 *
 * These are just the initial reservations made very very early during the VM creation
 * process and will be adjusted later in the GMMR0UpdateReservation call after the
 * ring-3 init has completed.
 *
 * @returns VBox status code.
 * @retval  VERR_GMM_MEMORY_RESERVATION_DECLINED
 * @retval  VERR_GMM_
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           VCPU id
 * @param   cBasePages      The number of pages that may be allocated for the base RAM and ROMs.
 *                          This does not include MMIO2 and similar.
 * @param   cShadowPages    The number of pages that may be allocated for shadow pageing structures.
 * @param   cFixedPages     The number of pages that may be allocated for fixed objects like the
 *                          hyper heap, MMIO2 and similar.
 * @param   enmPolicy       The OC policy to use on this VM.
 * @param   enmPriority     The priority in an out-of-memory situation.
 *
 * @thread  The creator thread / EMT.
 */
GMMR0DECL(int) GMMR0InitialReservation(PVM pVM, VMCPUID idCpu, uint64_t cBasePages, uint32_t cShadowPages, uint32_t cFixedPages,
                                       GMMOCPOLICY enmPolicy, GMMPRIORITY enmPriority)
{
    LogFlow(("GMMR0InitialReservation: pVM=%p cBasePages=%#llx cShadowPages=%#x cFixedPages=%#x enmPolicy=%d enmPriority=%d\n",
             pVM, cBasePages, cShadowPages, cFixedPages, enmPolicy, enmPriority));

    /*
     * Validate, get basics and take the semaphore.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    AssertReturn(cBasePages, VERR_INVALID_PARAMETER);
    AssertReturn(cShadowPages, VERR_INVALID_PARAMETER);
    AssertReturn(cFixedPages, VERR_INVALID_PARAMETER);
    AssertReturn(enmPolicy > GMMOCPOLICY_INVALID && enmPolicy < GMMOCPOLICY_END, VERR_INVALID_PARAMETER);
    AssertReturn(enmPriority > GMMPRIORITY_INVALID && enmPriority < GMMPRIORITY_END, VERR_INVALID_PARAMETER);

    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        if (    !pGVM->gmm.s.Reserved.cBasePages
            &&  !pGVM->gmm.s.Reserved.cFixedPages
            &&  !pGVM->gmm.s.Reserved.cShadowPages)
        {
            /*
             * Check if we can accomodate this.
             */
            /* ... later ... */
            if (RT_SUCCESS(rc))
            {
                /*
                 * Update the records.
                 */
                pGVM->gmm.s.Reserved.cBasePages = cBasePages;
                pGVM->gmm.s.Reserved.cFixedPages = cFixedPages;
                pGVM->gmm.s.Reserved.cShadowPages = cShadowPages;
                pGVM->gmm.s.enmPolicy = enmPolicy;
                pGVM->gmm.s.enmPriority = enmPriority;
                pGVM->gmm.s.fMayAllocate = true;

                pGMM->cReservedPages += cBasePages + cFixedPages + cShadowPages;
                pGMM->cRegisteredVMs++;
            }
        }
        else
            rc = VERR_WRONG_ORDER;
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_INTERNAL_ERROR_5;
    RTSemFastMutexRelease(pGMM->Mtx);
    LogFlow(("GMMR0InitialReservation: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0InitialReservation.
 *
 * @returns see GMMR0InitialReservation.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           VCPU id
 * @param   pReq            The request packet.
 */
GMMR0DECL(int) GMMR0InitialReservationReq(PVM pVM, VMCPUID idCpu, PGMMINITIALRESERVATIONREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GMMR0InitialReservation(pVM, idCpu, pReq->cBasePages, pReq->cShadowPages, pReq->cFixedPages, pReq->enmPolicy, pReq->enmPriority);
}


/**
 * This updates the memory reservation with the additional MMIO2 and ROM pages.
 *
 * @returns VBox status code.
 * @retval  VERR_GMM_MEMORY_RESERVATION_DECLINED
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           VCPU id
 * @param   cBasePages      The number of pages that may be allocated for the base RAM and ROMs.
 *                          This does not include MMIO2 and similar.
 * @param   cShadowPages    The number of pages that may be allocated for shadow pageing structures.
 * @param   cFixedPages     The number of pages that may be allocated for fixed objects like the
 *                          hyper heap, MMIO2 and similar.
 *
 * @thread  EMT.
 */
GMMR0DECL(int) GMMR0UpdateReservation(PVM pVM, VMCPUID idCpu, uint64_t cBasePages, uint32_t cShadowPages, uint32_t cFixedPages)
{
    LogFlow(("GMMR0UpdateReservation: pVM=%p cBasePages=%#llx cShadowPages=%#x cFixedPages=%#x\n",
             pVM, cBasePages, cShadowPages, cFixedPages));

    /*
     * Validate, get basics and take the semaphore.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    AssertReturn(cBasePages, VERR_INVALID_PARAMETER);
    AssertReturn(cShadowPages, VERR_INVALID_PARAMETER);
    AssertReturn(cFixedPages, VERR_INVALID_PARAMETER);

    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        if (    pGVM->gmm.s.Reserved.cBasePages
            &&  pGVM->gmm.s.Reserved.cFixedPages
            &&  pGVM->gmm.s.Reserved.cShadowPages)
        {
            /*
             * Check if we can accomodate this.
             */
            /* ... later ... */
            if (RT_SUCCESS(rc))
            {
                /*
                 * Update the records.
                 */
                pGMM->cReservedPages -= pGVM->gmm.s.Reserved.cBasePages
                                      + pGVM->gmm.s.Reserved.cFixedPages
                                      + pGVM->gmm.s.Reserved.cShadowPages;
                pGMM->cReservedPages += cBasePages + cFixedPages + cShadowPages;

                pGVM->gmm.s.Reserved.cBasePages = cBasePages;
                pGVM->gmm.s.Reserved.cFixedPages = cFixedPages;
                pGVM->gmm.s.Reserved.cShadowPages = cShadowPages;
            }
        }
        else
            rc = VERR_WRONG_ORDER;
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_INTERNAL_ERROR_5;
    RTSemFastMutexRelease(pGMM->Mtx);
    LogFlow(("GMMR0UpdateReservation: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0UpdateReservation.
 *
 * @returns see GMMR0UpdateReservation.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           VCPU id
 * @param   pReq            The request packet.
 */
GMMR0DECL(int) GMMR0UpdateReservationReq(PVM pVM, VMCPUID idCpu, PGMMUPDATERESERVATIONREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GMMR0UpdateReservation(pVM, idCpu, pReq->cBasePages, pReq->cShadowPages, pReq->cFixedPages);
}


/**
 * Performs sanity checks on a free set.
 *
 * @returns Error count.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pSet        Pointer to the set.
 * @param   pszSetName  The set name.
 * @param   pszFunction The function from which it was called.
 * @param   uLine       The line number.
 */
static uint32_t gmmR0SanityCheckSet(PGMM pGMM, PGMMCHUNKFREESET pSet, const char *pszSetName,
                                    const char *pszFunction, unsigned uLineNo)
{
    uint32_t cErrors = 0;

    /*
     * Count the free pages in all the chunks and match it against pSet->cFreePages.
     */
    uint32_t cPages = 0;
    for (unsigned i = 0; i < RT_ELEMENTS(pSet->apLists); i++)
    {
        for (PGMMCHUNK pCur = pSet->apLists[i]; pCur; pCur = pCur->pFreeNext)
        {
            /** @todo check that the chunk is hash into the right set. */
            cPages += pCur->cFree;
        }
    }
    if (RT_UNLIKELY(cPages != pSet->cFreePages))
    {
        SUPR0Printf("GMM insanity: found %#x pages in the %s set, expected %#x. (%s, line %u)\n",
                    cPages, pszSetName, pSet->cFreePages, pszFunction, uLineNo);
        cErrors++;
    }

    return cErrors;
}


/**
 * Performs some sanity checks on the GMM while owning lock.
 *
 * @returns Error count.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pszFunction The function from which it is called.
 * @param   uLineNo     The line number.
 */
static uint32_t gmmR0SanityCheck(PGMM pGMM, const char *pszFunction, unsigned uLineNo)
{
    uint32_t cErrors = 0;

    cErrors += gmmR0SanityCheckSet(pGMM, &pGMM->Private, "private", pszFunction, uLineNo);
    cErrors += gmmR0SanityCheckSet(pGMM, &pGMM->Shared,  "shared",  pszFunction, uLineNo);
    /** @todo add more sanity checks. */

    return cErrors;
}


/**
 * Looks up a chunk in the tree and fill in the TLB entry for it.
 *
 * This is not expected to fail and will bitch if it does.
 *
 * @returns Pointer to the allocation chunk, NULL if not found.
 * @param   pGMM        Pointer to the GMM instance.
 * @param   idChunk     The ID of the chunk to find.
 * @param   pTlbe       Pointer to the TLB entry.
 */
static PGMMCHUNK gmmR0GetChunkSlow(PGMM pGMM, uint32_t idChunk, PGMMCHUNKTLBE pTlbe)
{
    PGMMCHUNK pChunk = (PGMMCHUNK)RTAvlU32Get(&pGMM->pChunks, idChunk);
    AssertMsgReturn(pChunk, ("Chunk %#x not found!\n", idChunk), NULL);
    pTlbe->idChunk = idChunk;
    pTlbe->pChunk = pChunk;
    return pChunk;
}


/**
 * Finds a allocation chunk.
 *
 * This is not expected to fail and will bitch if it does.
 *
 * @returns Pointer to the allocation chunk, NULL if not found.
 * @param   pGMM        Pointer to the GMM instance.
 * @param   idChunk     The ID of the chunk to find.
 */
DECLINLINE(PGMMCHUNK) gmmR0GetChunk(PGMM pGMM, uint32_t idChunk)
{
    /*
     * Do a TLB lookup, branch if not in the TLB.
     */
    PGMMCHUNKTLBE pTlbe = &pGMM->ChunkTLB.aEntries[GMM_CHUNKTLB_IDX(idChunk)];
    if (    pTlbe->idChunk != idChunk
        ||  !pTlbe->pChunk)
        return gmmR0GetChunkSlow(pGMM, idChunk, pTlbe);
    return pTlbe->pChunk;
}


/**
 * Finds a page.
 *
 * This is not expected to fail and will bitch if it does.
 *
 * @returns Pointer to the page, NULL if not found.
 * @param   pGMM        Pointer to the GMM instance.
 * @param   idPage      The ID of the page to find.
 */
DECLINLINE(PGMMPAGE) gmmR0GetPage(PGMM pGMM, uint32_t idPage)
{
    PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, idPage >> GMM_CHUNKID_SHIFT);
    if (RT_LIKELY(pChunk))
        return &pChunk->aPages[idPage & GMM_PAGEID_IDX_MASK];
    return NULL;
}


/**
 * Unlinks the chunk from the free list it's currently on (if any).
 *
 * @param   pChunk      The allocation chunk.
 */
DECLINLINE(void) gmmR0UnlinkChunk(PGMMCHUNK pChunk)
{
    PGMMCHUNKFREESET pSet = pChunk->pSet;
    if (RT_LIKELY(pSet))
    {
        pSet->cFreePages -= pChunk->cFree;

        PGMMCHUNK pPrev = pChunk->pFreePrev;
        PGMMCHUNK pNext = pChunk->pFreeNext;
        if (pPrev)
            pPrev->pFreeNext = pNext;
        else
            pSet->apLists[(pChunk->cFree - 1) >> GMM_CHUNK_FREE_SET_SHIFT] = pNext;
        if (pNext)
            pNext->pFreePrev = pPrev;

        pChunk->pSet = NULL;
        pChunk->pFreeNext = NULL;
        pChunk->pFreePrev = NULL;
    }
    else
    {
        Assert(!pChunk->pFreeNext);
        Assert(!pChunk->pFreePrev);
        Assert(!pChunk->cFree);
    }
}


/**
 * Links the chunk onto the appropriate free list in the specified free set.
 *
 * If no free entries, it's not linked into any list.
 *
 * @param   pChunk      The allocation chunk.
 * @param   pSet        The free set.
 */
DECLINLINE(void) gmmR0LinkChunk(PGMMCHUNK pChunk, PGMMCHUNKFREESET pSet)
{
    Assert(!pChunk->pSet);
    Assert(!pChunk->pFreeNext);
    Assert(!pChunk->pFreePrev);

    if (pChunk->cFree > 0)
    {
        pChunk->pSet = pSet;
        pChunk->pFreePrev = NULL;
        unsigned iList = (pChunk->cFree - 1) >> GMM_CHUNK_FREE_SET_SHIFT;
        pChunk->pFreeNext = pSet->apLists[iList];
        if (pChunk->pFreeNext)
            pChunk->pFreeNext->pFreePrev = pChunk;
        pSet->apLists[iList] = pChunk;

        pSet->cFreePages += pChunk->cFree;
    }
}


/**
 * Frees a Chunk ID.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   idChunk     The Chunk ID to free.
 */
static void gmmR0FreeChunkId(PGMM pGMM, uint32_t idChunk)
{
    AssertReturnVoid(idChunk != NIL_GMM_CHUNKID);
    AssertMsg(ASMBitTest(&pGMM->bmChunkId[0], idChunk), ("%#x\n", idChunk));
    ASMAtomicBitClear(&pGMM->bmChunkId[0], idChunk);
}


/**
 * Allocates a new Chunk ID.
 *
 * @returns The Chunk ID.
 * @param   pGMM        Pointer to the GMM instance.
 */
static uint32_t gmmR0AllocateChunkId(PGMM pGMM)
{
    AssertCompile(!((GMM_CHUNKID_LAST + 1) & 31)); /* must be a multiple of 32 */
    AssertCompile(NIL_GMM_CHUNKID == 0);

    /*
     * Try the next sequential one.
     */
    int32_t idChunk = ++pGMM->idChunkPrev;
#if 0 /* test the fallback first */
    if (    idChunk <= GMM_CHUNKID_LAST
        &&  idChunk > NIL_GMM_CHUNKID
        &&  !ASMAtomicBitTestAndSet(&pVMM->bmChunkId[0], idChunk))
        return idChunk;
#endif

    /*
     * Scan sequentially from the last one.
     */
    if (    (uint32_t)idChunk < GMM_CHUNKID_LAST
        &&  idChunk > NIL_GMM_CHUNKID)
    {
        idChunk = ASMBitNextClear(&pGMM->bmChunkId[0], GMM_CHUNKID_LAST + 1, idChunk);
        if (idChunk > NIL_GMM_CHUNKID)
        {
            AssertMsgReturn(!ASMAtomicBitTestAndSet(&pGMM->bmChunkId[0], idChunk), ("%#x\n", idChunk), NIL_GMM_CHUNKID);
            return pGMM->idChunkPrev = idChunk;
        }
    }

    /*
     * Ok, scan from the start.
     * We're not racing anyone, so there is no need to expect failures or have restart loops.
     */
    idChunk = ASMBitFirstClear(&pGMM->bmChunkId[0], GMM_CHUNKID_LAST + 1);
    AssertMsgReturn(idChunk > NIL_GMM_CHUNKID, ("%#x\n", idChunk), NIL_GVM_HANDLE);
    AssertMsgReturn(!ASMAtomicBitTestAndSet(&pGMM->bmChunkId[0], idChunk), ("%#x\n", idChunk), NIL_GMM_CHUNKID);

    return pGMM->idChunkPrev = idChunk;
}


/**
 * Registers a new chunk of memory.
 *
 * This is called by both gmmR0AllocateOneChunk and GMMR0SeedChunk. The caller
 * must own the global lock.
 *
 * @returns VBox status code.
 * @param   pGMM            Pointer to the GMM instance.
 * @param   pSet            Pointer to the set.
 * @param   MemObj          The memory object for the chunk.
 * @param   hGVM            The affinity of the chunk. NIL_GVM_HANDLE for no
 *                          affinity.
 * @param   enmChunkType    Chunk type (continuous or non-continuous)
 * @param   ppChunk         Chunk address (out)
 */
static int gmmR0RegisterChunk(PGMM pGMM, PGMMCHUNKFREESET pSet, RTR0MEMOBJ MemObj, uint16_t hGVM, GMMCHUNKTYPE enmChunkType, PGMMCHUNK *ppChunk = NULL)
{
    Assert(hGVM != NIL_GVM_HANDLE || pGMM->fBoundMemoryMode);

    int rc;
    PGMMCHUNK pChunk = (PGMMCHUNK)RTMemAllocZ(sizeof(*pChunk));
    if (pChunk)
    {
        /*
         * Initialize it.
         */
        pChunk->MemObj = MemObj;
        pChunk->cFree = GMM_CHUNK_NUM_PAGES;
        pChunk->hGVM = hGVM;
        pChunk->iFreeHead = 0;
        pChunk->enmType = enmChunkType;
        for (unsigned iPage = 0; iPage < RT_ELEMENTS(pChunk->aPages) - 1; iPage++)
        {
            pChunk->aPages[iPage].Free.u2State = GMM_PAGE_STATE_FREE;
            pChunk->aPages[iPage].Free.iNext = iPage + 1;
        }
        pChunk->aPages[RT_ELEMENTS(pChunk->aPages) - 1].Free.u2State = GMM_PAGE_STATE_FREE;
        pChunk->aPages[RT_ELEMENTS(pChunk->aPages) - 1].Free.iNext = UINT16_MAX;

        /*
         * Allocate a Chunk ID and insert it into the tree.
         * This has to be done behind the mutex of course.
         */
        if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
        {
            pChunk->Core.Key = gmmR0AllocateChunkId(pGMM);
            if (    pChunk->Core.Key != NIL_GMM_CHUNKID
                &&  pChunk->Core.Key <= GMM_CHUNKID_LAST
                &&  RTAvlU32Insert(&pGMM->pChunks, &pChunk->Core))
            {
                pGMM->cChunks++;
                gmmR0LinkChunk(pChunk, pSet);
                LogFlow(("gmmR0RegisterChunk: pChunk=%p id=%#x cChunks=%d\n", pChunk, pChunk->Core.Key, pGMM->cChunks));

                if (ppChunk)
                    *ppChunk = pChunk;

                GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
                return VINF_SUCCESS;
            }

            /* bail out */
            rc = VERR_INTERNAL_ERROR;
        }
        else
            rc = VERR_INTERNAL_ERROR_5;

        RTMemFree(pChunk);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Allocate one new chunk and add it to the specified free set.
 *
 * @returns VBox status code.
 * @param   pGMM            Pointer to the GMM instance.
 * @param   pSet            Pointer to the set.
 * @param   hGVM            The affinity of the new chunk.
 * @param   enmChunkType    Chunk type (continuous or non-continuous)
 * @param   ppChunk         Chunk address (out)
 *
 * @remarks Called without owning the mutex.
 */
static int gmmR0AllocateOneChunk(PGMM pGMM, PGMMCHUNKFREESET pSet, uint16_t hGVM, GMMCHUNKTYPE enmChunkType, PGMMCHUNK *ppChunk = NULL)
{
    /*
     * Allocate the memory.
     */
    RTR0MEMOBJ MemObj;
    int        rc;

    AssertCompile(GMM_CHUNK_SIZE == _2M);
    AssertReturn(enmChunkType == GMMCHUNKTYPE_NON_CONTINUOUS || enmChunkType == GMMCHUNKTYPE_CONTINUOUS, VERR_INVALID_PARAMETER);

    /* Leave the lock temporarily as the allocation might take long. */
    RTSemFastMutexRelease(pGMM->Mtx);
    if (enmChunkType == GMMCHUNKTYPE_NON_CONTINUOUS)
        rc = RTR0MemObjAllocPhysNC(&MemObj, GMM_CHUNK_SIZE, NIL_RTHCPHYS);
    else
        rc = RTR0MemObjAllocPhysEx(&MemObj, GMM_CHUNK_SIZE, NIL_RTHCPHYS, GMM_CHUNK_SIZE);

    /* Grab the lock again. */
    int rc2 = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRCReturn(rc2, rc2);

    if (RT_SUCCESS(rc))
    {
        rc = gmmR0RegisterChunk(pGMM, pSet, MemObj, hGVM, enmChunkType, ppChunk);
        if (RT_FAILURE(rc))
            RTR0MemObjFree(MemObj, false /* fFreeMappings */);
    }
    /** @todo Check that RTR0MemObjAllocPhysNC always returns VERR_NO_MEMORY on
     *        allocation failure. */
    return rc;
}


/**
 * Attempts to allocate more pages until the requested amount is met.
 *
 * @returns VBox status code.
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   pGVM        The calling VM.
 * @param   pSet        Pointer to the free set to grow.
 * @param   cPages      The number of pages needed.
 *
 * @remarks Called owning the mutex, but will leave it temporarily while
 *          allocating the memory!
 */
static int gmmR0AllocateMoreChunks(PGMM pGMM, PGVM pGVM, PGMMCHUNKFREESET pSet, uint32_t cPages)
{
    Assert(!pGMM->fLegacyAllocationMode);

    if (!GMM_CHECK_SANITY_IN_LOOPS(pGMM))
        return VERR_INTERNAL_ERROR_4;

    if (!pGMM->fBoundMemoryMode)
    {
        /*
         * Try steal free chunks from the other set first. (Only take 100% free chunks.)
         */
        PGMMCHUNKFREESET pOtherSet = pSet == &pGMM->Private ? &pGMM->Shared : &pGMM->Private;
        while (     pSet->cFreePages < cPages
               &&   pOtherSet->cFreePages >= GMM_CHUNK_NUM_PAGES)
        {
            PGMMCHUNK pChunk = pOtherSet->apLists[GMM_CHUNK_FREE_SET_LISTS - 1];
            while (pChunk && pChunk->cFree != GMM_CHUNK_NUM_PAGES)
                pChunk = pChunk->pFreeNext;
            if (!pChunk)
                break;

            gmmR0UnlinkChunk(pChunk);
            gmmR0LinkChunk(pChunk, pSet);
        }

        /*
         * If we need still more pages, allocate new chunks.
         * Note! We will leave the mutex while doing the allocation,
         */
        while (pSet->cFreePages < cPages)
        {
            int rc = gmmR0AllocateOneChunk(pGMM, pSet, pGVM->hSelf, GMMCHUNKTYPE_NON_CONTINUOUS);
            if (RT_FAILURE(rc))
                return rc;
            if (!GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
                return VERR_INTERNAL_ERROR_5;
        }
    }
    else
    {
        /*
         * The memory is bound to the VM allocating it, so we have to count
         * the free pages carefully as well as making sure we brand them with
         * our VM handle.
         *
         * Note! We will leave the mutex while doing the allocation,
         */
        uint16_t const hGVM = pGVM->hSelf;
        for (;;)
        {
            /* Count and see if we've reached the goal. */
            uint32_t cPagesFound = 0;
            for (unsigned i = 0; i < RT_ELEMENTS(pSet->apLists); i++)
                for (PGMMCHUNK pCur = pSet->apLists[i]; pCur; pCur = pCur->pFreeNext)
                    if (pCur->hGVM == hGVM)
                    {
                        cPagesFound += pCur->cFree;
                        if (cPagesFound >= cPages)
                            break;
                    }
            if (cPagesFound >= cPages)
                break;

            /* Allocate more. */
            int rc = gmmR0AllocateOneChunk(pGMM, pSet, hGVM, GMMCHUNKTYPE_NON_CONTINUOUS);
            if (RT_FAILURE(rc))
                return rc;
            if (!GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
                return VERR_INTERNAL_ERROR_5;
        }
    }

    return VINF_SUCCESS;
}


/**
 * Allocates one private page.
 *
 * Worker for gmmR0AllocatePages.
 *
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   hGVM        The GVM handle of the VM requesting memory.
 * @param   pChunk      The chunk to allocate it from.
 * @param   pPageDesc   The page descriptor.
 */
static void gmmR0AllocatePage(PGMM pGMM, uint32_t hGVM, PGMMCHUNK pChunk, PGMMPAGEDESC pPageDesc)
{
    /* update the chunk stats. */
    if (pChunk->hGVM == NIL_GVM_HANDLE)
        pChunk->hGVM = hGVM;
    Assert(pChunk->cFree);
    pChunk->cFree--;
    pChunk->cPrivate++;

    /* unlink the first free page. */
    const uint32_t iPage = pChunk->iFreeHead;
    AssertReleaseMsg(iPage < RT_ELEMENTS(pChunk->aPages), ("%d\n", iPage));
    PGMMPAGE pPage = &pChunk->aPages[iPage];
    Assert(GMM_PAGE_IS_FREE(pPage));
    pChunk->iFreeHead = pPage->Free.iNext;
    Log3(("A pPage=%p iPage=%#x/%#x u2State=%d iFreeHead=%#x iNext=%#x\n",
          pPage, iPage, (pChunk->Core.Key << GMM_CHUNKID_SHIFT) | iPage,
          pPage->Common.u2State, pChunk->iFreeHead, pPage->Free.iNext));

    /* make the page private. */
    pPage->u = 0;
    AssertCompile(GMM_PAGE_STATE_PRIVATE == 0);
    pPage->Private.hGVM = hGVM;
    AssertCompile(NIL_RTHCPHYS >= GMM_GCPHYS_LAST);
    AssertCompile(GMM_GCPHYS_UNSHAREABLE >= GMM_GCPHYS_LAST);
    if (pPageDesc->HCPhysGCPhys <= GMM_GCPHYS_LAST)
        pPage->Private.pfn = pPageDesc->HCPhysGCPhys >> PAGE_SHIFT;
    else
        pPage->Private.pfn = GMM_PAGE_PFN_UNSHAREABLE; /* unshareable / unassigned - same thing. */

    /* update the page descriptor. */
    pPageDesc->HCPhysGCPhys = RTR0MemObjGetPagePhysAddr(pChunk->MemObj, iPage);
    Assert(pPageDesc->HCPhysGCPhys != NIL_RTHCPHYS);
    pPageDesc->idPage = (pChunk->Core.Key << GMM_CHUNKID_SHIFT) | iPage;
    pPageDesc->idSharedPage = NIL_GMM_PAGEID;
}


/**
 * Common worker for GMMR0AllocateHandyPages and GMMR0AllocatePages.
 *
 * @returns VBox status code:
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_GMM_SEED_ME if seeding via GMMR0SeedChunk or
 *          gmmR0AllocateMoreChunks is necessary.
 * @retval  VERR_GMM_HIT_GLOBAL_LIMIT if we've exhausted the available pages.
 * @retval  VERR_GMM_HIT_VM_ACCOUNT_LIMIT if we've hit the VM account limit,
 *          that is we're trying to allocate more than we've reserved.
 *
 * @param   pGMM                Pointer to the GMM instance data.
 * @param   pGVM                Pointer to the shared VM structure.
 * @param   cPages              The number of pages to allocate.
 * @param   paPages             Pointer to the page descriptors.
 *                              See GMMPAGEDESC for details on what is expected on input.
 * @param   enmAccount          The account to charge.
 */
static int gmmR0AllocatePages(PGMM pGMM, PGVM pGVM, uint32_t cPages, PGMMPAGEDESC paPages, GMMACCOUNT enmAccount)
{
    /*
     * Check allocation limits.
     */
    if (RT_UNLIKELY(pGMM->cAllocatedPages + cPages > pGMM->cMaxPages))
        return VERR_GMM_HIT_GLOBAL_LIMIT;

    switch (enmAccount)
    {
        case GMMACCOUNT_BASE:
            if (RT_UNLIKELY(pGVM->gmm.s.Allocated.cBasePages + pGVM->gmm.s.cBalloonedPages + cPages > pGVM->gmm.s.Reserved.cBasePages))
            {
                Log(("gmmR0AllocatePages:Base: Reserved=%#llx Allocated+Ballooned+Requested=%#llx+%#llx+%#x!\n",
                     pGVM->gmm.s.Reserved.cBasePages, pGVM->gmm.s.Allocated.cBasePages, pGVM->gmm.s.cBalloonedPages, cPages));
                return VERR_GMM_HIT_VM_ACCOUNT_LIMIT;
            }
            break;
        case GMMACCOUNT_SHADOW:
            if (RT_UNLIKELY(pGVM->gmm.s.Allocated.cShadowPages + cPages > pGVM->gmm.s.Reserved.cShadowPages))
            {
                Log(("gmmR0AllocatePages:Shadow: Reserved=%#llx Allocated+Requested=%#llx+%#x!\n",
                     pGVM->gmm.s.Reserved.cShadowPages, pGVM->gmm.s.Allocated.cShadowPages, cPages));
                return VERR_GMM_HIT_VM_ACCOUNT_LIMIT;
            }
            break;
        case GMMACCOUNT_FIXED:
            if (RT_UNLIKELY(pGVM->gmm.s.Allocated.cFixedPages + cPages > pGVM->gmm.s.Reserved.cFixedPages))
            {
                Log(("gmmR0AllocatePages:Fixed: Reserved=%#llx Allocated+Requested=%#llx+%#x!\n",
                     pGVM->gmm.s.Reserved.cFixedPages, pGVM->gmm.s.Allocated.cFixedPages, cPages));
                return VERR_GMM_HIT_VM_ACCOUNT_LIMIT;
            }
            break;
        default:
            AssertMsgFailedReturn(("enmAccount=%d\n", enmAccount), VERR_INTERNAL_ERROR);
    }

    /*
     * Check if we need to allocate more memory or not. In bound memory mode this
     * is a bit extra work but it's easier to do it upfront than bailing out later.
     */
    PGMMCHUNKFREESET pSet = &pGMM->Private;
    if (pSet->cFreePages < cPages)
        return VERR_GMM_SEED_ME;
    if (pGMM->fBoundMemoryMode)
    {
        uint16_t hGVM = pGVM->hSelf;
        uint32_t cPagesFound = 0;
        for (unsigned i = 0; i < RT_ELEMENTS(pSet->apLists); i++)
            for (PGMMCHUNK pCur = pSet->apLists[i]; pCur; pCur = pCur->pFreeNext)
                if (pCur->hGVM == hGVM)
                {
                    cPagesFound += pCur->cFree;
                    if (cPagesFound >= cPages)
                        break;
                }
        if (cPagesFound < cPages)
            return VERR_GMM_SEED_ME;
    }

    /*
     * Pick the pages.
     * Try make some effort keeping VMs sharing private chunks.
     */
    uint16_t hGVM = pGVM->hSelf;
    uint32_t iPage = 0;

    /* first round, pick from chunks with an affinity to the VM. */
    for (unsigned i = 0; i < RT_ELEMENTS(pSet->apLists) && iPage < cPages; i++)
    {
        PGMMCHUNK pCurFree = NULL;
        PGMMCHUNK pCur = pSet->apLists[i];
        while (pCur && iPage < cPages)
        {
            PGMMCHUNK pNext = pCur->pFreeNext;

            if (    pCur->hGVM == hGVM
                &&  pCur->cFree < GMM_CHUNK_NUM_PAGES)
            {
                gmmR0UnlinkChunk(pCur);
                for (; pCur->cFree && iPage < cPages; iPage++)
                    gmmR0AllocatePage(pGMM, hGVM, pCur, &paPages[iPage]);
                gmmR0LinkChunk(pCur, pSet);
            }

            pCur = pNext;
        }
    }

    if (iPage < cPages)
    {
        /* second round, pick pages from the 100% empty chunks we just skipped above. */
        PGMMCHUNK pCurFree = NULL;
        PGMMCHUNK pCur = pSet->apLists[RT_ELEMENTS(pSet->apLists) - 1];
        while (pCur && iPage < cPages)
        {
            PGMMCHUNK pNext = pCur->pFreeNext;

            if (    pCur->cFree == GMM_CHUNK_NUM_PAGES
                &&  (   pCur->hGVM == hGVM
                     || !pGMM->fBoundMemoryMode))
            {
                gmmR0UnlinkChunk(pCur);
                for (; pCur->cFree && iPage < cPages; iPage++)
                    gmmR0AllocatePage(pGMM, hGVM, pCur, &paPages[iPage]);
                gmmR0LinkChunk(pCur, pSet);
            }

            pCur = pNext;
        }
    }

    if (    iPage < cPages
        &&  !pGMM->fBoundMemoryMode)
    {
        /* third round, disregard affinity. */
        unsigned i = RT_ELEMENTS(pSet->apLists);
        while (i-- > 0 && iPage < cPages)
        {
            PGMMCHUNK pCurFree = NULL;
            PGMMCHUNK pCur = pSet->apLists[i];
            while (pCur && iPage < cPages)
            {
                PGMMCHUNK pNext = pCur->pFreeNext;

                if (    pCur->cFree >  GMM_CHUNK_NUM_PAGES / 2
                    &&  cPages      >= GMM_CHUNK_NUM_PAGES / 2)
                    pCur->hGVM = hGVM; /* change chunk affinity */

                gmmR0UnlinkChunk(pCur);
                for (; pCur->cFree && iPage < cPages; iPage++)
                    gmmR0AllocatePage(pGMM, hGVM, pCur, &paPages[iPage]);
                gmmR0LinkChunk(pCur, pSet);

                pCur = pNext;
            }
        }
    }

    /*
     * Update the account.
     */
    switch (enmAccount)
    {
        case GMMACCOUNT_BASE:   pGVM->gmm.s.Allocated.cBasePages   += iPage; break;
        case GMMACCOUNT_SHADOW: pGVM->gmm.s.Allocated.cShadowPages += iPage; break;
        case GMMACCOUNT_FIXED:  pGVM->gmm.s.Allocated.cFixedPages  += iPage; break;
        default:
            AssertMsgFailedReturn(("enmAccount=%d\n", enmAccount), VERR_INTERNAL_ERROR);
    }
    pGVM->gmm.s.cPrivatePages += iPage;
    pGMM->cAllocatedPages     += iPage;

    AssertMsgReturn(iPage == cPages, ("%u != %u\n", iPage, cPages), VERR_INTERNAL_ERROR);

    /*
     * Check if we've reached some threshold and should kick one or two VMs and tell
     * them to inflate their balloons a bit more... later.
     */

    return VINF_SUCCESS;
}


/**
 * Updates the previous allocations and allocates more pages.
 *
 * The handy pages are always taken from the 'base' memory account.
 * The allocated pages are not cleared and will contains random garbage.
 *
 * @returns VBox status code:
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_OWNER if the caller is not an EMT.
 * @retval  VERR_GMM_PAGE_NOT_FOUND if one of the pages to update wasn't found.
 * @retval  VERR_GMM_PAGE_NOT_PRIVATE if one of the pages to update wasn't a
 *          private page.
 * @retval  VERR_GMM_PAGE_NOT_SHARED if one of the pages to update wasn't a
 *          shared page.
 * @retval  VERR_GMM_NOT_PAGE_OWNER if one of the pages to be updated wasn't
 *          owned by the VM.
 * @retval  VERR_GMM_SEED_ME if seeding via GMMR0SeedChunk is necessary.
 * @retval  VERR_GMM_HIT_GLOBAL_LIMIT if we've exhausted the available pages.
 * @retval  VERR_GMM_HIT_VM_ACCOUNT_LIMIT if we've hit the VM account limit,
 *          that is we're trying to allocate more than we've reserved.
 *
 * @param   pVM                 Pointer to the shared VM structure.
 * @param   idCpu               VCPU id
 * @param   cPagesToUpdate      The number of pages to update (starting from the head).
 * @param   cPagesToAlloc       The number of pages to allocate (starting from the head).
 * @param   paPages             The array of page descriptors.
 *                              See GMMPAGEDESC for details on what is expected on input.
 * @thread  EMT.
 */
GMMR0DECL(int) GMMR0AllocateHandyPages(PVM pVM, VMCPUID idCpu, uint32_t cPagesToUpdate, uint32_t cPagesToAlloc, PGMMPAGEDESC paPages)
{
    LogFlow(("GMMR0AllocateHandyPages: pVM=%p cPagesToUpdate=%#x cPagesToAlloc=%#x paPages=%p\n",
             pVM, cPagesToUpdate, cPagesToAlloc, paPages));

    /*
     * Validate, get basics and take the semaphore.
     * (This is a relatively busy path, so make predictions where possible.)
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    AssertPtrReturn(paPages, VERR_INVALID_PARAMETER);
    AssertMsgReturn(    (cPagesToUpdate && cPagesToUpdate < 1024)
                    ||  (cPagesToAlloc  && cPagesToAlloc < 1024),
                    ("cPagesToUpdate=%#x cPagesToAlloc=%#x\n", cPagesToUpdate, cPagesToAlloc),
                    VERR_INVALID_PARAMETER);

    unsigned iPage = 0;
    for (; iPage < cPagesToUpdate; iPage++)
    {
        AssertMsgReturn(    (    paPages[iPage].HCPhysGCPhys <= GMM_GCPHYS_LAST
                             && !(paPages[iPage].HCPhysGCPhys & PAGE_OFFSET_MASK))
                        ||  paPages[iPage].HCPhysGCPhys == NIL_RTHCPHYS
                        ||  paPages[iPage].HCPhysGCPhys == GMM_GCPHYS_UNSHAREABLE,
                        ("#%#x: %RHp\n", iPage, paPages[iPage].HCPhysGCPhys),
                        VERR_INVALID_PARAMETER);
        AssertMsgReturn(    paPages[iPage].idPage <= GMM_PAGEID_LAST
                        /*||  paPages[iPage].idPage == NIL_GMM_PAGEID*/,
                        ("#%#x: %#x\n", iPage, paPages[iPage].idPage), VERR_INVALID_PARAMETER);
        AssertMsgReturn(    paPages[iPage].idPage <= GMM_PAGEID_LAST
                        /*||  paPages[iPage].idSharedPage == NIL_GMM_PAGEID*/,
                        ("#%#x: %#x\n", iPage, paPages[iPage].idSharedPage), VERR_INVALID_PARAMETER);
    }

    for (; iPage < cPagesToAlloc; iPage++)
    {
        AssertMsgReturn(paPages[iPage].HCPhysGCPhys == NIL_RTHCPHYS,   ("#%#x: %RHp\n", iPage, paPages[iPage].HCPhysGCPhys), VERR_INVALID_PARAMETER);
        AssertMsgReturn(paPages[iPage].idPage       == NIL_GMM_PAGEID, ("#%#x: %#x\n", iPage, paPages[iPage].idPage),        VERR_INVALID_PARAMETER);
        AssertMsgReturn(paPages[iPage].idSharedPage == NIL_GMM_PAGEID, ("#%#x: %#x\n", iPage, paPages[iPage].idSharedPage),  VERR_INVALID_PARAMETER);
    }

    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {

        /* No allocations before the initial reservation has been made! */
        if (RT_LIKELY(    pGVM->gmm.s.Reserved.cBasePages
                      &&  pGVM->gmm.s.Reserved.cFixedPages
                      &&  pGVM->gmm.s.Reserved.cShadowPages))
        {
            /*
             * Perform the updates.
             * Stop on the first error.
             */
            for (iPage = 0; iPage < cPagesToUpdate; iPage++)
            {
                if (paPages[iPage].idPage != NIL_GMM_PAGEID)
                {
                    PGMMPAGE pPage = gmmR0GetPage(pGMM, paPages[iPage].idPage);
                    if (RT_LIKELY(pPage))
                    {
                        if (RT_LIKELY(GMM_PAGE_IS_PRIVATE(pPage)))
                        {
                            if (RT_LIKELY(pPage->Private.hGVM == pGVM->hSelf))
                            {
                                AssertCompile(NIL_RTHCPHYS > GMM_GCPHYS_LAST && GMM_GCPHYS_UNSHAREABLE > GMM_GCPHYS_LAST);
                                if (RT_LIKELY(paPages[iPage].HCPhysGCPhys <= GMM_GCPHYS_LAST))
                                    pPage->Private.pfn = paPages[iPage].HCPhysGCPhys >> PAGE_SHIFT;
                                else if (paPages[iPage].HCPhysGCPhys == GMM_GCPHYS_UNSHAREABLE)
                                    pPage->Private.pfn = GMM_PAGE_PFN_UNSHAREABLE;
                                /* else: NIL_RTHCPHYS nothing */

                                paPages[iPage].idPage = NIL_GMM_PAGEID;
                                paPages[iPage].HCPhysGCPhys = NIL_RTHCPHYS;
                            }
                            else
                            {
                                Log(("GMMR0AllocateHandyPages: #%#x/%#x: Not owner! hGVM=%#x hSelf=%#x\n",
                                     iPage, paPages[iPage].idPage, pPage->Private.hGVM, pGVM->hSelf));
                                rc = VERR_GMM_NOT_PAGE_OWNER;
                                break;
                            }
                        }
                        else
                        {
                            Log(("GMMR0AllocateHandyPages: #%#x/%#x: Not private! %.*Rhxs\n", iPage, paPages[iPage].idPage, sizeof(*pPage), pPage));
                            rc = VERR_GMM_PAGE_NOT_PRIVATE;
                            break;
                        }
                    }
                    else
                    {
                        Log(("GMMR0AllocateHandyPages: #%#x/%#x: Not found! (private)\n", iPage, paPages[iPage].idPage));
                        rc = VERR_GMM_PAGE_NOT_FOUND;
                        break;
                    }
                }

                if (paPages[iPage].idSharedPage != NIL_GMM_PAGEID)
                {
                    PGMMPAGE pPage = gmmR0GetPage(pGMM, paPages[iPage].idSharedPage);
                    if (RT_LIKELY(pPage))
                    {
                        if (RT_LIKELY(GMM_PAGE_IS_SHARED(pPage)))
                        {
                            AssertCompile(NIL_RTHCPHYS > GMM_GCPHYS_LAST && GMM_GCPHYS_UNSHAREABLE > GMM_GCPHYS_LAST);
                            Assert(pPage->Shared.cRefs);
                            Assert(pGVM->gmm.s.cSharedPages);
                            Assert(pGVM->gmm.s.Allocated.cBasePages);

                            pGVM->gmm.s.cSharedPages--;
                            pGVM->gmm.s.Allocated.cBasePages--;
                            if (!--pPage->Shared.cRefs)
                                gmmR0FreeSharedPage(pGMM, paPages[iPage].idSharedPage, pPage);

                            paPages[iPage].idSharedPage = NIL_GMM_PAGEID;
                        }
                        else
                        {
                            Log(("GMMR0AllocateHandyPages: #%#x/%#x: Not shared!\n", iPage, paPages[iPage].idSharedPage));
                            rc = VERR_GMM_PAGE_NOT_SHARED;
                            break;
                        }
                    }
                    else
                    {
                        Log(("GMMR0AllocateHandyPages: #%#x/%#x: Not found! (shared)\n", iPage, paPages[iPage].idSharedPage));
                        rc = VERR_GMM_PAGE_NOT_FOUND;
                        break;
                    }
                }
            }

            /*
             * Join paths with GMMR0AllocatePages for the allocation.
             * Note! gmmR0AllocateMoreChunks may leave the protection of the mutex!
             */
            while (RT_SUCCESS(rc))
            {
                rc = gmmR0AllocatePages(pGMM, pGVM, cPagesToAlloc, paPages, GMMACCOUNT_BASE);
                if (    rc != VERR_GMM_SEED_ME
                    ||  pGMM->fLegacyAllocationMode)
                    break;
                rc = gmmR0AllocateMoreChunks(pGMM, pGVM, &pGMM->Private, cPagesToAlloc);
            }
        }
        else
            rc = VERR_WRONG_ORDER;
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_INTERNAL_ERROR_5;
    RTSemFastMutexRelease(pGMM->Mtx);
    LogFlow(("GMMR0AllocateHandyPages: returns %Rrc\n", rc));
    return rc;
}


/**
 * Allocate one or more pages.
 *
 * This is typically used for ROMs and MMIO2 (VRAM) during VM creation.
 * The allocated pages are not cleared and will contains random garbage.
 *
 * @returns VBox status code:
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_OWNER if the caller is not an EMT.
 * @retval  VERR_GMM_SEED_ME if seeding via GMMR0SeedChunk is necessary.
 * @retval  VERR_GMM_HIT_GLOBAL_LIMIT if we've exhausted the available pages.
 * @retval  VERR_GMM_HIT_VM_ACCOUNT_LIMIT if we've hit the VM account limit,
 *          that is we're trying to allocate more than we've reserved.
 *
 * @param   pVM                 Pointer to the shared VM structure.
 * @param   idCpu               VCPU id
 * @param   cPages              The number of pages to allocate.
 * @param   paPages             Pointer to the page descriptors.
 *                              See GMMPAGEDESC for details on what is expected on input.
 * @param   enmAccount          The account to charge.
 *
 * @thread  EMT.
 */
GMMR0DECL(int) GMMR0AllocatePages(PVM pVM, VMCPUID idCpu, uint32_t cPages, PGMMPAGEDESC paPages, GMMACCOUNT enmAccount)
{
    LogFlow(("GMMR0AllocatePages: pVM=%p cPages=%#x paPages=%p enmAccount=%d\n", pVM, cPages, paPages, enmAccount));

    /*
     * Validate, get basics and take the semaphore.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    AssertPtrReturn(paPages, VERR_INVALID_PARAMETER);
    AssertMsgReturn(enmAccount > GMMACCOUNT_INVALID && enmAccount < GMMACCOUNT_END, ("%d\n", enmAccount), VERR_INVALID_PARAMETER);
    AssertMsgReturn(cPages > 0 && cPages < RT_BIT(32 - PAGE_SHIFT), ("%#x\n", cPages), VERR_INVALID_PARAMETER);

    for (unsigned iPage = 0; iPage < cPages; iPage++)
    {
        AssertMsgReturn(    paPages[iPage].HCPhysGCPhys == NIL_RTHCPHYS
                        ||  paPages[iPage].HCPhysGCPhys == GMM_GCPHYS_UNSHAREABLE
                        ||  (    enmAccount == GMMACCOUNT_BASE
                             &&  paPages[iPage].HCPhysGCPhys <= GMM_GCPHYS_LAST
                             && !(paPages[iPage].HCPhysGCPhys & PAGE_OFFSET_MASK)),
                        ("#%#x: %RHp enmAccount=%d\n", iPage, paPages[iPage].HCPhysGCPhys, enmAccount),
                        VERR_INVALID_PARAMETER);
        AssertMsgReturn(paPages[iPage].idPage == NIL_GMM_PAGEID, ("#%#x: %#x\n", iPage, paPages[iPage].idPage), VERR_INVALID_PARAMETER);
        AssertMsgReturn(paPages[iPage].idSharedPage == NIL_GMM_PAGEID, ("#%#x: %#x\n", iPage, paPages[iPage].idSharedPage), VERR_INVALID_PARAMETER);
    }

    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {

        /* No allocations before the initial reservation has been made! */
        if (RT_LIKELY(    pGVM->gmm.s.Reserved.cBasePages
                      &&  pGVM->gmm.s.Reserved.cFixedPages
                      &&  pGVM->gmm.s.Reserved.cShadowPages))
        {
            /*
             * gmmR0AllocatePages seed loop.
             * Note! gmmR0AllocateMoreChunks may leave the protection of the mutex!
             */
            while (RT_SUCCESS(rc))
            {
                rc = gmmR0AllocatePages(pGMM, pGVM, cPages, paPages, enmAccount);
                if (    rc != VERR_GMM_SEED_ME
                    ||  pGMM->fLegacyAllocationMode)
                    break;
                rc = gmmR0AllocateMoreChunks(pGMM, pGVM, &pGMM->Private, cPages);
            }
        }
        else
            rc = VERR_WRONG_ORDER;
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_INTERNAL_ERROR_5;
    RTSemFastMutexRelease(pGMM->Mtx);
    LogFlow(("GMMR0AllocatePages: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0AllocatePages.
 *
 * @returns see GMMR0AllocatePages.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           VCPU id
 * @param   pReq            The request packet.
 */
GMMR0DECL(int) GMMR0AllocatePagesReq(PVM pVM, VMCPUID idCpu, PGMMALLOCATEPAGESREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq >= RT_UOFFSETOF(GMMALLOCATEPAGESREQ, aPages[0]),
                    ("%#x < %#x\n", pReq->Hdr.cbReq, RT_UOFFSETOF(GMMALLOCATEPAGESREQ, aPages[0])),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(pReq->Hdr.cbReq == RT_UOFFSETOF(GMMALLOCATEPAGESREQ, aPages[pReq->cPages]),
                    ("%#x != %#x\n", pReq->Hdr.cbReq, RT_UOFFSETOF(GMMALLOCATEPAGESREQ, aPages[pReq->cPages])),
                    VERR_INVALID_PARAMETER);

    return GMMR0AllocatePages(pVM, idCpu, pReq->cPages, &pReq->aPages[0], pReq->enmAccount);
}

/**
 * Allocate a large page to represent guest RAM
 *
 * The allocated pages are not cleared and will contains random garbage.
 *
 * @returns VBox status code:
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_OWNER if the caller is not an EMT.
 * @retval  VERR_GMM_SEED_ME if seeding via GMMR0SeedChunk is necessary.
 * @retval  VERR_GMM_HIT_GLOBAL_LIMIT if we've exhausted the available pages.
 * @retval  VERR_GMM_HIT_VM_ACCOUNT_LIMIT if we've hit the VM account limit,
 *          that is we're trying to allocate more than we've reserved.
 * @returns see GMMR0AllocatePages.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           VCPU id
 * @param   cbPage          Large page size
 */
GMMR0DECL(int)  GMMR0AllocateLargePage(PVM pVM, VMCPUID idCpu, uint32_t cbPage, uint32_t *pIdPage, RTHCPHYS *pHCPhys)
{
    LogFlow(("GMMR0AllocateLargePage: pVM=%p cbPage=%x\n", pVM, cbPage));

    AssertReturn(cbPage == GMM_CHUNK_SIZE, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pIdPage, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pHCPhys, VERR_INVALID_PARAMETER);

    /*
     * Validate, get basics and take the semaphore.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    /* Not supported in legacy mode where we allocate the memory in ring 3 and lock it in ring 0. */
    if (pGMM->fLegacyAllocationMode)
        return VERR_NOT_SUPPORTED;

    *pHCPhys = NIL_RTHCPHYS;
    *pIdPage = NIL_GMM_PAGEID;

    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRCReturn(rc, rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        const unsigned cPages = (GMM_CHUNK_SIZE >> PAGE_SHIFT);
        PGMMCHUNK      pChunk;
        GMMPAGEDESC    PageDesc;

        if (RT_UNLIKELY(pGVM->gmm.s.Allocated.cBasePages + pGVM->gmm.s.cBalloonedPages + cPages > pGVM->gmm.s.Reserved.cBasePages))
        {
            Log(("GMMR0AllocateLargePage: Reserved=%#llx Allocated+Requested=%#llx+%#x!\n",
                 pGVM->gmm.s.Reserved.cBasePages, pGVM->gmm.s.Allocated.cBasePages, cPages));
            RTSemFastMutexRelease(pGMM->Mtx);
            return VERR_GMM_HIT_VM_ACCOUNT_LIMIT;
        }

        /* Allocate a new continous chunk. */
        rc = gmmR0AllocateOneChunk(pGMM, &pGMM->Private, pGVM->hSelf, GMMCHUNKTYPE_CONTINUOUS, &pChunk);
        if (RT_FAILURE(rc))
        {
            RTSemFastMutexRelease(pGMM->Mtx);
            return rc;
        }

        /* Unlink the new chunk from the free list. */
        gmmR0UnlinkChunk(pChunk);

        /* Allocate all pages. */
        gmmR0AllocatePage(pGMM, pGVM->hSelf, pChunk, &PageDesc);
        /* Return the first page as we'll use the whole chunk as one big page. */
        *pIdPage = PageDesc.idPage;
        *pHCPhys = PageDesc.HCPhysGCPhys;

        for (unsigned i = 1; i < cPages; i++)
            gmmR0AllocatePage(pGMM, pGVM->hSelf, pChunk, &PageDesc);

        /* Update accounting. */
        pGVM->gmm.s.Allocated.cBasePages += cPages;
        pGVM->gmm.s.cPrivatePages        += cPages;
        pGMM->cAllocatedPages            += cPages;

        gmmR0LinkChunk(pChunk, &pGMM->Private);
    }
    else
        rc = VERR_INTERNAL_ERROR_5;

    RTSemFastMutexRelease(pGMM->Mtx);
    LogFlow(("GMMR0AllocatePages: returns %Rrc\n", rc));
    return rc;
}


/**
 * Free a large page
 *
 * @returns VBox status code:
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           VCPU id
 * @param   idPage          Large page id
 */
GMMR0DECL(int)  GMMR0FreeLargePage(PVM pVM, VMCPUID idCpu, uint32_t idPage)
{
    LogFlow(("GMMR0FreeLargePage: pVM=%p idPage=%x\n", pVM, idPage));

    /*
     * Validate, get basics and take the semaphore.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    /* Not supported in legacy mode where we allocate the memory in ring 3 and lock it in ring 0. */
    if (pGMM->fLegacyAllocationMode)
        return VERR_NOT_SUPPORTED;

    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        const unsigned cPages = (GMM_CHUNK_SIZE >> PAGE_SHIFT);

        if (RT_UNLIKELY(pGVM->gmm.s.Allocated.cBasePages < cPages))
        {
            Log(("GMMR0FreeLargePage: allocated=%#llx cPages=%#x!\n", pGVM->gmm.s.Allocated.cBasePages, cPages));
            RTSemFastMutexRelease(pGMM->Mtx);
            return VERR_GMM_ATTEMPT_TO_FREE_TOO_MUCH;
        }

        PGMMPAGE pPage = gmmR0GetPage(pGMM, idPage);
        if (    RT_LIKELY(pPage)
            &&  RT_LIKELY(GMM_PAGE_IS_PRIVATE(pPage)))
        {
                PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, idPage >> GMM_CHUNKID_SHIFT);
                Assert(pChunk);
                Assert(pChunk->cFree < GMM_CHUNK_NUM_PAGES);
                Assert(pChunk->cPrivate > 0);

                /* Release the memory immediately. */
                gmmR0FreeChunk(pGMM, NULL, pChunk);

                /* Update accounting. */
                pGVM->gmm.s.Allocated.cBasePages -= cPages;
                pGVM->gmm.s.cPrivatePages        -= cPages;
                pGMM->cAllocatedPages            -= cPages;
        }
        else
            rc = VERR_GMM_PAGE_NOT_FOUND;
    }
    else
        rc = VERR_INTERNAL_ERROR_5;

    RTSemFastMutexRelease(pGMM->Mtx);
    LogFlow(("GMMR0FreeLargePage: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0FreeLargePage.
 *
 * @returns see GMMR0FreeLargePage.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           VCPU id
 * @param   pReq            The request packet.
 */
GMMR0DECL(int) GMMR0FreeLargePageReq(PVM pVM, VMCPUID idCpu, PGMMFREELARGEPAGEREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(GMMFREEPAGESREQ),
                    ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(GMMFREEPAGESREQ)),
                    VERR_INVALID_PARAMETER);

    return GMMR0FreeLargePage(pVM, idCpu, pReq->idPage);
}

/**
 * Frees a chunk, giving it back to the host OS.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pGVM        This is set when called from GMMR0CleanupVM so we can
 *                      unmap and free the chunk in one go.
 * @param   pChunk      The chunk to free.
 */
static void gmmR0FreeChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk)
{
    Assert(pChunk->Core.Key != NIL_GMM_CHUNKID);

    /*
     * Cleanup hack! Unmap the chunk from the callers address space.
     */
    if (    pChunk->cMappings
        &&  pGVM)
        gmmR0UnmapChunk(pGMM, pGVM, pChunk);

    /*
     * If there are current mappings of the chunk, then request the
     * VMs to unmap them. Reposition the chunk in the free list so
     * it won't be a likely candidate for allocations.
     */
    if (pChunk->cMappings)
    {
        /** @todo R0 -> VM request */
        /* The chunk can be owned by more than one VM if fBoundMemoryMode is false! */
    }
    else
    {
        /*
         * Try free the memory object.
         */
        int rc = RTR0MemObjFree(pChunk->MemObj, false /* fFreeMappings */);
        if (RT_SUCCESS(rc))
        {
            pChunk->MemObj = NIL_RTR0MEMOBJ;

            /*
             * Unlink it from everywhere.
             */
            gmmR0UnlinkChunk(pChunk);

            PAVLU32NODECORE pCore = RTAvlU32Remove(&pGMM->pChunks, pChunk->Core.Key);
            Assert(pCore == &pChunk->Core); NOREF(pCore);

            PGMMCHUNKTLBE pTlbe = &pGMM->ChunkTLB.aEntries[GMM_CHUNKTLB_IDX(pChunk->Core.Key)];
            if (pTlbe->pChunk == pChunk)
            {
                pTlbe->idChunk = NIL_GMM_CHUNKID;
                pTlbe->pChunk = NULL;
            }

            Assert(pGMM->cChunks > 0);
            pGMM->cChunks--;

            /*
             * Free the Chunk ID and struct.
             */
            gmmR0FreeChunkId(pGMM, pChunk->Core.Key);
            pChunk->Core.Key = NIL_GMM_CHUNKID;

            RTMemFree(pChunk->paMappings);
            pChunk->paMappings = NULL;

            RTMemFree(pChunk);
        }
        else
            AssertRC(rc);
    }
}


/**
 * Free page worker.
 *
 * The caller does all the statistic decrementing, we do all the incrementing.
 *
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   pChunk      Pointer to the chunk this page belongs to.
 * @param   idPage      The Page ID.
 * @param   pPage       Pointer to the page.
 */
static void gmmR0FreePageWorker(PGMM pGMM, PGMMCHUNK pChunk, uint32_t idPage, PGMMPAGE pPage)
{
    Log3(("F pPage=%p iPage=%#x/%#x u2State=%d iFreeHead=%#x\n",
          pPage, pPage - &pChunk->aPages[0], idPage, pPage->Common.u2State, pChunk->iFreeHead)); NOREF(idPage);

    /*
     * Put the page on the free list.
     */
    pPage->u = 0;
    pPage->Free.u2State = GMM_PAGE_STATE_FREE;
    Assert(pChunk->iFreeHead < RT_ELEMENTS(pChunk->aPages) || pChunk->iFreeHead == UINT16_MAX);
    pPage->Free.iNext = pChunk->iFreeHead;
    pChunk->iFreeHead = pPage - &pChunk->aPages[0];

    /*
     * Update statistics (the cShared/cPrivate stats are up to date already),
     * and relink the chunk if necessary.
     */
    if ((pChunk->cFree & GMM_CHUNK_FREE_SET_MASK) == 0)
    {
        gmmR0UnlinkChunk(pChunk);
        pChunk->cFree++;
        gmmR0LinkChunk(pChunk, pChunk->cShared ? &pGMM->Shared : &pGMM->Private);
    }
    else
    {
        pChunk->cFree++;
        pChunk->pSet->cFreePages++;

        /*
         * If the chunk becomes empty, consider giving memory back to the host OS.
         *
         * The current strategy is to try give it back if there are other chunks
         * in this free list, meaning if there are at least 240 free pages in this
         * category. Note that since there are probably mappings of the chunk,
         * it won't be freed up instantly, which probably screws up this logic
         * a bit...
         */
        if (RT_UNLIKELY(   pChunk->cFree == GMM_CHUNK_NUM_PAGES
                        && pChunk->pFreeNext
                        && pChunk->pFreePrev
                        && !pGMM->fLegacyAllocationMode))
            gmmR0FreeChunk(pGMM, NULL, pChunk);
    }
}


/**
 * Frees a shared page, the page is known to exist and be valid and such.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   idPage      The Page ID
 * @param   pPage       The page structure.
 */
DECLINLINE(void) gmmR0FreeSharedPage(PGMM pGMM, uint32_t idPage, PGMMPAGE pPage)
{
    PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, idPage >> GMM_CHUNKID_SHIFT);
    Assert(pChunk);
    Assert(pChunk->cFree < GMM_CHUNK_NUM_PAGES);
    Assert(pChunk->cShared > 0);
    Assert(pGMM->cSharedPages > 0);
    Assert(pGMM->cAllocatedPages > 0);
    Assert(!pPage->Shared.cRefs);

    pChunk->cShared--;
    pGMM->cAllocatedPages--;
    pGMM->cSharedPages--;
    gmmR0FreePageWorker(pGMM, pChunk, idPage, pPage);
}

/**
 * Converts a private page to a shared page, the page is known to exist and be valid and such.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pGVM        Pointer to the GVM instance.
 * @param   HCPhys      Host physical address
 * @param   idPage      The Page ID
 * @param   pPage       The page structure.
 */
DECLINLINE(void) gmmR0ConvertToSharedPage(PGMM pGMM, PGVM pGVM, RTHCPHYS HCPhys, uint32_t idPage, PGMMPAGE pPage)
{
    PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, idPage >> GMM_CHUNKID_SHIFT);
    Assert(pChunk);
    Assert(pChunk->cFree < GMM_CHUNK_NUM_PAGES);
    Assert(GMM_PAGE_IS_PRIVATE(pPage));

    pChunk->cPrivate--;
    pChunk->cShared++;

    pGMM->cSharedPages++;

    pGVM->gmm.s.cSharedPages++;
    pGVM->gmm.s.cPrivatePages--;

    /* Modify the page structure. */
    pPage->Shared.pfn     = (uint32_t)(HCPhys >> PAGE_SHIFT);
    pPage->Shared.cRefs   = 1;
    pPage->Common.u2State = GMM_PAGE_STATE_SHARED;
}

/**
 * Increase the use count of a shared page, the page is known to exist and be valid and such.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pGVM        Pointer to the GVM instance.
 * @param   pPage       The page structure.
 */
DECLINLINE(void) gmmR0UseSharedPage(PGMM pGMM, PGVM pGVM, PGMMPAGE pPage)
{
    Assert(pGMM->cSharedPages > 0);
    Assert(pGMM->cAllocatedPages > 0);

    pPage->Shared.cRefs++;
    pGVM->gmm.s.cSharedPages++;
}

/**
 * Frees a private page, the page is known to exist and be valid and such.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   idPage      The Page ID
 * @param   pPage       The page structure.
 */
DECLINLINE(void) gmmR0FreePrivatePage(PGMM pGMM, uint32_t idPage, PGMMPAGE pPage)
{
    PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, idPage >> GMM_CHUNKID_SHIFT);
    Assert(pChunk);
    Assert(pChunk->cFree < GMM_CHUNK_NUM_PAGES);
    Assert(pChunk->cPrivate > 0);
    Assert(pGMM->cAllocatedPages > 0);

    pChunk->cPrivate--;
    pGMM->cAllocatedPages--;
    gmmR0FreePageWorker(pGMM, pChunk, idPage, pPage);
}

/**
 * Common worker for GMMR0FreePages and GMMR0BalloonedPages.
 *
 * @returns VBox status code:
 * @retval  xxx
 *
 * @param   pGMM                Pointer to the GMM instance data.
 * @param   pGVM                Pointer to the shared VM structure.
 * @param   cPages              The number of pages to free.
 * @param   paPages             Pointer to the page descriptors.
 * @param   enmAccount          The account this relates to.
 */
static int gmmR0FreePages(PGMM pGMM, PGVM pGVM, uint32_t cPages, PGMMFREEPAGEDESC paPages, GMMACCOUNT enmAccount)
{
    /*
     * Check that the request isn't impossible wrt to the account status.
     */
    switch (enmAccount)
    {
        case GMMACCOUNT_BASE:
            if (RT_UNLIKELY(pGVM->gmm.s.Allocated.cBasePages < cPages))
            {
                Log(("gmmR0FreePages: allocated=%#llx cPages=%#x!\n", pGVM->gmm.s.Allocated.cBasePages, cPages));
                return VERR_GMM_ATTEMPT_TO_FREE_TOO_MUCH;
            }
            break;
        case GMMACCOUNT_SHADOW:
            if (RT_UNLIKELY(pGVM->gmm.s.Allocated.cShadowPages < cPages))
            {
                Log(("gmmR0FreePages: allocated=%#llx cPages=%#x!\n", pGVM->gmm.s.Allocated.cShadowPages, cPages));
                return VERR_GMM_ATTEMPT_TO_FREE_TOO_MUCH;
            }
            break;
        case GMMACCOUNT_FIXED:
            if (RT_UNLIKELY(pGVM->gmm.s.Allocated.cFixedPages < cPages))
            {
                Log(("gmmR0FreePages: allocated=%#llx cPages=%#x!\n", pGVM->gmm.s.Allocated.cFixedPages, cPages));
                return VERR_GMM_ATTEMPT_TO_FREE_TOO_MUCH;
            }
            break;
        default:
            AssertMsgFailedReturn(("enmAccount=%d\n", enmAccount), VERR_INTERNAL_ERROR);
    }

    /*
     * Walk the descriptors and free the pages.
     *
     * Statistics (except the account) are being updated as we go along,
     * unlike the alloc code. Also, stop on the first error.
     */
    int rc = VINF_SUCCESS;
    uint32_t iPage;
    for (iPage = 0; iPage < cPages; iPage++)
    {
        uint32_t idPage = paPages[iPage].idPage;
        PGMMPAGE pPage = gmmR0GetPage(pGMM, idPage);
        if (RT_LIKELY(pPage))
        {
            if (RT_LIKELY(GMM_PAGE_IS_PRIVATE(pPage)))
            {
                if (RT_LIKELY(pPage->Private.hGVM == pGVM->hSelf))
                {
                    Assert(pGVM->gmm.s.cPrivatePages);
                    pGVM->gmm.s.cPrivatePages--;
                    gmmR0FreePrivatePage(pGMM, idPage, pPage);
                }
                else
                {
                    Log(("gmmR0AllocatePages: #%#x/%#x: not owner! hGVM=%#x hSelf=%#x\n", iPage, idPage,
                         pPage->Private.hGVM, pGVM->hSelf));
                    rc = VERR_GMM_NOT_PAGE_OWNER;
                    break;
                }
            }
            else if (RT_LIKELY(GMM_PAGE_IS_SHARED(pPage)))
            {
                Assert(pGVM->gmm.s.cSharedPages);
                pGVM->gmm.s.cSharedPages--;
                Assert(pPage->Shared.cRefs);
                if (!--pPage->Shared.cRefs)
                    gmmR0FreeSharedPage(pGMM, idPage, pPage);
            }
            else
            {
                Log(("gmmR0AllocatePages: #%#x/%#x: already free!\n", iPage, idPage));
                rc = VERR_GMM_PAGE_ALREADY_FREE;
                break;
            }
        }
        else
        {
            Log(("gmmR0AllocatePages: #%#x/%#x: not found!\n", iPage, idPage));
            rc = VERR_GMM_PAGE_NOT_FOUND;
            break;
        }
        paPages[iPage].idPage = NIL_GMM_PAGEID;
    }

    /*
     * Update the account.
     */
    switch (enmAccount)
    {
        case GMMACCOUNT_BASE:   pGVM->gmm.s.Allocated.cBasePages   -= iPage; break;
        case GMMACCOUNT_SHADOW: pGVM->gmm.s.Allocated.cShadowPages -= iPage; break;
        case GMMACCOUNT_FIXED:  pGVM->gmm.s.Allocated.cFixedPages  -= iPage; break;
        default:
            AssertMsgFailedReturn(("enmAccount=%d\n", enmAccount), VERR_INTERNAL_ERROR);
    }

    /*
     * Any threshold stuff to be done here?
     */

    return rc;
}


/**
 * Free one or more pages.
 *
 * This is typically used at reset time or power off.
 *
 * @returns VBox status code:
 * @retval  xxx
 *
 * @param   pVM                 Pointer to the shared VM structure.
 * @param   idCpu               VCPU id
 * @param   cPages              The number of pages to allocate.
 * @param   paPages             Pointer to the page descriptors containing the Page IDs for each page.
 * @param   enmAccount          The account this relates to.
 * @thread  EMT.
 */
GMMR0DECL(int) GMMR0FreePages(PVM pVM, VMCPUID idCpu, uint32_t cPages, PGMMFREEPAGEDESC paPages, GMMACCOUNT enmAccount)
{
    LogFlow(("GMMR0FreePages: pVM=%p cPages=%#x paPages=%p enmAccount=%d\n", pVM, cPages, paPages, enmAccount));

    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    AssertPtrReturn(paPages, VERR_INVALID_PARAMETER);
    AssertMsgReturn(enmAccount > GMMACCOUNT_INVALID && enmAccount < GMMACCOUNT_END, ("%d\n", enmAccount), VERR_INVALID_PARAMETER);
    AssertMsgReturn(cPages > 0 && cPages < RT_BIT(32 - PAGE_SHIFT), ("%#x\n", cPages), VERR_INVALID_PARAMETER);

    for (unsigned iPage = 0; iPage < cPages; iPage++)
        AssertMsgReturn(    paPages[iPage].idPage <= GMM_PAGEID_LAST
                        /*||  paPages[iPage].idPage == NIL_GMM_PAGEID*/,
                        ("#%#x: %#x\n", iPage, paPages[iPage].idPage), VERR_INVALID_PARAMETER);

    /*
     * Take the semaphore and call the worker function.
     */
    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        rc = gmmR0FreePages(pGMM, pGVM, cPages, paPages, enmAccount);
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_INTERNAL_ERROR_5;
    RTSemFastMutexRelease(pGMM->Mtx);
    LogFlow(("GMMR0FreePages: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0FreePages.
 *
 * @returns see GMMR0FreePages.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           VCPU id
 * @param   pReq            The request packet.
 */
GMMR0DECL(int) GMMR0FreePagesReq(PVM pVM, VMCPUID idCpu, PGMMFREEPAGESREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq >= RT_UOFFSETOF(GMMFREEPAGESREQ, aPages[0]),
                    ("%#x < %#x\n", pReq->Hdr.cbReq, RT_UOFFSETOF(GMMFREEPAGESREQ, aPages[0])),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(pReq->Hdr.cbReq == RT_UOFFSETOF(GMMFREEPAGESREQ, aPages[pReq->cPages]),
                    ("%#x != %#x\n", pReq->Hdr.cbReq, RT_UOFFSETOF(GMMFREEPAGESREQ, aPages[pReq->cPages])),
                    VERR_INVALID_PARAMETER);

    return GMMR0FreePages(pVM, idCpu, pReq->cPages, &pReq->aPages[0], pReq->enmAccount);
}


/**
 * Report back on a memory ballooning request.
 *
 * The request may or may not have been initiated by the GMM. If it was initiated
 * by the GMM it is important that this function is called even if no pages were
 * ballooned.
 *
 * @returns VBox status code:
 * @retval  VERR_GMM_ATTEMPT_TO_FREE_TOO_MUCH
 * @retval  VERR_GMM_ATTEMPT_TO_DEFLATE_TOO_MUCH
 * @retval  VERR_GMM_OVERCOMMITED_TRY_AGAIN_IN_A_BIT - reset condition
 *          indicating that we won't necessarily have sufficient RAM to boot
 *          the VM again and that it should pause until this changes (we'll try
 *          balloon some other VM).  (For standard deflate we have little choice
 *          but to hope the VM won't use the memory that was returned to it.)
 *
 * @param   pVM                 Pointer to the shared VM structure.
 * @param   idCpu               VCPU id
 * @param   enmAction           Inflate/deflate/reset
 * @param   cBalloonedPages     The number of pages that was ballooned.
 *
 * @thread  EMT.
 */
GMMR0DECL(int) GMMR0BalloonedPages(PVM pVM, VMCPUID idCpu, GMMBALLOONACTION enmAction, uint32_t cBalloonedPages)
{
    LogFlow(("GMMR0BalloonedPages: pVM=%p enmAction=%d cBalloonedPages=%#x\n",
             pVM, enmAction, cBalloonedPages));

    AssertMsgReturn(cBalloonedPages < RT_BIT(32 - PAGE_SHIFT), ("%#x\n", cBalloonedPages), VERR_INVALID_PARAMETER);

    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Take the sempahore and do some more validations.
     */
    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        switch (enmAction)
        {
            case GMMBALLOONACTION_INFLATE:
            {
                if (pGVM->gmm.s.Allocated.cBasePages >= cBalloonedPages)
                {
                    /*
                     * Record the ballooned memory.
                     */
                    pGMM->cBalloonedPages += cBalloonedPages;
                    if (pGVM->gmm.s.cReqBalloonedPages)
                    {
                        /* Codepath never taken. Might be interesting in the future to request ballooned memory from guests in low memory conditions.. */
                        AssertFailed();

                        pGVM->gmm.s.cBalloonedPages += cBalloonedPages;
                        pGVM->gmm.s.cReqActuallyBalloonedPages += cBalloonedPages;
                        Log(("GMMR0BalloonedPages: +%#x - Global=%#llx / VM: Total=%#llx Req=%#llx Actual=%#llx (pending)\n", cBalloonedPages,
                             pGMM->cBalloonedPages, pGVM->gmm.s.cBalloonedPages, pGVM->gmm.s.cReqBalloonedPages, pGVM->gmm.s.cReqActuallyBalloonedPages));
                    }
                    else
                    {
                        pGVM->gmm.s.cBalloonedPages += cBalloonedPages;
                        Log(("GMMR0BalloonedPages: +%#x - Global=%#llx / VM: Total=%#llx (user)\n",
                             cBalloonedPages, pGMM->cBalloonedPages, pGVM->gmm.s.cBalloonedPages));
                    }
                }
                else
                    rc = VERR_GMM_ATTEMPT_TO_FREE_TOO_MUCH;
                break;
            }

            case GMMBALLOONACTION_DEFLATE:
            {
                /* Deflate. */
                if (pGVM->gmm.s.cBalloonedPages >= cBalloonedPages)
                {
                    /*
                     * Record the ballooned memory.
                     */
                    Assert(pGMM->cBalloonedPages >= cBalloonedPages);
                    pGMM->cBalloonedPages       -= cBalloonedPages;
                    pGVM->gmm.s.cBalloonedPages -= cBalloonedPages;
                    if (pGVM->gmm.s.cReqDeflatePages)
                    {
                        AssertFailed(); /* This is path is for later. */
                        Log(("GMMR0BalloonedPages: -%#x - Global=%#llx / VM: Total=%#llx Req=%#llx\n",
                             cBalloonedPages, pGMM->cBalloonedPages, pGVM->gmm.s.cBalloonedPages, pGVM->gmm.s.cReqDeflatePages));

                        /*
                         * Anything we need to do here now when the request has been completed?
                         */
                        pGVM->gmm.s.cReqDeflatePages = 0;
                    }
                    else
                        Log(("GMMR0BalloonedPages: -%#x - Global=%#llx / VM: Total=%#llx (user)\n",
                             cBalloonedPages, pGMM->cBalloonedPages, pGVM->gmm.s.cBalloonedPages));
                }
                else
                    rc = VERR_GMM_ATTEMPT_TO_DEFLATE_TOO_MUCH;
                break;
            }

            case GMMBALLOONACTION_RESET:
            {
                /* Reset to an empty balloon. */
                Assert(pGMM->cBalloonedPages >= pGVM->gmm.s.cBalloonedPages);

                pGMM->cBalloonedPages       -= pGVM->gmm.s.cBalloonedPages;
                pGVM->gmm.s.cBalloonedPages  = 0;
                break;
            }

            default:
                rc = VERR_INVALID_PARAMETER;
                break;
        }
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_INTERNAL_ERROR_5;

    RTSemFastMutexRelease(pGMM->Mtx);
    LogFlow(("GMMR0BalloonedPages: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0BalloonedPages.
 *
 * @returns see GMMR0BalloonedPages.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           VCPU id
 * @param   pReq            The request packet.
 */
GMMR0DECL(int) GMMR0BalloonedPagesReq(PVM pVM, VMCPUID idCpu, PGMMBALLOONEDPAGESREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(GMMBALLOONEDPAGESREQ),
                    ("%#x < %#x\n", pReq->Hdr.cbReq, sizeof(GMMBALLOONEDPAGESREQ)),
                    VERR_INVALID_PARAMETER);

    return GMMR0BalloonedPages(pVM, idCpu, pReq->enmAction, pReq->cBalloonedPages);
}

/**
 * Return memory statistics for the hypervisor
 *
 * @returns VBox status code:
 * @param   pVM             Pointer to the shared VM structure.
 * @param   pReq            The request packet.
 */
GMMR0DECL(int) GMMR0QueryHypervisorMemoryStatsReq(PVM pVM, PGMMMEMSTATSREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(GMMMEMSTATSREQ),
                    ("%#x < %#x\n", pReq->Hdr.cbReq, sizeof(GMMMEMSTATSREQ)),
                    VERR_INVALID_PARAMETER);

    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    pReq->cAllocPages     = pGMM->cAllocatedPages;
    pReq->cFreePages      = (pGMM->cChunks << (GMM_CHUNK_SHIFT- PAGE_SHIFT)) - pGMM->cAllocatedPages;
    pReq->cBalloonedPages = pGMM->cBalloonedPages;
    pReq->cMaxPages       = pGMM->cMaxPages;
    GMM_CHECK_SANITY_UPON_LEAVING(pGMM);

    return VINF_SUCCESS;
}

/**
 * Return memory statistics for the VM
 *
 * @returns VBox status code:
 * @param   pVM             Pointer to the shared VM structure.
 * @parma   idCpu           Cpu id.
 * @param   pReq            The request packet.
 */
GMMR0DECL(int)  GMMR0QueryMemoryStatsReq(PVM pVM, VMCPUID idCpu, PGMMMEMSTATSREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(GMMMEMSTATSREQ),
                    ("%#x < %#x\n", pReq->Hdr.cbReq, sizeof(GMMMEMSTATSREQ)),
                    VERR_INVALID_PARAMETER);

    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Take the sempahore and do some more validations.
     */
    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        pReq->cAllocPages     = pGVM->gmm.s.Allocated.cBasePages;
        pReq->cBalloonedPages = pGVM->gmm.s.cBalloonedPages;
        pReq->cMaxPages       = pGVM->gmm.s.Reserved.cBasePages;
        pReq->cFreePages      = pReq->cMaxPages - pReq->cAllocPages;
    }
    else
        rc = VERR_INTERNAL_ERROR_5;

    RTSemFastMutexRelease(pGMM->Mtx);
    LogFlow(("GMMR3QueryVMMemoryStats: returns %Rrc\n", rc));
    return rc;
}

/**
 * Unmaps a chunk previously mapped into the address space of the current process.
 *
 * @returns VBox status code.
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   pGVM        Pointer to the Global VM structure.
 * @param   pChunk      Pointer to the chunk to be unmapped.
 */
static int gmmR0UnmapChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk)
{
    if (!pGMM->fLegacyAllocationMode)
    {
        /*
         * Find the mapping and try unmapping it.
         */
        for (uint32_t i = 0; i < pChunk->cMappings; i++)
        {
            Assert(pChunk->paMappings[i].pGVM && pChunk->paMappings[i].MapObj != NIL_RTR0MEMOBJ);
            if (pChunk->paMappings[i].pGVM == pGVM)
            {
                /* unmap */
                int rc = RTR0MemObjFree(pChunk->paMappings[i].MapObj, false /* fFreeMappings (NA) */);
                if (RT_SUCCESS(rc))
                {
                    /* update the record. */
                    pChunk->cMappings--;
                    if (i < pChunk->cMappings)
                        pChunk->paMappings[i] = pChunk->paMappings[pChunk->cMappings];
                    pChunk->paMappings[pChunk->cMappings].MapObj = NIL_RTR0MEMOBJ;
                    pChunk->paMappings[pChunk->cMappings].pGVM = NULL;
                }
                return rc;
            }
        }
    }
    else if (pChunk->hGVM == pGVM->hSelf)
        return VINF_SUCCESS;

    Log(("gmmR0MapChunk: Chunk %#x is not mapped into pGVM=%p/%#x\n", pChunk->Core.Key, pGVM, pGVM->hSelf));
    return VERR_GMM_CHUNK_NOT_MAPPED;
}


/**
 * Maps a chunk into the user address space of the current process.
 *
 * @returns VBox status code.
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   pGVM        Pointer to the Global VM structure.
 * @param   pChunk      Pointer to the chunk to be mapped.
 * @param   ppvR3       Where to store the ring-3 address of the mapping.
 *                      In the VERR_GMM_CHUNK_ALREADY_MAPPED case, this will be
 *                      contain the address of the existing mapping.
 */
static int gmmR0MapChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk, PRTR3PTR ppvR3)
{
    /*
     * If we're in legacy mode this is simple.
     */
    if (pGMM->fLegacyAllocationMode)
    {
        if (pChunk->hGVM != pGVM->hSelf)
        {
            Log(("gmmR0MapChunk: chunk %#x is already mapped at %p!\n", pChunk->Core.Key, *ppvR3));
            return VERR_GMM_CHUNK_NOT_FOUND;
        }

        *ppvR3 = RTR0MemObjAddressR3(pChunk->MemObj);
        return VINF_SUCCESS;
    }

    /*
     * Check to see if the chunk is already mapped.
     */
    for (uint32_t i = 0; i < pChunk->cMappings; i++)
    {
        Assert(pChunk->paMappings[i].pGVM && pChunk->paMappings[i].MapObj != NIL_RTR0MEMOBJ);
        if (pChunk->paMappings[i].pGVM == pGVM)
        {
            *ppvR3 = RTR0MemObjAddressR3(pChunk->paMappings[i].MapObj);
            Log(("gmmR0MapChunk: chunk %#x is already mapped at %p!\n", pChunk->Core.Key, *ppvR3));
            return VERR_GMM_CHUNK_ALREADY_MAPPED;
        }
    }

    /*
     * Do the mapping.
     */
    RTR0MEMOBJ MapObj;
    int rc = RTR0MemObjMapUser(&MapObj, pChunk->MemObj, (RTR3PTR)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE, NIL_RTR0PROCESS);
    if (RT_SUCCESS(rc))
    {
        /* reallocate the array? */
        if ((pChunk->cMappings & 1 /*7*/) == 0)
        {
            void *pvMappings = RTMemRealloc(pChunk->paMappings, (pChunk->cMappings + 2 /*8*/) * sizeof(pChunk->paMappings[0]));
            if (RT_UNLIKELY(!pvMappings))
            {
                rc = RTR0MemObjFree(MapObj, false /* fFreeMappings (NA) */);
                AssertRC(rc);
                return VERR_NO_MEMORY;
            }
            pChunk->paMappings = (PGMMCHUNKMAP)pvMappings;
        }

        /* insert new entry */
        pChunk->paMappings[pChunk->cMappings].MapObj = MapObj;
        pChunk->paMappings[pChunk->cMappings].pGVM = pGVM;
        pChunk->cMappings++;

        *ppvR3 = RTR0MemObjAddressR3(MapObj);
    }

    return rc;
}

/**
 * Check if a chunk is mapped into the specified VM
 *
 * @returns mapped yes/no
 * @param   pGVM        Pointer to the Global VM structure.
 * @param   pChunk      Pointer to the chunk to be mapped.
 * @param   ppvR3       Where to store the ring-3 address of the mapping.
 */
static int gmmR0IsChunkMapped(PGVM pGVM, PGMMCHUNK pChunk, PRTR3PTR ppvR3)
{
    /*
     * Check to see if the chunk is already mapped.
     */
    for (uint32_t i = 0; i < pChunk->cMappings; i++)
    {
        Assert(pChunk->paMappings[i].pGVM && pChunk->paMappings[i].MapObj != NIL_RTR0MEMOBJ);
        if (pChunk->paMappings[i].pGVM == pGVM)
        {
            *ppvR3 = RTR0MemObjAddressR3(pChunk->paMappings[i].MapObj);
            return true;
        }
    }
    *ppvR3 = NULL;
    return false;
}

/**
 * Map a chunk and/or unmap another chunk.
 *
 * The mapping and unmapping applies to the current process.
 *
 * This API does two things because it saves a kernel call per mapping when
 * when the ring-3 mapping cache is full.
 *
 * @returns VBox status code.
 * @param   pVM             The VM.
 * @param   idCpu           VCPU id
 * @param   idChunkMap      The chunk to map. NIL_GMM_CHUNKID if nothing to map.
 * @param   idChunkUnmap    The chunk to unmap. NIL_GMM_CHUNKID if nothing to unmap.
 * @param   ppvR3           Where to store the address of the mapped chunk. NULL is ok if nothing to map.
 * @thread  EMT
 */
GMMR0DECL(int) GMMR0MapUnmapChunk(PVM pVM, VMCPUID idCpu, uint32_t idChunkMap, uint32_t idChunkUnmap, PRTR3PTR ppvR3)
{
    LogFlow(("GMMR0MapUnmapChunk: pVM=%p idChunkMap=%#x idChunkUnmap=%#x ppvR3=%p\n",
             pVM, idChunkMap, idChunkUnmap, ppvR3));

    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    AssertCompile(NIL_GMM_CHUNKID == 0);
    AssertMsgReturn(idChunkMap <= GMM_CHUNKID_LAST, ("%#x\n", idChunkMap), VERR_INVALID_PARAMETER);
    AssertMsgReturn(idChunkUnmap <= GMM_CHUNKID_LAST, ("%#x\n", idChunkUnmap), VERR_INVALID_PARAMETER);

    if (    idChunkMap == NIL_GMM_CHUNKID
        &&  idChunkUnmap == NIL_GMM_CHUNKID)
        return VERR_INVALID_PARAMETER;

    if (idChunkMap != NIL_GMM_CHUNKID)
    {
        AssertPtrReturn(ppvR3, VERR_INVALID_POINTER);
        *ppvR3 = NIL_RTR3PTR;
    }

    /*
     * Take the semaphore and do the work.
     *
     * The unmapping is done last since it's easier to undo a mapping than
     * undoing an unmapping. The ring-3 mapping cache cannot not be so big
     * that it pushes the user virtual address space to within a chunk of
     * it it's limits, so, no problem here.
     */
    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        PGMMCHUNK pMap = NULL;
        if (idChunkMap != NIL_GVM_HANDLE)
        {
            pMap = gmmR0GetChunk(pGMM, idChunkMap);
            if (RT_LIKELY(pMap))
                rc = gmmR0MapChunk(pGMM, pGVM, pMap, ppvR3);
            else
            {
                Log(("GMMR0MapUnmapChunk: idChunkMap=%#x\n", idChunkMap));
                rc = VERR_GMM_CHUNK_NOT_FOUND;
            }
        }

        if (    idChunkUnmap != NIL_GMM_CHUNKID
            &&  RT_SUCCESS(rc))
        {
            PGMMCHUNK pUnmap = gmmR0GetChunk(pGMM, idChunkUnmap);
            if (RT_LIKELY(pUnmap))
                rc = gmmR0UnmapChunk(pGMM, pGVM, pUnmap);
            else
            {
                Log(("GMMR0MapUnmapChunk: idChunkUnmap=%#x\n", idChunkUnmap));
                rc = VERR_GMM_CHUNK_NOT_FOUND;
            }

            if (RT_FAILURE(rc) && pMap)
                gmmR0UnmapChunk(pGMM, pGVM, pMap);
        }

        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_INTERNAL_ERROR_5;
    RTSemFastMutexRelease(pGMM->Mtx);

    LogFlow(("GMMR0MapUnmapChunk: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0MapUnmapChunk.
 *
 * @returns see GMMR0MapUnmapChunk.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           VCPU id
 * @param   pReq            The request packet.
 */
GMMR0DECL(int)  GMMR0MapUnmapChunkReq(PVM pVM, VMCPUID idCpu, PGMMMAPUNMAPCHUNKREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GMMR0MapUnmapChunk(pVM, idCpu, pReq->idChunkMap, pReq->idChunkUnmap, &pReq->pvR3);
}


/**
 * Legacy mode API for supplying pages.
 *
 * The specified user address points to a allocation chunk sized block that
 * will be locked down and used by the GMM when the GM asks for pages.
 *
 * @returns VBox status code.
 * @param   pVM             The VM.
 * @param   idCpu           VCPU id
 * @param   pvR3            Pointer to the chunk size memory block to lock down.
 */
GMMR0DECL(int) GMMR0SeedChunk(PVM pVM, VMCPUID idCpu, RTR3PTR pvR3)
{
    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    AssertPtrReturn(pvR3, VERR_INVALID_POINTER);
    AssertReturn(!(PAGE_OFFSET_MASK & pvR3), VERR_INVALID_POINTER);

    if (!pGMM->fLegacyAllocationMode)
    {
        Log(("GMMR0SeedChunk: not in legacy allocation mode!\n"));
        return VERR_NOT_SUPPORTED;
    }

    /*
     * Lock the memory before taking the semaphore.
     */
    RTR0MEMOBJ MemObj;
    rc = RTR0MemObjLockUser(&MemObj, pvR3, GMM_CHUNK_SIZE, RTMEM_PROT_READ | RTMEM_PROT_WRITE, NIL_RTR0PROCESS);
    if (RT_SUCCESS(rc))
    {
        /* Grab the lock. */
        rc = RTSemFastMutexRequest(pGMM->Mtx);
        AssertRCReturn(rc, rc);

        /*
         * Add a new chunk with our hGVM.
         */
        rc = gmmR0RegisterChunk(pGMM, &pGMM->Private, MemObj, pGVM->hSelf, GMMCHUNKTYPE_NON_CONTINUOUS);
        RTSemFastMutexRelease(pGMM->Mtx);

        if (RT_FAILURE(rc))
            RTR0MemObjFree(MemObj, false /* fFreeMappings */);
    }

    LogFlow(("GMMR0SeedChunk: rc=%d (pvR3=%p)\n", rc, pvR3));
    return rc;
}


/**
 * Registers a new shared module for the VM
 *
 * @returns VBox status code.
 * @param   pVM                 VM handle
 * @param   idCpu               VCPU id
 * @param   pszModuleName       Module name
 * @param   pszVersion          Module version
 * @param   GCBaseAddr          Module base address
 * @param   cbModule            Module size
 * @param   cRegions            Number of shared region descriptors
 * @param   pRegions            Shared region(s)
 */
GMMR0DECL(int) GMMR0RegisterSharedModule(PVM pVM, VMCPUID idCpu, char *pszModuleName, char *pszVersion, RTGCPTR GCBaseAddr, uint32_t cbModule,
                                         unsigned cRegions, VMMDEVSHAREDREGIONDESC *pRegions)
{
#ifdef VBOX_WITH_PAGE_SHARING
    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Take the sempahore and do some more validations.
     */
    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        bool fNewModule = false;

        /* Check if this module is already locally registered. */
        PGMMSHAREDMODULEPERVM pRecVM = (PGMMSHAREDMODULEPERVM)RTAvlGCPtrGet(&pGVM->gmm.s.pSharedModuleTree, GCBaseAddr);
        if (!pRecVM)
        {
            pRecVM = (PGMMSHAREDMODULEPERVM)RTMemAllocZ(sizeof(*pRecVM));
            if (!pRecVM)
            {
                AssertFailed();
                rc = VERR_NO_MEMORY;
                goto end;
            }
            pRecVM->Core.Key = GCBaseAddr;

            bool ret = RTAvlGCPtrInsert(&pGVM->gmm.s.pSharedModuleTree, &pRecVM->Core);
            Assert(ret);

            fNewModule = true;
        }
        else
            rc = VINF_PGM_SHARED_MODULE_ALREADY_REGISTERED;

        /* Check if this module is already globally registered. */
        PGMMSHAREDMODULE pGlobalModule = (PGMMSHAREDMODULE)RTAvlGCPtrGet(&pGMM->pGlobalSharedModuleTree, GCBaseAddr);
        if (!pGlobalModule)
        {
            Assert(fNewModule);
            Assert(!pRecVM->fCollision);

            pGlobalModule = (PGMMSHAREDMODULE)RTMemAllocZ(RT_OFFSETOF(GMMSHAREDMODULE, aRegions[cRegions]));
            if (!pGlobalModule)
            {
                AssertFailed();
                rc = VERR_NO_MEMORY;
                goto end;
            }

            pGlobalModule->Core.Key = GCBaseAddr;
            pGlobalModule->cbModule = cbModule;
            /* Input limit already safe; no need to check again. */
            /** todo replace with RTStrCopy */
            strcpy(pGlobalModule->szName, pszModuleName);
            strcpy(pGlobalModule->szVersion, pszVersion);

            pGlobalModule->cRegions = cRegions;

            for (unsigned i = 0; i < cRegions; i++)
            {
                pGlobalModule->aRegions[i].GCRegionAddr      = pRegions[i].GCRegionAddr;
                pGlobalModule->aRegions[i].cbRegion          = pRegions[i].cbRegion;
                pGlobalModule->aRegions[i].u32Alignment      = 0;
                pGlobalModule->aRegions[i].paHCPhysPageID    = NULL; /* uninitialized. */
            }

            /* Save reference. */
            pRecVM->pGlobalModule = pGlobalModule;
            pRecVM->fCollision    = false;
            pGlobalModule->cUsers++;
            rc = VINF_SUCCESS;
        }
        else
        {
            Assert(pGlobalModule->cUsers > 0);

            /* Make sure the name and version are identical. */
            /** todo replace with RTStrNCmp */
            if (    !strcmp(pGlobalModule->szName, pszModuleName)
                &&  !strcmp(pGlobalModule->szVersion, pszVersion))
            {
                /* Save reference. */
                pRecVM->pGlobalModule = pGlobalModule;
                if (    fNewModule
                    ||  pRecVM->fCollision == true) /* colliding module unregistered and new one registerd since the last check */
                    pGlobalModule->cUsers++;

                pRecVM->fCollision    = false;
                rc = VINF_SUCCESS;
            }
            else
            {
                pRecVM->fCollision = true;
                rc = VINF_PGM_SHARED_MODULE_COLLISION;
                goto end;
            }
        }

        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_INTERNAL_ERROR_5;

end:
    RTSemFastMutexRelease(pGMM->Mtx);
    return rc;
#else
    return VERR_NOT_IMPLEMENTED;
#endif
}


/**
 * VMMR0 request wrapper for GMMR0RegisterSharedModule.
 *
 * @returns see GMMR0RegisterSharedModule.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           VCPU id
 * @param   pReq            The request packet.
 */
GMMR0DECL(int)  GMMR0RegisterSharedModuleReq(PVM pVM, VMCPUID idCpu, PGMMREGISTERSHAREDMODULEREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq >= sizeof(*pReq) && pReq->Hdr.cbReq == RT_UOFFSETOF(GMMREGISTERSHAREDMODULEREQ, aRegions[pReq->cRegions]), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GMMR0RegisterSharedModule(pVM, idCpu, pReq->szName, pReq->szVersion, pReq->GCBaseAddr, pReq->cbModule, pReq->cRegions, pReq->aRegions);
}

/**
 * Unregisters a shared module for the VM
 *
 * @returns VBox status code.
 * @param   pVM                 VM handle
 * @param   idCpu               VCPU id
 * @param   pszModuleName       Module name
 * @param   pszVersion          Module version
 * @param   GCBaseAddr          Module base address
 * @param   cbModule            Module size
 */
GMMR0DECL(int) GMMR0UnregisterSharedModule(PVM pVM, VMCPUID idCpu, char *pszModuleName, char *pszVersion, RTGCPTR GCBaseAddr, uint32_t cbModule)
{
#ifdef VBOX_WITH_PAGE_SHARING
    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Take the sempahore and do some more validations.
     */
    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        PGMMSHAREDMODULEPERVM pRecVM = (PGMMSHAREDMODULEPERVM)RTAvlGCPtrGet(&pGVM->gmm.s.pSharedModuleTree, GCBaseAddr);
        if (!pRecVM)
        {
            rc = VERR_PGM_SHARED_MODULE_NOT_FOUND;
            goto end;
        }
        /* Remove reference to global shared module. */
        if (!pRecVM->fCollision)
        {
            PGMMSHAREDMODULE pRec = pRecVM->pGlobalModule;
            Assert(pRec);

            if (pRec)   /* paranoia */
            {
                Assert(pRec->cUsers);
                pRec->cUsers--;
                if (pRec->cUsers == 0)
                {
                    /* Free the ranges, but leave the pages intact as there might still be references; they will be cleared by the COW mechanism. */
                    for (unsigned i = 0; i < pRec->cRegions; i++)
                        if (pRec->aRegions[i].paHCPhysPageID)
                            RTMemFree(pRec->aRegions[i].paHCPhysPageID);

                    RTMemFree(pRec);
                }
            }
            else
                rc = VERR_PGM_SHARED_MODULE_REGISTRATION_INCONSISTENCY;
        }
        else
            Assert(!pRecVM->pGlobalModule);

        RTMemFree(pRecVM);

        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_INTERNAL_ERROR_5;

end:
    RTSemFastMutexRelease(pGMM->Mtx);
    return rc;
#else
    return VERR_NOT_IMPLEMENTED;
#endif
}

/**
 * VMMR0 request wrapper for GMMR0UnregisterSharedModule.
 *
 * @returns see GMMR0UnregisterSharedModule.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   idCpu           VCPU id
 * @param   pReq            The request packet.
 */
GMMR0DECL(int)  GMMR0UnregisterSharedModuleReq(PVM pVM, VMCPUID idCpu, PGMMUNREGISTERSHAREDMODULEREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GMMR0UnregisterSharedModule(pVM, idCpu, pReq->szName, pReq->szVersion, pReq->GCBaseAddr, pReq->cbModule);
}


#ifdef VBOX_WITH_PAGE_SHARING
/**
 * Checks specified shared module range for changes
 *
 * Performs the following tasks:
 * - if a shared page is new, then it changes the GMM page type to shared and returns it in the paPageDesc array
 * - if a shared page already exists, then it checks if the VM page is identical and if so frees the VM page and returns the shared page in the paPageDesc array
 *
 * @returns VBox status code.
 * @param   pVM                 VM handle
 * @param   idCpu               VCPU id
 * @param   pReq                Module description
 * @param   idxRegion           Region index
 * @param   cPages              Number of entries in the paPageDesc array
 * @param   paPageDesc          Page descriptor array (in/out)
 */
GMMR0DECL(int) GMMR0SharedModuleCheckRange(PVM pVM, VMCPUID idCpu, PGMMREGISTERSHAREDMODULEREQ pReq, unsigned idxRegion, unsigned cPages, PGMMSHAREDPAGEDESC paPageDesc)
{
    AssertReturn(idxRegion < pReq->cRegions, VERR_INVALID_PARAMETER);
    AssertReturn(cPages == (pReq->aRegions[idxRegion].cbRegion >> PAGE_SHIFT), VERR_INVALID_PARAMETER);

    Log(("GMMR0SharedModuleCheckRange %s base %RGv region %d cPages %d\n", pReq->szName, pReq->GCBaseAddr, idxRegion, cPages));

    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Take the sempahore and do some more validations.
     */
    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        PGMMSHAREDMODULEPERVM pLocalModule = (PGMMSHAREDMODULEPERVM)RTAvlGCPtrGet(&pGVM->gmm.s.pSharedModuleTree, pReq->GCBaseAddr);
        if (    !pLocalModule
            ||  pLocalModule->fCollision)
        {
            Assert(!pLocalModule);
            rc = VERR_PGM_SHARED_MODULE_NOT_FOUND;
            goto end;
        }

        PGMMSHAREDMODULE     pGlobalModule = pLocalModule->pGlobalModule;
        PGMMSHAREDREGIONDESC pGlobalRegion = &pGlobalModule->aRegions[idxRegion];

        if (!pGlobalRegion->paHCPhysPageID)
        {
            /* First time; create a page descriptor array. */
            pGlobalRegion->paHCPhysPageID = (uint32_t *)RTMemAlloc(cPages * sizeof(*pGlobalRegion->paHCPhysPageID));
            if (!pGlobalRegion->paHCPhysPageID)
            {
                AssertFailed();
                rc = VERR_NO_MEMORY;
                goto end;
            }
            /* Invalidate all descriptors. */
            for (unsigned i = 0; i < cPages; i++)
                pGlobalRegion->paHCPhysPageID[i] = NIL_GMM_PAGEID;
        }

        /* Check all pages in the region. */
        for (unsigned i = 0; i < cPages; i++)
        {
            /* Valid page present? */
            if (paPageDesc[i].uHCPhysPageId != NIL_GMM_PAGEID)
            {
                /* We've seen this shared page for the first time? */
                if (pGlobalRegion->paHCPhysPageID == NIL_GMM_PAGEID)
                {
                    /* Easy case: just change the internal page type. */
                    PGMMPAGE pPage = gmmR0GetPage(pGMM, paPageDesc[i].uHCPhysPageId);
                    if (!pPage)
                    {
                        AssertFailed();
                        rc = VERR_PGM_PHYS_INVALID_PAGE_ID;
                        goto end;
                    }
                    Log(("New shared page guest %RGp host %RHp\n", paPageDesc[i].GCPhys, paPageDesc[i].HCPhys));

                    Assert(paPageDesc[i].HCPhys == (pPage->Private.pfn << 12));

                    gmmR0ConvertToSharedPage(pGMM, pGVM, paPageDesc[i].HCPhys, paPageDesc[i].uHCPhysPageId, pPage);

                    /* Keep track of these references. */
                    pGlobalRegion->paHCPhysPageID[i] = paPageDesc[i].uHCPhysPageId;
                }
                else
                {
                    uint8_t  *pbLocalPage, *pbSharedPage;
                    uint8_t  *pbChunk;
                    PGMMCHUNK pChunk;

                    Assert(paPageDesc[i].uHCPhysPageId != pGlobalRegion->paHCPhysPageID[i]);

                    /* Get the shared page source. */
                    PGMMPAGE pPage = gmmR0GetPage(pGMM, pGlobalRegion->paHCPhysPageID[i]);
                    if (!pPage)
                    {
                        AssertFailed();
                        rc = VERR_PGM_PHYS_INVALID_PAGE_ID;
                        goto end;
                    }
                    Assert(pPage->Common.u2State == GMM_PAGE_STATE_SHARED);

                    Log(("Replace existing page guest %RGp host %RHp -> %RHp\n", paPageDesc[i].GCPhys, paPageDesc[i].HCPhys, pPage->Shared.pfn << PAGE_SHIFT));

                    /* Calculate the virtual address of the local page. */
                    pChunk = gmmR0GetChunk(pGMM, paPageDesc[i].uHCPhysPageId >> GMM_CHUNKID_SHIFT);
                    if (pChunk)
                    {
                        if (!gmmR0IsChunkMapped(pGVM, pChunk, (PRTR3PTR)&pbChunk))
                        {
                            AssertFailed();
                            rc = VERR_PGM_PHYS_INVALID_PAGE_ID;
                            goto end;
                        }
                        pbLocalPage = pbChunk + ((paPageDesc[i].uHCPhysPageId & GMM_PAGEID_IDX_MASK) << PAGE_SHIFT);
                    }
                    else
                    {
                        AssertFailed();
                        rc = VERR_PGM_PHYS_INVALID_PAGE_ID;
                        goto end;
                    }
                    
                    /* Calculate the virtual address of the shared page. */
                    pChunk = gmmR0GetChunk(pGMM, pGlobalRegion->paHCPhysPageID[i] >> GMM_CHUNKID_SHIFT);
                    Assert(pChunk); /* can't fail as gmmR0GetPage succeeded. */

                    /* Get the virtual address of the physical page; map the chunk into the VM process if not already done. */
                    if (!gmmR0IsChunkMapped(pGVM, pChunk, (PRTR3PTR)&pbChunk))
                    {
                        rc = gmmR0MapChunk(pGMM, pGVM, pChunk, (PRTR3PTR)&pbChunk);
                        if (rc != VINF_SUCCESS)
                        {
                            AssertRC(rc);
                            goto end;
                        }
                    }
                    pbSharedPage = pbChunk + ((pGlobalRegion->paHCPhysPageID[i] & GMM_PAGEID_IDX_MASK) << PAGE_SHIFT);

                    /** todo write ASMMemComparePage. */
                    if (memcmp(pbSharedPage, pbLocalPage, PAGE_SIZE))
                    {
                        Log(("Unexpected differences found between local and shared page; skip\n"));
                        continue;
                    }

                    /* Free the old local page. */
                    GMMFREEPAGEDESC PageDesc;

                    PageDesc.idPage = paPageDesc[i].uHCPhysPageId;
                    rc = gmmR0FreePages(pGMM, pGVM, 1, &PageDesc, GMMACCOUNT_BASE);
                    AssertRC(rc);

                    gmmR0UseSharedPage(pGMM, pGVM, pPage);

                    /* Pass along the new physical address & page id. */
                    paPageDesc[i].HCPhys        = pPage->Shared.pfn << PAGE_SHIFT;
                    paPageDesc[i].uHCPhysPageId = pGlobalRegion->paHCPhysPageID[i];
                }
            }
        }

        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_INTERNAL_ERROR_5;

end:
    RTSemFastMutexRelease(pGMM->Mtx);
    return rc;
}

/**
 * RTAvlU32Destroy callback.
 *
 * @returns 0
 * @param   pNode   The node to destroy.
 * @param   pvGVM   The GVM handle.
 */
static DECLCALLBACK(int) gmmR0CleanupSharedModule(PAVLGCPTRNODECORE pNode, void *pvGVM)
{
    PGVM pGVM = (PGVM)pvGVM;
    PGMMSHAREDMODULEPERVM pRecVM = (PGMMSHAREDMODULEPERVM)pNode;

    Assert(pRecVM->pGlobalModule);
    if (pRecVM->pGlobalModule)
    {
        PGMMSHAREDMODULE pRec = pRecVM->pGlobalModule;
        Assert(pRec);
        Assert(pRec->cUsers);

        pRec->cUsers--;
        if (pRec->cUsers == 0)
        {
            for (unsigned i = 0; i < pRec->cRegions; i++)
                if (pRec->aRegions[i].paHCPhysPageID)
                    RTMemFree(pRec->aRegions[i].paHCPhysPageID);

            RTMemFree(pRec);
        }
    }
    RTMemFree(pRecVM);
    return 0;
}
#endif

/**
 * Removes all shared modules for the specified VM
 *
 * @returns VBox status code.
 * @param   pVM                 VM handle
 * @param   idCpu               VCPU id
 */
GMMR0DECL(int) GMMR0ResetSharedModules(PVM pVM, VMCPUID idCpu)
{
#ifdef VBOX_WITH_PAGE_SHARING
    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_INTERNAL_ERROR);
    PGVM pGVM;
    int rc = GVMMR0ByVMAndEMT(pVM, idCpu, &pGVM);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Take the sempahore and do some more validations.
     */
    rc = RTSemFastMutexRequest(pGMM->Mtx);
    AssertRC(rc);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        RTAvlGCPtrDestroy(&pGVM->gmm.s.pSharedModuleTree, gmmR0CleanupSharedModule, pGVM);

        rc = VINF_SUCCESS;
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_INTERNAL_ERROR_5;

    RTSemFastMutexRelease(pGMM->Mtx);
    return rc;      
#else
    return VERR_NOT_IMPLEMENTED;
#endif
}
