/** @file
 * VBox Remote Desktop Protocol:
 * Remote USB backend interface.
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

#ifndef __VBox_vrdpusb_h__
#define __VBox_vrdpusb_h__

#include <VBox/cdefs.h>
#include <VBox/types.h>

#ifdef IN_RING0
# error "There are no VRDP APIs available in Ring-0 Host Context!"
#endif
#ifdef IN_GC
# error "There are no VRDP APIs available Guest Context!"
#endif

#define REMOTE_USB_BACKEND_PREFIX_S   "REMOTEUSB"
#define REMOTE_USB_BACKEND_PREFIX_LEN 9

/* Forward declaration. */
struct _REMOTEUSBDEVICE;
typedef struct _REMOTEUSBDEVICE *PREMOTEUSBDEVICE;

/* Forward declaration. */
struct _REMOTEUSBQURB;
typedef struct _REMOTEUSBQURB *PREMOTEUSBQURB;

/* Forward declaration. Actually a class. */
struct _REMOTEUSBBACKEND;
typedef struct _REMOTEUSBBACKEND *PREMOTEUSBBACKEND;

/* Pointer to this structure is passed to pfnCreateProxyDevice
 * as the device specific pointer, when creating remote devices.
 */
typedef struct _REMOTEUSBCALLBACK
{
    PREMOTEUSBBACKEND pInstance;

    DECLCALLBACKMEMBER(int, pfnOpen)             (PREMOTEUSBBACKEND pInstance, const char *pszAddress, size_t cbAddress, PREMOTEUSBDEVICE *ppDevice);
    DECLCALLBACKMEMBER(void, pfnClose)           (PREMOTEUSBDEVICE pDevice);
    DECLCALLBACKMEMBER(int, pfnReset)            (PREMOTEUSBDEVICE pDevice);
    DECLCALLBACKMEMBER(int, pfnSetConfig)        (PREMOTEUSBDEVICE pDevice, uint8_t u8Cfg);
    DECLCALLBACKMEMBER(int, pfnClaimInterface)   (PREMOTEUSBDEVICE pDevice, uint8_t u8Ifnum);
    DECLCALLBACKMEMBER(int, pfnReleaseInterface) (PREMOTEUSBDEVICE pDevice, uint8_t u8Ifnum);
    DECLCALLBACKMEMBER(int, pfnInterfaceSetting) (PREMOTEUSBDEVICE pDevice, uint8_t u8Ifnum, uint8_t u8Setting);
    DECLCALLBACKMEMBER(int, pfnQueueURB)         (PREMOTEUSBDEVICE pDevice, uint8_t u8Type, uint8_t u8Ep, uint8_t u8Direction, uint32_t u32Len, void *pvData, void *pvURB, PREMOTEUSBQURB *ppRemoteURB);
    DECLCALLBACKMEMBER(int, pfnReapURB)          (PREMOTEUSBDEVICE pDevice, uint32_t u32Millies, void **ppvURB, uint32_t *pu32Len, uint32_t *pu32Err);
    DECLCALLBACKMEMBER(int, pfnClearHaltedEP)    (PREMOTEUSBDEVICE pDevice, uint8_t u8Ep);
    DECLCALLBACKMEMBER(void, pfnCancelURB)       (PREMOTEUSBDEVICE pDevice, PREMOTEUSBQURB pRemoteURB);
} REMOTEUSBCALLBACK;

#endif /* __VBox_vrdpusb_h__ */

