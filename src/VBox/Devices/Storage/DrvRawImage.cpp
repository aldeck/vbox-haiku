/** @file
 *
 * VBox storage devices:
 * Raw image driver
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_RAW_IMAGE
#include <VBox/pdmdrv.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/string.h>

#include "Builtins.h"


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/

/** Converts a pointer to RAWIMAGE::IMedia to a PRDVRAWIMAGE. */
#define PDMIMEDIA_2_DRVRAWIMAGE(pInterface) ( (PDRVRAWIMAGE)((uintptr_t)pInterface - RT_OFFSETOF(DRVRAWIMAGE, IMedia)) )

/** Converts a pointer to PDMDRVINS::IBase to a PPDMDRVINS. */
#define PDMIBASE_2_DRVINS(pInterface)   ( (PPDMDRVINS)((uintptr_t)pInterface - RT_OFFSETOF(PDMDRVINS, IBase)) )

/** Converts a pointer to PDMDRVINS::IBase to a PVBOXHDD. */
#define PDMIBASE_2_DRVRAWIMAGE(pInterface)  ( PDMINS2DATA(PDMIBASE_2_DRVINS(pInterface), PDRVRAWIMAGE) )



/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Block driver instance data.
 */
typedef struct DRVRAWIMAGE
{
    /** The media interface. */
    PDMIMEDIA       IMedia;
    /** Pointer to the driver instance. */
    PPDMDRVINS      pDrvIns;
    /** Pointer to the filename. (Freed by MM) */
    char           *pszFilename;
    /** File handle of the raw image file. */
    RTFILE          File;
} DRVRAWIMAGE, *PDRVRAWIMAGE;



/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static DECLCALLBACK(int) drvRawImageRead(PPDMIMEDIA pInterface, uint64_t off, void *pvBuf, size_t cbRead);
static DECLCALLBACK(int) drvRawImageWrite(PPDMIMEDIA pInterface, uint64_t off, const void *pvBuf, size_t cbWrite);
static DECLCALLBACK(int) drvRawImageFlush(PPDMIMEDIA pInterface);
static DECLCALLBACK(bool) drvRawImageIsReadOnly(PPDMIMEDIA pInterface);
static DECLCALLBACK(uint64_t) drvRawImageGetSize(PPDMIMEDIA pInterface);
static DECLCALLBACK(int) drvRawImageGetUuid(PPDMIMEDIA pInterface, PRTUUID pUuid);
static DECLCALLBACK(int) drvRawImageBiosGetGeometry(PPDMIMEDIA pInterface, uint32_t *pcCylinders, uint32_t *pcHeads, uint32_t *pcSectors);
static DECLCALLBACK(int) drvRawImageBiosSetGeometry(PPDMIMEDIA pInterface, uint32_t cCylinders, uint32_t cHeads, uint32_t cSectors);
static DECLCALLBACK(int) drvRawImageBiosGetTranslation(PPDMIMEDIA pInterface, PPDMBIOSTRANSLATION penmTranslation);
static DECLCALLBACK(int) drvRawImageBiosSetTranslation(PPDMIMEDIA pInterface, PDMBIOSTRANSLATION enmTranslation);

static DECLCALLBACK(void *) drvRawImageQueryInterface(PPDMIBASE pInterface, PDMINTERFACE enmInterface);




/**
 * Construct a raw image driver instance.
 *
 * @returns VBox status.
 * @param   pDrvIns     The driver instance data.
 *                      If the registration structure is needed, pDrvIns->pDrvReg points to it.
 * @param   pCfgHandle  Configuration node handle for the driver. Use this to obtain the configuration
 *                      of the driver instance. It's also found in pDrvIns->pCfgHandle, but like
 *                      iInstance it's expected to be used a bit in this function.
 */
static DECLCALLBACK(int) drvRawImageConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfgHandle)
{
    PDRVRAWIMAGE pData = PDMINS2DATA(pDrvIns, PDRVRAWIMAGE);

    /*
     * Init the static parts.
     */
    pData->pDrvIns                      = pDrvIns;
    pData->File                         = NIL_RTFILE;
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface    = drvRawImageQueryInterface;
    /* IMedia */
    pData->IMedia.pfnRead               = drvRawImageRead;
    pData->IMedia.pfnWrite              = drvRawImageWrite;
    pData->IMedia.pfnFlush              = drvRawImageFlush;
    pData->IMedia.pfnGetSize            = drvRawImageGetSize;
    pData->IMedia.pfnGetUuid            = drvRawImageGetUuid;
    pData->IMedia.pfnIsReadOnly         = drvRawImageIsReadOnly;
    pData->IMedia.pfnBiosGetGeometry    = drvRawImageBiosGetGeometry;
    pData->IMedia.pfnBiosSetGeometry    = drvRawImageBiosSetGeometry;
    pData->IMedia.pfnBiosGetTranslation = drvRawImageBiosGetTranslation;
    pData->IMedia.pfnBiosSetTranslation = drvRawImageBiosSetTranslation;

    /*
     * Read the configuration.
     */
    if (!CFGMR3AreValuesValid(pCfgHandle, "Path\0"))
        return VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES;

    char *pszName;
    int rc = CFGMR3QueryStringAlloc(pCfgHandle, "Path", &pszName);
    if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: query for \"Path\" string return %Vrc.\n", rc));
        return rc;
    }

    /*
     * Open the image.
     */
    rc = RTFileOpen(&pData->File, pszName,
                    RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (VBOX_SUCCESS(rc))
    {
        LogFlow(("drvRawImageConstruct: Raw image '%s' opened successfully.\n", pszName));
        pData->pszFilename = pszName;
    }
    else
    {
        AssertMsgFailed(("Could not open Raw image file %s, rc=%Vrc\n", pszName, rc));
        MMR3HeapFree(pszName);
    }

    return rc;
}


/**
 * Destruct a driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvRawImageDestruct(PPDMDRVINS pDrvIns)
{
    PDRVRAWIMAGE pData = PDMINS2DATA(pDrvIns, PDRVRAWIMAGE);
    LogFlow(("drvRawImageDestruct: '%s'\n", pData->pszFilename));

    if (pData->File != NIL_RTFILE)
    {
        RTFileClose(pData->File);
        pData->File = NIL_RTFILE;
    }
    if (pData->pszFilename)
        MMR3HeapFree(pData->pszFilename);
}


/** @copydoc PDMIMEDIA::pfnGetSize */
static DECLCALLBACK(uint64_t) drvRawImageGetSize(PPDMIMEDIA pInterface)
{
    PDRVRAWIMAGE pData = PDMIMEDIA_2_DRVRAWIMAGE(pInterface);
    LogFlow(("drvRawImageGetSize: '%s'\n", pData->pszFilename));

    uint64_t cbFile;
    int rc = RTFileGetSize(pData->File, &cbFile);
    if (VBOX_SUCCESS(rc))
    {
        LogFlow(("drvRawImageGetSize: returns %lld (%s)\n", cbFile, pData->pszFilename));
        return cbFile;
    }

    AssertMsgFailed(("Error querying Raw image file size, rc=%Vrc. (%s)\n", rc, pData->pszFilename));
    return 0;
}


/** @copydoc PDMIMEDIA::pfnBiosGetGeometry */
static DECLCALLBACK(int) drvRawImageBiosGetGeometry(PPDMIMEDIA pInterface, uint32_t *pcCylinders, uint32_t *pcHeads, uint32_t *pcSectors)
{
    return VERR_NOT_IMPLEMENTED;
}


/** @copydoc PDMIMEDIA::pfnBiosSetGeometry */
static DECLCALLBACK(int) drvRawImageBiosSetGeometry(PPDMIMEDIA pInterface, uint32_t cCylinders, uint32_t cHeads, uint32_t cSectors)
{
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Read bits.
 *
 * @see PDMIMEDIA::pfnRead for details.
 */
static DECLCALLBACK(int) drvRawImageRead(PPDMIMEDIA pInterface, uint64_t off, void *pvBuf, size_t cbRead)
{
    PDRVRAWIMAGE pData = PDMIMEDIA_2_DRVRAWIMAGE(pInterface);
    LogFlow(("drvRawImageRead: off=%#llx pvBuf=%p cbRead=%#x (%s)\n", off, pvBuf, cbRead, pData->pszFilename));

    Assert(pData->File);
    Assert(pvBuf);

    /*
     * Seek to the position and read.
     */
    int rc = RTFileSeek(pData->File, off, RTFILE_SEEK_BEGIN, NULL);
    if (VBOX_SUCCESS(rc))
    {
        rc = RTFileRead(pData->File, pvBuf, cbRead, NULL);
        if (VBOX_SUCCESS(rc))
        {
            Log2(("drvRawImageRead: off=%#llx pvBuf=%p cbRead=%#x (%s)\n"
                  "%16.*Vhxd\n",
                  off, pvBuf, cbRead, pData->pszFilename,
                  cbRead, pvBuf));
        }
        else
            AssertMsgFailed(("RTFileRead(%d, %p, %#x) -> %Vrc (off=%#llx '%s')\n",
                             pData->File, pvBuf, cbRead, rc, off, pData->pszFilename));
    }
    else
        AssertMsgFailed(("RTFileSeek(%d,%#llx,) -> %Vrc\n", pData->File, off, rc));
    LogFlow(("drvRawImageRead: returns %Vrc\n", rc));
    return rc;
}


/** @copydoc PDMIMEDIA::pfnWrite */
static DECLCALLBACK(int) drvRawImageWrite(PPDMIMEDIA pInterface, uint64_t off, const void *pvBuf, size_t cbWrite)
{
    PDRVRAWIMAGE pData = PDMIMEDIA_2_DRVRAWIMAGE(pInterface);
    LogFlow(("drvRawImageWrite: off=%#llx pvBuf=%p cbWrite=%#x (%s)\n", off, pvBuf, cbWrite, pData->pszFilename));

    Assert(pData->File);
    Assert(pvBuf);

    /*
     * Seek to the position and write.
     */
    int rc = RTFileSeek(pData->File, off, RTFILE_SEEK_BEGIN, NULL);
    if (VBOX_SUCCESS(rc))
    {
        rc = RTFileWrite(pData->File, pvBuf, cbWrite, NULL);
        if (VBOX_SUCCESS(rc))
        {
            Log2(("drvRawImageWrite: off=%#llx pvBuf=%p cbWrite=%#x (%s)\n"
                  "%16.*Vhxd\n",
                  off, pvBuf, cbWrite, pData->pszFilename,
                  cbWrite, pvBuf));
        }
        else
            AssertMsgFailed(("RTFileWrite(%d, %p, %#x) -> %Vrc (off=%#llx '%s')\n",
                             pData->File, pvBuf, cbWrite, rc, off, pData->pszFilename));
    }
    else
        AssertMsgFailed(("RTFileSeek(%d,%#llx,) -> %Vrc\n", pData->File, off, rc));
    LogFlow(("drvRawImageWrite: returns %Vrc\n", rc));
    return rc;
}


/** @copydoc PDMIMEDIA::pfnFlush */
static DECLCALLBACK(int) drvRawImageFlush(PPDMIMEDIA pInterface)
{
    PDRVRAWIMAGE pData = PDMIMEDIA_2_DRVRAWIMAGE(pInterface);
    LogFlow(("drvRawImageFlush: (%s)\n", pData->pszFilename));

    Assert(pData->File != NIL_RTFILE);
    int rc = RTFileFlush(pData->File);
    LogFlow(("drvRawImageFlush: returns %Vrc\n", rc));
    return rc;
}


/** @copydoc PDMIMEDIA::pfnGetUuid */
static DECLCALLBACK(int) drvRawImageGetUuid(PPDMIMEDIA pInterface, PRTUUID pUuid)
{
    LogFlow(("drvRawImageGetUuid: returns VERR_NOT_IMPLEMENTED\n"));
    return VERR_NOT_IMPLEMENTED;
}


/** @copydoc PDMIMEDIA::pfnIsReadOnly */
static DECLCALLBACK(bool) drvRawImageIsReadOnly(PPDMIMEDIA pInterface)
{
    return false;
}


/**
 * Stub - operation not supported.
 *
 * @copydoc PDMIMEDIA::pfnBiosGetTranslation
 */
static DECLCALLBACK(int) drvRawImageBiosGetTranslation(PPDMIMEDIA pInterface, PPDMBIOSTRANSLATION penmTranslation)
{
    NOREF(pInterface); NOREF(penmTranslation);
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Stub - operation not supported.
 *
 * @copydoc PDMIMEDIA::pfnBiosSetTranslation
 */
static DECLCALLBACK(int) drvRawImageBiosSetTranslation(PPDMIMEDIA pInterface, PDMBIOSTRANSLATION enmTranslation)
{
    NOREF(pInterface); NOREF(enmTranslation);
    return VERR_NOT_IMPLEMENTED;
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
static DECLCALLBACK(void *) drvRawImageQueryInterface(PPDMIBASE pInterface, PDMINTERFACE enmInterface)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_DRVINS(pInterface);
    PDRVRAWIMAGE pData = PDMINS2DATA(pDrvIns, PDRVRAWIMAGE);
    switch (enmInterface)
    {
        case PDMINTERFACE_BASE:
            return &pDrvIns->IBase;
        case PDMINTERFACE_MEDIA:
            return &pData->IMedia;
        default:
            return NULL;
    }
}


/**
 * Raw image driver registration record.
 */
const PDMDRVREG g_DrvRawImage =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szDriverName */
    "RawImage",
    /* pszDescription */
    "Raw image access driver.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_MEDIA,
    /* cMaxInstances */
    ~0,
    /* cbInstance */
    sizeof(DRVRAWIMAGE),
    /* pfnConstruct */
    drvRawImageConstruct,
    /* pfnDestruct */
    drvRawImageDestruct,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL
};
