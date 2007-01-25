/* $Id$ */
/** @file
 * InnoTek Portable Runtime - A Simple Heap.
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
#define LOG_GROUP RTLOGGROUP_DEFAULT
#include <iprt/heap.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/param.h>


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/** Pointer to the heap anchor block. */
typedef struct RTHEAPSIMPLEINTERNAL *PRTHEAPSIMPLEINTERNAL;
/** Pointer to a heap block. */
typedef struct RTHEAPSIMPLEBLOCK *PRTHEAPSIMPLEBLOCK;
/** Pointer to a free heap block. */
typedef struct RTHEAPSIMPLEFREE *PRTHEAPSIMPLEFREE;

/**
 * Structure describing a simple heap block.
 * If this block is allocated, it is followed by the user user data.
 * If this block is free, see RTHEAPSIMPLEFREE.
 */
typedef struct RTHEAPSIMPLEBLOCK
{
    /** The next block in the global block list. */
    PRTHEAPSIMPLEBLOCK      pNext;
    /** The previous block in the global block list. */
    PRTHEAPSIMPLEBLOCK      pPrev;
    /** Pointer to the heap anchor block. */
    PRTHEAPSIMPLEINTERNAL   pHeap;
    /** Flags + magic. */
    uintptr_t               fFlags;
} RTHEAPSIMPLEBLOCK;
AssertCompileSizeAlignment(RTHEAPSIMPLEBLOCK, 16);

/** The block is free if this flag is set. When cleared it's allocated. */
#define RTHEAPSIMPLEBLOCK_FLAGS_FREE        ((uintptr_t)BIT(0))
/** The magic value. */
#define RTHEAPSIMPLEBLOCK_FLAGS_MAGIC       ((uintptr_t)0xabcdef00)
/** The mask that needs to be applied to RTHEAPSIMPLEBLOCK::fFalgs to obtain the magic value. */
#define RTHEAPSIMPLEBLOCK_FLAGS_MAGIC_MASK  (~(uintptr_t)BIT(0))

/**
 * Checks if the specified block is valid or not.
 * @returns boolean answer.
 * @param   pBlock      Pointer to a RTHEAPSIMPLEBLOCK structure.
 */
#define RTHEAPSIMPLEBLOCK_IS_VALID(pBlock)  \
    ( ((pBlock)->fFlags & RTHEAPSIMPLEBLOCK_FLAGS_MAGIC_MASK) == RTHEAPSIMPLEBLOCK_FLAGS_MAGIC )

/**
 * Checks if the specified block is valid and in use.
 * @returns boolean answer.
 * @param   pBlock      Pointer to a RTHEAPSIMPLEBLOCK structure.
 */
#define RTHEAPSIMPLEBLOCK_IS_VALID_USED(pBlock)  \
    ( ((pBlock)->fFlags & (RTHEAPSIMPLEBLOCK_FLAGS_MAGIC_MASK | RTHEAPSIMPLEBLOCK_FLAGS_FREE)) \
       == RTHEAPSIMPLEBLOCK_FLAGS_MAGIC )

/**
 * Checks if the specified block is valid and free.
 * @returns boolean answer.
 * @param   pBlock      Pointer to a RTHEAPSIMPLEBLOCK structure.
 */
#define RTHEAPSIMPLEBLOCK_IS_VALID_FREE(pBlock)  \
    ( ((pBlock)->fFlags & (RTHEAPSIMPLEBLOCK_FLAGS_MAGIC_MASK | RTHEAPSIMPLEBLOCK_FLAGS_FREE)) \
       == (RTHEAPSIMPLEBLOCK_FLAGS_MAGIC | RTHEAPSIMPLEBLOCK_FLAGS_FREE) )

/**
 * Checks if the specified block is free or not.
 * @returns boolean answer.
 * @param   pBlock      Pointer to a valid RTHEAPSIMPLEBLOCK structure.
 */
#define RTHEAPSIMPLEBLOCK_IS_FREE(pBlock)   (!!((pBlock)->fFlags & RTHEAPSIMPLEBLOCK_FLAGS_FREE))

/**
 * A free heap block.
 * This is an extended version of RTHEAPSIMPLEBLOCK that takes the unused
 * user data to store free list pointers and a cached size value.
 */
typedef struct RTHEAPSIMPLEFREE
{
    /** Core stuff. */
    RTHEAPSIMPLEBLOCK       Core;
    /** Pointer to the next free block. */
    PRTHEAPSIMPLEFREE       pNext;
    /** Pointer to the previous free block. */
    PRTHEAPSIMPLEFREE       pPrev;
    /** The size of the block (excluding the RTHEAPSIMPLEBLOCK part). */
    size_t                  cb;
    /** An alignment filler to make it a multiple of (sizeof(void *) * 2). */
    size_t                  Alignment;
} RTHEAPSIMPLEFREE;


/**
 * The heap anchor block.
 * This structure is placed at the head of the memory block specified to RTHeapSimpleInit(),
 * which means that the first RTHEAPSIMPLEBLOCK appears immediately after this structure.
 */
typedef struct RTHEAPSIMPLEINTERNAL
{
    /** The typical magic (RTHEAPSIMPLE_MAGIC). */
    size_t                  uMagic;
    /** The heap size. (This structure is not included!) */
    size_t                  cbHeap;
    /** Pointer to the end of the heap. */
    void                   *pvEnd;
    /** The amount of free memory in the heap. */
    size_t                  cbFree;
    /** Free head pointer. */
    PRTHEAPSIMPLEFREE       pFreeHead;
    /** Free tail pointer. */
    PRTHEAPSIMPLEFREE       pFreeTail;
    /** Make the size of this structure is a multiple of 32. */
    size_t                  auAlignment[2];
} RTHEAPSIMPLEINTERNAL;
AssertCompileSizeAlignment(RTHEAPSIMPLEINTERNAL, 32);

/** Magic number for RTHEAPSIMPLEINTERNAL::u32Magic. (Kyoichi Katayama) */
#define RTHEAPSIMPLE_MAGIC  0x19590105


/** The minimum allocation size. */
#define RTHEAPSIMPLE_MIN_BLOCK  (sizeof(RTHEAPSIMPLEBLOCK))
AssertCompile(RTHEAPSIMPLE_MIN_BLOCK >= sizeof(RTHEAPSIMPLEBLOCK));
AssertCompile(RTHEAPSIMPLE_MIN_BLOCK >= sizeof(RTHEAPSIMPLEFREE) - sizeof(RTHEAPSIMPLEBLOCK));

/** The minimum and default alignment.  */
#define RTHEAPSIMPLE_ALIGNMENT  (sizeof(RTHEAPSIMPLEBLOCK))


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#ifdef RT_STRICT
# define RTHEAPSIMPLE_STRICT 1
#endif

#define ASSERT_L(a, b)    AssertMsg((uintptr_t)(a) <  (uintptr_t)(b), ("a=%p b=%p\n", (uintptr_t)(a), (uintptr_t)(b)))
#define ASSERT_LE(a, b)   AssertMsg((uintptr_t)(a) <= (uintptr_t)(b), ("a=%p b=%p\n", (uintptr_t)(a), (uintptr_t)(b)))
#define ASSERT_G(a, b)    AssertMsg((uintptr_t)(a) >  (uintptr_t)(b), ("a=%p b=%p\n", (uintptr_t)(a), (uintptr_t)(b)))
#define ASSERT_GE(a, b)   AssertMsg((uintptr_t)(a) >= (uintptr_t)(b), ("a=%p b=%p\n", (uintptr_t)(a), (uintptr_t)(b)))
#define ASSERT_ALIGN(a)   AssertMsg(!((uintptr_t)(a) & (RTHEAPSIMPLE_ALIGNMENT - 1)), ("a=%p\n", (uintptr_t)(a)))

#define ASSERT_PREV(pHeapInt, pBlock)  \
    do { ASSERT_ALIGN((pBlock)->pPrev); \
         if ((pBlock)->pPrev) \
         { \
             ASSERT_L((pBlock)->pPrev, (pBlock)); \
             ASSERT_GE((pBlock)->pPrev, (pHeapInt) + 1); \
         } \
         else \
             Assert((pBlock) == (PRTHEAPSIMPLEBLOCK)((pHeapInt) + 1)); \
    } while (0)

#define ASSERT_NEXT(pHeap, pBlock) \
    do { ASSERT_ALIGN((pBlock)->pNext); \
         if ((pBlock)->pNext) \
         { \
             ASSERT_L((pBlock)->pNext, (pHeapInt)->pvEnd); \
             ASSERT_G((pBlock)->pNext, (pBlock)); \
         } \
    } while (0)

#define ASSERT_BLOCK(pHeapInt, pBlock) \
    do { AssertMsg(RTHEAPSIMPLEBLOCK_IS_VALID(pBlock), ("%#x\n", (pBlock)->fFlags)); \
         AssertMsg((pBlock)->pHeap == (pHeapInt), ("%p != %p\n", (pBlock)->pHeap, (pHeapInt))); \
         ASSERT_GE((pBlock), (pHeapInt) + 1); \
         ASSERT_L((pBlock), (pHeapInt)->pvEnd); \
         ASSERT_NEXT(pHeapInt, pBlock); \
         ASSERT_PREV(pHeapInt, pBlock); \
    } while (0)

#define ASSERT_BLOCK_USED(pHeapInt, pBlock) \
    do { AssertMsg(RTHEAPSIMPLEBLOCK_IS_VALID_USED((pBlock)), ("%#x\n", (pBlock)->fFlags)); \
         AssertMsg((pBlock)->pHeap == (pHeapInt), ("%p != %p\n", (pBlock)->pHeap, (pHeapInt))); \
         ASSERT_GE((pBlock), (pHeapInt) + 1); \
         ASSERT_L((pBlock), (pHeapInt)->pvEnd); \
         ASSERT_NEXT(pHeapInt, pBlock); \
         ASSERT_PREV(pHeapInt, pBlock); \
    } while (0)

#define ASSERT_FREE_PREV(pHeapInt, pBlock) \
    do { ASSERT_ALIGN((pBlock)->pPrev); \
         if ((pBlock)->pPrev) \
         { \
             ASSERT_GE((pBlock)->pPrev, (pHeapInt)->pFreeHead); \
             ASSERT_L((pBlock)->pPrev, (pBlock)); \
             ASSERT_LE((pBlock)->pPrev, (pBlock)->Core.pPrev); \
         } \
         else \
             Assert((pBlock) == (pHeapInt)->pFreeHead); \
    } while (0)

#define ASSERT_FREE_NEXT(pHeapInt, pBlock) \
    do { ASSERT_ALIGN((pBlock)->pNext); \
         if ((pBlock)->pNext) \
         { \
             ASSERT_LE((pBlock)->pNext, (pHeapInt)->pFreeTail); \
             ASSERT_G((pBlock)->pNext, (pBlock)); \
             ASSERT_GE((pBlock)->pNext, (pBlock)->Core.pNext); \
         } \
         else \
             Assert((pBlock) == (pHeapInt)->pFreeTail); \
    } while (0)

#ifdef RTHEAPSIMPLE_STRICT
# define ASSERT_FREE_CB(pHeapInt, pBlock) \
    do { size_t cbCalc = ((pBlock)->Core.pNext ? (uintptr_t)(pBlock)->Core.pNext : (uintptr_t)(pHeapInt)->pvEnd) \
                       - (uintptr_t)(pBlock) - sizeof(RTHEAPSIMPLEBLOCK); \
         AssertMsg((pBlock)->cb == cbCalc, ("cb=%#zx cbCalc=%#zx\n", (pBlock)->cb, cbCalc)); \
    } while (0)
#else
# define ASSERT_FREE_CB(pHeapInt, pBlock) do {} while (0)
#endif

/** Asserts that a free block is valid. */
#define ASSERT_BLOCK_FREE(pHeapInt, pBlock) \
    do { ASSERT_BLOCK(pHeapInt, &(pBlock)->Core); \
         Assert(RTHEAPSIMPLEBLOCK_IS_VALID_FREE(&(pBlock)->Core)); \
         ASSERT_GE((pBlock), (pHeapInt)->pFreeHead); \
         ASSERT_LE((pBlock), (pHeapInt)->pFreeTail); \
         ASSERT_FREE_NEXT(pHeapInt, pBlock); \
         ASSERT_FREE_PREV(pHeapInt, pBlock); \
         ASSERT_FREE_CB(pHeapInt, pBlock); \
    } while (0)

/** Asserts that the heap anchor block is ok. */
#define ASSERT_ANCHOR(pHeapInt) \
    do { AssertPtr(pHeapInt);\
         Assert((pHeapInt)->uMagic == RTHEAPSIMPLE_MAGIC); \
    } while (0)


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
#ifdef RTHEAPSIMPLE_STRICT
static void rtHeapSimpleAssertAll(PRTHEAPSIMPLEINTERNAL pHeapInt);
#endif
static PRTHEAPSIMPLEBLOCK rtHeapSimpleAllocBlock(PRTHEAPSIMPLEINTERNAL pHeapInt, size_t cb, size_t uAlignment);
static void rtHeapSimpleFreeBlock(PRTHEAPSIMPLEINTERNAL pHeapInt, PRTHEAPSIMPLEBLOCK pBlock);


/**
 * Initializes the heap.
 *
 * @returns IPRT status code on success.
 * @param   pHeap       Where to store the heap anchor block on success.
 * @param   pvMemory    Pointer to the heap memory.
 * @param   cbMemory    The size of the heap memory.
 */
RTDECL(int) RTHeapSimpleInit(PRTHEAPSIMPLE pHeap, void *pvMemory, size_t cbMemory)
{
    /*
     * Validate input. The imposed minimum heap size is just a convenien value.
     */
    AssertReturn(cbMemory >= PAGE_SIZE, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pvMemory, VERR_INVALID_POINTER);
    AssertReturn((uintptr_t)pvMemory + (cbMemory - 1) > (uintptr_t)cbMemory, VERR_INVALID_PARAMETER);

    /*
     * Place the heap anchor block at the start of the heap memory,
     * enforce 32 byte alignment of it. Also align the heap size correctly.
     */
    PRTHEAPSIMPLEINTERNAL pHeapInt = (PRTHEAPSIMPLEINTERNAL)pvMemory;
    if ((uintptr_t)pvMemory & 31)
    {
        const unsigned off = 32 - ((uintptr_t)pvMemory & 31);
        cbMemory -= off;
        pHeapInt = (PRTHEAPSIMPLEINTERNAL)((uintptr_t)pvMemory + off);
    }
    cbMemory &= ~(RTHEAPSIMPLE_ALIGNMENT - 1);


    /* Init the heap anchor block. */
    pHeapInt->uMagic = RTHEAPSIMPLE_MAGIC;
    pHeapInt->pvEnd = (uint8_t *)pHeapInt + cbMemory;
    pHeapInt->cbHeap = cbMemory;
    pHeapInt->cbFree = cbMemory
                     - sizeof(RTHEAPSIMPLEBLOCK)
                     - sizeof(RTHEAPSIMPLEINTERNAL);
    pHeapInt->pFreeTail = pHeapInt->pFreeHead = (PRTHEAPSIMPLEFREE)(pHeapInt + 1);
    for (unsigned i = 0; i < ELEMENTS(pHeapInt->auAlignment); i++)
        pHeapInt->auAlignment[i] = ~(size_t)0;

    /* Init the single free block. */
    PRTHEAPSIMPLEFREE pFree = pHeapInt->pFreeHead;
    pFree->Core.pNext = NULL;
    pFree->Core.pPrev = NULL;
    pFree->Core.pHeap = pHeapInt;
    pFree->Core.fFlags = RTHEAPSIMPLEBLOCK_FLAGS_MAGIC | RTHEAPSIMPLEBLOCK_FLAGS_FREE;
    pFree->pNext = NULL;
    pFree->pPrev = NULL;
    pFree->cb = pHeapInt->cbFree;

    *pHeap = pHeapInt;

#ifdef RTHEAPSIMPLE_STRICT
    rtHeapSimpleAssertAll(pHeapInt);
#endif
    return VINF_SUCCESS;
}



/**
 * Allocates memory from the specified simple heap.
 *
 * @returns Pointer to the allocated memory block on success.
 * @returns NULL if the request cannot be satisfied. (A VERR_NO_MEMORY condition.)
 *
 * @param   Heap        The heap to allocate the memory on.
 * @param   cb          The requested heap block size.
 * @param   cbAlignment The requested heap block alignment. Pass 0 for default alignment.
 *                      Must be a power of 2.
 */
RTDECL(void *) RTHeapSimpleAlloc(RTHEAPSIMPLE Heap, size_t cb, size_t cbAlignment)
{
    PRTHEAPSIMPLEINTERNAL pHeapInt = Heap;

    /*
     * Validate and adjust the input.
     */
    AssertPtrReturn(pHeapInt, NULL);
    if (cb < RTHEAPSIMPLE_MIN_BLOCK)
        cb = RTHEAPSIMPLE_MIN_BLOCK;
    else
        cb = RT_ALIGN_Z(cb, RTHEAPSIMPLE_ALIGNMENT);
    if (!cbAlignment)
        cbAlignment = RTHEAPSIMPLE_ALIGNMENT;
    else
    {
        Assert(!(cbAlignment & (cbAlignment - 1)));
        Assert((cbAlignment & ~(cbAlignment - 1)) == cbAlignment);
        if (cbAlignment < RTHEAPSIMPLE_ALIGNMENT)
            cbAlignment = RTHEAPSIMPLE_ALIGNMENT;
    }

    /*
     * Do the allocation.
     */
    PRTHEAPSIMPLEBLOCK pBlock = rtHeapSimpleAllocBlock(pHeapInt, cb, cbAlignment);
    if (RT_LIKELY(pBlock))
    {
        void *pv = pBlock + 1;
        return pv;
    }
    return NULL;
}


/**
 * Allocates zeroed memory from the specified simple heap.
 *
 * @returns Pointer to the allocated memory block on success.
 * @returns NULL if the request cannot be satisfied. (A VERR_NO_MEMORY condition.)
 *
 * @param   Heap        The heap to allocate the memory on.
 * @param   cb          The requested heap block size.
 * @param   cbAlignment The requested heap block alignment. Pass 0 for default alignment.
 *                      Must be a power of 2.
 */
RTDECL(void *) RTHeapSimpleAllocZ(RTHEAPSIMPLE Heap, size_t cb, size_t cbAlignment)
{
    PRTHEAPSIMPLEINTERNAL pHeapInt = Heap;

    /*
     * Validate and adjust the input.
     */
    AssertPtrReturn(pHeapInt, NULL);
    if (cb < RTHEAPSIMPLE_MIN_BLOCK)
        cb = RTHEAPSIMPLE_MIN_BLOCK;
    else
        cb = RT_ALIGN_Z(cb, RTHEAPSIMPLE_ALIGNMENT);
    if (!cbAlignment)
        cbAlignment = RTHEAPSIMPLE_ALIGNMENT;
    else
    {
        Assert(!(cbAlignment & (cbAlignment - 1)));
        Assert((cbAlignment & ~(cbAlignment - 1)) == cbAlignment);
        if (cbAlignment < RTHEAPSIMPLE_ALIGNMENT)
            cbAlignment = RTHEAPSIMPLE_ALIGNMENT;
    }

    /*
     * Do the allocation.
     */
    PRTHEAPSIMPLEBLOCK pBlock = rtHeapSimpleAllocBlock(pHeapInt, cb, cbAlignment);
    if (RT_LIKELY(pBlock))
    {
        void *pv = pBlock + 1;
        memset(pv, 0, cb);
        return pv;
    }
    return NULL;
}


/**
 * Allocates a block of memory from the specified heap.
 *
 * No parameter validation or adjustment is preformed.
 *
 * @returns Pointer to the allocated block.
 * @returns NULL on failure.
 * @param   pHeap       The heap.
 * @param   cb     	Size of the memory block to allocate.
 * @param   uAlignment  The alignment specifications for the allocated block.
 */
static PRTHEAPSIMPLEBLOCK rtHeapSimpleAllocBlock(PRTHEAPSIMPLEINTERNAL pHeapInt, size_t cb, size_t uAlignment)
{
#ifdef RTHEAPSIMPLE_STRICT
    rtHeapSimpleAssertAll(pHeapInt);
#endif

    /*
     * Search for a fitting block from the lower end of the heap.
     */
    PRTHEAPSIMPLEBLOCK  pRet = NULL;
    for (PRTHEAPSIMPLEFREE pFree = pHeapInt->pFreeHead;
         pFree;
         pFree = pFree->pNext)
    {
        ASSERT_BLOCK_FREE(pHeapInt, pFree);

        /*
         * Match for size and alignment.
         */
        if (pFree->cb < cb)
            continue;
        uintptr_t offAlign = (uintptr_t)(&pFree->Core + 1) & (uAlignment - 1);
        if (offAlign)
        {
            offAlign = uAlignment - offAlign;
            if (pFree->cb - offAlign < cb)
                continue;

            /*
             * Make a stack copy of the free block header and adjust the pointer.
             */
            RTHEAPSIMPLEFREE Free = *pFree;
            pFree = (PRTHEAPSIMPLEFREE)((uintptr_t)pFree + offAlign);

            /*
             * Donate offAlign bytes to the node in front of us.
             * If we're the head node, we'll have to create a fake node. We'll
             * mark it USED for simplicity.
             *
             * (Should this policy of donating memory to the guy in front of us
             * cause big 'leaks', we could create a new free node if there is room
             * for that.)
             */
            PRTHEAPSIMPLEBLOCK  pPrev = Free.Core.pPrev;
            if (pPrev)
            {
                AssertMsg(!RTHEAPSIMPLEBLOCK_IS_FREE(pPrev), ("Impossible!\n"));
                pPrev->pNext = &pFree->Core;
            }
            else
            {
                pPrev = (PRTHEAPSIMPLEBLOCK)(pHeapInt + 1);
                Assert(pPrev == &pFree->Core);
                pPrev->pPrev = NULL;
                pPrev->pNext = &pFree->Core;
                pPrev->pHeap = pHeapInt;
                pPrev->fFlags = RTHEAPSIMPLEBLOCK_FLAGS_MAGIC;
            }
            pHeapInt->cbFree -= offAlign;

            /*
             * Recreate pFree in the new position and adjust the neighbours.
             */
            *pFree = Free;

            /* the core */
            if (pFree->Core.pNext)
                pFree->Core.pNext->pPrev = &pFree->Core;
            pFree->Core.pPrev = pPrev;

            /* the free part */
            pFree->cb -= offAlign;
            if (pFree->pNext)
                pFree->pNext->pPrev = pFree;
            else
                pHeapInt->pFreeTail = pFree;
            if (pFree->pPrev)
                pFree->pPrev->pNext = pFree;
            else
                pHeapInt->pFreeHead = pFree;
            ASSERT_BLOCK_FREE(pHeapInt, pFree);
            ASSERT_BLOCK_USED(pHeapInt, pPrev);
        }

        /*
         * Split off a new FREE block?
         */
        if (pFree->cb >= cb + RT_ALIGN_Z(sizeof(RTHEAPSIMPLEFREE), RTHEAPSIMPLE_ALIGNMENT))
        {
            /*
             * Move the FREE block up to make room for the new USED block.
             */
            PRTHEAPSIMPLEFREE   pNew = (PRTHEAPSIMPLEFREE)((uintptr_t)&pFree->Core + cb + sizeof(RTHEAPSIMPLEBLOCK));

            pNew->Core.pNext = pFree->Core.pNext;
            if (pFree->Core.pNext)
                pFree->Core.pNext->pPrev = &pNew->Core;
            pNew->Core.pPrev = &pFree->Core;
            pNew->Core.pHeap = pHeapInt;
            pNew->Core.fFlags = RTHEAPSIMPLEBLOCK_FLAGS_MAGIC | RTHEAPSIMPLEBLOCK_FLAGS_FREE;

            pNew->pNext = pFree->pNext;
            if (pNew->pNext)
                pNew->pNext->pPrev = pNew;
            else
                pHeapInt->pFreeTail = pNew;
            pNew->pPrev = pFree->pPrev;
            if (pNew->pPrev)
                pNew->pPrev->pNext = pNew;
            else
                pHeapInt->pFreeHead = pNew;
            pNew->cb    = (pNew->Core.pNext ? (uintptr_t)pNew->Core.pNext : (uintptr_t)pHeapInt->pvEnd) \
                        - (uintptr_t)pNew - sizeof(RTHEAPSIMPLEBLOCK);
            ASSERT_BLOCK_FREE(pHeapInt, pNew);

            /*
             * Update the old FREE node making it a USED node.
             */
            pFree->Core.fFlags &= ~RTHEAPSIMPLEBLOCK_FLAGS_FREE;
            pFree->Core.pNext = &pNew->Core;
            pHeapInt->cbFree -= pFree->cb;
            pHeapInt->cbFree += pNew->cb;
            pRet = &pFree->Core;
            ASSERT_BLOCK_USED(pHeapInt, pRet);
        }
        else
        {
            /*
             * Link it out of the free list.
             */
            if (pFree->pNext)
                pFree->pNext->pPrev = pFree->pPrev;
            else
                pHeapInt->pFreeTail = pFree->pPrev;
            if (pFree->pPrev)
                pFree->pPrev->pNext = pFree->pNext;
            else
                pHeapInt->pFreeHead = pFree->pNext;

            /*
             * Convert it to a used block.
             */
            pHeapInt->cbFree -= pFree->cb;
            pFree->Core.fFlags &= ~RTHEAPSIMPLEBLOCK_FLAGS_FREE;
            pRet = &pFree->Core;
            ASSERT_BLOCK_USED(pHeapInt, pRet);
        }
        break;
    }

#ifdef RTHEAPSIMPLE_STRICT
    rtHeapSimpleAssertAll(pHeapInt);
#endif
    return pRet;
}




/**
 * Frees memory allocated from a simple heap.
 *
 * @param   Heap    The heap. This is optional and will only be used for strict assertions.
 * @param   pv      The heap block returned by RTHeapSimple
 */
RTDECL(void) RTHeapSimpleFree(RTHEAPSIMPLE Heap, void *pv)
{
    /*
     * Validate input.
     */
    if (!pv)
        return;
    AssertPtr(pv);
    Assert(RT_ALIGN_P(pv, RTHEAPSIMPLE_ALIGNMENT) == pv);

    /*
     * Get the block and heap. If in strict mode, validate these.
     */
    PRTHEAPSIMPLEBLOCK pBlock = (PRTHEAPSIMPLEBLOCK)pv - 1;
    PRTHEAPSIMPLEINTERNAL pHeapInt = pBlock->pHeap;
    ASSERT_BLOCK_USED(pHeapInt, pBlock);
    ASSERT_ANCHOR(pHeapInt);
    Assert(pHeapInt == (PRTHEAPSIMPLEINTERNAL)Heap || !Heap);

#ifdef RTHEAPSIMPLE_FREE_POISON
    /*
     * Poison the block.
     */
    const size_t cbBlock = (pBlock->pNext ? (uintptr_t)pBlock->pNext : (uintptr_t)pHeapInt->pvEnd)
                         - (uintptr_t)pBlock - sizeof(RTHEAPSIMPLEBLOCK);
    memset(pBlock + 1, RTHEAPSIMPLE_FREE_POISON, cbBlock);
#endif

    /*
     * Call worker which does the actual job.
     */
    rtHeapSimpleFreeBlock(pHeapInt, pBlock);
}


/**
 * Free memory a memory block.
 *
 * @param   pHeapInt       The heap.
 * @param   pBlock         The memory block to free.
 */
static void rtHeapSimpleFreeBlock(PRTHEAPSIMPLEINTERNAL pHeapInt, PRTHEAPSIMPLEBLOCK pBlock)
{
    PRTHEAPSIMPLEFREE   pFree = (PRTHEAPSIMPLEFREE)pBlock;

#ifdef RTHEAPSIMPLE_STRICT
    rtHeapSimpleAssertAll(pHeapInt);
#endif

    /*
     * Look for the closest free list blocks by walking the blocks right
     * of us (both list are sorted on address).
     */
    PRTHEAPSIMPLEFREE   pLeft = NULL;
    PRTHEAPSIMPLEFREE   pRight = NULL;
    if (pHeapInt->pFreeTail)
    {
        pRight = (PRTHEAPSIMPLEFREE)pFree->Core.pNext;
        while (pRight && !RTHEAPSIMPLEBLOCK_IS_FREE(&pRight->Core))
        {
            ASSERT_BLOCK(pHeapInt, &pRight->Core);
            pRight = (PRTHEAPSIMPLEFREE)pRight->Core.pNext;
        }
        if (!pRight)
            pLeft = pHeapInt->pFreeTail;
        else
        {
            ASSERT_BLOCK_FREE(pHeapInt, pRight);
            pLeft = pRight->pPrev;
        }
        if (pLeft)
            ASSERT_BLOCK_FREE(pHeapInt, pLeft);
    }
    AssertMsgReturnVoid(pLeft != pFree, ("Freed twice! pv=%p (pBlock=%p)\n", pBlock + 1, pBlock));
    ASSERT_L(pLeft, pFree);
    Assert(!pRight || (uintptr_t)pRight > (uintptr_t)pFree);
    Assert(!pLeft || pLeft->pNext == pRight);

    /*
     * Insert at the head of the free block list?
     */
    if (!pLeft)
    {
        Assert(pRight == pHeapInt->pFreeHead);
        pFree->Core.fFlags |= RTHEAPSIMPLEBLOCK_FLAGS_FREE;
        pFree->pPrev = NULL;
        pFree->pNext = pRight;
        if (pRight)
            pRight->pPrev = pFree;
        else
            pHeapInt->pFreeTail = pFree;
        pHeapInt->pFreeHead = pFree;
    }
    else
    {
        /*
         * Can we merge with left hand free block?
         */
        if (pLeft->Core.pNext == &pFree->Core)
        {
            pLeft->Core.pNext = pFree->Core.pNext;
            if (pFree->Core.pNext)
                pFree->Core.pNext->pPrev = &pLeft->Core;
            pHeapInt->cbFree -= pLeft->cb;
            pFree = pLeft;
        }
        /*
         * No, just link it into the free list then.
         */
        else
        {
            pFree->Core.fFlags |= RTHEAPSIMPLEBLOCK_FLAGS_FREE;
            pFree->pNext = pRight;
            pFree->pPrev = pLeft;
            pLeft->pNext = pFree;
            if (pRight)
                pRight->pPrev = pFree;
            else
                pHeapInt->pFreeTail = pFree;
        }
    }

    /*
     * Can we merge with right hand free block?
     */
    if (    pRight
        &&  pRight->Core.pPrev == &pFree->Core)
    {
        /* core */
        pFree->Core.pNext = pRight->Core.pNext;
        if (pRight->Core.pNext)
            pRight->Core.pNext->pPrev = &pFree->Core;

        /* free */
        pFree->pNext = pRight->pNext;
        if (pRight->pNext)
            pRight->pNext->pPrev = pFree;
        else
            pHeapInt->pFreeTail = pFree;
        pHeapInt->cbFree -= pRight->cb;
    }

    /*
     * Calculate the size and update free stats.
     */
    pFree->cb = (pFree->Core.pNext ? (uintptr_t)pFree->Core.pNext : (uintptr_t)pHeapInt->pvEnd)
              - (uintptr_t)pFree - sizeof(RTHEAPSIMPLEBLOCK);
    pHeapInt->cbFree += pFree->cb;
    ASSERT_BLOCK_FREE(pHeapInt, pFree);

#ifdef RTHEAPSIMPLE_STRICT
    rtHeapSimpleAssertAll(pHeapInt);
#endif
}


#ifdef RTHEAPSIMPLE_STRICT
/**
 * Internal consitency check (relying on assertions).
 * @param   pHeapInt
 */
static void rtHeapSimpleAssertAll(PRTHEAPSIMPLEINTERNAL pHeapInt)
{
    PRTHEAPSIMPLEFREE pPrev = NULL;
    PRTHEAPSIMPLEFREE pPrevFree = NULL;
    for (PRTHEAPSIMPLEFREE pBlock = (PRTHEAPSIMPLEFREE)(pHeapInt + 1);
         pBlock;
         pBlock = (PRTHEAPSIMPLEFREE)pBlock->Core.pNext)
    {
        if (RTHEAPSIMPLEBLOCK_IS_FREE(&pBlock->Core))
        {
            ASSERT_BLOCK_FREE(pHeapInt, pBlock);
            Assert(pBlock->pPrev == pPrevFree);
            Assert(pPrevFree || pHeapInt->pFreeHead == pBlock);
            pPrevFree = pBlock;
        }
        else
            ASSERT_BLOCK_USED(pHeapInt, &pBlock->Core);
        Assert(!pPrev || pPrev == (PRTHEAPSIMPLEFREE)pBlock->Core.pPrev);
        pPrev = pBlock;
    }
    Assert(pHeapInt->pFreeTail == pPrevFree);
}
#endif


/**
 * Gets the size of the specified heap block.
 *
 * @returns The actual size of the heap block.
 * @returns 0 if \a pv is NULL or it doesn't point to a valid heap block. An invalid \a pv
 *          can also cause traps or trigger assertions.
 * @param   Heap    The heap. This is optional and will only be used for strict assertions.
 * @param   pv      The heap block returned by RTHeapSimple
 */
RTDECL(size_t) RTHeapSimpleSize(RTHEAPSIMPLE Heap, void *pv)
{
    /*
     * Validate input.
     */
    if (!pv)
        return 0;
    AssertPtrReturn(pv, 0);
    AssertReturn(RT_ALIGN_P(pv, RTHEAPSIMPLE_ALIGNMENT) == pv, 0);

    /*
     * Get the block and heap. If in strict mode, validate these.
     */
    PRTHEAPSIMPLEBLOCK pBlock = (PRTHEAPSIMPLEBLOCK)pv - 1;
    PRTHEAPSIMPLEINTERNAL pHeapInt = pBlock->pHeap;
    ASSERT_BLOCK_USED(pHeapInt, pBlock);
    ASSERT_ANCHOR(pHeapInt);
    Assert(pHeapInt == (PRTHEAPSIMPLEINTERNAL)Heap || !Heap);

    /*
     * Calculate the block size.
     */
    const size_t cbBlock = (pBlock->pNext ? (uintptr_t)pBlock->pNext : (uintptr_t)pHeapInt->pvEnd)
                         - (uintptr_t)pBlock- sizeof(RTHEAPSIMPLEBLOCK);
    return cbBlock;
}


/**
 * Gets the size of the heap.
 *
 * This size includes all the internal heap structures. So, even if the heap is
 * empty the RTHeapSimpleGetFreeSize() will never reach the heap size returned
 * by this function.
 *
 * @returns The heap size.
 * @returns 0 if heap was safely detected as being bad.
 * @param   Heap    The heap.
 */
RTDECL(size_t) RTHeapSimpleGetHeapSize(RTHEAPSIMPLE Heap)
{
    if (Heap == NIL_RTHEAPSIMPLE)
        return 0;
    PRTHEAPSIMPLEINTERNAL pHeapInt = Heap;
    AssertPtrReturn(pHeapInt, 0);
    ASSERT_ANCHOR(pHeapInt);
    return pHeapInt->cbHeap;
}


/**
 * Returns the sum of all free heap blocks.
 *
 * This is the amount of memory you can theoretically allocate
 * if you do allocations exactly matching the free blocks.
 *
 * @returns The size of the free blocks.
 * @returns 0 if heap was safely detected as being bad.
 * @param   Heap    The heap.
 */
RTDECL(size_t) RTHeapSimpleGetFreeSize(RTHEAPSIMPLE Heap)
{
    if (Heap == NIL_RTHEAPSIMPLE)
        return 0;
    PRTHEAPSIMPLEINTERNAL pHeapInt = Heap;
    AssertPtrReturn(pHeapInt, 0);
    ASSERT_ANCHOR(pHeapInt);
    return pHeapInt->cbFree;
}


/**
 * Dumps the hypervisor heap.
 *
 * @param   Heap        The heap handle.
 * @param   pfnPrintf   Printf like function that groks IPRT formatting.
 */
RTDECL(void) RTHeapSimpleDump(RTHEAPSIMPLE Heap, PFNRTHEAPSIMPLEPRINTF pfnPrintf)
{
    PRTHEAPSIMPLEINTERNAL pHeapInt = (PRTHEAPSIMPLEINTERNAL)Heap;
    pfnPrintf("**** Dumping Heap %p - cbHeap=%zx cbFree=%zx ****\n",
              Heap, pHeapInt->cbHeap, pHeapInt->cbFree);

    for (PRTHEAPSIMPLEFREE pBlock = (PRTHEAPSIMPLEFREE)(pHeapInt + 1);
         pBlock;
         pBlock = (PRTHEAPSIMPLEFREE)pBlock->Core.pNext)
    {
        size_t cb = (pBlock->pNext ? (uintptr_t)pBlock->Core.pNext : (uintptr_t)pHeapInt->pvEnd)
                  - (uintptr_t)pBlock - sizeof(RTHEAPSIMPLEBLOCK);
        if (RTHEAPSIMPLEBLOCK_IS_FREE(&pBlock->Core))
            pfnPrintf("%p  %06x FREE pNext=%p pPrev=%p fFlags=%#x cb=%#06x : cb=%#06x pNext=%p pPrev=%p\n",
                      pBlock, (uintptr_t)pBlock - (uintptr_t)(pHeapInt + 1), pBlock->Core.pNext, pBlock->Core.pPrev, pBlock->Core.fFlags, cb,
                      pBlock->cb, pBlock->pNext, pBlock->pPrev);
        else
            pfnPrintf("%p  %06x USED pNext=%p pPrev=%p fFlags=%#x cb=%#06x\n",
                      pBlock, (uintptr_t)pBlock - (uintptr_t)(pHeapInt + 1), pBlock->Core.pNext, pBlock->Core.pPrev, pBlock->Core.fFlags, cb);
    }
    pfnPrintf("**** Done dumping Heap %p ****\n", Heap);
}

