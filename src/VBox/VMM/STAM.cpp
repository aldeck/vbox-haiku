/* $Id$ */
/** @file
 * STAM - The Statistics Manager.
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
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_STAM
#include <VBox/stam.h>
#include "STAMInternal.h"
#include <VBox/vm.h>
#include <VBox/err.h>
#include <VBox/dbg.h>
#include <VBox/log.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/alloc.h>
#include <iprt/stream.h>
#include <iprt/string.h>


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Argument structure for stamR3PrintOne().
 */
typedef struct STAMR3PRINTONEARGS
{
    PVM pVM;
    void *pvArg;
    DECLCALLBACKMEMBER(void, pfnPrintf)(struct STAMR3PRINTONEARGS *pvArg, const char *pszFormat, ...);
} STAMR3PRINTONEARGS, *PSTAMR3PRINTONEARGS;


/**
 * Argument structure to stamR3EnumOne().
 */
typedef struct STAMR3ENUMONEARGS
{
    PVM             pVM;
    PFNSTAMR3ENUM   pfnEnum;
    void           *pvUser;
} STAMR3ENUMONEARGS, *PSTAMR3ENUMONEARGS;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static int stamR3Register(PVM pVM, void *pvSample, PFNSTAMR3CALLBACKRESET pfnReset, PFNSTAMR3CALLBACKPRINT pfnPrint,
                          STAMTYPE enmType, STAMVISIBILITY enmVisibility, const char *pszName, STAMUNIT enmUnit, const char *pszDesc);
static int stamR3ResetOne(PSTAMDESC pDesc, void *pvArg);
static DECLCALLBACK(void) stamR3EnumLogPrintf(PSTAMR3PRINTONEARGS pvArg, const char *pszFormat, ...);
static DECLCALLBACK(void) stamR3EnumRelLogPrintf(PSTAMR3PRINTONEARGS pvArg, const char *pszFormat, ...);
static DECLCALLBACK(void) stamR3EnumPrintf(PSTAMR3PRINTONEARGS pvArg, const char *pszFormat, ...);
static int stamR3PrintOne(PSTAMDESC pDesc, void *pvArg);
static int stamR3EnumOne(PSTAMDESC pDesc, void *pvArg);
static int stamR3Enum(PVM pVM, const char *pszPat, int (pfnCallback)(PSTAMDESC pDesc, void *pvArg), void *pvArg);

#ifdef VBOX_WITH_DEBUGGER
static DECLCALLBACK(int)  stamR3CmdStats(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PVM pVM, PCDBGCVAR paArgs, unsigned cArgs, PDBGCVAR pResult);
static DECLCALLBACK(void) stamR3EnumDbgfPrintf(PSTAMR3PRINTONEARGS pArgs, const char *pszFormat, ...);
static DECLCALLBACK(int)  stamR3CmdStatsReset(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PVM pVM, PCDBGCVAR paArgs, unsigned cArgs, PDBGCVAR pResult);
#endif


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
#ifdef VBOX_WITH_DEBUGGER
/** Pattern argument. */
static const DBGCVARDESC    g_aArgPat[] =
{
    /* cTimesMin,   cTimesMax,  enmCategory,            fFlags,                         pszName,        pszDescription */
    {  0,           1,          DBGCVAR_CAT_STRING,     0,                              "pattern",      "Which samples the command shall be applied to. Use '*' as wildcard. Use ';' to separate expression." }
};

/** Command descriptors. */
static const DBGCCMD    g_aCmds[] =
{
    /* pszCmd,      cArgsMin, cArgsMax, paArgDesc,          cArgDescs,                  pResultDesc,        fFlags,     pfnHandler          pszSyntax,          ....pszDescription */
    { "stats",      0,        1,        &g_aArgPat[0],      ELEMENTS(g_aArgPat),        NULL,               0,          stamR3CmdStats,     "[pattern]",        "Display statistics." },
    { "statsreset", 0,        1,        &g_aArgPat[0],      ELEMENTS(g_aArgPat),        NULL,               0,          stamR3CmdStatsReset,"[pattern]",        "Resets statistics." }
};
#endif



/**
 * Initializes the STAM.
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
STAMR3DECL(int) STAMR3Init(PVM pVM)
{
    LogFlow(("STAMR3Init\n"));

    /*
     * Assert alignment and sizes.
     */
    AssertRelease(!(RT_OFFSETOF(VM, stam.s) & 31));
    AssertRelease(sizeof(pVM->stam.s) <= sizeof(pVM->stam.padding));

    /*
     * Setup any fixed pointers and offsets.
     */
    pVM->stam.s.offVM = RT_OFFSETOF(VM, stam);
    int rc = RTSemRWCreate(&pVM->stam.s.RWSem);
    AssertRC(rc);
    if (VBOX_FAILURE(rc))
        return rc;

#ifdef VBOX_WITH_DEBUGGER
    /*
     * Register debugger commands.
     */
    static bool fRegisteredCmds = false;
    if (!fRegisteredCmds)
    {
        int rc = DBGCRegisterCommands(&g_aCmds[0], ELEMENTS(g_aCmds));
        if (VBOX_SUCCESS(rc))
            fRegisteredCmds = true;
    }
#endif

    return VINF_SUCCESS;
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * @param   pVM     The VM.
 */
STAMR3DECL(void) STAMR3Relocate(PVM pVM)
{
    LogFlow(("STAMR3Relocate\n"));
    NOREF(pVM);
}


/**
 * Terminates the STAM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM it self is at this point powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
STAMR3DECL(int) STAMR3Term(PVM pVM)
{
    /*
     * Free used memory and RWLock.
     */
    PSTAMDESC   pCur = pVM->stam.s.pHead;
    while (pCur)
    {
        void *pvFree = pCur;
        pCur = pCur->pNext;
        RTMemFree(pvFree);
    }

    RTSemRWDestroy(pVM->stam.s.RWSem);
    return VINF_SUCCESS;
}




/**
 * Registers a sample with the statistics mamanger.
 *
 * Statistics are maintained on a per VM basis and should therefore
 * be registered during the VM init stage. However, there is not problem
 * registering temporary samples or samples for hotpluggable devices. Samples
 * can be deregisterd using the STAMR3Deregister() function, but note that
 * this is only necessary for temporary samples or hotpluggable devices.
 *
 * It is not possible to register the same sample twice.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   pvSample    Pointer to the sample.
 * @param   enmType     Sample type. This indicates what pvSample is pointing at.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   pszName     Sample name. The name is on this form "/<component>/<sample>".
 *                      Further nesting is possible.
 * @param   enmUnit     Sample unit.
 * @param   pszDesc     Sample description.
 */
STAMR3DECL(int)  STAMR3Register(PVM pVM, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility, const char *pszName, STAMUNIT enmUnit, const char *pszDesc)
{
    AssertReturn(enmType != STAMTYPE_CALLBACK, VERR_INVALID_PARAMETER);
    return stamR3Register(pVM, pvSample, NULL, NULL, enmType, enmVisibility, pszName, enmUnit, pszDesc);
}


/**
 * Same as STAMR3Register except that the name is specified in a
 * RTStrPrintf like fashion.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   pvSample    Pointer to the sample.
 * @param   enmType     Sample type. This indicates what pvSample is pointing at.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit     Sample unit.
 * @param   pszDesc     Sample description.
 * @param   pszName     The sample name format string.
 * @param   ...         Arguments to the format string.
 */
STAMR3DECL(int)  STAMR3RegisterF(PVM pVM, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit,
                                 const char *pszDesc, const char *pszName, ...)
{
    va_list args;
    va_start(args, pszName);
    int rc = STAMR3RegisterV(pVM, pvSample, enmType, enmVisibility, enmUnit, pszDesc, pszName, args);
    va_end(args);
    return rc;
}


/**
 * Same as STAMR3Register except that the name is specified in a
 * RTStrPrintfV like fashion.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   pvSample    Pointer to the sample.
 * @param   enmType     Sample type. This indicates what pvSample is pointing at.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit     Sample unit.
 * @param   pszDesc     Sample description.
 * @param   pszName     The sample name format string.
 * @param   args        Arguments to the format string.
 */
STAMR3DECL(int)  STAMR3RegisterV(PVM pVM, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit,
                                 const char *pszDesc, const char *pszName, va_list args)
{
    AssertReturn(enmType != STAMTYPE_CALLBACK, VERR_INVALID_PARAMETER);

    char *pszFormattedName;
    RTStrAPrintfV(&pszFormattedName, pszName, args);
    if (!pszFormattedName)
        return VERR_NO_MEMORY;

    int rc = STAMR3Register(pVM, pvSample, enmType, enmVisibility, pszFormattedName, enmUnit, pszDesc);
    RTStrFree(pszFormattedName);
    return rc;
}


/**
 * Similar to STAMR3Register except for the two callbacks, the implied type (STAMTYPE_CALLBACK),
 * and name given in an RTStrPrintf like fashion.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   pvSample    Pointer to the sample.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit     Sample unit.
 * @param   pfnReset    Callback for resetting the sample. NULL should be used if the sample can't be reset.
 * @param   pfnPrint    Print the sample.
 * @param   pszDesc     Sample description.
 * @param   pszName     The sample name format string.
 * @param   ...         Arguments to the format string.
 * @remark  There is currently no device or driver variant of this API. Add one if it should become necessary!
 */
STAMR3DECL(int)  STAMR3RegisterCallback(PVM pVM, void *pvSample, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit,
                                        PFNSTAMR3CALLBACKRESET pfnReset, PFNSTAMR3CALLBACKPRINT pfnPrint,
                                        const char *pszDesc, const char *pszName, ...)
{
    va_list args;
    va_start(args, pszName);
    int rc = STAMR3RegisterCallbackV(pVM, pvSample, enmVisibility, enmUnit, pfnReset, pfnPrint, pszDesc, pszName, args);
    va_end(args);
    return rc;
}


/**
 * Same as STAMR3RegisterCallback() except for the ellipsis which is a va_list here.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   pvSample    Pointer to the sample.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit     Sample unit.
 * @param   pfnReset    Callback for resetting the sample. NULL should be used if the sample can't be reset.
 * @param   pfnPrint    Print the sample.
 * @param   pszDesc     Sample description.
 * @param   pszName     The sample name format string.
 * @param   args        Arguments to the format string.
 * @remark  There is currently no device or driver variant of this API. Add one if it should become necessary!
 */
STAMR3DECL(int)  STAMR3RegisterCallbackV(PVM pVM, void *pvSample, STAMVISIBILITY enmVisibility, STAMUNIT enmUnit,
                                         PFNSTAMR3CALLBACKRESET pfnReset, PFNSTAMR3CALLBACKPRINT pfnPrint,
                                         const char *pszDesc, const char *pszName, va_list args)
{
    char *pszFormattedName;
    RTStrAPrintfV(&pszFormattedName, pszName, args);
    if (!pszFormattedName)
        return VERR_NO_MEMORY;

    int rc = stamR3Register(pVM, pvSample, pfnReset, pfnPrint, STAMTYPE_CALLBACK, enmVisibility, pszFormattedName, enmUnit, pszDesc);
    RTStrFree(pszFormattedName);
    return rc;
}


/**
 * Internal worker for the different register calls.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   pvSample    Pointer to the sample.
 * @param   pfnReset    Callback for resetting the sample. NULL should be used if the sample can't be reset.
 * @param   pfnPrint    Print the sample.
 * @param   enmType     Sample type. This indicates what pvSample is pointing at.
 * @param   enmVisibility  Visibility type specifying whether unused statistics should be visible or not.
 * @param   enmUnit     Sample unit.
 * @param   pszDesc     Sample description.
 * @param   pszName     The sample name format string.
 * @param   args        Arguments to the format string.
 * @remark  There is currently no device or driver variant of this API. Add one if it should become necessary!
 */
static int stamR3Register(PVM pVM, void *pvSample, PFNSTAMR3CALLBACKRESET pfnReset, PFNSTAMR3CALLBACKPRINT pfnPrint,
                          STAMTYPE enmType, STAMVISIBILITY enmVisibility, const char *pszName, STAMUNIT enmUnit, const char *pszDesc)
{
    STAM_LOCK_WR(pVM);

    /*
     * Check if exists.
     */
    PSTAMDESC   pPrev = NULL;
    PSTAMDESC   pCur = pVM->stam.s.pHead;
    while (pCur)
    {
        int iDiff = strcmp(pCur->pszName, pszName);
        /* passed it */
        if (iDiff > 0)
            break;
        /* found it. */
        if (!iDiff)
        {
            STAM_UNLOCK_WR(pVM);
            AssertMsgFailed(("Duplicate sample name: %s\n", pszName));
            return VERR_ALREADY_EXISTS;
        }

        /* next */
        pPrev = pCur;
        pCur = pCur->pNext;
    }

    /*
     * Create a new node and insert it at the current location.
     */
    int rc;
    int cchName = strlen(pszName) + 1;
    int cchDesc = pszDesc ? strlen(pszDesc) + 1 : 0;
    PSTAMDESC pNew = (PSTAMDESC)RTMemAlloc(sizeof(*pNew) + cchName + cchDesc);
    if (pNew)
    {
        pNew->pszName       = (char *)memcpy((char *)(pNew + 1), pszName, cchName);
        pNew->enmType       = enmType;
        pNew->enmVisibility = enmVisibility;
        if (enmType != STAMTYPE_CALLBACK)
            pNew->u.pv      = pvSample;
        else
        {
            pNew->u.Callback.pvSample = pvSample;
            pNew->u.Callback.pfnReset = pfnReset;
            pNew->u.Callback.pfnPrint = pfnPrint;
        }
        pNew->enmUnit       = enmUnit;
        pNew->pszDesc       = NULL;
        if (pszDesc)
            pNew->pszDesc   = (char *)memcpy((char *)(pNew + 1) + cchName,  pszDesc,  cchDesc);

        pNew->pNext         = pCur;
        if (pPrev)
            pPrev->pNext    = pNew;
        else
            pVM->stam.s.pHead = pNew;

        stamR3ResetOne(pNew, pVM);
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_NO_MEMORY;

    STAM_UNLOCK_WR(pVM);
    return rc;
}


/**
 * Deregisters a sample previously registered by STAR3Register().
 *
 * This is intended used for devices which can be unplugged and for
 * temporary samples.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   pvSample    Pointer to the sample registered with STAMR3Register().
 */
STAMR3DECL(int)  STAMR3Deregister(PVM pVM, void *pvSample)
{
    STAM_LOCK_WR(pVM);

    /*
     * Search for it.
     */
    int         rc = VERR_INVALID_HANDLE;
    PSTAMDESC   pPrev = NULL;
    PSTAMDESC   pCur = pVM->stam.s.pHead;
    while (pCur)
    {
        if (pCur->u.pv == pvSample)
        {
            void *pvFree = pCur;
            pCur = pCur->pNext;
            if (pPrev)
                pPrev->pNext = pCur;
            else
                pVM->stam.s.pHead = pCur;

            RTMemFree(pvFree);
            rc = VINF_SUCCESS;
            continue;
        }

        /* next */
        pPrev = pCur;
        pCur = pCur->pNext;
    }

    STAM_UNLOCK_WR(pVM);
    return rc;
}


/**
 * Resets statistics for the specified VM.
 * It's possible to select a subset of the samples.
 *
 * @returns VBox status. (Basically, it cannot fail.)
 * @param   pVM         The VM handle.
 * @param   pszPat      The name matching pattern. See somewhere_where_this_is_described_in_detail.
 *                      If NULL all samples are reset.
 */
STAMR3DECL(int)  STAMR3Reset(PVM pVM, const char *pszPat)
{
    STAM_LOCK_WR(pVM);
    stamR3Enum(pVM, pszPat, stamR3ResetOne, pVM);
    STAM_UNLOCK_WR(pVM);
    return VINF_SUCCESS;
}


/**
 * Resets one statistics sample.
 * Callback for stamR3Enum().
 *
 * @returns VINF_SUCCESS
 * @param   pDesc   Pointer to the current descriptor.
 * @param   pvArg   User argument - The VM handle.
 */
static int stamR3ResetOne(PSTAMDESC pDesc, void *pvArg)
{
    switch (pDesc->enmType)
    {
        case STAMTYPE_COUNTER:
            ASMAtomicXchgU64(&pDesc->u.pCounter->c, 0);
            break;

        case STAMTYPE_PROFILE:
        case STAMTYPE_PROFILE_ADV:
            ASMAtomicXchgU64(&pDesc->u.pProfile->cPeriods, 0);
            ASMAtomicXchgU64(&pDesc->u.pProfile->cTicks, 0);
            ASMAtomicXchgU64(&pDesc->u.pProfile->cTicksMax, 0);
            ASMAtomicXchgU64(&pDesc->u.pProfile->cTicksMin, ~0);
            break;

        case STAMTYPE_RATIO_U32_RESET:
            ASMAtomicXchgU32(&pDesc->u.pRatioU32->u32A, 0);
            ASMAtomicXchgU32(&pDesc->u.pRatioU32->u32B, 0);
            break;

        case STAMTYPE_CALLBACK:
            if (pDesc->u.Callback.pfnReset)
                pDesc->u.Callback.pfnReset((PVM)pvArg, pDesc->u.Callback.pvSample);
            break;

        case STAMTYPE_U8_RESET:
        case STAMTYPE_X8_RESET:
            ASMAtomicXchgU8(pDesc->u.pu8, 0);
            break;

        case STAMTYPE_U16_RESET:
        case STAMTYPE_X16_RESET:
            ASMAtomicXchgU16(pDesc->u.pu16, 0);
            break;

        case STAMTYPE_U32_RESET:
        case STAMTYPE_X32_RESET:
            ASMAtomicXchgU32(pDesc->u.pu32, 0);
            break;

        case STAMTYPE_U64_RESET:
        case STAMTYPE_X64_RESET:
            ASMAtomicXchgU64(pDesc->u.pu64, 0);
            break;

        /* These are custom and will not be touched. */
        case STAMTYPE_U8:
        case STAMTYPE_X8:
        case STAMTYPE_U16:
        case STAMTYPE_X16:
        case STAMTYPE_U32:
        case STAMTYPE_X32:
        case STAMTYPE_U64:
        case STAMTYPE_X64:
        case STAMTYPE_RATIO_U32:
            break;

        default:
            AssertMsgFailed(("enmType=%d\n", pDesc->enmType));
            break;
    }
    NOREF(pvArg);
    return VINF_SUCCESS;
}


/**
 * Get a snapshot of the statistics.
 * It's possible to select a subset of the samples.
 *
 * @returns VBox status. (Basically, it cannot fail.)
 * @param   pVM             The VM handle.
 * @param   pszPat          The name matching pattern. See somewhere_where_this_is_described_in_detail.
 *                          If NULL all samples are reset.
 * @param   ppszSnapshot    Where to store the pointer to the snapshot data.
 *                          The format of the snapshot should be XML, but that will have to be discussed
 *                          when this function is implemented.
 *                          The returned pointer must be freed by calling STAMR3SnapshotFree().
 * @param   pcchSnapshot    Where to store the size of the snapshot data. (Excluding the trailing '\0')
 */
STAMR3DECL(int)  STAMR3Snapshot(PVM pVM, const char *pszPat, char **ppszSnapshot, size_t *pcchSnapshot)
{
    AssertMsgFailed(("not implemented yet\n"));
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Releases a statistics snapshot returned by STAMR3Snapshot().
 *
 * @returns VBox status.
 * @param   pVM             The VM handle.
 * @param   pszSnapshot     The snapshot data pointer returned by STAMR3Snapshot().
 *                          NULL is allowed.
 */
STAMR3DECL(int)  STAMR3SnapshotFree(PVM pVM, char *pszSnapshot)
{
    if (!pszSnapshot)
        RTMemFree(pszSnapshot);
    return VINF_SUCCESS;
}


/**
 * Dumps the selected statistics to the log.
 *
 * @returns VBox status.
 * @param   pVM             The VM handle.
 * @param   pszPat          The name matching pattern. See somewhere_where_this_is_described_in_detail.
 *                          If NULL all samples are written to the log.
 */
STAMR3DECL(int)  STAMR3Dump(PVM pVM, const char *pszPat)
{
    STAMR3PRINTONEARGS Args;
    Args.pVM = pVM;
    Args.pvArg = NULL;
    Args.pfnPrintf = stamR3EnumLogPrintf;

    STAM_LOCK_RD(pVM);
    stamR3Enum(pVM, pszPat, stamR3PrintOne, &Args);
    STAM_UNLOCK_RD(pVM);
    return VINF_SUCCESS;
}


/**
 * Prints to the log.
 *
 * @param   pArgs       Pointer to the print one argument structure.
 * @param   pszFormat   Format string.
 * @param   ...         Format arguments.
 */
static DECLCALLBACK(void) stamR3EnumLogPrintf(PSTAMR3PRINTONEARGS pArgs, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTLogPrintfV(pszFormat, va);
    va_end(va);
    NOREF(pArgs);
}


/**
 * Dumps the selected statistics to the release log.
 *
 * @returns VBox status.
 * @param   pVM             The VM handle.
 * @param   pszPat          The name matching pattern. See somewhere_where_this_is_described_in_detail.
 *                          If NULL all samples are written to the log.
 */
STAMR3DECL(int)  STAMR3DumpToReleaseLog(PVM pVM, const char *pszPat)
{
    STAMR3PRINTONEARGS Args;
    Args.pVM = pVM;
    Args.pvArg = NULL;
    Args.pfnPrintf = stamR3EnumRelLogPrintf;

    STAM_LOCK_RD(pVM);
    stamR3Enum(pVM, pszPat, stamR3PrintOne, &Args);
    STAM_UNLOCK_RD(pVM);

    return VINF_SUCCESS;
}


/**
 * Prints to the release log.
 *
 * @param   pArgs       Pointer to the print one argument structure.
 * @param   pszFormat   Format string.
 * @param   ...         Format arguments.
 */
static DECLCALLBACK(void) stamR3EnumRelLogPrintf(PSTAMR3PRINTONEARGS pArgs, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTLogRelPrintfV(pszFormat, va);
    va_end(va);
    NOREF(pArgs);
}


/**
 * Prints the selected statistics to standard out.
 *
 * @returns VBox status.
 * @param   pVM             The VM handle.
 * @param   pszPat          The name matching pattern. See somewhere_where_this_is_described_in_detail.
 *                          If NULL all samples are reset.
 */
STAMR3DECL(int)  STAMR3Print(PVM pVM, const char *pszPat)
{
    STAMR3PRINTONEARGS Args;
    Args.pVM = pVM;
    Args.pvArg = NULL;
    Args.pfnPrintf = stamR3EnumPrintf;

    STAM_LOCK_RD(pVM);
    stamR3Enum(pVM, pszPat, stamR3PrintOne, &Args);
    STAM_UNLOCK_RD(pVM);
    return VINF_SUCCESS;
}


/**
 * Prints to stdout.
 *
 * @param   pArgs       Pointer to the print one argument structure.
 * @param   pszFormat   Format string.
 * @param   ...         Format arguments.
 */
static DECLCALLBACK(void) stamR3EnumPrintf(PSTAMR3PRINTONEARGS pArgs, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTPrintfV(pszFormat, va);
    va_end(va);
    NOREF(pArgs);
}


/**
 * Prints one sample.
 * Callback for stamR3Enum().
 *
 * @returns VINF_SUCCESS
 * @param   pDesc   Pointer to the current descriptor.
 * @param   pvArg   User argument - STAMR3PRINTONEARGS.
 */
static int stamR3PrintOne(PSTAMDESC pDesc, void *pvArg)
{
    PSTAMR3PRINTONEARGS pArgs = (PSTAMR3PRINTONEARGS)pvArg;

    switch (pDesc->enmType)
    {
        case STAMTYPE_COUNTER:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && pDesc->u.pCounter->c == 0)
                return VINF_SUCCESS;

            pArgs->pfnPrintf(pArgs, "%-32s %8llu %s\n", pDesc->pszName, pDesc->u.pCounter->c, STAMR3GetUnit(pDesc->enmUnit));
            break;
        case STAMTYPE_PROFILE:
        case STAMTYPE_PROFILE_ADV:
        {
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && pDesc->u.pProfile->cPeriods == 0)
                return VINF_SUCCESS;

            uint64_t u64 = pDesc->u.pProfile->cPeriods ? pDesc->u.pProfile->cPeriods : 1;
            pArgs->pfnPrintf(pArgs, "%-32s %8llu %s (%12llu ticks, %7llu times, max %9llu, min %7lld)\n", pDesc->pszName,
                             pDesc->u.pProfile->cTicks / u64, STAMR3GetUnit(pDesc->enmUnit),
                             pDesc->u.pProfile->cTicks, pDesc->u.pProfile->cPeriods, pDesc->u.pProfile->cTicksMax, pDesc->u.pProfile->cTicksMin);
            break;
        }

        case STAMTYPE_RATIO_U32:
        case STAMTYPE_RATIO_U32_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && !pDesc->u.pRatioU32->u32A && !pDesc->u.pRatioU32->u32B)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-32s %8u:%-8u %s\n", pDesc->pszName,
                             pDesc->u.pRatioU32->u32A, pDesc->u.pRatioU32->u32B, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_CALLBACK:
        {
            char szBuf[512];
            pDesc->u.Callback.pfnPrint(pArgs->pVM, pDesc->u.Callback.pvSample, szBuf, sizeof(szBuf));
            pArgs->pfnPrintf(pArgs, "%-32s %s %s\n", pDesc->pszName, szBuf, STAMR3GetUnit(pDesc->enmUnit));
            break;
        }

        case STAMTYPE_U8:
        case STAMTYPE_U8_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu8 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-32s %8u %s\n", pDesc->pszName, *pDesc->u.pu8, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_X8:
        case STAMTYPE_X8_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu8 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-32s %8x %s\n", pDesc->pszName, *pDesc->u.pu8, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_U16:
        case STAMTYPE_U16_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu16 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-32s %8u %s\n", pDesc->pszName, *pDesc->u.pu16, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_X16:
        case STAMTYPE_X16_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu16 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-32s %8x %s\n", pDesc->pszName, *pDesc->u.pu16, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_U32:
        case STAMTYPE_U32_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu32 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-32s %8u %s\n", pDesc->pszName, *pDesc->u.pu32, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_X32:
        case STAMTYPE_X32_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu32 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-32s %8x %s\n", pDesc->pszName, *pDesc->u.pu32, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_U64:
        case STAMTYPE_U64_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu64 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-32s %8llu %s\n", pDesc->pszName, *pDesc->u.pu64, STAMR3GetUnit(pDesc->enmUnit));
            break;

        case STAMTYPE_X64:
        case STAMTYPE_X64_RESET:
            if (pDesc->enmVisibility == STAMVISIBILITY_USED && *pDesc->u.pu64 == 0)
                return VINF_SUCCESS;
            pArgs->pfnPrintf(pArgs, "%-32s %8llx %s\n", pDesc->pszName, *pDesc->u.pu64, STAMR3GetUnit(pDesc->enmUnit));
            break;

        default:
            AssertMsgFailed(("enmType=%d\n", pDesc->enmType));
            break;
    }
    NOREF(pvArg);
    return VINF_SUCCESS;
}


/**
 * Enumerate the statistics by the means of a callback function.
 *
 * @returns Whatever the callback returns.
 *
 * @param   pVM         The VM handle.
 * @param   pszPat      The pattern to match samples.
 * @param   pfnEnum     The callback function.
 * @param   pvUser      The pvUser argument of the callback function.
 */
STAMR3DECL(int) STAMR3Enum(PVM pVM, const char *pszPat, PFNSTAMR3ENUM pfnEnum, void *pvUser)
{
    STAMR3ENUMONEARGS Args;
    Args.pVM     = pVM;
    Args.pfnEnum = pfnEnum;
    Args.pvUser  = pvUser;

    STAM_LOCK_RD(pVM);
    int rc = stamR3Enum(pVM, pszPat, stamR3EnumOne, &Args);
    STAM_UNLOCK_RD(pVM);
    return rc;
}


/**
 * Callback function for STARTR3Enum().
 *
 * @returns whatever the callback returns.
 * @param   pDesc       Pointer to the current descriptor.
 * @param   pvArg       Points to a STAMR3ENUMONEARGS structure.
 */
static int stamR3EnumOne(PSTAMDESC pDesc, void *pvArg)
{
    PSTAMR3ENUMONEARGS pArgs = (PSTAMR3ENUMONEARGS)pvArg;
    int rc;
    if (pDesc->enmType == STAMTYPE_CALLBACK)
    {
        /* Give the enumerator something useful. */
        char szBuf[512];
        pDesc->u.Callback.pfnPrint(pArgs->pVM, pDesc->u.Callback.pvSample, szBuf, sizeof(szBuf));
        rc = pArgs->pfnEnum(pDesc->pszName, pDesc->enmType, szBuf, pDesc->enmUnit,
                            pDesc->enmVisibility, pDesc->pszDesc, pArgs->pvUser);
    }
    else
        rc = pArgs->pfnEnum(pDesc->pszName, pDesc->enmType, pDesc->u.pv, pDesc->enmUnit,
                            pDesc->enmVisibility, pDesc->pszDesc, pArgs->pvUser);
    return rc;
}


/**
 * Matches a sample name against a pattern.
 *
 * @returns True if matches, false if not.
 * @param   pszPat      Pattern.
 * @param   pszName     Name to match against the pattern.
 */
static bool stamr3Match(const char *pszPat, const char *pszName)
{
    if (!pszPat)
        return true;

    /* ASSUMES ASCII */
    for (;;)
    {
        char chPat = *pszPat;
        switch (chPat)
        {
            case '\0':
                return !*pszName;

            case '*':
            {
                while ((chPat = *++pszPat) == '*' || chPat == '?')
                    /* nothing */;

                for (;;)
                {
                    char ch = *pszName++;
                    if (    ch == chPat
                        &&  (   !chPat
                             || stamr3Match(pszPat + 1, pszName)))
                        return true;
                    if (!ch)
                        return false;
                }
                /* won't ever get here */
                break;
            }

            case '?':
                if (!*pszName)
                    return false;
                break;

            default:
                if (*pszName != chPat)
                    return false;
                break;
        }
        pszName++;
        pszPat++;
    }
    return true;
}


/**
 * Enumerates the nodes selected by a pattern or all nodes if no pattern
 * is specified.
 *
 * The call must own at least a read lock to the STAM data.
 *
 * @returns The rc from the callback.
 * @param   pVM         VM handle
 * @param   pszPat      Pattern.
 * @param   pfnCallback Callback function which shall be called for matching nodes.
 *                      If it returns anything but VINF_SUCCESS the enumeration is
 *                      terminated and the status code returned to the caller.
 * @param   pvArg       User parameter for the callback.
 */
static int stamR3Enum(PVM pVM, const char *pszPat, int (*pfnCallback)(PSTAMDESC pDesc, void *pvArg), void *pvArg)
{
    /*
     * Search for it.
     */
    int         rc = VINF_SUCCESS;
    PSTAMDESC   pCur = pVM->stam.s.pHead;
    while (pCur)
    {
        if (stamr3Match(pszPat, pCur->pszName))
        {
            rc = pfnCallback(pCur, pvArg);
            if (rc)
                break;
        }

        /* next */
        pCur = pCur->pNext;
    }

    return rc;
}


/**
 * Get the unit string.
 *
 * @returns Pointer to read only unit string.
 * @param   enmUnit     The unit.
 */
STAMR3DECL(const char *) STAMR3GetUnit(STAMUNIT enmUnit)
{
    switch (enmUnit)
    {
        case STAMUNIT_NONE:                 return "";
        case STAMUNIT_CALLS:                return "calls";
        case STAMUNIT_COUNT:                return "count";
        case STAMUNIT_BYTES:                return "bytes";
        case STAMUNIT_PAGES:                return "pages";
        case STAMUNIT_ERRORS:               return "errors";
        case STAMUNIT_OCCURENCES:           return "times";
        case STAMUNIT_TICKS_PER_CALL:       return "ticks/call";
        case STAMUNIT_TICKS_PER_OCCURENCE:  return "ticks/time";
        case STAMUNIT_GOOD_BAD:             return "good:bad";
        case STAMUNIT_MEGABYTES:            return "megabytes";
        case STAMUNIT_KILOBYTES:            return "kilobytes";
        case STAMUNIT_NS:                   return "ns";
        case STAMUNIT_NS_PER_CALL:          return "ns/call";
        case STAMUNIT_NS_PER_OCCURENCE:     return "ns/time";
        case STAMUNIT_PCT:                  return "%";

        default:
            AssertMsgFailed(("Unknown unit %d\n", enmUnit));
            return "(?unit?)";
    }
}


#ifdef VBOX_WITH_DEBUGGER
/**
 * The '.stats' command.
 *
 * @returns VBox status.
 * @param   pCmd        Pointer to the command descriptor (as registered).
 * @param   pCmdHlp     Pointer to command helper functions.
 * @param   pVM         Pointer to the current VM (if any).
 * @param   paArgs      Pointer to (readonly) array of arguments.
 * @param   cArgs       Number of arguments in the array.
 */
static DECLCALLBACK(int) stamR3CmdStats(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PVM pVM, PCDBGCVAR paArgs, unsigned cArgs, PDBGCVAR pResult)
{
    /*
     * Validate input.
     */
    if (!pVM)
        return pCmdHlp->pfnPrintf(pCmdHlp, NULL, "error: The command requires VM to be selected.\n");
    if (!pVM->stam.s.pHead)
        return pCmdHlp->pfnPrintf(pCmdHlp, NULL, "Sorry, no statistics present.\n");

    /*
     * Do the printing.
     */
    STAMR3PRINTONEARGS Args;
    Args.pVM = pVM;
    Args.pvArg = pCmdHlp;
    Args.pfnPrintf = stamR3EnumDbgfPrintf;

    STAM_LOCK_RD(pVM);
    int rc = stamR3Enum(pVM, cArgs ? paArgs[0].u.pszString : NULL, stamR3PrintOne, &Args);
    STAM_UNLOCK_RD(pVM);

    return rc;
}


/**
 * Display one sample in the debugger.
 *
 * @param   pArgs       Pointer to the print one argument structure.
 * @param   pszFormat   Format string.
 * @param   ...         Format arguments.
 */
static DECLCALLBACK(void) stamR3EnumDbgfPrintf(PSTAMR3PRINTONEARGS pArgs, const char *pszFormat, ...)
{
    PDBGCCMDHLP pCmdHlp = (PDBGCCMDHLP)pArgs->pvArg;

    va_list va;
    va_start(va, pszFormat);
    pCmdHlp->pfnPrintfV(pCmdHlp, NULL, pszFormat, va);
    va_end(va);
    NOREF(pArgs);
}


/**
 * The '.statsreset' command.
 *
 * @returns VBox status.
 * @param   pCmd        Pointer to the command descriptor (as registered).
 * @param   pCmdHlp     Pointer to command helper functions.
 * @param   pVM         Pointer to the current VM (if any).
 * @param   paArgs      Pointer to (readonly) array of arguments.
 * @param   cArgs       Number of arguments in the array.
 */
static DECLCALLBACK(int) stamR3CmdStatsReset(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PVM pVM, PCDBGCVAR paArgs, unsigned cArgs, PDBGCVAR pResult)
{
    /*
     * Validate input.
     */
    if (!pVM)
        return pCmdHlp->pfnPrintf(pCmdHlp, NULL, "error: The command requires VM to be selected.\n");
    if (!pVM->stam.s.pHead)
        return pCmdHlp->pfnPrintf(pCmdHlp, NULL, "Sorry, no statistics present.\n");

    /*
     * Execute reset.
     */
    int rc = STAMR3Reset(pVM, cArgs ? paArgs[0].u.pszString : NULL);
    if (VBOX_SUCCESS(rc))
        return pCmdHlp->pfnPrintf(pCmdHlp, NULL, "info: Statistics reset.\n");

    return pCmdHlp->pfnVBoxError(pCmdHlp, rc, "Restting statistics.\n");
}
#endif

