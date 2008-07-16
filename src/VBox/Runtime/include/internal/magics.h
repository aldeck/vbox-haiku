/* $Id$ */
/** @file
 * IPRT - Internal Header Defining The Magic Numbers.
 */

/*
 * Copyright (C) 2007 Sun Microsystems, Inc.
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
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef ___internal_magics_h
#define ___internal_magics_h

/** @name Magic Numbers.
 * @{ */

/** The value of RTENVINTERNAL::u32Magic. (Rumiko Takahashi) */
#define RTENV_MAGIC                 0x19571010
/** The value of RTDIR::u32Magic. (Michael Ende) */
#define RTDIR_MAGIC                 0x19291112
/** The value of RTDIR::u32Magic after RTDirClose().  */
#define RTDIR_MAGIC_DEAD            0x19950829
/** Magic number for RTHEAPSIMPLEINTERNAL::u32Magic. (Kyoichi Katayama) */
#define RTHEAPSIMPLE_MAGIC          0x19590105
/** The magic value for RTLDRMODINTERNAL::u32Magic. (Alan Moore) */
#define RTLDRMOD_MAGIC              0x19531118
/** Magic number for heap blocks. (Edgar Allan Poe) */
#define RTMEMHDR_MAGIC              0x18090119
/** RTR0MEMOBJ::u32Magic. (Masakazu Katsura) */
#define RTR0MEMOBJ_MAGIC            0x19611210
/** Magic for the event semaphore structure. (Neil Gaiman) */
#define RTSEMEVENT_MAGIC            0x19601110
/** Magic for the multiple release event semaphore structure. (Isaac Asimov) */
#define RTSEMEVENTMULTI_MAGIC       0x19200102
/** Magic value for RTSEMFASTMUTEXINTERNAL::u32Magic. (John Ronald Reuel Tolkien) */
#define RTSEMFASTMUTEX_MAGIC        0x18920102
/** Dead magic value for RTSEMFASTMUTEXINTERNAL::u32Magic. */
#define RTSEMFASTMUTEX_MAGIC_DEAD   0x19730902
/** Magic for the mutex semaphore structure. (Douglas Adams) */
#define RTSEMMUTEX_MAGIC            0x19520311
/** Dead magic for the mutex semaphore structure. */
#define RTSEMMUTEX_MAGIC_DEAD       0x20010511
/** RTSEMRWINTERNAL::u32Magic value. (Kosuke Fujishima) */
#define RTSEMRW_MAGIC               0x19640707
/** Magic value for RTSPINLOCKINTERNAL::u32Magic. (Terry Pratchett) */
#define RTSPINLOCK_MAGIC            0x19480428
/** The value of RTSTREAM::u32Magic for a valid stream. */
#define RTSTREAM_MAGIC              0xe44e44ee
/** RTTHREADINT::u32Magic value. (Gilbert Keith Chesterton) */
#define RTTHREADINT_MAGIC           0x18740529
/** RTTHREADINT::u32Magic value for a dead thread. */
#define RTTHREADINT_MAGIC_DEAD      0x19360614
/** Magic number for timer handles. (Jared Mason Diamond) */
#define RTTIMER_MAGIC               0x19370910
/** Magic number for cache object headers. (Joseph Weizenbaum) */
#define RTOBJCACHE_MAGIC            0x19230108

/** @} */

#endif
