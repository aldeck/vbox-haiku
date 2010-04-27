/** @file
 *
 * VBoxGuest -- VirtualBox Win32 guest support driver
 *
 * Copyright (C) 2006-2007 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __VBOXGUESTINTERNAL_h__
#define __VBOXGUESTINTERNAL_h__


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/

#include <iprt/cdefs.h>

/** @todo Use the-nt-kernel.h and keep the messy stuff all in one place? */
#ifdef IN_RING0
# if (_MSC_VER >= 1400) && !defined(VBOX_WITH_PATCHED_DDK)
#  include <iprt/asm.h>
#  define _InterlockedExchange           _InterlockedExchange_StupidDDKVsCompilerCrap
#  define _InterlockedExchangeAdd        _InterlockedExchangeAdd_StupidDDKVsCompilerCrap
#  define _InterlockedCompareExchange    _InterlockedCompareExchange_StupidDDKVsCompilerCrap
#  define _InterlockedAddLargeStatistic  _InterlockedAddLargeStatistic_StupidDDKVsCompilerCrap
#  pragma warning(disable : 4163)
RT_C_DECLS_BEGIN
#  include <ntddk.h>
RT_C_DECLS_END
#  pragma warning(default : 4163)
#  undef  _InterlockedExchange
#  undef  _InterlockedExchangeAdd
#  undef  _InterlockedCompareExchange
#  undef  _InterlockedAddLargeStatistic
# else
RT_C_DECLS_BEGIN
#  include <ntddk.h>
RT_C_DECLS_END
# endif
#endif

#include <iprt/spinlock.h>
#include <iprt/memobj.h>

#include <VBox/VMMDev.h>
#include <VBox/VBoxGuest.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/

/* debug printf */
# define OSDBGPRINT(a) DbgPrint a

/* dprintf */
#if (defined(DEBUG) && !defined(NO_LOGGING)) || defined(LOG_ENABLED)
# ifdef LOG_TO_BACKDOOR
#  include <VBox/log.h>
#  define dprintf(a) RTLogBackdoorPrintf a
# else
#  define dprintf(a) OSDBGPRINT(a)
# endif
#else
# define dprintf(a) do {} while (0)
#endif

/* dprintf2 - extended logging. */
#if 0
# define dprintf2 dprintf
#else
# define dprintf2(a) do { } while (0)
#endif

// the maximum scatter/gather transfer length
#define MAXIMUM_TRANSFER_LENGTH     64*1024

#define PCI_MAX_BUSES 256

/*
 * Error codes.
 */


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/

// possible device states for our state machine
enum DEVSTATE
{
    STOPPED,
    WORKING,
    PENDINGSTOP,
    PENDINGREMOVE,
    SURPRISEREMOVED,
    REMOVED
};

// undocumented API to set the system time
extern "C"
{
NTSYSAPI NTSTATUS NTAPI ZwSetSystemTime(IN PLARGE_INTEGER NewTime, OUT PLARGE_INTEGER OldTime OPTIONAL);
}

#ifdef IN_RING0
typedef struct _BASE_ADDRESS {

    PHYSICAL_ADDRESS RangeStart;    // Original device physical address
    ULONG   RangeLength;            // Length of I/O or memory range
    BOOLEAN RangeInMemory;          // Flag: unmapped range is I/O or memory range

    PVOID   MappedRangeStart;       // Mapped I/O or memory range
    BOOLEAN MappedRangeInMemory;    // Flag: mapped range is I/O or memory range

    BOOLEAN ResourceMapped;         // Flag: resource is mapped (i.e. MmMapIoSpace called)

} BASE_ADDRESS, *PBASE_ADDRESS;


/**
 * Device extension.
 */
typedef struct VBOXGUESTDEVEXT
{
//// legacy stuff
    // bus number where the device is located
    ULONG busNumber;
    // slot number where the device is located
    ULONG slotNumber;
    // device interrupt level
    ULONG interruptLevel;
    // device interrupt vector
    ULONG interruptVector;
    // affinity mask
    KAFFINITY interruptAffinity;
    // LevelSensitive or Latched
    KINTERRUPT_MODE interruptMode;

    // PCI base address information
    ULONG addressCount;
    BASE_ADDRESS baseAddress[PCI_TYPE0_ADDRESSES];

    // adapter object pointer, returned by HalGetAdapter
    PADAPTER_OBJECT adapterObject;

    // interrupt object pointer
    PKINTERRUPT interruptObject;
/////

    // the driver name
    UCHAR szDriverName[32];
    // our functional driver object
    PDEVICE_OBJECT deviceObject;
    // the top of the stack
    PDEVICE_OBJECT nextLowerDriver;
    // currently active Irp
    IRP *currentIrp;
    PKTHREAD workerThread;
    PKTHREAD idleThread;
    KEVENT workerThreadRequest;
    BOOLEAN stopThread;
    // device state
    DEVSTATE devState;
    // start port address
    ULONG startPortAddress;
    // start of hypervisor mapping
    PVOID hypervisorMapping;
    // size in bytes of the hypervisor mapping
    ULONG hypervisorMappingSize;

    /* Patch memory object. */
    RTR0MEMOBJ PatchMemObj;

    /* Physical address and length of VMMDev memory */
    PHYSICAL_ADDRESS memoryAddress;
    ULONG memoryLength;

    /* Virtual address of VMMDev memory */
    VMMDevMemory *pVMMDevMemory;

    /* Pending event flags signalled by host */
    ULONG u32Events;

    /* Notification semaphore */
    KEVENT keventNotification;

    LARGE_INTEGER HGCMWaitTimeout;

    /* Old Windows session id */
    ULONG   ulOldActiveConsoleId;

    /* VRDP hook state */
    BOOLEAN fVRDPEnabled;

    /* Preallocated VMMDevEvents for IRQ handler */
    VMMDevEvents *irqAckEvents;

#ifdef VBOX_WITH_HGCM
    /** Spinlock various items in the VBOXGUESTSESSION. */
    RTSPINLOCK SessionSpinlock;
#endif

    struct
    {
        uint32_t     cBalloonChunks;
        uint32_t     cMaxBalloonChunks;
        PMDL        *paMdlMemBalloon;
    } MemBalloon;

    /* Preallocated generic request for shutdown. */
    VMMDevPowerStateRequest *powerStateRequest;

    /** Is the bugcheck callback registered? */
    BOOLEAN bBugcheckCallbackRegistered;
    /** The bugcheck registration record. */
    KBUGCHECK_CALLBACK_RECORD bugcheckRecord;

} VBOXGUESTDEVEXT, *PVBOXGUESTDEVEXT;

// Windows version identifier
typedef enum
{
    WINNT4   = 1,
    WIN2K    = 2,
    WINXP    = 3,
    WIN2K3   = 4,
    WINVISTA = 5,
    WIN7     = 6
} winVersion_t;
extern winVersion_t winVersion;

#ifdef VBOX_WITH_HGCM
/**
 * The VBoxGuest per session data.
 *
 * @remark  Just to store hgcm ID's, perhaps could combine with one from common/VBoxGuest/vboxguestinternal.h?
 */
typedef struct VBOXGUESTSESSION
{
    /** Array containing HGCM client IDs associated with this session.
     * This will be automatically disconnected when the session is closed.
     * Note that array size also affects/is maximum number of supported opengl threads per guest process.
     */
    uint32_t volatile           aHGCMClientIds[8];
} VBOXGUESTSESSION, *PVBOXGUESTSESSION;
#endif

extern "C"
{
VOID     VBoxGuestDpcHandler(PKDPC dpc, PDEVICE_OBJECT pDevObj,
                             PIRP irp, PVOID context);
BOOLEAN  VBoxGuestIsrHandler(PKINTERRUPT interrupt, PVOID serviceContext);
NTSTATUS createThreads(PVBOXGUESTDEVEXT pDevExt);
VOID     unreserveHypervisorMemory(PVBOXGUESTDEVEXT pDevExt);
void     VBoxInitMemBalloon(PVBOXGUESTDEVEXT pDevExt);
void     VBoxCleanupMemBalloon(PVBOXGUESTDEVEXT pDevExt);
}

#endif

#endif // __H_VBOXGUESTINTERNAL
