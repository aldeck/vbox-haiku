/** @file
 * InnoTek Portable Runtime - Runtime Init/Term.
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

#ifndef __iprt_initterm_h__
#define __iprt_initterm_h__

#include <iprt/cdefs.h>
#include <iprt/types.h>


__BEGIN_DECLS

/** @defgroup grp_rt    InnoTek Portable Runtime APIs
 * @{
 */

/** @defgroup grp_rt_initterm  Init / Term
 * @{
 */

#ifdef IN_RING3
/**
 * Initalizes the runtime library.
 *
 * @returns iprt status code.
 *
 * @param   fInitSUPLib     Set if SUPInit() shall be called during init (default).
 *                          Clear if not to call it.
 * @param   cbReserve       The number of bytes of contiguous memory that should be reserved by
 *                          the runtime / support library.
 *                          Set this to 0 if no reservation is required. (default)
 *                          Set this to ~(size_t)0 if the maximum amount supported by the VM is to be
 *                          attempted reserved, or the maximum available.
 *                          This argument only applies if fInitSUPLib is true and we're in ring-3 HC.
 */
RTR3DECL(int) RTR3Init(
#ifdef __cplusplus
    bool fInitSUPLib = true,
    size_t cbReserve = 0
#else
    bool fInitSUPLib,
    size_t cbReserve
#endif
    );

/**
 * Terminates the runtime library.
 */
RTR3DECL(void) RTR3Term(void);
#endif


#ifdef IN_RING0
/**
 * Initalizes the ring-0 driver runtime library.
 *
 * @returns iprt status code.
 * @param   fReserved       Flags reserved for the future.
 */
RTR0DECL(int) RTR0Init(unsigned fReserved);

/**
 * Terminates the ring-0 driver runtime library.
 */
RTR0DECL(void) RTR0Term(void);
#endif

/** @} */

/** @} */

__END_DECLS


#endif

