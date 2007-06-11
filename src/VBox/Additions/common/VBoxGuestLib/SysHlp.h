/** @file
 *
 * VBoxGuestLib - A support library for VirtualBox guest additions:
 * System dependent helpers internal header
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

#ifndef __SYSHLP__H
#define __SYSHLP__H

#ifdef __WIN__
# if (_MSC_VER >= 1400) && !defined(VBOX_WITH_PATCHED_DDK)
#  include <iprt/asm.h>
#  define _InterlockedExchange           _InterlockedExchange_StupidDDKVsCompilerCrap
#  define _InterlockedExchangeAdd        _InterlockedExchangeAdd_StupidDDKVsCompilerCrap
#  define _InterlockedCompareExchange    _InterlockedCompareExchange_StupidDDKVsCompilerCrap
#  define _InterlockedAddLargeStatistic  _InterlockedAddLargeStatistic_StupidDDKVsCompilerCrap
__BEGIN_DECLS
#  include <ntddk.h>
__END_DECLS
#  undef  _InterlockedExchange
#  undef  _InterlockedExchangeAdd
#  undef  _InterlockedCompareExchange
#  undef  _InterlockedAddLargeStatistic
# else
__BEGIN_DECLS
#  include <ntddk.h>
__END_DECLS
# endif
/* XP DDK #defines ExFreePool to ExFreePoolWithTag. The latter does not exist on NT4, so... */
#undef ExFreePool
#endif

typedef struct _VBGLDRIVER
{
#ifdef __WIN__
    PDEVICE_OBJECT pDeviceObject;
    PFILE_OBJECT pFileObject;
#else /* !__WIN__ */
    void *opaque;
#endif /* !__WIN__ */
} VBGLDRIVER;

int vbglLockLinear (void **ppvCtx, void *pv, uint32_t u32Size);
void vbglUnlockLinear (void *pvCtx, void *pv, uint32_t u32Size);


#ifndef VBGL_VBOXGUEST

/**
 * Open VBoxGuest driver.
 *
 * @param pDriver      Pointer to the driver structure.
 *
 * @return VBox error code
 */
int vbglDriverOpen (VBGLDRIVER *pDriver);

/**
 * Call VBoxGuest driver.
 *
 * @param pDriver      Pointer to the driver structure.
 * @param u32Function  Function code.
 * @param pvData       Pointer to supplied in/out data buffer.
 * @param cbData       Size of data buffer.
 *
 * @return VBox error code
 */
int vbglDriverIOCtl (VBGLDRIVER *pDriver, uint32_t u32Function, void *pvData, uint32_t cbData);

/**
 * Close VBoxGuest driver.
 *
 * @param pDriver      Pointer to the driver structure.
 *
 * @return VBox error code
 */
void vbglDriverClose (VBGLDRIVER *pDriver);

#endif

#endif /* __SYSHLP__H */
