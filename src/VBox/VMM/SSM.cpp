/* $Id$ */
/** @file
 * SSM - Saved State Manager.
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
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


/** @page pg_ssm        SSM - The Saved State Manager
 *
 * The Saved State Manager (SSM) implements facilities for saving and loading a
 * VM state in a structural manner using callbacks for each collection of data
 * which needs saving.
 *
 * At init time each of the VMM components, Devices, Drivers and one or two
 * other things will register data entities which they need to save and restore.
 * Each entity have a unique name (ascii), instance number, and a set of
 * callbacks associated with it.  The name will be used to identify the entity
 * during restore.  The callbacks are for the two operations, save and restore.
 * There are three callbacks for each of the two - a prepare, a execute and a
 * complete - giving each component ample opportunity to perform actions both
 * before and afterwards.
 *
 * The SSM provides a number of APIs for encoding and decoding the data.
 *
 * @see grp_ssm
 *
 *
 * @section sec_ssm_future  Future Changes
 *
 * There are plans to extend SSM to make it easier to be both backwards and
 * (somewhat) forwards compatible.  One of the new features will be being able
 * to classify units and data items as unimportant, one example where this would
 * be nice can be seen in with the SSM data unit.  Another potentail feature is
 * naming data items, perhaps by extending the SSMR3PutStruct API.
 *
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_SSM
#include <VBox/ssm.h>
#include <VBox/dbgf.h>
#include <VBox/mm.h>
#include "SSMInternal.h"
#include <VBox/vm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/version.h>

#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/alloc.h>
#include <iprt/uuid.h>
#include <iprt/zip.h>
#include <iprt/crc32.h>
#include <iprt/thread.h>
#include <iprt/string.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** Saved state file magic base string. */
#define SSMFILEHDR_MAGIC_BASE   "\177VirtualBox SavedState "
/** Saved state file v1.0 magic. */
#define SSMFILEHDR_MAGIC_V1_0   "\177VirtualBox SavedState V1.0\n"
/** Saved state file v1.1 magic. */
#define SSMFILEHDR_MAGIC_V1_1   "\177VirtualBox SavedState V1.1\n"
/** Saved state file v1.2 magic. */
#define SSMFILEHDR_MAGIC_V1_2   "\177VirtualBox SavedState V1.2\n\0\0\0"

/** Data unit magic. */
#define SSMFILEUNITHDR_MAGIC    "\nUnit\n"
/** Data end marker magic. */
#define SSMFILEUNITHDR_END      "\nTheEnd"

/** Start structure magic. (Isacc Asimov) */
#define SSMR3STRUCT_BEGIN       0x19200102
/** End structure magic. (Isacc Asimov) */
#define SSMR3STRUCT_END         0x19920406


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/** SSM state. */
typedef enum SSMSTATE
{
    SSMSTATE_INVALID = 0,
    SSMSTATE_SAVE_PREP,
    SSMSTATE_SAVE_EXEC,
    SSMSTATE_SAVE_DONE,
    SSMSTATE_LOAD_PREP,
    SSMSTATE_LOAD_EXEC,
    SSMSTATE_LOAD_DONE,
    SSMSTATE_OPEN_READ
} SSMSTATE;


/**
 * Handle structure.
 */
typedef struct SSMHANDLE
{
    /** The file handle. */
    RTFILE          File;
    /** The VM handle. */
    PVM             pVM;
    /** The size of the file header.
     * Because the file header was incorrectly aligned there we've ended up with
     * differences between the 64-bit and 32-bit file header. */
    size_t          cbFileHdr;
    /** The current operation. */
    SSMSTATE        enmOp;
    /** What to do after save completes. (move the enum) */
    SSMAFTER        enmAfter;
    /** The current rc of the save operation. */
    int             rc;
    /** The compressor of the current data unit. */
    PRTZIPCOMP      pZipComp;
    /** The decompressor of the current data unit. */
    PRTZIPDECOMP    pZipDecomp;
    /** Number of bytes left in the current data unit. */
    uint64_t        cbUnitLeft;

    /** Pointer to the progress callback function. */
    PFNVMPROGRESS   pfnProgress;
    /** User specified arguemnt to the callback function. */
    void           *pvUser;
    /** Next completion percentage. (corresponds to offEstProgress) */
    unsigned        uPercent;
    /** The position of the next progress callback in the estimated file. */
    uint64_t        offEstProgress;
    /** The estimated total byte count.
     * (Only valid after the prep.) */
    uint64_t        cbEstTotal;
    /** Current position in the estimated file. */
    uint64_t        offEst;
    /** End of current unit in the estimated file. */
    uint64_t        offEstUnitEnd;
    /** the amount of % we reserve for the 'prepare' phase */
    unsigned        uPercentPrepare;
    /** the amount of % we reserve for the 'done' stage */
    unsigned        uPercentDone;

    /** RTGCPHYS size in bytes. (Only applicable when loading/reading.) */
    unsigned        cbGCPhys;
    /** RTGCPTR size in bytes. (Only applicable when loading/reading.) */
    unsigned        cbGCPtr;
    /** Whether cbGCPtr is fixed or settable. */
    bool            fFixedGCPtrSize;
} SSMHANDLE;


/**
 * Header of the saved state file.
 *
 * @remarks This is a superset of SSMFILEHDRV11.
 */
typedef struct SSMFILEHDR
{
    /** Magic string which identifies this file as a version of VBox saved state
     *  file format (SSMFILEHDR_MAGIC_V1_2). */
    char            achMagic[32];
    /** The size of this file. Used to check
     * whether the save completed and that things are fine otherwise. */
    uint64_t        cbFile;
    /** File checksum. The actual calculation skips past the u32CRC field. */
    uint32_t        u32CRC;
    /** Padding. */
    uint32_t        u32Reserved;
    /** The machine UUID. (Ignored if NIL.) */
    RTUUID          MachineUuid;

    /** The major version number. */
    uint16_t        u16VerMajor;
    /** The minor version number. */
    uint16_t        u16VerMinor;
    /** The build number. */
    uint32_t        u32VerBuild;
    /** The SVN revision. */
    uint32_t        u32SvnRev;

    /** 32 or 64 depending on the host. */
    uint8_t         cHostBits;
    /** The size of RTGCPHYS. */
    uint8_t         cbGCPhys;
    /** The size of RTGCPTR. */
    uint8_t         cbGCPtr;
    /** Padding. */
    uint8_t         au8Reserved;
} SSMFILEHDR;
AssertCompileSize(SSMFILEHDR, 64+16);
AssertCompileMemberSize(SSMFILEHDR, achMagic, sizeof(SSMFILEHDR_MAGIC_V1_2));
/** Pointer to a saved state file header. */
typedef SSMFILEHDR *PSSMFILEHDR;


/**
 * Header of the saved state file, version 1.1.
 */
typedef struct SSMFILEHDRV11
{
    /** Magic string which identifies this file as a version of VBox saved state
     *  file format (SSMFILEHDR_MAGIC_V1_1). */
    char            achMagic[32];
    /** The size of this file. Used to check
     * whether the save completed and that things are fine otherwise. */
    uint64_t        cbFile;
    /** File checksum. The actual calculation skips past the u32CRC field. */
    uint32_t        u32CRC;
    /** Padding. */
    uint32_t        u32Reserved;
    /** The machine UUID. (Ignored if NIL.) */
    RTUUID          MachineUuid;
} SSMFILEHDRV11;
AssertCompileSize(SSMFILEHDRV11, 64);
/** Pointer to a saved state file header. */
typedef SSMFILEHDRV11 *PSSMFILEHDRV11;


/**
 * The x86 edition of the 1.0 header.
 */
#pragma pack(1) /* darn, MachineUuid got missaligned! */
typedef struct SSMFILEHDRV10X86
{
    /** Magic string which identifies this file as a version of VBox saved state
     * file format (SSMFILEHDR_MAGIC_V1_0). */
    char            achMagic[32];
    /** The size of this file. Used to check
     * whether the save completed and that things are fine otherwise. */
    uint64_t        cbFile;
    /** File checksum. The actual calculation skips past the u32CRC field. */
    uint32_t        u32CRC;
    /** The machine UUID. (Ignored if NIL.) */
    RTUUID          MachineUuid;
} SSMFILEHDRV10X86;
#pragma pack()
/** Pointer to a SSMFILEHDRV10X86. */
typedef SSMFILEHDRV10X86 *PSSMFILEHDRV10X86;

/**
 * The amd64 edition of the 1.0 header.
 */
typedef SSMFILEHDR SSMFILEHDRV10AMD64;
/** Pointer to SSMFILEHDRV10AMD64. */
typedef SSMFILEHDRV10AMD64 *PSSMFILEHDRV10AMD64;


/**
 * Data unit header.
 */
typedef struct SSMFILEUNITHDR
{
    /** Magic (SSMFILEUNITHDR_MAGIC or SSMFILEUNITHDR_END). */
    char            achMagic[8];
    /** Number of bytes in this data unit including the header. */
    uint64_t        cbUnit;
    /** Data version. */
    uint32_t        u32Version;
    /** Instance number. */
    uint32_t        u32Instance;
    /** Size of the data unit name including the terminator. (bytes) */
    uint32_t        cchName;
    /** Data unit name. */
    char            szName[1];
} SSMFILEUNITHDR;
/** Pointer to SSMFILEUNITHDR.  */
typedef SSMFILEUNITHDR *PSSMFILEUNITHDR;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static int                  ssmR3LazyInit(PVM pVM);
static DECLCALLBACK(int)    ssmR3SelfSaveExec(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(int)    ssmR3SelfLoadExec(PVM pVM, PSSMHANDLE pSSM, uint32_t u32Version);
static int                  ssmR3Register(PVM pVM, const char *pszName, uint32_t u32Instance, uint32_t u32Version, size_t cbGuess, PSSMUNIT *ppUnit);
static int                  ssmR3CalcChecksum(RTFILE File, uint64_t cbFile, uint32_t *pu32CRC);
static void                 ssmR3Progress(PSSMHANDLE pSSM, uint64_t cbAdvance);
static int                  ssmR3Validate(RTFILE File, PSSMFILEHDR pHdr, size_t *pcbFileHdr);
static PSSMUNIT             ssmR3Find(PVM pVM, const char *pszName, uint32_t u32Instance);
static int                  ssmR3WriteFinish(PSSMHANDLE pSSM);
static int                  ssmR3Write(PSSMHANDLE pSSM, const void *pvBuf, size_t cbBuf);
static DECLCALLBACK(int)    ssmR3WriteOut(void *pvSSM, const void *pvBuf, size_t cbBuf);
static void                 ssmR3ReadFinish(PSSMHANDLE pSSM);
static int                  ssmR3Read(PSSMHANDLE pSSM, void *pvBuf, size_t cbBuf);
static DECLCALLBACK(int)    ssmR3ReadIn(void *pvSSM, void *pvBuf, size_t cbBuf, size_t *pcbRead);


/**
 * Performs lazy initialization of the SSM.
 *
 * @returns VBox status code.
 * @param   pVM         The VM.
 */
static int ssmR3LazyInit(PVM pVM)
{
    /*
     * Register a saved state unit which we use to put the VirtualBox version,
     * revision and similar stuff in.
     */
    pVM->ssm.s.fInitialized = true;
    int rc = SSMR3RegisterInternal(pVM, "SSM", 0 /*u32Instance*/, 1/*u32Version*/, 64 /*cbGuess*/,
                                   NULL /*pfnSavePrep*/, ssmR3SelfSaveExec, NULL /*pfnSaveDone*/,
                                   NULL /*pfnSavePrep*/, ssmR3SelfLoadExec, NULL /*pfnSaveDone*/);
    pVM->ssm.s.fInitialized = RT_SUCCESS(rc);
    return rc;
}


/**
 * For saving usful things without having to go thru the tedious process of
 * adding it to the header.
 *
 * @returns VBox status code.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   pSSM            The SSM handle.
 */
static DECLCALLBACK(int) ssmR3SelfSaveExec(PVM pVM, PSSMHANDLE pSSM)
{
    char szTmp[128];

    /*
     * String table containg pairs of variable and value string.
     * Terminated by two empty strings.
     */
#ifdef VBOX_OSE
    SSMR3PutStrZ(pSSM, "OSE");
    SSMR3PutStrZ(pSSM, "true");
#endif

    /* terminator */
    SSMR3PutStrZ(pSSM, "");
    return SSMR3PutStrZ(pSSM, "");
}


/**
 * For load the version + revision and stuff.
 *
 * @returns VBox status code.
 * @param   pVM             Pointer to the shared VM structure.
 * @param   pSSM            The SSM handle.
 * @param   u32Version      The version (1).
 */
static DECLCALLBACK(int) ssmR3SelfLoadExec(PVM pVM, PSSMHANDLE pSSM, uint32_t u32Version)
{
    AssertLogRelMsgReturn(u32Version == 1, ("%d", u32Version), VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);

    /*
     * String table containg pairs of variable and value string.
     * Terminated by two empty strings.
     */
    for (unsigned i = 0; ; i++)
    {
        char szVar[128];
        char szValue[1024];
        int rc = SSMR3GetStrZ(pSSM, szVar, sizeof(szVar));
        AssertRCReturn(rc, rc);
        rc = SSMR3GetStrZ(pSSM, szValue, sizeof(szValue));
        AssertRCReturn(rc, rc);
        if (!szVar[0] && !szValue[0])
            break;
        if (i == 0)
            LogRel(("SSM: Saved state info:\n"));
        LogRel(("SSM:   %s: %s\n", szVar, szValue));
    }
    return VINF_SUCCESS;
}


/**
 * Internal registration worker.
 *
 * @returns VBox status code.
 * @param   pVM             The VM handle.
 * @param   pszName         Data unit name.
 * @param   u32Instance     The instance id.
 * @param   u32Version      The data unit version.
 * @param   cbGuess         The guessed data unit size.
 * @param   ppUnit          Where to store the insterted unit node.
 *                          Caller must fill in the missing details.
 */
static int ssmR3Register(PVM pVM, const char *pszName, uint32_t u32Instance, uint32_t u32Version, size_t cbGuess, PSSMUNIT *ppUnit)
{
    /*
     * Lazy init.
     */
    if (!pVM->ssm.s.fInitialized)
    {
        int rc = ssmR3LazyInit(pVM);
        AssertRCReturn(rc, rc);
    }

    /*
     * Walk to the end of the list checking for duplicates as we go.
     */
    size_t      cchName = strlen(pszName);
    PSSMUNIT    pUnitPrev = NULL;
    PSSMUNIT    pUnit = pVM->ssm.s.pHead;
    while (pUnit)
    {
        if (    pUnit->u32Instance == u32Instance
            &&  pUnit->cchName == cchName
            &&  !memcmp(pUnit->szName, pszName, cchName))
        {
            AssertMsgFailed(("Duplicate registration %s\n", pszName));
            return VERR_SSM_UNIT_EXISTS;
        }
        /* next */
        pUnitPrev = pUnit;
        pUnit = pUnit->pNext;
    }

    /*
     * Allocate new node.
     */
    pUnit = (PSSMUNIT)MMR3HeapAllocZ(pVM, MM_TAG_SSM, RT_OFFSETOF(SSMUNIT, szName[cchName + 1]));
    if (!pUnit)
        return VERR_NO_MEMORY;

    /*
     * Fill in (some) data. (Stuff is zero'ed.)
     */
    pUnit->u32Version = u32Version;
    pUnit->u32Instance = u32Instance;
    pUnit->cbGuess = cbGuess;
    pUnit->cchName = cchName;
    memcpy(pUnit->szName, pszName, cchName);

    /*
     * Insert
     */
    if (pUnitPrev)
        pUnitPrev->pNext = pUnit;
    else
        pVM->ssm.s.pHead = pUnit;

    *ppUnit = pUnit;
    return VINF_SUCCESS;
}


/**
 * Register a PDM Devices data unit.
 *
 * @returns VBox status.
 *
 * @param   pVM             The VM handle.
 * @param   pDevIns         Device instance.
 * @param   pszName         Data unit name.
 * @param   u32Instance     The instance identifier of the data unit.
 *                          This must together with the name be unique.
 * @param   u32Version      Data layout version number.
 * @param   cbGuess         The approximate amount of data in the unit.
 *                          Only for progress indicators.
 * @param   pfnSavePrep     Prepare save callback, optional.
 * @param   pfnSaveExec     Execute save callback, optional.
 * @param   pfnSaveDone     Done save callback, optional.
 * @param   pfnLoadPrep     Prepare load callback, optional.
 * @param   pfnLoadExec     Execute load callback, optional.
 * @param   pfnLoadDone     Done load callback, optional.
 */
VMMR3DECL(int) SSMR3RegisterDevice(PVM pVM, PPDMDEVINS pDevIns, const char *pszName, uint32_t u32Instance, uint32_t u32Version, size_t cbGuess,
    PFNSSMDEVSAVEPREP pfnSavePrep, PFNSSMDEVSAVEEXEC pfnSaveExec, PFNSSMDEVSAVEDONE pfnSaveDone,
    PFNSSMDEVLOADPREP pfnLoadPrep, PFNSSMDEVLOADEXEC pfnLoadExec, PFNSSMDEVLOADDONE pfnLoadDone)
{
    PSSMUNIT pUnit;
    int rc = ssmR3Register(pVM, pszName, u32Instance, u32Version, cbGuess, &pUnit);
    if (RT_SUCCESS(rc))
    {
        pUnit->enmType = SSMUNITTYPE_DEV;
        pUnit->u.Dev.pfnSavePrep = pfnSavePrep;
        pUnit->u.Dev.pfnSaveExec = pfnSaveExec;
        pUnit->u.Dev.pfnSaveDone = pfnSaveDone;
        pUnit->u.Dev.pfnLoadPrep = pfnLoadPrep;
        pUnit->u.Dev.pfnLoadExec = pfnLoadExec;
        pUnit->u.Dev.pfnLoadDone = pfnLoadDone;
        pUnit->u.Dev.pDevIns = pDevIns;
    }
    return rc;
}


/**
 * Register a PDM driver data unit.
 *
 * @returns VBox status.
 *
 * @param   pVM             The VM handle.
 * @param   pDrvIns         Driver instance.
 * @param   pszName         Data unit name.
 * @param   u32Instance     The instance identifier of the data unit.
 *                          This must together with the name be unique.
 * @param   u32Version      Data layout version number.
 * @param   cbGuess         The approximate amount of data in the unit.
 *                          Only for progress indicators.
 * @param   pfnSavePrep     Prepare save callback, optional.
 * @param   pfnSaveExec     Execute save callback, optional.
 * @param   pfnSaveDone     Done save callback, optional.
 * @param   pfnLoadPrep     Prepare load callback, optional.
 * @param   pfnLoadExec     Execute load callback, optional.
 * @param   pfnLoadDone     Done load callback, optional.
 */
VMMR3DECL(int) SSMR3RegisterDriver(PVM pVM, PPDMDRVINS pDrvIns, const char *pszName, uint32_t u32Instance, uint32_t u32Version, size_t cbGuess,
    PFNSSMDRVSAVEPREP pfnSavePrep, PFNSSMDRVSAVEEXEC pfnSaveExec, PFNSSMDRVSAVEDONE pfnSaveDone,
    PFNSSMDRVLOADPREP pfnLoadPrep, PFNSSMDRVLOADEXEC pfnLoadExec, PFNSSMDRVLOADDONE pfnLoadDone)
{
    PSSMUNIT pUnit;
    int rc = ssmR3Register(pVM, pszName, u32Instance, u32Version, cbGuess, &pUnit);
    if (RT_SUCCESS(rc))
    {
        pUnit->enmType = SSMUNITTYPE_DRV;
        pUnit->u.Drv.pfnSavePrep = pfnSavePrep;
        pUnit->u.Drv.pfnSaveExec = pfnSaveExec;
        pUnit->u.Drv.pfnSaveDone = pfnSaveDone;
        pUnit->u.Drv.pfnLoadPrep = pfnLoadPrep;
        pUnit->u.Drv.pfnLoadExec = pfnLoadExec;
        pUnit->u.Drv.pfnLoadDone = pfnLoadDone;
        pUnit->u.Drv.pDrvIns = pDrvIns;
    }
    return rc;
}


/**
 * Register a internal data unit.
 *
 * @returns VBox status.
 *
 * @param   pVM             The VM handle.
 * @param   pszName         Data unit name.
 * @param   u32Instance     The instance identifier of the data unit.
 *                          This must together with the name be unique.
 * @param   u32Version      Data layout version number.
 * @param   cbGuess         The approximate amount of data in the unit.
 *                          Only for progress indicators.
 * @param   pfnSavePrep     Prepare save callback, optional.
 * @param   pfnSaveExec     Execute save callback, optional.
 * @param   pfnSaveDone     Done save callback, optional.
 * @param   pfnLoadPrep     Prepare load callback, optional.
 * @param   pfnLoadExec     Execute load callback, optional.
 * @param   pfnLoadDone     Done load callback, optional.
 */
VMMR3DECL(int) SSMR3RegisterInternal(PVM pVM, const char *pszName, uint32_t u32Instance, uint32_t u32Version, size_t cbGuess,
    PFNSSMINTSAVEPREP pfnSavePrep, PFNSSMINTSAVEEXEC pfnSaveExec, PFNSSMINTSAVEDONE pfnSaveDone,
    PFNSSMINTLOADPREP pfnLoadPrep, PFNSSMINTLOADEXEC pfnLoadExec, PFNSSMINTLOADDONE pfnLoadDone)
{
    PSSMUNIT pUnit;
    int rc = ssmR3Register(pVM, pszName, u32Instance, u32Version, cbGuess, &pUnit);
    if (RT_SUCCESS(rc))
    {
        pUnit->enmType = SSMUNITTYPE_INTERNAL;
        pUnit->u.Internal.pfnSavePrep = pfnSavePrep;
        pUnit->u.Internal.pfnSaveExec = pfnSaveExec;
        pUnit->u.Internal.pfnSaveDone = pfnSaveDone;
        pUnit->u.Internal.pfnLoadPrep = pfnLoadPrep;
        pUnit->u.Internal.pfnLoadExec = pfnLoadExec;
        pUnit->u.Internal.pfnLoadDone = pfnLoadDone;
    }
    return rc;
}


/**
 * Register an external data unit.
 *
 * @returns VBox status.
 *
 * @param   pVM             The VM handle.
 * @param   pszName         Data unit name.
 * @param   u32Instance     The instance identifier of the data unit.
 *                          This must together with the name be unique.
 * @param   u32Version      Data layout version number.
 * @param   cbGuess         The approximate amount of data in the unit.
 *                          Only for progress indicators.
 * @param   pfnSavePrep     Prepare save callback, optional.
 * @param   pfnSaveExec     Execute save callback, optional.
 * @param   pfnSaveDone     Done save callback, optional.
 * @param   pfnLoadPrep     Prepare load callback, optional.
 * @param   pfnLoadExec     Execute load callback, optional.
 * @param   pfnLoadDone     Done load callback, optional.
 * @param   pvUser          User argument.
 */
VMMR3DECL(int) SSMR3RegisterExternal(PVM pVM, const char *pszName, uint32_t u32Instance, uint32_t u32Version, size_t cbGuess,
    PFNSSMEXTSAVEPREP pfnSavePrep, PFNSSMEXTSAVEEXEC pfnSaveExec, PFNSSMEXTSAVEDONE pfnSaveDone,
    PFNSSMEXTLOADPREP pfnLoadPrep, PFNSSMEXTLOADEXEC pfnLoadExec, PFNSSMEXTLOADDONE pfnLoadDone, void *pvUser)
{
    PSSMUNIT pUnit;
    int rc = ssmR3Register(pVM, pszName, u32Instance, u32Version, cbGuess, &pUnit);
    if (RT_SUCCESS(rc))
    {
        pUnit->enmType = SSMUNITTYPE_EXTERNAL;
        pUnit->u.External.pfnSavePrep = pfnSavePrep;
        pUnit->u.External.pfnSaveExec = pfnSaveExec;
        pUnit->u.External.pfnSaveDone = pfnSaveDone;
        pUnit->u.External.pfnLoadPrep = pfnLoadPrep;
        pUnit->u.External.pfnLoadExec = pfnLoadExec;
        pUnit->u.External.pfnLoadDone = pfnLoadDone;
        pUnit->u.External.pvUser      = pvUser;
    }
    return rc;
}


/**
 * Deregister one or more PDM Device data units.
 *
 * @returns VBox status.
 *
 * @param   pVM             The VM handle.
 * @param   pDevIns         Device instance.
 * @param   pszName         Data unit name.
 *                          Use NULL to deregister all data units for that device instance.
 * @param   u32Instance     The instance identifier of the data unit.
 *                          This must together with the name be unique.
 * @remark  Only for dynmaic data units and dynamic unloaded modules.
 */
VMMR3DECL(int) SSMR3DeregisterDevice(PVM pVM, PPDMDEVINS pDevIns, const char *pszName, uint32_t u32Instance)
{
    /*
     * Validate input.
     */
    if (!pDevIns)
    {
        AssertMsgFailed(("pDevIns is NULL!\n"));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Search the list.
     */
    size_t      cchName = pszName ? strlen(pszName) : 0;
    int         rc = pszName ? VERR_SSM_UNIT_NOT_FOUND : VINF_SUCCESS;
    PSSMUNIT    pUnitPrev = NULL;
    PSSMUNIT    pUnit = pVM->ssm.s.pHead;
    while (pUnit)
    {
        if (    pUnit->enmType == SSMUNITTYPE_DEV
            &&  (   !pszName
                 || (   pUnit->cchName == cchName
                     && !memcmp(pUnit->szName, pszName, cchName)))
            &&  pUnit->u32Instance == u32Instance
            )
        {
            if (pUnit->u.Dev.pDevIns == pDevIns)
            {
                /*
                 * Unlink it, advance pointer, and free the node.
                 */
                PSSMUNIT pFree = pUnit;
                pUnit = pUnit->pNext;
                if (pUnitPrev)
                    pUnitPrev->pNext = pUnit;
                else
                    pVM->ssm.s.pHead = pUnit;
                Log(("SSM: Removed data unit '%s' (pdm dev).\n", pFree->szName));
                MMR3HeapFree(pFree);
                if (pszName)
                    return VINF_SUCCESS;
                rc = VINF_SUCCESS;
                continue;
            }
            else if (pszName)
            {
                AssertMsgFailed(("Caller is not owner! Owner=%p Caller=%p %s\n",
                                 pUnit->u.Dev.pDevIns, pDevIns, pszName));
                return VERR_SSM_UNIT_NOT_OWNER;
            }
        }

        /* next */
        pUnitPrev = pUnit;
        pUnit = pUnit->pNext;
    }

    return rc;
}


/**
 * Deregister one ore more PDM Driver data units.
 *
 * @returns VBox status.
 * @param   pVM             The VM handle.
 * @param   pDrvIns         Driver instance.
 * @param   pszName         Data unit name.
 *                          Use NULL to deregister all data units for that driver instance.
 * @param   u32Instance     The instance identifier of the data unit.
 *                          This must together with the name be unique. Ignored if pszName is NULL.
 * @remark  Only for dynmaic data units and dynamic unloaded modules.
 */
VMMR3DECL(int) SSMR3DeregisterDriver(PVM pVM, PPDMDRVINS pDrvIns, const char *pszName, uint32_t u32Instance)
{
    /*
     * Validate input.
     */
    if (!pDrvIns)
    {
        AssertMsgFailed(("pDrvIns is NULL!\n"));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Search the list.
     */
    size_t      cchName = pszName ? strlen(pszName) : 0;
    int         rc = pszName ? VERR_SSM_UNIT_NOT_FOUND : VINF_SUCCESS;
    PSSMUNIT    pUnitPrev = NULL;
    PSSMUNIT    pUnit = pVM->ssm.s.pHead;
    while (pUnit)
    {
        if (    pUnit->enmType == SSMUNITTYPE_DRV
            &&  (   !pszName
                 || (   pUnit->cchName == cchName
                     && !memcmp(pUnit->szName, pszName, cchName)
                     && pUnit->u32Instance == u32Instance))
            )
        {
            if (pUnit->u.Drv.pDrvIns == pDrvIns)
            {
                /*
                 * Unlink it, advance pointer, and free the node.
                 */
                PSSMUNIT pFree = pUnit;
                pUnit = pUnit->pNext;
                if (pUnitPrev)
                    pUnitPrev->pNext = pUnit;
                else
                    pVM->ssm.s.pHead = pUnit;
                Log(("SSM: Removed data unit '%s' (pdm drv).\n", pFree->szName));
                MMR3HeapFree(pFree);
                if (pszName)
                    return VINF_SUCCESS;
                rc = VINF_SUCCESS;
                continue;
            }
            else if (pszName)
            {
                AssertMsgFailed(("Caller is not owner! Owner=%p Caller=%p %s\n",
                                 pUnit->u.Drv.pDrvIns, pDrvIns, pszName));
                return VERR_SSM_UNIT_NOT_OWNER;
            }
        }

        /* next */
        pUnitPrev = pUnit;
        pUnit = pUnit->pNext;
    }

    return rc;
}


/**
 * Deregister a data unit.
 *
 * @returns VBox status.
 * @param   pVM             The VM handle.
 * @param   enmType         Unit type
 * @param   pszName         Data unit name.
 * @remark  Only for dynmaic data units.
 */
static int ssmR3DeregisterByNameAndType(PVM pVM, const char *pszName, SSMUNITTYPE enmType)
{
    /*
     * Validate input.
     */
    if (!pszName)
    {
        AssertMsgFailed(("pszName is NULL!\n"));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Search the list.
     */
    size_t      cchName = strlen(pszName);
    int         rc = VERR_SSM_UNIT_NOT_FOUND;
    PSSMUNIT    pUnitPrev = NULL;
    PSSMUNIT    pUnit = pVM->ssm.s.pHead;
    while (pUnit)
    {
        if (    pUnit->enmType == enmType
            &&  pUnit->cchName == cchName
            &&  !memcmp(pUnit->szName, pszName, cchName))
        {
            /*
             * Unlink it, advance pointer, and free the node.
             */
            PSSMUNIT pFree = pUnit;
            pUnit = pUnit->pNext;
            if (pUnitPrev)
                pUnitPrev->pNext = pUnit;
            else
                pVM->ssm.s.pHead = pUnit;
            Log(("SSM: Removed data unit '%s' (type=%d).\n", pFree->szName, enmType));
            MMR3HeapFree(pFree);
            return VINF_SUCCESS;
        }

        /* next */
        pUnitPrev = pUnit;
        pUnit = pUnit->pNext;
    }

    return rc;
}


/**
 * Deregister an internal data unit.
 *
 * @returns VBox status.
 * @param   pVM             The VM handle.
 * @param   pszName         Data unit name.
 * @remark  Only for dynmaic data units.
 */
VMMR3DECL(int) SSMR3DeregisterInternal(PVM pVM, const char *pszName)
{
    return ssmR3DeregisterByNameAndType(pVM, pszName, SSMUNITTYPE_INTERNAL);
}


/**
 * Deregister an external data unit.
 *
 * @returns VBox status.
 * @param   pVM             The VM handle.
 * @param   pszName         Data unit name.
 * @remark  Only for dynmaic data units.
 */
VMMR3DECL(int) SSMR3DeregisterExternal(PVM pVM, const char *pszName)
{
    return ssmR3DeregisterByNameAndType(pVM, pszName, SSMUNITTYPE_EXTERNAL);
}


/**
 * Calculate the checksum of a file portion.
 *
 * The current implementation is a cut&past of the libkern/crc32.c file from FreeBSD.
 *
 * @returns VBox status.
 * @param   File        Handle to the file.
 * @param   cbFile      Size of the file.
 * @param   pu32CRC     Where to store the calculated checksum.
 */
static int ssmR3CalcChecksum(RTFILE File, uint64_t cbFile, uint32_t *pu32CRC)
{
    /*
     * Allocate a buffer.
     */
    void *pvBuf = RTMemTmpAlloc(32*1024);
    if (!pvBuf)
        return VERR_NO_TMP_MEMORY;

    /*
     * Loop reading and calculating CRC32.
     */
    int         rc = VINF_SUCCESS;
    uint32_t    u32CRC = RTCrc32Start();
    while (cbFile)
    {
        /* read chunk */
        register unsigned cbToRead = 32*1024;
        if (cbFile < 32*1024)
            cbToRead = (unsigned)cbFile;
        rc = RTFileRead(File, pvBuf, cbToRead, NULL);
        if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("Failed with rc=%Rrc while calculating crc.\n", rc));
            RTMemTmpFree(pvBuf);
            return rc;
        }

        /* update total */
        cbFile -= cbToRead;

        /* calc crc32. */
        u32CRC = RTCrc32Process(u32CRC, pvBuf,  cbToRead);
    }
    RTMemTmpFree(pvBuf);

    /* store the calculated crc */
    u32CRC = RTCrc32Finish(u32CRC);
    Log(("SSM: u32CRC=0x%08x\n", u32CRC));
    *pu32CRC = u32CRC;

    return VINF_SUCCESS;
}


/**
 * Works the progress calculation.
 *
 * @param   pSSM        The SSM handle.
 * @param   cbAdvance   Number of bytes to advance
 */
static void ssmR3Progress(PSSMHANDLE pSSM, uint64_t cbAdvance)
{
    /* Can't advance it beyond the estimated end of the unit. */
    uint64_t cbLeft = pSSM->offEstUnitEnd - pSSM->offEst;
    if (cbAdvance > cbLeft)
        cbAdvance = cbLeft;
    pSSM->offEst += cbAdvance;

    /* uPercentPrepare% prepare, xx% exec, uPercentDone% done+crc */
    while (pSSM->offEst >= pSSM->offEstProgress && pSSM->uPercent <= 100-pSSM->uPercentDone)
    {
        if (pSSM->pfnProgress)
            pSSM->pfnProgress(pSSM->pVM, pSSM->uPercent, pSSM->pvUser);
        pSSM->uPercent++;
        pSSM->offEstProgress = (pSSM->uPercent - pSSM->uPercentPrepare) * pSSM->cbEstTotal
                             / (100 - pSSM->uPercentDone - pSSM->uPercentPrepare);
    }
}


/**
 * Start VM save operation.
 *
 * @returns VBox status.
 *
 * @param   pVM             The VM handle.
 * @param   pszFilename     Name of the file to save the state in.
 * @param   enmAfter        What is planned after a successful save operation.
 * @param   pfnProgress     Progress callback. Optional.
 * @param   pvUser          User argument for the progress callback.
 *
 * @thread  EMT
 */
VMMR3DECL(int) SSMR3Save(PVM pVM, const char *pszFilename, SSMAFTER enmAfter, PFNVMPROGRESS pfnProgress, void *pvUser)
{
    LogFlow(("SSMR3Save: pszFilename=%p:{%s} enmAfter=%d pfnProgress=%p pvUser=%p\n", pszFilename, pszFilename, enmAfter, pfnProgress, pvUser));
    VM_ASSERT_EMT(pVM);

    /*
     * Validate input.
     */
    if (    enmAfter != SSMAFTER_DESTROY
        &&  enmAfter != SSMAFTER_CONTINUE)
    {
        AssertMsgFailed(("Invalid enmAfter=%d!\n", enmAfter));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Create the handle and try open the file.
     *
     * Note that there might be quite some work to do after executing the saving,
     * so we reserve 20% for the 'Done' period.  The checksumming and closing of
     * the saved state file might take a long time.
     */
    SSMHANDLE Handle       = {0};
    Handle.File             = NIL_RTFILE;
    Handle.pVM              = pVM;
    Handle.cbFileHdr        = sizeof(SSMFILEHDR);
    Handle.enmOp            = SSMSTATE_INVALID;
    Handle.enmAfter         = enmAfter;
    Handle.rc               = VINF_SUCCESS;
    Handle.pZipComp         = NULL;
    Handle.pZipDecomp       = NULL;
    Handle.pfnProgress      = pfnProgress;
    Handle.pvUser           = pvUser;
    Handle.uPercent         = 0;
    Handle.offEstProgress   = 0;
    Handle.cbEstTotal       = 0;
    Handle.offEst           = 0;
    Handle.offEstUnitEnd    = 0;
    Handle.uPercentPrepare  = 20;
    Handle.uPercentDone     = 2;
    Handle.cbGCPhys         = sizeof(RTGCPHYS);
    Handle.cbGCPtr          = sizeof(RTGCPTR);
    Handle.fFixedGCPtrSize  = true;

    int rc = RTFileOpen(&Handle.File, pszFilename, RTFILE_O_READWRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(rc))
    {
        LogRel(("SSM: Failed to create save state file '%s', rc=%Rrc.\n",  pszFilename, rc));
        return rc;
    }

    Log(("SSM: Starting state save to file '%s'...\n", pszFilename));

    /*
     * Write header.
     */
    SSMFILEHDR Hdr =
    {
        /* .achMagic[32] = */ SSMFILEHDR_MAGIC_V1_2,
        /* .cbFile       = */ 0,
        /* .u32CRC       = */ 0,
        /* .u32Reserved  = */ 0,
        /* .MachineUuid  = */ {{0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0}},
        /* .u16VerMajor  = */ VBOX_VERSION_MAJOR,
        /* .u16VerMinor  = */ VBOX_VERSION_MINOR,
        /* .u32VerBuild  = */ VBOX_VERSION_BUILD,
        /* .u32SvnRev    = */ VMMGetSvnRev(),
        /* .cHostBits    = */ HC_ARCH_BITS,
        /* .cbGCPhys     = */ sizeof(RTGCPHYS),
        /* .cbGCPtr      = */ sizeof(RTGCPTR),
        /* .au8Reserved  = */ 0
    };
    rc = RTFileWrite(Handle.File, &Hdr, sizeof(Hdr), NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * Clear the per unit flags.
         */
        PSSMUNIT pUnit;
        for (pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
            pUnit->fCalled = false;

        /*
         * Do the prepare run.
         */
        Handle.rc = VINF_SUCCESS;
        Handle.enmOp = SSMSTATE_SAVE_PREP;
        for (pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
        {
            switch (pUnit->enmType)
            {
                case SSMUNITTYPE_DEV:
                    if (pUnit->u.Dev.pfnSavePrep)
                    {
                        rc = pUnit->u.Dev.pfnSavePrep(pUnit->u.Dev.pDevIns, &Handle);
                        pUnit->fCalled = true;
                    }
                    break;
                case SSMUNITTYPE_DRV:
                    if (pUnit->u.Drv.pfnSavePrep)
                    {
                        rc = pUnit->u.Drv.pfnSavePrep(pUnit->u.Drv.pDrvIns, &Handle);
                        pUnit->fCalled = true;
                    }
                    break;
                case SSMUNITTYPE_INTERNAL:
                    if (pUnit->u.Internal.pfnSavePrep)
                    {
                        rc = pUnit->u.Internal.pfnSavePrep(pVM, &Handle);
                        pUnit->fCalled = true;
                    }
                    break;
                case SSMUNITTYPE_EXTERNAL:
                    if (pUnit->u.External.pfnSavePrep)
                    {
                        rc = pUnit->u.External.pfnSavePrep(&Handle, pUnit->u.External.pvUser);
                        pUnit->fCalled = true;
                    }
                    break;
            }
            if (RT_FAILURE(rc))
            {
                LogRel(("SSM: Prepare save failed with rc=%Rrc for data unit '%s.\n", rc, pUnit->szName));
                break;
            }

            Handle.cbEstTotal += pUnit->cbGuess;
        }

        /* Progress. */
        if (pfnProgress)
            pfnProgress(pVM, Handle.uPercentPrepare-1, pvUser);
        Handle.uPercent = Handle.uPercentPrepare;

        /*
         * Do the execute run.
         */
        if (RT_SUCCESS(rc))
        {
            Handle.enmOp = SSMSTATE_SAVE_EXEC;
            for (pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
            {
                /*
                 * Estimate.
                 */
                ssmR3Progress(&Handle, Handle.offEstUnitEnd - Handle.offEst);
                Handle.offEstUnitEnd += pUnit->cbGuess;

                /*
                 * Does this unit have a callback? If, not skip it.
                 */
                bool fSkip;
                switch (pUnit->enmType)
                {
                    case SSMUNITTYPE_DEV:       fSkip = pUnit->u.Dev.pfnSaveExec      == NULL; break;
                    case SSMUNITTYPE_DRV:       fSkip = pUnit->u.Drv.pfnSaveExec      == NULL; break;
                    case SSMUNITTYPE_INTERNAL:  fSkip = pUnit->u.Internal.pfnSaveExec == NULL; break;
                    case SSMUNITTYPE_EXTERNAL:  fSkip = pUnit->u.External.pfnSaveExec == NULL; break;
                    default:                    fSkip = true; break;
                }
                if (fSkip)
                {
                    pUnit->fCalled = true;
                    continue;
                }

                /*
                 * Write data unit header
                 */
                uint64_t   offHdr = RTFileTell(Handle.File);
                SSMFILEUNITHDR  UnitHdr = { SSMFILEUNITHDR_MAGIC, 0, pUnit->u32Version, pUnit->u32Instance, (uint32_t)pUnit->cchName + 1, { '\0' } };
                rc = RTFileWrite(Handle.File, &UnitHdr, RT_OFFSETOF(SSMFILEUNITHDR, szName[0]), NULL);
                if (RT_SUCCESS(rc))
                {
                    rc = RTFileWrite(Handle.File, &pUnit->szName[0], pUnit->cchName + 1, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        /*
                         * Call the execute handler.
                         */
                        switch (pUnit->enmType)
                        {
                            case SSMUNITTYPE_DEV:
                                rc = pUnit->u.Dev.pfnSaveExec(pUnit->u.Dev.pDevIns, &Handle);
                                break;
                            case SSMUNITTYPE_DRV:
                                rc = pUnit->u.Drv.pfnSaveExec(pUnit->u.Drv.pDrvIns, &Handle);
                                break;
                            case SSMUNITTYPE_INTERNAL:
                                rc = pUnit->u.Internal.pfnSaveExec(pVM, &Handle);
                                break;
                            case SSMUNITTYPE_EXTERNAL:
                                pUnit->u.External.pfnSaveExec(&Handle, pUnit->u.External.pvUser);
                                rc = Handle.rc;
                                break;
                        }
                        pUnit->fCalled = true;
                        if (RT_FAILURE(Handle.rc) && RT_SUCCESS(rc))
                            rc = Handle.rc;
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Flush buffer / end compression stream.
                             */
                            if (Handle.pZipComp)
                                rc = ssmR3WriteFinish(&Handle);
                            if (RT_SUCCESS(rc))
                            {
                                /*
                                 * Update header with correct length.
                                 */
                                uint64_t    offEnd = RTFileTell(Handle.File);
                                rc = RTFileSeek(Handle.File, offHdr, RTFILE_SEEK_BEGIN, NULL);
                                if (RT_SUCCESS(rc))
                                {
                                    UnitHdr.cbUnit = offEnd - offHdr;
                                    rc = RTFileWrite(Handle.File, &UnitHdr, RT_OFFSETOF(SSMFILEUNITHDR, szName[0]), NULL);
                                    if (RT_SUCCESS(rc))
                                    {
                                        rc = RTFileSeek(Handle.File, offEnd, RTFILE_SEEK_BEGIN, NULL);
                                        if (RT_SUCCESS(rc))
                                            Log(("SSM: Data unit: offset %#9llx size %9lld '%s'\n", offHdr, UnitHdr.cbUnit, pUnit->szName));
                                    }
                                }
                            }
                            else
                            {
                                LogRel(("SSM: Failed ending compression stream. rc=%Rrc\n", rc));
                                break;
                            }
                        }
                        else
                        {
                            LogRel(("SSM: Execute save failed with rc=%Rrc for data unit '%s.\n", rc, pUnit->szName));
                            break;
                        }
                    }
                }
                if (RT_FAILURE(rc))
                {
                    LogRel(("SSM: Failed to write unit header. rc=%Rrc\n", rc));
                    break;
                }
            } /* for each unit */

            /* finish the progress. */
            if (RT_SUCCESS(rc))
                ssmR3Progress(&Handle, Handle.offEstUnitEnd - Handle.offEst);
        }
        /* (progress should be pending 99% now) */
        AssertMsg(RT_FAILURE(rc) || Handle.uPercent == (101-Handle.uPercentDone), ("%d\n", Handle.uPercent));

        /*
         * Do the done run.
         */
        Handle.rc = rc;
        Handle.enmOp = SSMSTATE_SAVE_DONE;
        for (pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
        {
            switch (pUnit->enmType)
            {
                case SSMUNITTYPE_DEV:
                    if (    pUnit->u.Dev.pfnSaveDone
                        &&  (   pUnit->fCalled
                             || (!pUnit->u.Dev.pfnSavePrep && !pUnit->u.Dev.pfnSaveExec)))
                        rc = pUnit->u.Dev.pfnSaveDone(pUnit->u.Dev.pDevIns, &Handle);
                    break;
                case SSMUNITTYPE_DRV:
                    if (    pUnit->u.Drv.pfnSaveDone
                        &&  (   pUnit->fCalled
                             || (!pUnit->u.Drv.pfnSavePrep && !pUnit->u.Drv.pfnSaveExec)))
                        rc = pUnit->u.Drv.pfnSaveDone(pUnit->u.Drv.pDrvIns, &Handle);
                    break;
                case SSMUNITTYPE_INTERNAL:
                    if (    pUnit->u.Internal.pfnSaveDone
                        &&  (   pUnit->fCalled
                             || (!pUnit->u.Internal.pfnSavePrep && !pUnit->u.Internal.pfnSaveExec)))
                        rc = pUnit->u.Internal.pfnSaveDone(pVM, &Handle);
                    break;
                case SSMUNITTYPE_EXTERNAL:
                    if (    pUnit->u.External.pfnSaveDone
                        &&  (   pUnit->fCalled
                             || (!pUnit->u.External.pfnSavePrep && !pUnit->u.External.pfnSaveExec)))
                        rc = pUnit->u.External.pfnSaveDone(&Handle, pUnit->u.External.pvUser);
                    break;
            }
            if (RT_FAILURE(rc))
            {
                LogRel(("SSM: Done save failed with rc=%Rrc for data unit '%s.\n", rc, pUnit->szName));
                if (RT_SUCCESS(Handle.rc))
                    Handle.rc = rc;
            }
        }
        rc = Handle.rc;

        /*
         * Finalize the file if successfully saved.
         */
        if (RT_SUCCESS(rc))
        {
            /* end record */
            SSMFILEUNITHDR  UnitHdr = { SSMFILEUNITHDR_END, RT_OFFSETOF(SSMFILEUNITHDR, szName[0]), 0, '\0'};
            rc = RTFileWrite(Handle.File, &UnitHdr, RT_OFFSETOF(SSMFILEUNITHDR, szName[0]), NULL);
            if (RT_SUCCESS(rc))
            {
                /* get size */
                Hdr.cbFile = RTFileTell(Handle.File);
                /* calc checksum */
                rc = RTFileSeek(Handle.File, RT_OFFSETOF(SSMFILEHDR, u32CRC) + sizeof(Hdr.u32CRC), RTFILE_SEEK_BEGIN, NULL);
                if (RT_SUCCESS(rc))
                    rc = ssmR3CalcChecksum(Handle.File, Hdr.cbFile - sizeof(Hdr), &Hdr.u32CRC);
                if (RT_SUCCESS(rc))
                {
                    if (pfnProgress)
                        pfnProgress(pVM, 90, pvUser);

                    /*
                     * Write the update the header to the file.
                     */
                    rc = RTFileSeek(Handle.File, 0, RTFILE_SEEK_BEGIN, NULL);
                    if (RT_SUCCESS(rc))
                        rc = RTFileWrite(Handle.File, &Hdr, sizeof(Hdr), NULL);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTFileClose(Handle.File);
                        AssertRC(rc);
                        if (pfnProgress)
                            pfnProgress(pVM, 100, pvUser);
                        Log(("SSM: Successfully saved the vm state to '%s'.\n", pszFilename));
                        Log(("\n\n\n"));
                        DBGFR3InfoLog(pVM, "cpum", "verbose");
                        DBGFR3InfoLog(pVM, "timers", NULL);
                        DBGFR3InfoLog(pVM, "activetimers", NULL);
                        DBGFR3InfoLog(pVM, "ioport", NULL);
                        DBGFR3InfoLog(pVM, "mmio", NULL);
                        DBGFR3InfoLog(pVM, "phys", NULL);
                        Log(("\n\n\n"));
                        return VINF_SUCCESS;
                    }

                }
            }
            LogRel(("SSM: Failed to finalize state file! rc=%Rrc\n", pszFilename));
        }
    }

    /*
     * Delete the file on failure and destroy any compressors.
     */
    int rc2 = RTFileClose(Handle.File);
    AssertRC(rc2);
    rc2 = RTFileDelete(pszFilename);
    AssertRC(rc2);
    if (Handle.pZipComp)
        RTZipCompDestroy(Handle.pZipComp);

    return rc;
}


/**
 * Validates the integrity of a saved state file.
 *
 * @returns VBox status.
 * @param   File        File to validate.
 *                      The file position is undefined on return.
 * @param   pHdr        Where to store the file header.
 * @param   pcbFileHdr  Where to store the file header size.
 */
static int ssmR3Validate(RTFILE File, PSSMFILEHDR pHdr, size_t *pcbFileHdr)
{
    /*
     * Read the header.
     */
    int rc = RTFileRead(File, pHdr, sizeof(*pHdr), NULL);
    if (RT_FAILURE(rc))
    {
        Log(("SSM: Failed to read file header. rc=%Rrc\n", rc));
        return rc;
    }

    /*
     * Verify the magic and make adjustments for versions differences.
     */
    if (memcmp(pHdr->achMagic, SSMFILEHDR_MAGIC_BASE, sizeof(SSMFILEHDR_MAGIC_BASE) - 1))
    {
        Log(("SSM: Not a saved state file. magic=%.*s\n", sizeof(pHdr->achMagic) - 1, pHdr->achMagic));
        return VERR_SSM_INTEGRITY_MAGIC;
    }

    size_t offCrc32 = RT_OFFSETOF(SSMFILEHDR, u32CRC) + sizeof(pHdr->u32CRC);
    *pcbFileHdr = sizeof(*pHdr);
    if (!memcmp(pHdr->achMagic, SSMFILEHDR_MAGIC_V1_0, sizeof(SSMFILEHDR_MAGIC_V1_0)))
    {
        if (pHdr->MachineUuid.au32[3])
        {
            SSMFILEHDRV10X86 OldHdr;
            memcpy(&OldHdr, pHdr, sizeof(OldHdr));
            pHdr->cbFile = OldHdr.cbFile;
            pHdr->u32CRC = OldHdr.u32CRC;
            pHdr->u32Reserved = 0;
            pHdr->MachineUuid = OldHdr.MachineUuid;
            pHdr->cHostBits   = 32;

            offCrc32 = RT_OFFSETOF(SSMFILEHDRV10X86, u32CRC) + sizeof(pHdr->u32CRC);
            *pcbFileHdr = sizeof(OldHdr);
        }
        else
        {
            SSMFILEHDRV10AMD64 OldHdr;
            memcpy(&OldHdr, pHdr, sizeof(OldHdr));
            pHdr->cbFile = OldHdr.cbFile;
            pHdr->u32CRC = OldHdr.u32CRC;
            pHdr->u32Reserved = 0;
            pHdr->MachineUuid = OldHdr.MachineUuid;
            pHdr->cHostBits   = 64;

            offCrc32 = RT_OFFSETOF(SSMFILEHDRV10AMD64, u32CRC) + sizeof(pHdr->u32CRC);
            *pcbFileHdr = sizeof(OldHdr);
        }
        pHdr->u16VerMajor = 0;
        pHdr->u16VerMinor = 0;
        pHdr->u32VerBuild = 0;
        pHdr->u32SvnRev   = 0;
        pHdr->cbGCPhys    = sizeof(uint32_t);
        pHdr->cbGCPtr     = sizeof(uint32_t);
        pHdr->au8Reserved = 0;
    }
    else if (!memcmp(pHdr->achMagic, SSMFILEHDR_MAGIC_V1_1, sizeof(SSMFILEHDR_MAGIC_V1_1)))
    {
        *pcbFileHdr = sizeof(SSMFILEHDRV11);
        pHdr->u16VerMajor = 0;
        pHdr->u16VerMinor = 0;
        pHdr->u32VerBuild = 0;
        pHdr->u32SvnRev   = 0;
        pHdr->cHostBits   = 0; /* unknown */
        pHdr->cbGCPhys    = sizeof(RTGCPHYS);
        pHdr->cbGCPtr     = 0; /* settable. */
        pHdr->au8Reserved = 0;
    }
    else if (!memcmp(pHdr->achMagic, SSMFILEHDR_MAGIC_V1_2, sizeof(pHdr->achMagic)))
    {
        if (    pHdr->u16VerMajor == 0
            ||  pHdr->u16VerMajor > 1000
            ||  pHdr->u32SvnRev == 0
            ||  pHdr->u32SvnRev > 10000000 /*100M*/)
        {
            LogRel(("SSM: Incorrect version values: %d.%d.%d.r%d\n",
                    pHdr->u16VerMajor, pHdr->u16VerMinor, pHdr->u32VerBuild, pHdr->u32SvnRev));
            return VERR_SSM_INTEGRITY_VBOX_VERSION;
        }
        if (    pHdr->cHostBits != 32
            &&  pHdr->cHostBits != 64)
        {
            LogRel(("SSM: Incorrect cHostBits value: %d\n", pHdr->cHostBits));
            return VERR_SSM_INTEGRITY_SIZES;
        }
        if (    pHdr->cbGCPhys != sizeof(uint32_t)
            &&  pHdr->cbGCPhys != sizeof(uint64_t))
        {
            LogRel(("SSM: Incorrect cbGCPhys value: %d\n", pHdr->cbGCPhys));
            return VERR_SSM_INTEGRITY_SIZES;
        }
        if (    pHdr->cbGCPtr != sizeof(uint32_t)
            &&  pHdr->cbGCPtr != sizeof(uint64_t))
        {
            LogRel(("SSM: Incorrect cbGCPtr value: %d\n", pHdr->cbGCPtr));
            return VERR_SSM_INTEGRITY_SIZES;
        }
    }
    else
    {
        Log(("SSM: Unknown file format version. magic=%.*s\n", sizeof(pHdr->achMagic) - 1, pHdr->achMagic));
        return VERR_SSM_INTEGRITY_VERSION;
    }

    /*
     * Verify the file size.
     */
    uint64_t cbFile;
    rc = RTFileGetSize(File, &cbFile);
    if (RT_FAILURE(rc))
    {
        Log(("SSM: Failed to get file size. rc=%Rrc\n", rc));
        return rc;
    }
    if (cbFile != pHdr->cbFile)
    {
        Log(("SSM: File size mismatch. hdr.cbFile=%lld actual %lld\n", pHdr->cbFile, cbFile));
        return VERR_SSM_INTEGRITY_SIZE;
    }

    /*
     * Verify the checksum.
     */
    rc = RTFileSeek(File, offCrc32, RTFILE_SEEK_BEGIN, NULL);
    if (RT_FAILURE(rc))
    {
        Log(("SSM: Failed to seek to crc start. rc=%Rrc\n", rc));
        return rc;
    }
    uint32_t u32CRC;
    rc = ssmR3CalcChecksum(File, pHdr->cbFile - *pcbFileHdr, &u32CRC);
    if (RT_FAILURE(rc))
        return rc;
    if (u32CRC != pHdr->u32CRC)
    {
        Log(("SSM: Invalid CRC! Calculated %#08x, in header %#08x\n", u32CRC, pHdr->u32CRC));
        return VERR_SSM_INTEGRITY_CRC;
    }

    /*
     * Verify Virtual Machine UUID.
     */
    RTUUID  Uuid;
    memset(&Uuid, 0, sizeof(Uuid));
/** @todo get machine uuids  CFGGetUuid(, &Uuid); */
    if (    RTUuidCompare(&pHdr->MachineUuid, &Uuid)
        &&  !RTUuidIsNull(&pHdr->MachineUuid)) /* temporary hack, allowing NULL uuids. */
    {
        Log(("SSM: The UUID of the saved state doesn't match the running VM.\n"));
        return VERR_SMM_INTEGRITY_MACHINE;
    }

    return VINF_SUCCESS;
}


/**
 * Find a data unit by name.
 *
 * @returns Pointer to the unit.
 * @returns NULL if not found.
 *
 * @param   pVM             VM handle.
 * @param   pszName         Data unit name.
 * @param   u32Instance     The data unit instance id.
 */
static PSSMUNIT ssmR3Find(PVM pVM, const char *pszName, uint32_t u32Instance)
{
    size_t   cchName = strlen(pszName);
    PSSMUNIT pUnit = pVM->ssm.s.pHead;
    while (     pUnit
           &&   (   pUnit->u32Instance != u32Instance
                 || pUnit->cchName != cchName
                 || memcmp(pUnit->szName, pszName, cchName)))
        pUnit = pUnit->pNext;
    return pUnit;
}


/**
 * Load VM save operation.
 *
 * @returns VBox status.
 *
 * @param   pVM             The VM handle.
 * @param   pszFilename     Name of the file to save the state in.
 * @param   enmAfter        What is planned after a successful load operation.
 *                          Only acceptable values are SSMAFTER_RESUME and SSMAFTER_DEBUG_IT.
 * @param   pfnProgress     Progress callback. Optional.
 * @param   pvUser          User argument for the progress callback.
 *
 * @thread  EMT
 */
VMMR3DECL(int) SSMR3Load(PVM pVM, const char *pszFilename, SSMAFTER enmAfter, PFNVMPROGRESS pfnProgress, void *pvUser)
{
    LogFlow(("SSMR3Load: pszFilename=%p:{%s} enmAfter=%d pfnProgress=%p pvUser=%p\n", pszFilename, pszFilename, enmAfter, pfnProgress, pvUser));
    VM_ASSERT_EMT(pVM);

    /*
     * Validate input.
     */
    if (    enmAfter != SSMAFTER_RESUME
        &&  enmAfter != SSMAFTER_DEBUG_IT)
    {
        AssertMsgFailed(("Invalid enmAfter=%d!\n", enmAfter));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Create the handle and open the file.
     * Note that we reserve 20% of the time on validating the image since this might
     * take a long time.
     */
    SSMHANDLE Handle = {0};
    Handle.File             = NIL_RTFILE;
    Handle.pVM              = pVM;
    Handle.cbFileHdr        = sizeof(SSMFILEHDR);
    Handle.enmOp            = SSMSTATE_INVALID;
    Handle.enmAfter         = enmAfter;
    Handle.rc               = VINF_SUCCESS;
    Handle.pZipComp         = NULL;
    Handle.pZipDecomp       = NULL;
    Handle.pfnProgress      = pfnProgress;
    Handle.pvUser           = pvUser;
    Handle.uPercent         = 0;
    Handle.offEstProgress   = 0;
    Handle.cbEstTotal       = 0;
    Handle.offEst           = 0;
    Handle.offEstUnitEnd    = 0;
    Handle.uPercentPrepare  = 20;
    Handle.uPercentDone     = 2;
    Handle.cbGCPhys         = sizeof(RTGCPHYS);
    Handle.cbGCPtr          = sizeof(RTGCPTR);
    Handle.fFixedGCPtrSize  = false;

    int rc = RTFileOpen(&Handle.File, pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(rc))
    {
        Log(("SSM: Failed to open save state file '%s', rc=%Rrc.\n",  pszFilename, rc));
        return rc;
    }

    /*
     * Read file header and validate it.
     */
    SSMFILEHDR Hdr;
    rc = ssmR3Validate(Handle.File, &Hdr, &Handle.cbFileHdr);
    if (RT_SUCCESS(rc))
    {
        if (Hdr.cbGCPhys)
            Handle.cbGCPhys = Hdr.cbGCPhys;
        if (Hdr.cbGCPtr)
        {
            Handle.cbGCPtr = Hdr.cbGCPtr;
            Handle.fFixedGCPtrSize = true;
        }

        if (Handle.cbFileHdr == sizeof(Hdr))
            LogRel(("SSM: File header: Format %.4s, VirtualBox Version %u.%u.%u r%u, %u-bit host, cbGCPhys=%u, cbGCPtr=%u\n",
                    &Hdr.achMagic[sizeof(SSMFILEHDR_MAGIC_BASE) - 1],
                    Hdr.u16VerMajor, Hdr.u16VerMinor, Hdr.u32VerBuild, Hdr.u32SvnRev,
                    Hdr.cHostBits, Hdr.cbGCPhys, Hdr.cbGCPtr));
        else
            LogRel(("SSM: File header: Format %.4s, %u-bit host, cbGCPhys=%u, cbGCPtr=%u\n" ,
                    &Hdr.achMagic[sizeof(SSMFILEHDR_MAGIC_BASE)-1], Hdr.cHostBits, Hdr.cbGCPhys, Hdr.cbGCPtr));


        /*
         * Clear the per unit flags.
         */
        PSSMUNIT pUnit;
        for (pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
            pUnit->fCalled = false;

        /*
         * Do the prepare run.
         */
        Handle.rc = VINF_SUCCESS;
        Handle.enmOp = SSMSTATE_LOAD_PREP;
        for (pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
        {
            switch (pUnit->enmType)
            {
                case SSMUNITTYPE_DEV:
                    if (pUnit->u.Dev.pfnLoadPrep)
                    {
                        rc = pUnit->u.Dev.pfnLoadPrep(pUnit->u.Dev.pDevIns, &Handle);
                        pUnit->fCalled = true;
                    }
                    break;
                case SSMUNITTYPE_DRV:
                    if (pUnit->u.Drv.pfnLoadPrep)
                    {
                        rc = pUnit->u.Drv.pfnLoadPrep(pUnit->u.Drv.pDrvIns, &Handle);
                        pUnit->fCalled = true;
                    }
                    break;
                case SSMUNITTYPE_INTERNAL:
                    if (pUnit->u.Internal.pfnLoadPrep)
                    {
                        rc = pUnit->u.Internal.pfnLoadPrep(pVM, &Handle);
                        pUnit->fCalled = true;
                    }
                    break;
                case SSMUNITTYPE_EXTERNAL:
                    if (pUnit->u.External.pfnLoadPrep)
                    {
                        rc = pUnit->u.External.pfnLoadPrep(&Handle, pUnit->u.External.pvUser);
                        pUnit->fCalled = true;
                    }
                    break;
            }
            if (RT_FAILURE(rc))
            {
                LogRel(("SSM: Prepare load failed with rc=%Rrc for data unit '%s.\n", rc, pUnit->szName));
                break;
            }
        }

        /* pending 2% */
        if (pfnProgress)
            pfnProgress(pVM, Handle.uPercentPrepare-1, pvUser);
        Handle.uPercent   = Handle.uPercentPrepare;
        Handle.cbEstTotal = Hdr.cbFile;

        /*
         * Do the execute run.
         */
        if (RT_SUCCESS(rc))
            rc = RTFileSeek(Handle.File, Handle.cbFileHdr, RTFILE_SEEK_BEGIN, NULL);
        if (RT_SUCCESS(rc))
        {
            char   *pszName = NULL;
            size_t  cchName = 0;
            Handle.enmOp = SSMSTATE_LOAD_EXEC;
            for (;;)
            {
                /*
                 * Save the current file position and read the data unit header.
                 */
                uint64_t        offUnit = RTFileTell(Handle.File);
                SSMFILEUNITHDR  UnitHdr;
                rc = RTFileRead(Handle.File, &UnitHdr, RT_OFFSETOF(SSMFILEUNITHDR, szName), NULL);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Check the magic and see if it's valid and whether it is a end header or not.
                     */
                    if (memcmp(&UnitHdr.achMagic[0], SSMFILEUNITHDR_MAGIC, sizeof(SSMFILEUNITHDR_MAGIC)))
                    {
                        if (!memcmp(&UnitHdr.achMagic[0], SSMFILEUNITHDR_END, sizeof(SSMFILEUNITHDR_END)))
                        {
                            Log(("SSM: EndOfFile: offset %#9llx size %9d\n", offUnit, UnitHdr.cbUnit));
                            /* Complete the progress bar (pending 99% afterwards). */
                            Handle.offEstUnitEnd = Handle.cbEstTotal;
                            ssmR3Progress(&Handle, Handle.cbEstTotal - Handle.offEst);
                            break;
                        }
                        LogRel(("SSM: Invalid unit magic at offset %#llx (%lld), '%.*s'!\n",
                                offUnit, offUnit, sizeof(UnitHdr.achMagic) - 1, &UnitHdr.achMagic[0]));
                        rc = VERR_SSM_INTEGRITY_UNIT_MAGIC;
                        break;
                    }

                    /*
                     * Read the name.
                     * Adjust the name buffer first.
                     */
                    if (cchName < UnitHdr.cchName)
                    {
                        if (pszName)
                            RTMemTmpFree(pszName);
                        cchName = RT_ALIGN_Z(UnitHdr.cchName, 64);
                        pszName = (char *)RTMemTmpAlloc(cchName);
                    }
                    if (pszName)
                    {
                        rc = RTFileRead(Handle.File, pszName, UnitHdr.cchName, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            if (!pszName[UnitHdr.cchName - 1])
                            {
                                Log(("SSM: Data unit: offset %#9llx size %9lld '%s'\n", offUnit, UnitHdr.cbUnit, pszName));

                                /*
                                 * Progress
                                 */
                                Handle.offEstUnitEnd += UnitHdr.cbUnit;

                                /*
                                 * Find the data unit in our internal table.
                                 */
                                pUnit = ssmR3Find(pVM, pszName, UnitHdr.u32Instance);
                                if (pUnit)
                                {
                                    /*
                                     * Call the execute handler.
                                     */
                                    Handle.cbUnitLeft = UnitHdr.cbUnit - RT_OFFSETOF(SSMFILEUNITHDR, szName[UnitHdr.cchName]);
                                    switch (pUnit->enmType)
                                    {
                                        case SSMUNITTYPE_DEV:
                                            if (pUnit->u.Dev.pfnLoadExec)
                                            {
                                                rc = pUnit->u.Dev.pfnLoadExec(pUnit->u.Dev.pDevIns, &Handle, UnitHdr.u32Version);
                                                AssertRC(rc);
                                            }
                                            else
                                                rc = VERR_SSM_NO_LOAD_EXEC;
                                            break;
                                        case SSMUNITTYPE_DRV:
                                            if (pUnit->u.Drv.pfnLoadExec)
                                            {
                                                rc = pUnit->u.Drv.pfnLoadExec(pUnit->u.Drv.pDrvIns, &Handle, UnitHdr.u32Version);
                                                AssertRC(rc);
                                            }
                                            else
                                                rc = VERR_SSM_NO_LOAD_EXEC;
                                            break;
                                        case SSMUNITTYPE_INTERNAL:
                                            if (pUnit->u.Internal.pfnLoadExec)
                                            {
                                                rc = pUnit->u.Internal.pfnLoadExec(pVM, &Handle, UnitHdr.u32Version);
                                                AssertRC(rc);
                                            }
                                            else
                                                rc = VERR_SSM_NO_LOAD_EXEC;
                                            break;
                                        case SSMUNITTYPE_EXTERNAL:
                                            if (pUnit->u.External.pfnLoadExec)
                                            {
                                                rc = pUnit->u.External.pfnLoadExec(&Handle, pUnit->u.External.pvUser, UnitHdr.u32Version);
                                                if (!rc)
                                                    rc = Handle.rc;
                                            }
                                            else
                                                rc = VERR_SSM_NO_LOAD_EXEC;
                                            break;
                                    }
                                    if (rc != VERR_SSM_NO_LOAD_EXEC)
                                    {
                                        /*
                                         * Close the reader stream.
                                         */
                                        if (Handle.pZipDecomp)
                                            ssmR3ReadFinish(&Handle);

                                        pUnit->fCalled = true;
                                        if (RT_SUCCESS(rc))
                                            rc = Handle.rc;
                                        if (RT_SUCCESS(rc))
                                        {
                                            /*
                                             * Now, we'll check the current position to see if all, or
                                             * more than all, the data was read.
                                             *
                                             * Note! Because of buffering / compression we'll only see the
                                             * really bad ones here.
                                             */
                                            uint64_t off = RTFileTell(Handle.File);
                                            int64_t i64Diff = off - (offUnit + UnitHdr.cbUnit);
                                            if (i64Diff < 0)
                                            {
                                                Log(("SSM: Unit '%s' left %lld bytes unread!\n", pszName, -i64Diff));
                                                rc = RTFileSeek(Handle.File, offUnit + UnitHdr.cbUnit, RTFILE_SEEK_BEGIN, NULL);
                                            }
                                            else if (i64Diff > 0)
                                            {
                                                LogRel(("SSM: Unit '%s' read %lld bytes too much!\n", pszName, i64Diff));
                                                rc = VERR_SSM_INTEGRITY;
                                                break;
                                            }

                                            /* Advance the progress bar to the end of the block. */
                                            ssmR3Progress(&Handle, Handle.offEstUnitEnd - Handle.offEst);
                                        }
                                        else
                                        {
                                            /*
                                             * We failed, but if loading for the debugger ignore certain failures
                                             * just to get it all loaded (big hack).
                                             */
                                            LogRel(("SSM: LoadExec failed with rc=%Rrc for unit '%s'!\n", rc, pszName));
                                            if (    Handle.enmAfter != SSMAFTER_DEBUG_IT
                                                ||  rc != VERR_SSM_LOADED_TOO_MUCH)
                                                break;
                                            Handle.rc = rc = VINF_SUCCESS;
                                            ssmR3Progress(&Handle, Handle.offEstUnitEnd - Handle.offEst);
                                        }
                                    }
                                    else
                                    {
                                        LogRel(("SSM: No load exec callback for unit '%s'!\n", pszName));
                                        rc = VERR_SSM_INTEGRITY;
                                        break;
                                    }
                                }
                                else
                                {
                                    /*
                                     * SSM unit wasn't found - ignore this when loading for the debugger.
                                     */
                                    LogRel(("SSM: Found no handler for unit '%s'!\n", pszName));
                                    rc = VERR_SSM_INTEGRITY_UNIT_NOT_FOUND;
                                    if (Handle.enmAfter != SSMAFTER_DEBUG_IT)
                                        break;
                                    rc = RTFileSeek(Handle.File, offUnit + UnitHdr.cbUnit, RTFILE_SEEK_BEGIN, NULL);
                                }
                            }
                            else
                            {
                                LogRel(("SSM: Unit name '%.*s' was not properly terminated.\n", UnitHdr.cchName, pszName));
                                rc = VERR_SSM_INTEGRITY;
                                break;
                            }
                        }
                    }
                    else
                        rc = VERR_NO_TMP_MEMORY;
                }

                /*
                 * I/O errors ends up here (yea, I know, very nice programming).
                 */
                if (RT_FAILURE(rc))
                {
                    LogRel(("SSM: I/O error. rc=%Rrc\n", rc));
                    break;
                }
            }
        }
        /* (progress should be pending 99% now) */
        AssertMsg(RT_FAILURE(rc) || Handle.uPercent == (101-Handle.uPercentDone), ("%d\n", Handle.uPercent));

        /*
         * Do the done run.
         */
        Handle.rc = rc;
        Handle.enmOp = SSMSTATE_LOAD_DONE;
        for (pUnit = pVM->ssm.s.pHead; pUnit; pUnit = pUnit->pNext)
        {
            rc = VINF_SUCCESS;
            switch (pUnit->enmType)
            {
                case SSMUNITTYPE_DEV:
                    if (    pUnit->u.Dev.pfnLoadDone
                        &&  (   pUnit->fCalled
                             || (!pUnit->u.Dev.pfnLoadPrep && !pUnit->u.Dev.pfnLoadExec)))
                        rc = pUnit->u.Dev.pfnLoadDone(pUnit->u.Dev.pDevIns, &Handle);
                    break;
                case SSMUNITTYPE_DRV:
                    if (    pUnit->u.Drv.pfnLoadDone
                        &&  (   pUnit->fCalled
                             || (!pUnit->u.Drv.pfnLoadPrep && !pUnit->u.Drv.pfnLoadExec)))
                        rc = pUnit->u.Drv.pfnLoadDone(pUnit->u.Drv.pDrvIns, &Handle);
                    break;
                case SSMUNITTYPE_INTERNAL:
                    if (pUnit->u.Internal.pfnLoadDone
                        &&  (   pUnit->fCalled
                             || (!pUnit->u.Internal.pfnLoadPrep && !pUnit->u.Internal.pfnLoadExec)))
                        rc = pUnit->u.Internal.pfnLoadDone(pVM, &Handle);
                    break;
                case SSMUNITTYPE_EXTERNAL:
                    if (pUnit->u.External.pfnLoadDone
                        &&  (   pUnit->fCalled
                             || (!pUnit->u.Internal.pfnLoadPrep && !pUnit->u.Internal.pfnLoadExec)))
                        rc = pUnit->u.External.pfnLoadDone(&Handle, pUnit->u.External.pvUser);
                    break;
            }
            if (RT_FAILURE(rc))
            {
                LogRel(("SSM: Done load failed with rc=%Rrc for data unit '%s'.\n", rc, pUnit->szName));
                if (RT_SUCCESS(Handle.rc))
                    Handle.rc = rc;
            }
        }
        rc = Handle.rc;

        /* progress */
        if (pfnProgress)
            pfnProgress(pVM, 99, pvUser);
    }

    /*
     * Done
     */
    int rc2 = RTFileClose(Handle.File);
    AssertRC(rc2);
    if (RT_SUCCESS(rc))
    {
        /* progress */
        if (pfnProgress)
            pfnProgress(pVM, 100, pvUser);
        Log(("SSM: Load of '%s' completed!\n", pszFilename));
        Log(("\n\n\n"));
        DBGFR3InfoLog(pVM, "cpum", "verbose");
        DBGFR3InfoLog(pVM, "timers", NULL);
        DBGFR3InfoLog(pVM, "activetimers", NULL);
        DBGFR3InfoLog(pVM, "ioport", NULL);
        DBGFR3InfoLog(pVM, "mmio", NULL);
        DBGFR3InfoLog(pVM, "phys", NULL);
        Log(("\n\n\n"));
    }
    return rc;
}


/**
 * Validates a file as a validate SSM saved state.
 *
 * This will only verify the file format, the format and content of individual
 * data units are not inspected.
 *
 * @returns VINF_SUCCESS if valid.
 * @returns VBox status code on other failures.
 *
 * @param   pszFilename     The path to the file to validate.
 *
 * @thread  Any.
 */
VMMR3DECL(int) SSMR3ValidateFile(const char *pszFilename)
{
    LogFlow(("SSMR3ValidateFile: pszFilename=%p:{%s}\n", pszFilename, pszFilename));

    /*
     * Try open the file and validate it.
     */
    RTFILE File;
    int rc = RTFileOpen(&File, pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        size_t cbFileHdr;
        SSMFILEHDR Hdr;
        rc = ssmR3Validate(File, &Hdr, &cbFileHdr);
        RTFileClose(File);
    }
    else
        Log(("SSM: Failed to open saved state file '%s', rc=%Rrc.\n",  pszFilename, rc));
    return rc;
}


/**
 * Opens a saved state file for reading.
 *
 * @returns VBox status code.
 *
 * @param   pszFilename     The path to the saved state file.
 * @param   fFlags          Open flags. Reserved, must be 0.
 * @param   ppSSM           Where to store the SSM handle.
 *
 * @thread  Any.
 */
VMMR3DECL(int) SSMR3Open(const char *pszFilename, unsigned fFlags, PSSMHANDLE *ppSSM)
{
    LogFlow(("SSMR3Open: pszFilename=%p:{%s} fFlags=%#x ppSSM=%p\n", pszFilename, pszFilename, fFlags, ppSSM));

    /*
     * Validate input.
     */
    AssertMsgReturn(VALID_PTR(pszFilename), ("%p\n", pszFilename), VERR_INVALID_PARAMETER);
    AssertMsgReturn(!fFlags, ("%#x\n", fFlags), VERR_INVALID_PARAMETER);
    AssertMsgReturn(VALID_PTR(ppSSM), ("%p\n", ppSSM), VERR_INVALID_PARAMETER);

    /*
     * Allocate a handle.
     */
    PSSMHANDLE pSSM = (PSSMHANDLE)RTMemAllocZ(sizeof(*pSSM));
    AssertReturn(pSSM, VERR_NO_MEMORY);

    /*
     * Try open the file and validate it.
     */
    int rc = RTFileOpen(&pSSM->File, pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        SSMFILEHDR Hdr;
        size_t cbFileHdr;
        rc = ssmR3Validate(pSSM->File, &Hdr, &cbFileHdr);
        if (RT_SUCCESS(rc))
        {
            //pSSM->pVM           = NULL;
            pSSM->cbFileHdr       = cbFileHdr;
            pSSM->enmOp           = SSMSTATE_OPEN_READ;
            pSSM->enmAfter        = SSMAFTER_OPENED;
            //pSSM->rc            = VINF_SUCCESS;
            //pSSM->pZipComp      = NULL;
            //pSSM->pZipDecomp    = NULL;
            //pSSM->cbUnitLeft    = 0;
            //pSSM->pfnProgress   = NULL;
            //pSSM->pvUser        = NULL;
            //pSSM->uPercent      = 0;
            //pSSM->offEstProgress= 0;
            //pSSM->cbEstTotal    = 0;
            //pSSM->offEst        = 0;
            //pSSM->offEstUnitEnd = 0;
            pSSM->uPercentPrepare = 20;
            pSSM->uPercentDone    = 2;
            pSSM->cbGCPhys        = Hdr.cbGCPhys ? Hdr.cbGCPhys : sizeof(RTGCPHYS);
            pSSM->cbGCPtr         = sizeof(RTGCPTR);
            pSSM->fFixedGCPtrSize = false;
            if (Hdr.cbGCPtr)
            {
                pSSM->cbGCPtr         = Hdr.cbGCPtr;
                pSSM->fFixedGCPtrSize = true;
            }

            *ppSSM = pSSM;
            LogFlow(("SSMR3Open: returns VINF_SUCCESS *ppSSM=%p\n", *ppSSM));
            return VINF_SUCCESS;
        }

        Log(("SSMR3Open: Validation of '%s' failed, rc=%Rrc.\n",  pszFilename, rc));
        RTFileClose(pSSM->File);
    }
    else
        Log(("SSMR3Open: Failed to open saved state file '%s', rc=%Rrc.\n",  pszFilename, rc));
    RTMemFree(pSSM);
    return rc;

}


/**
 * Closes a saved state file opened by SSMR3Open().
 *
 * @returns VBox status code.
 *
 * @param   pSSM            The SSM handle returned by SSMR3Open().
 *
 * @thread  Any, but the caller is responsible for serializing calls per handle.
 */
VMMR3DECL(int) SSMR3Close(PSSMHANDLE pSSM)
{
    LogFlow(("SSMR3Close: pSSM=%p\n", pSSM));

    /*
     * Validate input.
     */
    AssertMsgReturn(VALID_PTR(pSSM), ("%p\n", pSSM), VERR_INVALID_PARAMETER);
    AssertMsgReturn(pSSM->enmAfter == SSMAFTER_OPENED, ("%d\n", pSSM->enmAfter),VERR_INVALID_PARAMETER);
    AssertMsgReturn(pSSM->enmOp == SSMSTATE_OPEN_READ, ("%d\n", pSSM->enmOp), VERR_INVALID_PARAMETER);

    /*
     * Close the file and free the handle.
     */
    int rc = RTFileClose(pSSM->File);
    AssertRC(rc);
    RTMemFree(pSSM);
    return rc;
}


/**
 * Seeks to a specific data unit.
 *
 * After seeking it's possible to use the getters to on
 * that data unit.
 *
 * @returns VBox status code.
 * @returns VERR_SSM_UNIT_NOT_FOUND if the unit+instance wasn't found.
 *
 * @param   pSSM            The SSM handle returned by SSMR3Open().
 * @param   pszUnit         The name of the data unit.
 * @param   iInstance       The instance number.
 * @param   piVersion       Where to store the version number. (Optional)
 *
 * @thread  Any, but the caller is responsible for serializing calls per handle.
 */
VMMR3DECL(int) SSMR3Seek(PSSMHANDLE pSSM, const char *pszUnit, uint32_t iInstance, uint32_t *piVersion)
{
    LogFlow(("SSMR3Seek: pSSM=%p pszUnit=%p:{%s} iInstance=%RU32 piVersion=%p\n",
             pSSM, pszUnit, pszUnit, iInstance, piVersion));

    /*
     * Validate input.
     */
    AssertMsgReturn(VALID_PTR(pSSM), ("%p\n", pSSM), VERR_INVALID_PARAMETER);
    AssertMsgReturn(pSSM->enmAfter == SSMAFTER_OPENED, ("%d\n", pSSM->enmAfter),VERR_INVALID_PARAMETER);
    AssertMsgReturn(pSSM->enmOp == SSMSTATE_OPEN_READ, ("%d\n", pSSM->enmOp), VERR_INVALID_PARAMETER);
    AssertMsgReturn(VALID_PTR(pszUnit), ("%p\n", pszUnit), VERR_INVALID_POINTER);
    AssertMsgReturn(!piVersion || VALID_PTR(piVersion), ("%p\n", piVersion), VERR_INVALID_POINTER);

    /*
     * Reset the state.
     */
    if (pSSM->pZipDecomp)
    {
        RTZipDecompDestroy(pSSM->pZipDecomp);
        pSSM->pZipDecomp = NULL;
    }
    pSSM->rc            = VERR_SSM_UNIT_NOT_FOUND;
    pSSM->cbUnitLeft    = 0;

    /*
     * Walk the data units until we find EOF or a match.
     */
    size_t  cchUnit = strlen(pszUnit) + 1;
    int     rc = VINF_SUCCESS;
    char   *pszName = NULL;
    size_t  cchName = 0;
    SSMFILEUNITHDR  UnitHdr;
    for (RTFOFF off = pSSM->cbFileHdr; ; off += UnitHdr.cbUnit)
    {
        /*
         * Read the unit header and verify it.
         */
        rc = RTFileReadAt(pSSM->File, off, &UnitHdr, RT_OFFSETOF(SSMFILEUNITHDR, szName), NULL);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            if (!memcmp(&UnitHdr.achMagic[0], SSMFILEUNITHDR_MAGIC, sizeof(SSMFILEUNITHDR_MAGIC)))
            {
                /*
                 * Does it match thus far or should we just skip along?
                 */
                if (    UnitHdr.u32Instance != iInstance
                    &&  UnitHdr.cchName != cchUnit)
                    continue;

                /*
                 * Read the name.
                 * Adjust the name buffer first.
                 */
                if (cchName < UnitHdr.cchName)
                {
                    if (pszName)
                        RTMemTmpFree(pszName);
                    cchName = RT_ALIGN_Z(UnitHdr.cchName, 64);
                    pszName = (char *)RTMemTmpAlloc(cchName);
                }
                rc = VERR_NO_MEMORY;
                if (pszName)
                {
                    rc = RTFileRead(pSSM->File, pszName, UnitHdr.cchName, NULL);
                    AssertRC(rc);
                    if (RT_SUCCESS(rc))
                    {
                        if (!pszName[UnitHdr.cchName - 1])
                        {
                            /*
                             * Does the name match? If not continue with the next item.
                             */
                            if (memcmp(pszName, pszUnit, cchUnit))
                                continue;

                            pSSM->rc = rc = VINF_SUCCESS;
                            pSSM->cbUnitLeft = UnitHdr.cbUnit - RT_OFFSETOF(SSMFILEUNITHDR, szName[UnitHdr.cchName]);
                            if (piVersion)
                                *piVersion = UnitHdr.u32Version;
                        }
                        else
                        {
                            AssertMsgFailed((" Unit name '%.*s' was not properly terminated.\n", UnitHdr.cchName, pszName));
                            rc = VERR_SSM_INTEGRITY;
                        }
                    }
                }
            }
            else
            {
                if (!memcmp(&UnitHdr.achMagic[0], SSMFILEUNITHDR_END, sizeof(SSMFILEUNITHDR_END)))
                    rc = VERR_SSM_UNIT_NOT_FOUND;
                else
                {
                    AssertMsgFailed(("Invalid unit magic at offset %RTfoff, '%.*s'!\n",
                                     off, sizeof(UnitHdr.achMagic) - 1, &UnitHdr.achMagic[0]));
                    rc = VERR_SSM_INTEGRITY_UNIT_MAGIC;
                }
            }
        }

        /* error or success, two continue statements cover the iterating */
        break;
    }

    RTMemFree(pszName);
    return rc;
}


/**
 * Finishes a data unit.
 * All buffers and compressor instances are flushed and destroyed.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 */
static int ssmR3WriteFinish(PSSMHANDLE pSSM)
{
    //Log2(("ssmR3WriteFinish: %#010llx start\n", RTFileTell(pSSM->File)));
    if (!pSSM->pZipComp)
        return VINF_SUCCESS;

    int rc = RTZipCompFinish(pSSM->pZipComp);
    if (RT_SUCCESS(rc))
    {
        rc = RTZipCompDestroy(pSSM->pZipComp);
        if (RT_SUCCESS(rc))
        {
            pSSM->pZipComp = NULL;
            //Log2(("ssmR3WriteFinish: %#010llx done\n", RTFileTell(pSSM->File)));
            return VINF_SUCCESS;
        }
    }
    if (RT_SUCCESS(pSSM->rc))
        pSSM->rc = rc;
    Log2(("ssmR3WriteFinish: failure rc=%Rrc\n", rc));
    return rc;
}


/**
 * Writes something to the current data item in the saved state file.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pvBuf           The bits to write.
 * @param   cbBuf           The number of bytes to write.
 */
static int ssmR3Write(PSSMHANDLE pSSM, const void *pvBuf, size_t cbBuf)
{
    Log2(("ssmR3Write: pvBuf=%p cbBuf=%#x %.*Rhxs%s\n", pvBuf, cbBuf, RT_MIN(cbBuf, 128), pvBuf, cbBuf > 128 ? "..." : ""));

    /*
     * Check that everything is fine.
     */
    if (RT_SUCCESS(pSSM->rc))
    {
        /*
         * First call starts the compression.
         */
        if (!pSSM->pZipComp)
        {
            //int rc = RTZipCompCreate(&pSSM->pZipComp, pSSM, ssmR3WriteOut, RTZIPTYPE_ZLIB, RTZIPLEVEL_FAST);
            int rc = RTZipCompCreate(&pSSM->pZipComp, pSSM, ssmR3WriteOut, RTZIPTYPE_LZF, RTZIPLEVEL_FAST);
            if (RT_FAILURE(rc))
                return rc;
        }

        /*
         * Write the data item in 128kb chunks for progress indicator reasons.
         */
        while (cbBuf > 0)
        {
            size_t cbChunk = RT_MIN(cbBuf, 128*1024);
            pSSM->rc = RTZipCompress(pSSM->pZipComp, pvBuf, cbChunk);
            if (RT_FAILURE(pSSM->rc))
                break;
            ssmR3Progress(pSSM, cbChunk);
            cbBuf -= cbChunk;
            pvBuf = (char *)pvBuf + cbChunk;
        }
    }

    return pSSM->rc;
}


/**
 * Callback for flusing the output buffer of a compression stream.
 *
 * @returns VBox status.
 * @param   pvSSM           SSM operation handle.
 * @param   pvBuf           Compressed data.
 * @param   cbBuf           Size of the compressed data.
 */
static DECLCALLBACK(int) ssmR3WriteOut(void *pvSSM, const void *pvBuf, size_t cbBuf)
{
    //Log2(("ssmR3WriteOut: %#010llx cbBuf=%#x\n", RTFileTell(((PSSMHANDLE)pvSSM)->File), cbBuf));
    int rc = RTFileWrite(((PSSMHANDLE)pvSSM)->File, pvBuf, cbBuf, NULL);
    if (RT_SUCCESS(rc))
        return rc;
    Log(("ssmR3WriteOut: RTFileWrite(,,%d) -> %d\n", cbBuf, rc));
    return rc;
}


/**
 * Puts a structure.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pvStruct        The structure address.
 * @param   paFields        The array of structure fields descriptions.
 *                          The array must be terminated by a SSMFIELD_ENTRY_TERM().
 */
VMMR3DECL(int) SSMR3PutStruct(PSSMHANDLE pSSM, const void *pvStruct, PCSSMFIELD paFields)
{
    /* begin marker. */
    int rc = SSMR3PutU32(pSSM, SSMR3STRUCT_BEGIN);
    if (RT_FAILURE(rc))
        return rc;

    /* put the fields */
    for (PCSSMFIELD pCur = paFields;
         pCur->cb != UINT32_MAX && pCur->off != UINT32_MAX;
         pCur++)
    {
        rc = ssmR3Write(pSSM, (uint8_t *)pvStruct + pCur->off, pCur->cb);
        if (RT_FAILURE(rc))
            return rc;
    }

    /* end marker */
    return SSMR3PutU32(pSSM, SSMR3STRUCT_END);
}


/**
 * Saves a boolean item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   fBool           Item to save.
 */
VMMR3DECL(int) SSMR3PutBool(PSSMHANDLE pSSM, bool fBool)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
    {
        uint8_t u8 = fBool; /* enforce 1 byte size */
        return ssmR3Write(pSSM, &u8, sizeof(u8));
    }
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a 8-bit unsigned integer item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   u8              Item to save.
 */
VMMR3DECL(int) SSMR3PutU8(PSSMHANDLE pSSM, uint8_t u8)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &u8, sizeof(u8));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a 8-bit signed integer item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   i8              Item to save.
 */
VMMR3DECL(int) SSMR3PutS8(PSSMHANDLE pSSM, int8_t i8)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &i8, sizeof(i8));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a 16-bit unsigned integer item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   u16             Item to save.
 */
VMMR3DECL(int) SSMR3PutU16(PSSMHANDLE pSSM, uint16_t u16)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &u16, sizeof(u16));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a 16-bit signed integer item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   i16             Item to save.
 */
VMMR3DECL(int) SSMR3PutS16(PSSMHANDLE pSSM, int16_t i16)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &i16, sizeof(i16));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a 32-bit unsigned integer item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   u32             Item to save.
 */
VMMR3DECL(int) SSMR3PutU32(PSSMHANDLE pSSM, uint32_t u32)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &u32, sizeof(u32));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a 32-bit signed integer item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   i32             Item to save.
 */
VMMR3DECL(int) SSMR3PutS32(PSSMHANDLE pSSM, int32_t i32)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &i32, sizeof(i32));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a 64-bit unsigned integer item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   u64             Item to save.
 */
VMMR3DECL(int) SSMR3PutU64(PSSMHANDLE pSSM, uint64_t u64)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &u64, sizeof(u64));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a 64-bit signed integer item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   i64             Item to save.
 */
VMMR3DECL(int) SSMR3PutS64(PSSMHANDLE pSSM, int64_t i64)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &i64, sizeof(i64));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a 128-bit unsigned integer item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   u128            Item to save.
 */
VMMR3DECL(int) SSMR3PutU128(PSSMHANDLE pSSM, uint128_t u128)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &u128, sizeof(u128));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a 128-bit signed integer item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   i128            Item to save.
 */
VMMR3DECL(int) SSMR3PutS128(PSSMHANDLE pSSM, int128_t i128)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &i128, sizeof(i128));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a VBox unsigned integer item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   u               Item to save.
 */
VMMR3DECL(int) SSMR3PutUInt(PSSMHANDLE pSSM, RTUINT u)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &u, sizeof(u));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a VBox signed integer item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   i               Item to save.
 */
VMMR3DECL(int) SSMR3PutSInt(PSSMHANDLE pSSM, RTINT i)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &i, sizeof(i));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a GC natural unsigned integer item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   u               Item to save.
 *
 * @deprecated Silly type, don't use it.
 */
VMMR3DECL(int) SSMR3PutGCUInt(PSSMHANDLE pSSM, RTGCUINT u)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &u, sizeof(u));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a GC unsigned integer register item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   u               Item to save.
 */
VMMR3DECL(int) SSMR3PutGCUIntReg(PSSMHANDLE pSSM, RTGCUINTREG u)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &u, sizeof(u));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a 32 bits GC physical address item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   GCPhys          The item to save
 */
VMMR3DECL(int) SSMR3PutGCPhys32(PSSMHANDLE pSSM, RTGCPHYS32 GCPhys)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &GCPhys, sizeof(GCPhys));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a 64 bits GC physical address item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   GCPhys          The item to save
 */
VMMR3DECL(int) SSMR3PutGCPhys64(PSSMHANDLE pSSM, RTGCPHYS64 GCPhys)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &GCPhys, sizeof(GCPhys));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a GC physical address item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   GCPhys          The item to save
 */
VMMR3DECL(int) SSMR3PutGCPhys(PSSMHANDLE pSSM, RTGCPHYS GCPhys)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &GCPhys, sizeof(GCPhys));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a GC virtual address item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   GCPtr           The item to save.
 */
VMMR3DECL(int) SSMR3PutGCPtr(PSSMHANDLE pSSM, RTGCPTR GCPtr)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &GCPtr, sizeof(GCPtr));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves an RC virtual address item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   RCPtr           The item to save.
 */
VMMR3DECL(int) SSMR3PutRCPtr(PSSMHANDLE pSSM, RTRCPTR RCPtr)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &RCPtr, sizeof(RCPtr));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a GC virtual address (represented as an unsigned integer) item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   GCPtr           The item to save.
 */
VMMR3DECL(int) SSMR3PutGCUIntPtr(PSSMHANDLE pSSM, RTGCUINTPTR GCPtr)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &GCPtr, sizeof(GCPtr));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a I/O port address item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   IOPort          The item to save.
 */
VMMR3DECL(int) SSMR3PutIOPort(PSSMHANDLE pSSM, RTIOPORT IOPort)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &IOPort, sizeof(IOPort));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a selector item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   Sel             The item to save.
 */
VMMR3DECL(int) SSMR3PutSel(PSSMHANDLE pSSM, RTSEL Sel)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, &Sel, sizeof(Sel));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a memory item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pv              Item to save.
 * @param   cb              Size of the item.
 */
VMMR3DECL(int) SSMR3PutMem(PSSMHANDLE pSSM, const void *pv, size_t cb)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
        return ssmR3Write(pSSM, pv, cb);
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Saves a zero terminated string item to the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   psz             Item to save.
 */
VMMR3DECL(int) SSMR3PutStrZ(PSSMHANDLE pSSM, const char *psz)
{
    if (pSSM->enmOp == SSMSTATE_SAVE_EXEC)
    {
        size_t cch = strlen(psz);
        if (cch > 1024*1024)
        {
            AssertMsgFailed(("a %d byte long string, what's this!?!\n"));
            return VERR_TOO_MUCH_DATA;
        }
        uint32_t u32 = (uint32_t)cch;
        int rc = ssmR3Write(pSSM, &u32, sizeof(u32));
        if (rc)
            return rc;
        return ssmR3Write(pSSM, psz, cch);
    }
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}





/**
 * Closes the decompressor of a data unit.
 *
 * @param   pSSM            SSM operation handle.
 */
static void ssmR3ReadFinish(PSSMHANDLE pSSM)
{
    int rc = RTZipDecompDestroy(pSSM->pZipDecomp);
    AssertRC(rc);
    pSSM->pZipDecomp = NULL;
}


/**
 * Internal read worker.
 *
 * @param   pSSM            SSM operation handle.
 * @param   pvBuf           Where to store the read data.
 * @param   cbBuf           Number of bytes to read.
 */
static int ssmR3Read(PSSMHANDLE pSSM, void *pvBuf, size_t cbBuf)
{
    /*
     * Check that everything is fine.
     */
    if (RT_SUCCESS(pSSM->rc))
    {
        /*
         * Open the decompressor on the first read.
         */
        if (!pSSM->pZipDecomp)
        {
            pSSM->rc = RTZipDecompCreate(&pSSM->pZipDecomp, pSSM, ssmR3ReadIn);
            if (RT_FAILURE(pSSM->rc))
                return pSSM->rc;
        }

        /*
         * Do the requested read.
         * Use 32kb chunks to work the progress indicator.
         */
        pSSM->rc = RTZipDecompress(pSSM->pZipDecomp, pvBuf, cbBuf, NULL);
        if (RT_SUCCESS(pSSM->rc))
            Log2(("ssmR3Read: pvBuf=%p cbBuf=%#x %.*Rhxs%s\n", pvBuf, cbBuf, RT_MIN(cbBuf, 128), pvBuf, cbBuf > 128 ? "..." : ""));
        else
            AssertMsgFailed(("rc=%Rrc cbBuf=%#x\n", pSSM->rc, cbBuf));
    }

    return pSSM->rc;
}


/**
 * Callback for reading compressed data into the input buffer of the
 * decompressor.
 *
 * @returns VBox status code.
 * @param   pvSSM       The SSM handle.
 * @param   pvBuf       Where to store the compressed data.
 * @param   cbBuf       Size of the buffer.
 * @param   pcbRead     Number of bytes actually stored in the buffer.
 */
static DECLCALLBACK(int) ssmR3ReadIn(void *pvSSM, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    PSSMHANDLE pSSM = (PSSMHANDLE)pvSSM;
    size_t cbRead = cbBuf;
    if (pSSM->cbUnitLeft < cbBuf)
        cbRead = (size_t)pSSM->cbUnitLeft;
    if (cbRead)
    {
        //Log2(("ssmR3ReadIn: %#010llx cbBug=%#x cbRead=%#x\n", RTFileTell(pSSM->File), cbBuf, cbRead));
        int rc = RTFileRead(pSSM->File, pvBuf, cbRead, NULL);
        if (RT_SUCCESS(rc))
        {
            pSSM->cbUnitLeft -= cbRead;
            if (pcbRead)
                *pcbRead = cbRead;
            ssmR3Progress(pSSM, cbRead);
            return VINF_SUCCESS;
        }
        Log(("ssmR3ReadIn: RTFileRead(,,%d) -> %d\n", cbRead, rc));
        return rc;
    }

    /** @todo weed out lazy saving */
    if (pSSM->enmAfter != SSMAFTER_DEBUG_IT)
        AssertMsgFailed(("SSM: attempted reading more than the unit!\n"));
    return VERR_SSM_LOADED_TOO_MUCH;
}


/**
 * Gets a structure.
 *
 * @returns VBox status code.
 * @param   pSSM            The saved state handle.
 * @param   pvStruct        The structure address.
 * @param   paFields        The array of structure fields descriptions.
 *                          The array must be terminated by a SSMFIELD_ENTRY_TERM().
 */
VMMR3DECL(int) SSMR3GetStruct(PSSMHANDLE pSSM, void *pvStruct, PCSSMFIELD paFields)
{
    /* begin marker. */
    uint32_t u32Magic;
    int rc = SSMR3GetU32(pSSM, &u32Magic);
    if (RT_FAILURE(rc))
        return rc;
    if (u32Magic != SSMR3STRUCT_BEGIN)
        AssertMsgFailedReturn(("u32Magic=%#RX32\n", u32Magic), VERR_SSM_STRUCTURE_MAGIC);

    /* put the fields */
    for (PCSSMFIELD pCur = paFields;
         pCur->cb != UINT32_MAX && pCur->off != UINT32_MAX;
         pCur++)
    {
        rc = ssmR3Read(pSSM, (uint8_t *)pvStruct + pCur->off, pCur->cb);
        if (RT_FAILURE(rc))
            return rc;
    }

    /* end marker */
    rc = SSMR3GetU32(pSSM, &u32Magic);
    if (RT_FAILURE(rc))
        return rc;
    if (u32Magic != SSMR3STRUCT_END)
        AssertMsgFailedReturn(("u32Magic=%#RX32\n", u32Magic), VERR_SSM_STRUCTURE_MAGIC);
    return rc;
}


/**
 * Loads a boolean item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pfBool          Where to store the item.
 */
VMMR3DECL(int) SSMR3GetBool(PSSMHANDLE pSSM, bool *pfBool)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
    {
        uint8_t u8; /* see SSMR3PutBool */
        int rc = ssmR3Read(pSSM, &u8, sizeof(u8));
        if (RT_SUCCESS(rc))
        {
            Assert(u8 <= 1);
            *pfBool = !!u8;
        }
        return rc;
    }
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a 8-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pu8             Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU8(PSSMHANDLE pSSM, uint8_t *pu8)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pu8, sizeof(*pu8));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a 8-bit signed integer item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pi8             Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS8(PSSMHANDLE pSSM, int8_t *pi8)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pi8, sizeof(*pi8));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a 16-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pu16            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU16(PSSMHANDLE pSSM, uint16_t *pu16)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pu16, sizeof(*pu16));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a 16-bit signed integer item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pi16            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS16(PSSMHANDLE pSSM, int16_t *pi16)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pi16, sizeof(*pi16));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a 32-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pu32            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU32(PSSMHANDLE pSSM, uint32_t *pu32)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pu32, sizeof(*pu32));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a 32-bit signed integer item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pi32            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS32(PSSMHANDLE pSSM, int32_t *pi32)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pi32, sizeof(*pi32));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a 64-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pu64            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU64(PSSMHANDLE pSSM, uint64_t *pu64)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pu64, sizeof(*pu64));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a 64-bit signed integer item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pi64            Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS64(PSSMHANDLE pSSM, int64_t *pi64)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pi64, sizeof(*pi64));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a 128-bit unsigned integer item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pu128           Where to store the item.
 */
VMMR3DECL(int) SSMR3GetU128(PSSMHANDLE pSSM, uint128_t *pu128)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pu128, sizeof(*pu128));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a 128-bit signed integer item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pi128           Where to store the item.
 */
VMMR3DECL(int) SSMR3GetS128(PSSMHANDLE pSSM, int128_t *pi128)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pi128, sizeof(*pi128));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a VBox unsigned integer item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pu              Where to store the integer.
 */
VMMR3DECL(int) SSMR3GetUInt(PSSMHANDLE pSSM, PRTUINT pu)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pu, sizeof(*pu));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a VBox signed integer item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pi              Where to store the integer.
 */
VMMR3DECL(int) SSMR3GetSInt(PSSMHANDLE pSSM, PRTINT pi)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pi, sizeof(*pi));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a GC natural unsigned integer item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pu              Where to store the integer.
 *
 * @deprecated Silly type with an incorrect size, don't use it.
 */
VMMR3DECL(int) SSMR3GetGCUInt(PSSMHANDLE pSSM, PRTGCUINT pu)
{
    AssertCompile(sizeof(RTGCPTR) == sizeof(*pu));
    return SSMR3GetGCPtr(pSSM, (PRTGCPTR)pu);
}


/**
 * Loads a GC unsigned integer register item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pu              Where to store the integer.
 */
VMMR3DECL(int) SSMR3GetGCUIntReg(PSSMHANDLE pSSM, PRTGCUINTREG pu)
{
    AssertCompile(sizeof(RTGCPTR) == sizeof(*pu));
    return SSMR3GetGCPtr(pSSM, (PRTGCPTR)pu);
}


/**
 * Loads a 32 bits GC physical address item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pGCPhys         Where to store the GC physical address.
 */
VMMR3DECL(int) SSMR3GetGCPhys32(PSSMHANDLE pSSM, PRTGCPHYS32 pGCPhys)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pGCPhys, sizeof(*pGCPhys));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a 64 bits GC physical address item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pGCPhys         Where to store the GC physical address.
 */
VMMR3DECL(int) SSMR3GetGCPhys64(PSSMHANDLE pSSM, PRTGCPHYS64 pGCPhys)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pGCPhys, sizeof(*pGCPhys));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a GC physical address item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pGCPhys         Where to store the GC physical address.
 */
VMMR3DECL(int) SSMR3GetGCPhys(PSSMHANDLE pSSM, PRTGCPHYS pGCPhys)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
    {
        if (sizeof(*pGCPhys) != pSSM->cbGCPhys)
        {
            Assert(sizeof(*pGCPhys) == sizeof(uint64_t) || sizeof(*pGCPhys) == sizeof(uint32_t));
            Assert(pSSM->cbGCPhys   == sizeof(uint64_t) || pSSM->cbGCPhys   == sizeof(uint32_t));
            if (pSSM->cbGCPhys == sizeof(uint64_t))
            {
                /* 64-bit saved, 32-bit load: try truncate it. */
                uint64_t u64;
                int rc = ssmR3Read(pSSM, &u64, pSSM->cbGCPhys);
                if (RT_FAILURE(rc))
                    return rc;
                if (u64 >= _4G)
                    return VERR_SSM_GCPHYS_OVERFLOW;
                *pGCPhys = (RTGCPHYS)u64;
                return rc;
            }
            /* 32-bit saved, 64-bit load: clear the high part. */
            *pGCPhys = 0;
        }
        return ssmR3Read(pSSM, pGCPhys, pSSM->cbGCPhys);
    }
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a GC virtual address item from the current data unit.
 *
 * Only applies to in the 1.1 format:
 *  - SSMR3GetGCPtr
 *  - SSMR3GetGCUIntPtr
 *  - SSMR3GetGCUInt
 *  - SSMR3GetGCUIntReg
 *
 * Put functions are not affected.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   cbGCPtr         Size of RTGCPTR
 *
 * @remarks This interface only works with saved state version 1.1, if the
 *          format isn't 1.1 the call will be ignored.
 */
VMMR3DECL(int) SSMR3SetGCPtrSize(PSSMHANDLE pSSM, unsigned cbGCPtr)
{
    Assert(cbGCPtr == sizeof(RTGCPTR32) || cbGCPtr == sizeof(RTGCPTR64));
    if (!pSSM->fFixedGCPtrSize)
    {
        Log(("SSMR3SetGCPtrSize: %d -> %d bytes\n", pSSM->cbGCPtr, cbGCPtr));
        pSSM->cbGCPtr = cbGCPtr;
        pSSM->fFixedGCPtrSize = true;
    }
    else if (   pSSM->cbGCPtr != cbGCPtr
             && pSSM->cbFileHdr == sizeof(SSMFILEHDRV11))
        AssertMsgFailed(("SSMR3SetGCPtrSize: already fixed at %d bytes; requested %d bytes\n", pSSM->cbGCPtr, cbGCPtr));

    return VINF_SUCCESS;
}


/**
 * Loads a GC virtual address item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pGCPtr          Where to store the GC virtual address.
 */
VMMR3DECL(int) SSMR3GetGCPtr(PSSMHANDLE pSSM, PRTGCPTR pGCPtr)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
    {
        if (sizeof(*pGCPtr) != pSSM->cbGCPtr)
        {
            Assert(sizeof(*pGCPtr) == sizeof(uint64_t) || sizeof(*pGCPtr) == sizeof(uint32_t));
            Assert(pSSM->cbGCPtr   == sizeof(uint64_t) || pSSM->cbGCPtr   == sizeof(uint32_t));
            if (pSSM->cbGCPtr == sizeof(uint64_t))
            {
                /* 64-bit saved, 32-bit load: try truncate it. */
                uint64_t u64;
                int rc = ssmR3Read(pSSM, &u64, pSSM->cbGCPhys);
                if (RT_FAILURE(rc))
                    return rc;
                if (u64 >= _4G)
                    return VERR_SSM_GCPTR_OVERFLOW;
                *pGCPtr = (RTGCPTR)u64;
                return rc;
            }
            /* 32-bit saved, 64-bit load: clear the high part. */
            *pGCPtr = 0;
        }
        return ssmR3Read(pSSM, pGCPtr, pSSM->cbGCPtr);
    }
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a GC virtual address (represented as unsigned integer) item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pGCPtr          Where to store the GC virtual address.
 */
VMMR3DECL(int) SSMR3GetGCUIntPtr(PSSMHANDLE pSSM, PRTGCUINTPTR pGCPtr)
{
    AssertCompile(sizeof(RTGCPTR) == sizeof(*pGCPtr));
    return SSMR3GetGCPtr(pSSM, (PRTGCPTR)pGCPtr);
}


/**
 * Loads an RC virtual address item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pRCPtr          Where to store the RC virtual address.
 */
VMMR3DECL(int) SSMR3GetRCPtr(PSSMHANDLE pSSM, PRTRCPTR pRCPtr)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pRCPtr, sizeof(*pRCPtr));

    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a I/O port address item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pIOPort         Where to store the I/O port address.
 */
VMMR3DECL(int) SSMR3GetIOPort(PSSMHANDLE pSSM, PRTIOPORT pIOPort)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pIOPort, sizeof(*pIOPort));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a selector item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pSel            Where to store the selector.
 */
VMMR3DECL(int) SSMR3GetSel(PSSMHANDLE pSSM, PRTSEL pSel)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pSel, sizeof(*pSel));
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a memory item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   pv              Where to store the item.
 * @param   cb              Size of the item.
 */
VMMR3DECL(int) SSMR3GetMem(PSSMHANDLE pSSM, void *pv, size_t cb)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
        return ssmR3Read(pSSM, pv, cb);
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}


/**
 * Loads a string item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   psz             Where to store the item.
 * @param   cbMax           Max size of the item (including '\\0').
 */
VMMR3DECL(int) SSMR3GetStrZ(PSSMHANDLE pSSM, char *psz, size_t cbMax)
{
    return SSMR3GetStrZEx(pSSM, psz, cbMax, NULL);
}


/**
 * Loads a string item from the current data unit.
 *
 * @returns VBox status.
 * @param   pSSM            SSM operation handle.
 * @param   psz             Where to store the item.
 * @param   cbMax           Max size of the item (including '\\0').
 * @param   pcbStr          The length of the loaded string excluding the '\\0'. (optional)
 */
VMMR3DECL(int) SSMR3GetStrZEx(PSSMHANDLE pSSM, char *psz, size_t cbMax, size_t *pcbStr)
{
    if (pSSM->enmOp == SSMSTATE_LOAD_EXEC || pSSM->enmOp == SSMSTATE_OPEN_READ)
    {
        /* read size prefix. */
        uint32_t u32;
        int rc = SSMR3GetU32(pSSM, &u32);
        if (RT_SUCCESS(rc))
        {
            if (pcbStr)
                *pcbStr = u32;
            if (u32 < cbMax)
            {
                /* terminate and read string content. */
                psz[u32] = '\0';
                return ssmR3Read(pSSM, psz, u32);
            }
            return VERR_TOO_MUCH_DATA;
        }
        return rc;
    }
    AssertMsgFailed(("Invalid state %d\n", pSSM->enmOp));
    return VERR_SSM_INVALID_STATE;
}



/**
 * Query what the VBox status code of the operation is.
 *
 * This can be used for putting and getting a batch of values
 * without bother checking the result till all the calls have
 * been made.
 *
 * @returns SSMAFTER enum value.
 * @param   pSSM            SSM operation handle.
 */
VMMR3DECL(int) SSMR3HandleGetStatus(PSSMHANDLE pSSM)
{
    return pSSM->rc;
}


/**
 * Fail the load operation.
 *
 * This is mainly intended for sub item loaders (like timers) which
 * return code isn't necessarily heeded by the caller but is important
 * to SSM.
 *
 * @returns SSMAFTER enum value.
 * @param   pSSM            SSM operation handle.
 * @param   iStatus         Failure status code. This MUST be a VERR_*.
 */
VMMR3DECL(int) SSMR3HandleSetStatus(PSSMHANDLE pSSM, int iStatus)
{
    if (RT_FAILURE(iStatus))
    {
        if (RT_SUCCESS(pSSM->rc))
            pSSM->rc = iStatus;
        return pSSM->rc = iStatus;
    }
    AssertMsgFailed(("iStatus=%d %Rrc\n", iStatus, iStatus));
    return VERR_INVALID_PARAMETER;
}


/**
 * Query what to do after this operation.
 *
 * @returns SSMAFTER enum value.
 * @param   pSSM            SSM operation handle.
 */
VMMR3DECL(SSMAFTER) SSMR3HandleGetAfter(PSSMHANDLE pSSM)
{
    return pSSM->enmAfter;
}

