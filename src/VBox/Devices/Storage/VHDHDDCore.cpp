/** @file
 * VHD Disk image, Core Code.
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
#define LOG_GROUP LOG_GROUP_VD_VHD
#include <VBox/VBoxHDD-Plugin.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <VBox/version.h>
#include <iprt/cdefs.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/uuid.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/rand.h>

#define VHD_RELATIVE_MAX_PATH 512
#define VHD_ABSOLUTE_MAX_PATH 512

#define VHD_SECTOR_SIZE 512
#define VHD_BLOCK_SIZE  (2 * _1M)

/* This is common to all VHD disk types and is located at the end of the image */
#pragma pack(1)
typedef struct VHDFooter
{
    char     Cookie[8];
    uint32_t Features;
    uint32_t Version;
    uint64_t DataOffset;
    uint32_t TimeStamp;
    uint8_t  CreatorApp[4];
    uint32_t CreatorVer;
    uint32_t CreatorOS;
    uint64_t OrigSize;
    uint64_t CurSize;
    uint16_t DiskGeometryCylinder;
    uint8_t  DiskGeometryHeads;
    uint8_t  DiskGeometrySectors;
    uint32_t DiskType;
    uint32_t Checksum;
    char     UniqueID[16];
    uint8_t  SavedState;
    uint8_t  Reserved[427];
} VHDFooter;
#pragma pack()

#define VHD_FOOTER_COOKIE "conectix"
#define VHD_FOOTER_COOKIE_SIZE 8

#define VHD_FOOTER_FEATURES_NOT_ENABLED   0
#define VHD_FOOTER_FEATURES_TEMPORARY     1
#define VHD_FOOTER_FEATURES_RESERVED      2

#define VHD_FOOTER_FILE_FORMAT_VERSION    0x00010000
#define VHD_FOOTER_DATA_OFFSET_FIXED      UINT64_C(0xffffffffffffffff)
#define VHD_FOOTER_DISK_TYPE_FIXED        2
#define VHD_FOOTER_DISK_TYPE_DYNAMIC      3
#define VHD_FOOTER_DISK_TYPE_DIFFERENCING 4

#define VHD_MAX_LOCATOR_ENTRIES           8
#define VHD_PLATFORM_CODE_NONE            0
#define VHD_PLATFORM_CODE_WI2R            0x57693272
#define VHD_PLATFORM_CODE_WI2K            0x5769326B
#define VHD_PLATFORM_CODE_W2RU            0x57327275
#define VHD_PLATFORM_CODE_W2KU            0x57326B75
#define VHD_PLATFORM_CODE_MAC             0x4D163220
#define VHD_PLATFORM_CODE_MACX            0x4D163258

/* Header for expanding disk images. */
#pragma pack(1)
typedef struct VHDParentLocatorEntry
{
    uint32_t u32Code;
    uint32_t u32DataSpace;
    uint32_t u32DataLength;
    uint32_t u32Reserved;
    uint64_t u64DataOffset;
} VHDPLE, *PVHDPLE;

typedef struct VHDDynamicDiskHeader
{
    char     Cookie[8];
    uint64_t DataOffset;
    uint64_t TableOffset;
    uint32_t HeaderVersion;
    uint32_t MaxTableEntries;
    uint32_t BlockSize;
    uint32_t Checksum;
    uint8_t  ParentUuid[16];
    uint32_t ParentTimeStamp;
    uint32_t Reserved0;
    uint16_t ParentUnicodeName[256];
    VHDPLE   ParentLocatorEntry[VHD_MAX_LOCATOR_ENTRIES];
    uint8_t  Reserved1[256];
} VHDDynamicDiskHeader;
#pragma pack()

#define VHD_DYNAMIC_DISK_HEADER_COOKIE "cxsparse"
#define VHD_DYNAMIC_DISK_HEADER_COOKIE_SIZE 8
#define VHD_DYNAMIC_DISK_HEADER_VERSION 0x00010000

/**
 * Complete VHD image data structure.
 */
typedef struct VHDIMAGE
{
    /** Base image name. */
    const char      *pszFilename;
#ifndef VBOX_WITH_NEW_IO_CODE
    /** Descriptor file if applicable. */
    RTFILE          File;
#else
    /** Opaque storage handle. */
    void           *pvStorage;
#endif

    /** Pointer to the per-disk VD interface list. */
    PVDINTERFACE      pVDIfsDisk;
    /** Error interface. */
    PVDINTERFACE      pInterfaceError;
    /** Error interface callback table. */
    PVDINTERFACEERROR pInterfaceErrorCallbacks;
#ifdef VBOX_WITH_NEW_IO_CODE
    /** Async I/O interface. */
    PVDINTERFACE        pInterfaceAsyncIO;
    /** Async I/O interface callbacks. */
    PVDINTERFACEASYNCIO pInterfaceAsyncIOCallbacks;
#endif

    /** Open flags passed by VBoxHD layer. */
    unsigned        uOpenFlags;
    /** Image flags defined during creation or determined during open. */
    unsigned        uImageFlags;
    /** Total size of the image. */
    uint64_t        cbSize;
    /** Original size of the image. */
    uint64_t        cbOrigSize;
    /** Physical geometry of this image. */
    PDMMEDIAGEOMETRY    PCHSGeometry;
    /** Logical geometry of this image. */
    PDMMEDIAGEOMETRY    LCHSGeometry;
    /** Image UUID. */
    RTUUID          ImageUuid;
    /** Parent image UUID. */
    RTUUID          ParentUuid;
    /** Parent's time stamp at the time of image creation. */
    uint32_t        u32ParentTimeStamp;
    /** Relative path to the parent image. */
    char            *pszParentFilename;
    /** File size on the host disk (including all headers). */
    uint64_t        FileSize;
    /** The Block Allocation Table. */
    uint32_t        *pBlockAllocationTable;
    /** Number of entries in the table. */
    uint32_t        cBlockAllocationTableEntries;
    /** Size of one data block. */
    uint32_t        cbDataBlock;
    /** Sectors per data block. */
    uint32_t        cSectorsPerDataBlock;
    /** Length of the sector bitmap in bytes. */
    uint32_t        cbDataBlockBitmap;
    /** A copy of the disk footer. */
    VHDFooter       vhdFooterCopy;
    /** Current end offset of the file (without the disk footer). */
    uint64_t        uCurrentEndOfFile;
    /** Start offset of data blocks. */
    uint64_t        uDataBlockStart;
    /** Size of the data block bitmap in sectors. */
    uint32_t        cDataBlockBitmapSectors;
    /** Start of the block allocation table. */
    uint64_t        uBlockAllocationTableOffset;
    /** Buffer to hold block's bitmap for bit search operations. */
    uint8_t         *pu8Bitmap;
    /** Offset to the next data structure (dynamic disk header). */
    uint64_t        u64DataOffset;
    /** Flag to force dynamic disk header update. */
    bool            fDynHdrNeedsUpdate;
} VHDIMAGE, *PVHDIMAGE;

/*******************************************************************************
*   Static Variables                                                           *
*******************************************************************************/

/** NULL-terminated array of supported file extensions. */
static const char *const s_apszVhdFileExtensions[] =
{
    "vhd",
    NULL
};

/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/

static int vhdFlush(void *pBackendData);
static int vhdLoadDynamicDisk(PVHDIMAGE pImage, uint64_t uDynamicDiskHeaderOffset);

static int vhdFileOpen(PVHDIMAGE pImage, bool fReadonly, bool fCreate)
{
    int rc = VINF_SUCCESS;

    AssertMsg(!(fReadonly && fCreate), ("Image can't be opened readonly while being created\n"));

#ifndef VBOX_WITH_NEW_IO_CODE
    uint32_t fOpen = fReadonly ? RTFILE_O_READ      | RTFILE_O_DENY_NONE
                               : RTFILE_O_READWRITE | RTFILE_O_DENY_WRITE;

    if (fCreate)
        fOpen |= RTFILE_O_CREATE;
    else
        fOpen |= RTFILE_O_OPEN;

    rc = RTFileOpen(&pImage->File, pImage->pszFilename, fOpen);
#else

    unsigned uOpenFlags = fReadonly ? VD_INTERFACEASYNCIO_OPEN_FLAGS_READONLY : 0;

    if (fCreate)
        uOpenFlags |= VD_INTERFACEASYNCIO_OPEN_FLAGS_CREATE;

    rc = pImage->pInterfaceAsyncIOCallbacks->pfnOpen(pImage->pInterfaceAsyncIO->pvUser,
                                                     pImage->pszFilename,
                                                     uOpenFlags,
                                                     NULL, &pImage->pvStorage);
#endif

    return rc;
}

static int vhdFileClose(PVHDIMAGE pImage)
{
    int rc = VINF_SUCCESS;

#ifndef VBOX_WITH_NEW_IO_CODE
    if (pImage->File != NIL_RTFILE)
        rc = RTFileClose(pImage->File);

    pImage->File = NIL_RTFILE;
#else
    if (pImage->pvStorage)
        rc = pImage->pInterfaceAsyncIOCallbacks->pfnClose(pImage->pInterfaceAsyncIO->pvUser,
                                                          pImage->pvStorage);

    pImage->pvStorage = NULL;
#endif

    return rc;
}

static int vhdFileFlushSync(PVHDIMAGE pImage)
{
    int rc = VINF_SUCCESS;

#ifndef VBOX_WITH_NEW_IO_CODE
    rc = RTFileFlush(pImage->File);
#else
    if (pImage->pvStorage)
        rc = pImage->pInterfaceAsyncIOCallbacks->pfnFlushSync(pImage->pInterfaceAsyncIO->pvUser,
                                                              pImage->pvStorage);
#endif

    return rc;
}

static int vhdFileGetSize(PVHDIMAGE pImage, uint64_t *pcbSize)
{
    int rc = VINF_SUCCESS;

#ifndef VBOX_WITH_NEW_IO_CODE
    rc = RTFileGetSize(pImage->File, pcbSize);
#else
    if (pImage->pvStorage)
        rc = pImage->pInterfaceAsyncIOCallbacks->pfnGetSize(pImage->pInterfaceAsyncIO->pvUser,
                                                            pImage->pvStorage,
                                                            pcbSize);
#endif

    return rc;

}

static int vhdFileSetSize(PVHDIMAGE pImage, uint64_t cbSize)
{
    int rc = VINF_SUCCESS;

#ifndef VBOX_WITH_NEW_IO_CODE
    rc = RTFileSetSize(pImage->File, cbSize);
#else
    if (pImage->pvStorage)
        rc = pImage->pInterfaceAsyncIOCallbacks->pfnSetSize(pImage->pInterfaceAsyncIO->pvUser,
                                                            pImage->pvStorage,
                                                            cbSize);
#endif

    return rc;
}


static int vhdFileWriteSync(PVHDIMAGE pImage, uint64_t off, const void *pcvBuf, size_t cbWrite, size_t *pcbWritten)
{
    int rc = VINF_SUCCESS;

#ifndef VBOX_WITH_NEW_IO_CODE
    rc = RTFileWriteAt(pImage->File, off, pcvBuf, cbWrite, pcbWritten);
#else
    if (pImage->pvStorage)
        rc = pImage->pInterfaceAsyncIOCallbacks->pfnWriteSync(pImage->pInterfaceAsyncIO->pvUser,
                                                              pImage->pvStorage,
                                                              off, cbWrite, pcvBuf,
                                                              pcbWritten);
#endif

    return rc;
}

static int vhdFileReadSync(PVHDIMAGE pImage, uint64_t off, void *pvBuf, size_t cbRead, size_t *pcbRead)
{
    int rc = VINF_SUCCESS;

#ifndef VBOX_WITH_NEW_IO_CODE
    rc = RTFileReadAt(pImage->File, off, pvBuf, cbRead, pcbRead);
#else
    if (pImage->pvStorage)
        rc = pImage->pInterfaceAsyncIOCallbacks->pfnReadSync(pImage->pInterfaceAsyncIO->pvUser,
                                                             pImage->pvStorage,
                                                             off, cbRead, pvBuf,
                                                             pcbRead);
#endif

    return rc;
}

static bool vhdFileOpened(PVHDIMAGE pImage)
{
#ifndef VBOX_WITH_NEW_IO_CODE
    return pImage->File != NIL_RTFILE;
#else
    return pImage->pvStorage != NULL;
#endif
}

/* 946684800 is a number of seconds between 1/1/1970 and 1/1/2000 */
#define VHD_TO_UNIX_EPOCH_SECONDS UINT64_C(946684800)

static uint32_t vhdRtTime2VhdTime(PCRTTIMESPEC pRtTimeStamp)
{
    uint64_t u64Seconds = RTTimeSpecGetSeconds(pRtTimeStamp);
    return (uint32_t)(u64Seconds - VHD_TO_UNIX_EPOCH_SECONDS);
}

static void vhdTime2RtTime(PRTTIMESPEC pRtTimeStamp, uint32_t u32VhdTimeStamp)
{
    RTTimeSpecSetSeconds(pRtTimeStamp, VHD_TO_UNIX_EPOCH_SECONDS + u32VhdTimeStamp);
}

/**
 * Internal: Allocates the block bitmap rounding up to the next 32bit or 64bit boundary.
 *           Can be freed with RTMemFree. The memory is zeroed.
 */
DECLINLINE(uint8_t *)vhdBlockBitmapAllocate(PVHDIMAGE pImage)
{
#ifdef RT_ARCH_AMD64
    return (uint8_t *)RTMemAllocZ(pImage->cbDataBlockBitmap + 8);
#else
    return (uint8_t *)RTMemAllocZ(pImage->cbDataBlockBitmap + 4);
#endif
}

/**
 * Internal: Compute and update header checksum.
 */
static uint32_t vhdChecksum(void *pHeader, uint32_t cbSize)
{
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < cbSize; i++)
        checksum += ((unsigned char *)pHeader)[i];
    return ~checksum;
}

static int vhdFilenameToUtf16(const char *pszFilename, uint16_t *pu16Buf, uint32_t cbBufSize, uint32_t *pcbActualSize, bool fBigEndian)
{
    int      rc;
    PRTUTF16 tmp16 = NULL;
    size_t   cTmp16Len;

    rc = RTStrToUtf16(pszFilename, &tmp16);
    if (RT_FAILURE(rc))
        goto out;
    cTmp16Len = RTUtf16Len(tmp16);
    if (cTmp16Len * sizeof(*tmp16) > cbBufSize)
    {
        rc = VERR_FILENAME_TOO_LONG;
        goto out;
    }

    if (fBigEndian)
        for (unsigned i = 0; i < cTmp16Len; i++)
            pu16Buf[i] = RT_H2BE_U16(tmp16[i]);
    else
        memcpy(pu16Buf, tmp16, cTmp16Len * sizeof(*tmp16));
    if (pcbActualSize)
        *pcbActualSize = (uint32_t)(cTmp16Len * sizeof(*tmp16));

out:
    if (tmp16)
        RTUtf16Free(tmp16);
    return rc;
}

/**
 * Internal: Update one locator entry.
 */
static int vhdLocatorUpdate(PVHDIMAGE pImage, PVHDPLE pLocator, const char *pszFilename)
{
    int      rc;
    uint32_t cb, cbMaxLen = RT_BE2H_U32(pLocator->u32DataSpace) * VHD_SECTOR_SIZE;
    void     *pvBuf = RTMemTmpAllocZ(cbMaxLen);
    char     *pszTmp;

    if (!pvBuf)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }

    switch (RT_BE2H_U32(pLocator->u32Code))
    {
        case VHD_PLATFORM_CODE_WI2R:
            /* Update plain relative name. */
            cb = (uint32_t)strlen(pszFilename);
            if (cb > cbMaxLen)
            {
                rc = VERR_FILENAME_TOO_LONG;
                goto out;
            }
            memcpy(pvBuf, pszFilename, cb);
            pLocator->u32DataLength = RT_H2BE_U32(cb);
            break;
        case VHD_PLATFORM_CODE_WI2K:
            /* Update plain absolute name. */
            rc = RTPathAbs(pszFilename, (char *)pvBuf, cbMaxLen);
            if (RT_FAILURE(rc))
                goto out;
            pLocator->u32DataLength = RT_H2BE_U32((uint32_t)strlen((const char *)pvBuf));
            break;
        case VHD_PLATFORM_CODE_W2RU:
            /* Update unicode relative name. */
            rc = vhdFilenameToUtf16(pszFilename, (uint16_t *)pvBuf, cbMaxLen, &cb, false);
            if (RT_FAILURE(rc))
                goto out;
            pLocator->u32DataLength = RT_H2BE_U32(cb);
            break;
        case VHD_PLATFORM_CODE_W2KU:
            /* Update unicode absolute name. */
            pszTmp = (char*)RTMemTmpAllocZ(cbMaxLen);
            if (!pvBuf)
            {
                rc = VERR_NO_MEMORY;
                goto out;
            }
            rc = RTPathAbs(pszFilename, pszTmp, cbMaxLen);
            if (RT_FAILURE(rc))
            {
                RTMemTmpFree(pszTmp);
                goto out;
            }
            rc = vhdFilenameToUtf16(pszTmp, (uint16_t *)pvBuf, cbMaxLen, &cb, false);
            RTMemTmpFree(pszTmp);
            if (RT_FAILURE(rc))
                goto out;
            pLocator->u32DataLength = RT_H2BE_U32(cb);
            break;
        default:
            rc = VERR_NOT_IMPLEMENTED;
            goto out;
    }
    rc = vhdFileWriteSync(pImage, RT_BE2H_U64(pLocator->u64DataOffset), pvBuf,
                          RT_BE2H_U32(pLocator->u32DataSpace) * VHD_SECTOR_SIZE, NULL);

out:
    if (pvBuf)
        RTMemTmpFree(pvBuf);
    return rc;
}

/**
 * Internal: Update dynamic disk header from VHDIMAGE.
 */
static int vhdDynamicHeaderUpdate(PVHDIMAGE pImage)
{
    VHDDynamicDiskHeader ddh;
    int rc, i;

    if (!pImage)
        return VERR_VD_NOT_OPENED;

    rc = vhdFileReadSync(pImage, pImage->u64DataOffset, &ddh, sizeof(ddh), NULL);
    if (RT_FAILURE(rc))
        return rc;
    if (memcmp(ddh.Cookie, VHD_DYNAMIC_DISK_HEADER_COOKIE, VHD_DYNAMIC_DISK_HEADER_COOKIE_SIZE) != 0)
        return VERR_VD_VHD_INVALID_HEADER;

    uint32_t u32Checksum = RT_BE2H_U32(ddh.Checksum);
    ddh.Checksum = 0;
    if (u32Checksum != vhdChecksum(&ddh, sizeof(ddh)))
        return VERR_VD_VHD_INVALID_HEADER;

    /* Update parent's timestamp. */
    ddh.ParentTimeStamp = RT_H2BE_U32(pImage->u32ParentTimeStamp);
    /* Update parent's filename. */
    if (pImage->pszParentFilename)
    {
        rc = vhdFilenameToUtf16(RTPathFilename(pImage->pszParentFilename),
             ddh.ParentUnicodeName, sizeof(ddh.ParentUnicodeName) - 1, NULL, true);
        if (RT_FAILURE(rc))
            return rc;
    }

    /* Update parent's locators. */
    for (i = 0; i < VHD_MAX_LOCATOR_ENTRIES; i++)
    {
        /* Skip empty locators */
        if (ddh.ParentLocatorEntry[i].u32Code != RT_H2BE_U32(VHD_PLATFORM_CODE_NONE))
        {
            if (pImage->pszParentFilename)
            {
                rc = vhdLocatorUpdate(pImage, &ddh.ParentLocatorEntry[i], pImage->pszParentFilename);
                if (RT_FAILURE(rc))
                    goto out;
            }
            else
            {
                /* The parent was deleted. */
                ddh.ParentLocatorEntry[i].u32Code = RT_H2BE_U32(VHD_PLATFORM_CODE_NONE);
            }
        }
    }
    /* Update parent's UUID */
    memcpy(ddh.ParentUuid, pImage->ParentUuid.au8, sizeof(ddh.ParentUuid));
    ddh.Checksum = 0;
    ddh.Checksum = RT_H2BE_U32(vhdChecksum(&ddh, sizeof(ddh)));
    rc = vhdFileWriteSync(pImage, pImage->u64DataOffset, &ddh, sizeof(ddh), NULL);
    if (RT_FAILURE(rc))
        return rc;

    /* Update the VHD footer copy. */
    rc = vhdFileWriteSync(pImage, 0, &pImage->vhdFooterCopy, sizeof(VHDFooter), NULL);

out:
    return rc;
}


static int vhdOpenImage(PVHDIMAGE pImage, unsigned uOpenFlags)
{
    uint64_t FileSize;
    VHDFooter vhdFooter;

    if (uOpenFlags & VD_OPEN_FLAGS_ASYNC_IO)
        return VERR_NOT_SUPPORTED;

    pImage->uOpenFlags = uOpenFlags;

    pImage->pInterfaceError = VDInterfaceGet(pImage->pVDIfsDisk, VDINTERFACETYPE_ERROR);
    if (pImage->pInterfaceError)
        pImage->pInterfaceErrorCallbacks = VDGetInterfaceError(pImage->pInterfaceError);

#ifdef VBOX_WITH_NEW_IO_CODE
    /* Try to get async I/O interface. */
    pImage->pInterfaceAsyncIO = VDInterfaceGet(pImage->pVDIfsDisk, VDINTERFACETYPE_ASYNCIO);
    AssertPtr(pImage->pInterfaceAsyncIO);
    pImage->pInterfaceAsyncIOCallbacks = VDGetInterfaceAsyncIO(pImage->pInterfaceAsyncIO);
    AssertPtr(pImage->pInterfaceAsyncIOCallbacks);
#endif

    /*
     * Open the image.
     */
    int rc = vhdFileOpen(pImage, !!(uOpenFlags & VD_OPEN_FLAGS_READONLY), false);
    if (RT_FAILURE(rc))
    {
        /* Do NOT signal an appropriate error here, as the VD layer has the
         * choice of retrying the open if it failed. */
        return rc;
    }

    rc = vhdFileGetSize(pImage, &FileSize);
    pImage->FileSize = FileSize;
    pImage->uCurrentEndOfFile = FileSize - sizeof(VHDFooter);

    rc = vhdFileReadSync(pImage, pImage->uCurrentEndOfFile, &vhdFooter, sizeof(VHDFooter), NULL);
    if (memcmp(vhdFooter.Cookie, VHD_FOOTER_COOKIE, VHD_FOOTER_COOKIE_SIZE) != 0)
        return VERR_VD_VHD_INVALID_HEADER;

    switch (RT_BE2H_U32(vhdFooter.DiskType))
    {
        case VHD_FOOTER_DISK_TYPE_FIXED:
            {
                pImage->uImageFlags |= VD_IMAGE_FLAGS_FIXED;
            }
            break;
        case VHD_FOOTER_DISK_TYPE_DYNAMIC:
            {
                pImage->uImageFlags &= ~VD_IMAGE_FLAGS_FIXED;
            }
            break;
        case VHD_FOOTER_DISK_TYPE_DIFFERENCING:
            {
                pImage->uImageFlags |= VD_IMAGE_FLAGS_DIFF;
                pImage->uImageFlags &= ~VD_IMAGE_FLAGS_FIXED;
            }
            break;
        default:
            return VERR_NOT_IMPLEMENTED;
    }

    pImage->cbSize       = RT_BE2H_U64(vhdFooter.CurSize);
    pImage->LCHSGeometry.cCylinders   = 0;
    pImage->LCHSGeometry.cHeads       = 0;
    pImage->LCHSGeometry.cSectors     = 0;
    pImage->PCHSGeometry.cCylinders   = RT_BE2H_U16(vhdFooter.DiskGeometryCylinder);
    pImage->PCHSGeometry.cHeads       = vhdFooter.DiskGeometryHeads;
    pImage->PCHSGeometry.cSectors     = vhdFooter.DiskGeometrySectors;

    /*
     * Copy of the disk footer.
     * If we allocate new blocks in differencing disks on write access
     * the footer is overwritten. We need to write it at the end of the file.
     */
    memcpy(&pImage->vhdFooterCopy, &vhdFooter, sizeof(VHDFooter));

    /*
     * Is there a better way?
     */
    memcpy(&pImage->ImageUuid, &vhdFooter.UniqueID, 16);

    pImage->u64DataOffset = RT_BE2H_U64(vhdFooter.DataOffset);
    LogFlowFunc(("DataOffset=%llu\n", pImage->u64DataOffset));

    if (!(pImage->uImageFlags & VD_IMAGE_FLAGS_FIXED))
        rc = vhdLoadDynamicDisk(pImage, pImage->u64DataOffset);

    return rc;
}

static int vhdOpen(const char *pszFilename, unsigned uOpenFlags,
                   PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                   void **ppvBackendData)
{
    int rc = VINF_SUCCESS;
    PVHDIMAGE pImage;

    /* Check open flags. All valid flags are supported. */
    if (uOpenFlags & ~VD_OPEN_FLAGS_MASK)
    {
        rc = VERR_INVALID_PARAMETER;
        return rc;
    }

    pImage = (PVHDIMAGE)RTMemAllocZ(sizeof(VHDIMAGE));
    if (!pImage)
    {
        rc = VERR_NO_MEMORY;
        return rc;
    }
    pImage->pszFilename = pszFilename;
#ifndef VBOX_WITH_NEW_IO_CODE
    pImage->File = NIL_RTFILE;
#else
    pImage->pvStorage = NULL;
#endif
    pImage->pVDIfsDisk = pVDIfsDisk;

    rc = vhdOpenImage(pImage, uOpenFlags);
    if (RT_SUCCESS(rc))
        *ppvBackendData = pImage;
    return rc;
}

static int vhdLoadDynamicDisk(PVHDIMAGE pImage, uint64_t uDynamicDiskHeaderOffset)
{
    VHDDynamicDiskHeader vhdDynamicDiskHeader;
    int rc = VINF_SUCCESS;
    uint32_t *pBlockAllocationTable;
    uint64_t uBlockAllocationTableOffset;
    unsigned i = 0;

    Log(("Open a dynamic disk.\n"));

    /*
     * Read the dynamic disk header.
     */
    rc = vhdFileReadSync(pImage, uDynamicDiskHeaderOffset, &vhdDynamicDiskHeader, sizeof(VHDDynamicDiskHeader), NULL);
    if (memcmp(vhdDynamicDiskHeader.Cookie, VHD_DYNAMIC_DISK_HEADER_COOKIE, VHD_DYNAMIC_DISK_HEADER_COOKIE_SIZE) != 0)
        return VERR_INVALID_PARAMETER;

    pImage->cbDataBlock = RT_BE2H_U32(vhdDynamicDiskHeader.BlockSize);
    LogFlowFunc(("BlockSize=%u\n", pImage->cbDataBlock));
    pImage->cBlockAllocationTableEntries = RT_BE2H_U32(vhdDynamicDiskHeader.MaxTableEntries);
    LogFlowFunc(("MaxTableEntries=%lu\n", pImage->cBlockAllocationTableEntries));
    AssertMsg(!(pImage->cbDataBlock % VHD_SECTOR_SIZE), ("%s: Data block size is not a multiple of %!\n", __FUNCTION__, VHD_SECTOR_SIZE));

    pImage->cSectorsPerDataBlock = pImage->cbDataBlock / VHD_SECTOR_SIZE;
    LogFlowFunc(("SectorsPerDataBlock=%u\n", pImage->cSectorsPerDataBlock));

    /*
     * Every block starts with a bitmap indicating which sectors are valid and which are not.
     * We store the size of it to be able to calculate the real offset.
     */
    pImage->cbDataBlockBitmap = pImage->cSectorsPerDataBlock / 8;
    pImage->cDataBlockBitmapSectors = pImage->cbDataBlockBitmap / VHD_SECTOR_SIZE;
    /* Round up to full sector size */
    if (pImage->cbDataBlockBitmap % VHD_SECTOR_SIZE > 0)
        pImage->cDataBlockBitmapSectors++;
    LogFlowFunc(("cbDataBlockBitmap=%u\n", pImage->cbDataBlockBitmap));
    LogFlowFunc(("cDataBlockBitmapSectors=%u\n", pImage->cDataBlockBitmapSectors));

    pImage->pu8Bitmap = vhdBlockBitmapAllocate(pImage);
    if (!pImage->pu8Bitmap)
        return VERR_NO_MEMORY;

    pBlockAllocationTable = (uint32_t *)RTMemAllocZ(pImage->cBlockAllocationTableEntries * sizeof(uint32_t));
    if (!pBlockAllocationTable)
        return VERR_NO_MEMORY;

    /*
     * Read the table.
     */
    uBlockAllocationTableOffset = RT_BE2H_U64(vhdDynamicDiskHeader.TableOffset);
    LogFlowFunc(("uBlockAllocationTableOffset=%llu\n", uBlockAllocationTableOffset));
    pImage->uBlockAllocationTableOffset = uBlockAllocationTableOffset;
    rc = vhdFileReadSync(pImage, uBlockAllocationTableOffset, pBlockAllocationTable, pImage->cBlockAllocationTableEntries * sizeof(uint32_t), NULL);
    pImage->uDataBlockStart = uBlockAllocationTableOffset + pImage->cBlockAllocationTableEntries * sizeof(uint32_t);
    LogFlowFunc(("uDataBlockStart=%llu\n", pImage->uDataBlockStart));

    /*
     * Because the offset entries inside the allocation table are stored big endian
     * we need to convert them into host endian.
     */
    pImage->pBlockAllocationTable = (uint32_t *)RTMemAllocZ(pImage->cBlockAllocationTableEntries * sizeof(uint32_t));
    if (!pImage->pBlockAllocationTable)
        return VERR_NO_MEMORY;

    for (i = 0; i < pImage->cBlockAllocationTableEntries; i++)
        pImage->pBlockAllocationTable[i] = RT_BE2H_U32(pBlockAllocationTable[i]);

    RTMemFree(pBlockAllocationTable);

    if (pImage->uImageFlags & VD_IMAGE_FLAGS_DIFF)
        memcpy(pImage->ParentUuid.au8, vhdDynamicDiskHeader.ParentUuid, sizeof(pImage->ParentUuid));

    return rc;
}

static int vhdCheckIfValid(const char *pszFilename, PVDINTERFACE pVDIfsDisk)
{
    int rc = VINF_SUCCESS;
    RTFILE File;
    uint64_t cbFile;
    VHDFooter vhdFooter;

    rc = RTFileOpen(&File, pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
        return VERR_VD_VHD_INVALID_HEADER;

    rc = RTFileGetSize(File, &cbFile);
    if (RT_FAILURE(rc))
    {
        RTFileClose(File);
        return VERR_VD_VHD_INVALID_HEADER;
    }

    rc = RTFileReadAt(File, cbFile - sizeof(VHDFooter), &vhdFooter, sizeof(VHDFooter), NULL);
    if (RT_FAILURE(rc) || (memcmp(vhdFooter.Cookie, VHD_FOOTER_COOKIE, VHD_FOOTER_COOKIE_SIZE) != 0))
        rc = VERR_VD_VHD_INVALID_HEADER;
    else
        rc = VINF_SUCCESS;

    RTFileClose(File);

    return rc;
}

static unsigned vhdGetVersion(void *pBackendData)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;

    AssertPtr(pImage);

    if (pImage)
        return 1; /**< @todo use correct version */
    else
        return 0;
}

static int vhdGetPCHSGeometry(void *pBackendData, PPDMMEDIAGEOMETRY pPCHSGeometry)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        if (pImage->PCHSGeometry.cCylinders)
        {
            *pPCHSGeometry = pImage->PCHSGeometry;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_VD_GEOMETRY_NOT_SET;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returned %Rrc (CHS=%u/%u/%u)\n", rc, pImage->PCHSGeometry.cCylinders, pImage->PCHSGeometry.cHeads, pImage->PCHSGeometry.cSectors));
    return rc;
}

static int vhdSetPCHSGeometry(void *pBackendData, PCPDMMEDIAGEOMETRY pPCHSGeometry)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        {
            rc = VERR_VD_IMAGE_READ_ONLY;
            goto out;
        }

        pImage->PCHSGeometry = *pPCHSGeometry;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;

out:
    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static int vhdGetLCHSGeometry(void *pBackendData, PPDMMEDIAGEOMETRY pLCHSGeometry)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        if (pImage->LCHSGeometry.cCylinders)
        {
            *pLCHSGeometry = pImage->LCHSGeometry;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_VD_GEOMETRY_NOT_SET;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returned %Rrc (CHS=%u/%u/%u)\n", rc, pImage->LCHSGeometry.cCylinders, pImage->LCHSGeometry.cHeads, pImage->LCHSGeometry.cSectors));
    return rc;
}

static int vhdSetLCHSGeometry(void *pBackendData, PCPDMMEDIAGEOMETRY pLCHSGeometry)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        {
            rc = VERR_VD_IMAGE_READ_ONLY;
            goto out;
        }

        pImage->LCHSGeometry = *pLCHSGeometry;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;

out:
    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static unsigned vhdGetImageFlags(void *pBackendData)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    unsigned uImageFlags;

    AssertPtr(pImage);

    if (pImage)
        uImageFlags = pImage->uImageFlags;
    else
        uImageFlags = 0;

    LogFlowFunc(("returned %#x\n", uImageFlags));
    return uImageFlags;
}

static unsigned vhdGetOpenFlags(void *pBackendData)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    unsigned uOpenFlags;

    AssertPtr(pImage);

    if (pImage)
        uOpenFlags = pImage->uOpenFlags;
    else
        uOpenFlags = 0;

    LogFlowFunc(("returned %#x\n", uOpenFlags));
    return uOpenFlags;
}

static int vhdSetOpenFlags(void *pBackendData, unsigned uOpenFlags)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    /* Image must be opened and the new flags must be valid. Just readonly and
     * info flags are supported. */
    if (!pImage || (uOpenFlags & ~(VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO)))
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    rc = vhdFlush(pImage);
    if (RT_FAILURE(rc))
        goto out;
    vhdFileClose(pImage);
    pImage->uOpenFlags = uOpenFlags;
    rc = vhdFileOpen(pImage, !!(uOpenFlags & VD_OPEN_FLAGS_READONLY), false);

out:
    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static int vhdRename(void *pBackendData, const char *pszFilename)
{
    LogFlowFunc(("pBackendData=%#p pszFilename=%#p\n", pBackendData, pszFilename));

    int rc = VINF_SUCCESS;
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;

    /* Check arguments. */
    if (   !pImage
        || !pszFilename
        || !*pszFilename)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    /* Close the file. vhdFreeImage would additionally free pImage. */
    vhdFlush(pImage);
    vhdFileClose(pImage);

    /* Rename the file. */
    rc = RTFileMove(pImage->pszFilename, pszFilename, 0);
    if (RT_FAILURE(rc))
    {
        /* The move failed, try to reopen the original image. */
        int rc2 = vhdOpenImage(pImage, pImage->uOpenFlags);
        if (RT_FAILURE(rc2))
            rc = rc2;

        goto out;
    }

    /* Update pImage with the new information. */
    pImage->pszFilename = pszFilename;

    /* Open the new image. */
    rc = vhdOpenImage(pImage, pImage->uOpenFlags);
    if (RT_FAILURE(rc))
        goto out;

out:
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

static void vhdFreeImageMemory(PVHDIMAGE pImage)
{
    if (pImage->pszParentFilename)
    {
        RTStrFree(pImage->pszParentFilename);
        pImage->pszParentFilename = NULL;
    }
    if (pImage->pBlockAllocationTable)
    {
        RTMemFree(pImage->pBlockAllocationTable);
        pImage->pBlockAllocationTable = NULL;
    }
    if (pImage->pu8Bitmap)
    {
        RTMemFree(pImage->pu8Bitmap);
        pImage->pu8Bitmap = NULL;
    }
    RTMemFree(pImage);
}

static int vhdFreeImage(PVHDIMAGE pImage)
{
    int rc = VINF_SUCCESS;

    /* Freeing a never allocated image (e.g. because the open failed) is
     * not signalled as an error. After all nothing bad happens. */
    if (pImage)
    {
        vhdFlush(pImage);
        vhdFileClose(pImage);
        vhdFreeImageMemory(pImage);
    }

    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static int vhdClose(void *pBackendData, bool fDelete)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    /* Freeing a never allocated image (e.g. because the open failed) is
     * not signalled as an error. After all nothing bad happens. */
    if (pImage)
    {
        if (fDelete)
        {
            /* No point in updating the file that is deleted anyway. */
            vhdFileClose(pImage);
            RTFileDelete(pImage->pszFilename);
            vhdFreeImageMemory(pImage);
        }
        else
            rc = vhdFreeImage(pImage);
    }

    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

/**
 * Internal: Checks if a sector in the block bitmap is set
 */
DECLINLINE(bool) vhdBlockBitmapSectorContainsData(PVHDIMAGE pImage, uint32_t cBlockBitmapEntry)
{
    uint32_t iBitmap = (cBlockBitmapEntry / 8); /* Byte in the block bitmap. */

    /*
     * The index of the bit in the byte of the data block bitmap.
     * The most signifcant bit stands for a lower sector number.
     */
    uint8_t  iBitInByte = (8-1) - (cBlockBitmapEntry % 8);
    uint8_t *puBitmap = pImage->pu8Bitmap + iBitmap;

    AssertMsg(puBitmap < (pImage->pu8Bitmap + pImage->cbDataBlockBitmap),
                ("VHD: Current bitmap position exceeds maximum size of the bitmap\n"));

    return ASMBitTest(puBitmap, iBitInByte);
}

/**
 * Internal: Sets the given sector in the sector bitmap.
 */
DECLINLINE(void) vhdBlockBitmapSectorSet(PVHDIMAGE pImage, uint32_t cBlockBitmapEntry)
{
    uint32_t iBitmap = (cBlockBitmapEntry / 8); /* Byte in the block bitmap. */

    /*
     * The index of the bit in the byte of the data block bitmap.
     * The most signifcant bit stands for a lower sector number.
     */
    uint8_t  iBitInByte = (8-1) - (cBlockBitmapEntry % 8);
    uint8_t  *puBitmap  = pImage->pu8Bitmap + iBitmap;

    AssertMsg(puBitmap < (pImage->pu8Bitmap + pImage->cbDataBlockBitmap),
                ("VHD: Current bitmap position exceeds maximum size of the bitmap\n"));

    ASMBitSet(puBitmap, iBitInByte);
}

static int vhdRead(void *pBackendData, uint64_t uOffset, void *pvBuf, size_t cbRead, size_t *pcbActuallyRead)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pBackendData=%p uOffset=%#llx pvBuf=%p cbRead=%u pcbActuallyRead=%p\n", pBackendData, uOffset, pvBuf, cbRead, pcbActuallyRead));

    if (uOffset + cbRead > pImage->cbSize)
        return VERR_INVALID_PARAMETER;

    /*
     * If we have a dynamic disk image, we need to find the data block and sector to read.
     */
    if (pImage->pBlockAllocationTable)
    {
        /*
         * Get the data block first.
         */
        uint32_t cBlockAllocationTableEntry = (uOffset / VHD_SECTOR_SIZE) / pImage->cSectorsPerDataBlock;
        uint32_t cBATEntryIndex = (uOffset / VHD_SECTOR_SIZE) % pImage->cSectorsPerDataBlock;
        uint64_t uVhdOffset;

        LogFlowFunc(("cBlockAllocationTableEntry=%u cBatEntryIndex=%u\n", cBlockAllocationTableEntry, cBATEntryIndex));
        LogFlowFunc(("BlockAllocationEntry=%u\n", pImage->pBlockAllocationTable[cBlockAllocationTableEntry]));

        /*
         * If the block is not allocated the content of the entry is ~0
         */
        if (pImage->pBlockAllocationTable[cBlockAllocationTableEntry] == ~0U)
        {
            /* Return block size as read. */
            *pcbActuallyRead = RT_MIN(cbRead, pImage->cSectorsPerDataBlock * VHD_SECTOR_SIZE);
            return VERR_VD_BLOCK_FREE;
        }

        uVhdOffset = ((uint64_t)pImage->pBlockAllocationTable[cBlockAllocationTableEntry] + pImage->cDataBlockBitmapSectors + cBATEntryIndex) * VHD_SECTOR_SIZE;
        LogFlowFunc(("uVhdOffset=%llu cbRead=%u\n", uVhdOffset, cbRead));

        /*
         * Clip read range to remain in this data block.
         */
        cbRead = RT_MIN(cbRead, (pImage->cbDataBlock - (cBATEntryIndex * VHD_SECTOR_SIZE)));

        /* Read in the block's bitmap. */
        rc = vhdFileReadSync(pImage,
                             ((uint64_t)pImage->pBlockAllocationTable[cBlockAllocationTableEntry]) * VHD_SECTOR_SIZE,
                             pImage->pu8Bitmap, pImage->cbDataBlockBitmap, NULL);
        if (RT_SUCCESS(rc))
        {
            uint32_t cSectors = 0;

            if (vhdBlockBitmapSectorContainsData(pImage, cBATEntryIndex))
            {
                cBATEntryIndex++;
                cSectors = 1;

                /*
                 * The first sector being read is marked dirty, read as much as we
                 * can from child. Note that only sectors that are marked dirty
                 * must be read from child.
                 */
                while (   (cSectors < (cbRead / VHD_SECTOR_SIZE))
                       && vhdBlockBitmapSectorContainsData(pImage, cBATEntryIndex))
                {
                    cBATEntryIndex++;
                    cSectors++;
                }

                cbRead = cSectors * VHD_SECTOR_SIZE;

                LogFlowFunc(("uVhdOffset=%llu cbRead=%u\n", uVhdOffset, cbRead));
                rc = vhdFileReadSync(pImage, uVhdOffset, pvBuf, cbRead, NULL);
            }
            else
            {
                /*
                 * The first sector being read is marked clean, so we should read from
                 * our parent instead, but only as much as there are the following
                 * clean sectors, because the block may still contain dirty sectors
                 * further on. We just need to compute the number of clean sectors
                 * and pass it to our caller along with the notification that they
                 * should be read from the parent.
                 */
                cBATEntryIndex++;
                cSectors = 1;

                while (   (cSectors < (cbRead / VHD_SECTOR_SIZE))
                       && !vhdBlockBitmapSectorContainsData(pImage, cBATEntryIndex))
                {
                    cBATEntryIndex++;
                    cSectors++;
                }

                cbRead = cSectors * VHD_SECTOR_SIZE;
                Log(("%s: Sectors free: uVhdOffset=%llu cbRead=%u\n", __FUNCTION__, uVhdOffset, cbRead));
                rc = VERR_VD_BLOCK_FREE;
            }
        }
        else
            AssertMsgFailed(("Reading block bitmap failed rc=%Rrc\n", rc));
    }
    else
    {
        rc = vhdFileReadSync(pImage, uOffset, pvBuf, cbRead, NULL);
    }

    if (pcbActuallyRead)
        *pcbActuallyRead = cbRead;

    Log2(("vhdRead: off=%#llx pvBuf=%p cbRead=%d\n"
            "%.*Rhxd\n",
            uOffset, pvBuf, cbRead, cbRead, pvBuf));

    return rc;
}

static int vhdWrite(void *pBackendData, uint64_t uOffset, const void *pvBuf, size_t cbToWrite, size_t *pcbWriteProcess, size_t *pcbPreRead, size_t *pcbPostRead, unsigned fWrite)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pBackendData=%p uOffset=%llu pvBuf=%p cbToWrite=%u pcbWriteProcess=%p pcbPreRead=%p pcbPostRead=%p fWrite=%u\n",
             pBackendData, uOffset, pvBuf, cbToWrite, pcbWriteProcess, pcbPreRead, pcbPostRead, fWrite));

    AssertPtr(pImage);
    Assert(uOffset % VHD_SECTOR_SIZE == 0);
    Assert(cbToWrite % VHD_SECTOR_SIZE == 0);

    if (pImage->pBlockAllocationTable)
    {
        /*
         * Get the data block first.
         */
        uint32_t cSector = uOffset / VHD_SECTOR_SIZE;
        uint32_t cBlockAllocationTableEntry = cSector / pImage->cSectorsPerDataBlock;
        uint32_t cBATEntryIndex = cSector % pImage->cSectorsPerDataBlock;
        uint64_t uVhdOffset;

        /*
         * Clip write range.
         */
        cbToWrite = RT_MIN(cbToWrite, (pImage->cbDataBlock - (cBATEntryIndex * VHD_SECTOR_SIZE)));

        /*
         * If the block is not allocated the content of the entry is ~0
         * and we need to allocate a new block. Note that while blocks are
         * allocated with a relatively big granularity, each sector has its
         * own bitmap entry, indicating whether it has been written or not.
         * So that means for the purposes of the higher level that the
         * granularity is invisible. This means there's no need to return
         * VERR_VD_BLOCK_FREE unless the block hasn't been allocated yet.
         */
        if (pImage->pBlockAllocationTable[cBlockAllocationTableEntry] == ~0U)
        {
            /* Check if the block allocation should be suppressed. */
            if (fWrite & VD_WRITE_NO_ALLOC)
            {
                *pcbPreRead = cBATEntryIndex * VHD_SECTOR_SIZE;
                *pcbPostRead = pImage->cSectorsPerDataBlock * VHD_SECTOR_SIZE - cbToWrite - *pcbPreRead;

                if (pcbWriteProcess)
                    *pcbWriteProcess = cbToWrite;
                return VERR_VD_BLOCK_FREE;
            }

            size_t  cbNewBlock = pImage->cbDataBlock + (pImage->cDataBlockBitmapSectors * VHD_SECTOR_SIZE);
            uint8_t *pNewBlock = (uint8_t *)RTMemAllocZ(cbNewBlock);

            if (!pNewBlock)
                return VERR_NO_MEMORY;

            /*
             * Write the new block at the current end of the file.
             */
            rc = vhdFileWriteSync(pImage, pImage->uCurrentEndOfFile, pNewBlock, cbNewBlock, NULL);
            AssertRC(rc);

            /*
             * Set the new end of the file and link the new block into the BAT.
             */
            pImage->pBlockAllocationTable[cBlockAllocationTableEntry] = pImage->uCurrentEndOfFile / VHD_SECTOR_SIZE;
            pImage->uCurrentEndOfFile += cbNewBlock;
            RTMemFree(pNewBlock);

            /* Write the updated BAT and the footer to remain in a consistent state. */
            rc = vhdFlush(pImage);
            AssertRC(rc);
        }

        /*
         * Calculate the real offset in the file.
         */
        uVhdOffset = ((uint64_t)pImage->pBlockAllocationTable[cBlockAllocationTableEntry] + pImage->cDataBlockBitmapSectors + cBATEntryIndex) * VHD_SECTOR_SIZE;

        /* Write data. */
        vhdFileWriteSync(pImage, uVhdOffset, pvBuf, cbToWrite, NULL);

        /* Read in the block's bitmap. */
        rc = vhdFileReadSync(pImage,
            ((uint64_t)pImage->pBlockAllocationTable[cBlockAllocationTableEntry]) * VHD_SECTOR_SIZE,
            pImage->pu8Bitmap, pImage->cbDataBlockBitmap, NULL);
        if (RT_SUCCESS(rc))
        {
            /* Set the bits for all sectors having been written. */
            for (uint32_t iSector = 0; iSector < (cbToWrite / VHD_SECTOR_SIZE); iSector++)
            {
                vhdBlockBitmapSectorSet(pImage, cBATEntryIndex);
                cBATEntryIndex++;
            }

            /* Write the bitmap back. */
            rc = vhdFileWriteSync(pImage,
                ((uint64_t)pImage->pBlockAllocationTable[cBlockAllocationTableEntry]) * VHD_SECTOR_SIZE,
                pImage->pu8Bitmap, pImage->cbDataBlockBitmap, NULL);
        }
    }
    else
    {
        rc = vhdFileWriteSync(pImage, uOffset, pvBuf, cbToWrite, NULL);
    }

    if (pcbWriteProcess)
        *pcbWriteProcess = cbToWrite;

    /* Stay on the safe side. Do not run the risk of confusing the higher
     * level, as that can be pretty lethal to image consistency. */
    *pcbPreRead = 0;
    *pcbPostRead = 0;

    return rc;
}

static int vhdFlush(void *pBackendData)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;

    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        return VINF_SUCCESS;

    if (pImage->pBlockAllocationTable)
    {
        /*
         * This is an expanding image. Write the BAT and copy of the disk footer.
         */
        size_t   cbBlockAllocationTableToWrite = pImage->cBlockAllocationTableEntries * sizeof(uint32_t);
        uint32_t *pBlockAllocationTableToWrite = (uint32_t *)RTMemAllocZ(cbBlockAllocationTableToWrite);

        if (!pBlockAllocationTableToWrite)
            return VERR_NO_MEMORY;

        /*
         * The BAT entries have to be stored in big endian format.
         */
        for (unsigned i = 0; i < pImage->cBlockAllocationTableEntries; i++)
            pBlockAllocationTableToWrite[i] = RT_H2BE_U32(pImage->pBlockAllocationTable[i]);

        /*
         * Write the block allocation table after the copy of the disk footer and the dynamic disk header.
         */
        vhdFileWriteSync(pImage, pImage->uBlockAllocationTableOffset, pBlockAllocationTableToWrite, cbBlockAllocationTableToWrite, NULL);
        vhdFileWriteSync(pImage, pImage->uCurrentEndOfFile, &pImage->vhdFooterCopy, sizeof(VHDFooter), NULL);
        if (pImage->fDynHdrNeedsUpdate)
            vhdDynamicHeaderUpdate(pImage);
        RTMemFree(pBlockAllocationTableToWrite);
    }

    int rc = vhdFileFlushSync(pImage);

    return rc;
}

static uint64_t vhdGetSize(void *pBackendData)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;

    AssertPtr(pImage);

    if (pImage)
    {
        Log(("%s: cbSize=%llu\n", __FUNCTION__, pImage->cbSize));
        return pImage->cbSize;
    }
    else
        return 0;
}

static uint64_t vhdGetFileSize(void *pBackendData)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;

    AssertPtr(pImage);

    if (pImage)
    {
        uint64_t cb;
        int rc = vhdFileGetSize(pImage, &cb);
        if (RT_SUCCESS(rc))
            return cb;
        else
            return 0;
    }
    else
        return 0;
}

static int vhdGetUuid(void *pBackendData, PRTUUID pUuid)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        *pUuid = pImage->ImageUuid;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;
    LogFlowFunc(("returned %Rrc (%RTuuid)\n", rc, pUuid));
    return rc;
}

static int vhdSetUuid(void *pBackendData, PCRTUUID pUuid)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    LogFlowFunc(("Uuid=%RTuuid\n", pUuid));
    AssertPtr(pImage);

    if (pImage)
    {
        pImage->ImageUuid = *pUuid;
        /* Update the footer copy. It will get written to disk when the image is closed. */
        memcpy(&pImage->vhdFooterCopy.UniqueID, pUuid, 16);
        /* Update checksum. */
        pImage->vhdFooterCopy.Checksum = 0;
        pImage->vhdFooterCopy.Checksum = RT_H2BE_U32(vhdChecksum(&pImage->vhdFooterCopy, sizeof(VHDFooter)));

        /* Need to update the dynamic disk header to update the disk footer copy at the beginning. */
        if (!(pImage->uImageFlags & VD_IMAGE_FLAGS_FIXED))
            pImage->fDynHdrNeedsUpdate = true;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;
    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static int vhdGetComment(void *pBackendData, char *pszComment, size_t cbComment)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returned %Rrc comment='%s'\n", rc, pszComment));
    return rc;
}

static int vhdSetComment(void *pBackendData, const char *pszComment)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    LogFlowFunc(("pszComment='%s'\n", pszComment));
    AssertPtr(pImage);

    if (pImage)
    {
        /**@todo: implement */
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static int vhdGetModificationUuid(void *pBackendData, PRTUUID pUuid)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_VD_NOT_OPENED;
    LogFlowFunc(("returned %Rrc (%RTuuid)\n", rc, pUuid));
    return rc;
}

static int vhdSetModificationUuid(void *pBackendData, PCRTUUID pUuid)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    LogFlowFunc(("Uuid=%RTuuid\n", pUuid));
    AssertPtr(pImage);

    if (pImage)
    {
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;
    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static int vhdGetParentUuid(void *pBackendData, PRTUUID pUuid)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        *pUuid = pImage->ParentUuid;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;
    LogFlowFunc(("returned %Rrc (%RTuuid)\n", rc, pUuid));
    return rc;
}

static int vhdSetParentUuid(void *pBackendData, PCRTUUID pUuid)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    LogFlowFunc((" %RTuuid\n", pUuid));
    AssertPtr(pImage);

    if (pImage && vhdFileOpened(pImage))
    {
        if (!(pImage->uImageFlags & VD_IMAGE_FLAGS_FIXED))
        {
            pImage->ParentUuid = *pUuid;
            pImage->fDynHdrNeedsUpdate = true;
        }
        else
            rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_VD_NOT_OPENED;
    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static int vhdGetParentModificationUuid(void *pBackendData, PRTUUID pUuid)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_VD_NOT_OPENED;
    LogFlowFunc(("returned %Rrc (%RTuuid)\n", rc, pUuid));
    return rc;
}

static int vhdSetParentModificationUuid(void *pBackendData, PCRTUUID pUuid)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;
    int rc;

    LogFlowFunc(("%RTuuid\n", pUuid));
    AssertPtr(pImage);

    if (pImage)
    {
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;
    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

/**
 * Internal: Derive drive geometry from its size.
 */
static void vhdSetDiskGeometry(PVHDIMAGE pImage, uint64_t cbSize)
{
    uint64_t u64TotalSectors = cbSize / VHD_SECTOR_SIZE;
    uint32_t u32CylinderTimesHeads, u32Heads, u32SectorsPerTrack;

    if (u64TotalSectors > 65535 * 16 * 255)
    {
        /* ATA disks limited to 127 GB. */
        u64TotalSectors = 65535 * 16 * 255;
    }

    if (u64TotalSectors >= 65535 * 16 * 63)
    {
        u32SectorsPerTrack    = 255;
        u32Heads              = 16;
        u32CylinderTimesHeads = u64TotalSectors / u32SectorsPerTrack;
    }
    else
    {
        u32SectorsPerTrack    = 17;
        u32CylinderTimesHeads = u64TotalSectors / u32SectorsPerTrack;

        u32Heads = (u32CylinderTimesHeads + 1023) / 1024;

        if (u32Heads < 4)
        {
            u32Heads = 4;
        }
        if (u32CylinderTimesHeads >= (u32Heads * 1024) || u32Heads > 16)
        {
            u32SectorsPerTrack    = 31;
            u32Heads              = 16;
            u32CylinderTimesHeads = u64TotalSectors / u32SectorsPerTrack;
        }
        if (u32CylinderTimesHeads >= (u32Heads * 1024))
        {
            u32SectorsPerTrack    = 63;
            u32Heads              = 16;
            u32CylinderTimesHeads = u64TotalSectors / u32SectorsPerTrack;
        }
    }
    pImage->PCHSGeometry.cCylinders = u32CylinderTimesHeads / u32Heads;
    pImage->PCHSGeometry.cHeads     = u32Heads;
    pImage->PCHSGeometry.cSectors   = u32SectorsPerTrack;
    pImage->LCHSGeometry.cCylinders = 0;
    pImage->LCHSGeometry.cHeads     = 0;
    pImage->LCHSGeometry.cSectors   = 0;
}


/**
 * Internal: signal an error to the frontend.
 */
DECLINLINE(int) vhdError(PVHDIMAGE pImage, int rc, RT_SRC_POS_DECL,
                         const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    if (pImage->pInterfaceError && pImage->pInterfaceErrorCallbacks)
        pImage->pInterfaceErrorCallbacks->pfnError(pImage->pInterfaceError->pvUser, rc, RT_SRC_POS_ARGS,
                                                   pszFormat, va);
    va_end(va);
    return rc;
}

static uint32_t vhdAllocateParentLocators(PVHDIMAGE pImage, VHDDynamicDiskHeader *pDDH, uint64_t u64Offset)
{
    PVHDPLE pLocator = pDDH->ParentLocatorEntry;
    /* Relative Windows path. */
    pLocator->u32Code = RT_H2BE_U32(VHD_PLATFORM_CODE_WI2R);
    pLocator->u32DataSpace = RT_H2BE_U32(VHD_RELATIVE_MAX_PATH / VHD_SECTOR_SIZE);
    pLocator->u64DataOffset = RT_H2BE_U64(u64Offset);
    u64Offset += VHD_RELATIVE_MAX_PATH;
    pLocator++;
    /* Absolute Windows path. */
    pLocator->u32Code = RT_H2BE_U32(VHD_PLATFORM_CODE_WI2K);
    pLocator->u32DataSpace = RT_H2BE_U32(VHD_ABSOLUTE_MAX_PATH / VHD_SECTOR_SIZE);
    pLocator->u64DataOffset = RT_H2BE_U64(u64Offset);
    u64Offset += VHD_ABSOLUTE_MAX_PATH;
    pLocator++;
    /* Unicode relative Windows path. */
    pLocator->u32Code = RT_H2BE_U32(VHD_PLATFORM_CODE_W2RU);
    pLocator->u32DataSpace = RT_H2BE_U32(VHD_RELATIVE_MAX_PATH * sizeof(RTUTF16) / VHD_SECTOR_SIZE);
    pLocator->u64DataOffset = RT_H2BE_U64(u64Offset);
    u64Offset += VHD_RELATIVE_MAX_PATH * sizeof(RTUTF16);
    pLocator++;
    /* Unicode absolute Windows path. */
    pLocator->u32Code = RT_H2BE_U32(VHD_PLATFORM_CODE_W2KU);
    pLocator->u32DataSpace = RT_H2BE_U32(VHD_ABSOLUTE_MAX_PATH * sizeof(RTUTF16) / VHD_SECTOR_SIZE);
    pLocator->u64DataOffset = RT_H2BE_U64(u64Offset);
    return u64Offset + VHD_ABSOLUTE_MAX_PATH * sizeof(RTUTF16);
}

/**
 * Internal: Additional code for dynamic VHD image creation.
 */
static int vhdCreateDynamicImage(PVHDIMAGE pImage, uint64_t cbSize)
{
    int rc;
    VHDDynamicDiskHeader DynamicDiskHeader;
    uint32_t u32BlockAllocationTableSectors;
    void    *pvTmp = NULL;

    memset(&DynamicDiskHeader, 0, sizeof(DynamicDiskHeader));

    pImage->u64DataOffset           = sizeof(VHDFooter);
    pImage->cbDataBlock             = VHD_BLOCK_SIZE; /* 2 MB */
    pImage->cSectorsPerDataBlock    = pImage->cbDataBlock / VHD_SECTOR_SIZE;
    pImage->cbDataBlockBitmap       = pImage->cSectorsPerDataBlock / 8;
    pImage->cDataBlockBitmapSectors = pImage->cbDataBlockBitmap / VHD_SECTOR_SIZE;
    /* Align to sector boundary */
    if (pImage->cbDataBlockBitmap % VHD_SECTOR_SIZE > 0)
        pImage->cDataBlockBitmapSectors++;
    pImage->pu8Bitmap               = vhdBlockBitmapAllocate(pImage);
    if (!pImage->pu8Bitmap)
        return vhdError(pImage, VERR_NO_MEMORY, RT_SRC_POS, N_("VHD: cannot allocate memory for bitmap storage"));

    /* Initialize BAT. */
    pImage->uBlockAllocationTableOffset = (uint64_t)sizeof(VHDFooter) + sizeof(VHDDynamicDiskHeader);
    pImage->cBlockAllocationTableEntries = (uint32_t)((cbSize + pImage->cbDataBlock - 1) / pImage->cbDataBlock); /* Align table to the block size. */
    u32BlockAllocationTableSectors = (pImage->cBlockAllocationTableEntries * sizeof(uint32_t) + VHD_SECTOR_SIZE - 1) / VHD_SECTOR_SIZE;
    pImage->pBlockAllocationTable = (uint32_t *)RTMemAllocZ(pImage->cBlockAllocationTableEntries * sizeof(uint32_t));
    if (!pImage->pBlockAllocationTable)
        return vhdError(pImage, VERR_NO_MEMORY, RT_SRC_POS, N_("VHD: cannot allocate memory for BAT"));

    for (unsigned i = 0; i < pImage->cBlockAllocationTableEntries; i++)
    {
        pImage->pBlockAllocationTable[i] = 0xFFFFFFFF; /* It is actually big endian. */
    }

    /* Round up to the sector size. */
    if (pImage->uImageFlags & VD_IMAGE_FLAGS_DIFF) /* fix hyper-v unreadable error */
        pImage->uCurrentEndOfFile = vhdAllocateParentLocators(pImage, &DynamicDiskHeader,
                                                              pImage->uBlockAllocationTableOffset + u32BlockAllocationTableSectors * VHD_SECTOR_SIZE);
    else
        pImage->uCurrentEndOfFile = pImage->uBlockAllocationTableOffset + u32BlockAllocationTableSectors * VHD_SECTOR_SIZE;

    /* Set dynamic image size. */
    pvTmp = RTMemTmpAllocZ(pImage->uCurrentEndOfFile + sizeof(VHDFooter));
    if (!pvTmp)
        return vhdError(pImage, VERR_NO_MEMORY, RT_SRC_POS, N_("VHD: cannot set the file size for '%s'"), pImage->pszFilename);

    rc = vhdFileWriteSync(pImage, 0, pvTmp, pImage->uCurrentEndOfFile + sizeof(VHDFooter), NULL);
    if (RT_FAILURE(rc))
    {
        RTMemTmpFree(pvTmp);
        return vhdError(pImage, rc, RT_SRC_POS, N_("VHD: cannot set the file size for '%s'"), pImage->pszFilename);
    }

    RTMemTmpFree(pvTmp);

    /* Initialize and write the dynamic disk header. */
    memcpy(DynamicDiskHeader.Cookie, VHD_DYNAMIC_DISK_HEADER_COOKIE, sizeof(DynamicDiskHeader.Cookie));
    DynamicDiskHeader.DataOffset      = UINT64_C(0xFFFFFFFFFFFFFFFF); /* Initially the disk has no data. */
    DynamicDiskHeader.TableOffset     = RT_H2BE_U64(pImage->uBlockAllocationTableOffset);
    DynamicDiskHeader.HeaderVersion   = RT_H2BE_U32(VHD_DYNAMIC_DISK_HEADER_VERSION);
    DynamicDiskHeader.BlockSize       = RT_H2BE_U32(pImage->cbDataBlock);
    DynamicDiskHeader.MaxTableEntries = RT_H2BE_U32(pImage->cBlockAllocationTableEntries);
    /* Compute and update checksum. */
    DynamicDiskHeader.Checksum = 0;
    DynamicDiskHeader.Checksum = RT_H2BE_U32(vhdChecksum(&DynamicDiskHeader, sizeof(DynamicDiskHeader)));

    rc = vhdFileWriteSync(pImage, sizeof(VHDFooter), &DynamicDiskHeader, sizeof(DynamicDiskHeader), NULL);
    if (RT_FAILURE(rc))
        return vhdError(pImage, rc, RT_SRC_POS, N_("VHD: cannot write dynamic disk header to image '%s'"), pImage->pszFilename);

    /* Write BAT. */
    rc = vhdFileWriteSync(pImage, pImage->uBlockAllocationTableOffset, pImage->pBlockAllocationTable,
                          pImage->cBlockAllocationTableEntries * sizeof(uint32_t), NULL);
    if (RT_FAILURE(rc))
        return vhdError(pImage, rc, RT_SRC_POS, N_("VHD: cannot write BAT to image '%s'"), pImage->pszFilename);

    return rc;
}

/**
 * Internal: The actual code for VHD image creation, both fixed and dynamic.
 */
static int vhdCreateImage(PVHDIMAGE pImage, uint64_t cbSize,
                          unsigned uImageFlags, const char *pszComment,
                          PCPDMMEDIAGEOMETRY pPCHSGeometry,
                          PCPDMMEDIAGEOMETRY pLCHSGeometry, PCRTUUID pUuid,
                          unsigned uOpenFlags,
                          PFNVMPROGRESS pfnProgress, void *pvUser,
                          unsigned uPercentStart, unsigned uPercentSpan)
{
    int rc;
    VHDFooter Footer;
    RTTIMESPEC now;

    pImage->uOpenFlags = uOpenFlags;
    pImage->uImageFlags = uImageFlags;

    pImage->pInterfaceError = VDInterfaceGet(pImage->pVDIfsDisk, VDINTERFACETYPE_ERROR);
    if (pImage->pInterfaceError)
        pImage->pInterfaceErrorCallbacks = VDGetInterfaceError(pImage->pInterfaceError);

    rc = vhdFileOpen(pImage, false /* fReadonly */, true /* fCreate */);
    if (RT_FAILURE(rc))
        return vhdError(pImage, rc, RT_SRC_POS, N_("VHD: cannot create image '%s'"), pImage->pszFilename);


    pImage->cbSize = cbSize;
    pImage->ImageUuid = *pUuid;
    RTUuidClear(&pImage->ParentUuid);
    vhdSetDiskGeometry(pImage, cbSize);

    /* Initialize the footer. */
    memset(&Footer, 0, sizeof(Footer));
    memcpy(Footer.Cookie, VHD_FOOTER_COOKIE, sizeof(Footer.Cookie));
    Footer.Features = RT_H2BE_U32(0x2);
    Footer.Version  = RT_H2BE_U32(VHD_FOOTER_FILE_FORMAT_VERSION);
    Footer.TimeStamp = RT_H2BE_U32(vhdRtTime2VhdTime(RTTimeNow(&now)));
    memcpy(Footer.CreatorApp, "vbox", sizeof(Footer.CreatorApp));
    Footer.CreatorVer = RT_H2BE_U32(VBOX_VERSION);
#ifdef RT_OS_DARWIN
    Footer.CreatorOS  = RT_H2BE_U32(0x4D616320); /* "Mac " */
#else /* Virtual PC supports only two platforms atm, so everything else will be Wi2k. */
    Footer.CreatorOS  = RT_H2BE_U32(0x5769326B); /* "Wi2k" */
#endif
    Footer.OrigSize   = RT_H2BE_U64(cbSize);
    Footer.CurSize    = Footer.OrigSize;
    Footer.DiskGeometryCylinder = RT_H2BE_U16(pImage->PCHSGeometry.cCylinders);
    Footer.DiskGeometryHeads    = pImage->PCHSGeometry.cHeads;
    Footer.DiskGeometrySectors  = pImage->PCHSGeometry.cSectors;
    memcpy(Footer.UniqueID, pImage->ImageUuid.au8, sizeof(Footer.UniqueID));
    Footer.SavedState = 0;

    if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
    {
        Footer.DiskType   = RT_H2BE_U32(VHD_FOOTER_DISK_TYPE_FIXED);
        /*
         * Initialize fixed image.
         * "The size of the entire file is the size of the hard disk in
         * the guest operating system plus the size of the footer."
         */
        pImage->u64DataOffset     = VHD_FOOTER_DATA_OFFSET_FIXED;
        pImage->uCurrentEndOfFile = cbSize;
        /** @todo r=klaus replace this with actual data writes, see the experience
         * with VDI files on Windows, can cause long freezes when writing. */
        rc = vhdFileSetSize(pImage, pImage->uCurrentEndOfFile + sizeof(VHDFooter));
        if (RT_FAILURE(rc))
        {
            vhdError(pImage, rc, RT_SRC_POS, N_("VHD: cannot set the file size for '%s'"), pImage->pszFilename);
            goto out;
        }
    }
    else
    {
        /*
         * Initialize dynamic image.
         *
         * The overall structure of dynamic disk is:
         *
         * [Copy of hard disk footer (512 bytes)]
         * [Dynamic disk header (1024 bytes)]
         * [BAT (Block Allocation Table)]
         * [Parent Locators]
         * [Data block 1]
         * [Data block 2]
         * ...
         * [Data block N]
         * [Hard disk footer (512 bytes)]
         */
        Footer.DiskType   = (uImageFlags & VD_IMAGE_FLAGS_DIFF)
                              ? RT_H2BE_U32(VHD_FOOTER_DISK_TYPE_DIFFERENCING)
                              : RT_H2BE_U32(VHD_FOOTER_DISK_TYPE_DYNAMIC);
        /* We are half way thourgh with creation of image, let the caller know. */
        if (pfnProgress)
            pfnProgress(NULL /* WARNING! pVM=NULL  */, (uPercentStart + uPercentSpan) / 2, pvUser);

        rc = vhdCreateDynamicImage(pImage, cbSize);
        if (RT_FAILURE(rc))
            goto out;
    }

    Footer.DataOffset = RT_H2BE_U64(pImage->u64DataOffset);

    /* Compute and update the footer checksum. */
    Footer.Checksum = 0;
    Footer.Checksum = RT_H2BE_U32(vhdChecksum(&Footer, sizeof(Footer)));

    pImage->vhdFooterCopy = Footer;

    /* Store the footer */
    rc = vhdFileWriteSync(pImage, pImage->uCurrentEndOfFile, &Footer, sizeof(Footer), NULL);
    if (RT_FAILURE(rc))
    {
        vhdError(pImage, rc, RT_SRC_POS, N_("VHD: cannot write footer to image '%s'"), pImage->pszFilename);
        goto out;
    }

    /* Dynamic images contain a copy of the footer at the very beginning of the file. */
    if (!(uImageFlags & VD_IMAGE_FLAGS_FIXED))
    {
        /* Write the copy of the footer. */
        rc = vhdFileWriteSync(pImage, 0, &Footer, sizeof(Footer), NULL);
        if (RT_FAILURE(rc))
        {
            vhdError(pImage, rc, RT_SRC_POS, N_("VHD: cannot write a copy of footer to image '%s'"), pImage->pszFilename);
            goto out;
        }
    }

    if (pfnProgress)
        pfnProgress(NULL /* WARNING! pVM=NULL  */, uPercentStart + uPercentSpan, pvUser);

out:
    return rc;
}

static int vhdCreate(const char *pszFilename, uint64_t cbSize,
                     unsigned uImageFlags, const char *pszComment,
                     PCPDMMEDIAGEOMETRY pPCHSGeometry,
                     PCPDMMEDIAGEOMETRY pLCHSGeometry, PCRTUUID pUuid,
                     unsigned uOpenFlags, unsigned uPercentStart,
                     unsigned uPercentSpan, PVDINTERFACE pVDIfsDisk,
                     PVDINTERFACE pVDIfsImage, PVDINTERFACE pVDIfsOperation,
                     void **ppvBackendData)
{
    int rc = VINF_SUCCESS;
    PVHDIMAGE pImage;

    PFNVMPROGRESS pfnProgress = NULL;
    void *pvUser = NULL;
    PVDINTERFACE pIfProgress = VDInterfaceGet(pVDIfsOperation,
                                              VDINTERFACETYPE_PROGRESS);
    PVDINTERFACEPROGRESS pCbProgress = NULL;
    if (pIfProgress)
    {
        pCbProgress = VDGetInterfaceProgress(pIfProgress);
        if (pCbProgress)
            pfnProgress = pCbProgress->pfnProgress;
        pvUser = pIfProgress->pvUser;
    }

    /* Check open flags. All valid flags are supported. */
    if (uOpenFlags & ~VD_OPEN_FLAGS_MASK)
    {
        rc = VERR_INVALID_PARAMETER;
        return rc;
    }

    /* @todo Check the values of other params */

    pImage = (PVHDIMAGE)RTMemAllocZ(sizeof(VHDIMAGE));
    if (!pImage)
    {
        rc = VERR_NO_MEMORY;
        return rc;
    }
    pImage->pszFilename = pszFilename;
#ifndef VBOX_WITH_NEW_IO_CODE
    pImage->File = NIL_RTFILE;
#else
    pImage->pvStorage = NULL;
#endif
    pImage->pVDIfsDisk = pVDIfsDisk;

#ifdef VBOX_WITH_NEW_IO_CODE
    /* Try to get async I/O interface. */
    pImage->pInterfaceAsyncIO = VDInterfaceGet(pImage->pVDIfsDisk, VDINTERFACETYPE_ASYNCIO);
    AssertPtr(pImage->pInterfaceAsyncIO);
    pImage->pInterfaceAsyncIOCallbacks = VDGetInterfaceAsyncIO(pImage->pInterfaceAsyncIO);
    AssertPtr(pImage->pInterfaceAsyncIOCallbacks);
#endif

    rc = vhdCreateImage(pImage, cbSize, uImageFlags, pszComment,
                        pPCHSGeometry, pLCHSGeometry, pUuid, uOpenFlags,
                        pfnProgress, pvUser, uPercentStart, uPercentSpan);

    if (RT_SUCCESS(rc))
    {
        /* So far the image is opened in read/write mode. Make sure the
         * image is opened in read-only mode if the caller requested that. */
        if (uOpenFlags & VD_OPEN_FLAGS_READONLY)
        {
            vhdClose(pImage, false);
            rc = vhdOpenImage(pImage, uOpenFlags);
            if (RT_FAILURE(rc))
                goto out;
        }
        *ppvBackendData = pImage;
    }
out:
    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static void vhdDump(void *pBackendData)
{
    PVHDIMAGE pImage = (PVHDIMAGE)pBackendData;

    AssertPtr(pImage);
    if (pImage)
    {
        /** @todo this is just a stub */
    }
}


static int vhdGetTimeStamp(void *pvBackendData, PRTTIMESPEC pTimeStamp)
{
    int rc = VINF_SUCCESS;
    PVHDIMAGE pImage = (PVHDIMAGE)pvBackendData;

    AssertPtr(pImage);
    if (pImage)
    {
        RTFSOBJINFO info;

#ifndef VBOX_WITH_NEW_IO_CODE
        rc = RTFileQueryInfo(pImage->File, &info, RTFSOBJATTRADD_NOTHING);
#else
        /* Interface doesn't provide such a feature. */
        RTFILE File;
        rc = RTFileOpen(&File, pImage->pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
        if (RT_SUCCESS(rc))
        {
            rc = RTFileQueryInfo(File, &info, RTFSOBJATTRADD_NOTHING);
            RTFileClose(File);
        }
#endif

        *pTimeStamp = info.ModificationTime;
    }
    else
        rc = VERR_VD_NOT_OPENED;
    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static int vhdGetParentTimeStamp(void *pvBackendData, PRTTIMESPEC pTimeStamp)
{
    int rc = VINF_SUCCESS;
    PVHDIMAGE pImage = (PVHDIMAGE)pvBackendData;

    AssertPtr(pImage);
    if (pImage)
        vhdTime2RtTime(pTimeStamp, pImage->u32ParentTimeStamp);
    else
        rc = VERR_VD_NOT_OPENED;
    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static int vhdSetParentTimeStamp(void *pvBackendData, PCRTTIMESPEC pTimeStamp)
{
    int rc = VINF_SUCCESS;
    PVHDIMAGE pImage = (PVHDIMAGE)pvBackendData;

    AssertPtr(pImage);
    if (pImage)
    {
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
            rc = VERR_VD_IMAGE_READ_ONLY;
        else
        {
            pImage->u32ParentTimeStamp = vhdRtTime2VhdTime(pTimeStamp);
            pImage->fDynHdrNeedsUpdate = true;
        }
    }
    else
        rc = VERR_VD_NOT_OPENED;
    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static int vhdGetParentFilename(void *pvBackendData, char **ppszParentFilename)
{
    int rc = VINF_SUCCESS;
    PVHDIMAGE pImage = (PVHDIMAGE)pvBackendData;

    AssertPtr(pImage);
    if (pImage)
        *ppszParentFilename = RTStrDup(pImage->pszParentFilename);
    else
        rc = VERR_VD_NOT_OPENED;
    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static int vhdSetParentFilename(void *pvBackendData, const char *pszParentFilename)
{
    int rc = VINF_SUCCESS;
    PVHDIMAGE pImage = (PVHDIMAGE)pvBackendData;

    AssertPtr(pImage);
    if (pImage)
    {
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
            rc = VERR_VD_IMAGE_READ_ONLY;
        else
        {
            if (pImage->pszParentFilename)
                RTStrFree(pImage->pszParentFilename);
            pImage->pszParentFilename = RTStrDup(pszParentFilename);
            if (!pImage->pszParentFilename)
                rc = VERR_NO_MEMORY;
            else
                pImage->fDynHdrNeedsUpdate = true;
        }
    }
    else
        rc = VERR_VD_NOT_OPENED;
    LogFlowFunc(("returned %Rrc\n", rc));
    return rc;
}

static bool vhdIsAsyncIOSupported(void *pvBackendData)
{
    return false;
}

static int vhdAsyncRead(void *pvBackendData, uint64_t uOffset, size_t cbRead,
                        PPDMDATASEG paSeg, unsigned cSeg, void *pvUser)
{
    int rc = VERR_NOT_IMPLEMENTED;
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

static int vhdAsyncWrite(void *pvBackendData, uint64_t uOffset, size_t cbToWrite,
                         PPDMDATASEG paSeg, unsigned cSeg, void *pvUser)
{
    int rc = VERR_NOT_IMPLEMENTED;
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXHDDBACKEND g_VhdBackend =
{
    /* pszBackendName */
    "VHD",
    /* cbSize */
    sizeof(VBOXHDDBACKEND),
    /* uBackendCaps */
    VD_CAP_UUID | VD_CAP_DIFF | VD_CAP_FILE |
    VD_CAP_CREATE_FIXED | VD_CAP_CREATE_DYNAMIC,
    /* papszFileExtensions */
    s_apszVhdFileExtensions,
    /* paConfigInfo */
    NULL,
    /* hPlugin */
    NIL_RTLDRMOD,
    /* pfnCheckIfValid */
    vhdCheckIfValid,
    /* pfnOpen */
    vhdOpen,
    /* pfnCreate */
    vhdCreate,
    /* pfnRename */
    vhdRename,
    /* pfnClose */
    vhdClose,
    /* pfnRead */
    vhdRead,
    /* pfnWrite */
    vhdWrite,
    /* pfnFlush */
    vhdFlush,
    /* pfnGetVersion */
    vhdGetVersion,
    /* pfnGetSize */
    vhdGetSize,
    /* pfnGetFileSize */
    vhdGetFileSize,
    /* pfnGetPCHSGeometry */
    vhdGetPCHSGeometry,
    /* pfnSetPCHSGeometry */
    vhdSetPCHSGeometry,
    /* pfnGetLCHSGeometry */
    vhdGetLCHSGeometry,
    /* pfnSetLCHSGeometry */
    vhdSetLCHSGeometry,
    /* pfnGetImageFlags */
    vhdGetImageFlags,
    /* pfnGetOpenFlags */
    vhdGetOpenFlags,
    /* pfnSetOpenFlags */
    vhdSetOpenFlags,
    /* pfnGetComment */
    vhdGetComment,
    /* pfnSetComment */
    vhdSetComment,
    /* pfnGetUuid */
    vhdGetUuid,
    /* pfnSetUuid */
    vhdSetUuid,
    /* pfnGetModificationUuid */
    vhdGetModificationUuid,
    /* pfnSetModificationUuid */
    vhdSetModificationUuid,
    /* pfnGetParentUuid */
    vhdGetParentUuid,
    /* pfnSetParentUuid */
    vhdSetParentUuid,
    /* pfnGetParentModificationUuid */
    vhdGetParentModificationUuid,
    /* pfnSetParentModificationUuid */
    vhdSetParentModificationUuid,
    /* pfnDump */
    vhdDump,
    /* pfnGetTimeStamp */
    vhdGetTimeStamp,
    /* pfnGetParentTimeStamp */
    vhdGetParentTimeStamp,
    /* pfnSetParentTimeStamp */
    vhdSetParentTimeStamp,
    /* pfnGetParentFilename */
    vhdGetParentFilename,
    /* pfnSetParentFilename */
    vhdSetParentFilename,
    /* pfnIsAsyncIOSupported */
    vhdIsAsyncIOSupported,
    /* pfnAsyncRead */
    vhdAsyncRead,
    /* pfnAsyncWrite */
    vhdAsyncWrite,
    /* pfnComposeLocation */
    genericFileComposeLocation,
    /* pfnComposeName */
    genericFileComposeName
};
