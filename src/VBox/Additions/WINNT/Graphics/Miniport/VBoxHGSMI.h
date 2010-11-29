/** @file
 *
 * VirtualBox Video miniport driver for NT/2k/XP
 * HGSMI related functions.
 */

/*
 * Copyright (C) 2006-2009 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___VBoxHGSMI_h_
#define ___VBoxHGSMI_h_
typedef void* HVBOXVIDEOHGSMI;

/* Complete host commands addressed to the display */
typedef DECLCALLBACK(void) FNVBOXVIDEOHGSMICOMPLETION(HVBOXVIDEOHGSMI hHGSMI, struct VBVAHOSTCMD * pCmd);
typedef FNVBOXVIDEOHGSMICOMPLETION *PFNVBOXVIDEOHGSMICOMPLETION;

/* request the host commands addressed to the display */
typedef DECLCALLBACK(int) FNVBOXVIDEOHGSMICOMMANDS(HVBOXVIDEOHGSMI hHGSMI, uint8_t u8Channel, uint32_t iDevice, struct VBVAHOSTCMD ** ppCmd);
typedef FNVBOXVIDEOHGSMICOMMANDS *PFNVBOXVIDEOHGSMICOMMANDS;

/* post guest command (offset) to the host */
typedef DECLCALLBACK(void) FNVBOXVIDEOHGSMIPOSTCOMMAND(HVBOXVIDEOHGSMI hHGSMI, HGSMIOFFSET offCmd);
typedef FNVBOXVIDEOHGSMIPOSTCOMMAND *PFNVBOXVIDEOHGSMIPOSTCOMMAND;

/* Video Port API dynamically picked up at runtime for binary backwards compatibility with older NT versions */
#if 0
typedef VP_STATUS (*PFNWAITFORSINGLEOBJECT) (IN PVOID  HwDeviceExtension, IN PVOID  Object, IN PLARGE_INTEGER  Timeout  OPTIONAL);

typedef LONG (*PFNSETEVENT) (IN PVOID  HwDeviceExtension, IN PEVENT  pEvent);
typedef VOID (*PFNCLEAREVENT) (IN PVOID  HwDeviceExtension, IN PEVENT  pEvent);
typedef VP_STATUS (*PFNCREATEEVENT) (IN PVOID  HwDeviceExtension, IN ULONG  EventFlag, IN PVOID  Unused, OUT PEVENT  *ppEvent);
typedef VP_STATUS (*PFNDELETEEVENT) (IN PVOID  HwDeviceExtension, IN PEVENT  pEvent);
#endif

typedef long VBOXVP_STATUS;
typedef struct _VIDEO_PORT_EVENT *VBOXPEVENT;
typedef struct _VIDEO_PORT_SPIN_LOCK *VBOXPSPIN_LOCK;
typedef union _LARGE_INTEGER *VBOXPLARGE_INTEGER;

typedef enum VBOXVP_POOL_TYPE
{
    VBoxVpNonPagedPool,
    VBoxVpPagedPool,
    VBoxVpNonPagedPoolCacheAligned = 4,
    VBoxVpPagedPoolCacheAligned
} VBOXVP_POOL_TYPE;

#define VBOXNOTIFICATION_EVENT 0x00000001UL

#define VBOXNO_ERROR           0x00000000UL

typedef VBOXVP_STATUS (*PFNWAITFORSINGLEOBJECT) (void*  HwDeviceExtension, void*  Object, VBOXPLARGE_INTEGER  Timeout);

typedef long (*PFNSETEVENT) (void* HwDeviceExtension, VBOXPEVENT  pEvent);
typedef void (*PFNCLEAREVENT) (void*  HwDeviceExtension, VBOXPEVENT  pEvent);
typedef VBOXVP_STATUS (*PFNCREATEEVENT) (void*  HwDeviceExtension, unsigned long  EventFlag, void*  Unused, VBOXPEVENT  *ppEvent);
typedef VBOXVP_STATUS (*PFNDELETEEVENT) (void*  HwDeviceExtension, VBOXPEVENT  pEvent);

typedef void* (*PFNALLOCATEPOOL) (void*  HwDeviceExtension, VBOXVP_POOL_TYPE PoolType, size_t NumberOfBytes, unsigned long Tag);
typedef void (*PFNFREEPOOL) (void*  HwDeviceExtension, void*  Ptr);

typedef unsigned char (*PFNQUEUEDPC) (void* HwDeviceExtension, void (*CallbackRoutine)(void* HwDeviceExtension, void *Context), void *Context);

/* pfn*Event and pfnWaitForSingleObject functions are available */
#define VBOXVIDEOPORTPROCS_EVENT    0x00000002
/* pfn*Pool functions are available */
#define VBOXVIDEOPORTPROCS_POOL     0x00000004
/* pfnQueueDpc function is available */
#define VBOXVIDEOPORTPROCS_DPC      0x00000008

typedef struct VBOXVIDEOPORTPROCS
{
    /* ored VBOXVIDEOPORTPROCS_xxx constants describing the supported functionality */
    uint32_t fSupportedTypes;

    PFNWAITFORSINGLEOBJECT pfnWaitForSingleObject;

    PFNSETEVENT pfnSetEvent;
    PFNCLEAREVENT pfnClearEvent;
    PFNCREATEEVENT pfnCreateEvent;
    PFNDELETEEVENT pfnDeleteEvent;

    PFNALLOCATEPOOL pfnAllocatePool;
    PFNFREEPOOL pfnFreePool;

    PFNQUEUEDPC pfnQueueDpc;
} VBOXVIDEOPORTPROCS;

#endif /* #ifndef ___VBoxHGSMI_h_ */
