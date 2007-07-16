/** @file
 * innotek Portable Runtime - Spinlocks.
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
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */

#ifndef ___iprt_spinlock_h
#define ___iprt_spinlock_h

#include <iprt/cdefs.h>
#include <iprt/types.h>

__BEGIN_DECLS


/** @defgroup grp_rt_spinlock   RTSpinlock - Spinlocks
 * @ingroup grp_rt
 * @{
 */

/**
 * Temporary spinlock state variable.
 * All members are undefined and highly platform specific.
 */
typedef struct RTSPINLOCKTMP
{
#ifdef IN_RING0
# ifdef __LINUX__
    /** The saved [R|E]FLAGS. */
    unsigned long   flFlags;
#  define RTSPINLOCKTMP_INITIALIZER { 0 }

# elif defined(RT_OS_WINDOWS)
    /** The saved [R|E]FLAGS. */
    RTUINTREG       uFlags;
    /** The KIRQL. */
    unsigned char   uchIrqL;
#  define RTSPINLOCKTMP_INITIALIZER { 0, 0 }

# elif defined(__L4__)
    /** The saved [R|E]FLAGS. */
    unsigned long   flFlags;
#  define RTSPINLOCKTMP_INITIALIZER { 0 }

# elif defined(RT_OS_DARWIN)
    /** The saved [R|E]FLAGS. */
    RTUINTREG       uFlags;
#  define RTSPINLOCKTMP_INITIALIZER { 0 }

# elif defined(RT_OS_OS2) || defined(RT_OS_FREEBSD)
    /** The saved [R|E]FLAGS. (dummy) */
    RTUINTREG       uFlags;
#  define RTSPINLOCKTMP_INITIALIZER { 0 }

# else
#  error "Your OS is not supported.\n"
    /** The saved [R|E]FLAGS. */
    RTUINTREG       uFlags;
# endif

#else /* !IN_RING0 */
    /** The saved [R|E]FLAGS.
     * (RT spinlocks will by definition disable interrupts.) */
    RTUINTREG       uFlags;
# define RTSPINLOCKTMP_INITIALIZER { 0 }
#endif /* !IN_RING0 */
} RTSPINLOCKTMP;
/** Pointer to a temporary spinlock state variable. */
typedef RTSPINLOCKTMP *PRTSPINLOCKTMP;
/** Pointer to a const temporary spinlock state variable. */
typedef const RTSPINLOCKTMP *PCRTSPINLOCKTMP;

/** @def RTSPINLOCKTMP_INITIALIZER
 * What to assign to a RTSPINLOCKTMP at definition.
 */
#ifdef __DOXYGEN__
# define RTSPINLOCKTMP_INITIALIZER
#endif



/**
 * Creates a spinlock.
 *
 * @returns iprt status code.
 * @param   pSpinlock   Where to store the spinlock handle.
 */
RTDECL(int)  RTSpinlockCreate(PRTSPINLOCK pSpinlock);

/**
 * Destroys a spinlock created by RTSpinlockCreate().
 *
 * @returns iprt status code.
 * @param   Spinlock    Spinlock returned by RTSpinlockCreate().
 */
RTDECL(int)  RTSpinlockDestroy(RTSPINLOCK Spinlock);

/**
 * Acquires the spinlock.
 * Interrupts are disabled upon return.
 *
 * @param   Spinlock    The spinlock to acquire.
 * @param   pTmp        Where to save the state.
 */
RTDECL(void) RTSpinlockAcquireNoInts(RTSPINLOCK Spinlock, PRTSPINLOCKTMP pTmp);

/**
 * Releases the spinlock.
 *
 * @param   Spinlock    The spinlock to acquire.
 * @param   pTmp        The state to restore. (This better be the same as for the RTSpinlockAcquire() call!)
 */
RTDECL(void) RTSpinlockReleaseNoInts(RTSPINLOCK Spinlock, PRTSPINLOCKTMP pTmp);

/**
 * Acquires the spinlock.
 *
 * @param   Spinlock    The spinlock to acquire.
 * @param   pTmp        Where to save the state.
 */
RTDECL(void) RTSpinlockAcquire(RTSPINLOCK Spinlock, PRTSPINLOCKTMP pTmp);

/**
 * Releases the spinlock.
 *
 * @param   Spinlock    The spinlock to acquire.
 * @param   pTmp        The state to restore. (This better be the same as for the RTSpinlockAcquire() call!)
 */
RTDECL(void) RTSpinlockRelease(RTSPINLOCK Spinlock, PRTSPINLOCKTMP pTmp);


/** @} */

__END_DECLS

#endif

