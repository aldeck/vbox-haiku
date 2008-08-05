/* $Id$ */
/** @file
 * DevATA, DevAHCI - Shared ATA/ATAPI controller types.
 */

/*
 * Copyright (C) 2006-2008 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef ___Storage_ATAController_h
#define ___Storage_ATAController_h

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <VBox/pdmdev.h>
#ifdef IN_RING3
# include <iprt/semaphore.h>
# include <iprt/thread.h>
#endif /* IN_RING3 */
#include <iprt/critsect.h>
#include <VBox/stam.h>

#include "PIIX3ATABmDma.h"
#include "ide.h"


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/**
 * Maximum number of sectors to transfer in a READ/WRITE MULTIPLE request.
 * Set to 1 to disable multi-sector read support. According to the ATA
 * specification this must be a power of 2 and it must fit in an 8 bit
 * value. Thus the only valid values are 1, 2, 4, 8, 16, 32, 64 and 128.
 */
#define ATA_MAX_MULT_SECTORS 128

/**
 * Fastest PIO mode supported by the drive.
 */
#define ATA_PIO_MODE_MAX 4
/**
 * Fastest MDMA mode supported by the drive.
 */
#define ATA_MDMA_MODE_MAX 2
/**
 * Fastest UDMA mode supported by the drive.
 */
#define ATA_UDMA_MODE_MAX 6

/** The maximum number of release log entries per device. */
#define MAX_LOG_REL_ERRORS  1024


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
typedef struct ATADevState {
    /** Flag indicating whether the current command uses LBA48 mode. */
    bool fLBA48;
    /** Flag indicating whether this drive implements the ATAPI command set. */
    bool fATAPI;
    /** Set if this interface has asserted the IRQ. */
    bool fIrqPending;
    /** Currently configured number of sectors in a multi-sector transfer. */
    uint8_t cMultSectors;
    /** PCHS disk geometry. */
    PDMMEDIAGEOMETRY PCHSGeometry;
    /** Total number of sectors on this disk. */
    uint64_t cTotalSectors;
    /** Number of sectors to transfer per IRQ. */
    uint32_t cSectorsPerIRQ;

    /** ATA/ATAPI register 1: feature (write-only). */
    uint8_t uATARegFeature;
    /** ATA/ATAPI register 1: feature, high order byte. */
    uint8_t uATARegFeatureHOB;
    /** ATA/ATAPI register 1: error (read-only). */
    uint8_t uATARegError;
    /** ATA/ATAPI register 2: sector count (read/write). */
    uint8_t uATARegNSector;
    /** ATA/ATAPI register 2: sector count, high order byte. */
    uint8_t uATARegNSectorHOB;
    /** ATA/ATAPI register 3: sector (read/write). */
    uint8_t uATARegSector;
    /** ATA/ATAPI register 3: sector, high order byte. */
    uint8_t uATARegSectorHOB;
    /** ATA/ATAPI register 4: cylinder low (read/write). */
    uint8_t uATARegLCyl;
    /** ATA/ATAPI register 4: cylinder low, high order byte. */
    uint8_t uATARegLCylHOB;
    /** ATA/ATAPI register 5: cylinder high (read/write). */
    uint8_t uATARegHCyl;
    /** ATA/ATAPI register 5: cylinder high, high order byte. */
    uint8_t uATARegHCylHOB;
    /** ATA/ATAPI register 6: select drive/head (read/write). */
    uint8_t uATARegSelect;
    /** ATA/ATAPI register 7: status (read-only). */
    uint8_t uATARegStatus;
    /** ATA/ATAPI register 7: command (write-only). */
    uint8_t uATARegCommand;
    /** ATA/ATAPI drive control register (write-only). */
    uint8_t uATARegDevCtl;

    /** Currently active transfer mode (MDMA/UDMA) and speed. */
    uint8_t uATATransferMode;
    /** Current transfer direction. */
    uint8_t uTxDir;
    /** Index of callback for begin transfer. */
    uint8_t iBeginTransfer;
    /** Index of callback for source/sink of data. */
    uint8_t iSourceSink;
    /** Flag indicating whether the current command transfers data in DMA mode. */
    bool fDMA;
    /** Set to indicate that ATAPI transfer semantics must be used. */
    bool fATAPITransfer;

    /** Total ATA/ATAPI transfer size, shared PIO/DMA. */
    uint32_t cbTotalTransfer;
    /** Elementary ATA/ATAPI transfer size, shared PIO/DMA. */
    uint32_t cbElementaryTransfer;
    /** Current read/write buffer position, shared PIO/DMA. */
    uint32_t iIOBufferCur;
    /** First element beyond end of valid buffer content, shared PIO/DMA. */
    uint32_t iIOBufferEnd;

    /** ATA/ATAPI current PIO read/write transfer position. Not shared with DMA for safety reasons. */
    uint32_t iIOBufferPIODataStart;
    /** ATA/ATAPI current PIO read/write transfer end. Not shared with DMA for safety reasons. */
    uint32_t iIOBufferPIODataEnd;

    /** ATAPI current LBA position. */
    uint32_t iATAPILBA;
    /** ATAPI current sector size. */
    uint32_t cbATAPISector;
    /** ATAPI current command. */
    uint8_t aATAPICmd[ATAPI_PACKET_SIZE];
    /** ATAPI sense key. */
    uint8_t uATAPISenseKey;
    /** ATAPI additional sense code. */
    uint8_t uATAPIASC;
    /** HACK: Countdown till we report a newly unmounted drive as mounted. */
    uint8_t cNotifiedMediaChange;

    /** The status LED state for this drive. */
    PDMLED Led;

    /** Size of I/O buffer. */
    uint32_t cbIOBuffer;
    /** Pointer to the I/O buffer. */
    R3PTRTYPE(uint8_t *) pbIOBufferR3;
    /** Pointer to the I/O buffer. */
    R0PTRTYPE(uint8_t *) pbIOBufferR0;
    /** Pointer to the I/O buffer. */
    RCPTRTYPE(uint8_t *) pbIOBufferRC;
#if 1 /*HC_ARCH_BITS == 64*/
    RTRCPTR Aligmnent0; /**< Align the statistics at an 8-byte boundrary. */
#endif

    /*
     * No data that is part of the saved state after this point!!!!!
     */

    /* Release statistics: number of ATA DMA commands. */
    STAMCOUNTER StatATADMA;
    /* Release statistics: number of ATA PIO commands. */
    STAMCOUNTER StatATAPIO;
    /* Release statistics: number of ATAPI PIO commands. */
    STAMCOUNTER StatATAPIDMA;
    /* Release statistics: number of ATAPI PIO commands. */
    STAMCOUNTER StatATAPIPIO;
#ifdef VBOX_INSTRUMENT_DMA_WRITES
    /* Release statistics: number of DMA sector writes and the time spent. */
    STAMPROFILEADV StatInstrVDWrites;
#endif

    /** Statistics: number of read operations and the time spent reading. */
    STAMPROFILEADV  StatReads;
    /** Statistics: number of bytes read. */
    STAMCOUNTER     StatBytesRead;
    /** Statistics: number of write operations and the time spent writing. */
    STAMPROFILEADV  StatWrites;
    /** Statistics: number of bytes written. */
    STAMCOUNTER     StatBytesWritten;
    /** Statistics: number of flush operations and the time spend flushing. */
    STAMPROFILE     StatFlushes;

    /** Enable passing through commands directly to the ATAPI drive. */
    bool            fATAPIPassthrough;
    /** Number of errors we've reported to the release log.
     * This is to prevent flooding caused by something going horribly wrong.
     * this value against MAX_LOG_REL_ERRORS in places likely to cause floods
     * like the ones we currently seeing on the linux smoke tests (2006-11-10). */
    uint32_t        cErrors;
    /** Timestamp of last started command. 0 if no command pending. */
    uint64_t        u64CmdTS;

    /** Pointer to the attached driver's base interface. */
    R3PTRTYPE(PPDMIBASE)            pDrvBase;
    /** Pointer to the attached driver's block interface. */
    R3PTRTYPE(PPDMIBLOCK)           pDrvBlock;
    /** Pointer to the attached driver's block bios interface. */
    R3PTRTYPE(PPDMIBLOCKBIOS)       pDrvBlockBios;
    /** Pointer to the attached driver's mount interface.
     * This is NULL if the driver isn't a removable unit. */
    R3PTRTYPE(PPDMIMOUNT)           pDrvMount;
    /** The base interface. */
    PDMIBASE                        IBase;
    /** The block port interface. */
    PDMIBLOCKPORT                   IPort;
    /** The mount notify interface. */
    PDMIMOUNTNOTIFY                 IMountNotify;
    /** The LUN #. */
    RTUINT                          iLUN;
#if HC_ARCH_BITS == 64
    RTUINT                          Alignment2; /**< Align pDevInsR3 correctly. */
#endif
    /** Pointer to device instance. */
    PPDMDEVINSR3                        pDevInsR3;
    /** Pointer to controller instance. */
    R3PTRTYPE(struct ATACONTROLLER *)   pControllerR3;
    /** Pointer to device instance. */
    PPDMDEVINSR0                        pDevInsR0;
    /** Pointer to controller instance. */
    R0PTRTYPE(struct ATACONTROLLER *)   pControllerR0;
    /** Pointer to device instance. */
    PPDMDEVINSRC                        pDevInsRC;
    /** Pointer to controller instance. */
    RCPTRTYPE(struct ATACONTROLLER *)   pControllerRC;
} ATADevState;


typedef struct ATATransferRequest
{
    uint8_t iIf;
    uint8_t iBeginTransfer;
    uint8_t iSourceSink;
    uint32_t cbTotalTransfer;
    uint8_t uTxDir;
} ATATransferRequest;


typedef struct ATAAbortRequest
{
    uint8_t iIf;
    bool fResetDrive;
} ATAAbortRequest;


typedef enum
{
    /** Begin a new transfer. */
    ATA_AIO_NEW = 0,
    /** Continue a DMA transfer. */
    ATA_AIO_DMA,
    /** Continue a PIO transfer. */
    ATA_AIO_PIO,
    /** Reset the drives on current controller, stop all transfer activity. */
    ATA_AIO_RESET_ASSERTED,
    /** Reset the drives on current controller, resume operation. */
    ATA_AIO_RESET_CLEARED,
    /** Abort the current transfer of a particular drive. */
    ATA_AIO_ABORT
} ATAAIO;


typedef struct ATARequest
{
    ATAAIO ReqType;
    union
    {
        ATATransferRequest t;
        ATAAbortRequest a;
    } u;
} ATARequest;


typedef struct ATACONTROLLER
{
    /** The base of the first I/O Port range. */
    RTIOPORT    IOPortBase1;
    /** The base of the second I/O Port range. (0 if none) */
    RTIOPORT    IOPortBase2;
    /** The assigned IRQ. */
    RTUINT      irq;
    /** Access critical section */
    PDMCRITSECT lock;

    /** Selected drive. */
    uint8_t     iSelectedIf;
    /** The interface on which to handle async I/O. */
    uint8_t     iAIOIf;
    /** The state of the async I/O thread. */
    uint8_t     uAsyncIOState;
    /** Flag indicating whether the next transfer is part of the current command. */
    bool        fChainedTransfer;
    /** Set when the reset processing is currently active on this controller. */
    bool        fReset;
    /** Flag whether the current transfer needs to be redone. */
    bool        fRedo;
    /** Flag whether the redo suspend has been finished. */
    bool        fRedoIdle;
    /** Flag whether the DMA operation to be redone is the final transfer. */
    bool        fRedoDMALastDesc;
    /** The BusMaster DMA state. */
    BMDMAState  BmDma;
    /** Pointer to first DMA descriptor. */
    RTGCPHYS32  pFirstDMADesc;
    /** Pointer to last DMA descriptor. */
    RTGCPHYS32  pLastDMADesc;
    /** Pointer to current DMA buffer (for redo operations). */
    RTGCPHYS32  pRedoDMABuffer;
    /** Size of current DMA buffer (for redo operations). */
    uint32_t    cbRedoDMABuffer;

    /** The ATA/ATAPI interfaces of this controller. */
    ATADevState aIfs[2];

    /** Pointer to device instance. */
    PPDMDEVINSR3        pDevInsR3;
    /** Pointer to device instance. */
    PPDMDEVINSR0        pDevInsR0;
    /** Pointer to device instance. */
    PPDMDEVINSRC        pDevInsRC;

    /** Set when the destroying the device instance and the thread must exit. */
    uint32_t volatile   fShutdown;
    /** The async I/O thread handle. NIL_RTTHREAD if no thread. */
    RTTHREAD            AsyncIOThread;
    /** The event semaphore the thread is waiting on for requests. */
    RTSEMEVENT          AsyncIOSem;
    /** The request queue for the AIO thread. One element is always unused. */
    ATARequest          aAsyncIORequests[4];
    /** The position at which to insert a new request for the AIO thread. */
    uint8_t             AsyncIOReqHead;
    /** The position at which to get a new request for the AIO thread. */
    uint8_t             AsyncIOReqTail;
    uint8_t             Alignment3[2]; /**< Explicit padding of the 2 byte gap. */
    /** Magic delay before triggering interrupts in DMA mode. */
    uint32_t            DelayIRQMillies;
    /** The mutex protecting the request queue. */
    RTSEMMUTEX          AsyncIORequestMutex;
    /** The event semaphore the thread is waiting on during suspended I/O. */
    RTSEMEVENT          SuspendIOSem;
#if 0 /*HC_ARCH_BITS == 32*/
    uint32_t            Alignment0;
#endif

    /* Statistics */
    STAMCOUNTER     StatAsyncOps;
    uint64_t        StatAsyncMinWait;
    uint64_t        StatAsyncMaxWait;
    STAMCOUNTER     StatAsyncTimeUS;
    STAMPROFILEADV  StatAsyncTime;
    STAMPROFILE     StatLockWait;
} ATACONTROLLER, *PATACONTROLLER;

#ifndef VBOX_DEVICE_STRUCT_TESTCASE

#define ATADEVSTATE_2_CONTROLLER(pIf)          ( (pIf)->CTX_SUFF(pController) )
#define ATADEVSTATE_2_DEVINS(pIf)              ( (pIf)->CTX_SUFF(pDevIns) )
#define CONTROLLER_2_DEVINS(pController)       ( (pController)->CTX_SUFF(pDevIns) )
#define PDMIBASE_2_ATASTATE(pInterface)        ( (ATADevState *)((uintptr_t)(pInterface) - RT_OFFSETOF(ATADevState, IBase)) )


/*******************************************************************************
 *  Internal Functions                                                         *
 ******************************************************************************/
__BEGIN_DECLS
int ataControllerIOPortWrite1(PATACONTROLLER pCtl, RTIOPORT Port, uint32_t u32, unsigned cb);
int ataControllerIOPortRead1(PATACONTROLLER pCtl, RTIOPORT Port, uint32_t *u32, unsigned cb);
int ataControllerIOPortWriteStr1(PATACONTROLLER pCtl, RTIOPORT Port, RTGCPTR *pGCPtrSrc, PRTGCUINTREG pcTransfer, unsigned cb);
int ataControllerIOPortReadStr1(PATACONTROLLER pCtl, RTIOPORT Port, RTGCPTR *pGCPtrDst, PRTGCUINTREG pcTransfer, unsigned cb);
int ataControllerIOPortWrite2(PATACONTROLLER pCtl, RTIOPORT Port, uint32_t u32, unsigned cb);
int ataControllerIOPortRead2(PATACONTROLLER pCtl, RTIOPORT Port, uint32_t *u32, unsigned cb);
int ataControllerBMDMAIOPortRead(PATACONTROLLER pCtl, RTIOPORT Port, uint32_t *pu32, unsigned cb);
int ataControllerBMDMAIOPortWrite(PATACONTROLLER pCtl, RTIOPORT Port, uint32_t u32, unsigned cb);
__END_DECLS

#ifdef IN_RING3
/**
 * Initialize a controller state.
 *
 * @returns VBox status code.
 * @param   pDevIns Pointer to the device instance which creates a controller.
 * @param   pCtl    Pointer to the unitialized ATA controller structure.
 * @param   pDrvBaseMaster Pointer to the base driver interface which acts as the master.
 * @param   pDrvBaseSlave  Pointer to the base driver interface which acts as the slave.
 * @param   pcbSSMState    Where to store the size of the device state for loading/saving.
 * @param   szName         Name of the controller (Used to initialize the critical section).
 */
int ataControllerInit(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PPDMIBASE pDrvBaseMaster, PPDMIBASE pDrvBaseSlave,
                      uint32_t *pcbSSMState, const char *szName);

/**
 * Free all allocated resources for one controller instance.
 *
 * @returns VBox status code.
 * @param   pCtl The controller instance.
 */
int ataControllerDestroy(PATACONTROLLER pCtl);

/**
 * Power off a controller.
 *
 * @returns nothing.
 * @param   pCtl the controller instance.
 */
void ataControllerPowerOff(PATACONTROLLER pCtl);

/**
 * Reset a controller instance to an initial state.
 *
 * @returns VBox status code.
 * @param   pCtl Pointer to the controller.
 */
void ataControllerReset(PATACONTROLLER pCtl);

/**
 * Suspend operation of an controller.
 *
 * @returns nothing
 * @param   pCtl The controller instance.
 */
void ataControllerSuspend(PATACONTROLLER pCtl);

/**
 * Resume operation of an controller.
 *
 * @returns nothing
 * @param   pCtl The controller instance.
 */

void ataControllerResume(PATACONTROLLER pCtl);

/**
 * Relocate neccessary pointers.
 *
 * @returns nothing.
 * @param   pCtl     The controller instance.
 * @param   offDelta The relocation delta relative to the old location.
 */
void ataControllerRelocate(PATACONTROLLER pCtl, RTGCINTPTR offDelta);

/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pCtl    The controller instance.
 * @param   pSSM    SSM operation handle.
 */
int ataControllerSaveExec(PATACONTROLLER pCtl, PSSMHANDLE pSSM);

/**
 * Prepare state save operation.
 *
 * @returns VBox status code.
 * @param   pCtl    The controller instance.
 * @param   pSSM    SSM operation handle.
 */
int ataControllerSavePrep(PATACONTROLLER pCtl, PSSMHANDLE pSSM);

/**
 * Excute state load operation.
 *
 * @returns VBox status code.
 * @param   pCtl    The controller instance.
 * @param   pSSM    SSM operation handle.
 */
int ataControllerLoadExec(PATACONTROLLER pCtl, PSSMHANDLE pSSM);

/**
 * Prepare state load operation.
 *
 * @returns VBox status code.
 * @param   pCtl    The controller instance.
 * @param   pSSM    SSM operation handle.
 */
int ataControllerLoadPrep(PATACONTROLLER pCtl, PSSMHANDLE pSSM);

#endif /* IN_RING3 */

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */
#endif /* !___Storage_ATAController_h */

