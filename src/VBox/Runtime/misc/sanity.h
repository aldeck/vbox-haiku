/* $Id$ */
/** @file
 * InnoTek Portable Runtime - Setup Sanity Checks, C and C++.
 */

/*
 * Copyright (c) 2007 InnoTek Systemberatung GmbH
 *
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
 *
 */

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/assert.h>

/*
 * Check that the IN_[RING3|RING0|GC] and [|R3_|R0_|GC_]ARCH_BITS
 * match up correctly.
 *
 * IPRT assumes r0 and r3 to has the same bit count.
 */

#if defined(IN_RING3) && ARCH_BITS != R3_ARCH_BITS
# error "defined(IN_RING3) && ARCH_BITS != R3_ARCH_BITS"
#endif
#if defined(IN_RING0) && ARCH_BITS != R0_ARCH_BITS
# error "defined(IN_RING0) && ARCH_BITS != R0_ARCH_BITS"
#endif
#if defined(IN_GC) && ARCH_BITS != GC_ARCH_BITS
# error "defined(IN_GC) && ARCH_BITS != GC_ARCH_BITS"
#endif
#if (defined(IN_RING0) || defined(IN_RING3)) && HC_ARCH_BITS != ARCH_BITS
# error "(defined(IN_RING0) || defined(IN_RING3)) && HC_ARCH_BITS != ARCH_BITS"
#endif
#if defined(IN_GC) && GC_ARCH_BITS != ARCH_BITS
# error "defined(IN_GC) && GC_ARCH_BITS != ARCH_BITS"
#endif


/*
 * Check basic host (hc/r0/r3) types.
 */
#if HC_ARCH_BITS == 64

AssertCompileSize(RTHCPTR, 8);
AssertCompileSize(RTHCINT, 4);
AssertCompileSize(RTHCUINT, 4);
AssertCompileSize(RTHCINTPTR, 8);
AssertCompileSize(RTHCUINTPTR, 8);
//AssertCompileSize(RTHCINTREG, 8);
AssertCompileSize(RTHCUINTREG, 8);
AssertCompileSize(RTR0PTR, 8);
//AssertCompileSize(RTR0INT, 4);
//AssertCompileSize(RTR0UINT, 4);
AssertCompileSize(RTR0INTPTR, 8);
AssertCompileSize(RTR0UINTPTR, 8);
//AssertCompileSize(RTR3PTR, 8);
//AssertCompileSize(RTR3INT, 4);
//AssertCompileSize(RTR3UINT, 4);
AssertCompileSize(RTR3INTPTR, 8);
AssertCompileSize(RTR3UINTPTR, 8);
AssertCompileSize(RTUINTPTR, 8);

# if defined(IN_RING3) || defined(IN_RING0)
//AssertCompileSize(RTCCINTREG, 8);
AssertCompileSize(RTCCUINTREG, 8);
# endif

#else

AssertCompileSize(RTHCPTR, 4);
AssertCompileSize(RTHCINT, 4);
AssertCompileSize(RTHCUINT, 4);
//AssertCompileSize(RTHCINTPTR, 4);
AssertCompileSize(RTHCUINTPTR, 4);
AssertCompileSize(RTR0PTR, 4);
//AssertCompileSize(RTR0INT, 4);
//AssertCompileSize(RTR0UINT, 4);
AssertCompileSize(RTR0INTPTR, 4);
AssertCompileSize(RTR0UINTPTR, 4);
//AssertCompileSize(RTR3PTR, 4);
//AssertCompileSize(RTR3INT, 4);
//AssertCompileSize(RTR3UINT, 4);
AssertCompileSize(RTR3INTPTR, 4);
AssertCompileSize(RTR3UINTPTR, 4);
# if GC_ARCH_BITS == 64
AssertCompileSize(RTUINTPTR, 8);
# else
AssertCompileSize(RTUINTPTR, 4);
# endif

# if defined(IN_RING3) || defined(IN_RING0)
//AssertCompileSize(RTCCINTREG, 4);
AssertCompileSize(RTCCUINTREG, 4);
# endif

#endif

AssertCompileSize(RTHCPHYS, 8);


/*
 * Check basic guest context types.
 */
#if GC_ARCH_BITS == 64

AssertCompileSize(RTGCINT, 4);
AssertCompileSize(RTGCUINT, 4);
AssertCompileSize(RTGCINTPTR, 8);
AssertCompileSize(RTGCUINTPTR, 8);
//AssertCompileSize(RTGCINTREG, 8);
AssertCompileSize(RTGCUINTREG, 8);

# ifdef IN_GC
//AssertCompileSize(RTCCINTREG, 8);
AssertCompileSize(RTCCUINTREG, 8);
# endif

#else

AssertCompileSize(RTGCINT, 4);
AssertCompileSize(RTGCUINT, 4);
AssertCompileSize(RTGCINTPTR, 4);
AssertCompileSize(RTGCUINTPTR, 4);
//AssertCompileSize(RTGCINTREG, 4);
AssertCompileSize(RTGCUINTREG, 4);

# ifdef IN_GC
//AssertCompileSize(RTCCINTREG, 4);
AssertCompileSize(RTCCUINTREG, 4);
# endif

#endif

AssertCompileSize(RTGCPHYS, 4);


/*
 * Check basic current context types.
 */
#if ARCH_BITS == 64

AssertCompileSize(void *, 8);

#else

AssertCompileSize(void *, 4);

#endif
