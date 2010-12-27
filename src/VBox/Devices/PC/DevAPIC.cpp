/* $Id$ */
/** @file
 * Advanced Programmable Interrupt Controller (APIC) Device and
 * I/O Advanced Programmable Interrupt Controller (IO-APIC) Device.
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 * apic.c revision 1.5  @@OSETODO
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_APIC
#include <VBox/vmm/pdmdev.h>

#include <VBox/log.h>
#include <VBox/vmm/stam.h>
#include <iprt/assert.h>
#include <iprt/asm.h>

#include <VBox/msi.h>

#include "Builtins2.h"

#define MSR_IA32_APICBASE               0x1b
#define MSR_IA32_APICBASE_BSP           (1<<8)
#define MSR_IA32_APICBASE_ENABLE        (1<<11)
#define MSR_IA32_APICBASE_X2ENABLE      (1<<10)
#define MSR_IA32_APICBASE_BASE          (0xfffff<<12)

#ifdef _MSC_VER
# pragma warning(disable:4244)
#endif

/** The current saved state version.*/
#define APIC_SAVED_STATE_VERSION            3
/** The saved state version used by VirtualBox v3 and earlier.
 * This does not include the config.  */
#define APIC_SAVED_STATE_VERSION_VBOX_30    2
/** Some ancient version... */
#define APIC_SAVED_STATE_VERSION_ANCIENT    1

/* version 0x14: Pentium 4, Xeon; LVT count depends on that */
#define APIC_HW_VERSION                    0x14

/** @def APIC_LOCK
 * Acquires the PDM lock. */
#define APIC_LOCK(pThis, rcBusy) \
    do { \
        int rc2 = PDMCritSectEnter((pThis)->CTX_SUFF(pCritSect), (rcBusy)); \
        if (rc2 != VINF_SUCCESS) \
            return rc2; \
    } while (0)

/** @def APIC_LOCK_VOID
 * Acquires the PDM lock and does not expect failure (i.e. ring-3 only!). */
#define APIC_LOCK_VOID(pThis, rcBusy) \
    do { \
        int rc2 = PDMCritSectEnter((pThis)->CTX_SUFF(pCritSect), (rcBusy)); \
        AssertLogRelRCReturnVoid(rc2); \
    } while (0)

/** @def APIC_UNLOCK
 * Releases the PDM lock. */
#define APIC_UNLOCK(pThis) \
    PDMCritSectLeave((pThis)->CTX_SUFF(pCritSect))

/** @def IOAPIC_LOCK
 * Acquires the PDM lock. */
#define IOAPIC_LOCK(pThis, rc) \
    do { \
        int rc2 = (pThis)->CTX_SUFF(pIoApicHlp)->pfnLock((pThis)->CTX_SUFF(pDevIns), rc); \
        if (rc2 != VINF_SUCCESS) \
            return rc2; \
    } while (0)

/** @def IOAPIC_UNLOCK
 * Releases the PDM lock. */
#define IOAPIC_UNLOCK(pThis) (pThis)->CTX_SUFF(pIoApicHlp)->pfnUnlock((pThis)->CTX_SUFF(pDevIns))


#define foreach_apic(dev, mask, code)                     \
    do {                                                  \
        uint32_t i;                                       \
        APICState *apic = (dev)->CTX_SUFF(paLapics);      \
        for (i = 0; i < (dev)->cCpus; i++)                  \
        {                                                 \
            if (mask & (1 << (apic->id)))                 \
            {                                             \
                code;                                     \
            }                                             \
            apic++;                                       \
        }                                                 \
    } while (0)

# define set_bit(pvBitmap, iBit)    ASMBitSet(pvBitmap, iBit)
# define reset_bit(pvBitmap, iBit)  ASMBitClear(pvBitmap, iBit)
# define fls_bit(value)             (ASMBitLastSetU32(value) - 1)
# define ffs_bit(value)             (ASMBitFirstSetU32(value) - 1)

/*
 *  APIC support
 *
 *  Copyright (c) 2004-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#define DEBUG_APIC
#define DEBUG_IOAPIC

/* APIC Local Vector Table */
#define APIC_LVT_TIMER   0
#define APIC_LVT_THERMAL 1
#define APIC_LVT_PERFORM 2
#define APIC_LVT_LINT0   3
#define APIC_LVT_LINT1   4
#define APIC_LVT_ERROR   5
#define APIC_LVT_NB      6

/* APIC delivery modes */
#define APIC_DM_FIXED   0
#define APIC_DM_LOWPRI  1
#define APIC_DM_SMI     2
#define APIC_DM_NMI     4
#define APIC_DM_INIT    5
#define APIC_DM_SIPI    6
#define APIC_DM_EXTINT  7

/* APIC destination mode */
#define APIC_DESTMODE_FLAT      0xf
#define APIC_DESTMODE_CLUSTER   0x0

#define APIC_TRIGGER_EDGE  0
#define APIC_TRIGGER_LEVEL 1

#define APIC_LVT_TIMER_PERIODIC         (1<<17)
#define APIC_LVT_MASKED                 (1<<16)
#define APIC_LVT_LEVEL_TRIGGER          (1<<15)
#define APIC_LVT_REMOTE_IRR             (1<<14)
#define APIC_INPUT_POLARITY             (1<<13)
#define APIC_SEND_PENDING               (1<<12)

#define IOAPIC_NUM_PINS                 0x18

#define ESR_ILLEGAL_ADDRESS (1 << 7)

#define APIC_SV_ENABLE (1 << 8)

#define APIC_MAX_PATCH_ATTEMPTS         100

typedef uint32_t PhysApicId;
typedef uint32_t LogApicId;

typedef struct APICState {
    uint32_t apicbase;
    /* Task priority register (interrupt level) */
    uint32_t   tpr;
    /* Logical APIC id - user programmable */
    LogApicId  id;
    /* Physical APIC id - not visible to user, constant */
    PhysApicId phys_id;
    /** @todo: is it logical or physical? Not really used anyway now. */
    PhysApicId arb_id;
    uint32_t spurious_vec;
    uint8_t log_dest;
    uint8_t dest_mode;
    uint32_t isr[8];  /* in service register */
    uint32_t tmr[8];  /* trigger mode register */
    uint32_t irr[8]; /* interrupt request register */
    uint32_t lvt[APIC_LVT_NB];
    uint32_t esr; /* error register */
    uint32_t icr[2];
    uint32_t divide_conf;
    int count_shift;
    uint32_t initial_count;
    uint32_t Alignment0;

    /** The time stamp of the initial_count load, i.e. when it was started. */
    uint64_t                initial_count_load_time;
    /** The time stamp of the next timer callback. */
    uint64_t                next_time;
     /** The APIC timer - R3 Ptr. */
    PTMTIMERR3              pTimerR3;
    /** The APIC timer - R0 Ptr. */
    PTMTIMERR0              pTimerR0;
    /** The APIC timer - RC Ptr. */
    PTMTIMERRC              pTimerRC;
    /** Whether the timer is armed or not */
    bool                    fTimerArmed;
    /** Alignment */
    bool                    afAlignment[3];
    /** The initial_count value used for the current frequency hint. */
    uint32_t                uHintedInitialCount;
    /** The count_shift value used for the current frequency hint. */
    uint32_t                uHintedCountShift;
    /** Timer description timer. */
    R3PTRTYPE(char *)       pszDesc;
# ifdef VBOX_WITH_STATISTICS
#  if HC_ARCH_BITS == 32
    uint32_t                u32Alignment0;
#  endif
    STAMCOUNTER             StatTimerSetInitialCount;
    STAMCOUNTER             StatTimerSetInitialCountArm;
    STAMCOUNTER             StatTimerSetInitialCountDisarm;
    STAMCOUNTER             StatTimerSetLvt;
    STAMCOUNTER             StatTimerSetLvtClearPeriodic;
    STAMCOUNTER             StatTimerSetLvtPostponed;
    STAMCOUNTER             StatTimerSetLvtArmed;
    STAMCOUNTER             StatTimerSetLvtArm;
    STAMCOUNTER             StatTimerSetLvtArmRetries;
    STAMCOUNTER             StatTimerSetLvtNoRelevantChange;
# endif

} APICState;

AssertCompileMemberAlignment(APICState, initial_count_load_time, 8);
# ifdef VBOX_WITH_STATISTICS
AssertCompileMemberAlignment(APICState, StatTimerSetInitialCount, 8);
# endif

struct IOAPICState {
    uint8_t id;
    uint8_t ioregsel;

    uint32_t irr;
    uint64_t ioredtbl[IOAPIC_NUM_PINS];

    /** The device instance - R3 Ptr. */
    PPDMDEVINSR3            pDevInsR3;
    /** The IOAPIC helpers - R3 Ptr. */
    PCPDMIOAPICHLPR3        pIoApicHlpR3;

    /** The device instance - R0 Ptr. */
    PPDMDEVINSR0            pDevInsR0;
    /** The IOAPIC helpers - R0 Ptr. */
    PCPDMIOAPICHLPR0        pIoApicHlpR0;

    /** The device instance - RC Ptr. */
    PPDMDEVINSRC            pDevInsRC;
    /** The IOAPIC helpers - RC Ptr. */
    PCPDMIOAPICHLPRC        pIoApicHlpRC;

# ifdef VBOX_WITH_STATISTICS
    STAMCOUNTER             StatMMIOReadGC;
    STAMCOUNTER             StatMMIOReadHC;
    STAMCOUNTER             StatMMIOWriteGC;
    STAMCOUNTER             StatMMIOWriteHC;
    STAMCOUNTER             StatSetIrqGC;
    STAMCOUNTER             StatSetIrqHC;
# endif
};

typedef struct IOAPICState IOAPICState;

typedef struct
{
    /** The device instance - R3 Ptr. */
    PPDMDEVINSR3            pDevInsR3;
    /** The APIC helpers - R3 Ptr. */
    PCPDMAPICHLPR3          pApicHlpR3;
    /** LAPICs states - R3 Ptr */
    R3PTRTYPE(APICState *)  paLapicsR3;
    /** The critical section - R3 Ptr. */
    R3PTRTYPE(PPDMCRITSECT) pCritSectR3;

    /** The device instance - R0 Ptr. */
    PPDMDEVINSR0            pDevInsR0;
    /** The APIC helpers - R0 Ptr. */
    PCPDMAPICHLPR0          pApicHlpR0;
    /** LAPICs states - R0 Ptr */
    R0PTRTYPE(APICState *)  paLapicsR0;
    /** The critical section - R3 Ptr. */
    R0PTRTYPE(PPDMCRITSECT) pCritSectR0;

    /** The device instance - RC Ptr. */
    PPDMDEVINSRC            pDevInsRC;
    /** The APIC helpers - RC Ptr. */
    PCPDMAPICHLPRC          pApicHlpRC;
    /** LAPICs states - RC Ptr */
    RCPTRTYPE(APICState *)  paLapicsRC;
    /** The critical section - R3 Ptr. */
    RCPTRTYPE(PPDMCRITSECT) pCritSectRC;

    /** APIC specification version in this virtual hardware configuration. */
    PDMAPICVERSION          enmVersion;

    /** Number of attempts made to optimize TPR accesses. */
    uint32_t                cTPRPatchAttempts;

    /** Number of CPUs on the system (same as LAPIC count). */
    uint32_t                cCpus;
    /** Whether we've got an IO APIC or not. */
    bool                    fIoApic;
    /** Alignment padding. */
    bool                    afPadding[3];

# ifdef VBOX_WITH_STATISTICS
    STAMCOUNTER             StatMMIOReadGC;
    STAMCOUNTER             StatMMIOReadHC;
    STAMCOUNTER             StatMMIOWriteGC;
    STAMCOUNTER             StatMMIOWriteHC;
    STAMCOUNTER             StatClearedActiveIrq;
# endif
} APICDeviceInfo;
# ifdef VBOX_WITH_STATISTICS
AssertCompileMemberAlignment(APICDeviceInfo, StatMMIOReadGC, 8);
# endif

#ifndef VBOX_DEVICE_STRUCT_TESTCASE

/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
RT_C_DECLS_BEGIN
PDMBOTHCBDECL(int)  apicMMIORead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void *pv, unsigned cb);
PDMBOTHCBDECL(int)  apicMMIOWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void *pv, unsigned cb);
PDMBOTHCBDECL(int)  apicGetInterrupt(PPDMDEVINS pDevIns);
PDMBOTHCBDECL(bool) apicHasPendingIrq(PPDMDEVINS pDevIns);
PDMBOTHCBDECL(void) apicSetBase(PPDMDEVINS pDevIns, uint64_t val);
PDMBOTHCBDECL(uint64_t) apicGetBase(PPDMDEVINS pDevIns);
PDMBOTHCBDECL(void) apicSetTPR(PPDMDEVINS pDevIns, VMCPUID idCpu, uint8_t val);
PDMBOTHCBDECL(uint8_t) apicGetTPR(PPDMDEVINS pDevIns, VMCPUID idCpu);
PDMBOTHCBDECL(int)  apicBusDeliverCallback(PPDMDEVINS pDevIns, uint8_t u8Dest, uint8_t u8DestMode,
                                           uint8_t u8DeliveryMode, uint8_t iVector, uint8_t u8Polarity,
                                           uint8_t u8TriggerMode);
PDMBOTHCBDECL(int)  apicLocalInterrupt(PPDMDEVINS pDevIns, uint8_t u8Pin, uint8_t u8Level);
PDMBOTHCBDECL(int)  apicWriteMSR(PPDMDEVINS pDevIns, VMCPUID iCpu, uint32_t u32Reg, uint64_t u64Value);
PDMBOTHCBDECL(int)  apicReadMSR(PPDMDEVINS pDevIns, VMCPUID iCpu, uint32_t u32Reg, uint64_t *pu64Value);
PDMBOTHCBDECL(int)  ioapicMMIORead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void *pv, unsigned cb);
PDMBOTHCBDECL(int)  ioapicMMIOWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void *pv, unsigned cb);
PDMBOTHCBDECL(void) ioapicSetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel);
PDMBOTHCBDECL(void) ioapicSendMsi(PPDMDEVINS pDevIns, RTGCPHYS GCAddr, uint32_t uValue);

static void apic_update_tpr(APICDeviceInfo *dev, APICState* s, uint32_t val);
RT_C_DECLS_END

static void apic_eoi(APICDeviceInfo *dev, APICState* s); /*  */
static uint32_t apic_get_delivery_bitmask(APICDeviceInfo* dev, uint8_t dest, uint8_t dest_mode);
static int apic_deliver(APICDeviceInfo* dev, APICState *s,
                        uint8_t dest, uint8_t dest_mode,
                        uint8_t delivery_mode, uint8_t vector_num,
                        uint8_t polarity, uint8_t trigger_mode);
static int apic_get_arb_pri(APICState *s);
static int apic_get_ppr(APICState *s);
static uint32_t apic_get_current_count(APICDeviceInfo* dev, APICState *s);
static void apicTimerSetInitialCount(APICDeviceInfo *dev, APICState *s, uint32_t initial_count);
static void apicTimerSetLvt(APICDeviceInfo *dev, APICState *pThis, uint32_t fNew);
static void apicSendInitIpi(APICDeviceInfo* dev, APICState *s);

static void apic_init_ipi(APICDeviceInfo* dev, APICState *s);
static void apic_set_irq(APICDeviceInfo* dev, APICState *s, int vector_num, int trigger_mode);
static bool apic_update_irq(APICDeviceInfo* dev, APICState *s);


DECLINLINE(APICState*) getLapicById(APICDeviceInfo* dev, VMCPUID id)
{
    AssertFatalMsg(id < dev->cCpus, ("CPU id %d out of range\n", id));
    return &dev->CTX_SUFF(paLapics)[id];
}

DECLINLINE(APICState*) getLapic(APICDeviceInfo* dev)
{
    /* LAPIC's array is indexed by CPU id */
    VMCPUID id = dev->CTX_SUFF(pApicHlp)->pfnGetCpuId(dev->CTX_SUFF(pDevIns));
    return getLapicById(dev, id);
}

DECLINLINE(VMCPUID) getCpuFromLapic(APICDeviceInfo* dev, APICState *s)
{
    /* for now we assume LAPIC physical id == CPU id */
    return VMCPUID(s->phys_id);
}

DECLINLINE(void) cpuSetInterrupt(APICDeviceInfo* dev, APICState *s, PDMAPICIRQ enmType = PDMAPICIRQ_HARDWARE)
{
    LogFlow(("apic: setting interrupt flag for cpu %d\n", getCpuFromLapic(dev, s)));
    dev->CTX_SUFF(pApicHlp)->pfnSetInterruptFF(dev->CTX_SUFF(pDevIns), enmType,
                                               getCpuFromLapic(dev, s));
}

DECLINLINE(void) cpuClearInterrupt(APICDeviceInfo* dev, APICState *s, PDMAPICIRQ enmType = PDMAPICIRQ_HARDWARE)
{
    LogFlow(("apic: clear interrupt flag\n"));
    dev->CTX_SUFF(pApicHlp)->pfnClearInterruptFF(dev->CTX_SUFF(pDevIns), enmType,
                                                 getCpuFromLapic(dev, s));
}

# ifdef IN_RING3

DECLINLINE(void) cpuSendSipi(APICDeviceInfo* dev, APICState *s, int vector)
{
    Log2(("apic: send SIPI vector=%d\n", vector));

    dev->pApicHlpR3->pfnSendSipi(dev->pDevInsR3,
                                 getCpuFromLapic(dev, s),
                                 vector);
}

DECLINLINE(void) cpuSendInitIpi(APICDeviceInfo* dev, APICState *s)
{
    Log2(("apic: send init IPI\n"));

    dev->pApicHlpR3->pfnSendInitIpi(dev->pDevInsR3,
                                    getCpuFromLapic(dev, s));
}

# endif /* IN_RING3 */

DECLINLINE(uint32_t) getApicEnableBits(APICDeviceInfo* dev)
{
    switch (dev->enmVersion)
    {
        case PDMAPICVERSION_NONE:
            return 0;
        case PDMAPICVERSION_APIC:
            return MSR_IA32_APICBASE_ENABLE;
        case PDMAPICVERSION_X2APIC:
            return MSR_IA32_APICBASE_ENABLE | MSR_IA32_APICBASE_X2ENABLE ;
        default:
            AssertMsgFailed(("Unsupported APIC version %d\n", dev->enmVersion));
            return 0;
    }
}

DECLINLINE(PDMAPICVERSION) getApicMode(APICState *apic)
{
    switch (((apic->apicbase) >> 10) & 0x3)
    {
        case 0:
            return PDMAPICVERSION_NONE;
        case 1:
        default:
            /* Invalid */
            return PDMAPICVERSION_NONE;
        case 2:
            return PDMAPICVERSION_APIC;
        case 3:
            return PDMAPICVERSION_X2APIC;
    }
}

static int apic_bus_deliver(APICDeviceInfo* dev,
                            uint32_t deliver_bitmask, uint8_t delivery_mode,
                            uint8_t vector_num, uint8_t polarity,
                            uint8_t trigger_mode)
{
    LogFlow(("apic_bus_deliver mask=%x mode=%x vector=%x polarity=%x trigger_mode=%x\n", deliver_bitmask, delivery_mode, vector_num, polarity, trigger_mode));
    switch (delivery_mode) {
        case APIC_DM_LOWPRI:
        {
            int d = -1;
            if (deliver_bitmask)
                d = ffs_bit(deliver_bitmask);
            if (d >= 0)
            {
                APICState* apic = getLapicById(dev, d);
                apic_set_irq(dev, apic, vector_num, trigger_mode);
            }
            return VINF_SUCCESS;
        }
        case APIC_DM_FIXED:
            /* XXX: arbitration */
            break;

        case APIC_DM_SMI:
            foreach_apic(dev, deliver_bitmask,
                         cpuSetInterrupt(dev, apic, PDMAPICIRQ_SMI));
            return VINF_SUCCESS;

        case APIC_DM_NMI:
            foreach_apic(dev, deliver_bitmask,
                         cpuSetInterrupt(dev, apic, PDMAPICIRQ_NMI));
            return VINF_SUCCESS;

        case APIC_DM_INIT:
            /* normal INIT IPI sent to processors */
#ifdef IN_RING3
            foreach_apic(dev, deliver_bitmask,
                         apicSendInitIpi(dev, apic));
            return VINF_SUCCESS;
#else
            /* We shall send init IPI only in R3, R0 calls should be
               rescheduled to R3 */
            return VINF_IOM_HC_MMIO_READ_WRITE;
#endif /* IN_RING3 */
        case APIC_DM_EXTINT:
            /* handled in I/O APIC code */
            break;

        default:
            return VINF_SUCCESS;
    }

    foreach_apic(dev, deliver_bitmask,
                       apic_set_irq (dev, apic, vector_num, trigger_mode));
    return VINF_SUCCESS;
}


PDMBOTHCBDECL(void) apicSetBase(PPDMDEVINS pDevIns, uint64_t val)
{
    APICDeviceInfo *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    Assert(PDMCritSectIsOwner(dev->CTX_SUFF(pCritSect)));
    APICState *s = getLapic(dev); /** @todo fix interface */
    Log(("apicSetBase: %016RX64\n", val));

    /** @todo: do we need to lock here ? */
    /* APIC_LOCK_VOID(dev, VERR_INTERNAL_ERROR); */
    /** @todo If this change is valid immediately, then we should change the MMIO registration! */
    /* We cannot change if this CPU is BSP or not by writing to MSR - it's hardwired */
    PDMAPICVERSION oldMode = getApicMode(s);
    s->apicbase =
            (val & 0xfffff000) | /* base */
            (val & getApicEnableBits(dev)) | /* mode */
            (s->apicbase & MSR_IA32_APICBASE_BSP) /* keep BSP bit */;
    PDMAPICVERSION newMode = getApicMode(s);

    if (oldMode != newMode)
    {
        switch (newMode)
        {
            case PDMAPICVERSION_NONE:
            {
                s->spurious_vec &= ~APIC_SV_ENABLE;
                /* Clear any pending APIC interrupt action flag. */
                cpuClearInterrupt(dev, s);
                /** @todo: why do we do that? */
                dev->CTX_SUFF(pApicHlp)->pfnChangeFeature(pDevIns, PDMAPICVERSION_NONE);
                break;
            }
            case PDMAPICVERSION_APIC:
                /** @todo: map MMIO ranges, if needed */
                break;
            case PDMAPICVERSION_X2APIC:
                /** @todo: unmap MMIO ranges of this APIC, according to the spec */
                break;
            default:
                break;
        }
    }
    /* APIC_UNLOCK(dev); */
}

PDMBOTHCBDECL(uint64_t) apicGetBase(PPDMDEVINS pDevIns)
{
    APICDeviceInfo *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    Assert(PDMCritSectIsOwner(dev->CTX_SUFF(pCritSect)));
    APICState *s = getLapic(dev); /** @todo fix interface */
    LogFlow(("apicGetBase: %016llx\n", (uint64_t)s->apicbase));
    return s->apicbase;
}

PDMBOTHCBDECL(void) apicSetTPR(PPDMDEVINS pDevIns, VMCPUID idCpu, uint8_t val)
{
    APICDeviceInfo *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    Assert(PDMCritSectIsOwner(dev->CTX_SUFF(pCritSect)));
    APICState *s = getLapicById(dev, idCpu);
    LogFlow(("apicSetTPR: val=%#x (trp %#x -> %#x)\n", val, s->tpr, val));
    apic_update_tpr(dev, s, val);
}

PDMBOTHCBDECL(uint8_t) apicGetTPR(PPDMDEVINS pDevIns, VMCPUID idCpu)
{
    /* We don't perform any locking here as that would cause a lot of contention for VT-x/AMD-V. */
    APICDeviceInfo *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    APICState *s = getLapicById(dev, idCpu);
    Log2(("apicGetTPR: returns %#x\n", s->tpr));
    return s->tpr;
}

/**
 * x2APIC MSR write interface.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns         The device instance.
 * @param   idCpu           The ID of the virtual CPU and thereby APIC index.
 * @param   u32Reg          Register to write (ecx).
 * @param   u64Value        The value to write (eax:edx / rax).
 *
 */
PDMBOTHCBDECL(int) apicWriteMSR(PPDMDEVINS pDevIns, VMCPUID idCpu, uint32_t u32Reg, uint64_t u64Value)
{
    APICDeviceInfo *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    Assert(PDMCritSectIsOwner(dev->CTX_SUFF(pCritSect)));
    int rc = VINF_SUCCESS;

    if (dev->enmVersion < PDMAPICVERSION_X2APIC)
        return VERR_EM_INTERPRETER;

    APICState *pThis = getLapicById(dev, idCpu);

    uint32_t index = (u32Reg - MSR_IA32_APIC_START) & 0xff;
    switch (index)
    {
        case 0x02:
            pThis->id = (u64Value >> 24);
            break;
        case 0x03:
            break;
        case 0x08:
            apic_update_tpr(dev, pThis, u64Value);
            break;
        case 0x09: case 0x0a:
            Log(("apicWriteMSR: write to read-only register %d ignored\n", index));
            break;
        case 0x0b: /* EOI */
            apic_eoi(dev, pThis);
            break;
        case 0x0d:
            pThis->log_dest = u64Value >> 24;
            break;
        case 0x0e:
            pThis->dest_mode = u64Value >> 28;
            break;
        case 0x0f:
            pThis->spurious_vec = u64Value & 0x1ff;
            apic_update_irq(dev, pThis);
            break;
        case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
        case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
        case 0x28:
            Log(("apicWriteMSR: write to read-only register %d ignored\n", index));
            break;

        case 0x30:
            /* Here one of the differences with regular APIC: ICR is single 64-bit register */
            pThis->icr[0] = (uint32_t)u64Value;
            pThis->icr[1] = (uint32_t)(u64Value >> 32);
            rc = apic_deliver(dev, pThis, (pThis->icr[1] >> 24) & 0xff, (pThis->icr[0] >> 11) & 1,
                             (pThis->icr[0] >>  8) & 7, (pThis->icr[0] & 0xff),
                             (pThis->icr[0] >> 14) & 1, (pThis->icr[0] >> 15) & 1);
            break;
        case 0x32 + APIC_LVT_TIMER:
            AssertCompile(APIC_LVT_TIMER == 0);
            apicTimerSetLvt(dev, pThis, u64Value);
            break;

        case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
            pThis->lvt[index - 0x32] = u64Value;
            break;
        case 0x38:
            apicTimerSetInitialCount(dev, pThis, u64Value);
            break;
        case 0x39:
            Log(("apicWriteMSR: write to read-only register %d ignored\n", index));
            break;
        case 0x3e:
        {
            int v;
            pThis->divide_conf = u64Value & 0xb;
            v = (pThis->divide_conf & 3) | ((pThis->divide_conf >> 1) & 4);
            pThis->count_shift = (v + 1) & 7;
            break;
        }
        case 0x3f:
        {
            /* Self IPI, see x2APIC book 2.4.5 */
            int vector = u64Value & 0xff;
            rc = apic_bus_deliver(dev,
                                  1 << getLapicById(dev, idCpu)->id /* Self */,
                                  0 /* Delivery mode - fixed */,
                                  vector,
                                  0 /* Polarity - conform to the bus */,
                                  0 /* Trigger mode - edge */);
            break;
        }
        default:
            AssertMsgFailed(("apicWriteMSR: unknown index %x\n", index));
            pThis->esr |= ESR_ILLEGAL_ADDRESS;
            break;
    }

    return rc;
}

/**
 * x2APIC MSR read interface.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns         The device instance.
 * @param   idCpu           The ID of the virtual CPU and thereby APIC index.
 * @param   u32Reg          Register to write (ecx).
 * @param   pu64Value       Where to return the value (eax:edx / rax).
 */
PDMBOTHCBDECL(int) apicReadMSR(PPDMDEVINS pDevIns, VMCPUID idCpu, uint32_t u32Reg, uint64_t *pu64Value)
{
    APICDeviceInfo *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    Assert(PDMCritSectIsOwner(dev->CTX_SUFF(pCritSect)));

    if (dev->enmVersion < PDMAPICVERSION_X2APIC)
        return VERR_EM_INTERPRETER;

    uint32_t index = (u32Reg - MSR_IA32_APIC_START) & 0xff;
    APICState* apic = getLapicById(dev, idCpu);
    uint64_t val = 0;

    switch (index)
    {
        case 0x02: /* id */
            val = apic->id << 24;
            break;
        case 0x03: /* version */
            val =   APIC_HW_VERSION                                     |
                    ((APIC_LVT_NB - 1) << 16) /* Max LVT index */       |
                    (0 << 24) /* Support for EOI broadcast suppression */;
            break;
        case 0x08:
            val = apic->tpr;
            break;
        case 0x09:
            val = apic_get_arb_pri(apic);
            break;
        case 0x0a:
            /* ppr */
            val = apic_get_ppr(apic);
            break;
        case 0x0b:
            val = 0;
            break;
        case 0x0d:
            val = (uint64_t)apic->log_dest << 24;
            break;
        case 0x0e:
            /* Bottom 28 bits are always 1 */
            val = ((uint64_t)apic->dest_mode << 28) | 0xfffffff;
            break;
        case 0x0f:
            val = apic->spurious_vec;
        break;
        case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
            val = apic->isr[index & 7];
            break;
        case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
            val = apic->tmr[index & 7];
            break;
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
            val = apic->irr[index & 7];
            break;
        case 0x28:
            val = apic->esr;
            break;
        case 0x30:
            /* Here one of the differences with regular APIC: ICR is single 64-bit register */
            val = ((uint64_t)apic->icr[1] << 32) | apic->icr[0];
            break;
        case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
            val = apic->lvt[index - 0x32];
            break;
        case 0x38:
            val = apic->initial_count;
            break;
        case 0x39:
            val = apic_get_current_count(dev, apic);
            break;
        case 0x3e:
            val = apic->divide_conf;
            break;
        case 0x3f:
            /* Self IPI register is write only */
            Log(("apicReadMSR: read from write-only register %d ignored\n", index));
            break;
        case 0x2f:
            /**
             * Correctable machine check exception vector, @todo: implement me!
             */
        default:
            AssertMsgFailed(("apicReadMSR: unknown index %x\n", index));
            /**
             * @todo: according to spec when APIC writes to ESR it msut raise error interrupt,
             *        i.e. LVT[5]
             */
            apic->esr |= ESR_ILLEGAL_ADDRESS;
            val = 0;
            break;
    }
    *pu64Value = val;
    return VINF_SUCCESS;
}

/**
 * More or less private interface between IOAPIC, only PDM is responsible
 * for connecting the two devices.
 */
PDMBOTHCBDECL(int) apicBusDeliverCallback(PPDMDEVINS pDevIns, uint8_t u8Dest, uint8_t u8DestMode,
                                           uint8_t u8DeliveryMode, uint8_t iVector, uint8_t u8Polarity,
                                           uint8_t u8TriggerMode)
{
    APICDeviceInfo *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    Assert(PDMCritSectIsOwner(dev->CTX_SUFF(pCritSect)));
    LogFlow(("apicBusDeliverCallback: pDevIns=%p u8Dest=%#x u8DestMode=%#x u8DeliveryMode=%#x iVector=%#x u8Polarity=%#x u8TriggerMode=%#x\n",
             pDevIns, u8Dest, u8DestMode, u8DeliveryMode, iVector, u8Polarity, u8TriggerMode));
    return apic_bus_deliver(dev, apic_get_delivery_bitmask(dev, u8Dest, u8DestMode),
                            u8DeliveryMode, iVector, u8Polarity, u8TriggerMode);
}

/**
 * Local interrupt delivery, for devices attached to the CPU's LINT0/LINT1 pin.
 * Normally used for 8259A PIC and NMI.
 */
PDMBOTHCBDECL(int) apicLocalInterrupt(PPDMDEVINS pDevIns, uint8_t u8Pin, uint8_t u8Level)
{
    APICDeviceInfo  *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    APICState       *s = getLapicById(dev, 0);

    Assert(PDMCritSectIsOwner(dev->CTX_SUFF(pCritSect)));
    LogFlow(("apicLocalInterrupt: pDevIns=%p u8Pin=%x u8Level=%x\n", pDevIns, u8Pin, u8Level));

    /* If LAPIC is disabled, go straight to the CPU. */
    if (!(s->spurious_vec & APIC_SV_ENABLE))
    {
        LogFlow(("apicLocalInterrupt: LAPIC disabled, delivering directly to CPU core.\n"));
        if (u8Level)
            cpuSetInterrupt(dev, s, PDMAPICIRQ_EXTINT);
        else
            cpuClearInterrupt(dev, s, PDMAPICIRQ_EXTINT);

        return VINF_SUCCESS;
    }

    /* If LAPIC is enabled, interrupts are subject to LVT programming. */

    /* There are only two local interrupt pins. */
    AssertMsgReturn(u8Pin <= 1, ("Invalid LAPIC pin %d\n", u8Pin), VERR_INVALID_PARAMETER);

    /* NB: We currently only deliver local interrupts to the first CPU. In theory they
     * should be delivered to all CPUs and it is the guest's responsibility to ensure
     * no more than one CPU has the interrupt unmasked.
     */
    uint32_t    u32Lvec;

    u32Lvec = s->lvt[APIC_LVT_LINT0 + u8Pin];   /* Fetch corresponding LVT entry. */
    /* Drop int if entry is masked. May not be correct for level-triggered interrupts. */
    if (!(u32Lvec & APIC_LVT_MASKED))
    {   uint8_t     u8Delivery;
        PDMAPICIRQ  enmType;

        u8Delivery = (u32Lvec >> 8) & 7;
        switch (u8Delivery)
        {
            case APIC_DM_EXTINT:
                Assert(u8Pin == 0); /* PIC should be wired to LINT0. */
                enmType = PDMAPICIRQ_EXTINT;
                /* ExtINT can be both set and cleared, NMI/SMI/INIT can only be set. */
                LogFlow(("apicLocalInterrupt: %s ExtINT interrupt\n", u8Level ? "setting" : "clearing"));
                if (u8Level)
                    cpuSetInterrupt(dev, s, enmType);
                else
                    cpuClearInterrupt(dev, s, enmType);
                return VINF_SUCCESS;
            case APIC_DM_NMI:
                /* External NMI should be wired to LINT1, but Linux sometimes programs
                 * LVT0 to NMI delivery mode as well.
                 */
                enmType = PDMAPICIRQ_NMI;
                /* Currently delivering NMIs through here causes problems with NMI watchdogs
                 * on certain Linux kernels, e.g. 64-bit CentOS 5.3. Disable NMIs for now.
                 */
                return VINF_SUCCESS;
            case APIC_DM_SMI:
                enmType = PDMAPICIRQ_SMI;
                break;
            case APIC_DM_FIXED:
            {
                /** @todo implement APIC_DM_FIXED! */
                static unsigned s_c = 0;
                if (s_c++ < 5)
                    LogRel(("delivery type APIC_DM_FIXED not implemented. u8Pin=%d u8Level=%d\n", u8Pin, u8Level));
                return  VINF_SUCCESS;
            }
            case APIC_DM_INIT:
                /** @todo implement APIC_DM_INIT? */
            default:
            {
                static unsigned s_c = 0;
                if (s_c++ < 100)
                    AssertLogRelMsgFailed(("delivery type %d not implemented. u8Pin=%d u8Level=%d\n", u8Delivery, u8Pin, u8Level));
                return VERR_INTERNAL_ERROR_4;
            }
        }
        LogFlow(("apicLocalInterrupt: setting local interrupt type %d\n", enmType));
        cpuSetInterrupt(dev, s, enmType);
    }
    return VINF_SUCCESS;
}

/* return -1 if no bit is set */
static int get_highest_priority_int(uint32_t *tab)
{
    int i;
    for(i = 7; i >= 0; i--) {
        if (tab[i] != 0) {
            return i * 32 + fls_bit(tab[i]);
        }
    }
    return -1;
}

static int apic_get_ppr(APICState *s)
{
    int tpr, isrv, ppr;

    tpr = (s->tpr >> 4);
    isrv = get_highest_priority_int(s->isr);
    if (isrv < 0)
        isrv = 0;
    isrv >>= 4;
    if (tpr >= isrv)
        ppr = s->tpr;
    else
        ppr = isrv << 4;
    return ppr;
}

static int apic_get_ppr_zero_tpr(APICState *s)
{
    int isrv;

    isrv = get_highest_priority_int(s->isr);
    if (isrv < 0)
        isrv = 0;
    return isrv;
}

static int apic_get_arb_pri(APICState *s)
{
    /* XXX: arbitration */
    return 0;
}

/* signal the CPU if an irq is pending */
static bool apic_update_irq(APICDeviceInfo *dev, APICState* s)
{
    int irrv, ppr;
    if (!(s->spurious_vec & APIC_SV_ENABLE))
    {
        /* Clear any pending APIC interrupt action flag. */
        cpuClearInterrupt(dev, s);
        return false;
    }

    irrv = get_highest_priority_int(s->irr);
    if (irrv < 0)
        return false;
    ppr = apic_get_ppr(s);
    if (ppr && (irrv & 0xf0) <= (ppr & 0xf0))
        return false;
    cpuSetInterrupt(dev, s);
    return true;
}

/* Check if the APIC has a pending interrupt/if a TPR change would active one. */
PDMBOTHCBDECL(bool) apicHasPendingIrq(PPDMDEVINS pDevIns)
{
    int irrv, ppr;
    APICDeviceInfo *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    if (!dev)
        return false;

    /* We don't perform any locking here as that would cause a lot of contention for VT-x/AMD-V. */

    APICState *s = getLapic(dev); /** @todo fix interface */

    /*
     * All our callbacks now come from single IOAPIC, thus locking
     * seems to be excessive now (@todo: check)
     */
    irrv = get_highest_priority_int(s->irr);
    if (irrv < 0)
        return false;

    ppr = apic_get_ppr_zero_tpr(s);

    if (ppr && (irrv & 0xf0) <= (ppr & 0xf0))
        return false;

    return true;
}

static void apic_update_tpr(APICDeviceInfo *dev, APICState* s, uint32_t val)
{
    bool fIrqIsActive = false;
    bool fIrqWasActive = false;

    fIrqWasActive = apic_update_irq(dev, s);
    s->tpr        = val;
    fIrqIsActive  = apic_update_irq(dev, s);

    /* If an interrupt is pending and now masked, then clear the FF flag. */
    if (fIrqWasActive && !fIrqIsActive)
    {
        Log(("apic_update_tpr: deactivate interrupt that was masked by the TPR update (%x)\n", val));
        STAM_COUNTER_INC(&dev->StatClearedActiveIrq);
        cpuClearInterrupt(dev, s);
    }
}

static void apic_set_irq(APICDeviceInfo *dev,  APICState* s, int vector_num, int trigger_mode)
{
    LogFlow(("CPU%d: apic_set_irq vector=%x, trigger_mode=%x\n", s->phys_id, vector_num, trigger_mode));
    set_bit(s->irr, vector_num);
    if (trigger_mode)
        set_bit(s->tmr, vector_num);
    else
        reset_bit(s->tmr, vector_num);
    apic_update_irq(dev, s);
}

static void apic_eoi(APICDeviceInfo *dev, APICState* s)
{
    int isrv;
    isrv = get_highest_priority_int(s->isr);
    if (isrv < 0)
        return;
    reset_bit(s->isr, isrv);
    LogFlow(("CPU%d: apic_eoi isrv=%x\n", s->phys_id, isrv));
    /* XXX: send the EOI packet to the APIC bus to allow the I/O APIC to
            set the remote IRR bit for level triggered interrupts. */
    apic_update_irq(dev, s);
}

static uint32_t apic_get_delivery_bitmask(APICDeviceInfo *dev, uint8_t dest, uint8_t dest_mode)
{
    uint32_t mask = 0;

    if (dest_mode == 0)
    {
        if (dest == 0xff)
            mask = 0xff;
        else
            mask = 1 << dest;
    }
    else
    {
        APICState *apic = dev->CTX_SUFF(paLapics);
        uint32_t i;

        /* XXX: cluster mode */
        for(i = 0; i < dev->cCpus; i++)
        {
            if (apic->dest_mode == APIC_DESTMODE_FLAT)
            {
                if (dest & apic->log_dest)
                    mask |= (1 << i);
            }
            else if (apic->dest_mode == APIC_DESTMODE_CLUSTER)
            {
                if ((dest & 0xf0) == (apic->log_dest & 0xf0)
                    &&
                    (dest & apic->log_dest & 0x0f))
                {
                    mask |= (1 << i);
                }
            }
            apic++;
        }
    }

    return mask;
}

#ifdef IN_RING3
static void apic_init_ipi(APICDeviceInfo* dev, APICState *s)
{
    int i;

    for(i = 0; i < APIC_LVT_NB; i++)
        s->lvt[i] = 1 << 16; /* mask LVT */
    s->tpr = 0;
    s->spurious_vec = 0xff;
    s->log_dest = 0;
    s->dest_mode = 0xff;
    memset(s->isr, 0, sizeof(s->isr));
    memset(s->tmr, 0, sizeof(s->tmr));
    memset(s->irr, 0, sizeof(s->irr));
    s->esr = 0;
    memset(s->icr, 0, sizeof(s->icr));
    s->divide_conf = 0;
    s->count_shift = 1;
    s->initial_count = 0;
    s->initial_count_load_time = 0;
    s->next_time = 0;
}


static void apicSendInitIpi(APICDeviceInfo* dev, APICState *s)
{
    apic_init_ipi(dev, s);
    cpuSendInitIpi(dev, s);
}

/* send a SIPI message to the CPU to start it */
static void apic_startup(APICDeviceInfo* dev, APICState *s, int vector_num)
{
    Log(("[SMP] apic_startup: %d on CPUs %d\n", vector_num, s->phys_id));
    cpuSendSipi(dev, s, vector_num);
}
#endif /* IN_RING3 */

static int  apic_deliver(APICDeviceInfo* dev, APICState *s,
                         uint8_t dest, uint8_t dest_mode,
                         uint8_t delivery_mode, uint8_t vector_num,
                         uint8_t polarity, uint8_t trigger_mode)
{
    uint32_t deliver_bitmask = 0;
    int dest_shorthand = (s->icr[0] >> 18) & 3;

    LogFlow(("apic_deliver dest=%x dest_mode=%x dest_shorthand=%x delivery_mode=%x vector_num=%x polarity=%x trigger_mode=%x\n", dest, dest_mode, dest_shorthand, delivery_mode, vector_num, polarity, trigger_mode));

    switch (dest_shorthand) {
        case 0:
            deliver_bitmask = apic_get_delivery_bitmask(dev, dest, dest_mode);
            break;
        case 1:
            deliver_bitmask = (1 << s->id);
            break;
        case 2:
            deliver_bitmask = 0xffffffff;
            break;
        case 3:
            deliver_bitmask = 0xffffffff & ~(1 << s->id);
            break;
    }

    switch (delivery_mode) {
        case APIC_DM_INIT:
            {
                int trig_mode = (s->icr[0] >> 15) & 1;
                int level = (s->icr[0] >> 14) & 1;
                if (level == 0 && trig_mode == 1) {
                    foreach_apic(dev, deliver_bitmask,
                                       apic->arb_id = apic->id);
                    Log(("CPU%d: APIC_DM_INIT arbitration id(s) set\n", s->phys_id));
                    return VINF_SUCCESS;
                }
            }
            break;

        case APIC_DM_SIPI:
# ifdef IN_RING3
            foreach_apic(dev, deliver_bitmask,
                         apic_startup(dev, apic, vector_num));
            return VINF_SUCCESS;
# else
            /* We shall send SIPI only in R3, R0 calls should be
               rescheduled to R3 */
            return  VINF_IOM_HC_MMIO_WRITE;
# endif
    }

    return apic_bus_deliver(dev, deliver_bitmask, delivery_mode, vector_num,
                            polarity, trigger_mode);
}


PDMBOTHCBDECL(int) apicGetInterrupt(PPDMDEVINS pDevIns)
{
    APICDeviceInfo *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    /* if the APIC is not installed or enabled, we let the 8259 handle the
       IRQs */
    if (!dev)
    {
        Log(("apic_get_interrupt: returns -1 (!s)\n"));
        return -1;
    }

    Assert(PDMCritSectIsOwner(dev->CTX_SUFF(pCritSect)));

    APICState *s = getLapic(dev);  /** @todo fix interface */
    int intno;

    if (!(s->spurious_vec & APIC_SV_ENABLE)) {
        Log(("CPU%d: apic_get_interrupt: returns -1 (APIC_SV_ENABLE)\n", s->phys_id));
        return -1;
    }

    /* XXX: spurious IRQ handling */
    intno = get_highest_priority_int(s->irr);
    if (intno < 0) {
        Log(("CPU%d: apic_get_interrupt: returns -1 (irr)\n", s->phys_id));
        return -1;
    }
    if (s->tpr && (uint32_t)intno <= s->tpr) {
        Log(("apic_get_interrupt: returns %d (sp)\n", s->spurious_vec & 0xff));
        return s->spurious_vec & 0xff;
    }
    reset_bit(s->irr, intno);
    set_bit(s->isr, intno);
    apic_update_irq(dev, s);
    LogFlow(("CPU%d: apic_get_interrupt: returns %d\n", s->phys_id, intno));
    return intno;
}

static uint32_t apic_get_current_count(APICDeviceInfo *dev, APICState *s)
{
    int64_t d;
    uint32_t val;

    d = (TMTimerGet(s->CTX_SUFF(pTimer)) - s->initial_count_load_time) >>
        s->count_shift;

    if (s->lvt[APIC_LVT_TIMER] & APIC_LVT_TIMER_PERIODIC) {
        /* periodic */
        val = s->initial_count - (d % ((uint64_t)s->initial_count + 1));
    } else {
        if (d >= s->initial_count)
            val = 0;
        else
            val = s->initial_count - d;
    }
    return val;
}

/**
 * Does the frequency hinting and logging.
 *
 * @param   pThis               The device state.
 */
DECLINLINE(void) apicDoFrequencyHinting(APICState *pThis)
{
    if (   pThis->uHintedInitialCount != pThis->initial_count
        || pThis->uHintedCountShift   != (uint32_t)pThis->count_shift)
    {
        pThis->uHintedInitialCount  = pThis->initial_count;
        pThis->uHintedCountShift    = pThis->count_shift;

        uint32_t uHz;
        if (pThis->initial_count > 0)
        {
            Assert((unsigned)pThis->count_shift < 30);
            uint64_t cTickPerPeriod = ((uint64_t)pThis->initial_count + 1) << pThis->count_shift;
            uHz = TMTimerGetFreq(pThis->CTX_SUFF(pTimer)) / cTickPerPeriod;
        }
        else
            uHz = 0;
        TMTimerSetFrequencyHint(pThis->CTX_SUFF(pTimer), uHz);
        Log(("apic: %u Hz\n", uHz));
    }
}

/**
 * Implementation of the 0380h access: Timer reset + new initial count.
 *
 * @param   dev                 The device state.
 * @param   pThis               The APIC sub-device state.
 * @param   u32NewInitialCount  The new initial count for the timer.
 */
static void apicTimerSetInitialCount(APICDeviceInfo *dev, APICState *pThis, uint32_t u32NewInitialCount)
{
    STAM_COUNTER_INC(&pThis->StatTimerSetInitialCount);
    pThis->initial_count = u32NewInitialCount;

    /*
     * Don't (re-)arm the timer if the it's masked or if it's
     * a zero length one-shot timer.
     */
    if (    !(pThis->lvt[APIC_LVT_TIMER] & APIC_LVT_MASKED)
        &&  u32NewInitialCount > 0)
    {
        /*
         * Calculate the relative next time and perform a combined timer get/set
         * operation. This avoids racing the clock between get and set.
         */
        uint64_t cTicksNext = u32NewInitialCount;
        cTicksNext         += 1;
        cTicksNext        <<= pThis->count_shift;
        TMTimerSetRelative(pThis->CTX_SUFF(pTimer), cTicksNext, &pThis->initial_count_load_time);
        pThis->next_time = pThis->initial_count_load_time + cTicksNext;
        pThis->fTimerArmed = true;
        apicDoFrequencyHinting(pThis);
        STAM_COUNTER_INC(&pThis->StatTimerSetInitialCountArm);
        Log(("apicTimerSetInitialCount: cTicksNext=%'llu (%#llx) ic=%#x sh=%#x nxt=%#llx\n", cTicksNext, cTicksNext, u32NewInitialCount, pThis->count_shift, pThis->next_time));
    }
    else
    {
        /* Stop it if necessary and record the load time for unmasking. */
        if (pThis->fTimerArmed)
        {
            STAM_COUNTER_INC(&pThis->StatTimerSetInitialCountDisarm);
            TMTimerStop(pThis->CTX_SUFF(pTimer));
            pThis->fTimerArmed = false;
            pThis->uHintedCountShift = pThis->uHintedInitialCount = 0;
        }
        pThis->initial_count_load_time = TMTimerGet(pThis->CTX_SUFF(pTimer));
        Log(("apicTimerSetInitialCount: ic=%#x sh=%#x iclt=%#llx\n", u32NewInitialCount, pThis->count_shift, pThis->initial_count_load_time));
    }
}

/**
 * Implementation of the 0320h access: change the LVT flags.
 *
 * @param   dev             The device state.
 * @param   pThis           The APIC sub-device state to operate on.
 * @param   fNew            The new flags.
 */
static void apicTimerSetLvt(APICDeviceInfo *dev, APICState *pThis, uint32_t fNew)
{
    STAM_COUNTER_INC(&pThis->StatTimerSetLvt);

    /*
     * Make the flag change, saving the old ones so we can avoid
     * unnecessary work.
     */
    uint32_t const fOld = pThis->lvt[APIC_LVT_TIMER];
    pThis->lvt[APIC_LVT_TIMER] = fNew;

    /* Only the masked and peridic bits are relevant (see apic_timer_update). */
    if (    (fOld & (APIC_LVT_MASKED | APIC_LVT_TIMER_PERIODIC))
        !=  (fNew & (APIC_LVT_MASKED | APIC_LVT_TIMER_PERIODIC)))
    {
        /*
         * If changed to one-shot from periodic, stop the timer if we're not
         * in the first period.
         */
        /** @todo check how clearing the periodic flag really should behave when not
         *        in period 1. The current code just mirrors the behavior of the
         *        original implementation. */
        if (    (fOld & APIC_LVT_TIMER_PERIODIC)
            && !(fNew & APIC_LVT_TIMER_PERIODIC))
        {
            STAM_COUNTER_INC(&pThis->StatTimerSetLvtClearPeriodic);
            uint64_t cTicks = (pThis->next_time - pThis->initial_count_load_time) >> pThis->count_shift;
            if (cTicks >= pThis->initial_count)
            {
                /* not first period, stop it. */
                TMTimerStop(pThis->CTX_SUFF(pTimer));
                pThis->fTimerArmed = false;
                pThis->uHintedCountShift = pThis->uHintedInitialCount = 0;
            }
            /* else: first period, let it fire normally. */
        }

        /*
         * We postpone stopping the timer when it's masked, this way we can
         * avoid some timer work when the guest temporarily masks the timer.
         * (apicTimerCallback will stop it if still masked.)
         */
        if (fNew & APIC_LVT_MASKED)
            STAM_COUNTER_INC(&pThis->StatTimerSetLvtPostponed);
        else if (pThis->fTimerArmed)
            STAM_COUNTER_INC(&pThis->StatTimerSetLvtArmed);
        /*
         * If unmasked, not armed and with a valid initial count value (according
         * to our interpretation of the spec), we will have to rearm the timer so
         * it will fire at the end of the current period.
         *
         * N.B. This is code is currently RACING the virtual sync clock!
         */
        else if (   (fOld & APIC_LVT_MASKED)
                 && pThis->initial_count > 0)
        {
            STAM_COUNTER_INC(&pThis->StatTimerSetLvtArm);
            for (unsigned cTries = 0; ; cTries++)
            {
                uint64_t NextTS;
                uint64_t cTicks = (TMTimerGet(pThis->CTX_SUFF(pTimer)) - pThis->initial_count_load_time) >> pThis->count_shift;
                if (fNew & APIC_LVT_TIMER_PERIODIC)
                    NextTS = ((cTicks / ((uint64_t)pThis->initial_count + 1)) + 1) * ((uint64_t)pThis->initial_count + 1);
                else
                {
                    if (cTicks >= pThis->initial_count)
                        break;
                    NextTS = (uint64_t)pThis->initial_count + 1;
                }
                NextTS <<= pThis->count_shift;
                NextTS += pThis->initial_count_load_time;

                /* Try avoid the assertion in TM.cpp... this isn't perfect! */
                if (    NextTS > TMTimerGet(pThis->CTX_SUFF(pTimer))
                    ||  cTries > 10)
                {
                    TMTimerSet(pThis->CTX_SUFF(pTimer), NextTS);
                    pThis->next_time = NextTS;
                    pThis->fTimerArmed = true;
                    apicDoFrequencyHinting(pThis);
                    Log(("apicTimerSetLvt: ic=%#x sh=%#x nxt=%#llx\n", pThis->initial_count, pThis->count_shift, pThis->next_time));
                    break;
                }
                STAM_COUNTER_INC(&pThis->StatTimerSetLvtArmRetries);
            }
        }
    }
    else
        STAM_COUNTER_INC(&pThis->StatTimerSetLvtNoRelevantChange);
}

# ifdef IN_RING3
/**
 * Timer callback function.
 *
 * @param  pDevIns      The device state.
 * @param  pTimer       The timer handle.
 * @param  pvUser       User argument pointing to the APIC instance.
 */
static DECLCALLBACK(void) apicTimerCallback(PPDMDEVINS pDevIns, PTMTIMER pTimer, void *pvUser)
{
    APICDeviceInfo *dev   = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    APICState      *pThis = (APICState *)pvUser;
    Assert(pThis->pTimerR3 == pTimer);
    Assert(pThis->fTimerArmed);

    if (!(pThis->lvt[APIC_LVT_TIMER] & APIC_LVT_MASKED)) {
        LogFlow(("apic_timer: trigger irq\n"));
        apic_set_irq(dev, pThis, pThis->lvt[APIC_LVT_TIMER] & 0xff, APIC_TRIGGER_EDGE);

        if (   (pThis->lvt[APIC_LVT_TIMER] & APIC_LVT_TIMER_PERIODIC)
            && pThis->initial_count > 0) {
            /* new interval. */
            pThis->next_time += (((uint64_t)pThis->initial_count + 1) << pThis->count_shift);
            TMTimerSet(pThis->CTX_SUFF(pTimer), pThis->next_time);
            pThis->fTimerArmed = true;
            apicDoFrequencyHinting(pThis);
            Log2(("apicTimerCallback: ic=%#x sh=%#x nxt=%#llx\n", pThis->initial_count, pThis->count_shift, pThis->next_time));
        } else {
            /* single shot or disabled. */
            pThis->fTimerArmed = false;
            pThis->uHintedCountShift = pThis->uHintedInitialCount = 0;
        }
    } else {
        /* masked, do not rearm. */
        pThis->fTimerArmed = false;
        pThis->uHintedCountShift = pThis->uHintedInitialCount = 0;
    }
}
# endif /* IN_RING3 */

static uint32_t apic_mem_readl(APICDeviceInfo* dev, APICState *s, RTGCPHYS addr)
{
    uint32_t val;
    int index;

    index = (addr >> 4) & 0xff;

    switch(index) {
    case 0x02: /* id */
        val = s->id << 24;
        break;
    case 0x03: /* version */
        val = APIC_HW_VERSION | ((APIC_LVT_NB - 1) << 16);
        break;
    case 0x08:
        val = s->tpr;
        break;
    case 0x09:
        val = apic_get_arb_pri(s);
        break;
    case 0x0a:
        /* ppr */
        val = apic_get_ppr(s);
        break;
    case 0x0b:
        Log(("apic_mem_readl %x %x -> write only returning 0\n", addr, index));
        val = 0;
        break;
    case 0x0d:
        val = s->log_dest << 24;
        break;
    case 0x0e:
        /* Bottom 28 bits are always 1 */
        val = (s->dest_mode << 28) | 0xfffffff;
        break;
    case 0x0f:
        val = s->spurious_vec;
        break;
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
        val = s->isr[index & 7];
        break;
    case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
        val = s->tmr[index & 7];
        break;
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
        val = s->irr[index & 7];
        break;
    case 0x28:
        val = s->esr;
        break;
    case 0x30:
    case 0x31:
        val = s->icr[index & 1];
        break;
    case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
        val = s->lvt[index - 0x32];
        break;
    case 0x38:
        val = s->initial_count;
        break;
    case 0x39:
        val = apic_get_current_count(dev, s);
        break;
    case 0x3e:
        val = s->divide_conf;
        break;
    case 0x2f:
        /**
         * Correctable machine check exception vector, @todo: implement me!
         */
    default:
        AssertMsgFailed(("apic_mem_readl: unknown index %x\n", index));
        s->esr |= ESR_ILLEGAL_ADDRESS;
        val = 0;
        break;
    }
#ifdef DEBUG_APIC
    Log(("CPU%d: APIC read: %08x = %08x\n", s->phys_id, (uint32_t)addr, val));
#endif
    return val;
}

static int apic_mem_writel(APICDeviceInfo* dev, APICState *s, RTGCPHYS addr, uint32_t val)
{
    int rc = VINF_SUCCESS;
    int index;

#ifdef DEBUG_APIC
    Log(("CPU%d: APIC write: %08x = %08x\n", s->phys_id, (uint32_t)addr, val));
#endif

    index = (addr >> 4) & 0xff;

    switch(index) {
    case 0x02:
        s->id = (val >> 24);
        break;
    case 0x03:
        Log(("apic_mem_writel: write to version register; ignored\n"));
        break;
    case 0x08:
        apic_update_tpr(dev, s, val);
        break;
    case 0x09:
    case 0x0a:
        Log(("apic_mem_writel: write to read-only register %d ignored\n", index));
        break;
    case 0x0b: /* EOI */
        apic_eoi(dev, s);
        break;
    case 0x0d:
        s->log_dest = val >> 24;
        break;
    case 0x0e:
        s->dest_mode = val >> 28;
        break;
    case 0x0f:
        s->spurious_vec = val & 0x1ff;
        apic_update_irq(dev, s);
        break;
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
    case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x28:
        Log(("apic_mem_writel: write to read-only register %d ignored\n", index));
        break;

    case 0x30:
        s->icr[0] = val;
        rc = apic_deliver(dev, s, (s->icr[1] >> 24) & 0xff,
                          (s->icr[0] >> 11) & 1,
                          (s->icr[0] >> 8) & 7, (s->icr[0] & 0xff),
                          (s->icr[0] >> 14) & 1, (s->icr[0] >> 15) & 1);
        break;
    case 0x31:
        s->icr[1] = val;
        break;
    case 0x32 + APIC_LVT_TIMER:
        AssertCompile(APIC_LVT_TIMER == 0);
        apicTimerSetLvt(dev, s, val);
        break;
    case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
        {
            int n = index - 0x32;
            s->lvt[n] = val;
        }
        break;
    case 0x38:
        apicTimerSetInitialCount(dev, s, val);
        break;
    case 0x39:
        Log(("apic_mem_writel: write to read-only register %d ignored\n", index));
        break;
    case 0x3e:
        {
            int v;
            s->divide_conf = val & 0xb;
            v = (s->divide_conf & 3) | ((s->divide_conf >> 1) & 4);
            s->count_shift = (v + 1) & 7;
        }
        break;
    default:
        AssertMsgFailed(("apic_mem_writel: unknown index %x\n", index));
        s->esr |= ESR_ILLEGAL_ADDRESS;
        break;
    }
    return rc;
}

#ifdef IN_RING3

static void apic_save(SSMHANDLE* f, void *opaque)
{
    APICState *s = (APICState*)opaque;
    int i;

    SSMR3PutU32(f, s->apicbase);
    SSMR3PutU32(f, s->id);
    SSMR3PutU32(f, s->phys_id);
    SSMR3PutU32(f, s->arb_id);
    SSMR3PutU32(f, s->tpr);
    SSMR3PutU32(f, s->spurious_vec);
    SSMR3PutU8(f,  s->log_dest);
    SSMR3PutU8(f,  s->dest_mode);
    for (i = 0; i < 8; i++) {
        SSMR3PutU32(f, s->isr[i]);
        SSMR3PutU32(f, s->tmr[i]);
        SSMR3PutU32(f, s->irr[i]);
    }
    for (i = 0; i < APIC_LVT_NB; i++) {
        SSMR3PutU32(f, s->lvt[i]);
    }
    SSMR3PutU32(f, s->esr);
    SSMR3PutU32(f, s->icr[0]);
    SSMR3PutU32(f, s->icr[1]);
    SSMR3PutU32(f, s->divide_conf);
    SSMR3PutU32(f, s->count_shift);
    SSMR3PutU32(f, s->initial_count);
    SSMR3PutU64(f, s->initial_count_load_time);
    SSMR3PutU64(f, s->next_time);

    TMR3TimerSave(s->CTX_SUFF(pTimer), f);
}

static int apic_load(SSMHANDLE *f, void *opaque, int version_id)
{
    APICState *s = (APICState*)opaque;
    int i;

     /* XXX: what if the base changes? (registered memory regions) */
    SSMR3GetU32(f, &s->apicbase);

    switch (version_id)
    {
        case APIC_SAVED_STATE_VERSION_ANCIENT:
        {
            uint8_t val = 0;
            SSMR3GetU8(f, &val);
            s->id = val;
            /* UP only in old saved states */
            s->phys_id = 0;
            SSMR3GetU8(f, &val);
            s->arb_id = val;
            break;
        }
        case APIC_SAVED_STATE_VERSION:
        case APIC_SAVED_STATE_VERSION_VBOX_30:
            SSMR3GetU32(f, &s->id);
            SSMR3GetU32(f, &s->phys_id);
            SSMR3GetU32(f, &s->arb_id);
            break;
        default:
            return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }
    SSMR3GetU32(f, &s->tpr);
    SSMR3GetU32(f, &s->spurious_vec);
    SSMR3GetU8(f, &s->log_dest);
    SSMR3GetU8(f, &s->dest_mode);
    for (i = 0; i < 8; i++) {
        SSMR3GetU32(f, &s->isr[i]);
        SSMR3GetU32(f, &s->tmr[i]);
        SSMR3GetU32(f, &s->irr[i]);
    }
    for (i = 0; i < APIC_LVT_NB; i++) {
        SSMR3GetU32(f, &s->lvt[i]);
    }
    SSMR3GetU32(f, &s->esr);
    SSMR3GetU32(f, &s->icr[0]);
    SSMR3GetU32(f, &s->icr[1]);
    SSMR3GetU32(f, &s->divide_conf);
    SSMR3GetU32(f, (uint32_t *)&s->count_shift);
    SSMR3GetU32(f, (uint32_t *)&s->initial_count);
    SSMR3GetU64(f, (uint64_t *)&s->initial_count_load_time);
    SSMR3GetU64(f, (uint64_t *)&s->next_time);

    int rc = TMR3TimerLoad(s->CTX_SUFF(pTimer), f);
    s->uHintedCountShift = s->uHintedInitialCount = 0;
    s->fTimerArmed = TMTimerIsActive(s->CTX_SUFF(pTimer));
    if (s->fTimerArmed)
        apicDoFrequencyHinting(s);

    return VINF_SUCCESS; /** @todo darn mess! */
}
#endif /* IN_RING3 */

static void ioapic_service(IOAPICState *s)
{
    uint8_t i;
    uint8_t trig_mode;
    uint8_t vector;
    uint8_t delivery_mode;
    uint32_t mask;
    uint64_t entry;
    uint8_t dest;
    uint8_t dest_mode;
    uint8_t polarity;

    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        mask = 1 << i;
        if (s->irr & mask) {
            entry = s->ioredtbl[i];
            if (!(entry & APIC_LVT_MASKED)) {
                trig_mode = ((entry >> 15) & 1);
                dest = entry >> 56;
                dest_mode = (entry >> 11) & 1;
                delivery_mode = (entry >> 8) & 7;
                polarity = (entry >> 13) & 1;
                if (trig_mode == APIC_TRIGGER_EDGE)
                    s->irr &= ~mask;
                if (delivery_mode == APIC_DM_EXTINT)
                    /* malc: i'm still not so sure about ExtINT delivery */
                {
                    AssertMsgFailed(("Delivery mode ExtINT"));
                    vector = 0xff; /* incorrect but shuts up gcc. */
                }
                else
                    vector = entry & 0xff;

                int rc = s->CTX_SUFF(pIoApicHlp)->pfnApicBusDeliver(s->CTX_SUFF(pDevIns),
                                                           dest,
                                                           dest_mode,
                                                           delivery_mode,
                                                           vector,
                                                           polarity,
                                                           trig_mode);
                /* We must be sure that attempts to reschedule in R3
                   never get here */
                Assert(rc == VINF_SUCCESS);
            }
        }
    }
}


static void ioapic_set_irq(void *opaque, int vector, int level)
{
    IOAPICState *s = (IOAPICState*)opaque;

    if (vector >= 0 && vector < IOAPIC_NUM_PINS) {
        uint32_t mask = 1 << vector;
        uint64_t entry = s->ioredtbl[vector];

        if ((entry >> 15) & 1) {
            /* level triggered */
            if (level) {
                s->irr |= mask;
                ioapic_service(s);
                if ((level & PDM_IRQ_LEVEL_FLIP_FLOP) == PDM_IRQ_LEVEL_FLIP_FLOP) {
                    s->irr &= ~mask;
                }
            } else {
                s->irr &= ~mask;
            }
        } else {
            /* edge triggered */
            if (level) {
                s->irr |= mask;
                ioapic_service(s);
            }
        }
    }
}

static uint32_t ioapic_mem_readl(void *opaque, RTGCPHYS addr)
{
    IOAPICState *s = (IOAPICState*)opaque;
    int index;
    uint32_t val = 0;

    addr &= 0xff;
    if (addr == 0x00) {
        val = s->ioregsel;
    } else if (addr == 0x10) {
        switch (s->ioregsel) {
            case 0x00:
                val = s->id << 24;
                break;
            case 0x01:
                val = 0x11 | ((IOAPIC_NUM_PINS - 1) << 16); /* version 0x11 */
                break;
            case 0x02:
                val = 0;
                break;
            default:
                index = (s->ioregsel - 0x10) >> 1;
                if (index >= 0 && index < IOAPIC_NUM_PINS) {
                    if (s->ioregsel & 1)
                        val = s->ioredtbl[index] >> 32;
                    else
                        val = s->ioredtbl[index] & 0xffffffff;
                }
        }
#ifdef DEBUG_IOAPIC
        Log(("I/O APIC read: %08x = %08x\n", s->ioregsel, val));
#endif
    }
    return val;
}

static void ioapic_mem_writel(void *opaque, RTGCPHYS addr, uint32_t val)
{
    IOAPICState *s = (IOAPICState*)opaque;
    int index;

    addr &= 0xff;
    if (addr == 0x00)  {
        s->ioregsel = val;
        return;
    } else if (addr == 0x10) {
#ifdef DEBUG_IOAPIC
        Log(("I/O APIC write: %08x = %08x\n", s->ioregsel, val));
#endif
        switch (s->ioregsel) {
            case 0x00:
                s->id = (val >> 24) & 0xff;
                return;
            case 0x01:
            case 0x02:
                return;
            default:
                index = (s->ioregsel - 0x10) >> 1;
                if (index >= 0 && index < IOAPIC_NUM_PINS) {
                    if (s->ioregsel & 1) {
                        s->ioredtbl[index] &= 0xffffffff;
                        s->ioredtbl[index] |= (uint64_t)val << 32;
                    } else {
                        /* According to IOAPIC spec, vectors should be from 0x10 to 0xfe */
                        uint8_t vec = val & 0xff;
                        if ((val & APIC_LVT_MASKED) ||
                            ((vec >= 0x10) && (vec < 0xff)))
                        {
                            s->ioredtbl[index] &= ~0xffffffffULL;
                            s->ioredtbl[index] |= val;
                        }
                        else
                        {
                            /*
                             * Linux 2.6 kernels has pretty strange function
                             * unlock_ExtINT_logic() which writes
                             * absolutely bogus (all 0) value into the vector
                             * with pretty vague explanation why.
                             * So we just ignore such writes.
                             */
                            LogRel(("IOAPIC GUEST BUG: bad vector writing %x(sel=%x) to %d\n", val, s->ioregsel, index));
                        }
                    }
                    ioapic_service(s);
                }
        }
    }
}

#ifdef IN_RING3

static void ioapic_save(SSMHANDLE *f, void *opaque)
{
    IOAPICState *s = (IOAPICState*)opaque;
    int i;

    SSMR3PutU8(f, s->id);
    SSMR3PutU8(f, s->ioregsel);
    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        SSMR3PutU64(f, s->ioredtbl[i]);
    }
}

static int ioapic_load(SSMHANDLE *f, void *opaque, int version_id)
{
    IOAPICState *s = (IOAPICState*)opaque;
    int i;

    if (version_id != 1)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    SSMR3GetU8(f, &s->id);
    SSMR3GetU8(f, &s->ioregsel);
    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        SSMR3GetU64(f, &s->ioredtbl[i]);
    }
    return 0;
}

static void ioapic_reset(void *opaque)
{
    IOAPICState *s = (IOAPICState*)opaque;
    PPDMDEVINSR3        pDevIns    = s->pDevInsR3;
    PCPDMIOAPICHLPR3    pIoApicHlp = s->pIoApicHlpR3;
    int i;

    memset(s, 0, sizeof(*s));
    for(i = 0; i < IOAPIC_NUM_PINS; i++)
        s->ioredtbl[i] = 1 << 16; /* mask LVT */

    if (pDevIns)
    {
        s->pDevInsR3 = pDevIns;
        s->pDevInsRC = PDMDEVINS_2_RCPTR(pDevIns);
        s->pDevInsR0 = PDMDEVINS_2_R0PTR(pDevIns);
    }
    if (pIoApicHlp)
    {
        s->pIoApicHlpR3 = pIoApicHlp;
        s->pIoApicHlpRC = s->pIoApicHlpR3->pfnGetRCHelpers(pDevIns);
        s->pIoApicHlpR0 = s->pIoApicHlpR3->pfnGetR0Helpers(pDevIns);
    }
}

#endif /* IN_RING3 */

/* LAPIC */
PDMBOTHCBDECL(int) apicMMIORead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void *pv, unsigned cb)
{
    APICDeviceInfo *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    APICState *s = getLapic(dev);

    Log(("CPU%d: apicMMIORead at %llx\n", s->phys_id,  (uint64_t)GCPhysAddr));

    /** @todo: add LAPIC range validity checks (different LAPICs can theoretically have
               different physical addresses, see #3092) */

    STAM_COUNTER_INC(&CTXSUFF(dev->StatMMIORead));
    switch (cb)
    {
        case 1:
            *(uint8_t *)pv = 0;
            break;

        case 2:
            *(uint16_t *)pv = 0;
            break;

        case 4:
        {
#if 0 /** @note experimental */
#ifndef IN_RING3
            uint32_t index = (GCPhysAddr >> 4) & 0xff;

            if (    index == 0x08 /* TPR */
                &&  ++s->cTPRPatchAttempts < APIC_MAX_PATCH_ATTEMPTS)
            {
#ifdef IN_RC
                pDevIns->pDevHlpGC->pfnPATMSetMMIOPatchInfo(pDevIns, GCPhysAddr, &s->tpr);
#else
                RTGCPTR pDevInsGC = PDMINS2DATA_GCPTR(pDevIns);
                pDevIns->pHlpR0->pfnPATMSetMMIOPatchInfo(pDevIns, GCPhysAddr, pDevIns + RT_OFFSETOF(APICState, tpr));
#endif
                return VINF_PATM_HC_MMIO_PATCH_READ;
            }
#endif
#endif /* experimental */
            APIC_LOCK(dev, VINF_IOM_HC_MMIO_READ);
            *(uint32_t *)pv = apic_mem_readl(dev, s, GCPhysAddr);
            APIC_UNLOCK(dev);
            break;
        }
        default:
            AssertReleaseMsgFailed(("cb=%d\n", cb)); /* for now we assume simple accesses. */
            return VERR_INTERNAL_ERROR;
    }
    return VINF_SUCCESS;
}

PDMBOTHCBDECL(int) apicMMIOWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void *pv, unsigned cb)
{
    APICDeviceInfo *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    APICState *s = getLapic(dev);

    Log(("CPU%d: apicMMIOWrite at %llx\n", s->phys_id, (uint64_t)GCPhysAddr));

    /** @todo: add LAPIC range validity checks (multiple LAPICs can theoretically have
               different physical addresses, see #3092) */

    STAM_COUNTER_INC(&CTXSUFF(dev->StatMMIOWrite));
    switch (cb)
    {
        case 1:
        case 2:
            /* ignore */
            break;

        case 4:
        {
            int rc;
            APIC_LOCK(dev, VINF_IOM_HC_MMIO_WRITE);
            rc = apic_mem_writel(dev, s, GCPhysAddr, *(uint32_t *)pv);
            APIC_UNLOCK(dev);
            return rc;
        }

        default:
            AssertReleaseMsgFailed(("cb=%d\n", cb)); /* for now we assume simple accesses. */
            return VERR_INTERNAL_ERROR;
    }
    return VINF_SUCCESS;
}

#ifdef IN_RING3

/* Print a 8-dword LAPIC bit map (256 bits). */
static void lapicDumpVec(APICDeviceInfo  *dev, APICState *lapic, PCDBGFINFOHLP pHlp, unsigned start)
{
    unsigned    i;
    uint32_t    val;

    for (i = 0; i < 8; ++i)
    {
        val = apic_mem_readl(dev, lapic, start + (i << 4));
        pHlp->pfnPrintf(pHlp, "%08X", val);
    }
    pHlp->pfnPrintf(pHlp, "\n");
}

/* Print basic LAPIC state. */
static DECLCALLBACK(void) lapicInfoBasic(APICDeviceInfo  *dev, APICState *lapic, PCDBGFINFOHLP pHlp)
{
    uint32_t        val;
    unsigned        max_lvt;

    pHlp->pfnPrintf(pHlp, "Local APIC at %08X:\n", lapic->apicbase);
    val = apic_mem_readl(dev, lapic, 0x20);
    pHlp->pfnPrintf(pHlp, "  LAPIC ID  : %08X\n", val);
    pHlp->pfnPrintf(pHlp, "    APIC ID = %02X\n", (val >> 24) & 0xff);
    val = apic_mem_readl(dev, lapic, 0x30);
    max_lvt = (val >> 16) & 0xff;
    pHlp->pfnPrintf(pHlp, "  APIC VER   : %08X\n", val);
    pHlp->pfnPrintf(pHlp, "    version  = %02X\n", val & 0xff);
    pHlp->pfnPrintf(pHlp, "    lvts     = %d\n", ((val >> 16) & 0xff) + 1);
    val = apic_mem_readl(dev, lapic, 0x80);
    pHlp->pfnPrintf(pHlp, "  TPR        : %08X\n", val);
    pHlp->pfnPrintf(pHlp, "    task pri = %d/%d\n", (val >> 4) & 0xf, val & 0xf);
    val = apic_mem_readl(dev, lapic, 0xA0);
    pHlp->pfnPrintf(pHlp, "  PPR        : %08X\n", val);
    pHlp->pfnPrintf(pHlp, "    cpu pri  = %d/%d\n", (val >> 4) & 0xf, val & 0xf);
    val = apic_mem_readl(dev, lapic, 0xD0);
    pHlp->pfnPrintf(pHlp, "  LDR       : %08X\n", val);
    pHlp->pfnPrintf(pHlp, "    log id  = %02X\n", (val >> 24) & 0xff);
    val = apic_mem_readl(dev, lapic, 0xE0);
    pHlp->pfnPrintf(pHlp, "  DFR       : %08X\n", val);
    val = apic_mem_readl(dev, lapic, 0xF0);
    pHlp->pfnPrintf(pHlp, "  SVR       : %08X\n", val);
    pHlp->pfnPrintf(pHlp, "    focus   = %s\n", val & (1 << 9) ? "check off" : "check on");
    pHlp->pfnPrintf(pHlp, "    lapic   = %s\n", val & (1 << 8) ? "ENABLED" : "DISABLED");
    pHlp->pfnPrintf(pHlp, "    vector  = %02X\n", val & 0xff);
    pHlp->pfnPrintf(pHlp, "  ISR       : ");
    lapicDumpVec(dev, lapic, pHlp, 0x100);
    val = get_highest_priority_int(lapic->isr);
    pHlp->pfnPrintf(pHlp, "    highest = %02X\n", val == ~0U ? 0 : val);
    pHlp->pfnPrintf(pHlp, "  IRR       : ");
    lapicDumpVec(dev, lapic, pHlp, 0x200);
    val = get_highest_priority_int(lapic->irr);
    pHlp->pfnPrintf(pHlp, "    highest = %02X\n", val == ~0U ? 0 : val);
    val = apic_mem_readl(dev, lapic, 0x320);
}

/* Print the more interesting LAPIC LVT entries. */
static DECLCALLBACK(void) lapicInfoLVT(APICDeviceInfo  *dev, APICState *lapic, PCDBGFINFOHLP pHlp)
{
    uint32_t        val;
    static const char *dmodes[] = { "Fixed ", "Reserved", "SMI", "Reserved",
                                    "NMI", "INIT", "Reserved", "ExtINT" };

    val = apic_mem_readl(dev, lapic, 0x320);
    pHlp->pfnPrintf(pHlp, "  LVT Timer : %08X\n", val);
    pHlp->pfnPrintf(pHlp, "    mode    = %s\n", val & (1 << 17) ? "periodic" : "one-shot");
    pHlp->pfnPrintf(pHlp, "    mask    = %d\n", (val >> 16) & 1);
    pHlp->pfnPrintf(pHlp, "    status  = %s\n", val & (1 << 12) ? "pending" : "idle");
    pHlp->pfnPrintf(pHlp, "    vector  = %02X\n", val & 0xff);
    val = apic_mem_readl(dev, lapic, 0x350);
    pHlp->pfnPrintf(pHlp, "  LVT LINT0 : %08X\n", val);
    pHlp->pfnPrintf(pHlp, "    mask    = %d\n", (val >> 16) & 1);
    pHlp->pfnPrintf(pHlp, "    trigger = %s\n", val & (1 << 15) ? "level" : "edge");
    pHlp->pfnPrintf(pHlp, "    rem irr = %d\n", (val >> 14) & 1);
    pHlp->pfnPrintf(pHlp, "    polarty = %d\n", (val >> 13) & 1);
    pHlp->pfnPrintf(pHlp, "    status  = %s\n", val & (1 << 12) ? "pending" : "idle");
    pHlp->pfnPrintf(pHlp, "    delivry = %s\n", dmodes[(val >> 8) & 7]);
    pHlp->pfnPrintf(pHlp, "    vector  = %02X\n", val & 0xff);
    val = apic_mem_readl(dev, lapic, 0x360);
    pHlp->pfnPrintf(pHlp, "  LVT LINT1 : %08X\n", val);
    pHlp->pfnPrintf(pHlp, "    mask    = %d\n", (val >> 16) & 1);
    pHlp->pfnPrintf(pHlp, "    trigger = %s\n", val & (1 << 15) ? "level" : "edge");
    pHlp->pfnPrintf(pHlp, "    rem irr = %d\n", (val >> 14) & 1);
    pHlp->pfnPrintf(pHlp, "    polarty = %d\n", (val >> 13) & 1);
    pHlp->pfnPrintf(pHlp, "    status  = %s\n", val & (1 << 12) ? "pending" : "idle");
    pHlp->pfnPrintf(pHlp, "    delivry = %s\n", dmodes[(val >> 8) & 7]);
    pHlp->pfnPrintf(pHlp, "    vector  = %02X\n", val & 0xff);
}

/* Print LAPIC timer state. */
static DECLCALLBACK(void) lapicInfoTimer(APICDeviceInfo  *dev, APICState *lapic, PCDBGFINFOHLP pHlp)
{
    uint32_t        val;
    unsigned        divider;

    pHlp->pfnPrintf(pHlp, "Local APIC timer:\n");
    val = apic_mem_readl(dev, lapic, 0x380);
    pHlp->pfnPrintf(pHlp, "  Initial count : %08X\n", val);
    val = apic_mem_readl(dev, lapic, 0x390);
    pHlp->pfnPrintf(pHlp, "  Current count : %08X\n", val);
    val = apic_mem_readl(dev, lapic, 0x3E0);
    pHlp->pfnPrintf(pHlp, "  Divide config : %08X\n", val);
    divider = ((val >> 1) & 0x04) | (val & 0x03);
    pHlp->pfnPrintf(pHlp, "    divider     = %d\n", divider == 7 ? 1 : 2 << divider);
}

/**
 * Info handler, device version. Dumps Local APIC(s) state according to given argument.
 *
 * @param   pDevIns     Device instance which registered the info.
 * @param   pHlp        Callback functions for doing output.
 * @param   pszArgs     Argument string. Optional.
 */
static DECLCALLBACK(void) lapicInfo(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    APICDeviceInfo  *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    APICState       *lapic;

    lapic = getLapic(dev);

    if (pszArgs == NULL || !strcmp(pszArgs, "basic"))
    {
        lapicInfoBasic(dev, lapic, pHlp);
    }
    else if (!strcmp(pszArgs, "lvt"))
    {
        lapicInfoLVT(dev, lapic, pHlp);
    }
    else if (!strcmp(pszArgs, "timer"))
    {
        lapicInfoTimer(dev, lapic, pHlp);
    }
    else
    {
        pHlp->pfnPrintf(pHlp, "Invalid argument. Recognized arguments are 'basic', 'lvt', 'timer'.\n");
    }
}

/**
 * @copydoc FNSSMDEVLIVEEXEC
 */
static DECLCALLBACK(int) apicLiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    APICDeviceInfo *pThis = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);

    SSMR3PutU32( pSSM, pThis->cCpus);
    SSMR3PutBool(pSSM, pThis->fIoApic);
    SSMR3PutU32( pSSM, pThis->enmVersion);
    AssertCompile(PDMAPICVERSION_APIC == 2);

    return VINF_SSM_DONT_CALL_AGAIN;
}

/**
 * @copydoc FNSSMDEVSAVEEXEC
 */
static DECLCALLBACK(int) apicSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    APICDeviceInfo *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);

    /* config */
    apicLiveExec(pDevIns, pSSM, SSM_PASS_FINAL);

    /* save all APICs data, @todo: is it correct? */
    foreach_apic(dev, 0xffffffff, apic_save(pSSM, apic));

    return VINF_SUCCESS;
}

/**
 * @copydoc FNSSMDEVLOADEXEC
 */
static DECLCALLBACK(int) apicLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    APICDeviceInfo *pThis = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);

    if (    uVersion != APIC_SAVED_STATE_VERSION
        &&  uVersion != APIC_SAVED_STATE_VERSION_VBOX_30
        &&  uVersion != APIC_SAVED_STATE_VERSION_ANCIENT)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* config */
    if (uVersion > APIC_SAVED_STATE_VERSION_VBOX_30) {
        uint32_t cCpus;
        int rc = SSMR3GetU32(pSSM, &cCpus); AssertRCReturn(rc, rc);
        if (cCpus != pThis->cCpus)
            return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - cCpus: saved=%#x config=%#x"), cCpus, pThis->cCpus);
        bool fIoApic;
        rc = SSMR3GetBool(pSSM, &fIoApic); AssertRCReturn(rc, rc);
        if (fIoApic != pThis->fIoApic)
            return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - fIoApic: saved=%RTbool config=%RTbool"), fIoApic, pThis->fIoApic);
        uint32_t uApicVersion;
        rc = SSMR3GetU32(pSSM, &uApicVersion); AssertRCReturn(rc, rc);
        if (uApicVersion != (uint32_t)pThis->enmVersion)
            return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - uApicVersion: saved=%#x config=%#x"), uApicVersion, pThis->enmVersion);
    }

    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;

    /* load all APICs data */ /** @todo: is it correct? */
    APIC_LOCK(pThis, VERR_INTERNAL_ERROR_3);
    foreach_apic(pThis, 0xffffffff,
                 if (apic_load(pSSM, apic, uVersion)) {
                      AssertFailed();
                      APIC_UNLOCK(pThis);
                      return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
                 }
                 );
    APIC_UNLOCK(pThis);
    return VINF_SUCCESS;
}

/**
 * @copydoc FNPDMDEVRESET
 */
static DECLCALLBACK(void) apicReset(PPDMDEVINS pDevIns)
{
    APICDeviceInfo *dev = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    unsigned i;

    APIC_LOCK_VOID(dev, VERR_INTERNAL_ERROR);

    /* Reset all APICs. */
    for (i = 0; i < dev->cCpus; i++) {
        APICState *pApic = &dev->CTX_SUFF(paLapics)[i];
        TMTimerStop(pApic->CTX_SUFF(pTimer));

        /* Clear LAPIC state as if an INIT IPI was sent. */
        apic_init_ipi(dev, pApic);
        /* The IDs are not touched by apic_init_ipi() and must be reset now. */
        pApic->arb_id = pApic->id = i;
        Assert(pApic->id == pApic->phys_id);    /* The two should match again. */
        /* Reset should re-enable the APIC, see comment in msi.h */
        pApic->apicbase = VBOX_MSI_ADDR_BASE | MSR_IA32_APICBASE_ENABLE;
        if (pApic->phys_id == 0)
            pApic->apicbase |= MSR_IA32_APICBASE_BSP;

        /* Clear any pending APIC interrupt action flag. */
        cpuClearInterrupt(dev, pApic);
    }
    /** @todo r=bird: Why is this done everytime, while the constructor first
     *        checks the CPUID?  Who is right? */
    dev->pApicHlpR3->pfnChangeFeature(dev->pDevInsR3, dev->enmVersion);

    APIC_UNLOCK(dev);
}

/**
 * @copydoc FNPDMDEVRELOCATE
 */
static DECLCALLBACK(void) apicRelocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    APICDeviceInfo *pThis = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    pThis->pDevInsRC   = PDMDEVINS_2_RCPTR(pDevIns);
    pThis->pApicHlpRC  = pThis->pApicHlpR3->pfnGetRCHelpers(pDevIns);
    pThis->paLapicsRC  = MMHyperR3ToRC(PDMDevHlpGetVM(pDevIns), pThis->paLapicsR3);
    pThis->pCritSectRC = pThis->pApicHlpR3->pfnGetRCCritSect(pDevIns);
    for (uint32_t i = 0; i < pThis->cCpus; i++)
        pThis->paLapicsR3[i].pTimerRC = TMTimerRCPtr(pThis->paLapicsR3[i].pTimerR3);
}

DECLINLINE(void) initApicData(APICState* apic, uint8_t id)
{
    int i;
    memset(apic, 0, sizeof(*apic));

    /* See comment in msi.h for LAPIC base info */
    apic->apicbase = VBOX_MSI_ADDR_BASE | MSR_IA32_APICBASE_ENABLE;
    /* Mark first CPU as BSP */
    if (id == 0)
        apic->apicbase |= MSR_IA32_APICBASE_BSP;
    for (i = 0; i < APIC_LVT_NB; i++)
        apic->lvt[i] = 1 << 16; /* mask LVT */
    apic->spurious_vec = 0xff;
    apic->phys_id = apic->id = id;
}

/**
 * @copydoc FNPDMDEVCONSTRUCT
 */
static DECLCALLBACK(int) apicConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMAPICREG      ApicReg;
    int             rc;
    uint32_t        i;
    bool            fIoApic;
    bool            fGCEnabled;
    bool            fR0Enabled;
    APICDeviceInfo  *pThis = PDMINS_2_DATA(pDevIns, APICDeviceInfo *);
    uint32_t        cCpus;

    /*
     * Only single device instance.
     */
    Assert(iInstance == 0);

    /*
     * Validate configuration.
     */
    if (!CFGMR3AreValuesValid(pCfg,
                              "IOAPIC\0"
                              "GCEnabled\0"
                              "R0Enabled\0"
                              "NumCPUs\0"))
        return VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES;

    rc = CFGMR3QueryBoolDef(pCfg, "IOAPIC", &fIoApic, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to read \"IOAPIC\""));

    rc = CFGMR3QueryBoolDef(pCfg, "GCEnabled", &fGCEnabled, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to query boolean value \"GCEnabled\""));

    rc = CFGMR3QueryBoolDef(pCfg, "R0Enabled", &fR0Enabled, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to query boolean value \"R0Enabled\""));

    rc = CFGMR3QueryU32Def(pCfg, "NumCPUs", &cCpus, 1);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to query integer value \"NumCPUs\""));

    Log(("APIC: cCpus=%d fR0Enabled=%RTbool fGCEnabled=%RTbool fIoApic=%RTbool\n", cCpus, fR0Enabled, fGCEnabled, fIoApic));

    /** @todo Current implementation is limited to 32 CPUs due to the use of 32
     *        bits bitmasks. */
    if (cCpus > 32)
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Invalid value for \"NumCPUs\""));

    /*
     * Init the data.
     */
    pThis->pDevInsR3  = pDevIns;
    pThis->pDevInsR0  = PDMDEVINS_2_R0PTR(pDevIns);
    pThis->pDevInsRC  = PDMDEVINS_2_RCPTR(pDevIns);
    pThis->cCpus      = cCpus;
    pThis->fIoApic    = fIoApic;
    /* Use PDMAPICVERSION_X2APIC to activate x2APIC mode */
    pThis->enmVersion = PDMAPICVERSION_APIC;

    PVM pVM = PDMDevHlpGetVM(pDevIns);
    /*
     * We are not freeing this memory, as it's automatically released when guest exits.
     */
    rc = MMHyperAlloc(pVM, cCpus * sizeof(APICState), 1, MM_TAG_PDM_DEVICE_USER, (void **)&pThis->paLapicsR3);
    if (RT_FAILURE(rc))
        return VERR_NO_MEMORY;
    pThis->paLapicsR0 = MMHyperR3ToR0(pVM, pThis->paLapicsR3);
    pThis->paLapicsRC = MMHyperR3ToRC(pVM, pThis->paLapicsR3);

    for (i = 0; i < cCpus; i++)
        initApicData(&pThis->paLapicsR3[i], i);

    /*
     * Register the APIC.
     */
    ApicReg.u32Version              = PDM_APICREG_VERSION;
    ApicReg.pfnGetInterruptR3       = apicGetInterrupt;
    ApicReg.pfnHasPendingIrqR3      = apicHasPendingIrq;
    ApicReg.pfnSetBaseR3            = apicSetBase;
    ApicReg.pfnGetBaseR3            = apicGetBase;
    ApicReg.pfnSetTPRR3             = apicSetTPR;
    ApicReg.pfnGetTPRR3             = apicGetTPR;
    ApicReg.pfnWriteMSRR3           = apicWriteMSR;
    ApicReg.pfnReadMSRR3            = apicReadMSR;
    ApicReg.pfnBusDeliverR3         = apicBusDeliverCallback;
    ApicReg.pfnLocalInterruptR3     = apicLocalInterrupt;
    if (fGCEnabled) {
        ApicReg.pszGetInterruptRC   = "apicGetInterrupt";
        ApicReg.pszHasPendingIrqRC  = "apicHasPendingIrq";
        ApicReg.pszSetBaseRC        = "apicSetBase";
        ApicReg.pszGetBaseRC        = "apicGetBase";
        ApicReg.pszSetTPRRC         = "apicSetTPR";
        ApicReg.pszGetTPRRC         = "apicGetTPR";
        ApicReg.pszWriteMSRRC       = "apicWriteMSR";
        ApicReg.pszReadMSRRC        = "apicReadMSR";
        ApicReg.pszBusDeliverRC     = "apicBusDeliverCallback";
        ApicReg.pszLocalInterruptRC = "apicLocalInterrupt";
    } else {
        ApicReg.pszGetInterruptRC   = NULL;
        ApicReg.pszHasPendingIrqRC  = NULL;
        ApicReg.pszSetBaseRC        = NULL;
        ApicReg.pszGetBaseRC        = NULL;
        ApicReg.pszSetTPRRC         = NULL;
        ApicReg.pszGetTPRRC         = NULL;
        ApicReg.pszWriteMSRRC       = NULL;
        ApicReg.pszReadMSRRC        = NULL;
        ApicReg.pszBusDeliverRC     = NULL;
        ApicReg.pszLocalInterruptRC = NULL;
    }
    if (fR0Enabled) {
        ApicReg.pszGetInterruptR0   = "apicGetInterrupt";
        ApicReg.pszHasPendingIrqR0  = "apicHasPendingIrq";
        ApicReg.pszSetBaseR0        = "apicSetBase";
        ApicReg.pszGetBaseR0        = "apicGetBase";
        ApicReg.pszSetTPRR0         = "apicSetTPR";
        ApicReg.pszGetTPRR0         = "apicGetTPR";
        ApicReg.pszWriteMSRR0       = "apicWriteMSR";
        ApicReg.pszReadMSRR0        = "apicReadMSR";
        ApicReg.pszBusDeliverR0     = "apicBusDeliverCallback";
        ApicReg.pszLocalInterruptR0 = "apicLocalInterrupt";
    } else {
        ApicReg.pszGetInterruptR0   = NULL;
        ApicReg.pszHasPendingIrqR0  = NULL;
        ApicReg.pszSetBaseR0        = NULL;
        ApicReg.pszGetBaseR0        = NULL;
        ApicReg.pszSetTPRR0         = NULL;
        ApicReg.pszGetTPRR0         = NULL;
        ApicReg.pszWriteMSRR0       = NULL;
        ApicReg.pszReadMSRR0        = NULL;
        ApicReg.pszBusDeliverR0     = NULL;
        ApicReg.pszLocalInterruptR0 = NULL;
    }

    rc = PDMDevHlpAPICRegister(pDevIns, &ApicReg, &pThis->pApicHlpR3);
    AssertLogRelRCReturn(rc, rc);
    pThis->pCritSectR3 = pThis->pApicHlpR3->pfnGetR3CritSect(pDevIns);

    /*
     * The the CPUID feature bit.
     */
    /** @todo r=bird: See remark in the apicReset. */
    uint32_t u32Eax, u32Ebx, u32Ecx, u32Edx;
    PDMDevHlpGetCpuId(pDevIns, 0, &u32Eax, &u32Ebx, &u32Ecx, &u32Edx);
    if (u32Eax >= 1) {
        if (   fIoApic                       /* If IOAPIC is enabled, enable Local APIC in any case */
            || (   u32Ebx == X86_CPUID_VENDOR_INTEL_EBX
                && u32Ecx == X86_CPUID_VENDOR_INTEL_ECX
                && u32Edx == X86_CPUID_VENDOR_INTEL_EDX /* GenuineIntel */)
            || (   u32Ebx == X86_CPUID_VENDOR_AMD_EBX
                && u32Ecx == X86_CPUID_VENDOR_AMD_ECX
                && u32Edx == X86_CPUID_VENDOR_AMD_EDX   /* AuthenticAMD */)) {
            LogRel(("Activating Local APIC\n"));
            pThis->pApicHlpR3->pfnChangeFeature(pDevIns, pThis->enmVersion);
        }
    }

    /*
     * Register the MMIO range.
     * @todo: shall reregister, if base changes.
     */
    uint32_t ApicBase = pThis->paLapicsR3[0].apicbase & ~0xfff;
    rc = PDMDevHlpMMIORegister(pDevIns, ApicBase, 0x1000, pThis,
                               apicMMIOWrite, apicMMIORead, NULL, "APIC Memory");
    if (RT_FAILURE(rc))
        return rc;

    if (fGCEnabled) {
        pThis->pApicHlpRC  = pThis->pApicHlpR3->pfnGetRCHelpers(pDevIns);
        pThis->pCritSectRC = pThis->pApicHlpR3->pfnGetRCCritSect(pDevIns);

        rc = PDMDevHlpMMIORegisterRC(pDevIns, ApicBase, 0x1000, 0,
                                     "apicMMIOWrite", "apicMMIORead", NULL);
        if (RT_FAILURE(rc))
            return rc;
    }

    if (fR0Enabled) {
        pThis->pApicHlpR0  = pThis->pApicHlpR3->pfnGetR0Helpers(pDevIns);
        pThis->pCritSectR0 = pThis->pApicHlpR3->pfnGetR0CritSect(pDevIns);

        rc = PDMDevHlpMMIORegisterR0(pDevIns, ApicBase, 0x1000, 0,
                                     "apicMMIOWrite", "apicMMIORead", NULL);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Create the APIC timers.
     */
    for (i = 0; i < cCpus; i++) {
        APICState *pApic = &pThis->paLapicsR3[i];
        pApic->pszDesc = MMR3HeapAPrintf(pVM, MM_TAG_PDM_DEVICE_USER, "APIC Timer #%u", i);
        rc = PDMDevHlpTMTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, apicTimerCallback, pApic,
                                    TMTIMER_FLAGS_NO_CRIT_SECT, pApic->pszDesc, &pApic->pTimerR3);
        if (RT_FAILURE(rc))
            return rc;
        pApic->pTimerR0 = TMTimerR0Ptr(pApic->pTimerR3);
        pApic->pTimerRC = TMTimerRCPtr(pApic->pTimerR3);
        TMR3TimerSetCritSect(pApic->pTimerR3, pThis->pCritSectR3);
    }

    /*
     * Saved state.
     */
    rc = PDMDevHlpSSMRegister3(pDevIns, APIC_SAVED_STATE_VERSION, sizeof(*pThis),
                               apicLiveExec, apicSaveExec, apicLoadExec);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register debugger info callback.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "lapic", "Display Local APIC state for current CPU. "
                              "Recognizes 'basic', 'lvt', 'timer' as arguments, defaulting to 'basic'.", lapicInfo);

#ifdef VBOX_WITH_STATISTICS
    /*
     * Statistics.
     */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMMIOReadGC,     STAMTYPE_COUNTER,  "/Devices/APIC/MMIOReadGC",   STAMUNIT_OCCURENCES, "Number of APIC MMIO reads in GC.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMMIOReadHC,     STAMTYPE_COUNTER,  "/Devices/APIC/MMIOReadHC",   STAMUNIT_OCCURENCES, "Number of APIC MMIO reads in HC.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMMIOWriteGC,    STAMTYPE_COUNTER,  "/Devices/APIC/MMIOWriteGC",  STAMUNIT_OCCURENCES, "Number of APIC MMIO writes in GC.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMMIOWriteHC,    STAMTYPE_COUNTER,  "/Devices/APIC/MMIOWriteHC",  STAMUNIT_OCCURENCES, "Number of APIC MMIO writes in HC.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatClearedActiveIrq,STAMTYPE_COUNTER, "/Devices/APIC/MaskedActiveIRQ", STAMUNIT_OCCURENCES, "Number of cleared irqs.");
    for (i = 0; i < cCpus; i++) {
        APICState *pApic = &pThis->paLapicsR3[i];
        PDMDevHlpSTAMRegisterF(pDevIns, &pApic->StatTimerSetInitialCount,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Calls to apicTimerSetInitialCount.",   "/Devices/APIC/%u/TimerSetInitialCount", i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pApic->StatTimerSetInitialCountArm,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "TMTimerSetRelative calls.",            "/Devices/APIC/%u/TimerSetInitialCount/Arm", i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pApic->StatTimerSetInitialCountDisarm, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "TMTimerStop calls.",                   "/Devices/APIC/%u/TimerSetInitialCount/Disasm", i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pApic->StatTimerSetLvt,                STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Calls to apicTimerSetLvt.",            "/Devices/APIC/%u/TimerSetLvt", i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pApic->StatTimerSetLvtClearPeriodic,   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "Clearing APIC_LVT_TIMER_PERIODIC.",    "/Devices/APIC/%u/TimerSetLvt/ClearPeriodic", i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pApic->StatTimerSetLvtPostponed,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "TMTimerStop postponed.",               "/Devices/APIC/%u/TimerSetLvt/Postponed", i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pApic->StatTimerSetLvtArmed,           STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "TMTimerSet avoided.",                  "/Devices/APIC/%u/TimerSetLvt/Armed", i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pApic->StatTimerSetLvtArm,             STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "TMTimerSet necessary.",                "/Devices/APIC/%u/TimerSetLvt/Arm", i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pApic->StatTimerSetLvtArmRetries,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "TMTimerSet retries.",                  "/Devices/APIC/%u/TimerSetLvt/ArmRetries", i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pApic->StatTimerSetLvtNoRelevantChange,STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, "No relevant flags changed.",           "/Devices/APIC/%u/TimerSetLvt/NoRelevantChange", i);
    }
#endif

    return VINF_SUCCESS;
}


/**
 * APIC device registration structure.
 */
const PDMDEVREG g_DeviceAPIC =
{
    /* u32Version */
    PDM_DEVREG_VERSION,
    /* szName */
    "apic",
    /* szRCMod */
    "VBoxDD2GC.gc",
    /* szR0Mod */
    "VBoxDD2R0.r0",
    /* pszDescription */
    "Advanced Programmable Interrupt Controller (APIC) Device",
    /* fFlags */
    PDM_DEVREG_FLAGS_HOST_BITS_DEFAULT | PDM_DEVREG_FLAGS_GUEST_BITS_32_64 | PDM_DEVREG_FLAGS_PAE36 | PDM_DEVREG_FLAGS_RC | PDM_DEVREG_FLAGS_R0,
    /* fClass */
    PDM_DEVREG_CLASS_PIC,
    /* cMaxInstances */
    1,
    /* cbInstance */
    sizeof(APICState),
    /* pfnConstruct */
    apicConstruct,
    /* pfnDestruct */
    NULL,
    /* pfnRelocate */
    apicRelocate,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    apicReset,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnQueryInterface. */
    NULL,
    /* pfnInitComplete */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32VersionEnd */
    PDM_DEVREG_VERSION
};

#endif /* IN_RING3 */


/* IOAPIC */

PDMBOTHCBDECL(int) ioapicMMIORead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void *pv, unsigned cb)
{
    IOAPICState *s = PDMINS_2_DATA(pDevIns, IOAPICState *);
    IOAPIC_LOCK(s, VINF_IOM_HC_MMIO_READ);

    STAM_COUNTER_INC(&CTXSUFF(s->StatMMIORead));
    switch (cb) {
    case 1:
        *(uint8_t *)pv = ioapic_mem_readl(s, GCPhysAddr);
        break;

    case 2:
        *(uint16_t *)pv = ioapic_mem_readl(s, GCPhysAddr);
        break;

    case 4:
        *(uint32_t *)pv = ioapic_mem_readl(s, GCPhysAddr);
        break;

    default:
        AssertReleaseMsgFailed(("cb=%d\n", cb)); /* for now we assume simple accesses. */
        IOAPIC_UNLOCK(s);
        return VERR_INTERNAL_ERROR;
    }
    IOAPIC_UNLOCK(s);
    return VINF_SUCCESS;
}

PDMBOTHCBDECL(int) ioapicMMIOWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void *pv, unsigned cb)
{
    IOAPICState *s = PDMINS_2_DATA(pDevIns, IOAPICState *);

    STAM_COUNTER_INC(&CTXSUFF(s->StatMMIOWrite));
    switch (cb) {
    case 1:
    case 2:
    case 4:
        IOAPIC_LOCK(s, VINF_IOM_HC_MMIO_WRITE);
        ioapic_mem_writel(s, GCPhysAddr, *(uint32_t *)pv);
        IOAPIC_UNLOCK(s);
        break;

    default:
        AssertReleaseMsgFailed(("cb=%d\n", cb)); /* for now we assume simple accesses. */
        return VERR_INTERNAL_ERROR;
    }
    return VINF_SUCCESS;
}

PDMBOTHCBDECL(void) ioapicSetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel)
{
    /* PDM lock is taken here; @todo add assertion */
    IOAPICState *pThis = PDMINS_2_DATA(pDevIns, IOAPICState *);
    STAM_COUNTER_INC(&pThis->CTXSUFF(StatSetIrq));
    LogFlow(("ioapicSetIrq: iIrq=%d iLevel=%d\n", iIrq, iLevel));
    ioapic_set_irq(pThis, iIrq, iLevel);
}

PDMBOTHCBDECL(void) ioapicSendMsi(PPDMDEVINS pDevIns, RTGCPHYS GCAddr, uint32_t uValue)
{
    IOAPICState *pThis = PDMINS_2_DATA(pDevIns, IOAPICState *);

    LogFlow(("ioapicSendMsi: Address=%p uValue=%\n", GCAddr, uValue));

    uint8_t  dest = (GCAddr & VBOX_MSI_ADDR_DEST_ID_MASK) >> VBOX_MSI_ADDR_DEST_ID_SHIFT;
    uint8_t  vector_num = (uValue & VBOX_MSI_DATA_VECTOR_MASK) >> VBOX_MSI_DATA_VECTOR_SHIFT;
    uint8_t  dest_mode = (GCAddr >> VBOX_MSI_ADDR_DEST_MODE_SHIFT) & 0x1;
    uint8_t  trigger_mode = (uValue >> VBOX_MSI_DATA_TRIGGER_SHIFT) & 0x1;
    uint8_t  delivery_mode = (uValue >> VBOX_MSI_DATA_DELIVERY_MODE_SHIFT) & 0x7;
    /**
     * This bit indicates whether the message should be directed to the
     * processor with the lowest interrupt priority among
     * processors that can receive the interrupt, ignored ATM.
     */
    uint8_t  redir_hint = (GCAddr >> VBOX_MSI_ADDR_REDIRECTION_SHIFT) & 0x1;

    int rc = pThis->CTX_SUFF(pIoApicHlp)->pfnApicBusDeliver(pDevIns,
                                                            dest,
                                                            dest_mode,
                                                            delivery_mode,
                                                            vector_num,
                                                            0 /* polarity, n/a */,
                                                            trigger_mode);
    /* We must be sure that attempts to reschedule in R3
       never get here */
    Assert(rc == VINF_SUCCESS);
}

#ifdef IN_RING3

/**
 * Info handler, device version. Dumps I/O APIC state.
 *
 * @param   pDevIns     Device instance which registered the info.
 * @param   pHlp        Callback functions for doing output.
 * @param   pszArgs     Argument string. Optional and specific to the handler.
 */
static DECLCALLBACK(void) ioapicInfo(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    IOAPICState *s = PDMINS_2_DATA(pDevIns, IOAPICState *);
    uint32_t    val;
    unsigned    i;
    unsigned    max_redir;

    pHlp->pfnPrintf(pHlp, "I/O APIC at %08X:\n", 0xfec00000);
    val = s->id << 24;  /* Would be nice to call ioapic_mem_readl() directly, but that's not so simple. */
    pHlp->pfnPrintf(pHlp, "  IOAPICID  : %08X\n", val);
    pHlp->pfnPrintf(pHlp, "    APIC ID = %02X\n", (val >> 24) & 0xff);
    val = 0x11 | ((IOAPIC_NUM_PINS - 1) << 16);
    max_redir = (val >> 16) & 0xff;
    pHlp->pfnPrintf(pHlp, "  IOAPICVER : %08X\n", val);
    pHlp->pfnPrintf(pHlp, "    version = %02X\n", val & 0xff);
    pHlp->pfnPrintf(pHlp, "    redirs  = %d\n", ((val >> 16) & 0xff) + 1);
    val = 0;
    pHlp->pfnPrintf(pHlp, "  IOAPICARB : %08X\n", val);
    pHlp->pfnPrintf(pHlp, "    arb ID  = %02X\n", (val >> 24) & 0xff);
    Assert(sizeof(s->ioredtbl) / sizeof(s->ioredtbl[0]) > max_redir);
    pHlp->pfnPrintf(pHlp, "I/O redirection table\n");
    pHlp->pfnPrintf(pHlp, " idx dst_mode dst_addr mask trigger rirr polarity dlvr_st dlvr_mode vector\n");
    for (i = 0; i <= max_redir; ++i)
    {
        static const char *dmodes[] = { "Fixed ", "LowPri", "SMI   ", "Resrvd",
                                        "NMI   ", "INIT  ", "Resrvd", "ExtINT" };

        pHlp->pfnPrintf(pHlp, "  %02d   %s      %02X     %d    %s   %d   %s  %s     %s   %3d (%016llX)\n",
                        i,
                        s->ioredtbl[i] & (1 << 11) ? "log " : "phys",           /* dest mode */
                        (int)(s->ioredtbl[i] >> 56),                            /* dest addr */
                        (int)(s->ioredtbl[i] >> 16) & 1,                        /* mask */
                        s->ioredtbl[i] & (1 << 15) ? "level" : "edge ",         /* trigger */
                        (int)(s->ioredtbl[i] >> 14) & 1,                        /* remote IRR */
                        s->ioredtbl[i] & (1 << 13) ? "activelo" : "activehi",   /* polarity */
                        s->ioredtbl[i] & (1 << 12) ? "pend" : "idle",           /* delivery status */
                        dmodes[(s->ioredtbl[i] >> 8) & 0x07],                   /* delivery mode */
                        (int)s->ioredtbl[i] & 0xff,                             /* vector */
                        s->ioredtbl[i]                                          /* entire register */
                        );
    }
}

/**
 * @copydoc FNSSMDEVSAVEEXEC
 */
static DECLCALLBACK(int) ioapicSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    IOAPICState *s = PDMINS_2_DATA(pDevIns, IOAPICState *);
    ioapic_save(pSSM, s);
    return VINF_SUCCESS;
}

/**
 * @copydoc FNSSMDEVLOADEXEC
 */
static DECLCALLBACK(int) ioapicLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    IOAPICState *s = PDMINS_2_DATA(pDevIns, IOAPICState *);

    if (ioapic_load(pSSM, s, uVersion)) {
        AssertFailed();
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    return VINF_SUCCESS;
}

/**
 * @copydoc FNPDMDEVRESET
 */
static DECLCALLBACK(void) ioapicReset(PPDMDEVINS pDevIns)
{
    IOAPICState *s = PDMINS_2_DATA(pDevIns, IOAPICState *);
    s->pIoApicHlpR3->pfnLock(pDevIns, VERR_INTERNAL_ERROR);
    ioapic_reset(s);
    IOAPIC_UNLOCK(s);
}

/**
 * @copydoc FNPDMDEVRELOCATE
 */
static DECLCALLBACK(void) ioapicRelocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    IOAPICState *s = PDMINS_2_DATA(pDevIns, IOAPICState *);
    s->pDevInsRC    = PDMDEVINS_2_RCPTR(pDevIns);
    s->pIoApicHlpRC = s->pIoApicHlpR3->pfnGetRCHelpers(pDevIns);
}

/**
 * @copydoc FNPDMDEVCONSTRUCT
 */
static DECLCALLBACK(int) ioapicConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    IOAPICState *s = PDMINS_2_DATA(pDevIns, IOAPICState *);
    PDMIOAPICREG IoApicReg;
    bool         fGCEnabled;
    bool         fR0Enabled;
    int          rc;

    Assert(iInstance == 0);

    /*
     * Validate and read the configuration.
     */
    if (!CFGMR3AreValuesValid(pCfg, "GCEnabled\0" "R0Enabled\0"))
        return VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES;

    rc = CFGMR3QueryBoolDef(pCfg, "GCEnabled", &fGCEnabled, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to query boolean value \"GCEnabled\""));

    rc = CFGMR3QueryBoolDef(pCfg, "R0Enabled", &fR0Enabled, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to query boolean value \"R0Enabled\""));
    Log(("IOAPIC: fR0Enabled=%RTbool fGCEnabled=%RTbool\n", fR0Enabled, fGCEnabled));

    /*
     * Initialize the state data.
     */

    s->pDevInsR3 = pDevIns;
    s->pDevInsR0 = PDMDEVINS_2_R0PTR(pDevIns);
    s->pDevInsRC = PDMDEVINS_2_RCPTR(pDevIns);
    ioapic_reset(s);
    s->id = 0;

    /*
     * Register the IOAPIC and get helpers.
     */
    IoApicReg.u32Version  = PDM_IOAPICREG_VERSION;
    IoApicReg.pfnSetIrqR3 = ioapicSetIrq;
    IoApicReg.pszSetIrqRC = fGCEnabled ? "ioapicSetIrq" : NULL;
    IoApicReg.pszSetIrqR0 = fR0Enabled ? "ioapicSetIrq" : NULL;
    IoApicReg.pfnSendMsiR3 = ioapicSendMsi;
    IoApicReg.pszSendMsiRC = fGCEnabled ? "ioapicSendMsi" : NULL;
    IoApicReg.pszSendMsiR0 = fR0Enabled ? "ioapicSendMsi" : NULL;

    rc = PDMDevHlpIOAPICRegister(pDevIns, &IoApicReg, &s->pIoApicHlpR3);
    if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("IOAPICRegister -> %Rrc\n", rc));
        return rc;
    }

    /*
     * Register MMIO callbacks and saved state.
     */
    rc = PDMDevHlpMMIORegister(pDevIns, 0xfec00000, 0x1000, s,
                               ioapicMMIOWrite, ioapicMMIORead, NULL, "I/O APIC Memory");
    if (RT_FAILURE(rc))
        return rc;

    if (fGCEnabled) {
        s->pIoApicHlpRC = s->pIoApicHlpR3->pfnGetRCHelpers(pDevIns);

        rc = PDMDevHlpMMIORegisterRC(pDevIns, 0xfec00000, 0x1000, 0,
                                     "ioapicMMIOWrite", "ioapicMMIORead", NULL);
        if (RT_FAILURE(rc))
            return rc;
    }

    if (fR0Enabled) {
        s->pIoApicHlpR0 = s->pIoApicHlpR3->pfnGetR0Helpers(pDevIns);

        rc = PDMDevHlpMMIORegisterR0(pDevIns, 0xfec00000, 0x1000, 0,
                                     "ioapicMMIOWrite", "ioapicMMIORead", NULL);
        if (RT_FAILURE(rc))
            return rc;
    }

    rc = PDMDevHlpSSMRegister(pDevIns, 1 /* version */, sizeof(*s), ioapicSaveExec, ioapicLoadExec);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register debugger info callback.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "ioapic", "Display I/O APIC state.", ioapicInfo);

#ifdef VBOX_WITH_STATISTICS
    /*
     * Statistics.
     */
    PDMDevHlpSTAMRegister(pDevIns, &s->StatMMIOReadGC,     STAMTYPE_COUNTER,  "/Devices/IOAPIC/MMIOReadGC",   STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO reads in GC.");
    PDMDevHlpSTAMRegister(pDevIns, &s->StatMMIOReadHC,     STAMTYPE_COUNTER,  "/Devices/IOAPIC/MMIOReadHC",   STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO reads in HC.");
    PDMDevHlpSTAMRegister(pDevIns, &s->StatMMIOWriteGC,    STAMTYPE_COUNTER,  "/Devices/IOAPIC/MMIOWriteGC",  STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO writes in GC.");
    PDMDevHlpSTAMRegister(pDevIns, &s->StatMMIOWriteHC,    STAMTYPE_COUNTER,  "/Devices/IOAPIC/MMIOWriteHC",  STAMUNIT_OCCURENCES, "Number of IOAPIC MMIO writes in HC.");
    PDMDevHlpSTAMRegister(pDevIns, &s->StatSetIrqGC,       STAMTYPE_COUNTER,  "/Devices/IOAPIC/SetIrqGC",     STAMUNIT_OCCURENCES, "Number of IOAPIC SetIrq calls in GC.");
    PDMDevHlpSTAMRegister(pDevIns, &s->StatSetIrqHC,       STAMTYPE_COUNTER,  "/Devices/IOAPIC/SetIrqHC",     STAMUNIT_OCCURENCES, "Number of IOAPIC SetIrq calls in HC.");
#endif

    return VINF_SUCCESS;
}

/**
 * IO APIC device registration structure.
 */
const PDMDEVREG g_DeviceIOAPIC =
{
    /* u32Version */
    PDM_DEVREG_VERSION,
    /* szName */
    "ioapic",
    /* szRCMod */
    "VBoxDD2GC.gc",
    /* szR0Mod */
    "VBoxDD2R0.r0",
    /* pszDescription */
    "I/O Advanced Programmable Interrupt Controller (IO-APIC) Device",
    /* fFlags */
    PDM_DEVREG_FLAGS_HOST_BITS_DEFAULT | PDM_DEVREG_FLAGS_GUEST_BITS_32_64 | PDM_DEVREG_FLAGS_PAE36 | PDM_DEVREG_FLAGS_RC | PDM_DEVREG_FLAGS_R0,
    /* fClass */
    PDM_DEVREG_CLASS_PIC,
    /* cMaxInstances */
    1,
    /* cbInstance */
    sizeof(IOAPICState),
    /* pfnConstruct */
    ioapicConstruct,
    /* pfnDestruct */
    NULL,
    /* pfnRelocate */
    ioapicRelocate,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    ioapicReset,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnQueryInterface. */
    NULL,
    /* pfnInitComplete */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32VersionEnd */
    PDM_DEVREG_VERSION
};

#endif /* IN_RING3 */
#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */
