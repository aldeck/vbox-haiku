/** @file
 * VBoxGuest - VirtualBox Guest Additions Interface, 16-bit (OS/2) header.
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
 *
 * innotek GmbH confidential
 * All rights reserved
 */

#ifndef ___VBox_VBoxGuest16_h
#define ___VBox_VBoxGuest16_h

#define BIT(bit)                                (1UL << (bit))


#define VMMDEV_VERSION                          0x00010004UL

#define VBOXGUEST_DEVICE_NAME                   "vboxgst$"

/* aka VBOXGUESTOS2IDCCONNECT */
typedef struct VBGOS2IDC
{
    unsigned long u32Version;
    unsigned long u32Session;
    unsigned long pfnServiceEP;
    short (__cdecl __far *fpfnServiceEP)(unsigned long u32Session, unsigned short iFunction, 
                                         void __far *fpvData, unsigned short cbData, unsigned short __far *pcbDataReturned);
    unsigned long fpfnServiceAsmEP;
} VBGOS2IDC;
typedef VBGOS2IDC *PVBGOS2IDC;

#define VBOXGUEST_IOCTL_WAITEVENT               2
#define VBOXGUEST_IOCTL_VMMREQUEST              3
#define VBOXGUEST_IOCTL_OS2_IDC_DISCONNECT      48


#define VMMDEV_EVENT_MOUSE_CAPABILITIES_CHANGED BIT(0)
#define VMMDEV_EVENT_HGCM                       BIT(1)
#define VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST     BIT(2)
#define VMMDEV_EVENT_JUDGE_CREDENTIALS          BIT(3)
#define VMMDEV_EVENT_RESTORED                   BIT(4)


#define VBOXGUEST_WAITEVENT_OK                  0
#define VBOXGUEST_WAITEVENT_TIMEOUT             1
#define VBOXGUEST_WAITEVENT_INTERRUPTED         2
#define VBOXGUEST_WAITEVENT_ERROR               3

typedef struct _VBoxGuestWaitEventInfo
{
    unsigned long u32TimeoutIn;
    unsigned long u32EventMaskIn;
    unsigned long u32Result;
    unsigned long u32EventFlagsOut;
} VBoxGuestWaitEventInfo;


#define VMMDEV_REQUEST_HEADER_VERSION           (0x10001UL)
typedef struct
{
    unsigned long size;
    unsigned long version;
    unsigned long requestType;
    signed   long rc;
    unsigned long reserved1;
    unsigned long reserved2;
} VMMDevRequestHeader;

#define VMMDevReq_GetMouseStatus                1
#define VMMDevReq_SetMouseStatus                2
#define VMMDevReq_CtlGuestFilterMask            42

#define VBOXGUEST_MOUSE_GUEST_CAN_ABSOLUTE      BIT(0)
#define VBOXGUEST_MOUSE_HOST_CAN_ABSOLUTE       BIT(1)
#define VBOXGUEST_MOUSE_GUEST_NEEDS_HOST_CURSOR BIT(2)
#define VBOXGUEST_MOUSE_HOST_CANNOT_HWPOINTER   BIT(3)

typedef struct
{
    VMMDevRequestHeader header;
    unsigned long mouseFeatures;
    unsigned long pointerXPos;
    unsigned long pointerYPos;
} VMMDevReqMouseStatus;

typedef struct
{
    VMMDevRequestHeader header;
    unsigned long u32OrMask;
    unsigned long u32NotMask;
} VMMDevCtlGuestFilterMask;

#endif

