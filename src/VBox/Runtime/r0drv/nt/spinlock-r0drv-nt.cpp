/* $Id$ */
/** @file
 * InnoTek Portable Runtime - Spinlocks, Ring-0 Driver, NT.
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
#include "the-nt-kernel.h"
#include <iprt/spinlock.h>
#include <iprt/err.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/asm.h>


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Wrapper for the KSPIN_LOCK type.
 */
typedef struct RTSPINLOCKINTERNAL
{
    /** Spinlock magic value (RTSPINLOCK_MAGIC). */
    uint32_t volatile   u32Magic;
    /** The NT spinlock structure. */
    KSPIN_LOCK          Spinlock;
} RTSPINLOCKINTERNAL, *PRTSPINLOCKINTERNAL;

/** Magic value for RTSPINLOCKINTERNAL::u32Magic. (Terry Pratchett) */
#define RTSPINLOCK_MAGIC    0x19480428



RTDECL(int)  RTSpinlockCreate(PRTSPINLOCK pSpinlock)
{
    /*
     * Allocate.
     */
    Assert(sizeof(RTSPINLOCKINTERNAL) > sizeof(void *));
    PRTSPINLOCKINTERNAL pSpinlockInt = (PRTSPINLOCKINTERNAL)RTMemAlloc(sizeof(*pSpinlockInt));
    if (!pSpinlockInt)
        return VERR_NO_MEMORY;

    /*
     * Initialize & return.
     */
    pSpinlockInt->u32Magic = RTSPINLOCK_MAGIC;
    KeInitializeSpinLock(&pSpinlockInt->Spinlock);
    Assert(sizeof(KIRQL) == sizeof(unsigned char));

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
    Assert(pSpinlockInt && pSpinlockInt->u32Magic == RTSPINLOCK_MAGIC);

    KeAcquireSpinLock(&pSpinlockInt->Spinlock, &pTmp->uchIrqL);
    pTmp->uFlags = ASMGetFlags();
    ASMIntDisable();
}


RTDECL(void) RTSpinlockReleaseNoInts(RTSPINLOCK Spinlock, PRTSPINLOCKTMP pTmp)
{
    PRTSPINLOCKINTERNAL pSpinlockInt = (PRTSPINLOCKINTERNAL)Spinlock;
    Assert(pSpinlockInt && pSpinlockInt->u32Magic == RTSPINLOCK_MAGIC);

    ASMSetFlags(pTmp->uFlags);
    KeReleaseSpinLock(&pSpinlockInt->Spinlock, pTmp->uchIrqL);
}


RTDECL(void) RTSpinlockAcquire(RTSPINLOCK Spinlock, PRTSPINLOCKTMP pTmp)
{
    PRTSPINLOCKINTERNAL pSpinlockInt = (PRTSPINLOCKINTERNAL)Spinlock;
    Assert(pSpinlockInt && pSpinlockInt->u32Magic == RTSPINLOCK_MAGIC);

    KeAcquireSpinLock(&pSpinlockInt->Spinlock, &pTmp->uchIrqL);
}


RTDECL(void) RTSpinlockRelease(RTSPINLOCK Spinlock, PRTSPINLOCKTMP pTmp)
{
    PRTSPINLOCKINTERNAL pSpinlockInt = (PRTSPINLOCKINTERNAL)Spinlock;
    Assert(pSpinlockInt && pSpinlockInt->u32Magic == RTSPINLOCK_MAGIC);

    KeReleaseSpinLock(&pSpinlockInt->Spinlock, pTmp->uchIrqL);
}

