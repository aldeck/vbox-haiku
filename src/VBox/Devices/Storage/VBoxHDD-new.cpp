/** $Id$ */
/** @file
 * VBox HDD Container implementation.
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_VD
#include <VBox/VBoxHDD-new.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include <iprt/ldr.h>
#include <iprt/dir.h>
#include <iprt/path.h>
#include <iprt/param.h>

#include "VBoxHDD-newInternal.h"


#define VBOXHDDDISK_SIGNATURE 0x6f0e2a7d

/** Buffer size used for merging images. */
#define VD_MERGE_BUFFER_SIZE    (1024 * 1024)

/**
 * VBox HDD Container image descriptor.
 */
typedef struct VDIMAGE
{
    /** Link to parent image descriptor, if any. */
    struct VDIMAGE  *pPrev;
    /** Link to child image descriptor, if any. */
    struct VDIMAGE  *pNext;
    /** Container base filename. (UTF-8) */
    char            *pszFilename;
    /** Data managed by the backend which keeps the actual info. */
    void            *pvBackendData;
    /** Cached sanitized image type. */
    VDIMAGETYPE     enmImageType;
    /** Image open flags (only those handled generically in this code and which
     * the backends will never ever see). */
    unsigned        uOpenFlags;

    /** Handle for the shared object / DLL. */
    RTLDRMOD            hPlugin;
    /** Function pointers for the various backend methods. */
    PCVBOXHDDBACKEND    Backend;

    /** Pointer to list of VD interfaces, per-image. */
    PVDINTERFACE        pVDIfsImage;
} VDIMAGE, *PVDIMAGE;

/**
 * uModified bit flags.
 */
#define VD_IMAGE_MODIFIED_FLAG                  RT_BIT(0)
#define VD_IMAGE_MODIFIED_FIRST                 RT_BIT(1)
#define VD_IMAGE_MODIFIED_DISABLE_UUID_UPDATE   RT_BIT(2)


/**
 * VBox HDD Container main structure, private part.
 */
struct VBOXHDD
{
    /** Structure signature (VBOXHDDDISK_SIGNATURE). */
    uint32_t            u32Signature;

    /** Number of opened images. */
    unsigned            cImages;

    /** Base image. */
    PVDIMAGE            pBase;

    /** Last opened image in the chain.
     * The same as pBase if only one image is used. */
    PVDIMAGE            pLast;

    /** Flags representing the modification state. */
    unsigned            uModified;

    /** Cached size of this disk. */
    uint64_t            cbSize;
    /** Cached PCHS geometry for this disk. */
    PDMMEDIAGEOMETRY    PCHSGeometry;
    /** Cached LCHS geometry for this disk. */
    PDMMEDIAGEOMETRY    LCHSGeometry;

    /** Pointer to list of VD interfaces, per-disk. */
    PVDINTERFACE        pVDIfsDisk;
    /** Pointer to the common interface structure for error reporting. */
    PVDINTERFACE        pInterfaceError;
    /** Pointer to the error interface we use if available. */
    PVDINTERFACEERROR   pInterfaceErrorCallbacks;
};


extern VBOXHDDBACKEND g_RawBackend;
extern VBOXHDDBACKEND g_VmdkBackend;
extern VBOXHDDBACKEND g_VDIBackend;
extern VBOXHDDBACKEND g_VhdBackend;
#ifdef VBOX_WITH_ISCSI
extern VBOXHDDBACKEND g_ISCSIBackend;
#endif

static PCVBOXHDDBACKEND aBackends[] =
{
    &g_RawBackend,
    &g_VmdkBackend,
    &g_VDIBackend,
    &g_VhdBackend,
#ifdef VBOX_WITH_ISCSI
    &g_ISCSIBackend,
#endif
    NULL
};


/**
 * internal: issue error message.
 */
static int vdError(PVBOXHDD pDisk, int rc, RT_SRC_POS_DECL,
                   const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    if (pDisk->pInterfaceErrorCallbacks)
        pDisk->pInterfaceErrorCallbacks->pfnError(pDisk->pInterfaceError->pvUser, rc, RT_SRC_POS_ARGS, pszFormat, va);
    va_end(va);
    return rc;
}

/**
 * internal: find image format backend.
 */
static int vdFindBackend(const char *pszBackend, PCVBOXHDDBACKEND *ppBackend,
                         RTLDRMOD *phPlugin)
{
    int rc = VINF_SUCCESS;
    PCVBOXHDDBACKEND pBackend = NULL;
    RTLDRMOD hPlugin = NIL_RTLDRMOD;

    for (unsigned i = 0; aBackends[i] != NULL; i++)
    {
        if (!strcmp(pszBackend, aBackends[i]->pszBackendName))
        {
            pBackend = aBackends[i];
            break;
        }
    }

    /* If no static backend is found try loading a shared module with
     * pszBackend as filename. */
    if (!pBackend)
    {
        char szSharedLibPath[RTPATH_MAX];
        char *pszPluginName;

        rc = RTPathSharedLibs(szSharedLibPath, sizeof(szSharedLibPath));
        if (RT_FAILURE(rc))
            return rc;

        /* HDD Format Plugins have VBoxHDD as prefix, prepend it. */
        RTStrAPrintf(&pszPluginName, "%s/%s%s",
                     szSharedLibPath, VBOX_HDDFORMAT_PLUGIN_PREFIX, pszBackend);
        if (!pszPluginName)
        {
            rc = VERR_NO_MEMORY;
            goto out;
        }

        /* Try to load the plugin (RTLdrLoad takes care of the suffix). */
        rc = RTLdrLoad(pszPluginName, &hPlugin);
        if (RT_SUCCESS(rc))
        {
            PFNVBOXHDDFORMATLOAD pfnHDDFormatLoad;

            rc = RTLdrGetSymbol(hPlugin, VBOX_HDDFORMAT_LOAD_NAME,
                                (void**)&pfnHDDFormatLoad);
            if (RT_FAILURE(rc) || !pfnHDDFormatLoad)
            {
                LogFunc(("error resolving the entry point %s in plugin %s, rc=%Rrc, pfnHDDFormat=%#p\n", VBOX_HDDFORMAT_LOAD_NAME, pszPluginName, rc, pfnHDDFormatLoad));
                if (RT_SUCCESS(rc))
                    rc = VERR_SYMBOL_NOT_FOUND;
                goto out;
            }

            /* Get the function table. */
            PVBOXHDDBACKEND pBE;
            rc = pfnHDDFormatLoad(&pBE);
            if (RT_FAILURE(rc))
                goto out;
            /* Check if the sizes match. If not this plugin is too old. */
            if (pBE->cbSize != sizeof(VBOXHDDBACKEND))
            {
                rc = VERR_VDI_UNSUPPORTED_VERSION;
                goto out;
            }
            pBackend = pBE;
        }
        else
        {
            /* If the backend plugin doesn't exist, don't treat this as an
             * error. Just return the NULL pointers. */
            rc = VINF_SUCCESS;
        }

        RTStrFree(pszPluginName);
    }

out:
    if (RT_FAILURE(rc))
    {
        if (hPlugin != NIL_RTLDRMOD)
            RTLdrClose(hPlugin);
        hPlugin = NIL_RTLDRMOD;
        pBackend = NULL;
    }

    *ppBackend = pBackend;
    *phPlugin = hPlugin;
    return rc;
}

/**
 * internal: add image structure to the end of images list.
 */
static void vdAddImageToList(PVBOXHDD pDisk, PVDIMAGE pImage)
{
    pImage->pPrev = NULL;
    pImage->pNext = NULL;

    if (pDisk->pBase)
    {
        Assert(pDisk->cImages > 0);
        pImage->pPrev = pDisk->pLast;
        pDisk->pLast->pNext = pImage;
        pDisk->pLast = pImage;
    }
    else
    {
        Assert(pDisk->cImages == 0);
        pDisk->pBase = pImage;
        pDisk->pLast = pImage;
    }

    pDisk->cImages++;
}

/**
 * internal: remove image structure from the images list.
 */
static void vdRemoveImageFromList(PVBOXHDD pDisk, PVDIMAGE pImage)
{
    Assert(pDisk->cImages > 0);

    if (pImage->pPrev)
        pImage->pPrev->pNext = pImage->pNext;
    else
        pDisk->pBase = pImage->pNext;

    if (pImage->pNext)
        pImage->pNext->pPrev = pImage->pPrev;
    else
        pDisk->pLast = pImage->pPrev;

    pImage->pPrev = NULL;
    pImage->pNext = NULL;

    pDisk->cImages--;
}

/**
 * internal: find image by index into the images list.
 */
static PVDIMAGE vdGetImageByNumber(PVBOXHDD pDisk, unsigned nImage)
{
    PVDIMAGE pImage = pDisk->pBase;
    if (nImage == VD_LAST_IMAGE)
        return pDisk->pLast;
    while (pImage && nImage)
    {
        pImage = pImage->pNext;
        nImage--;
    }
    return pImage;
}

/**
 * internal: read the specified amount of data in whatever blocks the backend
 * will give us.
 */
static int vdReadHelper(PVBOXHDD pDisk, PVDIMAGE pImage, uint64_t uOffset,
                        void *pvBuf, size_t cbRead)
{
    int rc;
    size_t cbThisRead;

    /* Loop until all read. */
    do
    {
        /* Search for image with allocated block. Do not attempt to read more
         * than the previous reads marked as valid. Otherwise this would return
         * stale data when different block sizes are used for the images. */
        cbThisRead = cbRead;
        rc = VERR_VDI_BLOCK_FREE;
        for (PVDIMAGE pCurrImage = pImage;
             pCurrImage != NULL && rc == VERR_VDI_BLOCK_FREE;
             pCurrImage = pCurrImage->pPrev)
        {
            rc = pCurrImage->Backend->pfnRead(pCurrImage->pvBackendData,
                                              uOffset, pvBuf, cbThisRead,
                                              &cbThisRead);
        }

        /* No image in the chain contains the data for the block. */
        if (rc == VERR_VDI_BLOCK_FREE)
        {
            memset(pvBuf, '\0', cbThisRead);
            rc = VINF_SUCCESS;
        }

        cbRead -= cbThisRead;
        uOffset += cbThisRead;
        pvBuf = (char *)pvBuf + cbThisRead;
    } while (cbRead != 0 && RT_SUCCESS(rc));

    return rc;
}

/**
 * internal: mark the disk as not modified.
 */
static void vdResetModifiedFlag(PVBOXHDD pDisk)
{
    if (pDisk->uModified & VD_IMAGE_MODIFIED_FLAG)
    {
        /* generate new last-modified uuid */
        if (!(pDisk->uModified & VD_IMAGE_MODIFIED_DISABLE_UUID_UPDATE))
        {
            RTUUID Uuid;

            RTUuidCreate(&Uuid);
            pDisk->pLast->Backend->pfnSetModificationUuid(pDisk->pLast->pvBackendData,
                                                          &Uuid);
        }

        pDisk->uModified &= ~VD_IMAGE_MODIFIED_FLAG;
    }
}

/**
 * internal: mark the disk as modified.
 */
static void vdSetModifiedFlag(PVBOXHDD pDisk)
{
    pDisk->uModified |= VD_IMAGE_MODIFIED_FLAG;
    if (pDisk->uModified & VD_IMAGE_MODIFIED_FIRST)
    {
        pDisk->uModified &= ~VD_IMAGE_MODIFIED_FIRST;

        /* First modify, so create a UUID and ensure it's written to disk. */
        vdResetModifiedFlag(pDisk);

        if (!(pDisk->uModified | VD_IMAGE_MODIFIED_DISABLE_UUID_UPDATE))
            pDisk->pLast->Backend->pfnFlush(pDisk->pLast->pvBackendData);
    }
}

/**
 * internal: write a complete block (only used for diff images), taking the
 * remaining data from parent images. This implementation does not optimize
 * anything (except that it tries to read only that portions from parent
 * images that are really needed).
 */
static int vdWriteHelperStandard(PVBOXHDD pDisk, PVDIMAGE pImage,
                                 uint64_t uOffset, size_t cbWrite,
                                 size_t cbThisWrite, size_t cbPreRead,
                                 size_t cbPostRead, const void *pvBuf,
                                 void *pvTmp)
{
    int rc = VINF_SUCCESS;

    /* Read the data that goes before the write to fill the block. */
    if (cbPreRead)
    {
        rc = vdReadHelper(pDisk, pImage->pPrev, uOffset - cbPreRead, pvTmp,
                          cbPreRead);
        if (RT_FAILURE(rc))
            return rc;
    }

    /* Copy the data to the right place in the buffer. */
    memcpy((char *)pvTmp + cbPreRead, pvBuf, cbThisWrite);

    /* Read the data that goes after the write to fill the block. */
    if (cbPostRead)
    {
        /* If we have data to be written, use that instead of reading
         * data from the image. */
        size_t cbWriteCopy;
        if (cbWrite > cbThisWrite)
            cbWriteCopy = RT_MIN(cbWrite - cbThisWrite, cbPostRead);
        else
            cbWriteCopy = 0;
        /* Figure out how much we cannnot read from the image, because
         * the last block to write might exceed the nominal size of the
         * image for technical reasons. */
        size_t cbFill;
        if (uOffset + cbThisWrite + cbPostRead > pDisk->cbSize)
            cbFill = uOffset + cbThisWrite + cbPostRead - pDisk->cbSize;
        else
            cbFill = 0;
        /* The rest must be read from the image. */
        size_t cbReadImage = cbPostRead - cbWriteCopy - cbFill;

        /* Now assemble the remaining data. */
        if (cbWriteCopy)
            memcpy((char *)pvTmp + cbPreRead + cbThisWrite,
                   (char *)pvBuf + cbThisWrite, cbWriteCopy);
        if (cbReadImage)
            rc = vdReadHelper(pDisk, pImage->pPrev,
                              uOffset + cbThisWrite + cbWriteCopy,
                              (char *)pvTmp + cbPreRead + cbThisWrite + cbWriteCopy,
                              cbReadImage);
        if (RT_FAILURE(rc))
            return rc;
        /* Zero out the remainder of this block. Will never be visible, as this
         * is beyond the limit of the image. */
        if (cbFill)
            memset((char *)pvTmp + cbPreRead + cbThisWrite + cbWriteCopy + cbReadImage,
                   '\0', cbFill);
    }

    /* Write the full block to the virtual disk. */
    rc = pImage->Backend->pfnWrite(pImage->pvBackendData,
                                   uOffset - cbPreRead, pvTmp,
                                   cbPreRead + cbThisWrite + cbPostRead,
                                   NULL, &cbPreRead, &cbPostRead, 0);
    Assert(rc != VERR_VDI_BLOCK_FREE);
    Assert(cbPreRead == 0);
    Assert(cbPostRead == 0);

    return rc;
}

/**
 * internal: write a complete block (only used for diff images), taking the
 * remaining data from parent images. This implementation optimizes out writes
 * that do not change the data relative to the state as of the parent images.
 * All backends which support differential/growing images support this.
 */
static int vdWriteHelperOptimized(PVBOXHDD pDisk, PVDIMAGE pImage,
                                  uint64_t uOffset, size_t cbWrite,
                                  size_t cbThisWrite, size_t cbPreRead,
                                  size_t cbPostRead, const void *pvBuf,
                                  void *pvTmp)
{
    size_t cbFill = 0;
    size_t cbWriteCopy = 0;
    size_t cbReadImage = 0;
    int rc;

    if (cbPostRead)
    {
        /* Figure out how much we cannnot read from the image, because
         * the last block to write might exceed the nominal size of the
         * image for technical reasons. */
        if (uOffset + cbThisWrite + cbPostRead > pDisk->cbSize)
            cbFill = uOffset + cbThisWrite + cbPostRead - pDisk->cbSize;

        /* If we have data to be written, use that instead of reading
         * data from the image. */
        if (cbWrite > cbThisWrite)
            cbWriteCopy = RT_MIN(cbWrite - cbThisWrite, cbPostRead);

        /* The rest must be read from the image. */
        cbReadImage = cbPostRead - cbWriteCopy - cbFill;
    }

    /* Read the entire data of the block so that we can compare whether it will
     * be modified by the write or not. */
    rc = vdReadHelper(pDisk, pImage->pPrev, uOffset - cbPreRead, pvTmp,
                      cbPreRead + cbThisWrite + cbPostRead - cbFill);
    if (RT_FAILURE(rc))
        return rc;

    /* Check if the write would modify anything in this block. */
    if (   !memcmp((char *)pvTmp + cbPreRead, pvBuf, cbThisWrite)
        && (!cbWriteCopy || !memcmp((char *)pvTmp + cbPreRead + cbThisWrite,
                                    (char *)pvBuf + cbThisWrite, cbWriteCopy)))
    {
        /* Block is completely unchanged, so no need to write anything. */
        return VINF_SUCCESS;
    }

    /* Copy the data to the right place in the buffer. */
    memcpy((char *)pvTmp + cbPreRead, pvBuf, cbThisWrite);

    /* Handle the data that goes after the write to fill the block. */
    if (cbPostRead)
    {
        /* Now assemble the remaining data. */
        if (cbWriteCopy)
            memcpy((char *)pvTmp + cbPreRead + cbThisWrite,
                   (char *)pvBuf + cbThisWrite, cbWriteCopy);
        /* Zero out the remainder of this block. Will never be visible, as this
         * is beyond the limit of the image. */
        if (cbFill)
            memset((char *)pvTmp + cbPreRead + cbThisWrite + cbWriteCopy + cbReadImage,
                   '\0', cbFill);
    }

    /* Write the full block to the virtual disk. */
    rc = pImage->Backend->pfnWrite(pImage->pvBackendData,
                                   uOffset - cbPreRead, pvTmp,
                                   cbPreRead + cbThisWrite + cbPostRead,
                                   NULL, &cbPreRead, &cbPostRead, 0);
    Assert(rc != VERR_VDI_BLOCK_FREE);
    Assert(cbPreRead == 0);
    Assert(cbPostRead == 0);

    return rc;
}

/**
 * internal: write buffer to the image, taking care of block boundaries and
 * write optimizations.
 */
static int vdWriteHelper(PVBOXHDD pDisk, PVDIMAGE pImage, uint64_t uOffset,
                         const void *pvBuf, size_t cbWrite)
{
    int rc;
    unsigned fWrite;
    size_t cbThisWrite;
    size_t cbPreRead, cbPostRead;

    /* Loop until all written. */
    do
    {
        /* Try to write the possibly partial block to the last opened image.
         * This works when the block is already allocated in this image or
         * if it is a full-block write (and allocation isn't suppressed below).
         * For image formats which don't support zero blocks, it's beneficial
         * to avoid unnecessarily allocating unchanged blocks. This prevents
         * unwanted expanding of images. VMDK is an example. */
        cbThisWrite = cbWrite;
        fWrite =   (pImage->uOpenFlags & VD_OPEN_FLAGS_HONOR_SAME)
                 ? 0 : VD_WRITE_NO_ALLOC;
        rc = pImage->Backend->pfnWrite(pImage->pvBackendData, uOffset, pvBuf,
                                       cbThisWrite, &cbThisWrite, &cbPreRead,
                                       &cbPostRead, fWrite);
        if (rc == VERR_VDI_BLOCK_FREE)
        {
            void *pvTmp = RTMemTmpAlloc(cbPreRead + cbThisWrite + cbPostRead);
            AssertBreakStmt(VALID_PTR(pvTmp), rc = VERR_NO_MEMORY);

            if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_HONOR_SAME))
            {
                /* Optimized write, suppress writing to a so far unallocated
                 * block if the data is in fact not changed. */
                rc = vdWriteHelperOptimized(pDisk, pImage, uOffset, cbWrite,
                                            cbThisWrite, cbPreRead, cbPostRead,
                                            pvBuf, pvTmp);
            }
            else
            {
                /* Normal write, not optimized in any way. The block will
                 * be written no matter what. This will usually (unless the
                 * backend has some further optimization enabled) cause the
                 * block to be allocated. */
                rc = vdWriteHelperStandard(pDisk, pImage, uOffset, cbWrite,
                                           cbThisWrite, cbPreRead, cbPostRead,
                                           pvBuf, pvTmp);
            }
            RTMemTmpFree(pvTmp);
            if (RT_FAILURE(rc))
                break;
        }

        cbWrite -= cbThisWrite;
        uOffset += cbThisWrite;
        pvBuf = (char *)pvBuf + cbThisWrite;
    } while (cbWrite != 0 && RT_SUCCESS(rc));

    return rc;
}


/**
 * Lists all HDD backends and their capabilities in a caller-provided buffer.
 *
 * @returns VBox status code.
 *          VERR_BUFFER_OVERFLOW if not enough space is passed.
 * @param   cEntriesAlloc   Number of list entries available.
 * @param   pEntries        Pointer to array for the entries.
 * @param   pcEntriesUsed   Number of entries returned.
 */
VBOXDDU_DECL(int) VDBackendInfo(unsigned cEntriesAlloc, PVDBACKENDINFO pEntries,
                                unsigned *pcEntriesUsed)
{
    int rc = VINF_SUCCESS;
    PRTDIR pPluginDir = NULL;
    unsigned cEntries = 0;

    LogFlowFunc(("cEntriesAlloc=%u pEntries=%#p pcEntriesUsed=%#p\n", cEntriesAlloc, pEntries, pcEntriesUsed));
    do
    {
        /* Check arguments. */
        AssertMsgBreakStmt(cEntriesAlloc,
                           ("cEntriesAlloc=%u\n", cEntriesAlloc),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(VALID_PTR(pEntries),
                           ("pEntries=%#p\n", pEntries),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(VALID_PTR(pcEntriesUsed),
                           ("pcEntriesUsed=%#p\n", pcEntriesUsed),
                           rc = VERR_INVALID_PARAMETER);

        /* First enumerate static backends. */
        for (unsigned i = 0; aBackends[i] != NULL; i++)
        {
            char *pszName = RTStrDup(aBackends[i]->pszBackendName);
            if (!pszName)
            {
                rc = VERR_NO_MEMORY;
                break;
            }
            pEntries[cEntries].pszBackend = pszName;
            pEntries[cEntries].uBackendCaps = aBackends[i]->uBackendCaps;
            pEntries[cEntries].papszFileExtensions = aBackends[i]->papszFileExtensions;
            pEntries[cEntries].paConfigInfo = aBackends[i]->paConfigInfo;
            cEntries++;
            if (cEntries >= cEntriesAlloc)
            {
                rc = VERR_BUFFER_OVERFLOW;
                break;
            }
        }
        if (RT_FAILURE(rc))
            break;

        /* Then enumerate plugin backends. */
        char szPath[RTPATH_MAX];
        rc = RTPathSharedLibs(szPath, sizeof(szPath));
        if (RT_FAILURE(rc))
            break;

        /* To get all entries with VBoxHDD as prefix. */
        char *pszPluginFilter;
        rc = RTStrAPrintf(&pszPluginFilter, "%s/%s*", szPath,
                          VBOX_HDDFORMAT_PLUGIN_PREFIX);
        if (RT_FAILURE(rc))
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        /* The plugins are in the same directory as the other shared libs. */
        rc = RTDirOpenFiltered(&pPluginDir, pszPluginFilter, RTDIRFILTER_WINNT);
        if (RT_FAILURE(rc))
            break;

        PRTDIRENTRY pPluginDirEntry = NULL;
        unsigned cbPluginDirEntry = sizeof(RTDIRENTRY);
        pPluginDirEntry = (PRTDIRENTRY)RTMemAllocZ(sizeof(RTDIRENTRY));
        if (!pPluginDirEntry)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        while ((rc = RTDirRead(pPluginDir, pPluginDirEntry, &cbPluginDirEntry)) != VERR_NO_MORE_FILES)
        {
            RTLDRMOD hPlugin = NIL_RTLDRMOD;
            PFNVBOXHDDFORMATLOAD pfnHDDFormatLoad = NULL;
            PVBOXHDDBACKEND pBackend = NULL;
            char *pszPluginPath = NULL;

            if (rc == VERR_BUFFER_OVERFLOW)
            {
                /* allocate new buffer. */
                RTMemFree(pPluginDirEntry);
                pPluginDirEntry = (PRTDIRENTRY)RTMemAllocZ(cbPluginDirEntry);
                /* Retry. */
                rc = RTDirRead(pPluginDir, pPluginDirEntry, &cbPluginDirEntry);
                if (RT_FAILURE(rc))
                    break;
            }
            else if (RT_FAILURE(rc))
                break;

            /* We got the new entry. */
            if (pPluginDirEntry->enmType != RTDIRENTRYTYPE_FILE)
                continue;

            /* Prepend the path to the libraries. */
            rc = RTStrAPrintf(&pszPluginPath, "%s/%s", szPath, pPluginDirEntry->szName);
            if (RT_FAILURE(rc))
            {
                rc = VERR_NO_MEMORY;
                break;
            }

            rc = RTLdrLoad(pszPluginPath, &hPlugin);
            if (RT_SUCCESS(rc))
            {
                rc = RTLdrGetSymbol(hPlugin, VBOX_HDDFORMAT_LOAD_NAME, (void**)&pfnHDDFormatLoad);
                if (RT_FAILURE(rc) || !pfnHDDFormatLoad)
                {
                    LogFunc(("error resolving the entry point %s in plugin %s, rc=%Rrc, pfnHDDFormat=%#p\n", VBOX_HDDFORMAT_LOAD_NAME, pPluginDirEntry->szName, rc, pfnHDDFormatLoad));
                    if (RT_SUCCESS(rc))
                        rc = VERR_SYMBOL_NOT_FOUND;
                }

                if (RT_SUCCESS(rc))
                {
                    /* Get the function table. */
                    rc = pfnHDDFormatLoad(&pBackend);
                    if (RT_SUCCESS(rc) && pBackend->cbSize == sizeof(VBOXHDDBACKEND))
                    {
                        char *pszName = RTStrDup(pBackend->pszBackendName);
                        if (!pszName)
                        {
                            rc = VERR_NO_MEMORY;
                            break;
                        }
                        pEntries[cEntries].pszBackend = pszName;
                        pEntries[cEntries].uBackendCaps = pBackend->uBackendCaps;
                        pEntries[cEntries].papszFileExtensions = pBackend->papszFileExtensions;
                        pEntries[cEntries].paConfigInfo = pBackend->paConfigInfo;
                        cEntries++;
                        if (cEntries >= cEntriesAlloc)
                        {
                            rc = VERR_BUFFER_OVERFLOW;
                            break;
                        }
                    }
                }
                else
                    pBackend = NULL;

                RTLdrClose(hPlugin);
            }
            RTStrFree(pszPluginPath);
        }
        if (rc == VERR_NO_MORE_FILES)
            rc = VINF_SUCCESS;
        RTStrFree(pszPluginFilter);
        if (pPluginDirEntry)
            RTMemFree(pPluginDirEntry);
        if (pPluginDir)
            RTDirClose(pPluginDir);
    } while (0);

    LogFlowFunc(("returns %Rrc *pcEntriesUsed=%u\n", rc, cEntries));
    *pcEntriesUsed = cEntries;
    return rc;
}

/**
 * Lists the capablities of a backend indentified by its name.
 * Free all returned names with RTStrFree() when you no longer need them.
 *
 * @returns VBox status code.
 * @param   pszBackend      The backend name.
 * @param   pEntries        Pointer to an entry.
 */
VBOXDDU_DECL(int) VDBackendInfoOne(const char *pszBackend, PVDBACKENDINFO pEntry)
{
    return VERR_NOT_IMPLEMENTED;
}

/**
 * Allocates and initializes an empty HDD container.
 * No image files are opened.
 *
 * @returns VBox status code.
 * @param   pVDIfsDisk      Pointer to the per-disk VD interface list.
 * @param   ppDisk          Where to store the reference to HDD container.
 */
VBOXDDU_DECL(int) VDCreate(PVDINTERFACE pVDIfsDisk, PVBOXHDD *ppDisk)
{
    int rc = VINF_SUCCESS;
    PVBOXHDD pDisk = NULL;

    LogFlowFunc(("pVDIfsDisk=%#p\n", pVDIfsDisk));
    do
    {
        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(ppDisk),
                           ("ppDisk=%#p\n", ppDisk),
                           rc = VERR_INVALID_PARAMETER);

        pDisk = (PVBOXHDD)RTMemAllocZ(sizeof(VBOXHDD));
        if (pDisk)
        {
            pDisk->u32Signature = VBOXHDDDISK_SIGNATURE;
            pDisk->cImages      = 0;
            pDisk->pBase        = NULL;
            pDisk->pLast        = NULL;
            pDisk->cbSize       = 0;
            pDisk->PCHSGeometry.cCylinders = 0;
            pDisk->PCHSGeometry.cHeads     = 0;
            pDisk->PCHSGeometry.cSectors   = 0;
            pDisk->LCHSGeometry.cCylinders = 0;
            pDisk->LCHSGeometry.cHeads     = 0;
            pDisk->LCHSGeometry.cSectors   = 0;
            pDisk->pVDIfsDisk  = pVDIfsDisk;
            pDisk->pInterfaceError = NULL;
            pDisk->pInterfaceErrorCallbacks = NULL;

            pDisk->pInterfaceError = VDInterfaceGet(pVDIfsDisk, VDINTERFACETYPE_ERROR);
            if (pDisk->pInterfaceError)
                pDisk->pInterfaceErrorCallbacks = VDGetInterfaceError(pDisk->pInterfaceError);
            *ppDisk = pDisk;
        }
        else
        {
            rc = VERR_NO_MEMORY;
            break;
        }
    } while (0);

    LogFlowFunc(("returns %Rrc (pDisk=%#p)\n", rc, pDisk));
    return rc;
}

/**
 * Destroys HDD container.
 * If container has opened image files they will be closed.
 *
 * @param   pDisk           Pointer to HDD container.
 */
VBOXDDU_DECL(void) VDDestroy(PVBOXHDD pDisk)
{
    LogFlowFunc(("pDisk=%#p\n", pDisk));
    do
    {
        /* sanity check */
        AssertPtrBreak(pDisk);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));
        VDCloseAll(pDisk);
        RTMemFree(pDisk);
    } while (0);
    LogFlowFunc(("returns\n"));
}

/**
 * Try to get the backend name which can use this image.
 *
 * @returns VBox status code.
 *          VINF_SUCCESS if a plugin was found.
 *                       ppszFormat contains the string which can be used as backend name.
 *          VERR_NOT_SUPPORTED if no backend was found.
 * @param   pszFilename     Name of the image file for which the backend is queried.
 * @param   ppszFormat      Receives pointer of the UTF-8 string which contains the format name.
 *                          The returned pointer must be freed using RTStrFree().
 */
VBOXDDU_DECL(int) VDGetFormat(const char *pszFilename, char **ppszFormat)
{
    PRTDIR pPluginDir = NULL;
    int rc = VERR_NOT_SUPPORTED;
    bool fPluginFound = false;

    LogFlowFunc(("pszFilename=\"%s\"\n", pszFilename));
    do
    {
        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pszFilename) && *pszFilename,
                           ("pszFilename=%#p \"%s\"\n", pszFilename, pszFilename),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(VALID_PTR(ppszFormat),
                           ("ppszFormat=%#p\n", ppszFormat),
                           rc = VERR_INVALID_PARAMETER);

        /* First check if static backends support this file format. */
        for (unsigned i = 0; aBackends[i] != NULL; i++)
        {
            if (aBackends[i]->pfnCheckIfValid)
            {
                rc = aBackends[i]->pfnCheckIfValid(pszFilename);
                if (RT_SUCCESS(rc))
                {
                    fPluginFound = true;
                    /* Copy the name into the new string. */
                    char *pszFormat = RTStrDup(aBackends[i]->pszBackendName);
                    if (!pszFormat)
                    {
                        rc = VERR_NO_MEMORY;
                        break;
                    }
                    *ppszFormat = pszFormat;
                    break;
                }
            }
        }
        if (fPluginFound)
            break;

        /* Then check if plugin backends support this file format. */
        char szPath[RTPATH_MAX];
        rc = RTPathSharedLibs(szPath, sizeof(szPath));
        if (RT_FAILURE(rc))
            break;

        /* To get all entries with VBoxHDD as prefix. */
        char *pszPluginFilter;
        rc = RTStrAPrintf(&pszPluginFilter, "%s/%s*", szPath,
                          VBOX_HDDFORMAT_PLUGIN_PREFIX);
        if (RT_FAILURE(rc))
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        /* The plugins are in the same directory as the other shared libs. */
        rc = RTDirOpenFiltered(&pPluginDir, pszPluginFilter, RTDIRFILTER_WINNT);
        if (RT_FAILURE(rc))
            break;

        PRTDIRENTRY pPluginDirEntry = NULL;
        unsigned cbPluginDirEntry = sizeof(RTDIRENTRY);
        pPluginDirEntry = (PRTDIRENTRY)RTMemAllocZ(sizeof(RTDIRENTRY));
        if (!pPluginDirEntry)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        while ((rc = RTDirRead(pPluginDir, pPluginDirEntry, &cbPluginDirEntry)) != VERR_NO_MORE_FILES)
        {
            RTLDRMOD hPlugin = NIL_RTLDRMOD;
            PFNVBOXHDDFORMATLOAD pfnHDDFormatLoad = NULL;
            PVBOXHDDBACKEND pBackend = NULL;
            char *pszPluginPath = NULL;

            if (rc == VERR_BUFFER_OVERFLOW)
            {
                /* allocate new buffer. */
                RTMemFree(pPluginDirEntry);
                pPluginDirEntry = (PRTDIRENTRY)RTMemAllocZ(cbPluginDirEntry);
                /* Retry. */
                rc = RTDirRead(pPluginDir, pPluginDirEntry, &cbPluginDirEntry);
                if (RT_FAILURE(rc))
                    break;
            }
            else if (RT_FAILURE(rc))
                break;

            /* We got the new entry. */
            if (pPluginDirEntry->enmType != RTDIRENTRYTYPE_FILE)
                continue;

            /* Prepend the path to the libraries. */
            rc = RTStrAPrintf(&pszPluginPath, "%s/%s", szPath, pPluginDirEntry->szName);
            if (RT_FAILURE(rc))
            {
                rc = VERR_NO_MEMORY;
                break;
            }

            rc = RTLdrLoad(pszPluginPath, &hPlugin);
            if (RT_SUCCESS(rc))
            {
                rc = RTLdrGetSymbol(hPlugin, VBOX_HDDFORMAT_LOAD_NAME, (void**)&pfnHDDFormatLoad);
                if (RT_FAILURE(rc) || !pfnHDDFormatLoad)
                {
                    LogFunc(("error resolving the entry point %s in plugin %s, rc=%Rrc, pfnHDDFormat=%#p\n", VBOX_HDDFORMAT_LOAD_NAME, pPluginDirEntry->szName, rc, pfnHDDFormatLoad));
                    if (RT_SUCCESS(rc))
                        rc = VERR_SYMBOL_NOT_FOUND;
                }

                if (RT_SUCCESS(rc))
                {
                    /* Get the function table. */
                    rc = pfnHDDFormatLoad(&pBackend);
                    if (RT_SUCCESS(rc) && pBackend->cbSize == sizeof(VBOXHDDBACKEND))
                    {

                        /* Check if the plugin can handle this file. */
                        rc = pBackend->pfnCheckIfValid(pszFilename);
                        if (RT_SUCCESS(rc))
                        {
                            fPluginFound = true;
                            rc = VINF_SUCCESS;

                            /* Report the format name. */
                            RTPathStripExt(pPluginDirEntry->szName);
                            AssertBreakStmt(strlen(pPluginDirEntry->szName) > VBOX_HDDFORMAT_PLUGIN_PREFIX_LENGTH,
                                            rc = VERR_INVALID_NAME);

                            char *pszFormat = NULL;
                            pszFormat = RTStrDup(pPluginDirEntry->szName + VBOX_HDDFORMAT_PLUGIN_PREFIX_LENGTH);
                            if (!pszFormat)
                                rc = VERR_NO_MEMORY;

                            *ppszFormat = pszFormat;
                        }
                    }
                }
                else
                    pBackend = NULL;

                RTLdrClose(hPlugin);
            }
            RTStrFree(pszPluginPath);

            /*
             * We take the first plugin which can handle this file.
             */
            if (fPluginFound)
                break;
        }
        if (rc == VERR_NO_MORE_FILES)
            rc = VERR_NOT_SUPPORTED;

        RTStrFree(pszPluginFilter);
        if (pPluginDirEntry)
            RTMemFree(pPluginDirEntry);
        if (pPluginDir)
            RTDirClose(pPluginDir);
    } while (0);

    LogFlowFunc(("returns %Rrc *ppszFormat=\"%s\"\n", rc, *ppszFormat));
    return rc;
}

/**
 * Opens an image file.
 *
 * The first opened image file in HDD container must have a base image type,
 * others (next opened images) must be a differencing or undo images.
 * Linkage is checked for differencing image to be in consistence with the previously opened image.
 * When another differencing image is opened and the last image was opened in read/write access
 * mode, then the last image is reopened in read-only with deny write sharing mode. This allows
 * other processes to use images in read-only mode too.
 *
 * Note that the image is opened in read-only mode if a read/write open is not possible.
 * Use VDIsReadOnly to check open mode.
 *
 * @returns VBox status code.
 * @param   pDisk           Pointer to HDD container.
 * @param   pszBackend      Name of the image file backend to use.
 * @param   pszFilename     Name of the image file to open.
 * @param   uOpenFlags      Image file open mode, see VD_OPEN_FLAGS_* constants.
 * @param   pVDIfsImage     Pointer to the per-image VD interface list.
 */
VBOXDDU_DECL(int) VDOpen(PVBOXHDD pDisk, const char *pszBackend,
                         const char *pszFilename, unsigned uOpenFlags,
                         PVDINTERFACE pVDIfsImage)
{
    int rc = VINF_SUCCESS;
    PVDIMAGE pImage = NULL;

    LogFlowFunc(("pDisk=%#p pszBackend=\"%s\" pszFilename=\"%s\" uOpenFlags=%#x\n, pVDIfsImage=%#p",
                 pDisk, pszBackend, pszFilename, uOpenFlags, pVDIfsImage));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pszBackend) && *pszBackend,
                           ("pszBackend=%#p \"%s\"\n", pszBackend, pszBackend),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(VALID_PTR(pszFilename) && *pszFilename,
                           ("pszFilename=%#p \"%s\"\n", pszFilename, pszFilename),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt((uOpenFlags & ~VD_OPEN_FLAGS_MASK) == 0,
                           ("uOpenFlags=%#x\n", uOpenFlags),
                           rc = VERR_INVALID_PARAMETER);

        /* Set up image descriptor. */
        pImage = (PVDIMAGE)RTMemAllocZ(sizeof(VDIMAGE));
        if (!pImage)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        pImage->pszFilename = RTStrDup(pszFilename);
        if (!pImage->pszFilename)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        pImage->pVDIfsImage = pVDIfsImage;

        rc = vdFindBackend(pszBackend, &pImage->Backend, &pImage->hPlugin);
        if (RT_FAILURE(rc))
            break;
        if (!pImage->Backend)
        {
            rc = vdError(pDisk, VERR_INVALID_PARAMETER, RT_SRC_POS,
                         N_("VD: unknown backend name '%s'"), pszBackend);
            break;
        }

        pImage->uOpenFlags = uOpenFlags & VD_OPEN_FLAGS_HONOR_SAME;
        rc = pImage->Backend->pfnOpen(pImage->pszFilename,
                                      uOpenFlags & ~VD_OPEN_FLAGS_HONOR_SAME,
                                      pDisk->pVDIfsDisk,
                                      pImage->pVDIfsImage,
                                      &pImage->pvBackendData);
        /* If the open in read-write mode failed, retry in read-only mode. */
        if (RT_FAILURE(rc))
        {
            if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY)
                &&  (rc == VERR_ACCESS_DENIED
                     || rc == VERR_PERMISSION_DENIED
                     || rc == VERR_WRITE_PROTECT
                     || rc == VERR_SHARING_VIOLATION
                     || rc == VERR_FILE_LOCK_FAILED))
                rc = pImage->Backend->pfnOpen(pImage->pszFilename,
                                                (uOpenFlags & ~VD_OPEN_FLAGS_HONOR_SAME)
                                               | VD_OPEN_FLAGS_READONLY,
                                               pDisk->pVDIfsDisk,
                                               pImage->pVDIfsImage,
                                               &pImage->pvBackendData);
            if (RT_FAILURE(rc))
            {
                rc = vdError(pDisk, rc, RT_SRC_POS,
                             N_("VD: error opening image file '%s'"), pszFilename);
                break;
            }
        }

        VDIMAGETYPE enmImageType;
        rc = pImage->Backend->pfnGetImageType(pImage->pvBackendData,
                                              &enmImageType);
        /* Check image type. As the image itself has only partial knowledge
         * whether it's a base image or not, this info is derived here. The
         * base image can be fixed or normal, all others must be normal or
         * diff images. Some image formats don't distinguish between normal
         * and diff images, so this must be corrected here. */
        if (RT_FAILURE(rc))
            enmImageType = VD_IMAGE_TYPE_INVALID;
        if (    RT_SUCCESS(rc)
            &&  !(uOpenFlags & VD_OPEN_FLAGS_INFO))
        {
            if (    pDisk->cImages == 0
                &&  enmImageType != VD_IMAGE_TYPE_FIXED
                &&  enmImageType != VD_IMAGE_TYPE_NORMAL)
            {
                rc = VERR_VDI_INVALID_TYPE;
                break;
            }
            else if (pDisk->cImages != 0)
            {
                if (    enmImageType != VD_IMAGE_TYPE_NORMAL
                    &&  enmImageType != VD_IMAGE_TYPE_DIFF)
                {
                    rc = VERR_VDI_INVALID_TYPE;
                    break;
                }
                else
                    enmImageType = VD_IMAGE_TYPE_DIFF;
            }
        }
        pImage->enmImageType = enmImageType;

        /* Force sane optimization settings. It's not worth avoiding writes
         * to fixed size images. The overhead would have almost no payback. */
        if (enmImageType == VD_IMAGE_TYPE_FIXED)
            pImage->uOpenFlags |= VD_OPEN_FLAGS_HONOR_SAME;

        /** @todo optionally check UUIDs */

        int rc2;

        /* Cache disk information. */
        pDisk->cbSize = pImage->Backend->pfnGetSize(pImage->pvBackendData);

        /* Cache PCHS geometry. */
        rc2 = pImage->Backend->pfnGetPCHSGeometry(pImage->pvBackendData,
                                                  &pDisk->PCHSGeometry);
        if (RT_FAILURE(rc2))
        {
            pDisk->PCHSGeometry.cCylinders = 0;
            pDisk->PCHSGeometry.cHeads = 0;
            pDisk->PCHSGeometry.cSectors = 0;
        }
        else
        {
            /* Make sure the PCHS geometry is properly clipped. */
            pDisk->PCHSGeometry.cCylinders = RT_MIN(pDisk->PCHSGeometry.cCylinders, 16383);
            pDisk->PCHSGeometry.cHeads = RT_MIN(pDisk->PCHSGeometry.cHeads, 16);
            pDisk->PCHSGeometry.cSectors = RT_MIN(pDisk->PCHSGeometry.cSectors, 63);
        }

        /* Cache LCHS geometry. */
        rc2 = pImage->Backend->pfnGetLCHSGeometry(pImage->pvBackendData,
                                                  &pDisk->LCHSGeometry);
        if (RT_FAILURE(rc2))
        {
            pDisk->LCHSGeometry.cCylinders = 0;
            pDisk->LCHSGeometry.cHeads = 0;
            pDisk->LCHSGeometry.cSectors = 0;
        }
        else
        {
            /* Make sure the LCHS geometry is properly clipped. */
            pDisk->LCHSGeometry.cCylinders = RT_MIN(pDisk->LCHSGeometry.cCylinders, 1024);
            pDisk->LCHSGeometry.cHeads = RT_MIN(pDisk->LCHSGeometry.cHeads, 255);
            pDisk->LCHSGeometry.cSectors = RT_MIN(pDisk->LCHSGeometry.cSectors, 63);
        }

        if (pDisk->cImages != 0)
        {
            /* Switch previous image to read-only mode. */
            unsigned uOpenFlagsPrevImg;
            uOpenFlagsPrevImg = pDisk->pLast->Backend->pfnGetOpenFlags(pDisk->pLast->pvBackendData);
            if (!(uOpenFlagsPrevImg & VD_OPEN_FLAGS_READONLY))
            {
                uOpenFlagsPrevImg |= VD_OPEN_FLAGS_READONLY;
                rc = pDisk->pLast->Backend->pfnSetOpenFlags(pDisk->pLast->pvBackendData, uOpenFlagsPrevImg);
            }
        }

        if (RT_SUCCESS(rc))
        {
            /* Image successfully opened, make it the last image. */
            vdAddImageToList(pDisk, pImage);
            if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY))
                pDisk->uModified = VD_IMAGE_MODIFIED_FIRST;
        }
        else
        {
            /* Error detected, but image opened. Close image. */
            int rc2;
            rc2 = pImage->Backend->pfnClose(pImage->pvBackendData, false);
            AssertRC(rc2);
            pImage->pvBackendData = NULL;
        }
    } while (0);

    if (RT_FAILURE(rc))
    {
        if (pImage)
        {
            if (pImage->hPlugin != NIL_RTLDRMOD)
                RTLdrClose(pImage->hPlugin);

            if (pImage->pszFilename)
                RTStrFree(pImage->pszFilename);
            RTMemFree(pImage);
        }
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Creates and opens a new base image file.
 *
 * @returns VBox status code.
 * @param   pDisk           Pointer to HDD container.
 * @param   pszBackend      Name of the image file backend to use.
 * @param   pszFilename     Name of the image file to create.
 * @param   enmType         Image type, only base image types are acceptable.
 * @param   cbSize          Image size in bytes.
 * @param   uImageFlags     Flags specifying special image features.
 * @param   pszComment      Pointer to image comment. NULL is ok.
 * @param   pPCHSGeometry   Pointer to physical disk geometry <= (16383,16,63). Not NULL.
 * @param   pLCHSGeometry   Pointer to logical disk geometry <= (1024,255,63). Not NULL.
 * @param   pUuid           New UUID of the image. If NULL, a new UUID is created.
 * @param   uOpenFlags      Image file open mode, see VD_OPEN_FLAGS_* constants.
 * @param   pVDIfsImage     Pointer to the per-image VD interface list.
 * @param   pVDIfsOperation Pointer to the per-operation VD interface list.
 */
VBOXDDU_DECL(int) VDCreateBase(PVBOXHDD pDisk, const char *pszBackend,
                               const char *pszFilename, VDIMAGETYPE enmType,
                               uint64_t cbSize, unsigned uImageFlags,
                               const char *pszComment,
                               PCPDMMEDIAGEOMETRY pPCHSGeometry,
                               PCPDMMEDIAGEOMETRY pLCHSGeometry,
                               PCRTUUID pUuid, unsigned uOpenFlags,
                               PVDINTERFACE pVDIfsImage,
                               PVDINTERFACE pVDIfsOperation)
{
    int rc = VINF_SUCCESS;
    PVDIMAGE pImage = NULL;
    RTUUID uuid;

    LogFlowFunc(("pDisk=%#p pszBackend=\"%s\" pszFilename=\"%s\" enmType=%#x cbSize=%llu uImageFlags=%#x pszComment=\"%s\" PCHS=%u/%u/%u LCHS=%u/%u/%u Uuid=%RTuuid uOpenFlags=%#x pVDIfsImage=%#p pVDIfsOperation=%#p\n",
                 pDisk, pszBackend, pszFilename, enmType, cbSize, uImageFlags, pszComment,
                 pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads,
                 pPCHSGeometry->cSectors, pLCHSGeometry->cCylinders,
                 pLCHSGeometry->cHeads, pLCHSGeometry->cSectors, pUuid,
                 uOpenFlags, pVDIfsImage, pVDIfsOperation));

    PVDINTERFACE pIfProgress = VDInterfaceGet(pVDIfsOperation,
                                              VDINTERFACETYPE_PROGRESS);
    PVDINTERFACEPROGRESS pCbProgress = NULL;
    if (pIfProgress)
        pCbProgress = VDGetInterfaceProgress(pIfProgress);

    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pszBackend) && *pszBackend,
                           ("pszBackend=%#p \"%s\"\n", pszBackend, pszBackend),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(VALID_PTR(pszFilename) && *pszFilename,
                           ("pszFilename=%#p \"%s\"\n", pszFilename, pszFilename),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(enmType == VD_IMAGE_TYPE_NORMAL || enmType == VD_IMAGE_TYPE_FIXED,
                           ("enmType=%#x\n", enmType),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(cbSize,
                           ("cbSize=%llu\n", cbSize),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt((uImageFlags & ~VD_IMAGE_FLAGS_MASK) == 0,
                           ("uImageFlags=%#x\n", uImageFlags),
                           rc = VERR_INVALID_PARAMETER);
        /* The PCHS geometry fields may be 0 to leave it for later. */
        AssertMsgBreakStmt(   VALID_PTR(pPCHSGeometry)
                           && pPCHSGeometry->cCylinders <= 16383
                           && pPCHSGeometry->cHeads <= 16
                           && pPCHSGeometry->cSectors <= 63,
                           ("pPCHSGeometry=%#p PCHS=%u/%u/%u\n", pPCHSGeometry,
                            pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads,
                            pPCHSGeometry->cSectors),
                           rc = VERR_INVALID_PARAMETER);
        /* The LCHS geometry fields may be 0 to leave it to later autodetection. */
        AssertMsgBreakStmt(   VALID_PTR(pLCHSGeometry)
                           && pLCHSGeometry->cCylinders <= 1024
                           && pLCHSGeometry->cHeads <= 255
                           && pLCHSGeometry->cSectors <= 63,
                           ("pLCHSGeometry=%#p LCHS=%u/%u/%u\n", pLCHSGeometry,
                            pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads,
                            pLCHSGeometry->cSectors),
                           rc = VERR_INVALID_PARAMETER);
        /* The UUID may be NULL. */
        AssertMsgBreakStmt(pUuid == NULL || VALID_PTR(pUuid),
                           ("pUuid=%#p UUID=%RTuuid\n", pUuid, pUuid),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt((uOpenFlags & ~VD_OPEN_FLAGS_MASK) == 0,
                           ("uOpenFlags=%#x\n", uOpenFlags),
                           rc = VERR_INVALID_PARAMETER);

        /* Check state. */
        AssertMsgBreakStmt(pDisk->cImages == 0,
                           ("Create base image cannot be done with other images open\n"),
                           rc = VERR_VDI_INVALID_STATE);

        /* Set up image descriptor. */
        pImage = (PVDIMAGE)RTMemAllocZ(sizeof(VDIMAGE));
        if (!pImage)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        pImage->pszFilename = RTStrDup(pszFilename);
        if (!pImage->pszFilename)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        pImage->pVDIfsImage = pVDIfsImage;

        rc = vdFindBackend(pszBackend, &pImage->Backend, &pImage->hPlugin);
        if (RT_FAILURE(rc))
            break;
        if (!pImage->Backend)
        {
            rc = vdError(pDisk, VERR_INVALID_PARAMETER, RT_SRC_POS,
                         N_("VD: unknown backend name '%s'"), pszBackend);
            break;
        }

        /* Create UUID if the caller didn't specify one. */
        if (!pUuid)
        {
            rc = RTUuidCreate(&uuid);
            if (RT_FAILURE(rc))
            {
                rc = vdError(pDisk, rc, RT_SRC_POS,
                             N_("VD: cannot generate UUID for image '%s'"),
                             pszFilename);
                break;
            }
            pUuid = &uuid;
        }

        pImage->uOpenFlags = uOpenFlags & VD_OPEN_FLAGS_HONOR_SAME;
        rc = pImage->Backend->pfnCreate(pImage->pszFilename, enmType, cbSize,
                                        uImageFlags, pszComment, pPCHSGeometry,
                                        pLCHSGeometry, pUuid,
                                        uOpenFlags & ~VD_OPEN_FLAGS_HONOR_SAME,
                                        0, 99,
                                        pDisk->pVDIfsDisk,
                                        pImage->pVDIfsImage,
                                        pVDIfsOperation,
                                        &pImage->pvBackendData);

        if (RT_SUCCESS(rc))
        {
            pImage->enmImageType = enmType;

            /* Force sane optimization settings. It's not worth avoiding writes
             * to fixed size images. The overhead would have almost no payback. */
            if (enmType == VD_IMAGE_TYPE_FIXED)
                pImage->uOpenFlags |= VD_OPEN_FLAGS_HONOR_SAME;

            /** @todo optionally check UUIDs */

            int rc2;

            /* Cache disk information. */
            pDisk->cbSize = pImage->Backend->pfnGetSize(pImage->pvBackendData);

            /* Cache PCHS geometry. */
            rc2 = pImage->Backend->pfnGetPCHSGeometry(pImage->pvBackendData,
                                                      &pDisk->PCHSGeometry);
            if (RT_FAILURE(rc2))
            {
                pDisk->PCHSGeometry.cCylinders = 0;
                pDisk->PCHSGeometry.cHeads = 0;
                pDisk->PCHSGeometry.cSectors = 0;
            }
            else
            {
                /* Make sure the CHS geometry is properly clipped. */
                pDisk->PCHSGeometry.cCylinders = RT_MIN(pDisk->PCHSGeometry.cCylinders, 16383);
                pDisk->PCHSGeometry.cHeads = RT_MIN(pDisk->PCHSGeometry.cHeads, 16);
                pDisk->PCHSGeometry.cSectors = RT_MIN(pDisk->PCHSGeometry.cSectors, 63);
            }

            /* Cache LCHS geometry. */
            rc2 = pImage->Backend->pfnGetLCHSGeometry(pImage->pvBackendData,
                                                      &pDisk->LCHSGeometry);
            if (RT_FAILURE(rc2))
            {
                pDisk->LCHSGeometry.cCylinders = 0;
                pDisk->LCHSGeometry.cHeads = 0;
                pDisk->LCHSGeometry.cSectors = 0;
            }
            else
            {
                /* Make sure the CHS geometry is properly clipped. */
                pDisk->LCHSGeometry.cCylinders = RT_MIN(pDisk->LCHSGeometry.cCylinders, 1024);
                pDisk->LCHSGeometry.cHeads = RT_MIN(pDisk->LCHSGeometry.cHeads, 255);
                pDisk->LCHSGeometry.cSectors = RT_MIN(pDisk->LCHSGeometry.cSectors, 63);
            }
        }

        if (RT_SUCCESS(rc))
        {
            /* Image successfully opened, make it the last image. */
            vdAddImageToList(pDisk, pImage);
            if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY))
                pDisk->uModified = VD_IMAGE_MODIFIED_FIRST;
        }
        else
        {
            /* Error detected, but image opened. Close and delete image. */
            int rc2;
            rc2 = pImage->Backend->pfnClose(pImage->pvBackendData, true);
            AssertRC(rc2);
            pImage->pvBackendData = NULL;
        }
    } while (0);

    if (RT_FAILURE(rc))
    {
        if (pImage)
        {
            if (pImage->hPlugin != NIL_RTLDRMOD)
                RTLdrClose(pImage->hPlugin);

            if (pImage->pszFilename)
                RTStrFree(pImage->pszFilename);
            RTMemFree(pImage);
        }
    }

    if (RT_SUCCESS(rc) && pCbProgress->pfnProgress)
        pCbProgress->pfnProgress(NULL /* WARNING! pVM=NULL */, 100,
                                 pIfProgress->pvUser);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Creates and opens a new differencing image file in HDD container.
 * See comments for VDOpen function about differencing images.
 *
 * @returns VBox status code.
 * @param   pDisk           Pointer to HDD container.
 * @param   pszBackend      Name of the image file backend to use.
 * @param   pszFilename     Name of the differencing image file to create.
 * @param   uImageFlags     Flags specifying special image features.
 * @param   pszComment      Pointer to image comment. NULL is ok.
 * @param   pUuid           New UUID of the image. If NULL, a new UUID is created.
 * @param   uOpenFlags      Image file open mode, see VD_OPEN_FLAGS_* constants.
 * @param   pVDIfsImage     Pointer to the per-image VD interface list.
 * @param   pVDIfsOperation Pointer to the per-operation VD interface list.
 */
VBOXDDU_DECL(int) VDCreateDiff(PVBOXHDD pDisk, const char *pszBackend,
                               const char *pszFilename, unsigned uImageFlags,
                               const char *pszComment, PCRTUUID pUuid,
                               unsigned uOpenFlags, PVDINTERFACE pVDIfsImage,
                               PVDINTERFACE pVDIfsOperation)
{
    int rc = VINF_SUCCESS;
    PVDIMAGE pImage = NULL;
    RTUUID uuid;

    LogFlowFunc(("pDisk=%#p pszBackend=\"%s\" pszFilename=\"%s\" uImageFlags=%#x pszComment=\"%s\" Uuid=%RTuuid uOpenFlags=%#x pVDIfsImage=%#p pVDIfsOperation=%#p\n",
                 pDisk, pszBackend, pszFilename, uImageFlags, pszComment, pUuid, uOpenFlags,
                 pVDIfsImage, pVDIfsOperation));

    PVDINTERFACE pIfProgress = VDInterfaceGet(pVDIfsOperation,
                                              VDINTERFACETYPE_PROGRESS);
    PVDINTERFACEPROGRESS pCbProgress = NULL;
    if (pIfProgress)
        pCbProgress = VDGetInterfaceProgress(pIfProgress);

    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pszBackend) && *pszBackend,
                           ("pszBackend=%#p \"%s\"\n", pszBackend, pszBackend),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(VALID_PTR(pszFilename) && *pszFilename,
                           ("pszFilename=%#p \"%s\"\n", pszFilename, pszFilename),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt((uImageFlags & ~VD_IMAGE_FLAGS_MASK) == 0,
                           ("uImageFlags=%#x\n", uImageFlags),
                           rc = VERR_INVALID_PARAMETER);
        /* The UUID may be NULL. */
        AssertMsgBreakStmt(pUuid == NULL || VALID_PTR(pUuid),
                           ("pUuid=%#p UUID=%RTuuid\n", pUuid, pUuid),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt((uOpenFlags & ~VD_OPEN_FLAGS_MASK) == 0,
                           ("uOpenFlags=%#x\n", uOpenFlags),
                           rc = VERR_INVALID_PARAMETER);

        /* Check state. */
        AssertMsgBreakStmt(pDisk->cImages != 0,
                           ("Create diff image cannot be done without other images open\n"),
                           rc = VERR_VDI_INVALID_STATE);

        /* Set up image descriptor. */
        pImage = (PVDIMAGE)RTMemAllocZ(sizeof(VDIMAGE));
        if (!pImage)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
        pImage->pszFilename = RTStrDup(pszFilename);
        if (!pImage->pszFilename)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        rc = vdFindBackend(pszBackend, &pImage->Backend, &pImage->hPlugin);
        if (RT_FAILURE(rc))
            break;
        if (!pImage->Backend)
        {
            rc = vdError(pDisk, VERR_INVALID_PARAMETER, RT_SRC_POS,
                         N_("VD: unknown backend name '%s'"), pszBackend);
            break;
        }

        /* Create UUID if the caller didn't specify one. */
        if (!pUuid)
        {
            rc = RTUuidCreate(&uuid);
            if (RT_FAILURE(rc))
            {
                rc = vdError(pDisk, rc, RT_SRC_POS,
                             N_("VD: cannot generate UUID for image '%s'"),
                             pszFilename);
                break;
            }
            pUuid = &uuid;
        }

        pImage->uOpenFlags = uOpenFlags & VD_OPEN_FLAGS_HONOR_SAME;
        rc = pImage->Backend->pfnCreate(pImage->pszFilename,
                                        VD_IMAGE_TYPE_DIFF, pDisk->cbSize,
                                        uImageFlags, pszComment,
                                        &pDisk->PCHSGeometry,
                                        &pDisk->LCHSGeometry, pUuid,
                                        uOpenFlags & ~VD_OPEN_FLAGS_HONOR_SAME,
                                        0, 99,
                                        pDisk->pVDIfsDisk,
                                        pImage->pVDIfsImage,
                                        pVDIfsOperation,
                                        &pImage->pvBackendData);

        if (RT_SUCCESS(rc) && pDisk->cImages != 0)
        {
            pImage->enmImageType = VD_IMAGE_TYPE_DIFF;

            /* Switch previous image to read-only mode. */
            unsigned uOpenFlagsPrevImg;
            uOpenFlagsPrevImg = pDisk->pLast->Backend->pfnGetOpenFlags(pDisk->pLast->pvBackendData);
            if (!(uOpenFlagsPrevImg & VD_OPEN_FLAGS_READONLY))
            {
                uOpenFlagsPrevImg |= VD_OPEN_FLAGS_READONLY;
                rc = pDisk->pLast->Backend->pfnSetOpenFlags(pDisk->pLast->pvBackendData, uOpenFlagsPrevImg);
            }
        }

        if (RT_SUCCESS(rc))
        {
            RTUUID Uuid;
            RTTIMESPEC ts;
            int rc2;

            rc2 = pDisk->pLast->Backend->pfnGetUuid(pDisk->pLast->pvBackendData,
                                                    &Uuid);
            if (RT_SUCCESS(rc2))
                pImage->Backend->pfnSetParentUuid(pImage->pvBackendData, &Uuid);
            rc2 = pDisk->pLast->Backend->pfnGetModificationUuid(pDisk->pLast->pvBackendData,
                                                                &Uuid);
            if (RT_SUCCESS(rc2))
                pImage->Backend->pfnSetParentModificationUuid(pImage->pvBackendData,
                                                              &Uuid);
            rc2 = pDisk->pLast->Backend->pfnGetTimeStamp(pDisk->pLast->pvBackendData,
                                                             &ts);
            if (RT_SUCCESS(rc2))
                pImage->Backend->pfnSetParentTimeStamp(pImage->pvBackendData, &ts);

            rc2 = pImage->Backend->pfnSetParentFilename(pImage->pvBackendData, pDisk->pLast->pszFilename);
        }

        if (RT_SUCCESS(rc))
        {
            /** @todo optionally check UUIDs */
        }

        if (RT_SUCCESS(rc))
        {
            /* Image successfully opened, make it the last image. */
            vdAddImageToList(pDisk, pImage);
            if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY))
                pDisk->uModified = VD_IMAGE_MODIFIED_FIRST;
        }
        else
        {
            /* Error detected, but image opened. Close and delete image. */
            int rc2;
            rc2 = pImage->Backend->pfnClose(pImage->pvBackendData, true);
            AssertRC(rc2);
            pImage->pvBackendData = NULL;
        }
    } while (0);

    if (RT_FAILURE(rc))
    {
        if (pImage->hPlugin != NIL_RTLDRMOD)
            RTLdrClose(pImage->hPlugin);

        if (pImage)
        {
            if (pImage->pszFilename)
                RTStrFree(pImage->pszFilename);
            RTMemFree(pImage);
        }
    }

    if (RT_SUCCESS(rc) && pCbProgress->pfnProgress)
        pCbProgress->pfnProgress(NULL /* WARNING! pVM=NULL */, 100,
                                 pIfProgress->pvUser);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Merges two images (not necessarily with direct parent/child relationship).
 * As a side effect the source image and potentially the other images which
 * are also merged to the destination are deleted from both the disk and the
 * images in the HDD container.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImageFrom      Name of the image file to merge from.
 * @param   nImageTo        Name of the image file to merge to.
 * @param   pVDIfsOperation Pointer to the per-operation VD interface list.
 */
VBOXDDU_DECL(int) VDMerge(PVBOXHDD pDisk, unsigned nImageFrom,
                          unsigned nImageTo, PVDINTERFACE pVDIfsOperation)
{
    int rc = VINF_SUCCESS;
    void *pvBuf = NULL;

    LogFlowFunc(("pDisk=%#p nImageFrom=%u nImageTo=%u pVDIfsOperation=%#p\n",
                 pDisk, nImageFrom, nImageTo, pVDIfsOperation));

    PVDINTERFACE pIfProgress = VDInterfaceGet(pVDIfsOperation,
                                              VDINTERFACETYPE_PROGRESS);
    PVDINTERFACEPROGRESS pCbProgress = NULL;
    if (pIfProgress)
        pCbProgress = VDGetInterfaceProgress(pIfProgress);

    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        PVDIMAGE pImageFrom = vdGetImageByNumber(pDisk, nImageFrom);
        PVDIMAGE pImageTo = vdGetImageByNumber(pDisk, nImageTo);
        if (!pImageFrom || !pImageTo)
        {
            rc = VERR_VDI_IMAGE_NOT_FOUND;
            break;
        }
        AssertBreakStmt(pImageFrom != pImageTo, rc = VERR_INVALID_PARAMETER);

        /* Make sure destination image is writable. */
        unsigned uOpenFlags = pImageTo->Backend->pfnGetOpenFlags(pImageTo->pvBackendData);
        if (uOpenFlags & VD_OPEN_FLAGS_READONLY)
        {
            uOpenFlags &= ~VD_OPEN_FLAGS_READONLY;
            rc = pImageTo->Backend->pfnSetOpenFlags(pImageTo->pvBackendData,
                                                    uOpenFlags);
            if (RT_FAILURE(rc))
                break;
        }

        /* Get size of destination image. */
        uint64_t cbSize = pImageTo->Backend->pfnGetSize(pImageTo->pvBackendData);

        /* Allocate tmp buffer. */
        pvBuf = RTMemTmpAlloc(VD_MERGE_BUFFER_SIZE);
        if (!pvBuf)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        /* Merging is done directly on the images itself. This potentially
         * causes trouble if the disk is full in the middle of operation. */
        /** @todo write alternative implementation which works with temporary
         * images (which is safer, but requires even more space). Also has the
         * drawback that merging into a raw disk parent simply isn't possible
         * this way (but in that case disk full isn't really a problem). */
        if (nImageFrom < nImageTo)
        {
            /* Merge parent state into child. This means writing all not
             * allocated blocks in the destination image which are allocated in
             * the images to be merged. */
            uint64_t uOffset = 0;
            uint64_t cbRemaining = cbSize;
            do
            {
                size_t cbThisRead = RT_MIN(VD_MERGE_BUFFER_SIZE, cbRemaining);
                rc = pImageTo->Backend->pfnRead(pImageTo->pvBackendData,
                                                uOffset, pvBuf, cbThisRead,
                                                &cbThisRead);
                if (rc == VERR_VDI_BLOCK_FREE)
                {
                    /* Search for image with allocated block. Do not attempt to
                     * read more than the previous reads marked as valid.
                     * Otherwise this would return stale data when different
                     * block sizes are used for the images. */
                    for (PVDIMAGE pCurrImage = pImageTo->pPrev;
                         pCurrImage != NULL && pCurrImage != pImageFrom->pPrev && rc == VERR_VDI_BLOCK_FREE;
                         pCurrImage = pCurrImage->pPrev)
                    {
                        rc = pCurrImage->Backend->pfnRead(pCurrImage->pvBackendData,
                                                          uOffset, pvBuf,
                                                          cbThisRead,
                                                          &cbThisRead);
                    }

                    if (rc != VERR_VDI_BLOCK_FREE)
                    {
                        if (RT_FAILURE(rc))
                            break;
                        rc = vdWriteHelper(pDisk, pImageTo, uOffset, pvBuf,
                                           cbThisRead);
                        if (RT_FAILURE(rc))
                            break;
                    }
                    else
                        rc = VINF_SUCCESS;
                }
                else if (RT_FAILURE(rc))
                    break;

                uOffset += cbThisRead;
                cbRemaining -= cbThisRead;

                if (pCbProgress->pfnProgress)
                {
                    rc = pCbProgress->pfnProgress(NULL /* WARNING! pVM=NULL */,
                                                  uOffset * 99 / cbSize,
                                                  pIfProgress->pvUser);
                    if (RT_FAILURE(rc))
                        break;
                }
            } while (uOffset < cbSize);
        }
        else
        {
            /* Merge child state into parent. This means writing all blocks
             * which are allocated in the image up to the source image to the
             * destination image. */
            uint64_t uOffset = 0;
            uint64_t cbRemaining = cbSize;
            do
            {
                size_t cbThisRead = RT_MIN(VD_MERGE_BUFFER_SIZE, cbRemaining);
                rc = VERR_VDI_BLOCK_FREE;
                /* Search for image with allocated block. Do not attempt to
                 * read more than the previous reads marked as valid. Otherwise
                 * this would return stale data when different block sizes are
                 * used for the images. */
                for (PVDIMAGE pCurrImage = pImageFrom;
                     pCurrImage != NULL && pCurrImage != pImageTo && rc == VERR_VDI_BLOCK_FREE;
                     pCurrImage = pCurrImage->pPrev)
                {
                    rc = pCurrImage->Backend->pfnRead(pCurrImage->pvBackendData,
                                                      uOffset, pvBuf,
                                                      cbThisRead, &cbThisRead);
                }

                if (rc != VERR_VDI_BLOCK_FREE)
                {
                    if (RT_FAILURE(rc))
                        break;
                    rc = vdWriteHelper(pDisk, pImageTo, uOffset, pvBuf,
                                       cbThisRead);
                    if (RT_FAILURE(rc))
                        break;
                }
                else
                    rc = VINF_SUCCESS;

                uOffset += cbThisRead;
                cbRemaining -= cbThisRead;

                if (pCbProgress->pfnProgress)
                {
                    rc = pCbProgress->pfnProgress(NULL /* WARNING! pVM=NULL */,
                                                  uOffset * 99 / cbSize,
                                                  pIfProgress->pvUser);
                    if (RT_FAILURE(rc))
                        break;
                }
            } while (uOffset < cbSize);
        }

        /* Update parent UUID so that image chain is consistent. */
        RTUUID Uuid;
        if (nImageFrom < nImageTo)
        {
            if (pImageTo->pPrev)
            {
                rc = pImageTo->Backend->pfnGetUuid(pImageTo->pPrev->pvBackendData,
                                                   &Uuid);
                AssertRC(rc);
            }
            else
                RTUuidClear(&Uuid);
            rc = pImageTo->Backend->pfnSetParentUuid(pImageTo->pvBackendData,
                                                     &Uuid);
            AssertRC(rc);
        }
        else
        {
            if (pImageFrom->pNext)
            {
                rc = pImageTo->Backend->pfnGetUuid(pImageTo->pvBackendData,
                                                   &Uuid);
                AssertRC(rc);
                rc = pImageFrom->Backend->pfnSetParentUuid(pImageFrom->pNext,
                                                           &Uuid);
                AssertRC(rc);
            }
        }

        /* Make sure destination image is back to read only if necessary. */
        if (pImageTo != pDisk->pLast && pImageFrom != pDisk->pLast)
        {
            uOpenFlags = pImageTo->Backend->pfnGetOpenFlags(pImageTo->pvBackendData);
            uOpenFlags |= VD_OPEN_FLAGS_READONLY;
            rc = pImageTo->Backend->pfnSetOpenFlags(pImageTo->pvBackendData,
                                                    uOpenFlags);
            if (RT_FAILURE(rc))
                break;
        }

        /* Delete the no longer needed images. */
        PVDIMAGE pImg = pImageFrom, pTmp;
        while (pImg != pImageTo)
        {
            if (nImageFrom < nImageTo)
                pTmp = pImg->pNext;
            else
                pTmp = pImg->pPrev;
            vdRemoveImageFromList(pDisk, pImg);
            pImg->Backend->pfnClose(pImg->pvBackendData, true);
            pImg = pTmp;
        }
    } while (0);

    if (pvBuf)
        RTMemTmpFree(pvBuf);

    if (RT_SUCCESS(rc) && pCbProgress->pfnProgress)
        pCbProgress->pfnProgress(NULL /* WARNING! pVM=NULL */, 100,
                                 pIfProgress->pvUser);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Copies an image from one HDD container to another.
 * The copy is opened in the target HDD container.
 * It is possible to convert between different image formats, because the
 * backend for the destination may be different from the source.
 * If both the source and destination reference the same HDD container,
 * then the image is moved (by copying/deleting or renaming) to the new location.
 * The source container is unchanged if the move operation fails, otherwise
 * the image at the new location is opened in the same way as the old one was.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDiskFrom       Pointer to source HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pDiskTo         Pointer to destination HDD container.
 * @param   pszBackend      Name of the image file backend to use.
 * @param   pszFilename     New name of the image (may be NULL if pDiskFrom == pDiskTo).
 * @param   fMoveByRename   If true, attempt to perform a move by renaming (if successful the new size is ignored).
 * @param   cbSize          New image size (0 means leave unchanged).
 * @param   pVDIfsOperation Pointer to the per-operation VD interface list.
 * @param   pDstVDIfsImage  Pointer to the per-image VD interface list, for the
 *                          destination image.
 * @param   pDstVDIfsOperation Pointer to the per-image VD interface list,
 *                          for the destination image.
 */
VBOXDDU_DECL(int) VDCopy(PVBOXHDD pDiskFrom, unsigned nImage, PVBOXHDD pDiskTo,
                         const char *pszBackend, const char *pszFilename,
                         bool fMoveByRename, uint64_t cbSize,
                         PVDINTERFACE pVDIfsOperation,
                         PVDINTERFACE pDstVDIfsImage,
                         PVDINTERFACE pDstVDIfsOperation)
{
    int rc, rc2 = VINF_SUCCESS;
    void *pvBuf = NULL;
    PVDIMAGE pImageTo = NULL;

    LogFlowFunc(("pDiskFrom=%#p nImage=%u pDiskTo=%#p pszBackend=\"%s\" pszFilename=\"%s\" fMoveByRename=%d cbSize=%llu pVDIfsOperation=%#p pDstVDIfsImage=%#p pDstVDIfsOperation=%#p\n",
                 pDiskFrom, nImage, pDiskTo, pszBackend, pszFilename, fMoveByRename, cbSize, pVDIfsOperation, pDstVDIfsImage, pDstVDIfsOperation));

    PVDINTERFACE pIfProgress = VDInterfaceGet(pVDIfsOperation,
                                              VDINTERFACETYPE_PROGRESS);
    PVDINTERFACEPROGRESS pCbProgress = NULL;
    if (pIfProgress)
        pCbProgress = VDGetInterfaceProgress(pIfProgress);

    PVDINTERFACE pDstIfProgress = VDInterfaceGet(pDstVDIfsOperation,
                                                 VDINTERFACETYPE_PROGRESS);
    PVDINTERFACEPROGRESS pDstCbProgress = NULL;
    if (pDstIfProgress)
        pDstCbProgress = VDGetInterfaceProgress(pDstIfProgress);

    do {
        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pDiskFrom), ("pDiskFrom=%#p\n", pDiskFrom),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDiskFrom->u32Signature == VBOXHDDDISK_SIGNATURE,
                  ("u32Signature=%08x\n", pDiskFrom->u32Signature));

        PVDIMAGE pImageFrom = vdGetImageByNumber(pDiskFrom, nImage);
        AssertPtrBreakStmt(pImageFrom, rc = VERR_VDI_IMAGE_NOT_FOUND);
        AssertMsgBreakStmt(VALID_PTR(pDiskTo), ("pDiskTo=%#p\n", pDiskTo),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDiskTo->u32Signature == VBOXHDDDISK_SIGNATURE,
                  ("u32Signature=%08x\n", pDiskTo->u32Signature));

        /* If the containers are equal and the backend is the same, rename the image. */
        if (   (pDiskFrom == pDiskTo)
            && (!strcmp(pszBackend, pImageFrom->Backend->pszBackendName)))
        {
            /* Rename the image. */
            rc = pImageFrom->Backend->pfnRename(pImageFrom->pvBackendData, pszFilename ? pszFilename : pImageFrom->pszFilename);
            break;
        }

        /* If the fMoveByRename flag is set and the backend is the same, rename the image. */
        if (   (fMoveByRename == true)
            && (!strcmp(pszBackend, pImageFrom->Backend->pszBackendName)))
        {
            /* Close the source image. */
            rc = pImageFrom->Backend->pfnClose(pImageFrom->pvBackendData, false);
            if (RT_FAILURE(rc))
                break;

            /* Open the source image in the destination container. */
            rc = VDOpen(pDiskTo, pImageFrom->Backend->pszBackendName, pImageFrom->pszFilename, pImageFrom->uOpenFlags, pDstVDIfsImage);
            if (RT_FAILURE(rc))
                goto movefail;

            pImageTo = pDiskTo->pLast;

            /* Rename the image. */
            rc = pImageTo->Backend->pfnRename(pImageTo->pvBackendData, pszFilename ? pszFilename : pImageTo->pszFilename);
            if (RT_FAILURE(rc))
                goto movefail;

            /* Cleanup the leftovers. */
            vdRemoveImageFromList(pDiskFrom, pImageFrom);
            pImageFrom->pvBackendData = NULL;

            if (pImageFrom->hPlugin != NIL_RTLDRMOD)
                RTLdrClose(pImageFrom->hPlugin);

            if (pImageFrom->pszFilename)
                RTStrFree(pImageFrom->pszFilename);

            RTMemFree(pImageFrom);

            break;
movefail:
            /* In case of failure, re-open the source image in the source container. */
            rc2 = VDOpen(pDiskFrom, pImageFrom->Backend->pszBackendName, pImageFrom->pszFilename, pImageFrom->uOpenFlags, pImageFrom->pVDIfsImage);
            if (RT_FAILURE(rc2))
                /** @todo Uncertain what to do on error. If this happens pImageFrom and pImageTo are both closed. */
                rc = rc2;
            break;
        }

        /* If fMoveByRename is set pszFilename is allowed to be NULL, so do the parameter check here. */
        AssertMsgBreakStmt(VALID_PTR(pszFilename) && *pszFilename,
                           ("pszFilename=%#p \"%s\"\n", pszFilename, pszFilename),
                           rc = VERR_INVALID_PARAMETER);

        /* Collect properties of source image. */
        VDIMAGETYPE enmTypeFrom = pImageFrom->enmImageType;

        uint64_t cbSizeFrom;
        cbSizeFrom = pImageFrom->Backend->pfnGetSize(pImageFrom->pvBackendData);
        if (cbSizeFrom == 0)
        {
            rc = VERR_VDI_VALUE_NOT_FOUND;
            break;
        }

        if (cbSize == 0)
            cbSize = cbSizeFrom;

        unsigned uImageFlagsFrom;
        uImageFlagsFrom = pImageFrom->Backend->pfnGetImageFlags(pImageFrom->pvBackendData);

        /** @todo Get this from the source image. */
        PDMMEDIAGEOMETRY PCHSGeometryFrom = {0, 0, 0};
        PDMMEDIAGEOMETRY LCHSGeometryFrom = {0, 0, 0};

        unsigned uOpenFlagsFrom;
        uOpenFlagsFrom = pImageFrom->Backend->pfnGetOpenFlags(pImageFrom->pvBackendData);

        /* Create destination image with the properties of the source image. */
        /** @todo Copy the comment. */
        /** @todo replace the VDCreateDiff/VDCreateBase calls by direct
         * calls to the backend. Unifies the code and reduces the API
         * dependencies. */
        if (enmTypeFrom == VD_IMAGE_TYPE_DIFF)
        {
            rc = VDCreateDiff(pDiskTo, pszBackend, pszFilename, uImageFlagsFrom,
                              "", NULL, uOpenFlagsFrom, NULL, NULL);
        } else {
            rc = VDCreateBase(pDiskTo, pszBackend, pszFilename, enmTypeFrom,
                              cbSize, uImageFlagsFrom, "",
                              &PCHSGeometryFrom, &LCHSGeometryFrom,
                              NULL, uOpenFlagsFrom, NULL, NULL);
        }
        if (RT_FAILURE(rc))
            break;

        pImageTo = pDiskTo->pLast;
        AssertPtrBreakStmt(pImageTo, rc = VERR_VDI_IMAGE_NOT_FOUND);

        /* Allocate tmp buffer. */
        pvBuf = RTMemTmpAlloc(VD_MERGE_BUFFER_SIZE);
        if (!pvBuf)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        /* Copy the data. */
        uint64_t uOffset = 0;
        uint64_t cbRemaining = cbSize;

        do
        {
            size_t cbThisRead = RT_MIN(VD_MERGE_BUFFER_SIZE, cbRemaining);

            rc = vdReadHelper(pDiskFrom, pImageFrom, uOffset, pvBuf,
                              cbThisRead);
            if (RT_FAILURE(rc))
                break;

            rc = vdWriteHelper(pDiskTo, pImageTo, uOffset, pvBuf,
                               cbThisRead);
            if (RT_FAILURE(rc))
                break;

            uOffset += cbThisRead;
            cbRemaining -= cbThisRead;

            if (pCbProgress->pfnProgress)
            {
                rc = pCbProgress->pfnProgress(NULL /* WARNING! pVM=NULL */,
                                              uOffset * 99 / cbSize,
                                              pIfProgress->pvUser);
                if (RT_FAILURE(rc))
                    break;
            }
            if (pDstCbProgress->pfnProgress)
            {
                rc = pDstCbProgress->pfnProgress(NULL /* WARNING! pVM=NULL */,
                                                 uOffset * 99 / cbSize,
                                                 pDstIfProgress->pvUser);
                if (RT_FAILURE(rc))
                    break;
            }
        } while (uOffset < cbSize);

        /* If fMoveByRename is set but the backend is different, close and delete pImageFrom. */
        if (   (fMoveByRename == true)
            && (strcmp(pszBackend, pImageFrom->Backend->pszBackendName)))
        {
            vdRemoveImageFromList(pDiskFrom, pImageFrom);

            /* Close and delete image. */
            rc2 = pImageFrom->Backend->pfnClose(pImageFrom->pvBackendData, true);
            AssertRC(rc2);
            pImageFrom->pvBackendData = NULL;

            /* Free remaining resources. */
            if (pImageFrom->hPlugin != NIL_RTLDRMOD)
                RTLdrClose(pImageFrom->hPlugin);

            if (pImageFrom->pszFilename)
                RTStrFree(pImageFrom->pszFilename);

            RTMemFree(pImageFrom);
        }
    } while (0);

    if (RT_FAILURE(rc) && pImageTo)
    {
        /* Error detected, but new image created. Remove image from list. */
        vdRemoveImageFromList(pDiskTo, pImageTo);

        /* Close and delete image. */
        rc2 = pImageTo->Backend->pfnClose(pImageTo->pvBackendData, true);
        AssertRC(rc2);
        pImageTo->pvBackendData = NULL;

        /* Free remaining resources. */
        if (pImageTo->hPlugin != NIL_RTLDRMOD)
            RTLdrClose(pImageTo->hPlugin);

        if (pImageTo->pszFilename)
            RTStrFree(pImageTo->pszFilename);

        RTMemFree(pImageTo);
    }

    if (pvBuf)
        RTMemTmpFree(pvBuf);

    if (RT_SUCCESS(rc))
    {
        if (pCbProgress->pfnProgress)
            pCbProgress->pfnProgress(NULL /* WARNING! pVM=NULL */, 100,
                                     pIfProgress->pvUser);
        if (pDstCbProgress->pfnProgress)
            pDstCbProgress->pfnProgress(NULL /* WARNING! pVM=NULL */, 100,
                                        pDstIfProgress->pvUser);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Closes the last opened image file in HDD container.
 * If previous image file was opened in read-only mode (that is normal) and closing image
 * was opened in read-write mode (the whole disk was in read-write mode) - the previous image
 * will be reopened in read/write mode.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_NOT_OPENED if no image is opened in HDD container.
 * @param   pDisk           Pointer to HDD container.
 * @param   fDelete         If true, delete the image from the host disk.
 */
VBOXDDU_DECL(int) VDClose(PVBOXHDD pDisk, bool fDelete)
{
    int rc = VINF_SUCCESS;;

    LogFlowFunc(("pDisk=%#p fDelete=%d\n", pDisk, fDelete));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        PVDIMAGE pImage = pDisk->pLast;
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_NOT_OPENED);
        unsigned uOpenFlags = pImage->Backend->pfnGetOpenFlags(pImage->pvBackendData);
        /* Remove image from list of opened images. */
        vdRemoveImageFromList(pDisk, pImage);
        /* Close (and optionally delete) image. */
        rc = pImage->Backend->pfnClose(pImage->pvBackendData, fDelete);
        /* Free remaining resources related to the image. */
        if (pImage->hPlugin != NIL_RTLDRMOD)
        {
            RTLdrClose(pImage->hPlugin);
            pImage->hPlugin = NIL_RTLDRMOD;
        }
        RTStrFree(pImage->pszFilename);
        RTMemFree(pImage);

        pImage = pDisk->pLast;
        if (!pImage)
            break;

        /* If disk was previously in read/write mode, make sure it will stay
         * like this (if possible) after closing this image. Set the open flags
         * accordingly. */
        if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY))
        {
            uOpenFlags = pImage->Backend->pfnGetOpenFlags(pImage->pvBackendData);
            uOpenFlags &= ~ VD_OPEN_FLAGS_READONLY;
            rc = pImage->Backend->pfnSetOpenFlags(pImage->pvBackendData, uOpenFlags);
        }

        int rc2;

        /* Cache disk information. */
        pDisk->cbSize = pImage->Backend->pfnGetSize(pImage->pvBackendData);

        /* Cache PCHS geometry. */
        rc2 = pImage->Backend->pfnGetPCHSGeometry(pImage->pvBackendData,
                                                 &pDisk->PCHSGeometry);
        if (RT_FAILURE(rc2))
        {
            pDisk->PCHSGeometry.cCylinders = 0;
            pDisk->PCHSGeometry.cHeads = 0;
            pDisk->PCHSGeometry.cSectors = 0;
        }
        else
        {
            /* Make sure the PCHS geometry is properly clipped. */
            pDisk->PCHSGeometry.cCylinders = RT_MIN(pDisk->PCHSGeometry.cCylinders, 16383);
            pDisk->PCHSGeometry.cHeads = RT_MIN(pDisk->PCHSGeometry.cHeads, 16);
            pDisk->PCHSGeometry.cSectors = RT_MIN(pDisk->PCHSGeometry.cSectors, 63);
        }

        /* Cache LCHS geometry. */
        rc2 = pImage->Backend->pfnGetLCHSGeometry(pImage->pvBackendData,
                                                  &pDisk->LCHSGeometry);
        if (RT_FAILURE(rc2))
        {
            pDisk->LCHSGeometry.cCylinders = 0;
            pDisk->LCHSGeometry.cHeads = 0;
            pDisk->LCHSGeometry.cSectors = 0;
        }
        else
        {
            /* Make sure the LCHS geometry is properly clipped. */
            pDisk->LCHSGeometry.cCylinders = RT_MIN(pDisk->LCHSGeometry.cCylinders, 1024);
            pDisk->LCHSGeometry.cHeads = RT_MIN(pDisk->LCHSGeometry.cHeads, 255);
            pDisk->LCHSGeometry.cSectors = RT_MIN(pDisk->LCHSGeometry.cSectors, 63);
        }
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Closes all opened image files in HDD container.
 *
 * @returns VBox status code.
 * @param   pDisk           Pointer to HDD container.
 */
VBOXDDU_DECL(int) VDCloseAll(PVBOXHDD pDisk)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p\n", pDisk));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        PVDIMAGE pImage = pDisk->pLast;
        while (VALID_PTR(pImage))
        {
            PVDIMAGE pPrev = pImage->pPrev;
            /* Remove image from list of opened images. */
            vdRemoveImageFromList(pDisk, pImage);
            /* Close image. */
            int rc2 = pImage->Backend->pfnClose(pImage->pvBackendData, false);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
            /* Free remaining resources related to the image. */
            if (pImage->hPlugin != NIL_RTLDRMOD)
            {
                RTLdrClose(pImage->hPlugin);
                pImage->hPlugin = NIL_RTLDRMOD;
            }
            RTStrFree(pImage->pszFilename);
            RTMemFree(pImage);
            pImage = pPrev;
        }
        Assert(!VALID_PTR(pDisk->pLast));
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Read data from virtual HDD.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_NOT_OPENED if no image is opened in HDD container.
 * @param   pDisk           Pointer to HDD container.
 * @param   uOffset         Offset of first reading byte from start of disk.
 * @param   pvBuf           Pointer to buffer for reading data.
 * @param   cbRead          Number of bytes to read.
 */
VBOXDDU_DECL(int) VDRead(PVBOXHDD pDisk, uint64_t uOffset, void *pvBuf,
                         size_t cbRead)
{
    int rc;

    LogFlowFunc(("pDisk=%#p uOffset=%llu pvBuf=%p cbRead=%zu\n",
                 pDisk, uOffset, pvBuf, cbRead));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pvBuf),
                           ("pvBuf=%#p\n", pvBuf),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(cbRead,
                           ("cbRead=%zu\n", cbRead),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(uOffset + cbRead <= pDisk->cbSize,
                           ("uOffset=%llu cbRead=%zu pDisk->cbSize=%llu\n",
                            uOffset, cbRead, pDisk->cbSize),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = pDisk->pLast;
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_NOT_OPENED);

        rc = vdReadHelper(pDisk, pImage, uOffset, pvBuf, cbRead);
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Write data to virtual HDD.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_NOT_OPENED if no image is opened in HDD container.
 * @param   pDisk           Pointer to HDD container.
 * @param   uOffset         Offset of the first byte being
 *                          written from start of disk.
 * @param   pvBuf           Pointer to buffer for writing data.
 * @param   cbWrite         Number of bytes to write.
 */
VBOXDDU_DECL(int) VDWrite(PVBOXHDD pDisk, uint64_t uOffset, const void *pvBuf,
                          size_t cbWrite)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p uOffset=%llu pvBuf=%p cbWrite=%zu\n",
                 pDisk, uOffset, pvBuf, cbWrite));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pvBuf),
                           ("pvBuf=%#p\n", pvBuf),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(cbWrite,
                           ("cbWrite=%zu\n", cbWrite),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(uOffset + cbWrite <= pDisk->cbSize,
                           ("uOffset=%llu cbWrite=%zu pDisk->cbSize=%llu\n",
                            uOffset, cbWrite, pDisk->cbSize),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = pDisk->pLast;
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_NOT_OPENED);

        vdSetModifiedFlag(pDisk);
        rc = vdWriteHelper(pDisk, pImage, uOffset, pvBuf, cbWrite);
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Make sure the on disk representation of a virtual HDD is up to date.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_NOT_OPENED if no image is opened in HDD container.
 * @param   pDisk           Pointer to HDD container.
 */
VBOXDDU_DECL(int) VDFlush(PVBOXHDD pDisk)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p\n", pDisk));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        PVDIMAGE pImage = pDisk->pLast;
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_NOT_OPENED);

        vdResetModifiedFlag(pDisk);
        rc = pImage->Backend->pfnFlush(pImage->pvBackendData);
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Get number of opened images in HDD container.
 *
 * @returns Number of opened images for HDD container. 0 if no images have been opened.
 * @param   pDisk           Pointer to HDD container.
 */
VBOXDDU_DECL(unsigned) VDGetCount(PVBOXHDD pDisk)
{
    unsigned cImages;

    LogFlowFunc(("pDisk=%#p\n", pDisk));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, cImages = 0);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        cImages = pDisk->cImages;
    } while (0);

    LogFlowFunc(("returns %u\n", cImages));
    return cImages;
}

/**
 * Get read/write mode of HDD container.
 *
 * @returns Virtual disk ReadOnly status.
 * @returns true if no image is opened in HDD container.
 * @param   pDisk           Pointer to HDD container.
 */
VBOXDDU_DECL(bool) VDIsReadOnly(PVBOXHDD pDisk)
{
    bool fReadOnly;

    LogFlowFunc(("pDisk=%#p\n", pDisk));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, fReadOnly = false);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        PVDIMAGE pImage = pDisk->pLast;
        AssertPtrBreakStmt(pImage, fReadOnly = true);

        unsigned uOpenFlags;
        uOpenFlags = pDisk->pLast->Backend->pfnGetOpenFlags(pDisk->pLast->pvBackendData);
        fReadOnly = !!(uOpenFlags & VD_OPEN_FLAGS_READONLY);
    } while (0);

    LogFlowFunc(("returns %d\n", fReadOnly));
    return fReadOnly;
}

/**
 * Get total capacity of an image in HDD container.
 *
 * @returns Virtual disk size in bytes.
 * @returns 0 if no image with specified number was not opened.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counds from 0. 0 is always base image of container.
 */
VBOXDDU_DECL(uint64_t) VDGetSize(PVBOXHDD pDisk, unsigned nImage)
{
    uint64_t cbSize;

    LogFlowFunc(("pDisk=%#p nImage=%u\n", pDisk, nImage));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, cbSize = 0);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, cbSize = 0);
        cbSize = pImage->Backend->pfnGetSize(pImage->pvBackendData);
    } while (0);

    LogFlowFunc(("returns %llu\n", cbSize));
    return cbSize;
}

/**
 * Get total file size of an image in HDD container.
 *
 * @returns Virtual disk size in bytes.
 * @returns 0 if no image is opened in HDD container.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 */
VBOXDDU_DECL(uint64_t) VDGetFileSize(PVBOXHDD pDisk, unsigned nImage)
{
    uint64_t cbSize;

    LogFlowFunc(("pDisk=%#p nImage=%u\n", pDisk, nImage));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, cbSize = 0);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, cbSize = 0);
        cbSize = pImage->Backend->pfnGetFileSize(pImage->pvBackendData);
    } while (0);

    LogFlowFunc(("returns %llu\n", cbSize));
    return cbSize;
}

/**
 * Get virtual disk PCHS geometry stored in HDD container.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @returns VERR_VDI_GEOMETRY_NOT_SET if no geometry present in the HDD container.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pPCHSGeometry   Where to store PCHS geometry. Not NULL.
 */
VBOXDDU_DECL(int) VDGetPCHSGeometry(PVBOXHDD pDisk, unsigned nImage,
                                    PPDMMEDIAGEOMETRY pPCHSGeometry)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p nImage=%u pPCHSGeometry=%#p\n",
                 pDisk, nImage, pPCHSGeometry));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pPCHSGeometry),
                           ("pPCHSGeometry=%#p\n", pPCHSGeometry),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        if (pImage == pDisk->pLast)
        {
            /* Use cached information if possible. */
            if (pDisk->PCHSGeometry.cCylinders != 0)
                *pPCHSGeometry = pDisk->PCHSGeometry;
            else
                rc = VERR_VDI_GEOMETRY_NOT_SET;
        }
        else
            rc = pImage->Backend->pfnGetPCHSGeometry(pImage->pvBackendData,
                                                     pPCHSGeometry);
    } while (0);

    LogFlowFunc(("%s: %Rrc (PCHS=%u/%u/%u)\n", __FUNCTION__, rc,
                 pDisk->PCHSGeometry.cCylinders, pDisk->PCHSGeometry.cHeads,
                 pDisk->PCHSGeometry.cSectors));
    return rc;
}

/**
 * Store virtual disk PCHS geometry in HDD container.
 *
 * Note that in case of unrecoverable error all images in HDD container will be closed.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @returns VERR_VDI_GEOMETRY_NOT_SET if no geometry present in the HDD container.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pPCHSGeometry   Where to load PCHS geometry from. Not NULL.
 */
VBOXDDU_DECL(int) VDSetPCHSGeometry(PVBOXHDD pDisk, unsigned nImage,
                                    PCPDMMEDIAGEOMETRY pPCHSGeometry)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p nImage=%u pPCHSGeometry=%#p PCHS=%u/%u/%u\n",
                 pDisk, nImage, pPCHSGeometry, pPCHSGeometry->cCylinders,
                 pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(   VALID_PTR(pPCHSGeometry)
                           && pPCHSGeometry->cCylinders <= 16383
                           && pPCHSGeometry->cHeads <= 16
                           && pPCHSGeometry->cSectors <= 63,
                           ("pPCHSGeometry=%#p PCHS=%u/%u/%u\n", pPCHSGeometry,
                            pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads,
                            pPCHSGeometry->cSectors),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        if (pImage == pDisk->pLast)
        {
            if (    pPCHSGeometry->cCylinders != pDisk->PCHSGeometry.cCylinders
                ||  pPCHSGeometry->cHeads != pDisk->PCHSGeometry.cHeads
                ||  pPCHSGeometry->cSectors != pDisk->PCHSGeometry.cSectors)
            {
                /* Only update geometry if it is changed. Avoids similar checks
                 * in every backend. Most of the time the new geometry is set
                 * to the previous values, so no need to go through the hassle
                 * of updating an image which could be opened in read-only mode
                 * right now. */
                rc = pImage->Backend->pfnSetPCHSGeometry(pImage->pvBackendData,
                                                         pPCHSGeometry);

                /* Cache new geometry values in any case. */
                int rc2 = pImage->Backend->pfnGetPCHSGeometry(pImage->pvBackendData,
                                                              &pDisk->PCHSGeometry);
                if (RT_FAILURE(rc2))
                {
                    pDisk->PCHSGeometry.cCylinders = 0;
                    pDisk->PCHSGeometry.cHeads = 0;
                    pDisk->PCHSGeometry.cSectors = 0;
                }
                else
                {
                    /* Make sure the CHS geometry is properly clipped. */
                    pDisk->PCHSGeometry.cCylinders = RT_MIN(pDisk->PCHSGeometry.cCylinders, 1024);
                    pDisk->PCHSGeometry.cHeads = RT_MIN(pDisk->PCHSGeometry.cHeads, 255);
                    pDisk->PCHSGeometry.cSectors = RT_MIN(pDisk->PCHSGeometry.cSectors, 63);
                }
            }
        }
        else
        {
            PDMMEDIAGEOMETRY PCHS;
            rc = pImage->Backend->pfnGetPCHSGeometry(pImage->pvBackendData,
                                                     &PCHS);
            if (    RT_FAILURE(rc)
                ||  pPCHSGeometry->cCylinders != PCHS.cCylinders
                ||  pPCHSGeometry->cHeads != PCHS.cHeads
                ||  pPCHSGeometry->cSectors != PCHS.cSectors)
            {
                /* Only update geometry if it is changed. Avoids similar checks
                 * in every backend. Most of the time the new geometry is set
                 * to the previous values, so no need to go through the hassle
                 * of updating an image which could be opened in read-only mode
                 * right now. */
                rc = pImage->Backend->pfnSetPCHSGeometry(pImage->pvBackendData,
                                                         pPCHSGeometry);
            }
        }
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Get virtual disk LCHS geometry stored in HDD container.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @returns VERR_VDI_GEOMETRY_NOT_SET if no geometry present in the HDD container.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pLCHSGeometry   Where to store LCHS geometry. Not NULL.
 */
VBOXDDU_DECL(int) VDGetLCHSGeometry(PVBOXHDD pDisk, unsigned nImage,
                                    PPDMMEDIAGEOMETRY pLCHSGeometry)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p nImage=%u pLCHSGeometry=%#p\n",
                 pDisk, nImage, pLCHSGeometry));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pLCHSGeometry),
                           ("pLCHSGeometry=%#p\n", pLCHSGeometry),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        if (pImage == pDisk->pLast)
        {
            /* Use cached information if possible. */
            if (pDisk->LCHSGeometry.cCylinders != 0)
                *pLCHSGeometry = pDisk->LCHSGeometry;
            else
                rc = VERR_VDI_GEOMETRY_NOT_SET;
        }
        else
            rc = pImage->Backend->pfnGetLCHSGeometry(pImage->pvBackendData,
                                                     pLCHSGeometry);
    } while (0);

    LogFlowFunc((": %Rrc (LCHS=%u/%u/%u)\n", rc,
                 pDisk->LCHSGeometry.cCylinders, pDisk->LCHSGeometry.cHeads,
                 pDisk->LCHSGeometry.cSectors));
    return rc;
}

/**
 * Store virtual disk LCHS geometry in HDD container.
 *
 * Note that in case of unrecoverable error all images in HDD container will be closed.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @returns VERR_VDI_GEOMETRY_NOT_SET if no geometry present in the HDD container.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pLCHSGeometry   Where to load LCHS geometry from. Not NULL.
 */
VBOXDDU_DECL(int) VDSetLCHSGeometry(PVBOXHDD pDisk, unsigned nImage,
                                    PCPDMMEDIAGEOMETRY pLCHSGeometry)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p nImage=%u pLCHSGeometry=%#p LCHS=%u/%u/%u\n",
                 pDisk, nImage, pLCHSGeometry, pLCHSGeometry->cCylinders,
                 pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(   VALID_PTR(pLCHSGeometry)
                           && pLCHSGeometry->cCylinders <= 1024
                           && pLCHSGeometry->cHeads <= 255
                           && pLCHSGeometry->cSectors <= 63,
                           ("pLCHSGeometry=%#p LCHS=%u/%u/%u\n", pLCHSGeometry,
                            pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads,
                            pLCHSGeometry->cSectors),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        if (pImage == pDisk->pLast)
        {
            if (    pLCHSGeometry->cCylinders != pDisk->LCHSGeometry.cCylinders
                ||  pLCHSGeometry->cHeads != pDisk->LCHSGeometry.cHeads
                ||  pLCHSGeometry->cSectors != pDisk->LCHSGeometry.cSectors)
            {
                /* Only update geometry if it is changed. Avoids similar checks
                 * in every backend. Most of the time the new geometry is set
                 * to the previous values, so no need to go through the hassle
                 * of updating an image which could be opened in read-only mode
                 * right now. */
                rc = pImage->Backend->pfnSetLCHSGeometry(pImage->pvBackendData,
                                                         pLCHSGeometry);

                /* Cache new geometry values in any case. */
                int rc2 = pImage->Backend->pfnGetLCHSGeometry(pImage->pvBackendData,
                                                              &pDisk->LCHSGeometry);
                if (RT_FAILURE(rc2))
                {
                    pDisk->LCHSGeometry.cCylinders = 0;
                    pDisk->LCHSGeometry.cHeads = 0;
                    pDisk->LCHSGeometry.cSectors = 0;
                }
                else
                {
                    /* Make sure the CHS geometry is properly clipped. */
                    pDisk->LCHSGeometry.cCylinders = RT_MIN(pDisk->LCHSGeometry.cCylinders, 1024);
                    pDisk->LCHSGeometry.cHeads = RT_MIN(pDisk->LCHSGeometry.cHeads, 255);
                    pDisk->LCHSGeometry.cSectors = RT_MIN(pDisk->LCHSGeometry.cSectors, 63);
                }
            }
        }
        else
        {
            PDMMEDIAGEOMETRY LCHS;
            rc = pImage->Backend->pfnGetLCHSGeometry(pImage->pvBackendData,
                                                     &LCHS);
            if (    RT_FAILURE(rc)
                ||  pLCHSGeometry->cCylinders != LCHS.cCylinders
                ||  pLCHSGeometry->cHeads != LCHS.cHeads
                ||  pLCHSGeometry->cSectors != LCHS.cSectors)
            {
                /* Only update geometry if it is changed. Avoids similar checks
                 * in every backend. Most of the time the new geometry is set
                 * to the previous values, so no need to go through the hassle
                 * of updating an image which could be opened in read-only mode
                 * right now. */
                rc = pImage->Backend->pfnSetLCHSGeometry(pImage->pvBackendData,
                                                         pLCHSGeometry);
            }
        }
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Get version of image in HDD container.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   puVersion       Where to store the image version.
 */
VBOXDDU_DECL(int) VDGetVersion(PVBOXHDD pDisk, unsigned nImage,
                               unsigned *puVersion)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p nImage=%u puVersion=%#p\n",
                 pDisk, nImage, puVersion));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(puVersion),
                           ("puVersion=%#p\n", puVersion),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        *puVersion = pImage->Backend->pfnGetVersion(pImage->pvBackendData);
    } while (0);

    LogFlowFunc(("returns %Rrc uVersion=%#x\n", rc, *puVersion));
    return rc;
}

/**
 * Get type of image in HDD container.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   penmType        Where to store the image type.
 */
VBOXDDU_DECL(int) VDGetImageType(PVBOXHDD pDisk, unsigned nImage,
                                 PVDIMAGETYPE penmType)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p nImage=%u penmType=%#p\n",
                 pDisk, nImage, penmType));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(penmType),
                           ("penmType=%#p\n", penmType),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        if (    pImage->enmImageType >= VD_IMAGE_TYPE_FIRST
            &&  pImage->enmImageType <= VD_IMAGE_TYPE_DIFF)
        {
            *penmType = pImage->enmImageType;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_VDI_INVALID_TYPE;
    } while (0);

    LogFlowFunc(("returns %Rrc uenmType=%u\n", rc, *penmType));
    return rc;
}


/**
 * List the capabilities of image backend in HDD container.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to the HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pbackendInfo    Where to store the backend information.
 */
VBOXDDU_DECL(int) VDBackendInfoSingle(PVBOXHDD pDisk, unsigned nImage,
                                      PVDBACKENDINFO pBackendInfo)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p nImage=%u penmType=%#p\n",
                 pDisk, nImage, pBackendInfo));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pBackendInfo),
                           ("pBackendInfo=%#p\n", pBackendInfo),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        if (    pImage->enmImageType >= VD_IMAGE_TYPE_FIRST
            &&  pImage->enmImageType <= VD_IMAGE_TYPE_DIFF)
        {
            pBackendInfo->pszBackend = RTStrDup(pImage->Backend->pszBackendName);
            pBackendInfo->uBackendCaps = pImage->Backend->uBackendCaps;
            pBackendInfo->papszFileExtensions = pImage->Backend->papszFileExtensions;
            pBackendInfo->paConfigInfo = pImage->Backend->paConfigInfo;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_VDI_INVALID_TYPE;
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Get flags of image in HDD container.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   puImageFlags    Where to store the image flags.
 */
VBOXDDU_DECL(int) VDGetImageFlags(PVBOXHDD pDisk, unsigned nImage,
                                  unsigned *puImageFlags)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p nImage=%u puImageFlags=%#p\n",
                 pDisk, nImage, puImageFlags));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(puImageFlags),
                           ("puImageFlags=%#p\n", puImageFlags),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        *puImageFlags = pImage->Backend->pfnGetImageFlags(pImage->pvBackendData);
    } while (0);

    LogFlowFunc(("returns %Rrc uImageFlags=%#x\n", rc, *puImageFlags));
    return rc;
}

/**
 * Get open flags of image in HDD container.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   puOpenFlags     Where to store the image open flags.
 */
VBOXDDU_DECL(int) VDGetOpenFlags(PVBOXHDD pDisk, unsigned nImage,
                                 unsigned *puOpenFlags)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p nImage=%u puOpenFlags=%#p\n",
                 pDisk, nImage, puOpenFlags));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(puOpenFlags),
                           ("puOpenFlags=%#p\n", puOpenFlags),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        *puOpenFlags = pImage->Backend->pfnGetOpenFlags(pImage->pvBackendData);
    } while (0);

    LogFlowFunc(("returns %Rrc uOpenFlags=%#x\n", rc, *puOpenFlags));
    return rc;
}

/**
 * Set open flags of image in HDD container.
 * This operation may cause file locking changes and/or files being reopened.
 * Note that in case of unrecoverable error all images in HDD container will be closed.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   uOpenFlags      Image file open mode, see VD_OPEN_FLAGS_* constants.
 */
VBOXDDU_DECL(int) VDSetOpenFlags(PVBOXHDD pDisk, unsigned nImage,
                                 unsigned uOpenFlags)
{
    int rc;

    LogFlowFunc(("pDisk=%#p uOpenFlags=%#u\n", pDisk, uOpenFlags));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt((uOpenFlags & ~VD_OPEN_FLAGS_MASK) == 0,
                           ("uOpenFlags=%#x\n", uOpenFlags),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        rc = pImage->Backend->pfnSetOpenFlags(pImage->pvBackendData,
                                              uOpenFlags);
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Get base filename of image in HDD container. Some image formats use
 * other filenames as well, so don't use this for anything but informational
 * purposes.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @returns VERR_BUFFER_OVERFLOW if pszFilename buffer too small to hold filename.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pszFilename     Where to store the image file name.
 * @param   cbFilename      Size of buffer pszFilename points to.
 */
VBOXDDU_DECL(int) VDGetFilename(PVBOXHDD pDisk, unsigned nImage,
                                char *pszFilename, unsigned cbFilename)
{
    int rc;

    LogFlowFunc(("pDisk=%#p nImage=%u pszFilename=%#p cbFilename=%u\n",
                 pDisk, nImage, pszFilename, cbFilename));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pszFilename) && *pszFilename,
                           ("pszFilename=%#p \"%s\"\n", pszFilename, pszFilename),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(cbFilename,
                           ("cbFilename=%u\n", cbFilename),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        size_t cb = strlen(pImage->pszFilename);
        if (cb <= cbFilename)
        {
            strcpy(pszFilename, pImage->pszFilename);
            rc = VINF_SUCCESS;
        }
        else
        {
            strncpy(pszFilename, pImage->pszFilename, cbFilename - 1);
            pszFilename[cbFilename - 1] = '\0';
            rc = VERR_BUFFER_OVERFLOW;
        }
    } while (0);

    LogFlowFunc(("returns %Rrc, pszFilename=\"%s\"\n", rc, pszFilename));
    return rc;
}

/**
 * Get the comment line of image in HDD container.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @returns VERR_BUFFER_OVERFLOW if pszComment buffer too small to hold comment text.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pszComment      Where to store the comment string of image. NULL is ok.
 * @param   cbComment       The size of pszComment buffer. 0 is ok.
 */
VBOXDDU_DECL(int) VDGetComment(PVBOXHDD pDisk, unsigned nImage,
                               char *pszComment, unsigned cbComment)
{
    int rc;

    LogFlowFunc(("pDisk=%#p nImage=%u pszComment=%#p cbComment=%u\n",
                 pDisk, nImage, pszComment, cbComment));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pszComment),
                           ("pszComment=%#p \"%s\"\n", pszComment, pszComment),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(cbComment,
                           ("cbComment=%u\n", cbComment),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        rc = pImage->Backend->pfnGetComment(pImage->pvBackendData, pszComment,
                                            cbComment);
    } while (0);

    LogFlowFunc(("returns %Rrc, pszComment=\"%s\"\n", rc, pszComment));
    return rc;
}

/**
 * Changes the comment line of image in HDD container.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pszComment      New comment string (UTF-8). NULL is allowed to reset the comment.
 */
VBOXDDU_DECL(int) VDSetComment(PVBOXHDD pDisk, unsigned nImage,
                               const char *pszComment)
{
    int rc;

    LogFlowFunc(("pDisk=%#p nImage=%u pszComment=%#p \"%s\"\n",
                 pDisk, nImage, pszComment, pszComment));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pszComment) || pszComment == NULL,
                           ("pszComment=%#p \"%s\"\n", pszComment, pszComment),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        rc = pImage->Backend->pfnSetComment(pImage->pvBackendData, pszComment);
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * Get UUID of image in HDD container.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pUuid           Where to store the image creation UUID.
 */
VBOXDDU_DECL(int) VDGetUuid(PVBOXHDD pDisk, unsigned nImage, PRTUUID pUuid)
{
    int rc;

    LogFlowFunc(("pDisk=%#p nImage=%u pUuid=%#p\n", pDisk, nImage, pUuid));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pUuid),
                           ("pUuid=%#p\n", pUuid),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        rc = pImage->Backend->pfnGetUuid(pImage->pvBackendData, pUuid);
    } while (0);

    LogFlowFunc(("returns %Rrc, Uuid={%RTuuid}\n", rc, pUuid));
    return rc;
}

/**
 * Set the image's UUID. Should not be used by normal applications.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pUuid           New UUID of the image. If NULL, a new UUID is created.
 */
VBOXDDU_DECL(int) VDSetUuid(PVBOXHDD pDisk, unsigned nImage, PCRTUUID pUuid)
{
    int rc;

    LogFlowFunc(("pDisk=%#p nImage=%u pUuid=%#p {%RTuuid}\n",
                 pDisk, nImage, pUuid, pUuid));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        AssertMsgBreakStmt(VALID_PTR(pUuid) || pUuid == NULL,
                           ("pUuid=%#p\n", pUuid),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        RTUUID Uuid;
        if (!pUuid)
        {
            RTUuidCreate(&Uuid);
            pUuid = &Uuid;
        }
        rc = pImage->Backend->pfnSetUuid(pImage->pvBackendData, pUuid);
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Get last modification UUID of image in HDD container.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pUuid           Where to store the image modification UUID.
 */
VBOXDDU_DECL(int) VDGetModificationUuid(PVBOXHDD pDisk, unsigned nImage, PRTUUID pUuid)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p nImage=%u pUuid=%#p\n", pDisk, nImage, pUuid));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pUuid),
                           ("pUuid=%#p\n", pUuid),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        rc = pImage->Backend->pfnGetModificationUuid(pImage->pvBackendData,
                                                     pUuid);
    } while (0);

    LogFlowFunc(("returns %Rrc, Uuid={%RTuuid}\n", rc, pUuid));
    return rc;
}

/**
 * Set the image's last modification UUID. Should not be used by normal applications.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pUuid           New modification UUID of the image. If NULL, a new UUID is created.
 */
VBOXDDU_DECL(int) VDSetModificationUuid(PVBOXHDD pDisk, unsigned nImage, PCRTUUID pUuid)
{
    int rc;

    LogFlowFunc(("pDisk=%#p nImage=%u pUuid=%#p {%RTuuid}\n",
                 pDisk, nImage, pUuid, pUuid));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pUuid) || pUuid == NULL,
                           ("pUuid=%#p\n", pUuid),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        RTUUID Uuid;
        if (!pUuid)
        {
            RTUuidCreate(&Uuid);
            pUuid = &Uuid;
        }
        rc = pImage->Backend->pfnSetModificationUuid(pImage->pvBackendData,
                                                     pUuid);
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Get parent UUID of image in HDD container.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pUuid           Where to store the parent image UUID.
 */
VBOXDDU_DECL(int) VDGetParentUuid(PVBOXHDD pDisk, unsigned nImage,
                                  PRTUUID pUuid)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p nImage=%u pUuid=%#p\n", pDisk, nImage, pUuid));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pUuid),
                           ("pUuid=%#p\n", pUuid),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        rc = pImage->Backend->pfnGetParentUuid(pImage->pvBackendData, pUuid);
    } while (0);

    LogFlowFunc(("returns %Rrc, Uuid={%RTuuid}\n", rc, pUuid));
    return rc;
}

/**
 * Set the image's parent UUID. Should not be used by normal applications.
 *
 * @returns VBox status code.
 * @param   pDisk           Pointer to HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pUuid           New parent UUID of the image. If NULL, a new UUID is created.
 */
VBOXDDU_DECL(int) VDSetParentUuid(PVBOXHDD pDisk, unsigned nImage,
                                  PCRTUUID pUuid)
{
    int rc;

    LogFlowFunc(("pDisk=%#p nImage=%u pUuid=%#p {%RTuuid}\n",
                 pDisk, nImage, pUuid, pUuid));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pUuid) || pUuid == NULL,
                           ("pUuid=%#p\n", pUuid),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        RTUUID Uuid;
        if (!pUuid)
        {
            RTUuidCreate(&Uuid);
            pUuid = &Uuid;
        }
        rc = pImage->Backend->pfnSetParentUuid(pImage->pvBackendData, pUuid);
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * Debug helper - dumps all opened images in HDD container into the log file.
 *
 * @param   pDisk           Pointer to HDD container.
 */
VBOXDDU_DECL(void) VDDumpImages(PVBOXHDD pDisk)
{
    do
    {
        /* sanity check */
        AssertPtrBreak(pDisk);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        RTLogPrintf("--- Dumping VD Disk, Images=%u\n", pDisk->cImages);
        for (PVDIMAGE pImage = pDisk->pBase; pImage; pImage = pImage->pNext)
        {
            RTLogPrintf("Dumping VD image \"%s\" (Backend=%s)\n",
                        pImage->pszFilename, pImage->Backend->pszBackendName);
            pImage->Backend->pfnDump(pImage->pvBackendData);
        }
    } while (0);
}

/**
 * Query if asynchronous operations are supported for this disk.
 *
 * @returns VBox status code.
 * @returns VERR_VDI_IMAGE_NOT_FOUND if image with specified number was not opened.
 * @param   pDisk           Pointer to the HDD container.
 * @param   nImage          Image number, counts from 0. 0 is always base image of container.
 * @param   pfAIOSupported  Where to store if async IO is supported.
 */
VBOXDDU_DECL(int) VDImageIsAsyncIOSupported(PVBOXHDD pDisk, unsigned nImage, bool *pfAIOSupported)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pDisk=%#p nImage=%u pfAIOSupported=%#p\n", pDisk, nImage, pfAIOSupported));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(VALID_PTR(pfAIOSupported),
                           ("pfAIOSupported=%#p\n", pfAIOSupported),
                           rc = VERR_INVALID_PARAMETER);

        PVDIMAGE pImage = vdGetImageByNumber(pDisk, nImage);
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_IMAGE_NOT_FOUND);

        if (pImage->Backend->uBackendCaps & VD_CAP_ASYNC)
            *pfAIOSupported = pImage->Backend->pfnIsAsyncIOSupported(pImage->pvBackendData);
        else
            *pfAIOSupported = false;
    } while (0);

    LogFlowFunc(("returns %Rrc, fAIOSupported=%u\n", rc, *pfAIOSupported));
    return rc;
}

/**
 * Start a asynchronous read request.
 *
 * @returns VBox status code.
 * @param   pDisk           Pointer to the HDD container.
 * @param   uOffset         The offset of the virtual disk to read from.
 * @param   cbRead          How many bytes to read.
 * @param   paSeg           Pointer to an array of segments.
 * @param   cSeg            Number of segments in the array.
 * @param   pvUser          User data which is passed on completion
 */
VBOXDDU_DECL(int) VDAsyncRead(PVBOXHDD pDisk, uint64_t uOffset, size_t cbRead,
                              PPDMDATASEG paSeg, unsigned cSeg,
                              void *pvUser)
{
    int rc = VERR_VDI_BLOCK_FREE;

    LogFlowFunc(("pDisk=%#p uOffset=%llu paSeg=%p cSeg=%u cbRead=%zu\n",
                 pDisk, uOffset, paSeg, cSeg, cbRead));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(cbRead,
                           ("cbRead=%zu\n", cbRead),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(uOffset + cbRead <= pDisk->cbSize,
                           ("uOffset=%llu cbRead=%zu pDisk->cbSize=%llu\n",
                            uOffset, cbRead, pDisk->cbSize),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(VALID_PTR(paSeg),
                           ("paSeg=%#p\n", paSeg),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(cSeg,
                           ("cSeg=%zu\n", cSeg),
                           rc = VERR_INVALID_PARAMETER);


        PVDIMAGE pImage = pDisk->pLast;
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_NOT_OPENED);

        /* @todo: This does not work for images which do not have all meta data in memory. */
        for (PVDIMAGE pCurrImage = pImage;
             pCurrImage != NULL && rc == VERR_VDI_BLOCK_FREE;
             pCurrImage = pCurrImage->pPrev)
        {
            rc = pCurrImage->Backend->pfnAsyncRead(pCurrImage->pvBackendData,
                                                   uOffset, cbRead, paSeg, cSeg,
                                                   pvUser);
        }

        /* No image in the chain contains the data for the block. */
        if (rc == VERR_VDI_BLOCK_FREE)
        {
            for (unsigned i = 0; i < cSeg && (cbRead > 0); i++)
            {
                memset(paSeg[i].pvSeg, '\0', paSeg[i].cbSeg);
                cbRead -= paSeg[i].cbSeg;
            }
            /* Request finished without the need to enqueue a async I/O request. Tell caller. */
            rc = VINF_VDI_ASYNC_IO_FINISHED;
        }

    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * Start a asynchronous write request.
 *
 * @returns VBox status code.
 * @param   pDisk           Pointer to the HDD container.
 * @param   uOffset         The offset of the virtual disk to write to.
 * @param   cbWrtie         How many bytes to write.
 * @param   paSeg           Pointer to an array of segments.
 * @param   cSeg            Number of segments in the array.
 * @param   pvUser          User data which is passed on completion.
 */
VBOXDDU_DECL(int) VDAsyncWrite(PVBOXHDD pDisk, uint64_t uOffset, size_t cbWrite,
                               PPDMDATASEG paSeg, unsigned cSeg,
                               void *pvUser)
{
    int rc;

    LogFlowFunc(("pDisk=%#p uOffset=%llu paSeg=%p cSeg=%u cbWrite=%zu\n",
                 pDisk, uOffset, paSeg, cSeg, cbWrite));
    do
    {
        /* sanity check */
        AssertPtrBreakStmt(pDisk, rc = VERR_INVALID_PARAMETER);
        AssertMsg(pDisk->u32Signature == VBOXHDDDISK_SIGNATURE, ("u32Signature=%08x\n", pDisk->u32Signature));

        /* Check arguments. */
        AssertMsgBreakStmt(cbWrite,
                           ("cbWrite=%zu\n", cbWrite),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(uOffset + cbWrite <= pDisk->cbSize,
                           ("uOffset=%llu cbWrite=%zu pDisk->cbSize=%llu\n",
                            uOffset, cbWrite, pDisk->cbSize),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(VALID_PTR(paSeg),
                           ("paSeg=%#p\n", paSeg),
                           rc = VERR_INVALID_PARAMETER);
        AssertMsgBreakStmt(cSeg,
                           ("cSeg=%zu\n", cSeg),
                           rc = VERR_INVALID_PARAMETER);


        PVDIMAGE pImage = pDisk->pLast;
        AssertPtrBreakStmt(pImage, rc = VERR_VDI_NOT_OPENED);

        vdSetModifiedFlag(pDisk);
        rc = pImage->Backend->pfnAsyncWrite(pImage->pvBackendData,
                                            uOffset, cbWrite,
                                            paSeg, cSeg, pvUser);
    } while (0);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;

}

