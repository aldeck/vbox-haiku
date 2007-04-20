/** @file
 *
 * VBox network devices:
 * AMD PC-Net II (Am79C970A + Am79C973) Ethernet Controller
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
 *
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 * AMD PC-Net II (Am79C970A) emulation
 *
 * Copyright (c) 2004 Antony T Curtis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* This software was written to be compatible with the specification:
 * AMD Am79C970A PCnet-PCI II Ethernet Controller Data-Sheet
 * AMD Publication# 19436  Rev:E  Amendment/0  Issue Date: June 2000
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_PCNET
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/mm.h>
#include <VBox/pdm.h>
#include <VBox/pgm.h>
#include <VBox/stam.h>
#include <VBox/vm.h> /* for VM_IS_EMT */
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/string.h>
#include <iprt/time.h>
#ifdef IN_RING3
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#endif

#include "Builtins.h"
#include "vl_vbox.h"

/* Enable this to catch writes to the ring descriptors instead of using excessive polling */
/* #define PCNET_NO_POLLING */

/* Enable to handle frequent io reads in the guest context */
#define PCNET_GC_ENABLED

#ifdef __GNUC__
#define PACKED __attribute__ ((packed))
#else
#define PACKED
#endif

#if 0
#define LOG_REGISTER(a)                 LogRel(a)
#else
#define LOG_REGISTER(a)
#endif
#if 0
#define LOG_PACKET(name, buf, count)    LogPkt(name, buf, count)
#define LOG_PACKETS
#else
#define LOG_PACKET(name, buf, count)
#undef  LOG_PACKETS
#endif

#if defined(LOG_ENABLED)
#define PCNET_DEBUG_IO
#define PCNET_DEBUG_BCR
#define PCNET_DEBUG_CSR
#define PCNET_DEBUG_RMD
#define PCNET_DEBUG_TMD
#define PCNET_DEBUG_MATCH
#define PCNET_DEBUG_MII
#endif

#define PCNET_IOPORT_SIZE               0x20
#define PCNET_PNPMMIO_SIZE              0x20

#define PCNET_SAVEDSTATE_VERSION        6

#define BCR_MAX_RAP                     50
#define MII_MAX_REG                     32
#define CSR_MAX_REG                     128

/* Maximum number of times we report a link down to the guest (failure to send frame) */
#define PCNET_MAX_LINKDOWN_REPORTED     3

/* Frame cache */
typedef struct PCNETFRAME
{
    /** The current frame size. Starts at -1. Only the top frame can be expanded. */
    int32_t  cb;
#if HC_ARCH_BITS == 64
    uint32_t Alignment;
#endif
    /** The virtual address of the frame (copied or direct pointer) */
    RTR3PTR  pvBuf;
} PCNETFRAME;
/* Pointer to PCNETFRAME */
typedef PCNETFRAME *PPCNETFRAME;

typedef struct PCNetState_st PCNetState;

struct PCNetState_st
{
    PCIDEVICE                           PciDev;
#ifndef PCNET_NO_POLLING
    /** Poll timer (address for host context) */
    PTMTIMERHC                          pTimerPollHC;
    /** Poll timer (address for guest context) */
    PTMTIMERGC                          pTimerPollGC;
#endif
    /** Register Address Pointer */
    uint32_t                            u32RAP;
    /** Internal interrupt service */
    int32_t                             iISR;
    /** ??? */
    uint32_t                            u32Lnkst;
    /** Address of the RX descriptor table (ring). Loaded at init. */
    RTGCPHYS                            GCRDRA;
    /** Address of the TX descriptor table (ring). Loaded at init. */
    RTGCPHYS                            GCTDRA;
    uint8_t                             aPROM[16];
    uint16_t                            aCSR[CSR_MAX_REG];
    uint16_t                            aBCR[BCR_MAX_RAP];
    uint16_t                            aMII[MII_MAX_REG];
    uint16_t                            u16CSR0LastSeenByGuest;
    uint16_t                            Alignment0[HC_ARCH_BITS == 32 ? 2 : 4];
    /** Last time we polled the queues */
    uint64_t                            u64LastPoll;

    /** Array of frames. */
    PCNETFRAME                          SendFrame;
    /** The xmit buffer. */
    uint8_t                             abSendBuf[4096];
    /** The recv buffer. */
    uint8_t                             abRecvBuf[4096];

    /** Pending send packet counter. */
    uint32_t                            cPendingSends;

    /** Size of a RX/TX descriptor (8 or 16 bytes according to SWSTYLE */
    int                                 iLog2DescSize;
    /** Bits 16..23 in 16-bit mode */
    RTGCPHYS                            GCUpperPhys;

    /** Transmit signaller */
    GCPTRTYPE(PPDMQUEUE)                pXmitQueueGC;
    HCPTRTYPE(PPDMQUEUE)                pXmitQueueHC;

    /** Receive signaller */
    HCPTRTYPE(PPDMQUEUE)                pCanRxQueueHC;
    GCPTRTYPE(PPDMQUEUE)                pCanRxQueueGC;
    /** Pointer to the device instance. */
    GCPTRTYPE(PPDMDEVINS)               pDevInsGC;
    /** Pointer to the device instance. */
    HCPTRTYPE(PPDMDEVINS)               pDevInsHC;
    /** Restore timer.
     *  This is used to disconnect and reconnect the link after a restore. */
    PTMTIMERHC                          pTimerRestore;
    /** Pointer to the connector of the attached network driver. */
    HCPTRTYPE(PPDMINETWORKCONNECTOR)    pDrv;
    /** Pointer to the attached network driver. */
    HCPTRTYPE(PPDMIBASE)                pDrvBase;
    /** The base interface. */
    PDMIBASE                            IBase;
    /** The network port interface. */
    PDMINETWORKPORT                     INetworkPort;
    /** The network config port interface. */
    PDMINETWORKCONFIG                   INetworkConfig;
    /** Base address of the MMIO region. */
    RTGCPHYS                            MMIOBase;
    /** Base port of the I/O space region. */
    RTIOPORT                            IOPortBase;
    /** If set the link is currently up. */
    bool                                fLinkUp;
    /** If set the link is temporarily down because of a saved state load. */
    bool                                fLinkTempDown;
    /** This flag is set on SavePrep to prevent altering of memory after pgmR3Save() was called */
    bool                                fSaving;

    /** Number of times we've reported the link down. */
    RTUINT                              cLinkDownReported;
    /** The configured MAC address. */
    PDMMAC                              MacConfigured;

    /** The LED. */
    PDMLED                              Led;
    /** The LED ports. */
    PDMILEDPORTS                        ILeds;
    /** Partner of ILeds. */
    HCPTRTYPE(PPDMILEDCONNECTORS)       pLedsConnector;

    /** Async send thread */
    RTSEMEVENT                          hSendEventSem;
    RTTHREAD                            hSendThread;

    /** Access critical section. */
    PDMCRITSECT                         CritSect;

#ifdef PCNET_NO_POLLING
    RTGCPHYS                            TDRAPhysOld;
    uint32_t                            cbTDRAOld;

    RTGCPHYS                            RDRAPhysOld;
    uint32_t                            cbRDRAOld;

    DECLGCCALLBACKMEMBER(int, pfnEMInterpretInstructionGC, (PVM pVM, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault, uint32_t *pcbSize));
    DECLR0CALLBACKMEMBER(int, pfnEMInterpretInstructionR0, (PVM pVM, PCPUMCTXCORE pRegFrame, RTGCPTR pvFault, uint32_t *pcbSize));
#endif

    bool                                fGCEnabled;
    bool                                fR0Enabled;
    bool                                fAm79C973;
    bool                                afAlignment[5];

#ifdef VBOX_WITH_STATISTICS
    STAMPROFILEADV                      StatMMIOReadGC;
    STAMPROFILEADV                      StatMMIOReadHC;
    STAMPROFILEADV                      StatMMIOWriteGC;
    STAMPROFILEADV                      StatMMIOWriteHC;
    STAMPROFILEADV                      StatAPROMRead;
    STAMPROFILEADV                      StatAPROMWrite;
    STAMPROFILEADV                      StatIOReadGC;
    STAMPROFILEADV                      StatIOReadHC;
    STAMPROFILEADV                      StatIOWriteGC;
    STAMPROFILEADV                      StatIOWriteHC;
    STAMPROFILEADV                      StatTimer;
    STAMPROFILEADV                      StatReceive;
    STAMPROFILEADV                      StatTransmit;
    STAMPROFILEADV                      StatTransmitSend;
    STAMPROFILEADV                      StatTdtePollGC;
    STAMPROFILEADV                      StatTdtePollHC;
    STAMPROFILEADV                      StatTmdStoreGC;
    STAMPROFILEADV                      StatTmdStoreHC;
    STAMPROFILEADV                      StatRdtePollGC;
    STAMPROFILEADV                      StatRdtePollHC;
    STAMCOUNTER                         aStatXmitFlush[16];
    STAMCOUNTER                         aStatXmitChainCounts[16];
    STAMCOUNTER                         StatXmitSkipCurrent;
    STAMPROFILEADV                      StatInterrupt;
    STAMPROFILEADV                      StatPollTimer;
    STAMCOUNTER                         StatMIIReads;
# ifdef PCNET_NO_POLLING
    STAMCOUNTER                         StatRCVRingWrite;
    STAMCOUNTER                         StatTXRingWrite;
    STAMCOUNTER                         StatRingWriteHC;
    STAMCOUNTER                         StatRingWriteR0;
    STAMCOUNTER                         StatRingWriteGC;

    STAMCOUNTER                         StatRingWriteFailedHC;
    STAMCOUNTER                         StatRingWriteFailedR0;
    STAMCOUNTER                         StatRingWriteFailedGC;

    STAMCOUNTER                         StatRingWriteOutsideRangeHC;
    STAMCOUNTER                         StatRingWriteOutsideRangeR0;
    STAMCOUNTER                         StatRingWriteOutsideRangeGC;
# endif
#endif /* VBOX_WITH_STATISTICS */
};

#define PCNETSTATE_2_DEVINS(pPCNet)            ( (pPCNet)->CTXSUFF(pDevIns) )
#define PCIDEV_2_PCNETSTATE(pPciDev)           ( (PCNetState *)(pPciDev) )

/* BUS CONFIGURATION REGISTERS */
#define BCR_MSRDA       0
#define BCR_MSWRA       1
#define BCR_MC          2
#define BCR_RESERVED3   3
#define BCR_LNKST       4
#define BCR_LED1        5
#define BCR_LED2        6
#define BCR_LED3        7
#define BCR_RESERVED8   8
#define BCR_FDC         9
/* 10 - 15 = reserved */
#define BCR_IOBASEL     16  /* Reserved */
#define BCR_IOBASEU     16  /* Reserved */
#define BCR_BSBC        18
#define BCR_EECAS       19
#define BCR_SWS         20
#define BCR_INTCON      21  /* Reserved */
#define BCR_PLAT        22
#define BCR_PCISID      23
#define BCR_PCISVID     24
#define BCR_SRAMSIZ     25
#define BCR_SRAMB       26
#define BCR_SRAMIC      27
#define BCR_EBADDRL     28
#define BCR_EBADDRU     29
#define BCR_EBD         30
#define BCR_STVAL       31
#define BCR_MIICAS      32
#define BCR_MIIADDR     33
#define BCR_MIIMDR      34
#define BCR_PCIVID      35
#define BCR_PMC_A       36
#define BCR_DATA0       37
#define BCR_DATA1       38
#define BCR_DATA2       39
#define BCR_DATA3       40
#define BCR_DATA4       41
#define BCR_DATA5       42
#define BCR_DATA6       43
#define BCR_DATA7       44
#define BCR_PMR1        45
#define BCR_PMR2        46
#define BCR_PMR3        47

#define BCR_DWIO(S)      !!((S)->aBCR[BCR_BSBC] & 0x0080)
#define BCR_SSIZE32(S)   !!((S)->aBCR[BCR_SWS ] & 0x0100)
#define BCR_SWSTYLE(S)     ((S)->aBCR[BCR_SWS ] & 0x00FF)

#define CSR_INIT(S)      !!((S)->aCSR[0] & 0x0001)  /**< Init assertion */
#define CSR_STRT(S)      !!((S)->aCSR[0] & 0x0002)  /**< Start assertion */
#define CSR_STOP(S)      !!((S)->aCSR[0] & 0x0004)  /**< Stop assertion */
#define CSR_TDMD(S)      !!((S)->aCSR[0] & 0x0008)  /**< Transmit demand. (perform xmit poll now
                                                         (readable, settable, not clearable) */
#define CSR_TXON(S)      !!((S)->aCSR[0] & 0x0010)  /**< Transmit on (readonly) */
#define CSR_RXON(S)      !!((S)->aCSR[0] & 0x0020)  /**< Receive On */
#define CSR_INEA(S)      !!((S)->aCSR[0] & 0x0040)  /**< Interrupt Enable */
#define CSR_LAPPEN(S)    !!((S)->aCSR[3] & 0x0020)  /**< Look Ahead Packet Processing Enable */
#define CSR_DXSUFLO(S)   !!((S)->aCSR[3] & 0x0040)  /**< Disable Transmit Stop on
                                                           Underflow error */
#define CSR_ASTRP_RCV(S) !!((S)->aCSR[4] & 0x0400)  /**< Auto Strip Receive */
#define CSR_DPOLL(S)     !!((S)->aCSR[4] & 0x1000)  /**< Disable Transmit Polling */
#define CSR_SPND(S)      !!((S)->aCSR[5] & 0x0001)  /**< Suspend */
#define CSR_LTINTEN(S)   !!((S)->aCSR[5] & 0x4000)  /**< Last Transmit Interrupt Enable */
#define CSR_TOKINTD(S)   !!((S)->aCSR[5] & 0x8000)  /**< Transmit OK Interrupt Disable */
#define CSR_DRX(S)       !!((S)->aCSR[15] & 0x0001) /**< Disable Receiver */
#define CSR_DTX(S)       !!((S)->aCSR[15] & 0x0002) /**< Disable Transmit */
#define CSR_LOOP(S)      !!((S)->aCSR[15] & 0x0004) /**< Loopback Enable */
#define CSR_DRCVPA(S)    !!((S)->aCSR[15] & 0x2000) /**< Disable Receive Physical Address */
#define CSR_DRCVBC(S)    !!((S)->aCSR[15] & 0x4000) /**< Disable Receive Broadcast */
#define CSR_PROM(S)      !!((S)->aCSR[15] & 0x8000) /**< Promiscuous Mode */

#if !defined(__X86__) && !defined(__AMD64__)
#error fix macros (and more in this file) for big-endian machines
#endif

#define CSR_IADR(S)  (*(uint32_t*)((S)->aCSR +  1)) /**< Initialization Block Address */
#define CSR_CRBA(S)  (*(uint32_t*)((S)->aCSR + 18)) /**< Current Receive Buffer Address */
#define CSR_CXBA(S)  (*(uint32_t*)((S)->aCSR + 20)) /**< Current Transmit Buffer Address */
#define CSR_NRBA(S)  (*(uint32_t*)((S)->aCSR + 22)) /**< Next Receive Buffer Address */
#define CSR_BADR(S)  (*(uint32_t*)((S)->aCSR + 24)) /**< Base Address of Receive Ring */
#define CSR_NRDA(S)  (*(uint32_t*)((S)->aCSR + 26)) /**< Next Receive Descriptor Address */
#define CSR_CRDA(S)  (*(uint32_t*)((S)->aCSR + 28)) /**< Current Receive Descriptor Address */
#define CSR_BADX(S)  (*(uint32_t*)((S)->aCSR + 30)) /**< Base Address of Transmit Descriptor */
#define CSR_NXDA(S)  (*(uint32_t*)((S)->aCSR + 32)) /**< Next Transmit Descriptor Address */
#define CSR_CXDA(S)  (*(uint32_t*)((S)->aCSR + 34)) /**< Current Transmit Descriptor Address */
#define CSR_NNRD(S)  (*(uint32_t*)((S)->aCSR + 36)) /**< Next Next Receive Descriptor Address */
#define CSR_NNXD(S)  (*(uint32_t*)((S)->aCSR + 38)) /**< Next Next Transmit Descriptor Address */
#define CSR_CRBC(S)  ((S)->aCSR[40])                /**< Current Receive Byte Count */
#define CSR_CRST(S)  ((S)->aCSR[41])                /**< Current Receive Status */
#define CSR_CXBC(S)  ((S)->aCSR[42])                /**< Current Transmit Byte Count */
#define CSR_CXST(S)  ((S)->aCSR[43])                /**< Current transmit status */
#define CSR_NRBC(S)  ((S)->aCSR[44])                /**< Next Receive Byte Count */
#define CSR_NRST(S)  ((S)->aCSR[45])                /**< Next Receive Status */
#define CSR_POLL(S)  ((S)->aCSR[46])                /**< Transmit Poll Time Counter */
#define CSR_PINT(S)  ((S)->aCSR[47])                /**< Transmit Polling Interval */
#define CSR_PXDA(S)  (*(uint32_t*)((S)->aCSR + 60)) /**< Previous Transmit Descriptor Address*/
#define CSR_PXBC(S)  ((S)->aCSR[62])                /**< Previous Transmit Byte Count */
#define CSR_PXST(S)  ((S)->aCSR[63])                /**< Previous Transmit Status */
#define CSR_NXBA(S)  (*(uint32_t*)((S)->aCSR + 64)) /**< Next Transmit Buffer Address */
#define CSR_NXBC(S)  ((S)->aCSR[66])                /**< Next Transmit Byte Count */
#define CSR_NXST(S)  ((S)->aCSR[67])                /**< Next Transmit Status */
#define CSR_RCVRC(S) ((S)->aCSR[72])                /**< Receive Descriptor Ring Counter */
#define CSR_XMTRC(S) ((S)->aCSR[74])                /**< Transmit Descriptor Ring Counter */
#define CSR_RCVRL(S) ((S)->aCSR[76])                /**< Receive Descriptor Ring Length */
#define CSR_XMTRL(S) ((S)->aCSR[78])                /**< Transmit Descriptor Ring Length */
#define CSR_MISSC(S) ((S)->aCSR[112])               /**< Missed Frame Count */

#define PHYSADDR(S,A) ((A) | (S)->GCUpperPhys)

/* Version for the PCnet/FAST III 79C973 card */
#define CSR_VERSION_LOW_79C973  0x5003  /* the lower two bits must be 11b for AMD */
#define CSR_VERSION_LOW_79C970A 0x1003  /* the lower two bits must be 11b for AMD */
#define CSR_VERSION_HIGH        0x0262

/** @todo   All structs: big endian? */

#pragma pack(1) /* bird: MSC paranoia */
/** frank: From the gcc manual: ``This attribute, attached to `struct' or `union' type
 *         definition, specifies that each member (other than zero-width bitfields) of
 *         the structure or union is placed to minimize the memory required. ...
 *         Specifying this attribute for `struct' and `union' types is equivalent to
 *         specifying the `packed attribute' on each the structure or union members.''
 * @todo r=bird: #pragma pack(1) / __attribute__((packed)) isn't necessary here
 *               because of the way the alignment rules work.
 */
struct PACKED INITBLK16
{
    uint16_t mode;      /**< copied into csr15 */
    uint16_t padr1;     /**< MAC  0..15 */
    uint16_t padr2;     /**< MAC 16..32 */
    uint16_t padr3;     /**< MAC 33..47 */
    uint16_t ladrf1;    /**< logical address filter  0..15 */
    uint16_t ladrf2;    /**< logical address filter 16..31 */
    uint16_t ladrf3;    /**< logical address filter 32..47 */
    uint16_t ladrf4;    /**< logical address filter 48..63 */
    uint32_t rdra:24;   /**< address of receive descriptor ring */
    uint32_t res1:5;    /**< reserved */
    uint32_t rlen:3;    /**< number of receive descriptor ring entries */
    uint32_t tdra:24;   /**< address of transmit descriptor ring */
    uint32_t res2:5;    /**< reserved */
    uint32_t tlen:3;    /**< number of transmit descriptor ring entries */
};
AssertCompileSize(INITBLK16, 24);

/** bird:  I've changed the type for the bitfields. They should only be 16-bit all together.
 *  frank: I've changed the bitfiled types to uint32_t to prevent compiler warnings. */
struct PACKED INITBLK32
{
    uint16_t mode;      /**< copied into csr15 */
    uint16_t res1:4;    /**< reserved */
    uint16_t rlen:4;    /**< number of receive descriptor ring entries */
    uint16_t res2:4;    /**< reserved */
    uint16_t tlen:4;    /**< number of transmit descriptor ring entries */
    uint16_t padr1;     /**< MAC  0..15 */
    uint16_t padr2;     /**< MAC 16..31 */
    uint16_t padr3;     /**< MAC 32..47 */
    uint16_t res3;      /**< reserved */
    uint16_t ladrf1;    /**< logical address filter  0..15 */
    uint16_t ladrf2;    /**< logical address filter 16..31 */
    uint16_t ladrf3;    /**< logibal address filter 32..47 */
    uint16_t ladrf4;    /**< logical address filter 48..63 */
    uint32_t rdra;      /**< address of receive descriptor ring */
    uint32_t tdra;      /**< address of transmit descriptor ring */
};
AssertCompileSize(INITBLK32, 28);

/** Transmit Message Descriptor */
typedef struct TMD
{
    struct
    {
        uint32_t tbadr;         /**< transmit buffer address */
    } tmd0;
    struct PACKED
    {
        uint32_t bcnt:12;       /**< buffer byte count (two's complement) */
        uint32_t ones:4;        /**< must be 1111b */
        uint32_t res:7;         /**< reserved */
        uint32_t bpe:1;         /**< bus parity error */
        uint32_t enp:1;         /**< end of packet */
        uint32_t stp:1;         /**< start of packet */
        uint32_t def:1;         /**< deferred */
        uint32_t one:1;         /**< exactly one retry was needed to transmit a frame */
        uint32_t ltint:1;       /**< suppress interrupts after successful transmission */
        uint32_t nofcs:1;       /**< when set, the state of DXMTFCS is ignored and
                                     transmitter FCS generation is activated. */
        uint32_t err:1;         /**< error occured */
        uint32_t own:1;         /**< 0=owned by guest driver, 1=owned by controller */
    } tmd1;
    struct PACKED
    {
        uint32_t trc:4;         /**< transmit retry count */
        uint32_t res:12;        /**< reserved */
        uint32_t tdr:10;        /**< ??? */
        uint32_t rtry:1;        /**< retry error */
        uint32_t lcar:1;        /**< loss of carrier */
        uint32_t lcol:1;        /**< late collision */
        uint32_t exdef:1;       /**< excessive deferral */
        uint32_t uflo:1;        /**< underflow error */
        uint32_t buff:1;        /**< out of buffers (ENP not found) */
    } tmd2;
    struct
    {
        uint32_t res;           /**< reserved for user defined space */
    } tmd3;
} TMD;
AssertCompileSize(TMD, 16);

/** Receive Message Descriptor */
typedef struct RMD
{
    struct
    {
        uint32_t rbadr;         /**< receive buffer address */
    } rmd0;
    struct PACKED
    {
        uint32_t bcnt:12;       /**< buffer byte count (two's complement) */
        uint32_t ones:4;        /**< must be 1111b */
        uint32_t res:4;         /**< reserved */
        uint32_t bam:1;         /**< broadcast address match */
        uint32_t lafm:1;        /**< logical filter address match */
        uint32_t pam:1;         /**< physcial address match */
        uint32_t bpe:1;         /**< bus parity error */
        uint32_t enp:1;         /**< end of packet */
        uint32_t stp:1;         /**< start of packet */
        uint32_t buff:1;        /**< buffer error */
        uint32_t crc:1;         /**< crc error on incoming frame */
        uint32_t oflo:1;        /**< overflow error (lost all or part of incoming frame) */
        uint32_t fram:1;        /**< frame error */
        uint32_t err:1;         /**< error occured */
        uint32_t own:1;         /**< 0=owned by guest driver, 1=owned by controller */
    } rmd1;
    struct PACKED
    {
        uint32_t mcnt:12;       /**< message byte count */
        uint32_t zeros:4;       /**< 0000b */
        uint32_t rpc:8;         /**< receive frame tag */
        uint32_t rcc:8;         /**< receive frame tag + reserved */
    } rmd2;
    struct
    {
        uint32_t res;           /**< reserved for user defined space */
    } rmd3;
} RMD;
AssertCompileSize(RMD, 16);
#pragma pack()


#ifndef VBOX_DEVICE_STRUCT_TESTCASE
/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
#define PRINT_TMD(T) Log2((    \
        "TMD0 : TBADR=0x%08x\n" \
        "TMD1 : OWN=%d, ERR=%d, FCS=%d, LTI=%d, "       \
        "ONE=%d, DEF=%d, STP=%d, ENP=%d,\n"             \
        "       BPE=%d, BCNT=%d\n"                      \
        "TMD2 : BUF=%d, UFL=%d, EXD=%d, LCO=%d, "       \
        "LCA=%d, RTR=%d,\n"                             \
        "       TDR=%d, TRC=%d\n",                      \
        (T)->tmd0.tbadr,                                \
        (T)->tmd1.own, (T)->tmd1.err, (T)->tmd1.nofcs,  \
        (T)->tmd1.ltint, (T)->tmd1.one, (T)->tmd1.def,  \
        (T)->tmd1.stp, (T)->tmd1.enp, (T)->tmd1.bpe,    \
        4096-(T)->tmd1.bcnt,                            \
        (T)->tmd2.buff, (T)->tmd2.uflo, (T)->tmd2.exdef,\
        (T)->tmd2.lcol, (T)->tmd2.lcar, (T)->tmd2.rtry, \
        (T)->tmd2.tdr, (T)->tmd2.trc))

#define PRINT_RMD(R) Log2((    \
        "RMD0 : RBADR=0x%08x\n" \
        "RMD1 : OWN=%d, ERR=%d, FRAM=%d, OFLO=%d, "     \
        "CRC=%d, BUFF=%d, STP=%d, ENP=%d,\n       "     \
        "BPE=%d, PAM=%d, LAFM=%d, BAM=%d, ONES=%d, BCNT=%d\n" \
        "RMD2 : RCC=%d, RPC=%d, MCNT=%d, ZEROS=%d\n",   \
        (R)->rmd0.rbadr,                                \
        (R)->rmd1.own, (R)->rmd1.err, (R)->rmd1.fram,   \
        (R)->rmd1.oflo, (R)->rmd1.crc, (R)->rmd1.buff,  \
        (R)->rmd1.stp, (R)->rmd1.enp, (R)->rmd1.bpe,    \
        (R)->rmd1.pam, (R)->rmd1.lafm, (R)->rmd1.bam,   \
        (R)->rmd1.ones, 4096-(R)->rmd1.bcnt,            \
        (R)->rmd2.rcc, (R)->rmd2.rpc, (R)->rmd2.mcnt,   \
        (R)->rmd2.zeros))

/**
 * Load transmit message descriptor
 * Make sure we read the own flag first.
 */
DECLINLINE(void) pcnetTmdLoad(PCNetState *pData, TMD *tmd, RTGCPHYS addr)
{
    PPDMDEVINS pDevIns = PCNETSTATE_2_DEVINS(pData);
    uint8_t    ownbyte;

    if (!BCR_SWSTYLE(pData))
    {
        uint16_t xda[4];

        PDMDevHlpPhysRead(pDevIns, addr+3, &ownbyte, 1);
        PDMDevHlpPhysRead(pDevIns, addr, (void*)&xda[0], sizeof(xda));
        ((uint32_t *)tmd)[0] = (uint32_t)xda[0] | ((uint32_t)(xda[1] & 0x00ff) << 16);
        ((uint32_t *)tmd)[1] = (uint32_t)xda[2] | ((uint32_t)(xda[1] & 0xff00) << 16);
        ((uint32_t *)tmd)[2] = (uint32_t)xda[3] << 16;
        ((uint32_t *)tmd)[3] = 0;
    }
    else if (BCR_SWSTYLE(pData) != 3)
    {
        PDMDevHlpPhysRead(pDevIns, addr+7, &ownbyte, 1);
        PDMDevHlpPhysRead(pDevIns, addr, (void*)tmd, 16);
    }
    else
    {
        uint32_t xda[4];
        PDMDevHlpPhysRead(pDevIns, addr+7, &ownbyte, 1);
        PDMDevHlpPhysRead(pDevIns, addr, (void*)&xda[0], sizeof(xda));
        ((uint32_t *)tmd)[0] = xda[2];
        ((uint32_t *)tmd)[1] = xda[1];
        ((uint32_t *)tmd)[2] = xda[0];
        ((uint32_t *)tmd)[3] = xda[3];
    }
    /* Double check the own bit; guest drivers might be buggy and lock prefixes in the recompiler are ignored by other threads. */
#ifdef DEBUG
    if (tmd->tmd1.own == 1 && !(ownbyte & 0x80))
        Log(("pcnetTmdLoad: own bit flipped while reading!!\n"));
#endif
    if (!(ownbyte & 0x80))
        tmd->tmd1.own = 0;
}

/**
 * Store transmit message descriptor and hand it over to the host (the VM guest).
 * Make sure that all data are transmitted before we clear the own flag.
 */
DECLINLINE(void) pcnetTmdStorePassHost(PCNetState *pData, TMD *tmd, RTGCPHYS addr)
{
    STAM_PROFILE_ADV_START(&pData->CTXSUFF(StatTmdStore), a);
    PPDMDEVINS pDevIns = PCNETSTATE_2_DEVINS(pData);
    if (!BCR_SWSTYLE(pData))
    {
        uint16_t xda[4];
        xda[0] =   ((uint32_t *)tmd)[0]        & 0xffff;
        xda[1] = ((((uint32_t *)tmd)[0] >> 16) &   0xff) | ((((uint32_t *)tmd)[1]>>16) & 0xff00);
        xda[2] =   ((uint32_t *)tmd)[1]        & 0xffff;
        xda[3] =   ((uint32_t *)tmd)[2] >> 16;
        xda[1] |=  0x8000;
        PDMDevHlpPhysWrite(pDevIns, addr, (void*)&xda[0], sizeof(xda));
        xda[1] &= ~0x8000;
        PDMDevHlpPhysWrite(pDevIns, addr+3, (uint8_t*)xda + 3, 1);
    }
    else if (BCR_SWSTYLE(pData) != 3)
    {
        ((uint32_t*)tmd)[1] |=  0x80000000;
        PDMDevHlpPhysWrite(pDevIns, addr, (void*)tmd, 16);
        ((uint32_t*)tmd)[1] &= ~0x80000000;
        PDMDevHlpPhysWrite(pDevIns, addr+7, (uint8_t*)tmd + 7, 1);
    }
    else
    {
        uint32_t xda[4];
        xda[0] = ((uint32_t *)tmd)[2];
        xda[1] = ((uint32_t *)tmd)[1];
        xda[2] = ((uint32_t *)tmd)[0];
        xda[3] = ((uint32_t *)tmd)[3];
        xda[1] |=  0x80000000;
        PDMDevHlpPhysWrite(pDevIns, addr, (void*)&xda[0], sizeof(xda));
        xda[1] &= ~0x80000000;
        PDMDevHlpPhysWrite(pDevIns, addr+7, (uint8_t*)xda + 7, 1);
    }
    STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatTmdStore), a);
}

/**
 * Load receive message descriptor
 * Make sure we read the own flag first.
 */
DECLINLINE(void) pcnetRmdLoad(PCNetState *pData, RMD *rmd, RTGCPHYS addr)
{
    PPDMDEVINS pDevIns = PCNETSTATE_2_DEVINS(pData);
    uint8_t    ownbyte;

    if (!BCR_SWSTYLE(pData))
    {
        uint16_t rda[4];
        PDMDevHlpPhysRead(pDevIns, addr+3, &ownbyte, 1);
        PDMDevHlpPhysRead(pDevIns, addr, (void*)&rda[0], sizeof(rda));
        ((uint32_t *)rmd)[0] = (uint32_t)rda[0] | ((rda[1] & 0x00ff) << 16);
        ((uint32_t *)rmd)[1] = (uint32_t)rda[2] | ((rda[1] & 0xff00) << 16);
        ((uint32_t *)rmd)[2] = (uint32_t)rda[3];
        ((uint32_t *)rmd)[3] = 0;
    }
    else if (BCR_SWSTYLE(pData) != 3)
    {
        PDMDevHlpPhysRead(pDevIns, addr+7, &ownbyte, 1);
        PDMDevHlpPhysRead(pDevIns, addr, (void*)rmd, 16);
    }
    else
    {
        uint32_t rda[4];
        PDMDevHlpPhysRead(pDevIns, addr+7, &ownbyte, 1);
        PDMDevHlpPhysRead(pDevIns, addr, (void*)&rda[0], sizeof(rda));
        ((uint32_t *)rmd)[0] = rda[2];
        ((uint32_t *)rmd)[1] = rda[1];
        ((uint32_t *)rmd)[2] = rda[0];
        ((uint32_t *)rmd)[3] = rda[3];
    }
    /* Double check the own bit; guest drivers might be buggy and lock prefixes in the recompiler are ignored by other threads. */
#ifdef DEBUG
    if (rmd->rmd1.own == 1 && !(ownbyte & 0x80))
        Log(("pcnetTmdLoad: own bit flipped while reading!!\n"));
#endif
    if (!(ownbyte & 0x80))
        rmd->rmd1.own = 0;
}

/**
 * Store receive message descriptor and hand it over to the host (the VM guest).
 * Make sure that all data are transmitted before we clear the own flag.
 */
DECLINLINE(void) pcnetRmdStorePassHost(PCNetState *pData, RMD *rmd, RTGCPHYS addr)
{
    PPDMDEVINS pDevIns = PCNETSTATE_2_DEVINS(pData);
    if (!BCR_SWSTYLE(pData))
    {
        uint16_t rda[4];
        rda[0] =   ((uint32_t *)rmd)[0]      & 0xffff;
        rda[1] = ((((uint32_t *)rmd)[0]>>16) &   0xff) | ((((uint32_t *)rmd)[1]>>16) & 0xff00);
        rda[2] =   ((uint32_t *)rmd)[1]      & 0xffff;
        rda[3] =   ((uint32_t *)rmd)[2]      & 0xffff;
        rda[1] |=  0x8000;
        PDMDevHlpPhysWrite(pDevIns, addr, (void*)&rda[0], sizeof(rda));
        rda[1] &= ~0x8000;
        PDMDevHlpPhysWrite(pDevIns, addr+3, (uint8_t*)rda + 3, 1);
    }
    else if (BCR_SWSTYLE(pData) != 3)
    {
        ((uint32_t*)rmd)[1] |=  0x80000000;
        PDMDevHlpPhysWrite(pDevIns, addr, (void*)rmd, 16);
        ((uint32_t*)rmd)[1] &= ~0x80000000;
        PDMDevHlpPhysWrite(pDevIns, addr+7, (uint8_t*)rmd + 7, 1);
    }
    else
    {
        uint32_t rda[4];
        rda[0] = ((uint32_t *)rmd)[2];
        rda[1] = ((uint32_t *)rmd)[1];
        rda[2] = ((uint32_t *)rmd)[0];
        rda[3] = ((uint32_t *)rmd)[3];
        rda[1] |=  0x80000000;
        PDMDevHlpPhysWrite(pDevIns, addr, (void*)&rda[0], sizeof(rda));
        rda[1] &= ~0x80000000;
        PDMDevHlpPhysWrite(pDevIns, addr+7, (uint8_t*)rda + 7, 1);
    }
}

/** Checks if it's a bad (as in invalid) RMD.*/
#define IS_RMD_BAD(rmd)      ((rmd).rmd1.ones != 15 || (rmd).rmd2.zeros != 0)

/** The network card is the owner of the RDTE/TDTE, actually it is this driver */
#define CARD_IS_OWNER(desc)   (((desc) & 0x8000))

/** The host is the owner of the RDTE/TDTE -- actually the VM guest. */
#define HOST_IS_OWNER(desc)  (!((desc) & 0x8000))

#ifndef ETHER_IS_MULTICAST /* Net/Open BSD macro it seems */
#define ETHER_IS_MULTICAST(a) ((*(uint8_t *)(a)) & 1)
#endif

#define ETHER_ADDR_LEN ETH_ALEN
#define ETH_ALEN 6
#pragma pack(1)
struct ether_header
{
    uint8_t  ether_dhost[ETH_ALEN]; /**< destination ethernet address */
    uint8_t  ether_shost[ETH_ALEN]; /**< source ethernet address */
    uint16_t ether_type;            /**< packet type ID field */
};
#pragma pack()

#ifdef LOG_PACKETS
static void LogPkt(const char *name, const void *const src, int count)
{
    int i, j;
    const uint8_t * const p = (const uint8_t * const)src;
    LogRel(("%s:  ", name));
    i  = 14;               // length of MAC header
    i += 4*(p[i] & 15);    // length of IP  header
    i += 4*(p[i+12] >> 4); // length of TCP header
    for (j=i; j<70 && j<count; j++)
        LogRel((" %02x", p[j]));
    LogRel((" ("));
    for (j=i; j<70 && j<count; j++)
        LogRel(("%c", p[j] >= 32 && p[j] < 127 ? p[j] : '.'));
    LogRel((")\n"));
}
#endif

#define PRINT_PKTHDR(BUF) do {                                        \
    struct ether_header *hdr = (struct ether_header *)(BUF);          \
    Log(("packet dhost=%02x:%02x:%02x:%02x:%02x:%02x, "               \
         "shost=%02x:%02x:%02x:%02x:%02x:%02x, "                      \
         "type=0x%04x (bcast=%d)\n",                                  \
         hdr->ether_dhost[0],hdr->ether_dhost[1],hdr->ether_dhost[2], \
         hdr->ether_dhost[3],hdr->ether_dhost[4],hdr->ether_dhost[5], \
         hdr->ether_shost[0],hdr->ether_shost[1],hdr->ether_shost[2], \
         hdr->ether_shost[3],hdr->ether_shost[4],hdr->ether_shost[5], \
         htons(hdr->ether_type),                                      \
         !!ETHER_IS_MULTICAST(hdr->ether_dhost)));                    \
} while (0)


#ifdef IN_RING3

#define MULTICAST_FILTER_LEN 8

DECLINLINE(uint32_t) lnc_mchash(const uint8_t *ether_addr)
{
#define LNC_POLYNOMIAL          0xEDB88320UL
    uint32_t crc = 0xFFFFFFFF;
    int idx, bit;
    uint8_t data;

    for (idx = 0; idx < ETHER_ADDR_LEN; idx++)
    {
        for (data = *ether_addr++, bit = 0; bit < MULTICAST_FILTER_LEN; bit++)
        {
            crc = (crc >> 1) ^ (((crc ^ data) & 1) ? LNC_POLYNOMIAL : 0);
            data >>= 1;
        }
    }
    return crc;
#undef LNC_POLYNOMIAL
}

#define CRC(crc, ch)	 (crc = (crc >> 8) ^ crctab[(crc ^ (ch)) & 0xff])

/* generated using the AUTODIN II polynomial
 *	x^32 + x^26 + x^23 + x^22 + x^16 +
 *	x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x^1 + 1
 */
static const uint32_t crctab[256] =
{
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
	0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
	0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
	0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
	0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
	0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
	0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
	0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
	0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
	0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
	0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
	0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
	0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
	0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
	0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
	0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
	0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
	0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
	0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
	0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
	0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
	0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
	0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
	0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
	0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
	0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
	0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
	0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
	0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
	0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
	0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
	0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
	0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
	0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
	0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
	0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
	0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
	0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
	0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d,
};

DECLINLINE(int) padr_match(PCNetState *pData, const uint8_t *buf, int size)
{
    struct ether_header *hdr = (struct ether_header *)buf;
    int     result;
#if (defined(__X86__) || defined(__AMD64__)) && !defined(PCNET_DEBUG_MATCH)
    result = !CSR_DRCVPA(pData) && !memcmp(hdr->ether_dhost, pData->aCSR + 12, 6);
#else
    uint8_t padr[6];
    padr[0] = pData->aCSR[12] & 0xff;
    padr[1] = pData->aCSR[12] >> 8;
    padr[2] = pData->aCSR[13] & 0xff;
    padr[3] = pData->aCSR[13] >> 8;
    padr[4] = pData->aCSR[14] & 0xff;
    padr[5] = pData->aCSR[14] >> 8;
    result = !CSR_DRCVPA(pData) && !memcmp(hdr->ether_dhost, padr, 6);
#endif

#ifdef PCNET_DEBUG_MATCH
    Log(("#%d packet dhost=%02x:%02x:%02x:%02x:%02x:%02x, "
         "padr=%02x:%02x:%02x:%02x:%02x:%02x\n",
         PCNETSTATE_2_DEVINS(pData)->iInstance,
         hdr->ether_dhost[0],hdr->ether_dhost[1],hdr->ether_dhost[2],
         hdr->ether_dhost[3],hdr->ether_dhost[4],hdr->ether_dhost[5],
         padr[0],padr[1],padr[2],padr[3],padr[4],padr[5]));
    Log(("padr_match result=%d\n", result));
#endif
    return result;
}

DECLINLINE(int) padr_bcast(PCNetState *pData, const uint8_t *buf, int size)
{
    static uint8_t aBCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    struct ether_header *hdr = (struct ether_header *)buf;
    int result = !CSR_DRCVBC(pData) && !memcmp(hdr->ether_dhost, aBCAST, 6);
#ifdef PCNET_DEBUG_MATCH
    Log(("#%d padr_bcast result=%d\n", PCNETSTATE_2_DEVINS(pData)->iInstance, result));
#endif
   return result;
}

static int ladr_match(PCNetState *pData, const uint8_t *buf, int size)
{
    struct ether_header *hdr = (struct ether_header *)buf;
    if (RT_UNLIKELY(hdr->ether_dhost[0] & 0x01) && ((uint64_t *)&pData->aCSR[8])[0] != 0LL)
    {
        int index;
#if defined(__X86__) || defined(__AMD64__)
        index = lnc_mchash(hdr->ether_dhost) >> 26;
        return ((uint8_t*)(pData->aCSR + 8))[index >> 3] & (1 << (index & 7));
#else
        uint8_t ladr[8];
        ladr[0] = pData->aCSR[8] & 0xff;
        ladr[1] = pData->aCSR[8] >> 8;
        ladr[2] = pData->aCSR[9] & 0xff;
        ladr[3] = pData->aCSR[9] >> 8;
        ladr[4] = pData->aCSR[10] & 0xff;
        ladr[5] = pData->aCSR[10] >> 8;
        ladr[6] = pData->aCSR[11] & 0xff;
        ladr[7] = pData->aCSR[11] >> 8;
        index = lnc_mchash(hdr->ether_dhost) >> 26;
        return (ladr[index >> 3] & (1 << (index & 7)));
#endif
    }
    return 0;
}

#endif /* IN_RING3 */

/**
 * Get the receive descriptor ring address with a given index.
 */
DECLINLINE(RTGCPHYS) pcnetRdraAddr(PCNetState *pData, int idx)
{
    return pData->GCRDRA + ((CSR_RCVRL(pData) - idx) << pData->iLog2DescSize);
}

/**
 * Get the transmit descriptor ring address with a given index.
 */
DECLINLINE(RTGCPHYS) pcnetTdraAddr(PCNetState *pData, int idx)
{
    return pData->GCTDRA + ((CSR_XMTRL(pData) - idx) << pData->iLog2DescSize);
}

__BEGIN_DECLS
PDMBOTHCBDECL(int) pcnetIOPortRead(PPDMDEVINS pDevIns, void *pvUser,
                                   RTIOPORT Port, uint32_t *pu32, unsigned cb);
PDMBOTHCBDECL(int) pcnetIOPortWrite(PPDMDEVINS pDevIns, void *pvUser,
                                    RTIOPORT Port, uint32_t u32, unsigned cb);
PDMBOTHCBDECL(int) pcnetIOPortAPromWrite(PPDMDEVINS pDevIns, void *pvUser,
                                         RTIOPORT Port, uint32_t u32, unsigned cb);
PDMBOTHCBDECL(int) pcnetIOPortAPromRead(PPDMDEVINS pDevIns, void *pvUser,
                                        RTIOPORT Port, uint32_t *pu32, unsigned cb);
PDMBOTHCBDECL(int) pcnetMMIORead(PPDMDEVINS pDevIns, void *pvUser,
                                 RTGCPHYS GCPhysAddr, void *pv, unsigned cb);
PDMBOTHCBDECL(int) pcnetMMIOWrite(PPDMDEVINS pDevIns, void *pvUser,
                                  RTGCPHYS GCPhysAddr, void *pv, unsigned cb);
#ifndef IN_RING3
DECLEXPORT(int) pcnetHandleRingWrite(PVM pVM, RTGCUINT uErrorCode, PCPUMCTXCORE pRegFrame,
                                     RTGCPTR pvFault, RTGCPHYS GCPhysFault, void *pvUser);
#endif
__END_DECLS

#undef htonl
#define htonl(x)    ASMByteSwapU32(x)
#undef htons
#define htons(x)    ( (((x) & 0xff00) >> 8) | (((x) & 0x00ff) << 8) )

static void     pcnetPollRxTx(PCNetState *pData);
static void     pcnetPollTimer(PCNetState *pData);
static void     pcnetUpdateIrq(PCNetState *pData);
static uint32_t pcnetBCRReadU16(PCNetState *pData, uint32_t u32RAP);
static int      pcnetBCRWriteU16(PCNetState *pData, uint32_t u32RAP, uint32_t val);


#ifdef PCNET_NO_POLLING
# ifndef IN_RING3

/**
 * #PF Virtual Handler callback for Guest write access to the ring descriptor page(pData)
 *
 * @return  VBox status code (appropriate for trap handling and GC return).
 * @param   pVM         VM Handle.
 * @param   uErrorCode  CPU Error code.
 * @param   pRegFrame   Trap register frame.
 * @param   pvFault     The fault address (cr2).
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 * @param   pvUser      User argument.
 */
DECLEXPORT(int) pcnetHandleRingWrite(PVM pVM, RTGCUINT uErrorCode, PCPUMCTXCORE pRegFrame,
                                     RTGCPTR pvFault, RTGCPHYS GCPhysFault, void *pvUser)
{
    PCNetState *pData   = (PCNetState *)pvUser;

    Log(("#%d pcnetHandleRingWriteGC: write to %08x\n", PCNETSTATE_2_DEVINS(pData)->iInstance, GCPhysFault));

    uint32_t cb;
    int rc = CTXALLSUFF(pData->pfnEMInterpretInstruction)(pVM, pRegFrame, pvFault, &cb);
    if (VBOX_SUCCESS(rc) && cb)
    {
        if (    (GCPhysFault >= pData->GCTDRA && GCPhysFault + cb < pcnetTdraAddr(pData, 0))
#ifdef PCNET_MONITOR_RECEIVE_RING
            ||  (GCPhysFault >= pData->GCRDRA && GCPhysFault + cb < pcnetRdraAddr(pData, 0))
#endif
           )
        {
            uint32_t offsetTDRA = (GCPhysFault - pData->GCTDRA);

            int rc = PDMCritSectEnter(&pData->CritSect, VERR_SEM_BUSY);
            if (VBOX_SUCCESS(rc))
            {
                STAM_COUNTER_INC(&CTXALLSUFF(pData->StatRingWrite)); ;

                /* Check if we can do something now */
                pcnetPollRxTx(pData);
                pcnetUpdateIrq(pData);

                PDMCritSectLeave(&pData->CritSect);
                return VINF_SUCCESS;
            }
        }
        else
        {
            STAM_COUNTER_INC(&CTXALLSUFF(pData->StatRingWriteOutsideRange)); ;
            return VINF_SUCCESS;    /* outside of the ring range */
        }
    }
    STAM_COUNTER_INC(&CTXALLSUFF(pData->StatRingWriteFailed)); ;
    return VINF_IOM_HC_MMIO_WRITE; /* handle in ring3 */
}

# else /* IN_RING3 */

/**
 * #PF Handler callback for physical access handler ranges (MMIO among others) in HC.
 *
 * The handler can not raise any faults, it's mainly for monitoring write access
 * to certain pages.
 *
 * @returns VINF_SUCCESS if the handler have carried out the operation.
 * @returns VINF_PGM_HANDLER_DO_DEFAULT if the caller should carry out the access operation.
 * @param   pVM             VM Handle.
 * @param   GCPhys          The physical address the guest is writing to.
 * @param   pvPhys          The HC mapping of that address.
 * @param   pvBuf           What the guest is reading/writing.
 * @param   cbBuf           How much it's reading/writing.
 * @param   enmAccessType   The access type.
 * @param   pvUser          User argument.
 */
static DECLCALLBACK(int) pcnetHandleRingWrite(PVM pVM, RTGCPHYS GCPhys, void *pvPhys, void *pvBuf,
                                              size_t cbBuf, PGMACCESSTYPE enmAccessType, void *pvUser)
{
    PPDMDEVINS  pDevIns = (PPDMDEVINS)pvUser;
    PCNetState *pData   = PDMINS2DATA(pDevIns, PCNetState *);

    Log(("#%d pcnetHandleRingWrite: write to %08x\n", PCNETSTATE_2_DEVINS(pData)->iInstance, GCPhys));
#ifdef VBOX_WITH_STATISTICS
    STAM_COUNTER_INC(&CTXSUFF(pData->StatRingWrite));
    if (GCPhys >= pData->GCRDRA && GCPhys < pcnetRdraAddr(pData, 0))
        STAM_COUNTER_INC(&pData->StatRCVRingWrite);
    else if (GCPhys >= pData->GCTDRA && GCPhys < pcnetTdraAddr(pData, 0))
        STAM_COUNTER_INC(&pData->StatTXRingWrite);
#endif
    /* Perform the actual write */
    memcpy((char *)pvPhys, pvBuf, cbBuf);

    /* Writes done by our code don't require polling of course */
    if (PDMCritSectIsOwner(&pData->CritSect) == false)
    {
        if (    (GCPhys >= pData->GCTDRA && GCPhys + cbBuf < pcnetTdraAddr(pData, 0))
#ifdef PCNET_MONITOR_RECEIVE_RING
            ||  (GCPhys >= pData->GCRDRA && GCPhys + cbBuf < pcnetRdraAddr(pData, 0))
#endif
           )
        {
            int rc = PDMCritSectEnter(&pData->CritSect, VERR_SEM_BUSY);
            AssertReleaseRC(rc);
            /* Check if we can do something now */
            pcnetPollRxTx(pData);
            pcnetUpdateIrq(pData);
            PDMCritSectLeave(&pData->CritSect);
        }
    }
    return VINF_SUCCESS;
}
# endif /* !IN_RING3 */
#endif /* PCNET_NO_POLLING */

static void pcnetSoftReset(PCNetState *pData)
{
    Log(("#%d pcnetSoftReset:\n", PCNETSTATE_2_DEVINS(pData)->iInstance));

    pData->u32Lnkst = 0x40;
    pData->GCRDRA   = 0;
    pData->GCTDRA   = 0;
    pData->u32RAP   = 0;

    pData->aBCR[BCR_BSBC] &= ~0x0080;

    pData->aCSR[0]   = 0x0004;
    pData->aCSR[3]   = 0x0000;
    pData->aCSR[4]   = 0x0115;
    pData->aCSR[5]   = 0x0000;
    pData->aCSR[6]   = 0x0000;
    pData->aCSR[8]   = 0;
    pData->aCSR[9]   = 0;
    pData->aCSR[10]  = 0;
    pData->aCSR[11]  = 0;
    pData->aCSR[12]  = le16_to_cpu(((uint16_t *)&pData->aPROM[0])[0]);
    pData->aCSR[13]  = le16_to_cpu(((uint16_t *)&pData->aPROM[0])[1]);
    pData->aCSR[14]  = le16_to_cpu(((uint16_t *)&pData->aPROM[0])[2]);
    pData->aCSR[15] &= 0x21c4;
    CSR_RCVRC(pData) = 1;
    CSR_XMTRC(pData) = 1;
    CSR_RCVRL(pData) = 1;
    CSR_XMTRL(pData) = 1;
    pData->aCSR[80]  = 0x1410;
    pData->aCSR[88]  = pData->fAm79C973 ? CSR_VERSION_LOW_79C973 : CSR_VERSION_LOW_79C970A;
    pData->aCSR[89]  = CSR_VERSION_HIGH;
    pData->aCSR[94]  = 0x0000;
    pData->aCSR[100] = 0x0200;
    pData->aCSR[103] = 0x0105;
    pData->aCSR[103] = 0x0105;
    CSR_MISSC(pData) = 0;
    pData->aCSR[114] = 0x0000;
    pData->aCSR[122] = 0x0000;
    pData->aCSR[124] = 0x0000;
}

/**
 * Check if we have to send an interrupt to the guest. An interrupt can occur on
 * - csr0 (written quite often)
 * - csr4 (only written by pcnetSoftReset(), pcnetStop() or by the guest driver)
 * - csr5 (only written by pcnetSoftReset(), pcnetStop or by the driver guest)
 */
static void pcnetUpdateIrq(PCNetState *pData)
{
    register int      iISR = 0;
    register uint16_t csr0 = pData->aCSR[0];

    csr0 &= ~0x0080; /* clear INTR */

    STAM_PROFILE_ADV_START(&pData->StatInterrupt, a);

    /* Linux guests set csr4=0x0915
     * W2k   guests set csr3=0x4940 (disable BABL, MERR, IDON, DXSUFLO */

#if 1
    if (    ( (csr0               & ~pData->aCSR[3]) & 0x5f00)
         || (((pData->aCSR[4]>>1) & ~pData->aCSR[4]) & 0x0115)
         || (((pData->aCSR[5]>>1) &  pData->aCSR[5]) & 0x0048))
#else
    if (  ( !(pData->aCSR[3] & 0x4000) && !!(csr0           & 0x4000)) /* BABL */
        ||( !(pData->aCSR[3] & 0x1000) && !!(csr0           & 0x1000)) /* MISS */
        ||( !(pData->aCSR[3] & 0x0100) && !!(csr0           & 0x0100)) /* IDON */
        ||( !(pData->aCSR[3] & 0x0200) && !!(csr0           & 0x0200)) /* TINT */
        ||( !(pData->aCSR[3] & 0x0400) && !!(csr0           & 0x0400)) /* RINT */
        ||( !(pData->aCSR[3] & 0x0800) && !!(csr0           & 0x0800)) /* MERR */
        ||( !(pData->aCSR[4] & 0x0001) && !!(pData->aCSR[4] & 0x0002)) /* JAB */
        ||( !(pData->aCSR[4] & 0x0004) && !!(pData->aCSR[4] & 0x0008)) /* TXSTRT */
        ||( !(pData->aCSR[4] & 0x0010) && !!(pData->aCSR[4] & 0x0020)) /* RCVO */
        ||( !(pData->aCSR[4] & 0x0100) && !!(pData->aCSR[4] & 0x0200)) /* MFCO */
        ||(!!(pData->aCSR[5] & 0x0040) && !!(pData->aCSR[5] & 0x0080)) /* EXDINT */
        ||(!!(pData->aCSR[5] & 0x0008) && !!(pData->aCSR[5] & 0x0010)) /* MPINT */)
#endif
    {
        iISR = !!(csr0 & 0x0040); /* CSR_INEA */
        csr0 |= 0x0080; /* set INTR */
    }

#ifdef VBOX
    if (pData->aCSR[4] & 0x0080) /* UINTCMD */
    {
        pData->aCSR[4] &= ~0x0080; /* clear UINTCMD */
        pData->aCSR[4] |=  0x0040; /* set UINT */
        Log(("#%d user int\n", PCNETSTATE_2_DEVINS(pData)->iInstance));
    }
    if (pData->aCSR[4] & csr0 & 0x0040 /* CSR_INEA */)
    {
        csr0 |=  0x0080; /* set INTR */
        iISR = 1;
    }
#else /* !VBOX */
    if (!!(pData->aCSR[4] & 0x0080) && CSR_INEA(pData)) /* UINTCMD */
    {
        pData->aCSR[4] &= ~0x0080;
        pData->aCSR[4] |=  0x0040; /* set UINT */
        csr0           |=  0x0080; /* set INTR */
        iISR = 1;
        Log(("#%d user int\n", PCNETSTATE_2_DEVINS(pData)->iInstance));
    }
#endif /* !VBOX */

#if 1
    if (((pData->aCSR[5]>>1) & pData->aCSR[5]) & 0x0500)
#else
    if (   (!!(pData->aCSR[5] & 0x0400) && !!(pData->aCSR[5] & 0x0800)) /* SINT */
         ||(!!(pData->aCSR[5] & 0x0100) && !!(pData->aCSR[5] & 0x0200)) /* SLPINT */)
#endif
    {
        iISR = 1;
        csr0 |= 0x0080; /* INTR */
    }

    pData->aCSR[0] = csr0;

    Log2(("#%d set irq iISR=%d\n", PCNETSTATE_2_DEVINS(pData)->iInstance, iISR));

    /* normal path is to _not_ change the IRQ status */
    if (RT_UNLIKELY(iISR != pData->iISR))
    {
        Log(("#%d INTA=%d\n", PCNETSTATE_2_DEVINS(pData)->iInstance, iISR));
        PDMDevHlpPCISetIrqNoWait(PCNETSTATE_2_DEVINS(pData), 0, iISR);
        pData->iISR = iISR;
    }
    STAM_PROFILE_ADV_STOP(&pData->StatInterrupt, a);
}

#ifdef IN_RING3
#ifdef PCNET_NO_POLLING
static void pcnetUpdateRingHandlers(PCNetState *pData)
{
    PPDMDEVINS pDevIns = PCNETSTATE_2_DEVINS(pData);
    int rc;

    Log(("pcnetUpdateRingHandlers TD %VGp size %x -> %VGp size %x\n", pData->TDRAPhysOld, pData->cbTDRAOld, pData->GCTDRA, pcnetTdraAddr(pData, 0)));
    Log(("pcnetUpdateRingHandlers RX %VGp size %x -> %VGp size %x\n", pData->RDRAPhysOld, pData->cbRDRAOld, pData->GCRDRA, pcnetRdraAddr(pData, 0)));

    /** @todo unregister order not correct! */

#ifdef PCNET_MONITOR_RECEIVE_RING
    if (pData->GCRDRA != pData->RDRAPhysOld || CSR_RCVRL(pData) != pData->cbRDRAOld)
    {
        if (pData->RDRAPhysOld != 0)
            PGMHandlerPhysicalDeregister(PDMDevHlpGetVM(pDevIns),
                                        pData->RDRAPhysOld & ~PAGE_OFFSET_MASK);

        rc = PGMR3HandlerPhysicalRegister(PDMDevHlpGetVM(pDevIns),
                                          PGMPHYSHANDLERTYPE_PHYSICAL_WRITE,
                                          pData->GCRDRA & ~PAGE_OFFSET_MASK,
                                          RT_ALIGN(pcnetRdraAddr(pData, 0), PAGE_SIZE) - 1,
                                          pcnetHandleRingWrite, pDevIns,
                                          g_DevicePCNet.szR0Mod, "pcnetHandleRingWrite",
                                          pData->pDevInsHC->pvInstanceDataHC,
                                          g_DevicePCNet.szGCMod, "pcnetHandleRingWrite",
                                          pData->pDevInsHC->pvInstanceDataGC,
                                          "PCNet receive ring write access handler");
        AssertRC(rc);

        pData->RDRAPhysOld = pData->GCRDRA;
        pData->cbRDRAOld   = pcnetRdraAddr(pData, 0);
    }
#endif /* PCNET_MONITOR_RECEIVE_RING */

#ifdef PCNET_MONITOR_RECEIVE_RING
    /* 3 possibilities:
     * 1) TDRA on different physical page as RDRA
     * 2) TDRA completely on same physical page as RDRA
     * 3) TDRA & RDRA overlap partly with different physical pages
     */
    RTGCPHYS RDRAPageStart = pData->GCRDRA & ~PAGE_OFFSET_MASK;
    RTGCPHYS RDRAPageEnd   = (pcnetRdraAddr(pData, 0) - 1) & ~PAGE_OFFSET_MASK;
    RTGCPHYS TDRAPageStart = pData->GCTDRA & ~PAGE_OFFSET_MASK;
    RTGCPHYS TDRAPageEnd   = (pcnetTdraAddr(pData, 0) - 1) & ~PAGE_OFFSET_MASK;

    if (    RDRAPageStart > TDRAPageEnd
        ||  TDRAPageStart > RDRAPageEnd)
    {
#endif /* PCNET_MONITOR_RECEIVE_RING */
        /* 1) */
        if (pData->GCTDRA != pData->TDRAPhysOld || CSR_XMTRL(pData) != pData->cbTDRAOld)
        {
            if (pData->TDRAPhysOld != 0)
                PGMHandlerPhysicalDeregister(PDMDevHlpGetVM(pDevIns),
                                             pData->TDRAPhysOld & ~PAGE_OFFSET_MASK);

            rc = PGMR3HandlerPhysicalRegister(PDMDevHlpGetVM(pDevIns),
                                              PGMPHYSHANDLERTYPE_PHYSICAL_WRITE,
                                              pData->GCTDRA & ~PAGE_OFFSET_MASK,
                                              RT_ALIGN(pcnetTdraAddr(pData, 0), PAGE_SIZE) - 1,
                                              pcnetHandleRingWrite, pDevIns,
                                              g_DevicePCNet.szR0Mod, "pcnetHandleRingWrite",
                                              pData->pDevInsHC->pvInstanceDataHC,
                                              g_DevicePCNet.szGCMod, "pcnetHandleRingWrite",
                                              pData->pDevInsHC->pvInstanceDataGC,
                                              "PCNet transmit ring write access handler");
            AssertRC(rc);

            pData->TDRAPhysOld = pData->GCTDRA;
            pData->cbTDRAOld   = pcnetTdraAddr(pData, 0);
        }
#ifdef PCNET_MONITOR_RECEIVE_RING
    }
    else
    if (    RDRAPageStart != TDRAPageStart
        &&  (   TDRAPageStart == RDRAPageEnd
             || TDRAPageEnd   == RDRAPageStart
            )
        )
    {
        /* 3) */
        AssertFailed();
    }
    /* else 2) */
#endif
}
#endif /* PCNET_NO_POLLING */

static void pcnetInit(PCNetState *pData)
{
    PPDMDEVINS pDevIns = PCNETSTATE_2_DEVINS(pData);
    Log(("#%d pcnetInit: init_addr=0x%08x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         PHYSADDR(pData, CSR_IADR(pData))));

    /** @todo Documentation says that RCVRL and XMTRL are stored as two's complement!
     *        Software is allowed to write these registers directly. */
#define PCNET_INIT() do { \
        PDMDevHlpPhysRead(pDevIns, PHYSADDR(pData, CSR_IADR(pData)),         \
                          (uint8_t *)&initblk, sizeof(initblk));             \
        pData->aCSR[15]  = le16_to_cpu(initblk.mode);                        \
        CSR_RCVRL(pData) = (initblk.rlen < 9) ? (1 << initblk.rlen) : 512;   \
        CSR_XMTRL(pData) = (initblk.tlen < 9) ? (1 << initblk.tlen) : 512;   \
        pData->aCSR[ 6]  = (initblk.tlen << 12) | (initblk.rlen << 8);       \
        pData->aCSR[ 8]  = le16_to_cpu(initblk.ladrf1);                      \
        pData->aCSR[ 9]  = le16_to_cpu(initblk.ladrf2);                      \
        pData->aCSR[10]  = le16_to_cpu(initblk.ladrf3);                      \
        pData->aCSR[11]  = le16_to_cpu(initblk.ladrf4);                      \
        pData->aCSR[12]  = le16_to_cpu(initblk.padr1);                       \
        pData->aCSR[13]  = le16_to_cpu(initblk.padr2);                       \
        pData->aCSR[14]  = le16_to_cpu(initblk.padr3);                       \
        pData->GCRDRA    = PHYSADDR(pData, initblk.rdra);                    \
        pData->GCTDRA    = PHYSADDR(pData, initblk.tdra);                    \
} while (0)

    if (BCR_SSIZE32(pData))
    {
        struct INITBLK32 initblk;
        pData->GCUpperPhys = 0;
        PCNET_INIT();
        Log(("#%d initblk.rlen=0x%02x, initblk.tlen=0x%02x\n",
             PCNETSTATE_2_DEVINS(pData)->iInstance, initblk.rlen, initblk.tlen));
    }
    else
    {
        struct INITBLK16 initblk;
        pData->GCUpperPhys = (0xff00 & (uint32_t)pData->aCSR[2]) << 16;
        PCNET_INIT();
        Log(("#%d initblk.rlen=0x%02x, initblk.tlen=0x%02x\n",
             PCNETSTATE_2_DEVINS(pData)->iInstance, initblk.rlen, initblk.tlen));
    }

#undef PCNET_INIT

    if (pData->pDrv)
        pData->pDrv->pfnSetPromiscuousMode(pData->pDrv, CSR_PROM(pData));

    CSR_RCVRC(pData) = CSR_RCVRL(pData);
    CSR_XMTRC(pData) = CSR_XMTRL(pData);

#ifdef PCNET_NO_POLLING
    pcnetUpdateRingHandlers(pData);
#endif

    /* Reset cached RX and TX states */
    CSR_CRST(pData) = CSR_CRBC(pData) = CSR_NRST(pData) = CSR_NRBC(pData) = 0;
    CSR_CXST(pData) = CSR_CXBC(pData) = CSR_NXST(pData) = CSR_NXBC(pData) = 0;

    LogRel(("PCNet#%d: Init: ss32=%d GCRDRA=0x%08x[%d] GCTDRA=0x%08x[%d]\n",
            PCNETSTATE_2_DEVINS(pData)->iInstance, BCR_SSIZE32(pData),
            pData->GCRDRA, CSR_RCVRL(pData), pData->GCTDRA, CSR_XMTRL(pData)));

    pData->aCSR[0] |=  0x0101;       /* Initialization done */
    pData->aCSR[0] &= ~0x0004;       /* clear STOP bit */
}
#endif /* IN_RING3 */

/**
 * Start RX/TX operation.
 */
static void pcnetStart(PCNetState *pData)
{
    Log(("%#d pcnetStart:\n", PCNETSTATE_2_DEVINS(pData)->iInstance));
    if (!CSR_DTX(pData))
        pData->aCSR[0] |= 0x0010;    /* set TXON */
    if (!CSR_DRX(pData))
        pData->aCSR[0] |= 0x0020;    /* set RXON */
    pData->aCSR[0] &= ~0x0004;       /* clear STOP bit */
    pData->aCSR[0] |=  0x0002;       /* STRT */
}

/**
 * Stop RX/TX operation.
 */
static void pcnetStop(PCNetState *pData)
{
    Log(("#%d pcnetStop:\n", PCNETSTATE_2_DEVINS(pData)->iInstance));
    pData->aCSR[0] &= ~0x7feb;
    pData->aCSR[0] |=  0x0014;
    pData->aCSR[4] &= ~0x02c2;
    pData->aCSR[5] &= ~0x0011;
    pcnetPollTimer(pData);
}

#ifdef IN_RING3
static DECLCALLBACK(bool) pcnetCanRxQueueConsumer(PPDMDEVINS pDevIns, PPDMQUEUEITEMCORE pItem)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);
    pData->pDrv->pfnNotifyCanReceive(pData->pDrv);
    return true;
}
#endif

/**
 * Poll Receive Descriptor Table Entry and cache the results in the appropriate registers.
 * Note: Once a descriptor belongs to the network card (this driver), it cannot be changed
 * by the host (the guest driver) anymore. Well, it could but the results are undefined by
 * definition.
 * @param  fSkipCurrent       if true, don't scan the current RDTE.
 */
static void pcnetRdtePoll(PCNetState *pData, bool fSkipCurrent=false)
{
    STAM_PROFILE_ADV_START(&pData->CTXSUFF(StatRdtePoll), a);
    /* assume lack of a next receive descriptor */
    CSR_NRST(pData) = 0;

    if (RT_LIKELY(pData->GCRDRA))
    {
        /*
         * The current receive message descriptor.
         */
        RMD      rmd;
        int      i = CSR_RCVRC(pData);
        RTGCPHYS addr;

        if (i < 1)
            i = CSR_RCVRL(pData);

        if (!fSkipCurrent)
        {
            addr = pcnetRdraAddr(pData, i);
            pcnetRmdLoad(pData, &rmd, PHYSADDR(pData, addr));
            CSR_CRDA(pData) = CSR_CRBA(pData) = 0;
            CSR_CRBC(pData) = CSR_CRST(pData) = 0;
            if (!rmd.rmd1.own)
            {
                STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatRdtePoll), a);
                return;
            }
            if (RT_LIKELY(!IS_RMD_BAD(rmd)))
            {
                CSR_CRDA(pData) = addr;                        /* Receive Descriptor Address */
                CSR_CRBA(pData) = rmd.rmd0.rbadr;              /* Receive Buffer Address */
                CSR_CRBC(pData) = rmd.rmd1.bcnt;               /* Receive Byte Count */
                CSR_CRST(pData) = ((uint32_t *)&rmd)[1] >> 16; /* Receive Status */
#ifdef IN_RING3
                pData->pDrv->pfnNotifyCanReceive(pData->pDrv);
#else
                PPDMQUEUEITEMCORE pItem = PDMQueueAlloc(CTXSUFF(pData->pCanRxQueue));
                if (pItem)
                    PDMQueueInsert(CTXSUFF(pData->pCanRxQueue), pItem);
#endif
            }
            else
            {
                STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatRdtePoll), a);
                /* This is not problematic since we don't own the descriptor */
                LogRel(("PCNet#%d: BAD RMD ENTRIES AT 0x%08x (i=%d)\n",
                        PCNETSTATE_2_DEVINS(pData)->iInstance, addr, i));
                return;
            }
        }

        /*
         * The next descriptor.
         */
        if (--i < 1)
            i = CSR_RCVRL(pData);
        addr = pcnetRdraAddr(pData, i);
        pcnetRmdLoad(pData, &rmd, PHYSADDR(pData, addr));
        CSR_NRDA(pData) = CSR_NRBA(pData) = 0;
        CSR_NRBC(pData) = 0;
        if (!rmd.rmd1.own)
        {
            STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatRdtePoll), a);
            return;
        }
        if (RT_LIKELY(!IS_RMD_BAD(rmd)))
        {
            CSR_NRDA(pData) = addr;                         /* Receive Descriptor Address */
            CSR_NRBA(pData) = rmd.rmd0.rbadr;               /* Receive Buffer Address */
            CSR_NRBC(pData) = rmd.rmd1.bcnt;                /* Receive Byte Count */
            CSR_NRST(pData) = ((uint32_t *)&rmd)[1] >> 16;  /* Receive Status */
        }
        else
        {
            STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatRdtePoll), a);
            /* This is not problematic since we don't own the descriptor */
            LogRel(("PCNet#%d: BAD RMD ENTRIES + AT 0x%08x (i=%d)\n",
                    PCNETSTATE_2_DEVINS(pData)->iInstance, addr, i));
            return;
        }

        /**
         * @todo NNRD
         */
    }
    else
    {
        CSR_CRDA(pData) = CSR_CRBA(pData) = CSR_NRDA(pData) = CSR_NRBA(pData) = 0;
        CSR_CRBC(pData) = CSR_NRBC(pData) = CSR_CRST(pData) = 0;
    }
    STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatRdtePoll), a);
}

/**
 * Poll Transmit Descriptor Table Entry
 * @return true if transmit descriptors available
 */
static int pcnetTdtePoll(PCNetState *pData, TMD *tmd)
{
    STAM_PROFILE_ADV_START(&pData->CTXSUFF(StatTdtePoll), a);
    if (RT_LIKELY(pData->GCTDRA))
    {
        RTGCPHYS cxda = pcnetTdraAddr(pData, CSR_XMTRC(pData));

        pcnetTmdLoad(pData, tmd, PHYSADDR(pData, cxda));

        if (!tmd->tmd1.own)
        {
            STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatTdtePoll), a);
            return 0;
        }

        if (RT_UNLIKELY(tmd->tmd1.ones != 15))
        {
            STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatTdtePoll), a);
            LogRel(("PCNet#%d: BAD TMD XDA=0x%08x\n",
                    PCNETSTATE_2_DEVINS(pData)->iInstance, PHYSADDR(pData, cxda)));
            return 0;
        }

        /* previous xmit descriptor */
        CSR_PXDA(pData) = CSR_CXDA(pData);
        CSR_PXBC(pData) = CSR_CXBC(pData);
        CSR_PXST(pData) = CSR_CXST(pData);

        /* set current trasmit decriptor. */
        CSR_CXDA(pData) = cxda;
        CSR_CXBC(pData) = tmd->tmd1.bcnt;
        CSR_CXST(pData) = ((uint32_t *)tmd)[1] >> 16;
        STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatTdtePoll), a);
        return CARD_IS_OWNER(CSR_CXST(pData));
    }
    else
    {
        /** @todo consistency with previous receive descriptor */
        CSR_CXDA(pData) = 0;
        CSR_CXBC(pData) = CSR_CXST(pData) = 0;
        STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatTdtePoll), a);
        return 0;
    }
}


#ifdef IN_RING3

/**
 * Check if there is at least one free receive buffer available.
 */
static int pcnetCanReceiveNoSync(PCNetState *pData)
{
    if (RT_UNLIKELY(CSR_DRX(pData) || CSR_STOP(pData) || CSR_SPND(pData)))
        return 0;

    if (HOST_IS_OWNER(CSR_CRST(pData)) && pData->GCRDRA)
        pcnetRdtePoll(pData);

    if (HOST_IS_OWNER(CSR_CRST(pData)))
    {
        /** @todo Notify the guest _now_. Will potentially increase the interrupt load */
        pData->aCSR[0] |= 0x1000; /* Set MISS flag */
        return 0;
    }

    /* byte count stored in two's complement 12 bits wide */
    Log(("#%d pcnetCanReceiveNoSync %d bytes\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         4096 - CSR_CRBC(pData)));
    return 4096 - CSR_CRBC(pData);
}

/**
 * Write data into guest receive buffers.
 */
static void pcnetReceiveNoSync(PCNetState *pData, const uint8_t *buf, int size)
{
    PPDMDEVINS pDevIns = PCNETSTATE_2_DEVINS(pData);
    int is_padr = 0, is_bcast = 0, is_ladr = 0;

    if (RT_UNLIKELY(CSR_DRX(pData) || CSR_STOP(pData) || CSR_SPND(pData) || !size))
        return;

    Log(("#%d pcnetReceiveNoSync: size=%d\n", PCNETSTATE_2_DEVINS(pData)->iInstance, size));

    LOG_PACKET("rraw", buf, size);

    /*
     * Perform address matching.
     */
    if (   CSR_PROM(pData)
        || (is_padr  = padr_match(pData, buf, size))
        || (is_bcast = padr_bcast(pData, buf, size))
        || (is_ladr  = ladr_match(pData, buf, size)))
    {
        if (HOST_IS_OWNER(CSR_CRST(pData)))
            pcnetRdtePoll(pData);
        if (RT_UNLIKELY(HOST_IS_OWNER(CSR_CRST(pData))))
        {
            /* Not owned by controller. This should not be possible as
             * we already called pcnetCanReceive(). */
            LogRel(("PCNet#%d: no buffer: RCVRC=%d\n",
                    PCNETSTATE_2_DEVINS(pData)->iInstance, CSR_RCVRC(pData)));
            /* Dump the status of all RX descriptors */
            const unsigned  cb = 1 << pData->iLog2DescSize;
            RTGCPHYS        GCPhys = pData->GCRDRA;
            unsigned        i = CSR_RCVRL(pData);
            while (i-- > 0)
            {
                RMD rmd;
                pcnetRmdLoad(pData, &rmd, PHYSADDR(pData, GCPhys));
                LogRel(("  %08x\n", rmd.rmd1));
                GCPhys += cb;
            }
            pData->aCSR[0] |= 0x1000; /* Set MISS flag */
            CSR_MISSC(pData)++;
        }
        else
        {
            uint8_t *src = &pData->abRecvBuf[8];
            RTGCPHYS crda = CSR_CRDA(pData);
            RMD      rmd;
            int      pktcount = 0;

            memcpy(src, buf, size);
            if (!CSR_ASTRP_RCV(pData))
            {
                uint32_t fcs = ~0;
                uint8_t *p = src;

                while (size < 60)
                    src[size++] = 0;
                while (p != &src[size])
                    CRC(fcs, *p++);
                ((uint32_t *)&src[size])[0] = htonl(fcs);
                /* FCS at end of packet */
            }
            size += 4;

#ifdef PCNET_DEBUG_MATCH
            PRINT_PKTHDR(buf);
#endif

            pcnetRmdLoad(pData, &rmd, PHYSADDR(pData, crda));
            /*if (!CSR_LAPPEN(pData))*/
                rmd.rmd1.stp = 1;

            int count = RT_MIN(4096 - (int)rmd.rmd1.bcnt, size);
            RTGCPHYS rbadr = PHYSADDR(pData, rmd.rmd0.rbadr);
            PDMDevHlpPhysWrite(pDevIns, rbadr, src, count);
            src  += count;
            size -= count;
            rmd.rmd2.mcnt = count;
            pktcount++;
            if (size > 0)
            {
                if (HOST_IS_OWNER(CSR_NRST(pData)))
                {
                    /* From the manual: ``Regardless of ownership of the second receive
                     * descriptor, the Am79C972 controller will continue to perform receive
                     * data DMA transfers to the first buffer. If the frame length exceeds
                     * the length of the first buffer, and the Am79C972 controller does not
                     * own the second buffer, ownership of the current descriptor will be
                     * passed back to the system by writing a 0 to the OWN bit of RMD1.
                     * Status will be written indicating buffer (BUFF = 1) and possibly
                     * overflow (OFLO = 1) errors.
                     * If the frame length exceeds the length of the first (current) buffer,
                     * and the Am79C972 controller does own the second (next) buffer,
                     * ownership will be passed back to the system by writing a 0 to the OWN
                     * bit of RMD1 when the first buffer is full. The OWN bit is the only bit
                     * modified in the descriptor. Receive data transfers to the second buffer
                     * may occur before the Am79C972 controller proceeds to look ahead to the
                     * ownership of the third buffer. Such action will depend upon the state
                     * of the FIFO when the OWN bit has been updated in the first descriptor.
                     * In any case, lookahead will be performed to the third buffer and the
                     * information gathered will be stored in the chip, regardless of the state
                     * of the ownership bit.'' */
                    pcnetRdtePoll(pData, true);
                }
                if (CARD_IS_OWNER(CSR_NRST(pData)))
                {
                    /* write back, clear the own bit */
                    pcnetRmdStorePassHost(pData, &rmd, PHYSADDR(pData, crda));
                    crda  = CSR_NRDA(pData);
                    pcnetRmdLoad(pData, &rmd, PHYSADDR(pData, crda));
                    count = RT_MIN(4096 - (int)rmd.rmd1.bcnt, size);
                    rbadr = PHYSADDR(pData, rmd.rmd0.rbadr);
                    PDMDevHlpPhysWrite(pDevIns, rbadr, src, count);
                    src  += count;
                    size -= count;
                    rmd.rmd2.mcnt = count;
                    pktcount++;
                }
            }

            if (RT_LIKELY(size == 0))
            {
                rmd.rmd1.enp  = 1;
                rmd.rmd1.pam  = !CSR_PROM(pData) && is_padr;
                rmd.rmd1.lafm = !CSR_PROM(pData) && is_ladr;
                rmd.rmd1.bam  = !CSR_PROM(pData) && is_bcast;
            }
            else
            {
                LogRel(("PCNet#%d: Overflow by %ubytes\n",
                         PCNETSTATE_2_DEVINS(pData)->iInstance, size));
                rmd.rmd1.oflo = 1;
                rmd.rmd1.buff = 1;
                rmd.rmd1.err  = 1;
            }
            /* write back, clear the own bit */
            pcnetRmdStorePassHost(pData, &rmd, PHYSADDR(pData, crda));

            pData->aCSR[0] |= 0x0400;

            Log(("#%d RCVRC=%d CRDA=0x%08x BLKS=%d\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
                 CSR_RCVRC(pData), PHYSADDR(pData, CSR_CRDA(pData)), pktcount));
#ifdef PCNET_DEBUG_RMD
            PRINT_RMD(&rmd);
#endif

            while (pktcount--)
            {
                if (CSR_RCVRC(pData) < 2)
                    CSR_RCVRC(pData) = CSR_RCVRL(pData);
                else
                    CSR_RCVRC(pData)--;
            }
            /* guest driver is owner: force repoll of current and next RDTEs */
            CSR_CRST(pData) = 0;
        }
    }

    /* see description of TXDPOLL:
     * ``transmit polling will take place following receive activities'' */
    pcnetPollRxTx(pData);
    pcnetUpdateIrq(pData);
}


/**
 * Checks if the link is up.
 * @returns true if the link is up.
 * @returns false if the link is down.
 */
DECLINLINE(bool) pcnetIsLinkUp(PCNetState *pData)
{
    return pData->pDrv && !pData->fLinkTempDown && pData->fLinkUp;
}


/**
 * Transmit queue consumer
 * This is just a very simple way of delaying sending to R3.
 *
 * @returns Success indicator.
 *          If false the item will not be removed and the flushing will stop.
 * @param   pDevIns     The device instance.
 * @param   pItem       The item to consume. Upon return this item will be freed.
 */
static DECLCALLBACK(bool) pcnetXmitQueueConsumer(PPDMDEVINS pDevIns, PPDMQUEUEITEMCORE pItem)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);
    NOREF(pItem);

    /* Clear counter .*/
    ASMAtomicAndU32(&pData->cPendingSends, 0);
    int rc = RTSemEventSignal(pData->hSendEventSem);
    AssertRC(rc);
    return true;
}


/**
 * Scraps the top frame.
 * This is done as a precaution against mess left over by on
 */
DECLINLINE(void) pcnetXmitScrapFrame(PCNetState *pData)
{
    pData->SendFrame.pvBuf = NULL;
    pData->SendFrame.cb    = -1;
}


/**
 * If we are in RING3 don't copy the frame from GC here but only store the address. We
 * don't need to buffer the frames because a direct address translation was possible.
 */
DECLINLINE(void) pcnetXmitZeroCopyFrame(PCNetState *pData, RTR3PTR pv, const unsigned cbFrame)
{
    pData->SendFrame.pvBuf = pv;
    pData->SendFrame.cb    = cbFrame;
}


/**
 * Reads the first part of a frame
 */
DECLINLINE(void) pcnetXmitRead1st(PCNetState *pData, RTGCPHYS GCPhysFrame, const unsigned cbFrame)
{
    Assert(cbFrame < sizeof(pData->abSendBuf));

    PDMDevHlpPhysRead(pData->CTXSUFF(pDevIns), GCPhysFrame,
                      &pData->abSendBuf[0],
                      cbFrame);
    pData->SendFrame.pvBuf = pData->abSendBuf;
    pData->SendFrame.cb    = cbFrame;
}


/**
 * Reads more into the current frame.
 */
DECLINLINE(void) pcnetXmitReadMore(PCNetState *pData, RTGCPHYS GCPhysFrame, const unsigned cbFrame)
{
    Assert(pData->SendFrame.cb + cbFrame < sizeof(pData->abSendBuf));
    PDMDevHlpPhysRead(pData->CTXSUFF(pDevIns), GCPhysFrame,
                      &pData->abSendBuf[pData->SendFrame.cb],
                      cbFrame);
    pData->SendFrame.cb += cbFrame;
}


/**
 * Completes the current frame.
 * If we've reached the maxium number of frames, they will be flushed.
 */
DECLINLINE(int) pcnetXmitCompleteFrame(PCNetState *pData)
{
    /* Don't hold the critical section while transmitting data. */
    /** @note also avoids deadlocks with NAT as it can call us right back. */
    PDMCritSectLeave(&pData->CritSect);

    STAM_PROFILE_ADV_START(&pData->StatTransmitSend, a);
    pData->pDrv->pfnSend(pData->pDrv, pData->SendFrame.pvBuf, pData->SendFrame.cb);
    STAM_PROFILE_ADV_STOP(&pData->StatTransmitSend, a);

    return PDMCritSectEnter(&pData->CritSect, VERR_PERMISSION_DENIED);
}


/**
 * Fails a TMD with a link down error.
 */
static void pcnetXmitFailTMDLinkDown(PCNetState *pData, TMD *pTmd)
{
    /* make carrier error - hope this is correct. */
    pData->cLinkDownReported++;
    pTmd->tmd2.lcar = pTmd->tmd1.err = 1;
    pData->aCSR[0] |= BIT(15) | BIT(13); /* ERR | CERR */
    pData->Led.Asserted.s.fError = pData->Led.Actual.s.fError = 1;
    Log(("#%d pcnetTransmit: Signaling send error. swstyle=%#x\n",
         PCNETSTATE_2_DEVINS(pData)->iInstance, pData->aBCR[BCR_SWS]));
}

/**
 * Fails a TMD with a generic error.
 */
static void pcnetXmitFailTMDGeneric(PCNetState *pData, TMD *pTmd)
{
    /* make carrier error - hope this is correct. */
    pTmd->tmd2.lcar = pTmd->tmd1.err = 1;
    pData->aCSR[0] |= BIT(15) | BIT(13); /* ERR | CERR */
    pData->Led.Asserted.s.fError = pData->Led.Actual.s.fError = 1;
    Log(("#%d pcnetTransmit: Signaling send error. swstyle=%#x\n",
         PCNETSTATE_2_DEVINS(pData)->iInstance, pData->aBCR[BCR_SWS]));
}


/**
 * Transmit a loopback frame.
 */
DECLINLINE(void) pcnetXmitLoopbackFrame(PCNetState *pData)
{
    pData->Led.Asserted.s.fReading = pData->Led.Actual.s.fReading = 1;
    if (HOST_IS_OWNER(CSR_CRST(pData)))
        pcnetRdtePoll(pData);

    Assert(pData->SendFrame.pvBuf);
    pcnetReceiveNoSync(pData, (const uint8_t *)pData->SendFrame.pvBuf, pData->SendFrame.cb);
    pcnetXmitScrapFrame(pData);
    pData->Led.Actual.s.fReading = 0;
}

/**
 * Flushes queued frames.
 */
DECLINLINE(void) pcnetXmitFlushFrames(PCNetState *pData)
{
    pcnetXmitQueueConsumer(CTXSUFF(pData->pDevIns), NULL);
}

#endif /* IN_RING3 */



/**
 * Try to transmit frames
 */
static void pcnetTransmit(PCNetState *pData)
{
    if (RT_UNLIKELY(!CSR_TXON(pData)))
    {
        pData->aCSR[0] &= ~0x0008; /* Clear TDMD */
        return;
    }

    /*
     * Check the current transmit descriptors.
     */
    TMD tmd;
    if (!pcnetTdtePoll(pData, &tmd))
        return;

    /* Update TDMD, TXSTRT and TINT. */
    pData->aCSR[0] &= ~0x0008;       /* clear TDMD */

    /*
     * If we're in Ring-3 we should flush the queue now, in GC/R0 we'll queue a flush job.
     */
#ifdef IN_RING3
    pcnetXmitFlushFrames(pData);
#else
# if 1
    PPDMQUEUEITEMCORE pItem = PDMQueueAlloc(CTXSUFF(pData->pXmitQueue));
    if (RT_UNLIKELY(pItem))
        PDMQueueInsert(CTXSUFF(pData->pXmitQueue), pItem);
# else
    if (ASMAtomicIncU32(&pData->cPendingSends) < 16)
    {
        PPDMQUEUEITEMCORE pItem = PDMQueueAlloc(CTXSUFF(pData->pXmitQueue));
        if (RT_UNLIKELY(pItem))
            PDMQueueInsert(CTXSUFF(pData->pXmitQueue), pItem);
    }
    else
        PDMQueueFlush(CTXSUFF(pData->pXmitQueue));
# endif
#endif
}

#ifdef IN_RING3

/**
 * Try to transmit frames
 */
static int pcnetAsyncTransmit(PCNetState *pData)
{
    unsigned cFlushIrq = 0;

    Assert(PDMCritSectIsOwner(&pData->CritSect));

    if (RT_UNLIKELY(!CSR_TXON(pData)))
    {
        pData->aCSR[0] &= ~0x0008; /* Clear TDMD */
        return VINF_SUCCESS;
    }

    /*
     * Iterate the transmit descriptors.
     */
    STAM_PROFILE_ADV_START(&pData->StatTransmit, a);
    do
    {
#ifdef VBOX_WITH_STATISTICS
        unsigned cBuffers = 1;
#endif
        TMD tmd;
        if (!pcnetTdtePoll(pData, &tmd))
            break;

        /* Don't continue sending packets when the link is down. */
        if (RT_UNLIKELY(   !pcnetIsLinkUp(pData)
                        &&  pData->cLinkDownReported > PCNET_MAX_LINKDOWN_REPORTED)
            )
            break;

#ifdef PCNET_DEBUG_TMD
        Log2(("#%d TMDLOAD 0x%08x\n", PCNETSTATE_2_DEVINS(pData)->iInstance, PHYSADDR(pData, CSR_CXDA(pData))));
        PRINT_TMD(&tmd);
#endif
        pcnetXmitScrapFrame(pData);

        /*
         * The typical case - a complete packet.
         * This can be performed with zero copy in Ring-3.
         */
        if (tmd.tmd1.stp && tmd.tmd1.enp)
        {
            const unsigned cb = 4096 - tmd.tmd1.bcnt;
            Log(("#%d pcnetTransmit: stp&enp: cb=%d xmtrc=%#x\n", PCNETSTATE_2_DEVINS(pData)->iInstance, cb, CSR_XMTRC(pData)));

            if (RT_LIKELY(pcnetIsLinkUp(pData) || CSR_LOOP(pData)))
            {
                RTR3PTR pv;

                /* From the manual: ``A zero length buffer is acceptable as
                 * long as it is not the last buffer in a chain (STP = 0 and
                 * ENP = 1).'' That means that the first buffer might have a
                 * zero length if it is not the last one in the chain. */
                if (RT_LIKELY(cb <= 1536))
                {
                    int rc = PDMDevHlpPhys2HCVirt(pData->pDevInsHC,
                                                  PHYSADDR(pData, tmd.tmd0.tbadr), cb, &pv);
                    if (RT_SUCCESS(rc))
                        pcnetXmitZeroCopyFrame(pData, pv, cb);
                    else
                    {
                        pcnetXmitRead1st(pData, PHYSADDR(pData, tmd.tmd0.tbadr), cb);
                    }
                    if (CSR_LOOP(pData))
                        pcnetXmitLoopbackFrame(pData);
                    else
                    {
                        int rc = pcnetXmitCompleteFrame(pData);
                        if (VBOX_FAILURE(rc))
                            return rc; /* can happen during termination */
                    }
                }
                else if (cb == 4096)
                {
                    /* The Windows NT4 pcnet driver sometimes marks the first
                     * unused descriptor as owned by us. Ignore that (by
                     * passing it back). Do not update the ring counter in this
                     * case (otherwise that driver becomes even more confused,
                     * which causes transmit to stall for about 10 seconds).
                     * This is just a workaround, not a final solution. */
                    /* r=frank: IMHO this is the correct implementation. The
                     * manual says: ``If the OWN bit is set and the buffer
                     * length is 0, the OWN bit will be cleared. In the C-LANCE
                     * the buffer length of 0 is interpreted as a 4096-byte
                     * buffer.'' */
                    LogRel(("PCNET: pcnetAsyncTransmit: illegal 4kb frame -> ignoring\n"));
                    pcnetTmdStorePassHost(pData, &tmd, PHYSADDR(pData, CSR_CXDA(pData)));
                    break;
                }
                else
                {
                    /* Signal error, as this violates the Ethernet specs. */
                    /** @todo check if the correct error is generated. */
                    LogRel(("PCNET: pcnetAsyncTransmit: illegal 4kb frame -> signalling error\n"));

                    pcnetXmitFailTMDGeneric(pData, &tmd);
                }
            }
            else
                pcnetXmitFailTMDLinkDown(pData, &tmd);

            /* Write back the TMD and pass it to the host (clear own bit). */
            pcnetTmdStorePassHost(pData, &tmd, PHYSADDR(pData, CSR_CXDA(pData)));

            /* advance the ring counter register */
            if (CSR_XMTRC(pData) < 2)
                CSR_XMTRC(pData) = CSR_XMTRL(pData);
            else
                CSR_XMTRC(pData)--;
        }
        else if (tmd.tmd1.stp)
        {
            /*
             * Read TMDs until end-of-packet or tdte poll fails (underflow).
             */
            const unsigned cbMaxFrame = 1536;
            bool fDropFrame = false;
            unsigned cb = 4096 - tmd.tmd1.bcnt;
            pcnetXmitRead1st(pData, PHYSADDR(pData, tmd.tmd0.tbadr), cb);
            for (;;)
            {
                /*
                 * Advance the ring counter register and check the next tmd.
                 */
#ifdef LOG_ENABLED
                const uint32_t iStart = CSR_XMTRC(pData);
#endif
                const uint32_t GCPhysPrevTmd = PHYSADDR(pData, CSR_CXDA(pData));
                if (CSR_XMTRC(pData) < 2)
                    CSR_XMTRC(pData) = CSR_XMTRL(pData);
                else
                    CSR_XMTRC(pData)--;

                TMD dummy;
                if (!pcnetTdtePoll(pData, &dummy))
                {
                    /*
                     * Underflow!
                     */
                    tmd.tmd2.buff = tmd.tmd2.uflo = tmd.tmd1.err = 1;
                    pData->aCSR[0] |= 0x0200;        /* set TINT */
                    if (!CSR_DXSUFLO(pData))         /* stop on xmit underflow */
                        pData->aCSR[0] &= ~0x0010;   /* clear TXON */
                    pcnetTmdStorePassHost(pData, &tmd, GCPhysPrevTmd);
                    AssertMsgFailed(("pcnetTransmit: Underflow!!!\n"));
                    break;
                }

                /* release & save the previous tmd, pass it to the host */
                pcnetTmdStorePassHost(pData, &tmd, GCPhysPrevTmd);

                /*
                 * The next tdm.
                 */
#ifdef VBOX_WITH_STATISTICS
                cBuffers++;
#endif
                pcnetTmdLoad(pData, &tmd, PHYSADDR(pData, CSR_CXDA(pData)));
                cb = 4096 - tmd.tmd1.bcnt;
                if (    pData->SendFrame.cb + cb < cbMaxFrame
                    &&  !fDropFrame)
                    pcnetXmitReadMore(pData, PHYSADDR(pData, tmd.tmd0.tbadr), cb);
                else
                {
                    AssertMsg(fDropFrame, ("pcnetTransmit: Frame is too big!!! %d bytes\n",
                                           pData->SendFrame.cb + cb));
                    fDropFrame = true;
                }
                if (tmd.tmd1.enp)
                {
                    Log(("#%d pcnetTransmit: stp: cb=%d xmtrc=%#x-%#x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
                         pData->SendFrame.cb, iStart, CSR_XMTRC(pData)));
                    if (pcnetIsLinkUp(pData) && !fDropFrame)
                    {
                        int rc = pcnetXmitCompleteFrame(pData);
                        if (VBOX_FAILURE(rc))
                            return rc; /* can happen during termination */
                    }
                    else if (CSR_LOOP(pData) && !fDropFrame)
                        pcnetXmitLoopbackFrame(pData);
                    else
                    {
                        if (!fDropFrame)
                            pcnetXmitFailTMDLinkDown(pData, &tmd);
                        pcnetXmitScrapFrame(pData);
                    }

                    /* Write back the TMD, pass it to the host */
                    pcnetTmdStorePassHost(pData, &tmd, PHYSADDR(pData, CSR_CXDA(pData)));

                    /* advance the ring counter register */
                    if (CSR_XMTRC(pData) < 2)
                        CSR_XMTRC(pData) = CSR_XMTRL(pData);
                    else
                        CSR_XMTRC(pData)--;
                    break;
                }
            }
        }
        else
        {
            /*
             * We underflowed in a previous transfer, or the driver is giving us shit.
             * Simply stop the transmitting for now.
             */
            /** @todo according to the specs we're supposed to clear the own bit and move on to the next one. */
            Log(("#%d pcnetTransmit: guest is giving us shit!\n", PCNETSTATE_2_DEVINS(pData)->iInstance));
            break;
        }
        /* Update TDMD, TXSTRT and TINT. */
        pData->aCSR[0] &= ~0x0008;       /* clear TDMD */

        pData->aCSR[4] |=  0x0004;       /* set TXSTRT */
        if (    !CSR_TOKINTD(pData)      /* Transmit OK Interrupt Disable, no infl. on errors. */
            ||  (CSR_LTINTEN(pData) && tmd.tmd1.ltint)
            ||  tmd.tmd1.err)
        {
            cFlushIrq++;
            pData->aCSR[0] |= 0x0200;    /* set TINT */
        }

        /** @todo should we continue after an error (tmd.tmd1.err) or not? */

        STAM_COUNTER_INC(&pData->aStatXmitChainCounts[RT_MIN(cBuffers,
                                                      ELEMENTS(pData->aStatXmitChainCounts)) - 1]);
    } while (CSR_TXON(pData));          /* transfer on */

    if (cFlushIrq)
    {
        STAM_COUNTER_INC(&pData->aStatXmitFlush[RT_MIN(cFlushIrq, ELEMENTS(pData->aStatXmitFlush)) - 1]);
        pcnetUpdateIrq(pData);
    }

    STAM_PROFILE_ADV_STOP(&pData->StatTransmit, a);

    return VINF_SUCCESS;
}

/**
 * Async I/O thread for delayed sending of packets.
 */
static DECLCALLBACK(int) pcnetAsyncSend(RTTHREAD ThreadSelf, void *pvUser)
{
    PCNetState *pData = (PCNetState *)pvUser;
    RTSEMEVENT hEvent = pData->hSendEventSem;
    int        rc     = VINF_SUCCESS;

    while(rc == VINF_SUCCESS)
    {
        rc = RTSemEventWait(hEvent, RT_INDEFINITE_WAIT);
        if (VBOX_FAILURE(rc))
            break;

        rc = PDMCritSectEnter(&pData->CritSect, VERR_PERMISSION_DENIED);
        AssertReleaseRC(rc);

        if (!pData->fSaving)
            rc = pcnetAsyncTransmit(pData);

        PDMCritSectLeave(&pData->CritSect);
    }
    return VINF_SUCCESS;
}

#endif /* IN_RING3 */

/**
 * Poll for changes in RX and TX descriptor rings.
 */
static void pcnetPollRxTx(PCNetState *pData)
{
    if (CSR_RXON(pData))
        if (HOST_IS_OWNER(CSR_CRST(pData)))     /* Only poll RDTEs if none available */
            pcnetRdtePoll(pData);

    if (CSR_TDMD(pData) || CSR_TXON(pData) && !CSR_DPOLL(pData))
        pcnetTransmit(pData);
}

/**
 * Update the poller timer
 * @thread EMT,
 */
static void pcnetPollTimer(PCNetState *pData)
{
    STAM_PROFILE_ADV_START(&pData->StatPollTimer, a);

#ifdef LOG_ENABLED
    TMD dummy;
    Log2(("#%d pcnetPollTimer time=%08x TDMD=%d TXON=%d POLL=%d TDTE=%d TDRA=%x\n",
          PCNETSTATE_2_DEVINS(pData)->iInstance, RTTimeMilliTS(), CSR_TDMD(pData), CSR_TXON(pData),
          !CSR_DPOLL(pData), pcnetTdtePoll(pData, &dummy), pData->GCTDRA));
    Log2(("#%d pcnetPollTimer: CSR_CXDA=%x CSR_XMTRL=%d CSR_XMTRC=%d\n",
          PCNETSTATE_2_DEVINS(pData)->iInstance, CSR_CXDA(pData), CSR_XMTRL(pData), CSR_XMTRC(pData)));
#endif
#ifdef PCNET_DEBUG_TMD
    if (CSR_CXDA(pData))
    {
        TMD tmd;
        pcnetTmdLoad(pData, &tmd, PHYSADDR(pData, CSR_CXDA(pData)));
        Log2(("#%d pcnetPollTimer: TMDLOAD 0x%08x\n", PCNETSTATE_2_DEVINS(pData)->iInstance, PHYSADDR(pData, CSR_CXDA(pData))));
        PRINT_TMD(&tmd);
    }
#endif
    if (CSR_TDMD(pData))
        pcnetTransmit(pData);

    pcnetUpdateIrq(pData);

    if (RT_LIKELY(!CSR_STOP(pData) && !CSR_SPND(pData) && !CSR_DPOLL(pData)))
    {
        /* We ensure that we poll at least every 2ms (500Hz) but not more often than
         * 5000 times per second. This way we completely prevent the overhead from
         * heavy reprogramming the timer which turned out to be very CPU-intensive.
         * The drawback is that csr46 and csr47 are not updated properly anymore
         * but so far I have not seen any guest depending on these values. The 2ms
         * interval is the default polling interval of the PCNet card (65536/33MHz). */
#ifdef PCNET_NO_POLLING
        pcnetPollRxTx(pData);
#else
        uint64_t u64Now = TMTimerGet(pData->CTXSUFF(pTimerPoll));
        if (RT_UNLIKELY(u64Now - pData->u64LastPoll > 200000))
        {
            pData->u64LastPoll = u64Now;
            pcnetPollRxTx(pData);
        }
        if (!TMTimerIsActive(pData->CTXSUFF(pTimerPoll)))
            /* Poll timer interval is fixed to 500Hz. Don't stop it. */
            TMTimerSet(pData->CTXSUFF(pTimerPoll),
                       TMTimerGet(pData->CTXSUFF(pTimerPoll)) + 2000000);
#endif
    }
    STAM_PROFILE_ADV_STOP(&pData->StatPollTimer, a);
}


static int pcnetCSRWriteU16(PCNetState *pData, uint32_t u32RAP, uint32_t new_value)
{
    uint16_t val = new_value;
    int      rc  = VINF_SUCCESS;
#ifdef PCNET_DEBUG_CSR
    Log(("#%d pcnetCSRWriteU16: rap=%d val=0x%04x\n", PCNETSTATE_2_DEVINS(pData)->iInstance, u32RAP, val));
#endif
    switch (u32RAP)
    {
        case 0:
            {
                uint16_t csr0 = pData->aCSR[0];
                /* Clear any interrupt flags.
                 * Don't clear an interrupt flag which was not seen by the guest yet. */
                csr0 &= ~(val  &  0x7f00  & pData->u16CSR0LastSeenByGuest);
                csr0  =  (csr0 & ~0x0040) | (val  & 0x0048);
                val   =  (val  &  0x007f) | (csr0 & 0x7f00);

                /* Iff STOP, STRT and INIT are set, clear STRT and INIT */
                if ((val & 7) == 7)
                    val &= ~3;

#ifndef IN_RING3
                if (!(csr0 & 0x0001/*init*/) && (val & 1))
                {
                    Log(("#%d pcnetCSRWriteU16: pcnetInit requested => HC\n", PCNETSTATE_2_DEVINS(pData)->iInstance));
                    return VINF_IOM_HC_IOPORT_WRITE;
                }
#endif
                LOG_REGISTER(("PCNet#%d: WRITE CSR%d, %04x => %04x (%04x)\n",
                              PCNETSTATE_2_DEVINS(pData)->iInstance,
                              u32RAP, new_value, csr0, pData->aCSR[0]));
                pData->aCSR[0] = csr0;

                if (!CSR_STOP(pData) && (val & 4))
                    pcnetStop(pData);

#ifdef IN_RING3
                if (!CSR_INIT(pData) && (val & 1))
                    pcnetInit(pData);
#endif

                if (!CSR_STRT(pData) && (val & 2))
                    pcnetStart(pData);

                if (CSR_TDMD(pData))
                    pcnetTransmit(pData);

                return rc;
            }
        case 1:  /* IADRL */
        case 2:  /* IADRH */
        case 8:  /* LADRF  0..15 */
        case 9:  /* LADRF 16..31 */
        case 10: /* LADRF 32..47 */
        case 11: /* LADRF 48..63 */
        case 12: /* PADR   0..15 */
        case 13: /* PADR  16..31 */
        case 14: /* PADR  32..47 */
        case 18: /* CRBAL */
        case 19: /* CRBAU */
        case 20: /* CXBAL */
        case 21: /* CXBAU */
        case 22: /* NRBAL */
        case 23: /* NRBAU */
        case 24: /* BADRL */
        case 25: /* BADRU */
        case 26: /* NRDAL */
        case 27: /* NRDAU */
        case 28: /* CRDAL */
        case 29: /* CRDAU */
        case 30: /* BADXL */
        case 31: /* BADXU */
        case 32: /* NXDAL */
        case 33: /* NXDAU */
        case 34: /* CXDAL */
        case 35: /* CXDAU */
        case 36: /* NNRDL */
        case 37: /* NNRDU */
        case 38: /* NNXDL */
        case 39: /* NNXDU */
        case 40: /* CRBCL */
        case 41: /* CRBCU */
        case 42: /* CXBCL */
        case 43: /* CXBCU */
        case 44: /* NRBCL */
        case 45: /* NRBCU */
        case 46: /* POLL */
        case 47: /* POLLINT */
        case 72: /* RCVRC */
        case 74: /* XMTRC */
        case 76: /* RCVRL */ /** @todo call pcnetUpdateRingHandlers */
                             /** @todo receive ring length is stored in two's complement! */
        case 78: /* XMTRL */ /** @todo call pcnetUpdateRingHandlers */
                             /** @todo transmit ring length is stored in two's complement! */
        case 112: /* MISSC */
            if (CSR_STOP(pData) || CSR_SPND(pData))
                break;
            LOG_REGISTER(("PCNet#%d: WRITE CSR%d, %04x\n",
                         PCNETSTATE_2_DEVINS(pData)->iInstance, u32RAP, val));
            return rc;
        case 3: /* Interrupt Mask and Deferral Control */
            LOG_REGISTER(("PCNet#%d: WRITE CSR%d, %04x\n",
                         PCNETSTATE_2_DEVINS(pData)->iInstance, u32RAP, val));
            break;
        case 4: /* Test and Features Control */
            LOG_REGISTER(("PCNet#%d: WRITE CSR%d, %04x\n",
                         PCNETSTATE_2_DEVINS(pData)->iInstance, u32RAP, val));
            pData->aCSR[4] &= ~(val & 0x026a);
            val &= ~0x026a;
            val |= pData->aCSR[4] & 0x026a;
            break;
        case 5: /* Extended Control and Interrupt 1 */
            LOG_REGISTER(("PCNet#%d: WRITE CSR%d, %04x\n",
                        PCNETSTATE_2_DEVINS(pData)->iInstance, u32RAP, val));
            pData->aCSR[5] &= ~(val & 0x0a90);
            val &= ~0x0a90;
            val |= pData->aCSR[5] & 0x0a90;
            break;
        case 15: /* Mode */
            if ((pData->aCSR[15] & 0x8000) != (val & 0x8000) && pData->pDrv)
            {
                Log(("PCNet#%d: promiscuous mode changed to %d\n",
                     PCNETSTATE_2_DEVINS(pData)->iInstance, !!(val & 0x8000)));
#ifndef IN_RING3
                return VINF_IOM_HC_IOPORT_WRITE;
#else
                /* check for promiscuous mode change */
                pData->pDrv->pfnSetPromiscuousMode(pData->pDrv, !!(val & 0x8000));
#endif
            }
            break;
        case 16: /* IADRL */
            return pcnetCSRWriteU16(pData, 1, val);
        case 17: /* IADRH */
            return pcnetCSRWriteU16(pData, 2, val);
        case 58: /* Software Style */
            LOG_REGISTER(("PCNet#%d: WRITE SW_STYLE, %04x\n",
                         PCNETSTATE_2_DEVINS(pData)->iInstance, val));
            rc = pcnetBCRWriteU16(pData, BCR_SWS, val);
            break;
        default:
            return rc;
    }
    pData->aCSR[u32RAP] = val;
    return rc;
}

static uint32_t pcnetCSRReadU16(PCNetState *pData, uint32_t u32RAP)
{
    uint32_t val;
    switch (u32RAP)
    {
        case 0:
            pcnetUpdateIrq(pData);
            val = pData->aCSR[0];
            val |= (val & 0x7800) ? 0x8000 : 0;
            pData->u16CSR0LastSeenByGuest = val;
            break;
        case 16:
            return pcnetCSRReadU16(pData, 1);
        case 17:
            return pcnetCSRReadU16(pData, 2);
        case 58:
            return pcnetBCRReadU16(pData, BCR_SWS);
        case 88:
            val = pData->aCSR[89];
            val <<= 16;
            val |= pData->aCSR[88];
            break;
        default:
            val = pData->aCSR[u32RAP];
            LOG_REGISTER(("PCNet#%d: read  CSR%d => %04x\n",
                    PCNETSTATE_2_DEVINS(pData)->iInstance, u32RAP, val));
    }
#ifdef PCNET_DEBUG_CSR
    Log(("#%d pcnetCSRReadU16: u32RAP=%d val=0x%04x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         u32RAP, val));
#endif
    return val;
}

static int pcnetBCRWriteU16(PCNetState *pData, uint32_t u32RAP, uint32_t val)
{
    int rc = VINF_SUCCESS;
    u32RAP &= 0x7f;
#ifdef PCNET_DEBUG_BCR
    Log2(("#%d pcnetBCRWriteU16: u32RAP=%d val=0x%04x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         u32RAP, val));
#endif
    switch (u32RAP)
    {
        case BCR_SWS:
            if (!(CSR_STOP(pData) || CSR_SPND(pData)))
                return rc;
            val &= ~0x0300;
            switch (val & 0x00ff)
            {
                default:
                    Log(("Bad SWSTYLE=0x%02x\n", val & 0xff));
                    // fall through
                case 0:
                    val |= 0x0200; /* 16 bit */
                    pData->iLog2DescSize = 3;
                    pData->GCUpperPhys   = (0xff00 & (uint32_t)pData->aCSR[2]) << 16;
                    break;
                case 1:
                    val |= 0x0100; /* 32 bit */
                    pData->iLog2DescSize = 4;
                    pData->GCUpperPhys   = 0;
                    break;
                case 2:
                case 3:
                    val |= 0x0300; /* 32 bit */
                    pData->iLog2DescSize = 4;
                    pData->GCUpperPhys   = 0;
                    break;
            }
            LOG_REGISTER(("PCNet#%d: WRITE SW_STYLE, %04x\n",
                         PCNETSTATE_2_DEVINS(pData)->iInstance, val));
            Log(("BCR_SWS=0x%04x\n", val));
            pData->aCSR[58] = val;
            /* fall through */
        case BCR_LNKST:
        case BCR_LED1:
        case BCR_LED2:
        case BCR_LED3:
        case BCR_MC:
        case BCR_FDC:
        case BCR_BSBC:
        case BCR_EECAS:
        case BCR_PLAT:
        case BCR_MIIADDR:
            LOG_REGISTER(("PCNet#%d: WRITE BCR%d, %04x\n",
                         PCNETSTATE_2_DEVINS(pData)->iInstance, u32RAP, val));
            pData->aBCR[u32RAP] = val;
            break;

        case BCR_MIIMDR:
            LOG_REGISTER(("PCNet#%d: WRITE MII%d, %04x\n",
                         PCNETSTATE_2_DEVINS(pData)->iInstance, u32RAP, val));
            pData->aMII[pData->aBCR[BCR_MIIADDR] & 0x1f] = val;
            break;

        default:
            break;
    }
    return rc;
}

static uint32_t pcnetMIIReadU16(PCNetState *pData, uint32_t miiaddr)
{
    uint32_t val;
    STAM_COUNTER_INC(&pData->StatMIIReads);

    switch (miiaddr)
    {
        case 0:
            /* MII basic mode control register. */
            val = 0x1000;   /* Enable auto negotiation. */
            break;

        case 1:
            /* MII basic mode status register. */
            if (pData->fLinkUp && !pData->fLinkTempDown)
                val =   0x7800  /* Can do 100mbps FD/HD and 10mbps FD/HD. */
                      | 0x0020  /* Auto-negotiation complete. */
                      | 0x0008  /* Able to do auto-negotiation. */
                      | 0x0004  /* Link status. */
                      | 0x0001; /* Extended Capability, i.e. registers 4+ valid. */
            else
            {
                val =   0x7800  /* Can do 100mbps FD/HD and 10mbps FD/HD. */
                      | 0x0008  /* Able to do auto-negotiation. */
                      | 0x0001; /* Extended Capability, i.e. registers 4+ valid. */
                pData->cLinkDownReported++;
            }
            break;

        case 2:
            /* PHY identifier 1. */
            val = 0;    /* No name PHY. */
            break;

        case 3:
            /* PHY identifier 2. */
            val = 0;    /* No name PHY. */
            break;

        case 4:
            /* Advertisement control register. */
            val =   0x05e0  /* Try flow control, 100mbps FD/HD and 10mbps FD/HD. */
                  | 0x0001; /* CSMA selector. */
            break;

        case 5:
            /* Link partner ability register. */
            if (pData->fLinkUp && !pData->fLinkTempDown)
                val =   0x8000  /* Next page bit. */
                      | 0x4000  /* Link partner acked us. */
                      | 0x05e0  /* Can do flow control, 100mbps FD/HD and 10mbps FD/HD. */
                      | 0x0001; /* Use CSMA selector. */
            else
            {
                val = 0;
                pData->cLinkDownReported++;
            }
            break;

        case 6:
            /* Auto negotiation expansion register. */
            if (pData->fLinkUp && !pData->fLinkTempDown)
                val =   0x0008  /* Link partner supports npage. */
                      | 0x0004  /* Enable npage words. */
                      | 0x0001; /* Can do N-way auto-negotiation. */
            else
            {
                val = 0;
                pData->cLinkDownReported++;
            }
            break;

        default:
            val = 0;
            break;
    }

#ifdef PCNET_DEBUG_MII
    Log(("#%d pcnet: mii read %d -> %#x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         miiaddr, val));
#endif
    return val;
}

static uint32_t pcnetBCRReadU16(PCNetState *pData, uint32_t u32RAP)
{
    uint32_t val;
    u32RAP &= 0x7f;
    switch (u32RAP)
    {
        case BCR_LNKST:
        case BCR_LED1:
        case BCR_LED2:
        case BCR_LED3:
            val = pData->aBCR[u32RAP] & ~0x8000;
            /* Clear LNKSTE if we're not connected or if we've just loaded a VM state. */
            if (!pData->pDrv || pData->fLinkTempDown || !pData->fLinkUp)
            {
                if (u32RAP == 4)
                    pData->cLinkDownReported++;
                val &= ~0x40;
            }
            val |= (val & 0x017f & pData->u32Lnkst) ? 0x8000 : 0;
            break;

        case BCR_MIIMDR:
            if (pData->fAm79C973 && (pData->aBCR[BCR_MIIADDR] >> 5 & 0x1f) == 0)
            {
                size_t miiaddr = pData->aBCR[BCR_MIIADDR] & 0x1f;
                val = pcnetMIIReadU16(pData, miiaddr);
            }
            else
                val = 0xffff;
            break;

        default:
            val = u32RAP < BCR_MAX_RAP ? pData->aBCR[u32RAP] : 0;
            break;
    }
#ifdef PCNET_DEBUG_BCR
    Log2(("#%d pcnetBCRReadU16: u32RAP=%d val=0x%04x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         u32RAP, val));
#endif
    return val;
}

#ifdef IN_RING3 /* move down */
static void pcnetHardReset(PCNetState *pData)
{
    int      i;
    uint16_t checksum;

    /* Initialize the PROM */
    Assert(sizeof(pData->MacConfigured) == 6);
    memcpy(pData->aPROM, &pData->MacConfigured, sizeof(pData->MacConfigured));
    pData->aPROM[12] = pData->aPROM[13] = 0x00;
    pData->aPROM[14] = pData->aPROM[15] = 0x57;

    for (i = 0, checksum = 0; i < 16; i++)
        checksum += pData->aPROM[i];
    *(uint16_t *)&pData->aPROM[12] = cpu_to_le16(checksum);

    pData->aBCR[BCR_MSRDA] = 0x0005;
    pData->aBCR[BCR_MSWRA] = 0x0005;
    pData->aBCR[BCR_MC   ] = 0x0002;
    pData->aBCR[BCR_LNKST] = 0x00c0;
    pData->aBCR[BCR_LED1 ] = 0x0084;
    pData->aBCR[BCR_LED2 ] = 0x0088;
    pData->aBCR[BCR_LED3 ] = 0x0090;
    pData->aBCR[BCR_FDC  ] = 0x0000;
    pData->aBCR[BCR_BSBC ] = 0x9001;
    pData->aBCR[BCR_EECAS] = 0x0002;
    pData->aCSR[58       ] = /* CSR58 is an alias for BCR20 */
    pData->aBCR[BCR_SWS  ] = 0x0200;
    pData->iLog2DescSize   = 3;
    pData->aBCR[BCR_PLAT ] = 0xff06;

    pcnetSoftReset(pData);
}
#endif /* IN_RING3 */

static void pcnetAPROMWriteU8(PCNetState *pData, uint32_t addr, uint32_t val)
{
    addr &= 0x0f;
    val  &= 0xff;
    Log(("#%d pcnetAPROMWriteU8: addr=0x%08x val=0x%02x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         addr, val));
    /* Check APROMWE bit to enable write access */
    if (pcnetBCRReadU16(pData, 2) & 0x80)
        pData->aPROM[addr] = val;
}

static uint32_t pcnetAPROMReadU8(PCNetState *pData, uint32_t addr)
{
    uint32_t val = pData->aPROM[addr &= 0x0f];
    Log(("#%d pcnetAPROMReadU8: addr=0x%08x val=0x%02x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         addr, val));
    return val;
}

static int pcnetIoportWriteU16(PCNetState *pData, uint32_t addr, uint32_t val)
{
    int rc = VINF_SUCCESS;

#ifdef PCNET_DEBUG_IO
    Log2(("#%d pcnetIoportWriteU16: addr=0x%08x val=0x%04x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         addr, val));
#endif
    if (RT_LIKELY(!BCR_DWIO(pData)))
    {
        switch (addr & 0x0f)
        {
            case 0x00: /* RDP */
                pcnetPollTimer(pData);
                rc = pcnetCSRWriteU16(pData, pData->u32RAP, val);
                pcnetUpdateIrq(pData);
                break;
            case 0x02: /* RAP */
                pData->u32RAP = val & 0x7f;
                break;
            case 0x06: /* BDP */
                rc = pcnetBCRWriteU16(pData, pData->u32RAP, val);
                break;
        }
    }

    return rc;
}

static uint32_t pcnetIoportReadU16(PCNetState *pData, uint32_t addr, int *pRC)
{
    uint32_t val = ~0U;

    *pRC = VINF_SUCCESS;

    if (RT_LIKELY(!BCR_DWIO(pData)))
    {
        switch (addr & 0x0f)
        {
            case 0x00: /* RDP */
                /** @note if we're not polling, then the guest will tell us when to poll by setting TDMD in CSR0 */
                /** Polling is then useless here and possibly expensive. */
                if (!CSR_DPOLL(pData))
                    pcnetPollTimer(pData);

                val = pcnetCSRReadU16(pData, pData->u32RAP);
                if (pData->u32RAP == 0)  // pcnetUpdateIrq() already called by pcnetCSRReadU16()
                    goto skip_update_irq;
                break;
            case 0x02: /* RAP */
                val = pData->u32RAP;
                goto skip_update_irq;
            case 0x04: /* RESET */
                pcnetSoftReset(pData);
                val = 0;
                break;
            case 0x06: /* BDP */
                val = pcnetBCRReadU16(pData, pData->u32RAP);
                break;
        }
    }
    pcnetUpdateIrq(pData);

skip_update_irq:
#ifdef PCNET_DEBUG_IO
    Log2(("#%d pcnetIoportReadU16: addr=0x%08x val=0x%04x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         addr, val & 0xffff));
#endif
    return val;
}

static int pcnetIoportWriteU32(PCNetState *pData, uint32_t addr, uint32_t val)
{
    int rc = VINF_SUCCESS;

#ifdef PCNET_DEBUG_IO
    Log2(("#%d pcnetIoportWriteU32: addr=0x%08x val=0x%08x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         addr, val));
#endif
    if (RT_LIKELY(BCR_DWIO(pData)))
    {
        switch (addr & 0x0f)
        {
            case 0x00: /* RDP */
                pcnetPollTimer(pData);
                rc = pcnetCSRWriteU16(pData, pData->u32RAP, val & 0xffff);
                pcnetUpdateIrq(pData);
                break;
            case 0x04: /* RAP */
                pData->u32RAP = val & 0x7f;
                break;
            case 0x0c: /* BDP */
                rc = pcnetBCRWriteU16(pData, pData->u32RAP, val & 0xffff);
                break;
        }
    }
    else if (addr == 0)
    {
        /* switch device to dword I/O mode */
        pcnetBCRWriteU16(pData, BCR_BSBC, pcnetBCRReadU16(pData, BCR_BSBC) | 0x0080);
#ifdef PCNET_DEBUG_IO
        Log2(("device switched into dword i/o mode\n"));
#endif
    }

    return rc;
}

static uint32_t pcnetIoportReadU32(PCNetState *pData, uint32_t addr, int *pRC)
{
    uint32_t val = ~0U;

    *pRC = VINF_SUCCESS;

    if (RT_LIKELY(BCR_DWIO(pData)))
    {
        switch (addr & 0x0f)
        {
            case 0x00: /* RDP */
                /** @note if we're not polling, then the guest will tell us when to poll by setting TDMD in CSR0 */
                /** Polling is then useless here and possibly expensive. */
                if (!CSR_DPOLL(pData))
                    pcnetPollTimer(pData);

                val = pcnetCSRReadU16(pData, pData->u32RAP);
                if (pData->u32RAP == 0)  // pcnetUpdateIrq() already called by pcnetCSRReadU16()
                    goto skip_update_irq;
                break;
            case 0x04: /* RAP */
                val = pData->u32RAP;
                goto skip_update_irq;
            case 0x08: /* RESET */
                pcnetSoftReset(pData);
                val = 0;
                break;
            case 0x0c: /* BDP */
                val = pcnetBCRReadU16(pData, pData->u32RAP);
                break;
        }
    }
    pcnetUpdateIrq(pData);

skip_update_irq:
#ifdef PCNET_DEBUG_IO
    Log2(("#%d pcnetIoportReadU32: addr=0x%08x val=0x%08x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         addr, val));
#endif
    return val;
}

static void pcnetMMIOWriteU8(PCNetState *pData, RTGCPHYS addr, uint32_t val)
{
#ifdef PCNET_DEBUG_IO
    Log2(("#%d pcnetMMIOWriteU8: addr=0x%08x val=0x%02x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         addr, val));
#endif
    if (!(addr & 0x10))
        pcnetAPROMWriteU8(pData, addr, val);
}

static uint32_t pcnetMMIOReadU8(PCNetState *pData, RTGCPHYS addr)
{
    uint32_t val = ~0U;
    if (!(addr & 0x10))
        val = pcnetAPROMReadU8(pData, addr);
#ifdef PCNET_DEBUG_IO
    Log2(("#%d pcnetMMIOReadU8: addr=0x%08x val=0x%02x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         addr, val & 0xff));
#endif
    return val;
}

static void pcnetMMIOWriteU16(PCNetState *pData, RTGCPHYS addr, uint32_t val)
{
#ifdef PCNET_DEBUG_IO
    Log2(("#%d pcnetMMIOWriteU16: addr=0x%08x val=0x%04x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         addr, val));
#endif
    if (addr & 0x10)
        pcnetIoportWriteU16(pData, addr & 0x0f, val);
    else
    {
        pcnetAPROMWriteU8(pData, addr,   val     );
        pcnetAPROMWriteU8(pData, addr+1, val >> 8);
    }
}

static uint32_t pcnetMMIOReadU16(PCNetState *pData, RTGCPHYS addr)
{
    uint32_t val = ~0U;
    int      rc;

    if (addr & 0x10)
        val = pcnetIoportReadU16(pData, addr & 0x0f, &rc);
    else
    {
        val = pcnetAPROMReadU8(pData, addr+1);
        val <<= 8;
        val |= pcnetAPROMReadU8(pData, addr);
    }
#ifdef PCNET_DEBUG_IO
    Log2(("#%d pcnetMMIOReadU16: addr=0x%08x val = 0x%04x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         addr, val & 0xffff));
#endif
    return val;
}

static void pcnetMMIOWriteU32(PCNetState *pData, RTGCPHYS addr, uint32_t val)
{
#ifdef PCNET_DEBUG_IO
    Log2(("#%d pcnetMMIOWriteU32: addr=0x%08x val=0x%08x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         addr, val));
#endif
    if (addr & 0x10)
        pcnetIoportWriteU32(pData, addr & 0x0f, val);
    else
    {
        pcnetAPROMWriteU8(pData, addr,   val      );
        pcnetAPROMWriteU8(pData, addr+1, val >>  8);
        pcnetAPROMWriteU8(pData, addr+2, val >> 16);
        pcnetAPROMWriteU8(pData, addr+3, val >> 24);
    }
}

static uint32_t pcnetMMIOReadU32(PCNetState *pData, RTGCPHYS addr)
{
    uint32_t val;
    int      rc;

    if (addr & 0x10)
        val = pcnetIoportReadU32(pData, addr & 0x0f, &rc);
    else
    {
        val  = pcnetAPROMReadU8(pData, addr+3);
        val <<= 8;
        val |= pcnetAPROMReadU8(pData, addr+2);
        val <<= 8;
        val |= pcnetAPROMReadU8(pData, addr+1);
        val <<= 8;
        val |= pcnetAPROMReadU8(pData, addr  );
    }
#ifdef PCNET_DEBUG_IO
    Log2(("#%d pcnetMMIOReadU32: addr=0x%08x val=0x%08x\n", PCNETSTATE_2_DEVINS(pData)->iInstance,
         addr, val));
#endif
    return val;
}


/**
 * Port I/O Handler for IN operations.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pvUser      User argument.
 * @param   Port        Port number used for the IN operation.
 * @param   pu32        Where to store the result.
 * @param   cb          Number of bytes read.
 */
PDMBOTHCBDECL(int) pcnetIOPortAPromRead(PPDMDEVINS pDevIns, void *pvUser,
                                        RTIOPORT Port, uint32_t *pu32, unsigned cb)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);
    int        rc;
    if (cb == 1)
    {
        STAM_PROFILE_ADV_START(&pData->StatAPROMRead, a);
        rc = PDMCritSectEnter(&pData->CritSect, VINF_IOM_HC_IOPORT_WRITE);
        if (rc == VINF_SUCCESS)
        {
            *pu32 = pcnetAPROMReadU8(pData, Port);
            PDMCritSectLeave(&pData->CritSect);
        }
        STAM_PROFILE_ADV_STOP(&pData->StatAPROMRead, a);
    }
    else
        rc =  VERR_IOM_IOPORT_UNUSED;
    LogFlow(("#%d pcnetIOPortAPromRead: Port=%RTiop *pu32=%#RX32 cb=%d rc=%Vrc\n",
             PCNETSTATE_2_DEVINS(pData)->iInstance, Port, *pu32, cb, rc));
    return rc;
}


/**
 * Port I/O Handler for OUT operations.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pvUser      User argument.
 * @param   Port        Port number used for the IN operation.
 * @param   u32         The value to output.
 * @param   cb          The value size in bytes.
 */
PDMBOTHCBDECL(int) pcnetIOPortAPromWrite(PPDMDEVINS pDevIns, void *pvUser,
                                         RTIOPORT Port, uint32_t u32, unsigned cb)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);
    int        rc;

    if (cb == 1)
    {
        STAM_PROFILE_ADV_START(&pData->StatAPROMWrite, a);
        rc = PDMCritSectEnter(&pData->CritSect, VINF_IOM_HC_IOPORT_WRITE);
        if (rc == VINF_SUCCESS)
        {
            pcnetAPROMWriteU8(pData, Port, u32);
            PDMCritSectLeave(&pData->CritSect);
        }
        STAM_PROFILE_ADV_STOP(&pData->StatAPROMWrite, a);
    }
    else
    {
        AssertMsgFailed(("Port=%#x cb=%d u32=%#x\n", Port, cb, u32));
        rc = VINF_SUCCESS;
    }
    LogFlow(("#%d pcnetIOPortAPromWrite: Port=%RTiop u32=%#RX32 cb=%d rc=%Vrc\n",
             PCNETSTATE_2_DEVINS(pData)->iInstance, Port, u32, cb, rc));
    return rc;
}


/**
 * Port I/O Handler for IN operations.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pvUser      User argument.
 * @param   Port        Port number used for the IN operation.
 * @param   pu32        Where to store the result.
 * @param   cb          Number of bytes read.
 */
PDMBOTHCBDECL(int) pcnetIOPortRead(PPDMDEVINS pDevIns, void *pvUser,
                                   RTIOPORT Port, uint32_t *pu32, unsigned cb)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);
    int         rc    = VINF_SUCCESS;

    STAM_PROFILE_ADV_START(&pData->CTXSUFF(StatIORead), a);
    rc = PDMCritSectEnter(&pData->CritSect, VINF_IOM_HC_IOPORT_READ);
    if (rc == VINF_SUCCESS)
    {
        switch (cb)
        {
            case 2: *pu32 = pcnetIoportReadU16(pData, Port, &rc); break;
            case 4: *pu32 = pcnetIoportReadU32(pData, Port, &rc); break;
            default:
                rc = VERR_IOM_IOPORT_UNUSED;
                break;
        }
        PDMCritSectLeave(&pData->CritSect);
    }
    STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatIORead), a);
    LogFlow(("#%d pcnetIOPortRead: Port=%RTiop *pu32=%#RX32 cb=%d rc=%Vrc\n",
             PCNETSTATE_2_DEVINS(pData)->iInstance, Port, *pu32, cb, rc));
    return rc;
}


/**
 * Port I/O Handler for OUT operations.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pvUser      User argument.
 * @param   Port        Port number used for the IN operation.
 * @param   u32         The value to output.
 * @param   cb          The value size in bytes.
 */
PDMBOTHCBDECL(int) pcnetIOPortWrite(PPDMDEVINS pDevIns, void *pvUser,
                                    RTIOPORT Port, uint32_t u32, unsigned cb)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);
    int         rc    = VINF_SUCCESS;

    STAM_PROFILE_ADV_START(&pData->CTXSUFF(StatIOWrite), a);
    rc = PDMCritSectEnter(&pData->CritSect, VINF_IOM_HC_IOPORT_WRITE);
    if (rc == VINF_SUCCESS)
    {
        switch (cb)
        {
            case 2: rc = pcnetIoportWriteU16(pData, Port, u32); break;
            case 4: rc = pcnetIoportWriteU32(pData, Port, u32); break;
            default:
                AssertMsgFailed(("Port=%#x cb=%d u32=%#x\n", Port, cb, u32));
                rc = VERR_INTERNAL_ERROR;
                break;
        }
        PDMCritSectLeave(&pData->CritSect);
    }
    STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatIOWrite), a);
    LogFlow(("#%d pcnetIOPortWrite: Port=%RTiop u32=%#RX32 cb=%d rc=%Vrc\n",
             PCNETSTATE_2_DEVINS(pData)->iInstance, Port, u32, cb, rc));
    return rc;
}


/**
 * Memory mapped I/O Handler for read operations.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pvUser      User argument.
 * @param   GCPhysAddr  Physical address (in GC) where the read starts.
 * @param   pv          Where to store the result.
 * @param   cb          Number of bytes read.
 */
PDMBOTHCBDECL(int) pcnetMMIORead(PPDMDEVINS pDevIns, void *pvUser,
                                 RTGCPHYS GCPhysAddr, void *pv, unsigned cb)
{
    PCNetState *pData = (PCNetState *)pvUser;
    int         rc    = VINF_SUCCESS;

    /*
     * We have to check the range, because we're page aligning the MMIO stuff presently.
     */
    if (GCPhysAddr - pData->MMIOBase < PCNET_PNPMMIO_SIZE)
    {
        STAM_PROFILE_ADV_START(&pData->CTXSUFF(StatMMIORead), a);
        rc = PDMCritSectEnter(&pData->CritSect, VINF_IOM_HC_MMIO_READ);
        if (rc == VINF_SUCCESS)
        {
            switch (cb)
            {
                case 1:  *(uint8_t  *)pv = pcnetMMIOReadU8 (pData, GCPhysAddr); break;
                case 2:  *(uint16_t *)pv = pcnetMMIOReadU16(pData, GCPhysAddr); break;
                case 4:  *(uint32_t *)pv = pcnetMMIOReadU32(pData, GCPhysAddr); break;
                default:
                    AssertMsgFailed(("cb=%d\n", cb));
                    rc = VERR_INTERNAL_ERROR;
                    break;
            }
            PDMCritSectLeave(&pData->CritSect);
        }
        STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatMMIORead), a);
    }
    else
        memset(pv, 0, cb);

    LogFlow(("#%d pcnetMMIORead: pvUser=%p:{%.*Rhxs} cb=%d GCPhysAddr=%RGp rc=%Vrc\n",
             PCNETSTATE_2_DEVINS(pData)->iInstance, pv, cb, pv, cb, GCPhysAddr, rc));
    return rc;
}


/**
 * Port I/O Handler for write operations.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pvUser      User argument.
 * @param   GCPhysAddr  Physical address (in GC) where the read starts.
 * @param   pv          Where to fetch the result.
 * @param   cb          Number of bytes to write.
 */
PDMBOTHCBDECL(int) pcnetMMIOWrite(PPDMDEVINS pDevIns, void *pvUser,
                                  RTGCPHYS GCPhysAddr, void *pv, unsigned cb)
{
    PCNetState *pData = (PCNetState *)pvUser;
    int         rc    = VINF_SUCCESS;

    /*
     * We have to check the range, because we're page aligning the MMIO stuff presently.
     */
    if (GCPhysAddr - pData->MMIOBase < PCNET_PNPMMIO_SIZE)
    {
        STAM_PROFILE_ADV_START(&pData->CTXSUFF(StatMMIOWrite), a);
        rc = PDMCritSectEnter(&pData->CritSect, VINF_IOM_HC_MMIO_WRITE);
        if (rc == VINF_SUCCESS)
        {
            switch (cb)
            {
                case 1:  pcnetMMIOWriteU8 (pData, GCPhysAddr, *(uint8_t  *)pv); break;
                case 2:  pcnetMMIOWriteU16(pData, GCPhysAddr, *(uint16_t *)pv); break;
                case 4:  pcnetMMIOWriteU32(pData, GCPhysAddr, *(uint32_t *)pv); break;
                default:
                    AssertMsgFailed(("cb=%d\n", cb));
                    rc = VERR_INTERNAL_ERROR;
                    break;
            }
            PDMCritSectLeave(&pData->CritSect);
        }
        // else rc == VINF_IOM_HC_MMIO_WRITE => handle in ring3

        STAM_PROFILE_ADV_STOP(&pData->CTXSUFF(StatMMIOWrite), a);
    }
    LogFlow(("#%d pcnetMMIOWrite: pvUser=%p:{%.*Rhxs} cb=%d GCPhysAddr=%RGp rc=%Vrc\n",
             PCNETSTATE_2_DEVINS(pData)->iInstance, pv, cb, pv, cb, GCPhysAddr, rc));
    return rc;
}


#ifdef IN_RING3
/**
 * Device timer callback function.
 *
 * @param   pDevIns         Device instance of the device which registered the timer.
 * @param   pTimer          The timer handle.
 * @thread  EMT
 */
static DECLCALLBACK(void) pcnetTimer(PPDMDEVINS pDevIns, PTMTIMER pTimer)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);
    int         rc;

    STAM_PROFILE_ADV_START(&pData->StatTimer, a);
    rc = PDMCritSectEnter(&pData->CritSect, VERR_PERMISSION_DENIED);
    AssertReleaseRC(rc);

    pcnetPollTimer(pData);

    PDMCritSectLeave(&pData->CritSect);
    STAM_PROFILE_ADV_STOP(&pData->StatTimer, a);
}


/**
 * Restore timer callback.
 *
 * This is only called when've restored a saved state and temporarily
 * disconnected the network link to inform the guest that network connections
 * should be considered lost.
 *
 * @param   pDevIns         Device instance of the device which registered the timer.
 * @param   pTimer          The timer handle.
 */
static DECLCALLBACK(void) pcnetTimerRestore(PPDMDEVINS pDevIns, PTMTIMER pTimer)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);
    int         rc = PDMCritSectEnter(&pData->CritSect, VERR_PERMISSION_DENIED);
    AssertReleaseRC(rc);

    rc = VERR_GENERAL_FAILURE;
    if (pData->cLinkDownReported <= PCNET_MAX_LINKDOWN_REPORTED)
        rc = TMTimerSetMillies(pData->pTimerRestore, 1500);
    if (VBOX_FAILURE(rc))
    {
        pData->fLinkTempDown = false;
        if (pData->fLinkUp)
        {
            LogRel(("PCNet#%d: The link is back up again after the restore.\n",
                    pDevIns->iInstance));
            Log(("#%d pcnetTimerRestore: Clearing ERR and CERR after load. cLinkDownReported=%d\n",
                 pDevIns->iInstance, pData->cLinkDownReported));
            pData->aCSR[0] &= ~(BIT(15) | BIT(13)); /* ERR | CERR - probably not 100% correct either... */
            pData->Led.Actual.s.fError = 0;
        }
    }
    else
        Log(("#%d pcnetTimerRestore: cLinkDownReported=%d, wait another 1500ms...\n",
             pDevIns->iInstance, pData->cLinkDownReported));

    PDMCritSectLeave(&pData->CritSect);
}


/**
 * Callback function for mapping an PCI I/O region.
 *
 * @return VBox status code.
 * @param   pPciDev         Pointer to PCI device. Use pPciDev->pDevIns to get the device instance.
 * @param   iRegion         The region number.
 * @param   GCPhysAddress   Physical address of the region. If iType is PCI_ADDRESS_SPACE_IO, this is an
 *                          I/O port, else it's a physical address.
 *                          This address is *NOT* relative to pci_mem_base like earlier!
 * @param   cb              Region size.
 * @param   enmType         One of the PCI_ADDRESS_SPACE_* values.
 */
static DECLCALLBACK(int) pcnetIOPortMap(PPCIDEVICE pPciDev, /*unsigned*/ int iRegion,
                                        RTGCPHYS GCPhysAddress, uint32_t cb, PCIADDRESSSPACE enmType)
{
    int         rc;
    PPDMDEVINS  pDevIns = pPciDev->pDevIns;
    RTIOPORT    Port    = (RTIOPORT)GCPhysAddress;
    PCNetState *pData   = PCIDEV_2_PCNETSTATE(pPciDev);

    Assert(enmType == PCI_ADDRESS_SPACE_IO);
    Assert(cb >= 0x20);

    rc = PDMDevHlpIOPortRegister(pDevIns, Port, 0x10, 0, pcnetIOPortAPromWrite,
                                 pcnetIOPortAPromRead, NULL, NULL, "PCNet ARPOM");
    if (VBOX_FAILURE(rc))
        return rc;
    rc = PDMDevHlpIOPortRegister(pDevIns, Port + 0x10, 0x10, 0, pcnetIOPortWrite,
                                 pcnetIOPortRead, NULL, NULL, "PCNet");
    if (VBOX_FAILURE(rc))
        return rc;

    if (pData->fGCEnabled)
    {
        rc = PDMDevHlpIOPortRegisterGC(pDevIns, Port, 0x10, 0, "pcnetIOPortAPromWrite",
                                       "pcnetIOPortAPromRead", NULL, NULL, "PCNet aprom");
        if (VBOX_FAILURE(rc))
            return rc;
        rc = PDMDevHlpIOPortRegisterGC(pDevIns, Port + 0x10, 0x10, 0, "pcnetIOPortWrite",
                                       "pcnetIOPortRead", NULL, NULL, "PCNet");
        if (VBOX_FAILURE(rc))
            return rc;
    }
    if (pData->fR0Enabled)
    {
        rc = PDMDevHlpIOPortRegisterR0(pDevIns, Port, 0x10, 0, "pcnetIOPortAPromWrite",
                                       "pcnetIOPortAPromRead", NULL, NULL, "PCNet aprom");
        if (VBOX_FAILURE(rc))
            return rc;
        rc = PDMDevHlpIOPortRegisterR0(pDevIns, Port + 0x10, 0x10, 0, "pcnetIOPortWrite",
                                       "pcnetIOPortRead", NULL, NULL, "PCNet");
        if (VBOX_FAILURE(rc))
            return rc;
    }

    pData->IOPortBase = Port;
    return VINF_SUCCESS;
}


/**
 * Callback function for mapping the MMIO region.
 *
 * @return VBox status code.
 * @param   pPciDev         Pointer to PCI device. Use pPciDev->pDevIns to get the device instance.
 * @param   iRegion         The region number.
 * @param   GCPhysAddress   Physical address of the region. If iType is PCI_ADDRESS_SPACE_IO, this is an
 *                          I/O port, else it's a physical address.
 *                          This address is *NOT* relative to pci_mem_base like earlier!
 * @param   cb              Region size.
 * @param   enmType         One of the PCI_ADDRESS_SPACE_* values.
 */
static DECLCALLBACK(int) pcnetMMIOMap(PPCIDEVICE pPciDev, /*unsigned*/ int iRegion,
                                      RTGCPHYS GCPhysAddress, uint32_t cb, PCIADDRESSSPACE enmType)
{
    PCNetState *pData = PCIDEV_2_PCNETSTATE(pPciDev);
    int         rc;

    Assert(enmType == PCI_ADDRESS_SPACE_MEM);
    Assert(cb >= PCNET_PNPMMIO_SIZE);

    /* We use the assigned size here, because we currently only support page aligned MMIO ranges. */
    rc = PDMDevHlpMMIORegister(pPciDev->pDevIns, GCPhysAddress, cb, pData,
                               pcnetMMIOWrite, pcnetMMIORead, NULL, "PCNet");
    if (VBOX_FAILURE(rc))
        return rc;
    pData->MMIOBase = GCPhysAddress;
    return rc;
}


/**
 * PCNET status info callback.
 *
 * @param   pDevIns     The device instance.
 * @param   pHlp        The output helpers.
 * @param   pszArgs     The arguments.
 */
static DECLCALLBACK(void) pcnetInfo(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);
    bool        fRcvRing = false;
    bool        fXmtRing = false;

    /*
     * Parse args.
     */
    if (pszArgs)
    {
        fRcvRing = strstr(pszArgs, "verbose") || strstr(pszArgs, "rcv");
        fXmtRing = strstr(pszArgs, "verbose") || strstr(pszArgs, "xmt");
    }

    /*
     * Show info.
     */
    pHlp->pfnPrintf(pHlp,
                    "pcnet #%d: port=%RTiop mmio=%RGp mac-cfg=%.*Rhxs %s\n",
                    pDevIns->iInstance,
                    pData->IOPortBase, pData->MMIOBase, sizeof(pData->MacConfigured), &pData->MacConfigured,
                    pData->fAm79C973 ? "Am79C973" : "Am79C970A", pData->fGCEnabled ? " GC" : "", pData->fR0Enabled ? " R0" : "");

    PDMCritSectEnter(&pData->CritSect, VERR_INTERNAL_ERROR); /* Take it here so we know why we're hanging... */

    pHlp->pfnPrintf(pHlp,
                    "CSR0=%04RX32:\n",
                    pData->aCSR[0]);

    pHlp->pfnPrintf(pHlp,
                    "CSR1=%04RX32:\n",
                    pData->aCSR[1]);

    pHlp->pfnPrintf(pHlp,
                    "CSR2=%04RX32:\n",
                    pData->aCSR[2]);

    pHlp->pfnPrintf(pHlp,
                    "CSR3=%04RX32: BSWP=%d EMBA=%d DXMT2PD=%d LAPPEN=%d DXSUFLO=%d IDONM=%d TINTM=%d RINTM=%d MERRM=%d MISSM=%d BABLM=%d\n",
                    pData->aCSR[3],
                    !!(pData->aCSR[3] & BIT(2)), !!(pData->aCSR[3] & BIT(3)), !!(pData->aCSR[3] & BIT(4)), CSR_LAPPEN(pData),
                    CSR_DXSUFLO(pData), !!(pData->aCSR[3] & BIT(8)), !!(pData->aCSR[3] & BIT(9)), !!(pData->aCSR[3] & BIT(10)),
                    !!(pData->aCSR[3] & BIT(11)), !!(pData->aCSR[3] & BIT(12)), !!(pData->aCSR[3] & BIT(14)));

    pHlp->pfnPrintf(pHlp,
                    "CSR4=%04RX32: JABM=%d JAB=%d TXSTRM=%d TXSTRT=%d RCVCOOM=%d RCVCCO=%d UINT=%d UINTCMD=%d\n"
                    "            MFCOM=%d MFCO=%d ASTRP_RCV=%d APAD_XMT=%d DPOLL=%d TIMER=%d EMAPLUS=%d EN124=%d\n",
                    pData->aCSR[4],
                    !!(pData->aCSR[4] & BIT( 0)), !!(pData->aCSR[4] & BIT( 1)), !!(pData->aCSR[4] & BIT( 2)), !!(pData->aCSR[4] & BIT( 3)),
                    !!(pData->aCSR[4] & BIT( 4)), !!(pData->aCSR[4] & BIT( 5)), !!(pData->aCSR[4] & BIT( 6)), !!(pData->aCSR[4] & BIT( 7)),
                    !!(pData->aCSR[4] & BIT( 8)), !!(pData->aCSR[4] & BIT( 9)), !!(pData->aCSR[4] & BIT(10)), !!(pData->aCSR[4] & BIT(11)),
                    !!(pData->aCSR[4] & BIT(12)), !!(pData->aCSR[4] & BIT(13)), !!(pData->aCSR[4] & BIT(14)), !!(pData->aCSR[4] & BIT(15)));

    pHlp->pfnPrintf(pHlp,
                    "CSR5=%04RX32:\n",
                    pData->aCSR[5]);

    pHlp->pfnPrintf(pHlp,
                    "CSR6=%04RX32: RLEN=%03x* TLEN=%03x* [* encoded]\n",
                    pData->aCSR[6],
                    (pData->aCSR[6] >> 8) & 0xf, (pData->aCSR[6] >> 12) & 0xf);

    pHlp->pfnPrintf(pHlp,
                    "CSR8..11=%04RX32,%04RX32,%04RX32,%04RX32: LADRF=%016RX64\n",
                    pData->aCSR[8], pData->aCSR[9], pData->aCSR[10], pData->aCSR[11],
                      (uint64_t)(pData->aCSR[ 8] & 0xffff)
                    | (uint64_t)(pData->aCSR[ 9] & 0xffff) << 16
                    | (uint64_t)(pData->aCSR[10] & 0xffff) << 32
                    | (uint64_t)(pData->aCSR[11] & 0xffff) << 48);

    pHlp->pfnPrintf(pHlp,
                    "CSR12..14=%04RX32,%04RX32,%04RX32: PADR=%02x %02x %02x %02x %02x %02x (Current MAC Address)\n",
                    pData->aCSR[12], pData->aCSR[13], pData->aCSR[14],
                     pData->aCSR[12]       & 0xff,
                    (pData->aCSR[12] >> 8) & 0xff,
                     pData->aCSR[13]       & 0xff,
                    (pData->aCSR[13] >> 8) & 0xff,
                     pData->aCSR[14]       & 0xff,
                    (pData->aCSR[14] >> 8) & 0xff);

    pHlp->pfnPrintf(pHlp,
                    "CSR15=%04RX32: DXR=%d DTX=%d LOOP=%d DXMTFCS=%d FCOLL=%d DRTY=%d INTL=%d PORTSEL=%d LTR=%d\n"
                    "            MENDECL=%d DAPC=%d DLNKTST=%d DRCVPV=%d DRCVBC=%d PROM=%d\n",
                    pData->aCSR[15],
                    !!(pData->aCSR[15] & BIT( 0)), !!(pData->aCSR[15] & BIT( 1)), !!(pData->aCSR[15] & BIT( 2)), !!(pData->aCSR[15] & BIT( 3)),
                    !!(pData->aCSR[15] & BIT( 4)), !!(pData->aCSR[15] & BIT( 5)), !!(pData->aCSR[15] & BIT( 6)),   (pData->aCSR[15] >> 7) & 3,
                                                   !!(pData->aCSR[15] & BIT( 9)), !!(pData->aCSR[15] & BIT(10)), !!(pData->aCSR[15] & BIT(11)),
                    !!(pData->aCSR[15] & BIT(12)), !!(pData->aCSR[15] & BIT(13)), !!(pData->aCSR[15] & BIT(14)), !!(pData->aCSR[15] & BIT(15)));

    pHlp->pfnPrintf(pHlp,
                    "CSR46=%04RX32: POLL=%04x (Poll Time Counter)\n",
                    pData->aCSR[46], pData->aCSR[46] & 0xffff);

    pHlp->pfnPrintf(pHlp,
                    "CSR47=%04RX32: POLLINT=%04x (Poll Time Interval)\n",
                    pData->aCSR[47], pData->aCSR[47] & 0xffff);

    pHlp->pfnPrintf(pHlp,
                    "CSR58=%04RX32: SWSTYLE=%02x %s SSIZE32=%d CSRPCNET=%d APERRENT=%d\n",
                    pData->aCSR[58],
                    pData->aCSR[58] & 0x7f,
                    (pData->aCSR[58] & 0x7f) == 0 ? "C-LANCE / PCnet-ISA"
                    : (pData->aCSR[58] & 0x7f) == 1 ? "ILACC"
                    : (pData->aCSR[58] & 0x7f) == 2 ? "PCNet-PCI II"
                    : (pData->aCSR[58] & 0x7f) == 3 ? "PCNet-PCI II controller"
                    : "!!reserved!!",
                    !!(pData->aCSR[58] & BIT(8)), !!(pData->aCSR[58] & BIT(9)), !!(pData->aCSR[58] & BIT(10)));

    pHlp->pfnPrintf(pHlp,
                    "CSR112=%04RX32: MFC=%04x (Missed receive Frame Count)\n",
                    pData->aCSR[112], pData->aCSR[112] & 0xffff);

    pHlp->pfnPrintf(pHlp,
                    "CSR122=%04RX32: RCVALGN=%04x (Receive Frame Align)\n",
                    pData->aCSR[122], !!(pData->aCSR[122] & BIT(0)));

    pHlp->pfnPrintf(pHlp,
                    "CSR124=%04RX32: RPA=%04x (Runt Packet Accept)\n",
                    pData->aCSR[122], !!(pData->aCSR[122] & BIT(3)));


    /*
     * Dump the receive ring.
     */
    pHlp->pfnPrintf(pHlp,
                    "RCVRL=%04x RCVRC=%04x  GCRDRA=%RX32 \n"
                    "CRDA=%08RX32 CRBA=%08RX32 CRBC=%03x CRST=%04x\n"
                    "NRDA=%08RX32 NRBA=%08RX32 NRBC=%03x NRST=%04x\n"
                    "NNRDA=%08RX32\n"
                    ,
                    CSR_RCVRL(pData), CSR_RCVRC(pData), pData->GCRDRA,
                    CSR_CRDA(pData), CSR_CRBA(pData), CSR_CRBC(pData), CSR_CRST(pData),
                    CSR_NRDA(pData), CSR_NRBA(pData), CSR_NRBC(pData), CSR_NRST(pData),
                    CSR_NNRD(pData));
    if (fRcvRing)
    {
        const unsigned  cb = 1 << pData->iLog2DescSize;
        RTGCPHYS        GCPhys = pData->GCRDRA;
        unsigned        i = CSR_RCVRL(pData);
        while (i-- > 0)
        {
            RMD rmd;
            pcnetRmdLoad(pData, &rmd, PHYSADDR(pData, GCPhys));
            pHlp->pfnPrintf(pHlp,
                            "%04x %RGp:%c%c RBADR=%08RX32 BCNT=%03x MCNT=%03x "
                            "OWN=%d ERR=%d FRAM=%d OFLO=%d CRC=%d BUFF=%d STP=%d ENP=%d BPE=%d "
                            "PAM=%d LAFM=%d BAM=%d RCC=%02x RPC=%02x ONES=%x ZEROS=%d\n",
                            i, GCPhys, i + 1 == CSR_RCVRC(pData) ? '*' : ' ', GCPhys == CSR_CRDA(pData) ? '*' : ' ',
                            rmd.rmd0.rbadr, 4096 - rmd.rmd1.bcnt, rmd.rmd2.mcnt,
                            rmd.rmd1.own, rmd.rmd1.err, rmd.rmd1.fram, rmd.rmd1.oflo, rmd.rmd1.crc, rmd.rmd1.buff,
                            rmd.rmd1.stp, rmd.rmd1.enp, rmd.rmd1.bpe,
                            rmd.rmd1.pam, rmd.rmd1.lafm, rmd.rmd1.bam, rmd.rmd2.rcc, rmd.rmd2.rpc,
                            rmd.rmd1.ones, rmd.rmd2.zeros);

            GCPhys += cb;
        }
    }

    /*
     * Dump the transmit ring.
     */
    pHlp->pfnPrintf(pHlp,
                    "XMTRL=%04x XMTRC=%04x  GCTDRA=%08RX32 BADX=%08RX32\n"
                    "PXDA=%08RX32               PXBC=%03x PXST=%04x\n"
                    "CXDA=%08RX32 CXBA=%08RX32 CXBC=%03x CXST=%04x\n"
                    "NXDA=%08RX32 NXBA=%08RX32 NXBC=%03x NXST=%04x\n"
                    "NNXDA=%08RX32\n"
                    ,
                    CSR_XMTRL(pData), CSR_XMTRC(pData),
                    pData->GCTDRA, CSR_BADX(pData),
                    CSR_PXDA(pData),                  CSR_PXBC(pData), CSR_PXST(pData),
                    CSR_CXDA(pData), CSR_CXBA(pData), CSR_CXBC(pData), CSR_CXST(pData),
                    CSR_NXDA(pData), CSR_NXBA(pData), CSR_NXBC(pData), CSR_NXST(pData),
                    CSR_NNXD(pData));
    if (fXmtRing)
    {
        const unsigned  cb = 1 << pData->iLog2DescSize;
        RTGCPHYS        GCPhys = pData->GCTDRA;
        unsigned        i = CSR_RCVRL(pData);
        while (i-- > 0)
        {
            TMD tmd;
            pcnetTmdLoad(pData, &tmd, PHYSADDR(pData, GCPhys));
            pHlp->pfnPrintf(pHlp,
                            "%04x %RGp:%c%c TBADR=%08RX32 BCNT=%03x OWN=%d "
                            "ERR=%d NOFCS=%d LTINT=%d ONE=%d DEF=%d STP=%d ENP=%d BPE=%d "
                            "BUFF=%d UFLO=%d EXDEF=%d LCOL=%d LCAR=%d RTRY=%d TDR=%03x TRC=%x ONES=%x\n"
                            ,
                            i, GCPhys, i + 1 == CSR_XMTRC(pData) ? '*' : ' ', GCPhys == CSR_CXDA(pData) ? '*' : ' ',
                            tmd.tmd0.tbadr, 4096 - tmd.tmd1.bcnt,
                            tmd.tmd2.tdr,
                            tmd.tmd2.trc,
                            tmd.tmd1.own,
                            tmd.tmd1.err,
                            tmd.tmd1.nofcs,
                            tmd.tmd1.ltint,
                            tmd.tmd1.one,
                            tmd.tmd1.def,
                            tmd.tmd1.stp,
                            tmd.tmd1.enp,
                            tmd.tmd1.bpe,
                            tmd.tmd2.buff,
                            tmd.tmd2.uflo,
                            tmd.tmd2.exdef,
                            tmd.tmd2.lcol,
                            tmd.tmd2.lcar,
                            tmd.tmd2.rtry,
                            tmd.tmd2.tdr,
                            tmd.tmd2.trc,
                            tmd.tmd1.ones);

            GCPhys += cb;
        }
    }

    PDMCritSectLeave(&pData->CritSect);
}


/**
 * Prepares for state saving.
 * We must stop the RX process to prevent altering of the main memory after saving.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pSSMHandle  The handle to save the state to.
 */
static DECLCALLBACK(int) pcnetSavePrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSMHandle)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);

    PDMCritSectEnter(&pData->CritSect, VERR_PERMISSION_DENIED);

    pData->fSaving = true;
    /* From now on drop all received packets to prevent altering of main memory after
     * pgmR3Save() was called but before the RX thread is terminated */

    PDMCritSectLeave(&pData->CritSect);
    return VINF_SUCCESS;
}


/**
 * Saves a state of the PC-Net II device.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pSSMHandle  The handle to save the state to.
 */
static DECLCALLBACK(int) pcnetSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSMHandle)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);

    SSMR3PutBool(pSSMHandle, pData->fLinkUp);
    SSMR3PutU32(pSSMHandle, pData->u32RAP);
    SSMR3PutS32(pSSMHandle, pData->iISR);
    SSMR3PutU32(pSSMHandle, pData->u32Lnkst);
    SSMR3PutGCPhys(pSSMHandle, pData->GCRDRA);
    SSMR3PutGCPhys(pSSMHandle, pData->GCTDRA);
    SSMR3PutMem(pSSMHandle, pData->aPROM, sizeof(pData->aPROM));
    SSMR3PutMem(pSSMHandle, pData->aCSR, sizeof(pData->aCSR));
    SSMR3PutMem(pSSMHandle, pData->aBCR, sizeof(pData->aBCR));
    SSMR3PutMem(pSSMHandle, pData->aMII, sizeof(pData->aMII));
    SSMR3PutU16(pSSMHandle, pData->u16CSR0LastSeenByGuest);
    SSMR3PutU64(pSSMHandle, pData->u64LastPoll);
    SSMR3PutMem(pSSMHandle, &pData->MacConfigured, sizeof(pData->MacConfigured));
    SSMR3PutBool(pSSMHandle, pData->fAm79C973);
#ifdef PCNET_NO_POLLING
    return VINF_SUCCESS;
#else
    return TMR3TimerSave(pData->CTXSUFF(pTimerPoll), pSSMHandle);
#endif
}


/**
 * Loads a saved PC-Net II device state.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pSSMHandle  The handle to the saved state.
 * @param   u32Version  The data unit version number.
 */
static DECLCALLBACK(int) pcnetLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSMHandle, uint32_t u32Version)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);
    PDMMAC      Mac;
    if (u32Version != PCNET_SAVEDSTATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* restore data */
    SSMR3GetBool(pSSMHandle, &pData->fLinkUp);
    SSMR3GetU32(pSSMHandle, &pData->u32RAP);
    SSMR3GetS32(pSSMHandle, &pData->iISR);
    SSMR3GetU32(pSSMHandle, &pData->u32Lnkst);
    SSMR3GetGCPhys(pSSMHandle, &pData->GCRDRA);
    SSMR3GetGCPhys(pSSMHandle, &pData->GCTDRA);
    SSMR3GetMem(pSSMHandle, &pData->aPROM, sizeof(pData->aPROM));
    SSMR3GetMem(pSSMHandle, &pData->aCSR, sizeof(pData->aCSR));
    SSMR3GetMem(pSSMHandle, &pData->aBCR, sizeof(pData->aBCR));
    SSMR3GetMem(pSSMHandle, &pData->aMII, sizeof(pData->aMII));
    SSMR3GetU16(pSSMHandle, &pData->u16CSR0LastSeenByGuest);
    SSMR3GetU64(pSSMHandle, &pData->u64LastPoll);
    SSMR3GetMem(pSSMHandle, &Mac, sizeof(Mac));
    Assert(!memcmp(&Mac, &pData->MacConfigured, sizeof(Mac)));
    SSMR3GetBool(pSSMHandle, &pData->fAm79C973);
#ifndef PCNET_NO_POLLING
    TMR3TimerLoad(pData->CTXSUFF(pTimerPoll), pSSMHandle);
#endif

    pData->iLog2DescSize = BCR_SWSTYLE(pData)
                         ? 4
                         : 3;
    pData->GCUpperPhys   = BCR_SSIZE32(pData)
                         ? 0
                         : (0xff00 & (uint32_t)pData->aCSR[2]) << 16;

    /* update promiscuous mode. */
    if (pData->pDrv)
        pData->pDrv->pfnSetPromiscuousMode(pData->pDrv, CSR_PROM(pData));

#ifdef PCNET_NO_POLLING
    /* Enable physical monitoring again (!) */
    pcnetUpdateRingHandlers(pData);
#endif
    /* Indicate link down to the guest OS that all network connections have been lost. */
    if (pData->fLinkUp)
    {
        pData->fLinkTempDown = true;
        pData->cLinkDownReported = 0;
        pData->aCSR[0] |= BIT(15) | BIT(13); /* ERR | CERR (this is probably wrong) */
        pData->Led.Asserted.s.fError = pData->Led.Actual.s.fError = 1;
        return TMTimerSetMillies(pData->pTimerRestore, 5000);
    }
    return VINF_SUCCESS;
}


/**
 * Queries an interface to the driver.
 *
 * @returns Pointer to interface.
 * @returns NULL if the interface was not supported by the driver.
 * @param   pInterface          Pointer to this interface structure.
 * @param   enmInterface        The requested interface identification.
 * @thread  Any thread.
 */
static DECLCALLBACK(void *) pcnetQueryInterface(struct PDMIBASE *pInterface, PDMINTERFACE enmInterface)
{
    PCNetState *pData = (PCNetState *)((uintptr_t)pInterface - RT_OFFSETOF(PCNetState, IBase));
    Assert(&pData->IBase == pInterface);
    switch (enmInterface)
    {
        case PDMINTERFACE_BASE:
            return &pData->IBase;
        case PDMINTERFACE_NETWORK_PORT:
            return &pData->INetworkPort;
        case PDMINTERFACE_NETWORK_CONFIG:
            return &pData->INetworkConfig;
        case PDMINTERFACE_LED_PORTS:
            return &pData->ILeds;
        default:
            return NULL;
    }
}

/** Converts a pointer to PCNetState::INetworkPort to a PCNetState pointer. */
#define INETWORKPORT_2_DATA(pInterface)  ( (PCNetState *)((uintptr_t)pInterface - RT_OFFSETOF(PCNetState, INetworkPort)) )


/**
 * Check if the device/driver can receive data now.
 * This must be called before the pfnRecieve() method is called.
 *
 * @returns Number of bytes the driver can receive.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @thread  EMT
 */
static DECLCALLBACK(size_t) pcnetCanReceive(PPDMINETWORKPORT pInterface)
{
    size_t cb;
    int    rc;
    PCNetState *pData = INETWORKPORT_2_DATA(pInterface);

    rc = PDMCritSectEnter(&pData->CritSect, VERR_PERMISSION_DENIED);
    AssertReleaseRC(rc);

    cb = pcnetCanReceiveNoSync(pData);

    PDMCritSectLeave(&pData->CritSect);
    return cb;
}


/**
 * Receive data from the network.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   pvBuf           The available data.
 * @param   cb              Number of bytes available in the buffer.
 * @thread  EMT
 */
static DECLCALLBACK(int) pcnetReceive(PPDMINETWORKPORT pInterface, const void *pvBuf, size_t cb)
{
    PCNetState *pData = INETWORKPORT_2_DATA(pInterface);
    int         rc;

    STAM_PROFILE_ADV_START(&pData->StatReceive, a);
    rc = PDMCritSectEnter(&pData->CritSect, VERR_PERMISSION_DENIED);
    AssertReleaseRC(rc);

    if (!pData->fSaving)
    {
        if (cb > 70) /* unqualified guess */
            pData->Led.Asserted.s.fReading = pData->Led.Actual.s.fReading = 1;
        pcnetReceiveNoSync(pData, (const uint8_t*)pvBuf, cb);
        pData->Led.Actual.s.fReading = 0;
    }
    /* otherwise junk the data to Nirwana. */

    PDMCritSectLeave(&pData->CritSect);
    STAM_PROFILE_ADV_STOP(&pData->StatReceive, a);

    return VINF_SUCCESS;
}

/** Converts a pointer to PCNetState::INetworkConfig to a PCNetState pointer. */
#define INETWORKCONFIG_2_DATA(pInterface)  ( (PCNetState *)((uintptr_t)pInterface - RT_OFFSETOF(PCNetState, INetworkConfig)) )


/**
 * Gets the current Media Access Control (MAC) address.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   pMac            Where to store the MAC address.
 * @thread  EMT
 */
static DECLCALLBACK(int) pcnetGetMac(PPDMINETWORKCONFIG pInterface, PPDMMAC *pMac)
{
    PCNetState *pData = INETWORKCONFIG_2_DATA(pInterface);
    memcpy(pMac, pData->aPROM, sizeof(*pMac));
    return VINF_SUCCESS;
}


/**
 * Gets the new link state.
 *
 * @returns The current link state.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @thread  EMT
 */
static DECLCALLBACK(PDMNETWORKLINKSTATE) pcnetGetLinkState(PPDMINETWORKCONFIG pInterface)
{
    PCNetState *pData = INETWORKCONFIG_2_DATA(pInterface);
    if (pData->fLinkUp && !pData->fLinkTempDown)
        return PDMNETWORKLINKSTATE_UP;
    if (!pData->fLinkUp)
        return PDMNETWORKLINKSTATE_DOWN;
    if (pData->fLinkTempDown)
        return PDMNETWORKLINKSTATE_DOWN_RESUME;
    AssertMsgFailed(("Invalid link state!\n"));
    return PDMNETWORKLINKSTATE_INVALID;
}


/**
 * Sets the new link state.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   enmState        The new link state
 * @thread  EMT
 */
static DECLCALLBACK(int) pcnetSetLinkState(PPDMINETWORKCONFIG pInterface, PDMNETWORKLINKSTATE enmState)
{
    PCNetState *pData = INETWORKCONFIG_2_DATA(pInterface);
    bool fLinkUp;
    if (    enmState != PDMNETWORKLINKSTATE_DOWN
        &&  enmState != PDMNETWORKLINKSTATE_UP)
    {
        AssertMsgFailed(("Invalid parameter enmState=%d\n", enmState));
        return VERR_INVALID_PARAMETER;
    }

    /* has the state changed? */
    fLinkUp = enmState == PDMNETWORKLINKSTATE_UP;
    if (pData->fLinkUp != fLinkUp)
    {
        pData->fLinkUp = fLinkUp;
        if (fLinkUp)
        {
            /* connect */
            pData->aCSR[0] &= ~(BIT(15) | BIT(13)); /* ERR | CERR - probably not 100% correct either... */
            pData->Led.Actual.s.fError = 0;
        }
        else
        {
            /* disconnect */
            pData->cLinkDownReported = 0;
            pData->aCSR[0] |= BIT(15) | BIT(13); /* ERR | CERR (this is probably wrong) */
            pData->Led.Asserted.s.fError = pData->Led.Actual.s.fError = 1;
        }
        Assert(!PDMCritSectIsOwner(&pData->CritSect));
        pData->pDrv->pfnNotifyLinkChanged(pData->pDrv, enmState);
    }
    return VINF_SUCCESS;
}


/**
 * Gets the pointer to the status LED of a unit.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   iLUN            The unit which status LED we desire.
 * @param   ppLed           Where to store the LED pointer.
 */
static DECLCALLBACK(int) pcnetQueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PCNetState *pData = (PCNetState *)( (uintptr_t)pInterface - RT_OFFSETOF(PCNetState, ILeds) );
    if (iLUN == 0)
    {
        *ppLed = &pData->Led;
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}


/**
 * @copydoc FNPDMDEVRESET
 */
static DECLCALLBACK(void) pcnetReset(PPDMDEVINS pDevIns)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);
    if (pData->fLinkTempDown)
    {
        pData->cLinkDownReported = 0x10000;
        TMTimerStop(pData->pTimerRestore);
        pcnetTimerRestore(pDevIns, pData->pTimerRestore);
    }

    /** @todo How to flush the queues? */
    pcnetHardReset(pData);
}


/**
 * @copydoc FNPDMDEVRELOCATE
 */
static DECLCALLBACK(void) pcnetRelocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);
    pData->pDevInsGC     = PDMDEVINS_2_GCPTR(pDevIns);
    pData->pXmitQueueGC  = PDMQueueGCPtr(pData->pXmitQueueHC);
    pData->pCanRxQueueGC = PDMQueueGCPtr(pData->pCanRxQueueHC);
#ifdef PCNET_NO_POLLING
    *(RTHCUINTPTR *)&pData->pfnEMInterpretInstructionGC += offDelta;
#else
    pData->pTimerPollGC  = TMTimerGCPtr(pData->pTimerPollHC);
#endif
}


/**
 * Destruct a device instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @returns VBox status.
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(int) pcnetDestruct(PPDMDEVINS pDevIns)
{
    PCNetState *pData = PDMINS2DATA(pDevIns, PCNetState *);

    PDMCritSectEnter(&pData->CritSect, VERR_ACCESS_DENIED);

    RTSemEventDestroy(pData->hSendEventSem);
    pData->hSendEventSem = 0;
    PDMCritSectLeave(&pData->CritSect);

    PDMR3CritSectDelete(&pData->CritSect);
    return VINF_SUCCESS;
}


/**
 * Construct a device instance for a VM.
 *
 * @returns VBox status.
 * @param   pDevIns     The device instance data.
 *                      If the registration structure is needed, pDevIns->pDevReg points to it.
 * @param   iInstance   Instance number. Use this to figure out which registers and such to use.
 *                      The device number is also found in pDevIns->iInstance, but since it's
 *                      likely to be freqently used PDM passes it as parameter.
 * @param   pCfgHandle  Configuration node handle for the device. Use this to obtain the configuration
 *                      of the device instance. It's also found in pDevIns->pCfgHandle, but like
 *                      iInstance it's expected to be used a bit in this function.
 */
static DECLCALLBACK(int) pcnetConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfgHandle)
{
    PCNetState     *pData = PDMINS2DATA(pDevIns, PCNetState *);
    PPDMIBASE       pBase;
    char            szTmp[128];
    int             rc;

    /* up to four instances are supported */
    Assert((iInstance >= 0) && (iInstance < 4));

    Assert(RT_ELEMENTS(pData->aBCR) == BCR_MAX_RAP);
    Assert(RT_ELEMENTS(pData->aMII) == MII_MAX_REG);
    Assert(sizeof(pData->abSendBuf) == RT_ALIGN_Z(sizeof(pData->abSendBuf), 16));

    /*
     * Validate configuration.
     */
    if (!CFGMR3AreValuesValid(pCfgHandle, "MAC\0CableConnected\0Am79C973\0GCEnabled\0R0Enabled\0"))
        return PDMDEV_SET_ERROR(pDevIns, VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES,
                                N_("Invalid configuraton for pcnet device"));

    /*
     * Read the configuration.
     */
    rc = CFGMR3QueryBytes(pCfgHandle, "MAC", &pData->MacConfigured, sizeof(pData->MacConfigured));
    if (VBOX_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"MAC\" value"));
    rc = CFGMR3QueryBool(pCfgHandle, "CableConnected", &pData->fLinkUp);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pData->fLinkUp = true;
    else if (VBOX_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"CableConnected\" value"));

    rc = CFGMR3QueryBool(pCfgHandle, "Am79C973", &pData->fAm79C973);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pData->fAm79C973 = false;
    else if (VBOX_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"Am79C973\" value"));

#ifdef PCNET_GC_ENABLED
    rc = CFGMR3QueryBool(pCfgHandle, "GCEnabled", &pData->fGCEnabled);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pData->fGCEnabled = true;
    else if (VBOX_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"GCEnabled\" value"));

    rc = CFGMR3QueryBool(pCfgHandle, "R0Enabled", &pData->fR0Enabled);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pData->fR0Enabled = true;
    else if (VBOX_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"R0Enabled\" value"));

#else /* !PCNET_GC_ENABLED */
    pData->fGCEnabled = false;
    pData->fR0Enabled = false;
#endif /* !PCNET_GC_ENABLED */


    /*
     * Initialize data (most of it anyway).
     */
    pData->pDevInsHC                        = pDevIns;
    pData->pDevInsGC                        = PDMDEVINS_2_GCPTR(pDevIns);
    pData->Led.u32Magic                     = PDMLED_MAGIC;
    /* IBase */
    pData->IBase.pfnQueryInterface          = pcnetQueryInterface;
    /* INeworkPort */
    pData->INetworkPort.pfnCanReceive       = pcnetCanReceive;
    pData->INetworkPort.pfnReceive          = pcnetReceive;
    /* INetworkConfig */
    pData->INetworkConfig.pfnGetMac         = pcnetGetMac;
    pData->INetworkConfig.pfnGetLinkState   = pcnetGetLinkState;
    pData->INetworkConfig.pfnSetLinkState   = pcnetSetLinkState;
    /* ILeds */
    pData->ILeds.pfnQueryStatusLed          = pcnetQueryStatusLed;

    /* PCI Device */
    pData->PciDev.config[0x00] = 0x22; /* vendor id */
    pData->PciDev.config[0x01] = 0x10;
    pData->PciDev.config[0x02] = 0x00; /* device id */
    pData->PciDev.config[0x03] = 0x20;
    pData->PciDev.config[0x04] = 0x07; /* command */
    pData->PciDev.config[0x05] = 0x00;
    pData->PciDev.config[0x06] = 0x80; /* status */
    pData->PciDev.config[0x07] = 0x02;
    pData->PciDev.config[0x08] = pData->fAm79C973 ? 0x30 : 0x10; /* revision */
    pData->PciDev.config[0x09] = 0x00;
    pData->PciDev.config[0x0a] = 0x00; /* ethernet network controller */
    pData->PciDev.config[0x0b] = 0x02;
    pData->PciDev.config[0x0e] = 0x00; /* header_type */

    pData->PciDev.config[0x10] = 0x01; /* IO Base */
    pData->PciDev.config[0x11] = 0x00;
    pData->PciDev.config[0x12] = 0x00;
    pData->PciDev.config[0x13] = 0x00;
    pData->PciDev.config[0x14] = 0x00; /* MMIO Base */
    pData->PciDev.config[0x15] = 0x00;
    pData->PciDev.config[0x16] = 0x00;
    pData->PciDev.config[0x17] = 0x00;

    /* subsystem and subvendor IDs */
    pData->PciDev.config[0x2c] = 0x22; /* subsystem vendor id */
    pData->PciDev.config[0x2d] = 0x10;
    pData->PciDev.config[0x2e] = 0x00; /* subsystem id */
    pData->PciDev.config[0x2f] = 0x20;
    pData->PciDev.config[0x3d] = 1;    /* interrupt pin 0 */
    pData->PciDev.config[0x3e] = 0x06;
    pData->PciDev.config[0x3f] = 0xff;

    /*
     * Register the PCI device, its I/O regions, the timer and the saved state item.
     */
    rc = PDMDevHlpPCIRegister(pDevIns, &pData->PciDev);
    if (VBOX_FAILURE(rc))
        return rc;
    rc = PDMDevHlpPCIIORegionRegister(pDevIns, 0, PCNET_IOPORT_SIZE,
                                      PCI_ADDRESS_SPACE_IO, pcnetIOPortMap);
    if (VBOX_FAILURE(rc))
        return rc;
    rc = PDMDevHlpPCIIORegionRegister(pDevIns, 1, PCNET_PNPMMIO_SIZE,
                                      PCI_ADDRESS_SPACE_MEM, pcnetMMIOMap);
    if (VBOX_FAILURE(rc))
        return rc;

#ifdef PCNET_NO_POLLING
    rc = PDMR3GetSymbolR0Lazy(PDMDevHlpGetVM(pDevIns), NULL, "EMInterpretInstruction", &pData->pfnEMInterpretInstructionR0);
    if (VBOX_SUCCESS(rc))
    {
        /*
         * Resolve the GC handler.
         */
        RTGCPTR pfnHandlerGC;
        rc = PDMR3GetSymbolGCLazy(PDMDevHlpGetVM(pDevIns), NULL, "EMInterpretInstruction", (RTGCPTR *)&pData->pfnEMInterpretInstructionGC);
    }
    if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("PDMR3GetSymbolGCLazy -> %Vrc\n", rc));
        return rc;
    }
#else
    rc = PDMDevHlpTMTimerCreate(pDevIns, TMCLOCK_VIRTUAL, pcnetTimer,
                                "PCNet Poll Timer", &pData->pTimerPollHC);
    if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("pfnTMTimerCreate -> %Vrc\n", rc));
        return rc;
    }
#endif
    rc = PDMDevHlpTMTimerCreate(pDevIns, TMCLOCK_VIRTUAL, pcnetTimerRestore,
                                "PCNet Restore Timer", &pData->pTimerRestore);
    if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("pfnTMTimerCreate -> %Vrc\n", rc));
        return rc;
    }
/** @todo r=bird: we're not locking down pcnet properly during saving and loading! */
    rc = PDMDevHlpSSMRegister(pDevIns, pDevIns->pDevReg->szDeviceName, iInstance,
                              PCNET_SAVEDSTATE_VERSION, sizeof(*pData),
                              pcnetSavePrep, pcnetSaveExec, NULL,
                              NULL, pcnetLoadExec, NULL);
    if (VBOX_FAILURE(rc))
        return rc;

    /*
     * Initialize critical section.
     * This must of course be done before attaching drivers or anything else which can call us back..
     */
    char szName[24];
    RTStrPrintf(szName, sizeof(szName), "PCNet#%d", iInstance);
    rc = PDMDevHlpCritSectInit(pDevIns, &pData->CritSect, szName);
    if (VBOX_FAILURE(rc))
        return rc;

    /*
     * Create the transmit queue.
     */
    rc = PDMDevHlpPDMQueueCreate(pDevIns, sizeof(PDMQUEUEITEMCORE), 1, 0,
                                 pcnetXmitQueueConsumer, true, &pData->pXmitQueueHC);
    if (VBOX_FAILURE(rc))
        return rc;
    pData->pXmitQueueGC = PDMQueueGCPtr(pData->pXmitQueueHC);

    /*
     * Create the RX notifer signaller.
     */
    rc = PDMDevHlpPDMQueueCreate(pDevIns, sizeof(PDMQUEUEITEMCORE), 1, 0,
                                 pcnetCanRxQueueConsumer, true, &pData->pCanRxQueueHC);
    if (VBOX_FAILURE(rc))
        return rc;
    pData->pCanRxQueueGC = PDMQueueGCPtr(pData->pCanRxQueueHC);

    /*
     * Register the info item.
     */
    RTStrPrintf(szTmp, sizeof(szTmp), "pcnet%d", pDevIns->iInstance);
    PDMDevHlpDBGFInfoRegister(pDevIns, szTmp, "PCNET info.", pcnetInfo);

    /*
     * Attach status driver (optional).
     */
    rc = PDMDevHlpDriverAttach(pDevIns, PDM_STATUS_LUN, &pData->IBase, &pBase, "Status Port");
    if (VBOX_SUCCESS(rc))
        pData->pLedsConnector = (PPDMILEDCONNECTORS)
            pBase->pfnQueryInterface(pBase, PDMINTERFACE_LED_CONNECTORS);
    else if (rc != VERR_PDM_NO_ATTACHED_DRIVER)
    {
        AssertMsgFailed(("Failed to attach to status driver. rc=%Vrc\n", rc));
        return rc;
    }

    /*
     * Attach driver.
     */
    rc = PDMDevHlpDriverAttach(pDevIns, 0, &pData->IBase, &pData->pDrvBase, "Network Port");
    if (VBOX_SUCCESS(rc))
    {
        if (rc == VINF_NAT_DNS)
        {
#ifdef __LINUX__
            VMSetRuntimeError(PDMDevHlpGetVM(pDevIns), false, "NoDNSforNAT",
                              N_("A Domain Name Server (DNS) for NAT networking could not be determined. Please check your /etc/resolv.conf for <tt>nameserver</tt> entries. Either add one manually (<i>man resolv.conf</i>) or ensure that your host is correctly connected to an ISP. If you ignore this warning the guest will not be able to perform nameserver lookups and it will probably observe delays if trying so"));
#else
            VMSetRuntimeError(PDMDevHlpGetVM(pDevIns), false, "NoDNSforNAT",
                              N_("A Domain Name Server (DNS) for NAT networking could not be determined. Ensure that your host is correctly connected to an ISP. If you ignore this warning the guest will not be able to perform nameserver lookups and it will probably observe delays if trying so"));
#endif
        }
        pData->pDrv = (PPDMINETWORKCONNECTOR)
            pData->pDrvBase->pfnQueryInterface(pData->pDrvBase, PDMINTERFACE_NETWORK_CONNECTOR);
        if (!pData->pDrv)
        {
            AssertMsgFailed(("Failed to obtain the PDMINTERFACE_NETWORK_CONNECTOR interface!\n"));
            return VERR_PDM_MISSING_INTERFACE_BELOW;
        }
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        Log(("No attached driver!\n"));
    else
        return rc;

    /*
     * Reset the device state. (Do after attaching.)
     */
    pcnetHardReset(pData);

    /* Create send queue for the async send thread. */
    rc = RTSemEventCreate(&pData->hSendEventSem);
    AssertRC(rc);

    /* Create asynchronous thread */
    rc = RTThreadCreate(&pData->hSendThread, pcnetAsyncSend, (void *)pData, 128*1024, RTTHREADTYPE_IO, 0, "PCNET_SEND");
    AssertRC(rc);

#ifdef VBOX_WITH_STATISTICS
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatMMIOReadGC,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling MMIO reads in GC",         "/Devices/PCNet%d/MMIO/ReadGC", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatMMIOReadHC,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling MMIO reads in HC",         "/Devices/PCNet%d/MMIO/ReadHC", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatMMIOWriteGC,        STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling MMIO writes in GC",        "/Devices/PCNet%d/MMIO/WriteGC", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatMMIOWriteHC,        STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling MMIO writes in HC",        "/Devices/PCNet%d/MMIO/WriteHC", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatAPROMRead,          STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling APROM reads",              "/Devices/PCNet%d/IO/APROMRead", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatAPROMWrite,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling APROM writes",             "/Devices/PCNet%d/IO/APROMWrite", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatIOReadGC,           STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNetIO reads in GC",      "/Devices/PCNet%d/IO/ReadGC", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatIOReadHC,           STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNetIO reads in HC",      "/Devices/PCNet%d/IO/ReadHC", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatIOWriteGC,          STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet IO writes in GC",    "/Devices/PCNet%d/IO/WriteGC", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatIOWriteHC,          STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet IO writes in HC",    "/Devices/PCNet%d/IO/WriteHC", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatTimer,              STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet Timer",              "/Devices/PCNet%d/Timer", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatReceive,            STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet receive",            "/Devices/PCNet%d/Receive", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatTransmit,           STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet transmit in HC",     "/Devices/PCNet%d/Transmit/Total", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatTransmitSend,       STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet send transmit in HC",     "/Devices/PCNet%d/Transmit/Send", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatTdtePollGC,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet TdtePoll in GC",     "/Devices/PCNet%d/TdtePollGC", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatTdtePollHC,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet TdtePoll in HC",     "/Devices/PCNet%d/TdtePollHC", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatRdtePollGC,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet RdtePoll in GC",     "/Devices/PCNet%d/RdtePollGC", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatRdtePollHC,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet RdtePoll in HC",     "/Devices/PCNet%d/RdtePollHC", iInstance);

    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatTmdStoreGC,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet TmdStore in GC",     "/Devices/PCNet%d/TmdStoreGC", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatTmdStoreHC,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet TmdStore in HC",     "/Devices/PCNet%d/TmdStoreHC", iInstance);

    unsigned i;
    for (i = 0; i < ELEMENTS(pData->aStatXmitFlush) - 1; i++)
        PDMDevHlpSTAMRegisterF(pDevIns, &pData->aStatXmitFlush[i],  STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES, "",                                       "/Devices/PCNet%d/XmitFlushIrq/%d", iInstance, i + 1);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->aStatXmitFlush[i],      STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,    "",                                    "/Devices/PCNet%d/XmitFlushIrq/%d+", iInstance, i + 1);

    for (i = 0; i < ELEMENTS(pData->aStatXmitChainCounts) - 1; i++)
        PDMDevHlpSTAMRegisterF(pDevIns, &pData->aStatXmitChainCounts[i], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES, "",                                  "/Devices/PCNet%d/XmitChainCounts/%d", iInstance, i + 1);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->aStatXmitChainCounts[i], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,    "",                                   "/Devices/PCNet%d/XmitChainCounts/%d+", iInstance, i + 1);

    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatXmitSkipCurrent,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,    "",                                    "/Devices/PCNet%d/Xmit/Skipped", iInstance, i + 1);

    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatInterrupt,          STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet interrupt checks",   "/Devices/PCNet%d/Interrupt", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatPollTimer,          STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Profiling PCNet poll timer",         "/Devices/PCNet%d/PollTimer", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatMIIReads,           STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Number of MII reads",                "/Devices/PCNet%d/MIIReads", iInstance);
# ifdef PCNET_NO_POLLING
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatRCVRingWrite,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Nr of receive ring writes",          "/Devices/PCNet%d/Ring/RCVWrites", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatTXRingWrite,        STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Nr of transmit ring writes",         "/Devices/PCNet%d/Ring/TXWrites", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatRingWriteHC,        STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Nr of monitored ring page writes",   "/Devices/PCNet%d/Ring/HC/Writes", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatRingWriteR0,        STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Nr of monitored ring page writes",   "/Devices/PCNet%d/Ring/R0/Writes", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatRingWriteGC,        STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Nr of monitored ring page writes",   "/Devices/PCNet%d/Ring/GC/Writes", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatRingWriteFailedHC,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Nr of failed ring page writes",   "/Devices/PCNet%d/Ring/HC/Failed", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatRingWriteFailedR0,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Nr of failed ring page writes",   "/Devices/PCNet%d/Ring/R0/Failed", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatRingWriteFailedGC,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Nr of failed ring page writes",   "/Devices/PCNet%d/Ring/GC/Failed", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatRingWriteOutsideRangeHC,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Nr of monitored writes outside ring range",   "/Devices/PCNet%d/Ring/HC/Outside", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatRingWriteOutsideRangeR0,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Nr of monitored writes outside ring range",   "/Devices/PCNet%d/Ring/R0/Outside", iInstance);
    PDMDevHlpSTAMRegisterF(pDevIns, &pData->StatRingWriteOutsideRangeGC,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,     "Nr of monitored writes outside ring range",   "/Devices/PCNet%d/Ring/GC/Outside", iInstance);
# endif /* PCNET_NO_POLLING */
#endif

    return VINF_SUCCESS;
}


/**
 * The device registration structure.
 */
const PDMDEVREG g_DevicePCNet =
{
    /* u32Version */
    PDM_DEVREG_VERSION,
    /* szDeviceName */
    "pcnet",
    /* szGCMod */
#ifdef PCNET_GC_ENABLED
    "VBoxDDGC.gc",
    "VBoxDDR0.r0",
#else
    "",
    "",
#endif
    /* pszDescription */
    "AMD PC-Net II Ethernet controller.\n",
    /* fFlags */
#ifdef PCNET_GC_ENABLED
    PDM_DEVREG_FLAGS_HOST_BITS_DEFAULT | PDM_DEVREG_FLAGS_GUEST_BITS_DEFAULT | PDM_DEVREG_FLAGS_GC | PDM_DEVREG_FLAGS_R0,
#else
    PDM_DEVREG_FLAGS_HOST_BITS_DEFAULT | PDM_DEVREG_FLAGS_GUEST_BITS_DEFAULT,
#endif
    /* fClass */
    PDM_DEVREG_CLASS_NETWORK,
    /* cMaxInstances */
    4,
    /* cbInstance */
    sizeof(PCNetState),
    /* pfnConstruct */
    pcnetConstruct,
    /* pfnDestruct */
    pcnetDestruct,
    /* pfnRelocate */
    pcnetRelocate,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    pcnetReset,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnQueryInterface. */
    NULL
};

#endif /* IN_RING3 */
#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

