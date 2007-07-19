/* $Id$ */
/** @file
 * STAM Internal Header.
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

#ifndef ___STAMInternal_h
#define ___STAMInternal_h

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/stam.h>
#include <iprt/semaphore.h>

#if !defined(IN_STAM_R3) && !defined(IN_STAM_R0) && !defined(IN_STAM_GC)
# error "Not in STAM! This is an internal header!"
#endif


__BEGIN_DECLS

/** @defgroup grp_stam_int   Internals
 * @ingroup grp_stam
 * @internal
 * @{
 */

/**
 * Sample descriptor.
 */
typedef struct STAMDESC
{
    /** Pointer to the next sample. */
    struct STAMDESC    *pNext;
    /** Sample name. */
    const char         *pszName;
    /** Sample type. */
    STAMTYPE            enmType;
    /** Visibility type. */
    STAMVISIBILITY      enmVisibility;
    /** Pointer to the sample data. */
    union STAMDESCSAMPLEDATA
    {
        /** Counter. */
        PSTAMCOUNTER    pCounter;
        /** Profile. */
        PSTAMPROFILE    pProfile;
        /** Advanced profile. */
        PSTAMPROFILEADV pProfileAdv;
        /** Ratio, unsigned 32-bit. */
        PSTAMRATIOU32   pRatioU32;
        /** unsigned 8-bit. */
        uint8_t        *pu8;
        /** unsigned 16-bit. */
        uint16_t       *pu16;
        /** unsigned 32-bit. */
        uint32_t       *pu32;
        /** unsigned 64-bit. */
        uint64_t       *pu64;
        /** Simple void pointer. */
        void           *pv;
        /** */
        struct STAMDESCSAMPLEDATACALLBACKS
        {
            /** The same pointer. */
            void                   *pvSample;
            /** Pointer to the reset callback. */
            PFNSTAMR3CALLBACKRESET  pfnReset;
            /** Pointer to the print callback. */
            PFNSTAMR3CALLBACKPRINT  pfnPrint;
        }               Callback;
    }                   u;
    /** Unit. */
    STAMUNIT            enmUnit;
    /** Description. */
    const char         *pszDesc;
} STAMDESC;
/** Pointer to sample descriptor. */
typedef STAMDESC        *PSTAMDESC;
/** Pointer to const sample descriptor. */
typedef const STAMDESC  *PCSTAMDESC;


/**
 * Converts a STAM pointer into a VM pointer.
 * @returns Pointer to the VM structure the STAM is part of.
 * @param   pSTAM   Pointer to STAM instance data.
 */
#define STAM2VM(pSTAM)  ( (PVM)((char*)pSTAM - pSTAM->offVM) )


/**
 * The stam data in the VM.
 */
struct STAM
{
    /** Offset to the VM structure.
     * See STAM2VM(). */
    RTINT                   offVM;
    /** Alignment padding. */
    RTUINT                  uPadding;
    /** Pointer to the first sample. */
    HCPTRTYPE(PSTAMDESC)    pHead;
    /** RW Lock for the list. */
    RTSEMRW                 RWSem;
};

/** Locks the sample descriptors for reading. */
#define STAM_LOCK_RD(pVM)       do { int rcSem = RTSemRWRequestRead(pVM->stam.s.RWSem, RT_INDEFINITE_WAIT);  AssertRC(rcSem); } while (0)
/** Locks the sample descriptors for writing. */
#define STAM_LOCK_WR(pVM)       do { int rcSem = RTSemRWRequestWrite(pVM->stam.s.RWSem, RT_INDEFINITE_WAIT); AssertRC(rcSem); } while (0)
/** UnLocks the sample descriptors after reading. */
#define STAM_UNLOCK_RD(pVM)     do { int rcSem = RTSemRWReleaseRead(pVM->stam.s.RWSem);  AssertRC(rcSem); } while (0)
/** UnLocks the sample descriptors after writing. */
#define STAM_UNLOCK_WR(pVM)     do { int rcSem = RTSemRWReleaseWrite(pVM->stam.s.RWSem); AssertRC(rcSem); } while (0)

/** @} */

__END_DECLS

#endif
