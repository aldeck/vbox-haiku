/* $Id$ */
/** @file
 * InnoTek Portable Runtime - Spinlock, generic implementation.
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
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** @def RT_CFG_SPINLOCK_GENERIC_DO_SLEEP
 * Force cpu yields after spinning the number of times indicated by the define.
 * If 0 we will spin forever. */
#define RT_CFG_SPINLOCK_GENERIC_DO_SLEEP    100000


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/spinlock.h>
#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#if RT_CFG_SPINLOCK_GENERIC_DO_SLEEP
# include <iprt/thread.h>
#endif


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Generic spinlock structure.
 */
typedef struct RTSPINLOCKINTERNAL
{
    /** Spinlock magic value (RTSPINLOCK_MAGIC). */
    uint32_t            u32Magic;
    /** The spinlock. */
    uint32_t volatile   fLocked;
} RTSPINLOCKINTERNAL, *PRTSPINLOCKINTERNAL;

/** Magic value for RTSPINLOCKINTERNAL::u32Magic. (Terry Pratchett) */
#define RTSPINLOCK_MAGIC    0x19480428

RTDECL(int)  RTSpinlockCreate(PRTSPINLOCK pSpinlock)
{
    /*
     * Allocate.
     */
    PRTSPINLOCKINTERNAL pSpinlockInt;
    pSpinlockInt = (PRTSPINLOCKINTERNAL)RTMemAlloc(sizeof(*pSpinlockInt));
    if (!pSpinlockInt)
        return VERR_NO_MEMORY;

    /*
     * Initialize and return.
     */
    pSpinlockInt->u32Magic = RTSPINLOCK_MAGIC;
    ASMAtomicXchgU32(&pSpinlockInt->fLocked, 0);

    *pSpinlock = pSpinlockInt;
    return VINF_SUCCESS;
}


RTDECL(int)  RTSpinlockDestroy(RTSPINLOCK Spinlock)
{
    /*
     * Validate input.
     */
    PRTSPINLOCKINTERNAL pSpinlockInt = (PRTSPINLOCKINTERNAL)Spinlock;
    if (!pSpinlockInt)
        return VERR_INVALID_PARAMETER;
    if (pSpinlockInt->u32Magic != RTSPINLOCK_MAGIC)
    {
        AssertMsgFailed(("Invalid spinlock %p magic=%#x\n", pSpinlockInt, pSpinlockInt->u32Magic));
        return VERR_INVALID_PARAMETER;
    }

    ASMAtomicIncU32(&pSpinlockInt->u32Magic);
    RTMemFree(pSpinlockInt);
    return VINF_SUCCESS;
}


RTDECL(void) RTSpinlockAcquireNoInts(RTSPINLOCK Spinlock, PRTSPINLOCKTMP pTmp)
{
    PRTSPINLOCKINTERNAL pSpinlockInt = (PRTSPINLOCKINTERNAL)Spinlock;
    AssertMsg(pSpinlockInt && pSpinlockInt->u32Magic == RTSPINLOCK_MAGIC,
              ("pSpinlockInt=%p u32Magic=%08x\n", pSpinlockInt, pSpinlockInt ? (int)pSpinlockInt->u32Magic : 0));

    pTmp->uFlags = ASMGetFlags();
#if RT_CFG_SPINLOCK_GENERIC_DO_SLEEP
    for (;;)
    {
        ASMIntDisable();
        for (int c = RT_CFG_SPINLOCK_GENERIC_DO_SLEEP; c > 0; c--)
            if (ASMAtomicCmpXchgU32(&pSpinlockInt->fLocked, 1, 0))
                return;
        RTThreadYield();
    }
#else
    ASMIntDisable();
    while (!ASMAtomicCmpXchgU32(&pSpinlockInt->fLocked, 1, 0))
        /*nothing */;
#endif
}


RTDECL(void) RTSpinlockReleaseNoInts(RTSPINLOCK Spinlock, PRTSPINLOCKTMP pTmp)
{
    PRTSPINLOCKINTERNAL pSpinlockInt = (PRTSPINLOCKINTERNAL)Spinlock;
    AssertMsg(pSpinlockInt && pSpinlockInt->u32Magic == RTSPINLOCK_MAGIC,
              ("pSpinlockInt=%p u32Magic=%08x\n", pSpinlockInt, pSpinlockInt ? (int)pSpinlockInt->u32Magic : 0));
    NOREF(pSpinlockInt);

    if (!ASMAtomicCmpXchgU32(&pSpinlockInt->fLocked, 0, 1))
        AssertMsgFailed(("Spinlock %p was not locked!\n", pSpinlockInt));
    ASMSetFlags(pTmp->uFlags);
}


RTDECL(void) RTSpinlockAcquire(RTSPINLOCK Spinlock, PRTSPINLOCKTMP pTmp)
{
    PRTSPINLOCKINTERNAL pSpinlockInt = (PRTSPINLOCKINTERNAL)Spinlock;
    AssertMsg(pSpinlockInt && pSpinlockInt->u32Magic == RTSPINLOCK_MAGIC,
              ("pSpinlockInt=%p u32Magic=%08x\n", pSpinlockInt, pSpinlockInt ? (int)pSpinlockInt->u32Magic : 0));
    NOREF(pTmp);

#if RT_CFG_SPINLOCK_GENERIC_DO_SLEEP
    for (;;)
    {
        for (int c = RT_CFG_SPINLOCK_GENERIC_DO_SLEEP; c > 0; c--)
            if (ASMAtomicCmpXchgU32(&pSpinlockInt->fLocked, 1, 0))
                return;
        RTThreadYield();
    }
#else
    while (!ASMAtomicCmpXchgU32(&pSpinlockInt->fLocked, 1, 0))
        /*nothing */;
#endif
}


RTDECL(void) RTSpinlockRelease(RTSPINLOCK Spinlock, PRTSPINLOCKTMP pTmp)
{
    PRTSPINLOCKINTERNAL pSpinlockInt = (PRTSPINLOCKINTERNAL)Spinlock;
    AssertMsg(pSpinlockInt && pSpinlockInt->u32Magic == RTSPINLOCK_MAGIC,
              ("pSpinlockInt=%p u32Magic=%08x\n", pSpinlockInt, pSpinlockInt ? (int)pSpinlockInt->u32Magic : 0));
    NOREF(pTmp);

    if (!ASMAtomicCmpXchgU32(&pSpinlockInt->fLocked, 0, 1))
        AssertMsgFailed(("Spinlock %p was not locked!\n", pSpinlockInt));
}

