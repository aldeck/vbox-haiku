/** $Id$ */
/** @file
 * VMDK Disk image, Core Code.
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
#define LOG_GROUP LOG_GROUP_VD_VMDK
#include "VBoxHDD-newInternal.h"
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/uuid.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/rand.h>


/*******************************************************************************
*   Constants And Macros, Structures and Typedefs                              *
*******************************************************************************/

/**
 * Magic number for hosted images created by VMware Workstation 4, VMware
 * Workstation 5, VMware Server or VMware Player.
 */
#define VMDK_SPARSE_MAGICNUMBER 0x564d444b /* 'V' 'M' 'D' 'K' */

/** VMDK hosted sparse extent header. */
#pragma pack(1)
typedef struct SparseExtentHeader
{
    uint32_t    magicNumber;
    uint32_t    version;
    uint32_t    flags;
    uint64_t    capacity;
    uint64_t    grainSize;
    uint64_t    descriptorOffset;
    uint64_t    descriptorSize;
    uint32_t    numGTEsPerGT;
    uint64_t    rgdOffset;
    uint64_t    gdOffset;
    uint64_t    overHead;
    bool        uncleanShutdown;
    char        singleEndLineChar;
    char        nonEndLineChar;
    char        doubleEndLineChar1;
    char        doubleEndLineChar2;
    uint8_t     pad[435];
} SparseExtentHeader;
#pragma pack()


#ifdef VBOX_WITH_VMDK_ESX

/** @todo the ESX code is not tested, not used, and lacks error messages. */

/**
 * Magic number for images created by VMware GSX Server 3 or ESX Server 3.
 */
#define VMDK_ESX_SPARSE_MAGICNUMBER 0x44574f43 /* 'C' 'O' 'W' 'D' */

#pragma pack(1)
typedef struct COWDisk_Header
{
    uint32_t    magicNumber;
    uint32_t    version;
    uint32_t    flags;
    uint32_t    numSectors;
    uint32_t    grainSize;
    uint32_t    gdOffset;
    uint32_t    numGDEntries;
    uint32_t    freeSector;
    /* The spec incompletely documents quite a few further fields, but states
     * that they are not used by the current format. Replace them by padding. */
    char        reserved1[1604];
    uint32_t    savedGeneration;
    char        reserved2[8];
    uint32_t    uncleanShutdown;
    char        padding[396];
} COWDisk_Header;
#pragma pack()
#endif /* VBOX_WITH_VMDK_ESX */


/** Convert sector number/size to byte offset/size. */
#define VMDK_SECTOR2BYTE(u) ((u) << 9)

/** Convert byte offset/size to sector number/size. */
#define VMDK_BYTE2SECTOR(u) ((u) >> 9)

/**
 * VMDK extent type.
 */
typedef enum VMDKETYPE
{
    /** Hosted sparse extent. */
    VMDKETYPE_HOSTED_SPARSE = 1,
    /** Flat extent. */
    VMDKETYPE_FLAT,
    /** Zero extent. */
    VMDKETYPE_ZERO
#ifdef VBOX_WITH_VMDK_ESX
    ,
    /** ESX sparse extent. */
    VMDKETYPE_ESX_SPARSE
#endif /* VBOX_WITH_VMDK_ESX */
} VMDKETYPE, *PVMDKETYPE;

/**
 * VMDK access type for a extent.
 */
typedef enum VMDKACCESS
{
    /** No access allowed. */
    VMDKACCESS_NOACCESS = 0,
    /** Read-only access. */
    VMDKACCESS_READONLY,
    /** Read-write access. */
    VMDKACCESS_READWRITE
} VMDKACCESS, *PVMDKACCESS;

/**
 * VMDK extent data structure.
 */
typedef struct VMDKEXTENT
{
    /** File handle. */
    RTFILE      File;
    /** Base name of the image extent. */
    const char  *pszBasename;
    /** Full name of the image extent. */
    const char  *pszFullname;
    /** Number of sectors in this extent. */
    uint64_t    cSectors;
    /** Number of sectors per block (grain in VMDK speak). */
    uint64_t    cSectorsPerGrain;
    /** Starting sector number of descriptor. */
    uint64_t    uDescriptorSector;
    /** Size of descriptor in sectors. */
    uint64_t    cDescriptorSectors;
    /** Starting sector number of grain directory. */
    uint64_t    uSectorGD;
    /** Starting sector number of redundant grain directory. */
    uint64_t    uSectorRGD;
    /** Total number of metadata sectors. */
    uint64_t    cOverheadSectors;
    /** Nominal size (i.e. as described by the descriptor) of this extent. */
    uint64_t    cNominalSectors;
    /** Sector offset (i.e. as described by the descriptor) of this extent. */
    uint64_t    uSectorOffset;
    /** Number of entries in a grain table. */
    uint32_t    cGTEntries;
    /** Number of sectors reachable via a grain directory entry. */
    uint32_t    cSectorsPerGDE;
    /** Number of entries in the grain directory. */
    uint32_t    cGDEntries;
    /** Pointer to the next free sector. Legacy information. Do not use. */
    uint32_t    uFreeSector;
    /** Number of this extent in the list of images. */
    uint32_t    uExtent;
    /** Pointer to the descriptor (NULL if no descriptor in this extent). */
    char        *pDescData;
    /** Pointer to the grain directory. */
    uint32_t    *pGD;
    /** Pointer to the redundant grain directory. */
    uint32_t     *pRGD;
    /** Type of this extent. */
    VMDKETYPE   enmType;
    /** Access to this extent. */
    VMDKACCESS  enmAccess;
    /** Flag whether this extent is marked as unclean. */
    bool        fUncleanShutdown;
    /** Flag whether the metadata in the extent header needs to be updated. */
    bool        fMetaDirty;
    /** Reference to the image in which this extent is used. Do not use this
     * on a regular basis to avoid passing pImage references to functions
     * explicitly. */
    struct VMDKIMAGE *pImage;
} VMDKEXTENT, *PVMDKEXTENT;

/**
 * Grain table cache size. Allocated per image.
 */
#define VMDK_GT_CACHE_SIZE 256

/**
 * Grain table block size. Smaller than an actual grain table block to allow
 * more grain table blocks to be cached without having to allocate excessive
 * amounts of memory for the cache.
 */
#define VMDK_GT_CACHELINE_SIZE 128


/**
 * Maximum number of lines in a descriptor file. Not worth the effort of
 * making it variable. Descriptor files are generally very short (~20 lines).
 */
#define VMDK_DESCRIPTOR_LINES_MAX   100U

/**
 * Parsed descriptor information. Allows easy access and update of the
 * descriptor (whether separate file or not). Free form text files suck.
 */
typedef struct VMDKDESCRIPTOR
{
    /** Line number of first entry of the disk descriptor. */
    unsigned    uFirstDesc;
    /** Line number of first entry in the extent description. */
    unsigned    uFirstExtent;
    /** Line number of first disk database entry. */
    unsigned    uFirstDDB;
    /** Total number of lines. */
    unsigned    cLines;
    /** Total amount of memory available for the descriptor. */
    size_t      cbDescAlloc;
    /** Set if descriptor has been changed and not yet written to disk. */
    bool        fDirty;
    /** Array of pointers to the data in the descriptor. */
    char        *aLines[VMDK_DESCRIPTOR_LINES_MAX];
    /** Array of line indices pointing to the next non-comment line. */
    unsigned    aNextLines[VMDK_DESCRIPTOR_LINES_MAX];
} VMDKDESCRIPTOR, *PVMDKDESCRIPTOR;


/**
 * Cache entry for translating extent/sector to a sector number in that
 * extent.
 */
typedef struct VMDKGTCACHEENTRY
{
    /** Extent number for which this entry is valid. */
    uint32_t    uExtent;
    /** GT data block number. */
    uint64_t    uGTBlock;
    /** Data part of the cache entry. */
    uint32_t    aGTData[VMDK_GT_CACHELINE_SIZE];
} VMDKGTCACHEENTRY, *PVMDKGTCACHEENTRY;

/**
 * Cache data structure for blocks of grain table entries. For now this is a
 * fixed size direct mapping cache, but this should be adapted to the size of
 * the sparse image and maybe converted to a set-associative cache. The
 * implementation below implements a write-through cache with write allocate.
 */
typedef struct VMDKGTCACHE
{
    /** Cache entries. */
    VMDKGTCACHEENTRY    aGTCache[VMDK_GT_CACHE_SIZE];
    /** Number of cache entries (currently unused). */
    unsigned            cEntries;
} VMDKGTCACHE, *PVMDKGTCACHE;

/**
 * Complete VMDK image data structure. Mainly a collection of extents and a few
 * extra global data fields.
 */
typedef struct VMDKIMAGE
{
    PVMDKEXTENT     pExtents;
    unsigned        cExtents;

    /** Base image name. */
    const char      *pszFilename;
    /** Descriptor file if applicable. */
    RTFILE          File;

    /** Error callback. */
    PFNVDERROR      pfnError;
    /** Opaque data for error callback. */
    void            *pvErrorUser;

    /** Open flags passed by VBoxHD layer. */
    unsigned        uOpenFlags;
    /** Image type. */
    VDIMAGETYPE     enmImageType;
    /** Image flags defined during creation or determined during open. */
    unsigned        uImageFlags;
    /** Total size of the image. */
    uint64_t        cbSize;
    /** BIOS translation mode. */
    PDMBIOSTRANSLATION enmTranslation;
    /** Physical geometry of this image, cylinders. */
    uint32_t        cCylinders;
    /** Physical geometry of this image, heads. */
    uint32_t        cHeads;
    /** Physical geometry of this image, sectors. */
    uint32_t        cSectors;
    /** Image UUID. */
    RTUUID          ImageUuid;
    /** Image modification UUID. */
    RTUUID          ModificationUuid;
    /** Parent image UUID. */
    RTUUID          ParentUuid;

    /** Pointer to the grain table cache, if this image contains sparse extents. */
    PVMDKGTCACHE    pGTCache;
    /** Pointer to the descriptor (NULL if no separate descriptor file). */
    char            *pDescData;
    /** Allocation size of the descriptor file. */
    size_t          cbDescAlloc;
    /** Parsed descriptor file content. */
    VMDKDESCRIPTOR  Descriptor;
} VMDKIMAGE, *PVMDKIMAGE;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/

static int vmdkReadGrainDirectory(PVMDKEXTENT pExtent);
static void vmdkFreeGrainDirectory(PVMDKEXTENT pExtent);

static int vmdkPreprocessDescriptor(PVMDKIMAGE pImage, char *pDescData, size_t cbDescData, PVMDKDESCRIPTOR pDescriptor);
static int vmdkReadMetaSparseExtent(PVMDKEXTENT pExtent);
static int vmdkWriteMetaSparseExtent(PVMDKEXTENT pExtent);
#ifdef VBOX_WITH_VMDK_ESX
static int vmdkReadMetaESXSparseExtent(PVMDKEXTENT pExtent);
#endif /* VBOX_WITH_VMDK_ESX */
static void vmdkFreeExtentData(PVMDKEXTENT pExtent, bool fDelete);

static int vmdkCreateExtents(PVMDKIMAGE pImage, unsigned cExtents);
static int vmdkOpenImage(PVMDKIMAGE pImage, const char *pszFilename, unsigned uOpenFlags);
static int vmdkFlushImage(PVMDKIMAGE pImage);
static int vmdkSetImageComment(PVMDKIMAGE pImage, const char *pszComment);
static void vmdkFreeImage(PVMDKIMAGE pImage, bool fDelete);

static int vmdkOpen(const char *pszFilename, unsigned uOpenFlags, PFNVDERROR pfnError, void *pvErrorUser, void **ppvBackendData);
static int vmdkClose(void *pBackendData, bool fDelete);


DECLINLINE(int) vmdkError(PVMDKIMAGE pImage, int rc, RT_SRC_POS_DECL, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    pImage->pfnError(pImage->pvErrorUser, rc, RT_SRC_POS_ARGS, pszFormat, va);
    va_end(va);
    return rc;
}

/**
 * Internal: truncate a string (at a UTF8 code point boundary) and encode the
 * critical non-ASCII characters.
 */
static char *vmdkEncodeString(const char *psz)
{
    /** @todo implement me. */
    return RTStrDup(psz);
}

/**
 * Internal: decode a string and store it into the specified string.
 */
static int vmdkDecodeString(const char *pszEncoded, char *psz, size_t cb)
{
    /** @todo implement me. */
    if (!cb)
        return VINF_SUCCESS;
    strncpy(psz, pszEncoded, cb);
    psz[cb - 1] = '\0';
    return VINF_SUCCESS;
}

static int vmdkReadGrainDirectory(PVMDKEXTENT pExtent)
{
    int rc = VINF_SUCCESS;
    unsigned i;
    uint32_t *pGD = NULL, *pRGD = NULL, *pGDTmp, *pRGDTmp;
    size_t cbGD = pExtent->cGDEntries * sizeof(uint32_t);

    pGD = (uint32_t *)RTMemAllocZ(cbGD);
    if (!pGD)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }
    pExtent->pGD = pGD;
    rc = RTFileReadAt(pExtent->File, VMDK_SECTOR2BYTE(pExtent->uSectorGD),
                      pGD, cbGD, NULL);
    AssertRC(rc);
    if (VBOX_FAILURE(rc))
    {
        rc = vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: could not read grain directory in '%s'"), pExtent->pszFullname);
        goto out;
    }
    for (i = 0, pGDTmp = pGD; i < pExtent->cGDEntries; i++, pGDTmp++)
        *pGDTmp = RT_LE2H_U32(*pGDTmp);

    if (pExtent->uSectorRGD)
    {
        pRGD = (uint32_t *)RTMemAllocZ(cbGD);
        if (!pRGD)
        {
            rc = VERR_NO_MEMORY;
            goto out;
        }
        pExtent->pRGD = pRGD;
        rc = RTFileReadAt(pExtent->File, VMDK_SECTOR2BYTE(pExtent->uSectorRGD),
                          pRGD, cbGD, NULL);
        AssertRC(rc);
        if (VBOX_FAILURE(rc))
        {
            rc = vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: could not read redundant grain directory in '%s'"), pExtent->pszFullname);
            goto out;
        }
        for (i = 0, pRGDTmp = pRGD; i < pExtent->cGDEntries; i++, pRGDTmp++)
            *pRGDTmp = RT_LE2H_U32(*pRGDTmp);

        /* Check grain table and redundant grain table for consistency. */
        size_t cbGT = pExtent->cGTEntries;
        uint32_t *pTmpGT1 = (uint32_t *)RTMemTmpAlloc(cbGT);
        if (!pTmpGT1)
        {
            rc = VERR_NO_MEMORY;
            goto out;
        }
        uint32_t *pTmpGT2 = (uint32_t *)RTMemTmpAlloc(cbGT);
        if (!pTmpGT2)
        {
            RTMemTmpFree(pTmpGT1);
            rc = VERR_NO_MEMORY;
            goto out;
        }

        for (i = 0, pGDTmp = pGD, pRGDTmp = pRGD; i < pExtent->cGDEntries; i++, pGDTmp++, pRGDTmp++)
        {
            /* If no grain table is allocated skip the entry. */
            if (*pGDTmp == 0 && *pRGDTmp == 0)
                continue;

            if (*pGDTmp == 0 || *pRGDTmp == 0 || *pGDTmp == *pRGDTmp)
            {
                /* Just one grain directory entry refers to a not yet allocated
                 * grain table or both grain directory copies refer to the same
                 * grain table. Not allowed. */
                RTMemTmpFree(pTmpGT1);
                RTMemTmpFree(pTmpGT2);
                rc = vmdkError(pExtent->pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: inconsistent references to grain directory in '%s'"), pExtent->pszFullname);
                goto out;
            }
            rc = RTFileReadAt(pExtent->File, VMDK_SECTOR2BYTE(*pGDTmp),
                              pTmpGT1, cbGT, NULL);
            if (VBOX_FAILURE(rc))
            {
                rc = vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: error reading grain table in '%s'"), pExtent->pszFullname);
                RTMemTmpFree(pTmpGT1);
                RTMemTmpFree(pTmpGT2);
                goto out;
            }
            rc = RTFileReadAt(pExtent->File, VMDK_SECTOR2BYTE(*pRGDTmp),
                              pTmpGT2, cbGT, NULL);
            if (VBOX_FAILURE(rc))
            {
                rc = vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: error reading backup grain table in '%s'"), pExtent->pszFullname);
                RTMemTmpFree(pTmpGT1);
                RTMemTmpFree(pTmpGT2);
                goto out;
            }
            if (memcmp(pTmpGT1, pTmpGT2, cbGT))
            {
                RTMemTmpFree(pTmpGT1);
                RTMemTmpFree(pTmpGT2);
                rc = vmdkError(pExtent->pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: inconsistency between grain table and backup grain table in '%s'"), pExtent->pszFullname);
                goto out;
            }
        }

        /** @todo figure out what to do for unclean VMDKs. */
    }

out:
    if (VBOX_FAILURE(rc))
        vmdkFreeGrainDirectory(pExtent);
    return rc;
}

static int vmdkCreateGrainDirectory(PVMDKEXTENT pExtent, uint64_t uStartSector, bool fPreAlloc)
{
    int rc = VINF_SUCCESS;
    unsigned i;
    uint32_t *pGD = NULL, *pRGD = NULL;
    size_t cbGD = pExtent->cGDEntries * sizeof(uint32_t);
    size_t cbGDRounded = RT_ALIGN_64(pExtent->cGDEntries * sizeof(uint32_t), 512);
    size_t cbGTRounded;
    uint64_t cbOverhead;

    if (fPreAlloc)
        cbGTRounded = RT_ALIGN_64(pExtent->cGDEntries * pExtent->cGTEntries * sizeof(uint32_t), 512);
    else
        cbGTRounded = 0;

    pGD = (uint32_t *)RTMemAllocZ(cbGD);
    if (!pGD)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }
    pExtent->pGD = pGD;
    pRGD = (uint32_t *)RTMemAllocZ(cbGD);
    if (!pRGD)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }
    pExtent->pRGD = pRGD;

    cbOverhead = RT_ALIGN_64(VMDK_SECTOR2BYTE(uStartSector) + 2 * (cbGDRounded + cbGTRounded), VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain));
    rc = RTFileSetSize(pExtent->File, cbOverhead);
    if (VBOX_FAILURE(rc))
        goto out;
    pExtent->uSectorRGD = uStartSector;
    pExtent->uSectorGD = uStartSector + VMDK_BYTE2SECTOR(cbGDRounded + cbGTRounded);

    if (fPreAlloc)
    {
        uint32_t uGTSectorLE;
        uint32_t uOffsetSectors;

        uOffsetSectors = pExtent->uSectorRGD + VMDK_BYTE2SECTOR(cbGDRounded);
        for (i = 0; i < pExtent->cGDEntries; i++)
        {
            pRGD[i] = uOffsetSectors;
            uGTSectorLE = RT_H2LE_U64(uOffsetSectors);
            /* Write the redundant grain directory entry to disk. */
            rc = RTFileWriteAt(pExtent->File, VMDK_SECTOR2BYTE(pExtent->uSectorRGD) + i * sizeof(uGTSectorLE), &uGTSectorLE, sizeof(uGTSectorLE), NULL);
            if (VBOX_FAILURE(rc))
                return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: cannot write new redundant grain directory entry in '%s'"), pExtent->pszFullname);
            uOffsetSectors += VMDK_BYTE2SECTOR(pExtent->cGTEntries * sizeof(uint32_t));
        }

        uOffsetSectors = pExtent->uSectorGD + VMDK_BYTE2SECTOR(cbGDRounded);
        for (i = 0; i < pExtent->cGDEntries; i++)
        {
            pGD[i] = uOffsetSectors;
            uGTSectorLE = RT_H2LE_U64(uOffsetSectors);
            /* Write the grain directory entry to disk. */
            rc = RTFileWriteAt(pExtent->File, VMDK_SECTOR2BYTE(pExtent->uSectorGD) + i * sizeof(uGTSectorLE), &uGTSectorLE, sizeof(uGTSectorLE), NULL);
            if (VBOX_FAILURE(rc))
                return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: cannot write new grain directory entry in '%s'"), pExtent->pszFullname);
            uOffsetSectors += VMDK_BYTE2SECTOR(pExtent->cGTEntries * sizeof(uint32_t));
        }
    }
    pExtent->cOverheadSectors = VMDK_BYTE2SECTOR(cbOverhead);

out:
    if (VBOX_FAILURE(rc))
        vmdkFreeGrainDirectory(pExtent);
    return rc;
}

static void vmdkFreeGrainDirectory(PVMDKEXTENT pExtent)
{
    if (pExtent->pGD)
    {
        RTMemFree(pExtent->pGD);
        pExtent->pGD = NULL;
    }
    if (pExtent->pRGD)
    {
        RTMemFree(pExtent->pRGD);
        pExtent->pRGD = NULL;
    }
}

static int vmdkStringUnquote(PVMDKIMAGE pImage, const char *pszStr, char **ppszUnquoted, char **ppszNext)
{
    char *pszQ;
    char *pszUnquoted;

    /* Skip over whitespace. */
    while (*pszStr == ' ' || *pszStr == '\t')
        pszStr++;
    if (*pszStr++ != '"')
        return vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: incorrectly quoted value in descriptor in '%s'"), pImage->pszFilename);

    pszQ = (char*)strchr(pszStr, '"');
    if (pszQ == NULL)
        return vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: incorrectly quoted value in descriptor in '%s'"), pImage->pszFilename);
    pszUnquoted = (char *)RTMemTmpAlloc(pszQ - pszStr + 1);
    if (!pszUnquoted)
        return VERR_NO_MEMORY;
    memcpy(pszUnquoted, pszStr, pszQ - pszStr);
    pszUnquoted[pszQ - pszStr] = '\0';
    *ppszUnquoted = pszUnquoted;
    if (ppszNext)
        *ppszNext = pszQ + 1;
    return VINF_SUCCESS;
}

static int vmdkDescInitStr(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                           const char *pszLine)
{
    char *pEnd = pDescriptor->aLines[pDescriptor->cLines];
    ssize_t cbDiff = strlen(pszLine) + 1;

    if (    pDescriptor->cLines >= VMDK_DESCRIPTOR_LINES_MAX - 1
        &&  pEnd - pDescriptor->aLines[0] > (ptrdiff_t)pDescriptor->cbDescAlloc - cbDiff)
        return vmdkError(pImage, VERR_BUFFER_OVERFLOW, RT_SRC_POS, N_("VMDK: descriptor too big in '%s'"), pImage->pszFilename);

    memcpy(pEnd, pszLine, cbDiff);
    pDescriptor->cLines++;
    pDescriptor->aLines[pDescriptor->cLines] = pEnd + cbDiff;
    pDescriptor->fDirty = true;

    return VINF_SUCCESS;
}

static bool vmdkDescGetStr(PVMDKDESCRIPTOR pDescriptor, unsigned uStart,
                           const char *pszKey, const char **ppszValue)
{
    size_t cbKey = strlen(pszKey);
    const char *pszValue;

    while (uStart != 0)
    {
        if (!strncmp(pDescriptor->aLines[uStart], pszKey, cbKey))
        {
            /* Key matches, check if there is a '=' (preceded by whitespace). */
            pszValue = pDescriptor->aLines[uStart] + cbKey;
            while (*pszValue == ' ' || *pszValue == '\t')
                pszValue++;
            if (*pszValue == '=')
            {
                *ppszValue = pszValue + 1;
                break;
            }
        }
        uStart = pDescriptor->aNextLines[uStart];
    }
    return !!uStart;
}

static int vmdkDescSetStr(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                          unsigned uStart,
                          const char *pszKey, const char *pszValue)
{
    char *pszTmp;
    size_t cbKey = strlen(pszKey);
    unsigned uLast = 0;

    while (uStart != 0)
    {
        if (!strncmp(pDescriptor->aLines[uStart], pszKey, cbKey))
        {
            /* Key matches, check if there is a '=' (preceded by whitespace). */
            pszTmp = pDescriptor->aLines[uStart] + cbKey;
            while (*pszTmp == ' ' || *pszTmp == '\t')
                pszTmp++;
            if (*pszTmp == '=')
            {
                while (*pszTmp == ' ' || *pszTmp == '\t')
                    pszTmp++;
                break;
            }
        }
        if (!pDescriptor->aNextLines[uStart])
            uLast = uStart;
        uStart = pDescriptor->aNextLines[uStart];
    }
    if (uStart)
    {
        if (pszValue)
        {
            /* Key already exists, replace existing value. */
            size_t cbOldVal = strlen(pszTmp);
            size_t cbNewVal = strlen(pszValue);
            ssize_t cbDiff = cbNewVal - cbOldVal;
            /* Check for buffer overflow. */
            if (    pDescriptor->aLines[pDescriptor->cLines]
                -   pDescriptor->aLines[0] > (ptrdiff_t)pDescriptor->cbDescAlloc - cbDiff)
                return vmdkError(pImage, VERR_BUFFER_OVERFLOW, RT_SRC_POS, N_("VMDK: descriptor too big in '%s'"), pImage->pszFilename);

            memmove(pszTmp + cbNewVal, pszTmp + cbOldVal,
                    pDescriptor->aLines[pDescriptor->cLines] - pszTmp - cbOldVal);
            memcpy(pszTmp, pszValue, cbNewVal + 1);
            for (unsigned i = uStart + 1; i <= pDescriptor->cLines; i++)
                pDescriptor->aLines[i] += cbDiff;
        }
        else
        {
            memmove(pDescriptor->aLines[uStart], pDescriptor->aLines[uStart+1],
                    pDescriptor->aLines[pDescriptor->cLines] - pDescriptor->aLines[uStart+1] + 1);
            for (unsigned i = uStart + 1; i <= pDescriptor->cLines; i++)
            {
                pDescriptor->aLines[i-1] = pDescriptor->aLines[i];
                if (pDescriptor->aNextLines[i])
                    pDescriptor->aNextLines[i-1] = pDescriptor->aNextLines[i] - 1;
                else
                    pDescriptor->aNextLines[i-1] = 0;
            }
            pDescriptor->cLines--;
            /* Adjust starting line numbers of following descriptor sections. */
            if (uStart < pDescriptor->uFirstExtent)
                pDescriptor->uFirstExtent--;
            if (uStart < pDescriptor->uFirstDDB)
                pDescriptor->uFirstDDB--;
        }
    }
    else
    {
        /* Key doesn't exist, append it after the last entry in this category. */
        if (!pszValue)
        {
            /* Key doesn't exist, and it should be removed. Simply a no-op. */
            return VINF_SUCCESS;
        }
        size_t cbKey = strlen(pszKey);
        size_t cbValue = strlen(pszValue);
        ssize_t cbDiff = cbKey + 1 + cbValue + 1;
        /* Check for buffer overflow. */
        if (   (pDescriptor->cLines >= VMDK_DESCRIPTOR_LINES_MAX - 1)
            || (  pDescriptor->aLines[pDescriptor->cLines]
                - pDescriptor->aLines[0] > (ptrdiff_t)pDescriptor->cbDescAlloc - cbDiff))
            return vmdkError(pImage, VERR_BUFFER_OVERFLOW, RT_SRC_POS, N_("VMDK: descriptor too big in '%s'"), pImage->pszFilename);
        for (unsigned i = pDescriptor->cLines + 1; i > uLast + 1; i--)
        {
            pDescriptor->aLines[i] = pDescriptor->aLines[i - 1];
            if (pDescriptor->aNextLines[i - 1])
                pDescriptor->aNextLines[i] = pDescriptor->aNextLines[i - 1] + 1;
            else
                pDescriptor->aNextLines[i] = 0;
        }
        uStart = uLast + 1;
        pDescriptor->aNextLines[uLast] = uStart;
        pDescriptor->aNextLines[uStart] = 0;
        pDescriptor->cLines++;
        pszTmp = pDescriptor->aLines[uStart];
        memmove(pszTmp + cbDiff, pszTmp,
                pDescriptor->aLines[pDescriptor->cLines] - pszTmp);
        memcpy(pDescriptor->aLines[uStart], pszKey, cbKey);
        pDescriptor->aLines[uStart][cbKey] = '=';
        memcpy(pDescriptor->aLines[uStart] + cbKey + 1, pszValue, cbValue + 1);
        for (unsigned i = uStart + 1; i <= pDescriptor->cLines; i++)
            pDescriptor->aLines[i] += cbDiff;

        /* Adjust starting line numbers of following descriptor sections. */
        if (uStart <= pDescriptor->uFirstExtent)
            pDescriptor->uFirstExtent++;
        if (uStart <= pDescriptor->uFirstDDB)
            pDescriptor->uFirstDDB++;
    }
    pDescriptor->fDirty = true;
    return VINF_SUCCESS;
}

static int vmdkDescBaseGetU32(PVMDKDESCRIPTOR pDescriptor, const char *pszKey,
                              uint32_t *puValue)
{
    const char *pszValue;

    if (!vmdkDescGetStr(pDescriptor, pDescriptor->uFirstDesc, pszKey, &pszValue))
        return VERR_VDI_VALUE_NOT_FOUND;
    return RTStrToUInt32Ex(pszValue, NULL, 10, puValue);
}

static int vmdkDescBaseGetStr(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                              const char *pszKey, const char **ppszValue)
{
    const char *pszValue;
    char *pszValueUnquoted;

    if (!vmdkDescGetStr(pDescriptor, pDescriptor->uFirstDesc, pszKey, &pszValue))
        return VERR_VDI_VALUE_NOT_FOUND;
    int rc = vmdkStringUnquote(pImage, pszValue, &pszValueUnquoted, NULL);
    if (VBOX_FAILURE(rc))
        return rc;
    *ppszValue = pszValueUnquoted;
    return rc;
}

static int vmdkDescBaseSetStr(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                              const char *pszKey, const char *pszValue)
{
    char *pszValueQuoted;

    int rc = RTStrAPrintf(&pszValueQuoted, "\"%s\"", pszValue);
    if (VBOX_FAILURE(rc))
        return rc;
    rc = vmdkDescSetStr(pImage, pDescriptor, pDescriptor->uFirstDesc, pszKey, pszValueQuoted);
    RTStrFree(pszValueQuoted);
    return rc;
}

static void vmdkDescExtRemoveDummy(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor)
{
    unsigned uEntry = pDescriptor->uFirstExtent;
    ssize_t cbDiff;

    if (!uEntry)
        return;

    cbDiff = strlen(pDescriptor->aLines[uEntry]) + 1;
    /* Move everything including the \0 in the entry marking the end of buffer. */
    memmove(pDescriptor->aLines[uEntry], pDescriptor->aLines[uEntry + 1],
            pDescriptor->aLines[pDescriptor->cLines] - pDescriptor->aLines[uEntry + 1] + 1);
    for (unsigned i = uEntry + 1; i <= pDescriptor->cLines; i++)
    {
        pDescriptor->aLines[i - 1] = pDescriptor->aLines[i] - cbDiff;
        if (pDescriptor->aNextLines[i])
            pDescriptor->aNextLines[i - 1] = pDescriptor->aNextLines[i] - 1;
        else
            pDescriptor->aNextLines[i - 1] = 0;
    }
    pDescriptor->cLines--;
    if (pDescriptor->uFirstDDB)
        pDescriptor->uFirstDDB--;

    return;
}

static int vmdkDescExtInsert(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                             VMDKACCESS enmAccess, uint64_t cNominalSectors,
                             VMDKETYPE enmType, const char *pszBasename,
                             uint64_t uSectorOffset)
{
    static const char *apszAccess[] = { "NOACCESS", "RDONLY", "RW" };
    static const char *apszType[] = { "", "SPARSE", "FLAT", "ZERO" };
    char *pszTmp;
    unsigned uStart = pDescriptor->uFirstExtent, uLast = 0;
    char szExt[1024];
    ssize_t cbDiff;

    /* Find last entry in extent description. */
    while (uStart)
    {
        if (!pDescriptor->aNextLines[uStart])
            uLast = uStart;
        uStart = pDescriptor->aNextLines[uStart];
    }

    if (enmType == VMDKETYPE_ZERO)
    {
        RTStrPrintf(szExt, sizeof(szExt), "%s %llu %s ", apszAccess[enmAccess],
                    cNominalSectors, apszType[enmType]);
    }
    else
    {
        if (!uSectorOffset)
            RTStrPrintf(szExt, sizeof(szExt), "%s %llu %s \"%s\"",
                        apszAccess[enmAccess], cNominalSectors,
                        apszType[enmType], pszBasename);
        else
            RTStrPrintf(szExt, sizeof(szExt), "%s %llu %s \"%s\" %llu",
                        apszAccess[enmAccess], cNominalSectors,
                        apszType[enmType], pszBasename, uSectorOffset);
    }
    cbDiff = strlen(szExt) + 1;

    /* Check for buffer overflow. */
    if (   (pDescriptor->cLines >= VMDK_DESCRIPTOR_LINES_MAX - 1)
        || (  pDescriptor->aLines[pDescriptor->cLines]
            - pDescriptor->aLines[0] > (ptrdiff_t)pDescriptor->cbDescAlloc - cbDiff))
        return vmdkError(pImage, VERR_BUFFER_OVERFLOW, RT_SRC_POS, N_("VMDK: descriptor too big in '%s'"), pImage->pszFilename);

    for (unsigned i = pDescriptor->cLines + 1; i > uLast + 1; i--)
    {
        pDescriptor->aLines[i] = pDescriptor->aLines[i - 1];
        if (pDescriptor->aNextLines[i - 1])
            pDescriptor->aNextLines[i] = pDescriptor->aNextLines[i - 1] + 1;
        else
            pDescriptor->aNextLines[i] = 0;
    }
    uStart = uLast + 1;
    pDescriptor->aNextLines[uLast] = uStart;
    pDescriptor->aNextLines[uStart] = 0;
    pDescriptor->cLines++;
    pszTmp = pDescriptor->aLines[uStart];
    memmove(pszTmp + cbDiff, pszTmp,
            pDescriptor->aLines[pDescriptor->cLines] - pszTmp);
    memcpy(pDescriptor->aLines[uStart], szExt, cbDiff);
    for (unsigned i = uStart + 1; i <= pDescriptor->cLines; i++)
        pDescriptor->aLines[i] += cbDiff;

    /* Adjust starting line numbers of following descriptor sections. */
    if (uStart <= pDescriptor->uFirstDDB)
        pDescriptor->uFirstDDB++;

    pDescriptor->fDirty = true;
    return VINF_SUCCESS;
}

static int vmdkDescDDBGetStr(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                             const char *pszKey, const char **ppszValue)
{
    const char *pszValue;
    char *pszValueUnquoted;

    if (!vmdkDescGetStr(pDescriptor, pDescriptor->uFirstDDB, pszKey, &pszValue))
        return VERR_VDI_VALUE_NOT_FOUND;
    int rc = vmdkStringUnquote(pImage, pszValue, &pszValueUnquoted, NULL);
    if (VBOX_FAILURE(rc))
        return rc;
    *ppszValue = pszValueUnquoted;
    return rc;
}

static int vmdkDescDDBGetU32(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                             const char *pszKey, uint32_t *puValue)
{
    const char *pszValue;
    char *pszValueUnquoted;

    if (!vmdkDescGetStr(pDescriptor, pDescriptor->uFirstDDB, pszKey, &pszValue))
        return VERR_VDI_VALUE_NOT_FOUND;
    int rc = vmdkStringUnquote(pImage, pszValue, &pszValueUnquoted, NULL);
    if (VBOX_FAILURE(rc))
        return rc;
    rc = RTStrToUInt32Ex(pszValueUnquoted, NULL, 10, puValue);
    RTMemTmpFree(pszValueUnquoted);
    return rc;
}

static int vmdkDescDDBGetUuid(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor,
                              const char *pszKey, PRTUUID pUuid)
{
    const char *pszValue;
    char *pszValueUnquoted;

    if (!vmdkDescGetStr(pDescriptor, pDescriptor->uFirstDDB, pszKey, &pszValue))
        return VERR_VDI_VALUE_NOT_FOUND;
    int rc = vmdkStringUnquote(pImage, pszValue, &pszValueUnquoted, NULL);
    if (VBOX_FAILURE(rc))
        return rc;
    rc = RTUuidFromStr(pUuid, pszValueUnquoted);
    RTMemTmpFree(pszValueUnquoted);
    return rc;
}

static int vmdkDescDDBSetStr(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor, const char *pszKey, const char *pszVal)
{
    int rc;
    char *pszValQuoted;

    if (pszVal)
    {
        rc = RTStrAPrintf(&pszValQuoted, "\"%s\"", pszVal);
        if (VBOX_FAILURE(rc))
            return rc;
    }
    else
        pszValQuoted = NULL;
    rc = vmdkDescSetStr(pImage, pDescriptor, pDescriptor->uFirstDDB, pszKey, pszValQuoted);
    if (pszValQuoted)
        RTStrFree(pszValQuoted);
    return rc;
}

static int vmdkDescDDBSetUuid(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor, const char *pszKey, PCRTUUID pUuid)
{
    char *pszUuid;

    int rc = RTStrAPrintf(&pszUuid, "\"%Vuuid\"", pUuid);
    if (VBOX_FAILURE(rc))
        return rc;
    rc = vmdkDescSetStr(pImage, pDescriptor, pDescriptor->uFirstDDB, pszKey, pszUuid);
    RTStrFree(pszUuid);
    return rc;
}

int vmdkDescDDBSetU32(PVMDKIMAGE pImage, PVMDKDESCRIPTOR pDescriptor, const char *pszKey, uint32_t uValue)
{
    char *pszValue;

    int rc = RTStrAPrintf(&pszValue, "\"%d\"", uValue);
    if (VBOX_FAILURE(rc))
        return rc;
    rc = vmdkDescSetStr(pImage, pDescriptor, pDescriptor->uFirstDDB, pszKey, pszValue);
    RTStrFree(pszValue);
    return rc;
}

static int vmdkPreprocessDescriptor(PVMDKIMAGE pImage, char *pDescData, size_t cbDescData, PVMDKDESCRIPTOR pDescriptor)
{
    int rc = VINF_SUCCESS;
    unsigned cLine = 0, uLastNonEmptyLine = 0;
    char *pTmp = pDescData;

    pDescriptor->cbDescAlloc = cbDescData;
    while (*pTmp != '\0')
    {
        pDescriptor->aLines[cLine++] = pTmp;
        if (cLine >= VMDK_DESCRIPTOR_LINES_MAX)
        {
            rc = vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: descriptor too big in '%s'"), pImage->pszFilename);
            goto out;
        }

        while (*pTmp != '\0' && *pTmp != '\n')
        {
            if (*pTmp == '\r')
            {
                if (*(pTmp + 1) != '\n')
                {
                    rc = vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: unsupported end of line in descriptor in '%s'"), pImage->pszFilename);
                    goto out;
                }
                else
                {
                    /* Get rid of CR character. */
                    *pTmp = '\0';
                }
            }
            pTmp++;
        }
        /* Get rid of LF character. */
        if (*pTmp == '\n')
        {
            *pTmp = '\0';
            pTmp++;
        }
    }
    pDescriptor->cLines = cLine;
    /* Pointer right after the end of the used part of the buffer. */
    pDescriptor->aLines[cLine] = pTmp;

    if (strcmp(pDescriptor->aLines[0], "# Disk DescriptorFile"))
    {
        rc = vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: descriptor does not start as expected in '%s'"), pImage->pszFilename);
        goto out;
    }

    /* Initialize those, because we need to be able to reopen an image. */
    pDescriptor->uFirstDesc = 0;
    pDescriptor->uFirstExtent = 0;
    pDescriptor->uFirstDDB = 0;
    for (unsigned i = 0; i < cLine; i++)
    {
        if (*pDescriptor->aLines[i] != '#' && *pDescriptor->aLines[i] != '\0')
        {
            if (    !strncmp(pDescriptor->aLines[i], "RW", 2)
                ||  !strncmp(pDescriptor->aLines[i], "RDONLY", 6)
                ||  !strncmp(pDescriptor->aLines[i], "NOACCESS", 8) )
            {
                /* An extent descriptor. */
                if (!pDescriptor->uFirstDesc || pDescriptor->uFirstDDB)
                {
                    /* Incorrect ordering of entries. */
                    rc = vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: incorrect ordering of entries in descriptor in '%s'"), pImage->pszFilename);
                    goto out;
                }
                if (!pDescriptor->uFirstExtent)
                {
                    pDescriptor->uFirstExtent = i;
                    uLastNonEmptyLine = 0;
                }
            }
            else if (!strncmp(pDescriptor->aLines[i], "ddb.", 4))
            {
                /* A disk database entry. */
                if (!pDescriptor->uFirstDesc || !pDescriptor->uFirstExtent)
                {
                    /* Incorrect ordering of entries. */
                    rc = vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: incorrect ordering of entries in descriptor in '%s'"), pImage->pszFilename);
                    goto out;
                }
                if (!pDescriptor->uFirstDDB)
                {
                    pDescriptor->uFirstDDB = i;
                    uLastNonEmptyLine = 0;
                }
            }
            else
            {
                /* A normal entry. */
                if (pDescriptor->uFirstExtent || pDescriptor->uFirstDDB)
                {
                    /* Incorrect ordering of entries. */
                    rc = vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: incorrect ordering of entries in descriptor in '%s'"), pImage->pszFilename);
                    goto out;
                }
                if (!pDescriptor->uFirstDesc)
                {
                    pDescriptor->uFirstDesc = i;
                    uLastNonEmptyLine = 0;
                }
            }
            if (uLastNonEmptyLine)
                pDescriptor->aNextLines[uLastNonEmptyLine] = i;
            uLastNonEmptyLine = i;
        }
    }

out:
    return rc;
}

static int vmdkCreateDescriptor(PVMDKIMAGE pImage, char *pDescData, size_t cbDescData, PVMDKDESCRIPTOR pDescriptor)
{
    int rc;

    pDescriptor->uFirstDesc = 0;
    pDescriptor->uFirstExtent = 0;
    pDescriptor->uFirstDDB = 0;
    pDescriptor->cLines = 0;
    pDescriptor->cbDescAlloc = cbDescData;
    pDescriptor->fDirty = false;
    pDescriptor->aLines[pDescriptor->cLines] = pDescData;
    memset(pDescriptor->aNextLines, '\0', sizeof(pDescriptor->aNextLines));

    rc = vmdkDescInitStr(pImage, pDescriptor, "# Disk DescriptorFile");
    if (VBOX_FAILURE(rc))
        goto out;
    rc = vmdkDescInitStr(pImage, pDescriptor, "version=1");
    if (VBOX_FAILURE(rc))
        goto out;
    pDescriptor->uFirstDesc = pDescriptor->cLines - 1;
    rc = vmdkDescInitStr(pImage, pDescriptor, "");
    if (VBOX_FAILURE(rc))
        goto out;
    rc = vmdkDescInitStr(pImage, pDescriptor, "# Extent description");
    if (VBOX_FAILURE(rc))
        goto out;
    rc = vmdkDescInitStr(pImage, pDescriptor, "NOACCESS 0 ZERO ");
    if (VBOX_FAILURE(rc))
        goto out;
    pDescriptor->uFirstExtent = pDescriptor->cLines - 1;
    rc = vmdkDescInitStr(pImage, pDescriptor, "");
    if (VBOX_FAILURE(rc))
        goto out;
    /* The trailing space is created by VMware, too. */
    rc = vmdkDescInitStr(pImage, pDescriptor, "# The disk Data Base ");
    if (VBOX_FAILURE(rc))
        goto out;
    rc = vmdkDescInitStr(pImage, pDescriptor, "#DDB");
    if (VBOX_FAILURE(rc))
        goto out;
    rc = vmdkDescInitStr(pImage, pDescriptor, "");
    if (VBOX_FAILURE(rc))
        goto out;
    rc = vmdkDescInitStr(pImage, pDescriptor, "ddb.virtualHWVersion = \"4\"");
    if (VBOX_FAILURE(rc))
        goto out;
    pDescriptor->uFirstDDB = pDescriptor->cLines - 1;

    /* Now that the framework is in place, use the normal functions to insert
     * the remaining keys. */
    char szBuf[9];
    RTStrPrintf(szBuf, sizeof(szBuf), "%08x", RTRandU32());
    rc = vmdkDescSetStr(pImage, pDescriptor, pDescriptor->uFirstDesc, "CID", szBuf);
    if (VBOX_FAILURE(rc))
        goto out;
    rc = vmdkDescSetStr(pImage, pDescriptor, pDescriptor->uFirstDesc, "parentCID", "ffffffff");
    if (VBOX_FAILURE(rc))
        goto out;

    rc = vmdkDescDDBSetStr(pImage, pDescriptor, "ddb.adapterType", "ide");
    if (VBOX_FAILURE(rc))
        goto out;

out:
    return rc;
}

static int vmdkParseDescriptor(PVMDKIMAGE pImage, char *pDescData, size_t cbDescData)
{
    int rc;
    unsigned cExtents;
    unsigned uLine;

    rc = vmdkPreprocessDescriptor(pImage, pDescData, cbDescData, &pImage->Descriptor);
    if (VBOX_FAILURE(rc))
        return rc;

    /* Check version, must be 1. */
    uint32_t uVersion;
    rc = vmdkDescBaseGetU32(&pImage->Descriptor, "version", &uVersion);
    if (VBOX_FAILURE(rc))
        return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error finding key 'version' in descriptor in '%s'"), pImage->pszFilename);
    if (uVersion != 1)
        return vmdkError(pImage, VERR_VDI_UNSUPPORTED_VERSION, RT_SRC_POS, N_("VMDK: unsupported format version in descriptor in '%s'"), pImage->pszFilename);

    /* Get image creation type and determine image flags. */
    const char *pszCreateType;
    rc = vmdkDescBaseGetStr(pImage, &pImage->Descriptor, "createType",
                            &pszCreateType);
    if (VBOX_FAILURE(rc))
        return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: cannot get image type from descriptor in '%s'"), pImage->pszFilename);
    if (    !strcmp(pszCreateType, "twoGbMaxExtentSparse")
        ||  !strcmp(pszCreateType, "twoGbMaxExtentFlat"))
        pImage->uImageFlags = VD_VMDK_IMAGE_FLAGS_SPLIT_2G;
    if (    !strcmp(pszCreateType, "partitionedDevice")
        ||  !strcmp(pszCreateType, "fullDevice"))
        pImage->uImageFlags = VD_VMDK_IMAGE_FLAGS_RAWDISK;
    else
        pImage->uImageFlags = 0;
    RTStrFree((char *)(void *)pszCreateType);

    /* Count the number of extent config entries. */
    for (uLine = pImage->Descriptor.uFirstExtent, cExtents = 0;
         uLine != 0;
         uLine = pImage->Descriptor.aNextLines[uLine], cExtents++)
        /* nothing */;

    if (!pImage->pDescData && cExtents != 1)
    {
        /* Monolithic image, must have only one extent (already opened). */
        return vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: monolithic image may only have one extent in '%s'"), pImage->pszFilename);
    }

    if (pImage->pDescData)
    {
        /* Non-monolithic image, extents need to be allocated. */
        rc = vmdkCreateExtents(pImage, cExtents);
        if (VBOX_FAILURE(rc))
            return rc;
    }

    for (unsigned i = 0, uLine = pImage->Descriptor.uFirstExtent;
         i < cExtents; i++, uLine = pImage->Descriptor.aNextLines[uLine])
    {
        char *pszLine = pImage->Descriptor.aLines[uLine];

        /* Access type of the extent. */
        if (!strncmp(pszLine, "RW", 2))
        {
            pImage->pExtents[i].enmAccess = VMDKACCESS_READWRITE;
            pszLine += 2;
        }
        else if (!strncmp(pszLine, "RDONLY", 6))
        {
            pImage->pExtents[i].enmAccess = VMDKACCESS_READONLY;
            pszLine += 6;
        }
        else if (!strncmp(pszLine, "NOACCESS", 8))
        {
            pImage->pExtents[i].enmAccess = VMDKACCESS_NOACCESS;
            pszLine += 8;
        }
        else
            return vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);
        if (*pszLine++ != ' ')
            return vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);

        /* Nominal size of the extent. */
        rc = RTStrToUInt64Ex(pszLine, &pszLine, 10,
                             &pImage->pExtents[i].cNominalSectors);
        if (VBOX_FAILURE(rc))
            return vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);
        if (*pszLine++ != ' ')
            return vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);

        /* Type of the extent. */
#ifdef VBOX_WITH_VMDK_ESX
        /** @todo Add the ESX extent types. Not necessary for now because
         * the ESX extent types are only used inside an ESX server. They are
         * automatically converted if the VMDK is exported. */
#endif /* VBOX_WITH_VMDK_ESX */
        if (!strncmp(pszLine, "SPARSE", 6))
        {
            pImage->pExtents[i].enmType = VMDKETYPE_HOSTED_SPARSE;
            pszLine += 6;
        }
        else if (!strncmp(pszLine, "FLAT", 4))
        {
            pImage->pExtents[i].enmType = VMDKETYPE_FLAT;
            pszLine += 4;
        }
        else if (!strncmp(pszLine, "ZERO", 4))
        {
            pImage->pExtents[i].enmType = VMDKETYPE_ZERO;
            pszLine += 4;
        }
        else
            return vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);
        if (pImage->pExtents[i].enmType == VMDKETYPE_ZERO)
        {
            /* This one has no basename or offset. */
            if (*pszLine == ' ')
                pszLine++;
            if (*pszLine != '\0')
                return vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);
            pImage->pExtents[i].pszBasename = NULL;
        }
        else
        {
            /* All other extent types have basename and optional offset. */
            if (*pszLine++ != ' ')
                return vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);

            /* Basename of the image. Surrounded by quotes. */
            char *pszBasename;
            rc = vmdkStringUnquote(pImage, pszLine, &pszBasename, &pszLine);
            if (VBOX_FAILURE(rc))
                return rc;
            pImage->pExtents[i].pszBasename = pszBasename;
            if (*pszLine == ' ')
            {
                pszLine++;
                if (*pszLine != '\0')
                {
                    /* Optional offset in extent specified. */
                    rc = RTStrToUInt64Ex(pszLine, &pszLine, 10,
                                         &pImage->pExtents[i].uSectorOffset);
                    if (VBOX_FAILURE(rc))
                        return vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);
                }
            }

            if (*pszLine != '\0')
                return vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: parse error in extent description in '%s'"), pImage->pszFilename);
        }
    }

    rc = vmdkDescDDBGetU32(pImage, &pImage->Descriptor,
                           "ddb.geometry.cylinders", &pImage->cCylinders);
    if (VBOX_FAILURE(rc))
        return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error getting CHS geometry from extent description in '%s'"), pImage->pszFilename);
    rc = vmdkDescDDBGetU32(pImage, &pImage->Descriptor,
                           "ddb.geometry.heads", &pImage->cHeads);
    if (VBOX_FAILURE(rc))
        return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error getting CHS geometry from extent description in '%s'"), pImage->pszFilename);
    rc = vmdkDescDDBGetU32(pImage, &pImage->Descriptor,
                           "ddb.geometry.sectors", &pImage->cSectors);
    if (VBOX_FAILURE(rc))
        return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error getting CHS geometry from extent description in '%s'"), pImage->pszFilename);
    if (pImage->cCylinders >= 1024 || pImage->cHeads != 16)
        pImage->enmTranslation = PDMBIOSTRANSLATION_LBA;
    else
        pImage->enmTranslation = PDMBIOSTRANSLATION_NONE;

    /* Get image UUID. */
    rc = vmdkDescDDBGetUuid(pImage, &pImage->Descriptor, "ddb.uuid.image",
                            &pImage->ImageUuid);
    if (rc == VERR_VDI_VALUE_NOT_FOUND)
    {
        /* Image without UUID. Probably created by VMware and not yet used
         * by VirtualBox. Can only be added for images opened in read/write
         * mode, so don't bother producing a sensible UUID otherwise. */
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
            RTUuidClear(&pImage->ImageUuid);
        else
        {
            rc = RTUuidCreate(&pImage->ImageUuid);
            if (VBOX_FAILURE(rc))
                return rc;
            rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                    "ddb.uuid.image", &pImage->ImageUuid);
            if (VBOX_FAILURE(rc))
                return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error storing image UUID in descriptor in '%s'"), pImage->pszFilename);
        }
    }
    else if (VBOX_FAILURE(rc))
        return rc;

    /* Get image modification UUID. */
    rc = vmdkDescDDBGetUuid(pImage, &pImage->Descriptor, "ddb.uuid.modification",
                            &pImage->ModificationUuid);
    if (rc == VERR_VDI_VALUE_NOT_FOUND)
    {
        /* Image without UUID. Probably created by VMware and not yet used
         * by VirtualBox. Can only be added for images opened in read/write
         * mode, so don't bother producing a sensible UUID otherwise. */
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
            RTUuidClear(&pImage->ModificationUuid);
        else
        {
            rc = RTUuidCreate(&pImage->ModificationUuid);
            if (VBOX_FAILURE(rc))
                return rc;
            rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                    "ddb.uuid.modification", &pImage->ModificationUuid);
            if (VBOX_FAILURE(rc))
                return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error storing image modification UUID in descriptor in '%s'"), pImage->pszFilename);
        }
    }
    else if (VBOX_FAILURE(rc))
        return rc;

    /* Get UUID of parent image. */
    rc = vmdkDescDDBGetUuid(pImage, &pImage->Descriptor, "ddb.uuid.parent",
                            &pImage->ParentUuid);
    if (rc == VERR_VDI_VALUE_NOT_FOUND)
    {
        /* Image without UUID. Probably created by VMware and not yet used
         * by VirtualBox. Can only be added for images opened in read/write
         * mode, so don't bother producing a sensible UUID otherwise. */
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
            RTUuidClear(&pImage->ParentUuid);
        else
        {
            rc = RTUuidClear(&pImage->ParentUuid);
            if (VBOX_FAILURE(rc))
                return rc;
            rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                    "ddb.uuid.parent", &pImage->ParentUuid);
            if (VBOX_FAILURE(rc))
                return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error storing parent UUID in descriptor in '%s'"), pImage->pszFilename);
        }
    }
    else if (VBOX_FAILURE(rc))
        return rc;

    return VINF_SUCCESS;
}

static int vmdkWriteDescriptor(PVMDKIMAGE pImage)
{
    int rc = VINF_SUCCESS;
    uint64_t cbLimit;
    uint64_t uOffset;
    RTFILE DescFile;

    if (pImage->pDescData)
    {
        /* Separate descriptor file. */
        uOffset = 0;
        cbLimit = 0;
        DescFile = pImage->File;
    }
    else
    {
        /* Embedded descriptor file. */
        uOffset = VMDK_SECTOR2BYTE(pImage->pExtents[0].uDescriptorSector);
        cbLimit = VMDK_SECTOR2BYTE(pImage->pExtents[0].cDescriptorSectors);
        cbLimit += uOffset;
        DescFile = pImage->pExtents[0].File;
    }
    for (unsigned i = 0; i < pImage->Descriptor.cLines; i++)
    {
        const char *psz = pImage->Descriptor.aLines[i];
        size_t cb = strlen(psz);

        if (cbLimit && uOffset + cb + 1 > cbLimit)
            return vmdkError(pImage, VERR_BUFFER_OVERFLOW, RT_SRC_POS, N_("VMDK: descriptor too long in '%s'"), pImage->pszFilename);
        rc = RTFileWriteAt(DescFile, uOffset, psz, cb, NULL);
        if (VBOX_FAILURE(rc))
            return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error writing descriptor in '%s'"), pImage->pszFilename);
        uOffset += cb;
        rc = RTFileWriteAt(DescFile, uOffset, "\n", 1, NULL);
        if (VBOX_FAILURE(rc))
            return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error writing descriptor in '%s'"), pImage->pszFilename);
        uOffset++;
    }
    if (cbLimit)
    {
        /* Inefficient, but simple. */
        while (uOffset < cbLimit)
        {
            rc = RTFileWriteAt(DescFile, uOffset, "", 1, NULL);
            if (VBOX_FAILURE(rc))
                return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error writing descriptor in '%s'"), pImage->pszFilename);
            uOffset++;
        }
    }
    else
    {
        rc = RTFileSetSize(DescFile, uOffset);
        if (VBOX_FAILURE(rc))
            return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error truncating descriptor in '%s'"), pImage->pszFilename);
    }
    pImage->Descriptor.fDirty = false;
    return rc;
}

static int vmdkReadMetaSparseExtent(PVMDKEXTENT pExtent)
{
    SparseExtentHeader Header;
    uint64_t cbExtentSize, cSectorsPerGDE;

    int rc = RTFileReadAt(pExtent->File, 0, &Header, sizeof(Header), NULL);
    AssertRC(rc);
    if (VBOX_FAILURE(rc))
    {
        rc = vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: error reading extent header in '%s'"), pExtent->pszFullname);
        goto out;
    }
    if (    RT_LE2H_U32(Header.magicNumber) != VMDK_SPARSE_MAGICNUMBER
        ||  RT_LE2H_U32(Header.version) != 1)
    {
        rc = vmdkError(pExtent->pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: incorrect magic/version in extent header in '%s'"), pExtent->pszFullname);
        goto out;
    }
    /* The image must be a multiple of a sector in size. If not, it means the
     * image is at least truncated, or even seriously garbled. */
    rc = RTFileGetSize(pExtent->File, &cbExtentSize);
    if (VBOX_FAILURE(rc))
    {
        rc = vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: error getting size in '%s'"), pExtent->pszFullname);
        goto out;
    }
    if (    (RT_LE2H_U32(Header.flags) & 1)
        &&  (   Header.singleEndLineChar != '\n'
             || Header.nonEndLineChar != ' '
             || Header.doubleEndLineChar1 != '\r'
             || Header.doubleEndLineChar2 != '\n') )
    {
        rc = vmdkError(pExtent->pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: corrupted by CR/LF translation in '%s'"), pExtent->pszFullname);
        goto out;
    }
    pExtent->enmType = VMDKETYPE_HOSTED_SPARSE;
    pExtent->cSectors = RT_LE2H_U64(Header.capacity);
    pExtent->cSectorsPerGrain = RT_LE2H_U64(Header.grainSize);
    /* The spec says that this must be a power of two and greater than 8,
     * but probably they meant not less than 8. */
    if (    (pExtent->cSectorsPerGrain & (pExtent->cSectorsPerGrain - 1))
        ||  pExtent->cSectorsPerGrain < 8)
    {
        rc = vmdkError(pExtent->pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: invalid extent grain size %u in '%s'"), pExtent->cSectorsPerGrain, pExtent->pszFullname);
        goto out;
    }
    pExtent->uDescriptorSector = RT_LE2H_U64(Header.descriptorOffset);
    pExtent->cDescriptorSectors = RT_LE2H_U64(Header.descriptorSize);
    if (pExtent->uDescriptorSector && !pExtent->cDescriptorSectors)
    {
        rc = vmdkError(pExtent->pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: inconsistent embedded descriptor config in '%s'"), pExtent->pszFullname);
        goto out;
    }
    pExtent->cGTEntries = RT_LE2H_U32(Header.numGTEsPerGT);
    /* This code requires that a grain table must hold a power of two multiple
     * of the number of entries per GT cache entry. */
    if (    (pExtent->cGTEntries & (pExtent->cGTEntries - 1))
        ||  pExtent->cGTEntries < VMDK_GT_CACHELINE_SIZE)
    {
        rc = vmdkError(pExtent->pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: grain table cache size problem in '%s'"), pExtent->pszFullname);
        goto out;
    }
    if (RT_LE2H_U32(Header.flags) & 2)
    {
        pExtent->uSectorRGD = RT_LE2H_U64(Header.rgdOffset);
        pExtent->uSectorGD = RT_LE2H_U64(Header.gdOffset);
    }
    else
    {
        /** @todo this is just guesswork, the spec doesn't document this
         * properly and I don't have a vmdk without RGD. */
        pExtent->uSectorGD = RT_LE2H_U64(Header.rgdOffset);
        pExtent->uSectorRGD = 0;
    }
    pExtent->cOverheadSectors = RT_LE2H_U64(Header.overHead);
    pExtent->fUncleanShutdown = !!Header.uncleanShutdown;
    cSectorsPerGDE = pExtent->cGTEntries * pExtent->cSectorsPerGrain;
    if (!cSectorsPerGDE || cSectorsPerGDE > UINT32_MAX)
    {
        rc = vmdkError(pExtent->pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: incorrect grain directory size in '%s'"), pExtent->pszFullname);
        goto out;
    }
    pExtent->cSectorsPerGDE = cSectorsPerGDE;
    pExtent->cGDEntries = (pExtent->cSectors + cSectorsPerGDE - 1) / cSectorsPerGDE;

    rc = vmdkReadGrainDirectory(pExtent);

out:
    if (VBOX_FAILURE(rc))
        vmdkFreeExtentData(pExtent, false);

    return rc;
}

static int vmdkWriteMetaSparseExtent(PVMDKEXTENT pExtent)
{
    SparseExtentHeader Header;

    memset(&Header, '\0', sizeof(Header));
    Header.magicNumber = RT_H2LE_U32(VMDK_SPARSE_MAGICNUMBER);
    Header.version = RT_H2LE_U32(1);
    Header.flags = RT_H2LE_U32(1 | ((pExtent->pRGD) ? 2 : 0));
    Header.capacity = RT_H2LE_U64(pExtent->cSectors);
    Header.grainSize = RT_H2LE_U64(pExtent->cSectorsPerGrain);
    Header.descriptorOffset = RT_H2LE_U64(pExtent->uDescriptorSector);
    Header.descriptorSize = RT_H2LE_U64(pExtent->cDescriptorSectors);
    Header.numGTEsPerGT = RT_H2LE_U32(pExtent->cGTEntries);
    if (pExtent->pRGD)
    {
        Header.rgdOffset = RT_H2LE_U64(pExtent->uSectorRGD);
        Header.gdOffset = RT_H2LE_U64(pExtent->uSectorGD);
    }
    else
    {
        /** @todo this is just guesswork, the spec doesn't document this
         * properly and I don't have a vmdk without RGD. */
        Header.rgdOffset = RT_H2LE_U64(pExtent->uSectorGD);
    }
    Header.overHead = RT_H2LE_U64(pExtent->cOverheadSectors);
    Header.uncleanShutdown = pExtent->fUncleanShutdown;
    Header.singleEndLineChar = '\n';
    Header.nonEndLineChar = ' ';
    Header.doubleEndLineChar1 = '\r';
    Header.doubleEndLineChar2 = '\n';

    int rc = RTFileWriteAt(pExtent->File, 0, &Header, sizeof(Header), NULL);
    AssertRC(rc);
    if (VBOX_FAILURE(rc))
        rc = vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: error writing extent header in '%s'"), pExtent->pszFullname);
    return rc;
}

#ifdef VBOX_WITH_VMDK_ESX
static int vmdkReadMetaESXSparseExtent(PVMDKEXTENT pExtent)
{
    COWDisk_Header Header;
    uint64_t cSectorsPerGDE;

    int rc = RTFileReadAt(pExtent->File, 0, &Header, sizeof(Header), NULL);
    AssertRC(rc);
    if (VBOX_FAILURE(rc))
        goto out;
    if (    RT_LE2H_U32(Header.magicNumber) != VMDK_ESX_SPARSE_MAGICNUMBER
        ||  RT_LE2H_U32(Header.version) != 1
        ||  RT_LE2H_U32(Header.flags) != 3)
    {
        rc = VERR_VDI_INVALID_HEADER;
        goto out;
    }
    pExtent->enmType = VMDKETYPE_ESX_SPARSE;
    pExtent->cSectors = RT_LE2H_U32(Header.numSectors);
    pExtent->cSectorsPerGrain = RT_LE2H_U32(Header.grainSize);
    /* The spec says that this must be between 1 sector and 1MB. This code
     * assumes it's a power of two, so check that requirement, too. */
    if (    (pExtent->cSectorsPerGrain & (pExtent->cSectorsPerGrain - 1))
        ||  pExtent->cSectorsPerGrain == 0
        ||  pExtent->cSectorsPerGrain > 2048)
    {
        rc = VERR_VDI_INVALID_HEADER;
        goto out;
    }
    pExtent->uDescriptorSector = 0;
    pExtent->cDescriptorSectors = 0;
    pExtent->uSectorGD = RT_LE2H_U32(Header.gdOffset);
    pExtent->uSectorRGD = 0;
    pExtent->cOverheadSectors = 0;
    pExtent->cGTEntries = 4096;
    cSectorsPerGDE = pExtent->cGTEntries * pExtent->cSectorsPerGrain;
    if (!cSectorsPerGDE || cSectorsPerGDE > UINT32_MAX)
    {
        rc = VERR_VDI_INVALID_HEADER;
        goto out;
    }
    pExtent->cSectorsPerGDE = cSectorsPerGDE;
    pExtent->cGDEntries = (pExtent->cSectors + cSectorsPerGDE - 1) / cSectorsPerGDE;
    if (pExtent->cGDEntries != RT_LE2H_U32(Header.numGDEntries))
    {
        /* Inconsistency detected. Computed number of GD entries doesn't match
         * stored value. Better be safe than sorry. */
        rc = VERR_VDI_INVALID_HEADER;
        goto out;
    }
    pExtent->uFreeSector = RT_LE2H_U32(Header.freeSector);
    pExtent->fUncleanShutdown = !!Header.uncleanShutdown;

    rc = vmdkReadGrainDirectory(pExtent);

out:
    if (VBOX_FAILURE(rc))
        vmdkFreeExtentData(pExtent, false);

    return rc;
}
#endif /* VBOX_WITH_VMDK_ESX */

static void vmdkFreeExtentData(PVMDKEXTENT pExtent, bool fDelete)
{
    vmdkFreeGrainDirectory(pExtent);
    if (pExtent->pDescData)
    {
        RTMemFree(pExtent->pDescData);
        pExtent->pDescData = NULL;
    }
    if (pExtent->File != NIL_RTFILE)
    {
        RTFileClose(pExtent->File);
        pExtent->File = NIL_RTFILE;
        if (    fDelete
            &&  strcmp(pExtent->pszFullname, pExtent->pszBasename) != 0
            && pExtent->pszFullname)
            RTFileDelete(pExtent->pszFullname);
    }
    if (pExtent->pszBasename)
    {
        RTMemTmpFree((void *)pExtent->pszBasename);
        pExtent->pszBasename = NULL;
    }
    if (pExtent->pszFullname)
    {
        RTStrFree((char *)(void *)pExtent->pszFullname);
        pExtent->pszFullname = NULL;
    }
}

static int vmdkAllocateGrainTableCache(PVMDKIMAGE pImage)
{
    PVMDKEXTENT pExtent;

    /* Allocate grain table cache if any sparse extent is present. */
    for (unsigned i = 0; i < pImage->cExtents; i++)
    {
        pExtent = &pImage->pExtents[i];
        if (    pExtent->enmType == VMDKETYPE_HOSTED_SPARSE
#ifdef VBOX_WITH_VMDK_ESX
            ||  pExtent->enmType == VMDKETYPE_ESX_SPARSE
#endif /* VBOX_WITH_VMDK_ESX */
           )
        {
            /* Allocate grain table cache. */
            pImage->pGTCache = (PVMDKGTCACHE)RTMemAllocZ(sizeof(VMDKGTCACHE));
            if (!pImage->pGTCache)
                return VERR_NO_MEMORY;
            for (unsigned i = 0; i < VMDK_GT_CACHE_SIZE; i++)
            {
                PVMDKGTCACHEENTRY pGCE = &pImage->pGTCache->aGTCache[i];
                pGCE->uExtent = UINT32_MAX;
            }
            pImage->pGTCache->cEntries = VMDK_GT_CACHE_SIZE;
            break;
        }
    }

    return VINF_SUCCESS;
}

static int vmdkCreateExtents(PVMDKIMAGE pImage, unsigned cExtents)
{
    int rc = VINF_SUCCESS;
    PVMDKEXTENT pExtents = (PVMDKEXTENT)RTMemAllocZ(cExtents * sizeof(VMDKEXTENT));
    if (pImage)
    {
        for (unsigned i = 0; i < cExtents; i++)
        {
            pExtents[i].File = NIL_RTFILE;
            pExtents[i].pszBasename = NULL;
            pExtents[i].pszFullname = NULL;
            pExtents[i].pGD = NULL;
            pExtents[i].pRGD = NULL;
            pExtents[i].pDescData = NULL;
            pExtents[i].uExtent = i;
            pExtents[i].pImage = pImage;
        }
        pImage->pExtents = pExtents;
        pImage->cExtents = cExtents;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

static int vmdkOpenImage(PVMDKIMAGE pImage, const char *pszFilename, unsigned uOpenFlags)
{
    int rc = VINF_SUCCESS;
    uint32_t u32Magic;
    RTFILE File;
    PVMDKEXTENT pExtent;

    pImage->uOpenFlags = uOpenFlags;

    /** @todo check whether the same file is used somewhere else. don't open any file twice, leads to locking problems and can cause trouble with file caching. */

    /*
     * Open the image.
     */
    rc = RTFileOpen(&File, pszFilename, uOpenFlags & VD_OPEN_FLAGS_READONLY
                    ? RTFILE_O_READ      | RTFILE_O_OPEN | RTFILE_O_DENY_NONE
                    : RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (VBOX_FAILURE(rc))
    {
        /* Do NOT signal an appropriate error here, as the VD layer has the
         * choice of retrying the open if it failed. */
        goto out;
    }
    pImage->File = File;
    rc = RTFileReadAt(File, 0, &u32Magic, sizeof(u32Magic), NULL);
    AssertRC(rc);
    if (VBOX_FAILURE(rc))
    {
        rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error reading the magic number in '%s'"), pszFilename);
        goto out;
    }

    /* Handle the file according to its magic number. */
    if (RT_LE2H_U32(u32Magic) == VMDK_SPARSE_MAGICNUMBER)
    {
        /* It's a hosted sparse single-extent image. */
        rc = vmdkCreateExtents(pImage, 1);
        if (VBOX_FAILURE(rc))
            goto out;
        /* The opened file is passed to the extent. No separate descriptor
         * file, so no need to keep anything open for the image. */
        pExtent = &pImage->pExtents[0];
        pExtent->File = File;
        pImage->File = NIL_RTFILE;
        rc = vmdkReadMetaSparseExtent(pExtent);
        if (VBOX_FAILURE(rc))
            goto out;
        /* As we're dealing with a monolithic sparse image here, there must
         * be a descriptor embedded in the image file. */
        if (!pExtent->uDescriptorSector || !pExtent->cDescriptorSectors)
        {
            rc = vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: monolithic image without descriptor in '%s'"), pszFilename);
            goto out;
        }
        /* Read the descriptor from the extent. */
        pExtent->pDescData = (char *)RTMemAllocZ(VMDK_SECTOR2BYTE(pExtent->cDescriptorSectors));
        if (!pExtent->pDescData)
        {
            rc = VERR_NO_MEMORY;
            goto out;
        }
        rc = RTFileReadAt(pExtent->File,
                          VMDK_SECTOR2BYTE(pExtent->uDescriptorSector),
                          pExtent->pDescData,
                          VMDK_SECTOR2BYTE(pExtent->cDescriptorSectors), NULL);
        AssertRC(rc);
        if (VBOX_FAILURE(rc))
        {
            rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: read error for descriptor in '%s'"), pExtent->pszFullname);
            goto out;
        }

        rc = vmdkParseDescriptor(pImage, pExtent->pDescData,
                                 VMDK_SECTOR2BYTE(pExtent->cDescriptorSectors));
        if (VBOX_FAILURE(rc))
            goto out;

        /* Mark the extent as unclean if opened in read-write mode. */
        if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY))
        {
            pExtent->fUncleanShutdown = true;
            pExtent->fMetaDirty = true;
        }
    }
    else
    {
        pImage->cbDescAlloc = VMDK_SECTOR2BYTE(20);
        pImage->pDescData = (char *)RTMemAllocZ(pImage->cbDescAlloc);
        if (!pImage->pDescData)
        {
            rc = VERR_NO_MEMORY;
            goto out;
        }

        /*size_t*/unsigned cbRead;
        rc = RTFileReadAt(pImage->File, 0, pImage->pDescData,
                          pImage->cbDescAlloc, &cbRead);
        if (VBOX_FAILURE(rc))
        {
            rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: read error for descriptor in '%s'"), pszFilename);
            goto out;
        }
        if (cbRead == pImage->cbDescAlloc)
        {
            /* Likely the read is truncated. Better fail a bit too early
             * (normally the descriptor is much smaller than our buffer). */
            rc = vmdkError(pImage, VERR_VDI_INVALID_HEADER, RT_SRC_POS, N_("VMDK: cannot read descriptor in '%s'"), pszFilename);
            goto out;
        }

        rc = vmdkParseDescriptor(pImage, pImage->pDescData, pImage->cbDescAlloc);
        if (VBOX_FAILURE(rc))
            goto out;

        for (unsigned i = 0; i < pImage->cExtents; i++)
        {
            PVMDKEXTENT pExtent = &pImage->pExtents[i];

            if (pExtent->pszBasename)
            {
                /* Hack to figure out whether the specified name in the
                 * extent descriptor is absolute. Doesn't always work, but
                 * should be good enough for now. */
                char *pszFullname;
                /** @todo implement proper path absolute check. */
                if (pExtent->pszBasename[0] == RTPATH_SLASH)
                {
                    pszFullname = RTStrDup(pExtent->pszBasename);
                    if (!pszFullname)
                    {
                        rc = VERR_NO_MEMORY;
                        goto out;
                    }
                }
                else
                {
                    size_t cbDirname;
                    char *pszDirname = RTStrDup(pImage->pszFilename);
                    if (!pszDirname)
                    {
                        rc = VERR_NO_MEMORY;
                        goto out;
                    }
                    RTPathStripFilename(pszDirname);
                    cbDirname = strlen(pszDirname);
                    rc = RTStrAPrintf(&pszFullname, "%s%c%s", pszDirname,
                                      RTPATH_SLASH, pExtent->pszBasename);
                    RTStrFree(pszDirname);
                    if (VBOX_FAILURE(rc))
                        goto out;
                }
                pExtent->pszFullname = pszFullname;
            }
            else
                pExtent->pszFullname = NULL;

            switch (pExtent->enmType)
            {
                case VMDKETYPE_HOSTED_SPARSE:
                    rc = RTFileOpen(&pExtent->File, pExtent->pszFullname,
                                    uOpenFlags & VD_OPEN_FLAGS_READONLY
                                      ? RTFILE_O_READ      | RTFILE_O_OPEN | RTFILE_O_DENY_NONE
                                      : RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
                    if (VBOX_FAILURE(rc))
                    {
                        /* Do NOT signal an appropriate error here, as the VD
                         * layer has the choice of retrying the open if it
                         * failed. */
                        goto out;
                    }
                    rc = vmdkReadMetaSparseExtent(pExtent);
                    if (VBOX_FAILURE(rc))
                        goto out;

                    /* Mark the extent as unclean if opened in read-write mode. */
                    if (!(uOpenFlags & VD_OPEN_FLAGS_READONLY))
                    {
                        pExtent->fUncleanShutdown = true;
                        pExtent->fMetaDirty = true;
                    }
                    break;
                case VMDKETYPE_FLAT:
                    rc = RTFileOpen(&pExtent->File, pExtent->pszFullname,
                                    uOpenFlags & VD_OPEN_FLAGS_READONLY
                                      ? RTFILE_O_READ      | RTFILE_O_OPEN | RTFILE_O_DENY_NONE
                                      : RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
                    if (VBOX_FAILURE(rc))
                    {
                        /* Do NOT signal an appropriate error here, as the VD
                         * layer has the choice of retrying the open if it
                         * failed. */
                        goto out;
                    }
                    break;
                case VMDKETYPE_ZERO:
                    /* Nothing to do. */
                    break;
                default:
                    AssertMsgFailed(("unknown vmdk extent type %d\n", pExtent->enmType));
            }
        }
    }

    /* Make sure this is not reached accidentally with an error status. */
    AssertRC(rc);

    /* Update the image metadata now in case has changed. */
    rc = vmdkFlushImage(pImage);
    if (VBOX_FAILURE(rc))
        goto out;

    /* Figure out a few per-image constants from the extents. */
    pImage->cbSize = 0;
    for (unsigned i = 0; i < pImage->cExtents; i++)
    {
        pExtent = &pImage->pExtents[i];
        if (    pExtent->enmType == VMDKETYPE_HOSTED_SPARSE
#ifdef VBOX_WITH_VMDK_ESX
            ||  pExtent->enmType == VMDKETYPE_ESX_SPARSE
#endif /* VBOX_WITH_VMDK_ESX */
           )
        {
            /* Here used to be a check whether the nominal size of an extent
             * is a multiple of the grain size. The spec says that this is
             * always the case, but unfortunately some files out there in the
             * wild violate the spec (e.g. ReactOS 0.3.1). */
        }
        pImage->cbSize += VMDK_SECTOR2BYTE(pExtent->cNominalSectors);
    }

    pImage->enmImageType = VD_IMAGE_TYPE_NORMAL;
    for (unsigned i = 0; i < pImage->cExtents; i++)
    {
        pExtent = &pImage->pExtents[i];
        if (    pImage->pExtents[i].enmType == VMDKETYPE_FLAT
            ||  pImage->pExtents[i].enmType == VMDKETYPE_ZERO)
        {
            pImage->enmImageType = VD_IMAGE_TYPE_FIXED;
            break;
        }
    }

    rc = vmdkAllocateGrainTableCache(pImage);
    if (VBOX_FAILURE(rc))
        goto out;

out:
    if (VBOX_FAILURE(rc))
        vmdkFreeImage(pImage, false);
    return rc;
}

static int vmdkCreateImage(PVMDKIMAGE pImage, const char *pszFilename, VDIMAGETYPE enmType, uint64_t cbSize, unsigned uImageFlags, const char *pszComment, uint32_t cCylinders, uint32_t cHeads, uint32_t cSectors)
{
    int rc;
    uint64_t cSectorsPerGDE, cSectorsPerGD;
    PVMDKEXTENT pExtent;

    pImage->uImageFlags = uImageFlags;
    rc = vmdkCreateDescriptor(pImage, pImage->pDescData, pImage->cbDescAlloc, &pImage->Descriptor);
    if (VBOX_FAILURE(rc))
    {
        rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not create new descriptor in '%s'"), pszFilename);
        goto out;
    }

    if (    enmType == VD_IMAGE_TYPE_FIXED
        ||  (uImageFlags & VD_VMDK_IMAGE_FLAGS_SPLIT_2G)
        ||  (uImageFlags & VD_VMDK_IMAGE_FLAGS_RAWDISK))
    {
        /* Fixed images and split images in general have a separate descriptor
         * file. This is the more complicated case, as it requires setting up
         * potentially more than one extent, including filename generation. */

        if (    enmType == VD_IMAGE_TYPE_FIXED
            &&  (uImageFlags & VD_VMDK_IMAGE_FLAGS_RAWDISK))
        {
            PVBOXHDDRAW pRaw = (PVBOXHDDRAW)(void *)pszComment;
            /* As the comment is misused, zap it so that no garbage comment
             * is set below. */
            pszComment = NULL;
            if (pRaw->fRawDisk)
            {
                /* Full raw disk access. This requires setting up a descriptor
                 * file and open the (flat) raw disk. */
                rc = vmdkCreateExtents(pImage, 1);
                if (VBOX_FAILURE(rc))
                {
                    rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not create new extent list in '%s'"), pszFilename);
                    goto out;
                }
                pExtent = &pImage->pExtents[0];
                rc = RTFileOpen(&pImage->File, pszFilename,
                                RTFILE_O_READWRITE | RTFILE_O_CREATE | RTFILE_O_DENY_WRITE);
                if (VBOX_FAILURE(rc))
                {
                    rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not create new file '%s'"), pszFilename);
                    goto out;
                }

                /* Set up basename for extent description. Cannot use StrDup. */
                size_t cbBasename = strlen(pRaw->pszRawDisk) + 1;
                char *pszBasename = (char *)RTMemTmpAlloc(cbBasename);
                if (!pszBasename)
                {
                    rc = VERR_NO_MEMORY;
                    goto out;
                }
                memcpy(pszBasename, pRaw->pszRawDisk, cbBasename);
                pExtent->pszBasename = pszBasename;
                /* For raw disks the full name is identical to the base name. */
                pExtent->pszFullname = RTStrDup(pszBasename);
                if (!pExtent->pszFullname)
                {
                    rc = VERR_NO_MEMORY;
                    goto out;
                }
                pExtent->enmType = VMDKETYPE_FLAT;
                pExtent->cNominalSectors = VMDK_BYTE2SECTOR(cbSize);
                pExtent->uSectorOffset = 0;
                pExtent->enmAccess = VMDKACCESS_READWRITE;
                pExtent->fMetaDirty = false;

                pImage->enmImageType = enmType;
                rc = vmdkDescBaseSetStr(pImage, &pImage->Descriptor, "createType", "fullDevice");
                if (VBOX_FAILURE(rc))
                {
                    rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not set the image type in '%s'"), pszFilename);
                    goto out;
                }

                /* Open flat image, the raw disk. */
                rc = RTFileOpen(&pExtent->File, pExtent->pszFullname,
                                RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
                if (VBOX_FAILURE(rc))
                {
                    rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not open raw disk file '%s'"), pExtent->pszFullname);
                    goto out;
                }
            }
            else
            {
                /* Raw partition access. This requires setting up a descriptor
                 * file, write the partition information to a flat extent and
                 * open all the (flat) raw disk partitions. */

                /* First pass over the partitions to determine how many
                 * extents we need. One partition can require up to 4 extents.
                 * One to skip over unpartitioned space, one for the
                 * partitioning data, one to skip over unpartitioned space
                 * and one for the partition data. */
                unsigned cExtents = 0;
                uint64_t uStart = 0;
                for (unsigned i = 0; i < pRaw->cPartitions; i++)
                {
                    PVBOXHDDRAWPART pPart = &pRaw->pPartitions[i];
                    if (pPart->cbPartitionData)
                    {
                        if (uStart > pPart->uPartitionDataStart)
                        {
                            rc = vmdkError(pImage, VERR_INVALID_PARAMETER, RT_SRC_POS, N_("VMDK: cannot go backwards for partitioning information in '%s'"), pszFilename);
                            goto out;
                        } else if (uStart != pPart->uPartitionDataStart)
                            cExtents++;
                        uStart = pPart->uPartitionDataStart + pPart->cbPartitionData;
                        cExtents++;
                    }
                    if (pPart->cbPartition)
                    {
                        if (uStart > pPart->uPartitionStart)
                        {
                            rc = vmdkError(pImage, VERR_INVALID_PARAMETER, RT_SRC_POS, N_("VMDK: cannot go backwards for partition data in '%s'"), pszFilename);
                            goto out;
                        } else if (uStart != pPart->uPartitionStart)
                            cExtents++;
                        uStart = pPart->uPartitionStart + pPart->cbPartition;
                        cExtents++;
                    }
                }
                /* Another extent for filling up the rest of the image. */
                if (uStart != cbSize)
                    cExtents++;

                rc = vmdkCreateExtents(pImage, cExtents);
                if (VBOX_FAILURE(rc))
                {
                    rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not create new extent list in '%s'"), pszFilename);
                    goto out;
                }

                rc = RTFileOpen(&pImage->File, pszFilename,
                                RTFILE_O_READWRITE | RTFILE_O_CREATE | RTFILE_O_DENY_WRITE);
                if (VBOX_FAILURE(rc))
                {
                    rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not create new file '%s'"), pszFilename);
                    goto out;
                }

                /* Create base filename for the partition table extent. */
                /** @todo remove fixed buffer. */
                char pszPartition[1024];
                const char *pszBase = RTPathFilename(pszFilename);
                const char *pszExt = RTPathExt(pszBase);
                if (pszExt == NULL)
                {
                    rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: invalid filename '%s'"), pszFilename);
                    goto out;
                }
                memcpy(pszPartition, pszBase, pszExt - pszBase);
                memcpy(pszPartition + (pszExt - pszBase), "-pt", 3);
                memcpy(pszPartition + (pszExt - pszBase) + 3, pszExt, strlen(pszExt) + 1);

                /* Second pass over the partitions, now define all extents. */
                uint64_t uPartOffset = 0;
                cExtents = 0;
                uStart = 0;
                for (unsigned i = 0; i < pRaw->cPartitions; i++)
                {
                    PVBOXHDDRAWPART pPart = &pRaw->pPartitions[i];
                    if (pPart->cbPartitionData)
                    {
                        if (uStart != pPart->uPartitionDataStart)
                        {
                            pExtent = &pImage->pExtents[cExtents++];
                            pExtent->pszBasename = NULL;
                            pExtent->pszFullname = NULL;
                            pExtent->enmType = VMDKETYPE_ZERO;
                            pExtent->cNominalSectors = VMDK_BYTE2SECTOR(pPart->uPartitionDataStart - uStart);
                            pExtent->uSectorOffset = 0;
                            pExtent->enmAccess = VMDKACCESS_READWRITE;
                            pExtent->fMetaDirty = false;
                        }
                        uStart = pPart->uPartitionDataStart + pPart->cbPartitionData;
                        pExtent = &pImage->pExtents[cExtents++];
                        /* Set up basename for extent description. Cannot use StrDup. */
                        size_t cbBasename = strlen(pszPartition) + 1;
                        char *pszBasename = (char *)RTMemTmpAlloc(cbBasename);
                        if (!pszBasename)
                        {
                            rc = VERR_NO_MEMORY;
                            goto out;
                        }
                        memcpy(pszBasename, pszPartition, cbBasename);
                        pExtent->pszBasename = pszBasename;

                        /* Set up full name for partition extent. */
                        size_t cbDirname;
                        char *pszDirname = RTStrDup(pImage->pszFilename);
                        if (!pszDirname)
                        {
                            rc = VERR_NO_MEMORY;
                            goto out;
                        }
                        RTPathStripFilename(pszDirname);
                        cbDirname = strlen(pszDirname);
                        char *pszFullname;
                        rc = RTStrAPrintf(&pszFullname, "%s%c%s", pszDirname,
                                          RTPATH_SLASH, pExtent->pszBasename);
                        RTStrFree(pszDirname);
                        if (VBOX_FAILURE(rc))
                            goto out;
                        pExtent->pszFullname = pszFullname;
                        pExtent->enmType = VMDKETYPE_FLAT;
                        pExtent->cNominalSectors = VMDK_BYTE2SECTOR(pPart->cbPartitionData);
                        pExtent->uSectorOffset = uPartOffset;
                        pExtent->enmAccess = VMDKACCESS_READWRITE;
                        pExtent->fMetaDirty = false;

                        rc = RTFileOpen(&pExtent->File, pExtent->pszFullname,
                                        RTFILE_O_READWRITE | RTFILE_O_OPEN_CREATE | RTFILE_O_DENY_WRITE);
                        if (VBOX_FAILURE(rc))
                        {
                            rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not create new partition data file '%s'"), pExtent->pszFullname);
                            goto out;
                        }
                        rc = RTFileWriteAt(pExtent->File, VMDK_SECTOR2BYTE(uPartOffset), pPart->pvPartitionData, pPart->cbPartitionData, NULL);
                        if (VBOX_FAILURE(rc))
                        {
                            rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not write partition data to '%s'"), pExtent->pszFullname);
                            goto out;
                        }
                        uPartOffset += VMDK_BYTE2SECTOR(pPart->cbPartitionData);
                    }
                    if (pPart->cbPartition)
                    {
                        if (uStart != pPart->uPartitionStart)
                        {
                            pExtent = &pImage->pExtents[cExtents++];
                            pExtent->pszBasename = NULL;
                            pExtent->pszFullname = NULL;
                            pExtent->enmType = VMDKETYPE_ZERO;
                            pExtent->cNominalSectors = VMDK_BYTE2SECTOR(pPart->uPartitionStart - uStart);
                            pExtent->uSectorOffset = 0;
                            pExtent->enmAccess = VMDKACCESS_READWRITE;
                            pExtent->fMetaDirty = false;
                        }
                        uStart = pPart->uPartitionStart + pPart->cbPartition;
                        pExtent = &pImage->pExtents[cExtents++];
                        if (pPart->pszRawDevice)
                        {
                            /* Set up basename for extent description. Cannot use StrDup. */
                            size_t cbBasename = strlen(pPart->pszRawDevice) + 1;
                            char *pszBasename = (char *)RTMemTmpAlloc(cbBasename);
                            if (!pszBasename)
                            {
                                rc = VERR_NO_MEMORY;
                                goto out;
                            }
                            memcpy(pszBasename, pPart->pszRawDevice, cbBasename);
                            pExtent->pszBasename = pszBasename;
                            /* For raw disks the full name is identical to the base name. */
                            pExtent->pszFullname = RTStrDup(pszBasename);
                            if (!pExtent->pszFullname)
                            {
                                rc = VERR_NO_MEMORY;
                                goto out;
                            }
                            pExtent->enmType = VMDKETYPE_FLAT;
                            pExtent->cNominalSectors = VMDK_BYTE2SECTOR(pPart->cbPartition);
                            pExtent->uSectorOffset = VMDK_BYTE2SECTOR(pPart->uPartitionStartOffset);
                            pExtent->enmAccess = VMDKACCESS_READWRITE;
                            pExtent->fMetaDirty = false;

                            rc = RTFileOpen(&pExtent->File, pExtent->pszFullname,
                                            RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
                            if (VBOX_FAILURE(rc))
                            {
                                rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not open raw partition file '%s'"), pExtent->pszFullname);
                                goto out;
                            }
                        }
                        else
                        {
                            pExtent->pszBasename = NULL;
                            pExtent->pszFullname = NULL;
                            pExtent->enmType = VMDKETYPE_ZERO;
                            pExtent->cNominalSectors = VMDK_BYTE2SECTOR(pPart->cbPartition);
                            pExtent->uSectorOffset = 0;
                            pExtent->enmAccess = VMDKACCESS_READWRITE;
                            pExtent->fMetaDirty = false;
                        }
                    }
                }
                /* Another extent for filling up the rest of the image. */
                if (uStart != cbSize)
                {
                    pExtent = &pImage->pExtents[cExtents++];
                    pExtent->pszBasename = NULL;
                    pExtent->pszFullname = NULL;
                    pExtent->enmType = VMDKETYPE_ZERO;
                    pExtent->cNominalSectors = VMDK_BYTE2SECTOR(cbSize - uStart);
                    pExtent->uSectorOffset = 0;
                    pExtent->enmAccess = VMDKACCESS_READWRITE;
                    pExtent->fMetaDirty = false;
                }

                pImage->enmImageType = enmType;
                rc = vmdkDescBaseSetStr(pImage, &pImage->Descriptor, "createType", "partitionedDevice");
                if (VBOX_FAILURE(rc))
                {
                    rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not set the image type in '%s'"), pszFilename);
                    goto out;
                }
            }
        }
        else
        {
            rc = VERR_NOT_IMPLEMENTED;
            goto out;
        }
    }
    else
    {
        /* Normal (growing) image which is not split into pieces. */
        rc = vmdkCreateExtents(pImage, 1);
        if (VBOX_FAILURE(rc))
        {
            rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not create new extent list in '%s'"), pszFilename);
            goto out;
        }
        pExtent = &pImage->pExtents[0];
        pImage->File = NIL_RTFILE;
        rc = RTFileOpen(&pExtent->File, pszFilename,
                        RTFILE_O_READWRITE | RTFILE_O_CREATE | RTFILE_O_DENY_WRITE);
        if (VBOX_FAILURE(rc))
        {
            rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not create new file '%s'"), pszFilename);
            goto out;
        }

        /* Set up basename for extent description. Cannot use StrDup, as it is
         * not guaranteed that the memory can be freed with RTMemTmpFree, which
         * must be used as in other code paths StrDup is not usable. */
        char *pszBasenameSubstr = RTPathFilename(pszFilename);
        Assert(pszBasenameSubstr);
        size_t cbBasenameSubstr = strlen(pszBasenameSubstr) + 1;
        char *pszBasename = (char *)RTMemTmpAlloc(cbBasenameSubstr);
        if (!pszBasename)
        {
            rc = VERR_NO_MEMORY;
            goto out;
        }
        memcpy(pszBasename, pszBasenameSubstr, cbBasenameSubstr);
        pExtent->pszBasename = pszBasename;
        pExtent->pszFullname = RTStrDup(pszFilename);
        if (!pExtent->pszFullname)
        {
            rc = VERR_NO_MEMORY;
            goto out;
        }
        pExtent->enmType = VMDKETYPE_HOSTED_SPARSE;
        pExtent->cSectors = VMDK_BYTE2SECTOR(RT_ALIGN_64(cbSize, 65536));
        pExtent->cSectorsPerGrain = VMDK_BYTE2SECTOR(65536);
        pExtent->uDescriptorSector = 1;
        pExtent->cDescriptorSectors = VMDK_BYTE2SECTOR(pImage->cbDescAlloc);
        pExtent->cGTEntries = 512;
        cSectorsPerGDE = pExtent->cGTEntries * pExtent->cSectorsPerGrain;
        pExtent->cSectorsPerGDE = cSectorsPerGDE;
        pExtent->cGDEntries = (pExtent->cSectors + cSectorsPerGDE - 1) / cSectorsPerGDE;
        cSectorsPerGD = (pExtent->cGDEntries + (512 / sizeof(uint32_t) - 1)) / (512 / sizeof(uint32_t));
        pExtent->enmAccess = VMDKACCESS_READWRITE;
        pExtent->fUncleanShutdown = true;
        pExtent->cNominalSectors = VMDK_BYTE2SECTOR(cbSize);
        pExtent->uSectorOffset = 0;
        pExtent->fMetaDirty = true;

        rc = vmdkCreateGrainDirectory(pExtent, pExtent->uDescriptorSector + pExtent->cDescriptorSectors, true);
        if (VBOX_FAILURE(rc))
        {
            rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not create new grain directory in '%s'"), pszFilename);
            goto out;
        }

        pImage->enmImageType = enmType;
        rc = vmdkDescBaseSetStr(pImage, &pImage->Descriptor, "createType", "monolithicSparse");
        if (VBOX_FAILURE(rc))
        {
            rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not set the image type in '%s'"), pszFilename);
            goto out;
        }

        /* The descriptor is part of the extent, move info to extent. */
        pExtent->pDescData = pImage->pDescData;
        pImage->pDescData = NULL;
    }

    pImage->cbSize = cbSize;
    if (pImage->cCylinders >= 1024 || pImage->cHeads != 16)
        pImage->enmTranslation = PDMBIOSTRANSLATION_LBA;
    else
        pImage->enmTranslation = PDMBIOSTRANSLATION_NONE;

    for (unsigned i = 0; i < pImage->cExtents; i++)
    {
        pExtent = &pImage->pExtents[i];

        rc = vmdkDescExtInsert(pImage, &pImage->Descriptor, pExtent->enmAccess,
                               pExtent->cNominalSectors, pExtent->enmType,
                               pExtent->pszBasename, pExtent->uSectorOffset);
        if (VBOX_FAILURE(rc))
        {
            rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: could not insert the extent list into descriptor in '%s'"), pszFilename);
            goto out;
        }
    }
    vmdkDescExtRemoveDummy(pImage, &pImage->Descriptor);

    rc = vmdkDescDDBSetU32(pImage, &pImage->Descriptor,
                           "ddb.geometry.cylinders", cCylinders);
    if (VBOX_FAILURE(rc))
        goto out;
    rc = vmdkDescDDBSetU32(pImage, &pImage->Descriptor,
                           "ddb.geometry.heads", cHeads);
    if (VBOX_FAILURE(rc))
        goto out;
    rc = vmdkDescDDBSetU32(pImage, &pImage->Descriptor,
                           "ddb.geometry.sectors", cSectors);
    if (VBOX_FAILURE(rc))
        goto out;

    pImage->cCylinders = cCylinders;
    pImage->cHeads = cHeads;
    pImage->cSectors = cSectors;

    rc = RTUuidCreate(&pImage->ImageUuid);
    if (VBOX_FAILURE(rc))
        goto out;
    rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                            "ddb.uuid.image", &pImage->ImageUuid);
    if (VBOX_FAILURE(rc))
    {
        rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error storing image UUID in new descriptor in '%s'"), pszFilename);
        goto out;
    }
    RTUuidClear(&pImage->ParentUuid);
    rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                            "ddb.uuid.parent", &pImage->ParentUuid);
    if (VBOX_FAILURE(rc))
    {
        rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error storing parent image UUID in new descriptor in '%s'"), pszFilename);
        goto out;
    }
    RTUuidClear(&pImage->ModificationUuid);
    rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                            "ddb.uuid.modification", &pImage->ModificationUuid);
    if (VBOX_FAILURE(rc))
    {
        rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error storing modification UUID in new descriptor in '%s'"), pszFilename);
        goto out;
    }

    rc = vmdkAllocateGrainTableCache(pImage);
    if (VBOX_FAILURE(rc))
        goto out;

    rc = vmdkSetImageComment(pImage, pszComment);
    if (VBOX_FAILURE(rc))
    {
        rc = vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: cannot set image comment in '%s'"), pszFilename);
        goto out;
    }

    rc = vmdkFlushImage(pImage);

out:
    if (VBOX_FAILURE(rc))
        vmdkFreeImage(pImage, rc != VERR_ALREADY_EXISTS);
    return rc;
}

static int vmdkSetImageComment(PVMDKIMAGE pImage, const char *pszComment)
{
    char *pszCommentEncoded;
    if (pszComment)
    {
        pszCommentEncoded = vmdkEncodeString(pszComment);
        if (!pszCommentEncoded)
            return VERR_NO_MEMORY;
    }
    else
        pszCommentEncoded = NULL;
    int rc = vmdkDescDDBSetStr(pImage, &pImage->Descriptor,
                          "ddb.comment", pszCommentEncoded);
    if (pszComment)
        RTStrFree(pszCommentEncoded);
    if (VBOX_FAILURE(rc))
        return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error storing image comment in descriptor in '%s'"), pImage->pszFilename);
    return VINF_SUCCESS;
}

static void vmdkFreeImage(PVMDKIMAGE pImage, bool fDelete)
{
    if (pImage->enmImageType)
    {
        if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
        {
            /* Mark all extents as clean. */
            for (unsigned i = 0; i < pImage->cExtents; i++)
            {
                if ((   pImage->pExtents[i].enmType == VMDKETYPE_HOSTED_SPARSE
#ifdef VBOX_WITH_VMDK_ESX
                     || pImage->pExtents[i].enmType == VMDKETYPE_ESX_SPARSE
#endif /* VBOX_WITH_VMDK_ESX */
                    )
                    &&  pImage->pExtents[i].fUncleanShutdown)
                {
                    pImage->pExtents[i].fUncleanShutdown = false;
                    pImage->pExtents[i].fMetaDirty = true;
                }
            }
        }
        (void)vmdkFlushImage(pImage);
    }
    if (pImage->pExtents != NULL)
    {
        for (unsigned i = 0 ; i < pImage->cExtents; i++)
            vmdkFreeExtentData(&pImage->pExtents[i], fDelete);
        RTMemFree(pImage->pExtents);
        pImage->pExtents = NULL;
    }
    if (pImage->File != NIL_RTFILE)
    {
        RTFileClose(pImage->File);
        pImage->File = NIL_RTFILE;
    }
    if (fDelete && pImage->pszFilename)
        RTFileDelete(pImage->pszFilename);
}

static int vmdkFlushImage(PVMDKIMAGE pImage)
{
    PVMDKEXTENT pExtent;
    int rc = VINF_SUCCESS;

    /* Update descriptor if changed. */
    if (pImage->Descriptor.fDirty)
    {
        rc = vmdkWriteDescriptor(pImage);
        if (VBOX_FAILURE(rc))
            goto out;
    }

    for (unsigned i = 0; i < pImage->cExtents; i++)
    {
        pExtent = &pImage->pExtents[i];
        if (pExtent->File != NIL_RTFILE && pExtent->fMetaDirty)
        {
            switch (pExtent->enmType)
            {
                case VMDKETYPE_HOSTED_SPARSE:
                    rc = vmdkWriteMetaSparseExtent(pExtent);
                    if (VBOX_FAILURE(rc))
                        goto out;
                    break;
#ifdef VBOX_WITH_VMDK_ESX
                case VMDKETYPE_ESX_SPARSE:
                    /** @todo update the header. */
                    break;
#endif /* VBOX_WITH_VMDK_ESX */
                case VMDKETYPE_FLAT:
                    /* Nothing to do. */
                    break;
                case VMDKETYPE_ZERO:
                default:
                    AssertMsgFailed(("extent with type %d marked as dirty\n",
                                     pExtent->enmType));
                    break;
            }
        }
        switch (pExtent->enmType)
        {
            case VMDKETYPE_HOSTED_SPARSE:
#ifdef VBOX_WITH_VMDK_ESX
            case VMDKETYPE_ESX_SPARSE:
#endif /* VBOX_WITH_VMDK_ESX */
            case VMDKETYPE_FLAT:
                /** @todo implement proper path absolute check. */
                if (pExtent->File != NIL_RTFILE && !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY) && !(pExtent->pszBasename[0] == RTPATH_SLASH))
                    rc = RTFileFlush(pExtent->File);
                break;
            case VMDKETYPE_ZERO:
                /* No need to do anything for this extent. */
                break;
            default:
                AssertMsgFailed(("unknown extent type %d\n", pExtent->enmType));
                break;
        }
    }

out:
    return rc;
}

static int vmdkFindExtent(PVMDKIMAGE pImage, uint64_t offSector, PVMDKEXTENT *ppExtent, uint64_t *puSectorInExtent)
{
    PVMDKEXTENT pExtent = NULL;
    int rc = VINF_SUCCESS;

    for (unsigned i = 0; i < pImage->cExtents; i++)
    {
        if (offSector < pImage->pExtents[i].cNominalSectors)
        {
            pExtent = &pImage->pExtents[i];
            *puSectorInExtent = offSector + pImage->pExtents[i].uSectorOffset;
            break;
        }
        offSector -= pImage->pExtents[i].cNominalSectors;
    }

    if (pExtent)
        *ppExtent = pExtent;
    else
        rc = VERR_IO_SECTOR_NOT_FOUND;

    return rc;
}

static uint32_t vmdkGTCacheHash(PVMDKGTCACHE pCache, uint64_t uSector, unsigned uExtent)
{
    /** @todo this hash function is quite simple, maybe use a better one which
     * scrambles the bits better. */
    return (uSector + uExtent) % pCache->cEntries;
}

static int vmdkGetSector(PVMDKGTCACHE pCache, PVMDKEXTENT pExtent,
                         uint64_t uSector, uint64_t *puExtentSector)
{
    uint64_t uGDIndex, uGTSector, uGTBlock;
    uint32_t uGTHash, uGTBlockIndex;
    PVMDKGTCACHEENTRY pGTCacheEntry;
    uint32_t aGTDataTmp[VMDK_GT_CACHELINE_SIZE];
    int rc;

    uGDIndex = uSector / pExtent->cSectorsPerGDE;
    if (uGDIndex >= pExtent->cGDEntries)
        return VERR_OUT_OF_RANGE;
    uGTSector = pExtent->pGD[uGDIndex];
    if (!uGTSector)
    {
        /* There is no grain table referenced by this grain directory
         * entry. So there is absolutely no data in this area. */
        *puExtentSector = 0;
        return VINF_SUCCESS;
    }

    uGTBlock = uSector / (pExtent->cSectorsPerGrain * VMDK_GT_CACHELINE_SIZE);
    uGTHash = vmdkGTCacheHash(pCache, uGTBlock, pExtent->uExtent);
    pGTCacheEntry = &pCache->aGTCache[uGTHash];
    if (    pGTCacheEntry->uExtent != pExtent->uExtent
        ||  pGTCacheEntry->uGTBlock != uGTBlock)
    {
        /* Cache miss, fetch data from disk. */
        rc = RTFileReadAt(pExtent->File,
                          VMDK_SECTOR2BYTE(uGTSector) + (uGTBlock % (pExtent->cGTEntries / VMDK_GT_CACHELINE_SIZE)) * sizeof(aGTDataTmp),
                          aGTDataTmp, sizeof(aGTDataTmp), NULL);
        if (VBOX_FAILURE(rc))
            return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: cannot read grain table entry in '%s'"), pExtent->pszFullname);
        pGTCacheEntry->uExtent = pExtent->uExtent;
        pGTCacheEntry->uGTBlock = uGTBlock;
        for (unsigned i = 0; i < VMDK_GT_CACHELINE_SIZE; i++)
            pGTCacheEntry->aGTData[i] = RT_LE2H_U32(aGTDataTmp[i]);
    }
    uGTBlockIndex = (uSector / pExtent->cSectorsPerGrain) % VMDK_GT_CACHELINE_SIZE;
    uint64_t uGrainSector = pGTCacheEntry->aGTData[uGTBlockIndex];
    if (uGrainSector)
        *puExtentSector = uGrainSector + uSector % pExtent->cSectorsPerGrain;
    else
        *puExtentSector = 0;
    return VINF_SUCCESS;
}

/**
 * Internal. Allocates a new grain table (if necessary), writes the grain
 * and updates the grain table. The cache is also updated by this operation.
 * This is separate from vmdkGetSector, because that should be as fast as
 * possible. Most code from vmdkGetSector also appears here.
 */
static int vmdkAllocGrain(PVMDKGTCACHE pCache, PVMDKEXTENT pExtent,
                          uint64_t uSector, const void *pvBuf, uint64_t cbWrite)
{
    uint64_t uGDIndex, uGTSector, uRGTSector, uGTBlock;
    uint64_t cbExtentSize;
    uint32_t uGTHash, uGTBlockIndex;
    PVMDKGTCACHEENTRY pGTCacheEntry;
    uint32_t aGTDataTmp[VMDK_GT_CACHELINE_SIZE];
    int rc;

    uGDIndex = uSector / pExtent->cSectorsPerGDE;
    if (uGDIndex >= pExtent->cGDEntries)
        return VERR_OUT_OF_RANGE;
    uGTSector = pExtent->pGD[uGDIndex];
    uRGTSector = pExtent->pRGD[uGDIndex];
    if (!uGTSector)
    {
        /* There is no grain table referenced by this grain directory
         * entry. So there is absolutely no data in this area. Allocate
         * a new grain table and put the reference to it in the GDs. */
        rc = RTFileGetSize(pExtent->File, &cbExtentSize);
        if (VBOX_FAILURE(rc))
            return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: error getting size in '%s'"), pExtent->pszFullname);
        Assert(!(cbExtentSize % 512));
        uGTSector = VMDK_BYTE2SECTOR(cbExtentSize);
        /* Normally the grain table is preallocated for hosted sparse extents
         * that support more than 32 bit sector numbers. So this shouldn't
         * ever happen on a valid extent. */
        if (uGTSector > UINT32_MAX)
            return VERR_VDI_INVALID_HEADER;
        /* Write grain table by writing the required number of grain table
         * cache chunks. Avoids dynamic memory allocation, but is a bit
         * slower. But as this is a pretty infrequently occurring case it
         * should be acceptable. */
        memset(aGTDataTmp, '\0', sizeof(aGTDataTmp));
        for (unsigned i = 0; i < pExtent->cGTEntries / VMDK_GT_CACHELINE_SIZE; i++)
        {
            rc = RTFileWriteAt(pExtent->File, VMDK_SECTOR2BYTE(uGTSector) + i * sizeof(aGTDataTmp), aGTDataTmp, sizeof(aGTDataTmp), NULL);
            if (VBOX_FAILURE(rc))
                return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: cannot write grain table allocation in '%s'"), pExtent->pszFullname);
        }
        if (pExtent->pRGD)
        {
            rc = RTFileGetSize(pExtent->File, &cbExtentSize);
            if (VBOX_FAILURE(rc))
                return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: error getting size in '%s'"), pExtent->pszFullname);
            Assert(!(cbExtentSize % 512));
            uRGTSector = VMDK_BYTE2SECTOR(cbExtentSize);
            /* Write backup grain table by writing the required number of grain
             * table cache chunks. Avoids dynamic memory allocation, but is a
             * bit slower. But as this is a pretty infrequently occurring case
             * it should be acceptable. */
            for (unsigned i = 0; i < pExtent->cGTEntries / VMDK_GT_CACHELINE_SIZE; i++)
            {
                rc = RTFileWriteAt(pExtent->File, VMDK_SECTOR2BYTE(uRGTSector) + i * sizeof(aGTDataTmp), aGTDataTmp, sizeof(aGTDataTmp), NULL);
                if (VBOX_FAILURE(rc))
                    return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: cannot write backup grain table allocation in '%s'"), pExtent->pszFullname);
            }
        }

        /* Update the grain directory on disk (doing it before writing the
         * grain table will result in a garbled extent if the operation is
         * aborted for some reason. Otherwise the worst that can happen is
         * some unused sectors in the extent. */
        uint32_t uGTSectorLE = RT_H2LE_U64(uGTSector);
        rc = RTFileWriteAt(pExtent->File, VMDK_SECTOR2BYTE(pExtent->uSectorGD) + uGDIndex * sizeof(uGTSectorLE), &uGTSectorLE, sizeof(uGTSectorLE), NULL);
        if (VBOX_FAILURE(rc))
            return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: cannot write grain directory entry in '%s'"), pExtent->pszFullname);
        if (pExtent->pRGD)
        {
            uint32_t uRGTSectorLE = RT_H2LE_U64(uRGTSector);
            rc = RTFileWriteAt(pExtent->File, VMDK_SECTOR2BYTE(pExtent->uSectorRGD) + uGDIndex * sizeof(uRGTSectorLE), &uRGTSectorLE, sizeof(uRGTSectorLE), NULL);
            if (VBOX_FAILURE(rc))
                return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: cannot write backup grain directory entry in '%s'"), pExtent->pszFullname);
        }

        /* As the final step update the in-memory copy of the GDs. */
        pExtent->pGD[uGDIndex] = uGTSector;
        if (pExtent->pRGD)
            pExtent->pRGD[uGDIndex] = uRGTSector;
    }

    rc = RTFileGetSize(pExtent->File, &cbExtentSize);
    if (VBOX_FAILURE(rc))
        return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: error getting size in '%s'"), pExtent->pszFullname);
    Assert(!(cbExtentSize % 512));

    /* Write the data. */
    rc = RTFileWriteAt(pExtent->File, cbExtentSize, pvBuf, cbWrite, NULL);
    if (VBOX_FAILURE(rc))
        return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: cannot write allocated data block in '%s'"), pExtent->pszFullname);

    /* Update the grain table (and the cache). */
    uGTBlock = uSector / (pExtent->cSectorsPerGrain * VMDK_GT_CACHELINE_SIZE);
    uGTHash = vmdkGTCacheHash(pCache, uGTBlock, pExtent->uExtent);
    pGTCacheEntry = &pCache->aGTCache[uGTHash];
    if (    pGTCacheEntry->uExtent != pExtent->uExtent
        ||  pGTCacheEntry->uGTBlock != uGTBlock)
    {
        /* Cache miss, fetch data from disk. */
        rc = RTFileReadAt(pExtent->File,
                          VMDK_SECTOR2BYTE(uGTSector) + (uGTBlock % (pExtent->cGTEntries / VMDK_GT_CACHELINE_SIZE)) * sizeof(aGTDataTmp),
                          aGTDataTmp, sizeof(aGTDataTmp), NULL);
        if (VBOX_FAILURE(rc))
            return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: cannot read allocated grain table entry in '%s'"), pExtent->pszFullname);
        pGTCacheEntry->uExtent = pExtent->uExtent;
        pGTCacheEntry->uGTBlock = uGTBlock;
        for (unsigned i = 0; i < VMDK_GT_CACHELINE_SIZE; i++)
            pGTCacheEntry->aGTData[i] = RT_LE2H_U32(aGTDataTmp[i]);
    }
    else
    {
        /* Cache hit. Convert grain table block back to disk format, otherwise
         * the code below will write garbage for all but the updated entry. */
        for (unsigned i = 0; i < VMDK_GT_CACHELINE_SIZE; i++)
            aGTDataTmp[i] = RT_H2LE_U32(pGTCacheEntry->aGTData[i]);
    }
    uGTBlockIndex = (uSector / pExtent->cSectorsPerGrain) % VMDK_GT_CACHELINE_SIZE;
    aGTDataTmp[uGTBlockIndex] = RT_H2LE_U32(VMDK_BYTE2SECTOR(cbExtentSize));
    pGTCacheEntry->aGTData[uGTBlockIndex] = VMDK_BYTE2SECTOR(cbExtentSize);
    /* Update grain table on disk. */
    rc = RTFileWriteAt(pExtent->File,
                       VMDK_SECTOR2BYTE(uGTSector) + (uGTBlock % (pExtent->cGTEntries / VMDK_GT_CACHELINE_SIZE)) * sizeof(aGTDataTmp),
                       aGTDataTmp, sizeof(aGTDataTmp), NULL);
    if (VBOX_FAILURE(rc))
        return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: cannot write updated grain table in '%s'"), pExtent->pszFullname);
    if (pExtent->pRGD)
    {
        /* Update backup grain table on disk. */
        rc = RTFileWriteAt(pExtent->File,
                           VMDK_SECTOR2BYTE(uRGTSector) + (uGTBlock % (pExtent->cGTEntries / VMDK_GT_CACHELINE_SIZE)) * sizeof(aGTDataTmp),
                           aGTDataTmp, sizeof(aGTDataTmp), NULL);
        if (VBOX_FAILURE(rc))
            return vmdkError(pExtent->pImage, rc, RT_SRC_POS, N_("VMDK: cannot write updated backup grain table in '%s'"), pExtent->pszFullname);
    }
#ifdef VBOX_WITH_VMDK_ESX
    if (VBOX_SUCCESS(rc) && pExtent->enmType == VMDKETYPE_ESX_SPARSE)
    {
        pExtent->uFreeSector = uGTSector + VMDK_BYTE2SECTOR(cbWrite);
        pExtent->fMetaDirty = true;
    }
#endif /* VBOX_WITH_VMDK_ESX */
    return rc;
}

static int vmdkOpen(const char *pszFilename, unsigned uOpenFlags, PFNVDERROR pfnError, void *pvErrorUser, void **ppvBackendData)
{
    int rc;
    PVMDKIMAGE pImage;

    /** @todo check the image file name for invalid characters, especially double quotes. */

    /* Check open flags. All valid flags are supported. */
    if (uOpenFlags & ~VD_OPEN_FLAGS_MASK)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    pImage = (PVMDKIMAGE)RTMemAllocZ(sizeof(VMDKIMAGE));
    if (!pImage)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }
    pImage->pszFilename = pszFilename;
    pImage->File = NIL_RTFILE;
    pImage->pExtents = NULL;
    pImage->pGTCache = NULL;
    pImage->pDescData = NULL;
    pImage->pfnError = pfnError;
    pImage->pvErrorUser = pvErrorUser;

    rc = vmdkOpenImage(pImage, pszFilename, uOpenFlags);
    if (VBOX_SUCCESS(rc))
        *ppvBackendData = pImage;

out:
    LogFlow(("%s: returned %Vrc\n", __FUNCTION__, rc));
    return rc;
}

static int vmdkCreate(const char *pszFilename, VDIMAGETYPE enmType,
                      uint64_t cbSize, unsigned uImageFlags,
                      const char *pszComment, uint32_t cCylinders,
                      uint32_t cHeads, uint32_t cSectors, unsigned uOpenFlags,
                      PFNVMPROGRESS pfnProgress, void *pvUser,
                      PFNVDERROR pfnError, void *pvErrorUser,
                      void **ppvBackendData)
{
    int rc;
    PVMDKIMAGE pImage;

    /** @todo check the image file name for invalid characters, especially double quotes. */

    /* Check open flags. All valid flags are supported. */
    if (uOpenFlags & ~VD_OPEN_FLAGS_MASK)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    pImage = (PVMDKIMAGE)RTMemAllocZ(sizeof(VMDKIMAGE));
    if (!pImage)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }
    pImage->pszFilename = pszFilename;
    pImage->File = NIL_RTFILE;
    pImage->pExtents = NULL;
    pImage->pGTCache = NULL;
    pImage->pDescData = NULL;
    pImage->pfnError = pfnError;
    pImage->pvErrorUser = pvErrorUser;
    pImage->cbDescAlloc = VMDK_SECTOR2BYTE(20);
    pImage->pDescData = (char *)RTMemAllocZ(pImage->cbDescAlloc);
    if (!pImage->pDescData)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }

    rc = vmdkCreateImage(pImage, pszFilename, enmType, cbSize, uImageFlags,
                         pszComment, cCylinders, cHeads, cSectors);
    if (VBOX_SUCCESS(rc))
    {
        /* So far the image is opened in read/write mode. Make sure the
         * image is opened in read-only mode if the caller requested that. */
        if (uOpenFlags & VD_OPEN_FLAGS_READONLY)
        {
            vmdkFreeImage(pImage, false);
            rc = vmdkOpenImage(pImage, pszFilename, uOpenFlags);
            if (VBOX_FAILURE(rc))
                goto out;
        }
        *ppvBackendData = pImage;
    }

out:
    /** @todo implement meaningful progress stuff (especially for fixed images). */
    if (    VBOX_SUCCESS(rc)
        &&  pfnProgress)
        pfnProgress(NULL /* WARNING! pVM=NULL  */, 100, pvUser);

    LogFlow(("%s: returned %Vrc\n", __FUNCTION__, rc));
    return rc;
}

static int vmdkClose(void *pBackendData, bool fDelete)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    /* Freeing a never allocated image (e.g. because the open failed) is
     * not signalled as an error. After all nothing bad happens. */
    if (pImage)
        vmdkFreeImage(pImage, fDelete);

    LogFlow(("%s: returned %Vrc\n", __FUNCTION__, rc));
    return rc;
}

static int vmdkRead(void *pBackendData, uint64_t uOffset, void *pvBuf, size_t cbRead, size_t *pcbActuallyRead)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    PVMDKEXTENT pExtent;
    uint64_t uSectorExtentRel;
    uint64_t uSectorExtentAbs;
    int rc;

    Assert(uOffset % 512 == 0);
    Assert(cbRead % 512 == 0);

    if (uOffset + cbRead > pImage->cbSize)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    rc = vmdkFindExtent(pImage, VMDK_BYTE2SECTOR(uOffset),
                        &pExtent, &uSectorExtentRel);
    if (VBOX_FAILURE(rc))
        goto out;

    /* Check access permissions as defined in the extent descriptor. */
    if (pExtent->enmAccess == VMDKACCESS_NOACCESS)
    {
        rc = VERR_VDI_INVALID_STATE;
        goto out;
    }

    /* Clip read range to remain in this extent. */
    cbRead = RT_MIN(cbRead, VMDK_SECTOR2BYTE(pExtent->uSectorOffset + pExtent->cNominalSectors - uSectorExtentRel));

    /* Handle the read according to the current extent type. */
    switch (pExtent->enmType)
    {
        case VMDKETYPE_HOSTED_SPARSE:
#ifdef VBOX_WITH_VMDK_ESX
        case VMDKETYPE_ESX_SPARSE:
#endif /* VBOX_WITH_VMDK_ESX */
            rc = vmdkGetSector(pImage->pGTCache, pExtent, uSectorExtentRel,
                               &uSectorExtentAbs);
            if (VBOX_FAILURE(rc))
                goto out;
            /* Clip read range to at most the rest of the grain. */
            cbRead = RT_MIN(cbRead, VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain - uSectorExtentRel % pExtent->cSectorsPerGrain));
            Assert(!(cbRead % 512));
            if (uSectorExtentAbs == 0)
                rc = VINF_VDI_BLOCK_FREE;
            else
                rc = RTFileReadAt(pExtent->File,
                                  VMDK_SECTOR2BYTE(uSectorExtentAbs),
                                  pvBuf, cbRead, NULL);
            break;
        case VMDKETYPE_FLAT:
            rc = RTFileReadAt(pExtent->File, VMDK_SECTOR2BYTE(uSectorExtentRel),
                              pvBuf, cbRead, NULL);
            break;
        case VMDKETYPE_ZERO:
            memset(pvBuf, '\0', cbRead);
            break;
    }
    *pcbActuallyRead = cbRead;

out:
    return rc;
}

static int vmdkWrite(void *pBackendData, uint64_t uOffset, const void *pvBuf, size_t cbWrite, size_t *pcbWriteProcess, size_t *pcbPreRead, size_t *pcbPostRead)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    PVMDKEXTENT pExtent;
    uint64_t uSectorExtentRel;
    uint64_t uSectorExtentAbs;
    int rc;

    Assert(uOffset % 512 == 0);
    Assert(cbWrite % 512 == 0);

    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
    {
        rc = VERR_VDI_IMAGE_READ_ONLY;
        goto out;
    }

    /* No size check here, will do that later when the extent is located.
     * There are sparse images out there which according to the spec are
     * invalid, because the total size is not a multiple of the grain size.
     * Also for sparse images which are stitched together in odd ways (not at
     * grain boundaries, and with the nominal size not being a multiple of the
     * grain size), this would prevent writing to the last grain. */

    rc = vmdkFindExtent(pImage, VMDK_BYTE2SECTOR(uOffset),
                        &pExtent, &uSectorExtentRel);
    if (VBOX_FAILURE(rc))
        goto out;

    /* Check access permissions as defined in the extent descriptor. */
    if (pExtent->enmAccess != VMDKACCESS_READWRITE)
    {
        rc = VERR_VDI_INVALID_STATE;
        goto out;
    }

    /** @todo implement suppressing of zero data writes (a bit tricky in this
     * case, as VMDK has no marker for zero blocks). We somehow need to get the
     * information whether the information in this area is all zeroes as of the
     * parent image. Then (based on the assumption that parent images are
     * immutable) the write can be ignored. */

    /* Handle the write according to the current extent type. */
    switch (pExtent->enmType)
    {
        case VMDKETYPE_HOSTED_SPARSE:
#ifdef VBOX_WITH_VMDK_ESX
        case VMDKETYPE_ESX_SPARSE:
#endif /* VBOX_WITH_VMDK_ESX */
            rc = vmdkGetSector(pImage->pGTCache, pExtent, uSectorExtentRel,
                               &uSectorExtentAbs);
            if (VBOX_FAILURE(rc))
                goto out;
            /* Clip write range to at most the rest of the grain. */
            cbWrite = RT_MIN(cbWrite, VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain - uSectorExtentRel % pExtent->cSectorsPerGrain));
            if (uSectorExtentAbs == 0)
            {
                if (cbWrite == VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain))
                {
                    /* Full block write to a previously unallocated block.
                     * Allocate GT and find out where to store the grain. */
                    rc = vmdkAllocGrain(pImage->pGTCache, pExtent,
                                        uSectorExtentRel, pvBuf, cbWrite);
                    *pcbPreRead = 0;
                    *pcbPostRead = 0;
                }
                else
                {
                    /* Clip write range to remain in this extent. */
                    cbWrite = RT_MIN(cbWrite, VMDK_SECTOR2BYTE(pExtent->uSectorOffset + pExtent->cNominalSectors - uSectorExtentRel));
                    *pcbPreRead = VMDK_SECTOR2BYTE(uSectorExtentRel % pExtent->cSectorsPerGrain);
                    *pcbPostRead = VMDK_SECTOR2BYTE(pExtent->cSectorsPerGrain) - cbWrite - *pcbPreRead;
                    rc = VINF_VDI_BLOCK_FREE;
                }
            }
            else
                rc = RTFileWriteAt(pExtent->File,
                                   VMDK_SECTOR2BYTE(uSectorExtentAbs),
                                   pvBuf, cbWrite, NULL);
            break;
        case VMDKETYPE_FLAT:
            /* Clip write range to remain in this extent. */
            cbWrite = RT_MIN(cbWrite, VMDK_SECTOR2BYTE(pExtent->uSectorOffset + pExtent->cNominalSectors - uSectorExtentRel));
            rc = RTFileWriteAt(pExtent->File, VMDK_SECTOR2BYTE(uSectorExtentRel), pvBuf, cbWrite, NULL);
            break;
        case VMDKETYPE_ZERO:
            /* Clip write range to remain in this extent. */
            cbWrite = RT_MIN(cbWrite, VMDK_SECTOR2BYTE(pExtent->uSectorOffset + pExtent->cNominalSectors - uSectorExtentRel));
            break;
    }
    if (pcbWriteProcess)
        *pcbWriteProcess = cbWrite;

out:
    return rc;
}

static int vmdkFlush(void *pBackendData)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    int rc = vmdkFlushImage(pImage);

    return rc;
}

static int vmdkGetImageType(void *pBackendData, PVDIMAGETYPE penmImageType)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    Assert(pImage);
    Assert(penmImageType);

    if (pImage && pImage->cExtents != 0)
        *penmImageType = pImage->enmImageType;
    else
        rc = VERR_VDI_NOT_OPENED;

    return rc;
}

static uint64_t vmdkGetSize(void *pBackendData)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;

    Assert(pImage);

    if (pImage)
        return pImage->cbSize;
    else
        return 0;
}

static int vmdkGetGeometry(void *pBackendData, unsigned *pcCylinders, unsigned *pcHeads, unsigned *pcSectors)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    Assert(pImage);

    if (pImage)
    {
        if (pImage->cCylinders)
        {
            *pcCylinders = pImage->cCylinders;
            *pcHeads = pImage->cHeads;
            *pcSectors = pImage->cSectors;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_VDI_GEOMETRY_NOT_SET;
    }
    else
        rc = VERR_VDI_NOT_OPENED;
    LogFlow(("%s: returned %Vrc (CHS=%u/%u/%u)\n", __FUNCTION__, rc,
             pImage->cCylinders, pImage->cHeads, pImage->cSectors));
    return rc;
}

static int vmdkSetGeometry(void *pBackendData, unsigned cCylinders, unsigned cHeads, unsigned cSectors)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    Assert(pImage);

    if (pImage)
    {
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        {
            rc = VERR_VDI_IMAGE_READ_ONLY;
            goto out;
        }
        rc = vmdkDescDDBSetU32(pImage, &pImage->Descriptor,
                               "ddb.geometry.cylinders", cCylinders);
        if (VBOX_FAILURE(rc))
            goto out;
        rc = vmdkDescDDBSetU32(pImage, &pImage->Descriptor,
                               "ddb.geometry.heads", cHeads);
        if (VBOX_FAILURE(rc))
            goto out;
        rc = vmdkDescDDBSetU32(pImage, &pImage->Descriptor,
                               "ddb.geometry.sectors", cSectors);
        if (VBOX_FAILURE(rc))
            goto out;

        pImage->cCylinders = cCylinders;
        pImage->cHeads = cHeads;
        pImage->cSectors = cSectors;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VDI_NOT_OPENED;

out:
    LogFlow(("%s: returned %Vrc\n", __FUNCTION__, rc));
    return rc;
}

static int vmdkGetTranslation(void *pBackendData, PPDMBIOSTRANSLATION penmTranslation)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    Assert(pImage);

    if (pImage)
    {
        if (pImage->enmTranslation)
        {
            *penmTranslation = pImage->enmTranslation;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_VDI_GEOMETRY_NOT_SET;
    }
    else
        rc = VERR_VDI_NOT_OPENED;
    LogFlow(("%s: returned %Vrc (%d)\n", __FUNCTION__, rc,
             pImage->enmTranslation));
    return rc;
}

static int vmdkSetTranslation(void *pBackendData, PDMBIOSTRANSLATION enmTranslation)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    Assert(pImage);

    if (pImage)
    {
        /** @todo maybe store this in the image descriptor */
        pImage->enmTranslation = enmTranslation;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VDI_NOT_OPENED;
    LogFlow(("%s: returned %Vrc\n", __FUNCTION__, rc));
    return rc;
}

static unsigned vmdkGetOpenFlags(void *pBackendData)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    unsigned uOpenFlags;

    Assert(pImage);

    if (pImage)
        uOpenFlags = pImage->uOpenFlags;
    else
        uOpenFlags = 0;

    LogFlow(("%s: returned %d\n", __FUNCTION__, uOpenFlags));
    return uOpenFlags;
}

static int vmdkSetOpenFlags(void *pBackendData, unsigned uOpenFlags)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;
    const char *pszFilename;

    /* Image must be opened and the new flags must be valid. Just readonly flag
     * is supported. */
    if (!pImage || uOpenFlags & ~VD_OPEN_FLAGS_READONLY)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    /* Implement this operation via reopening the image. */
    pszFilename = pImage->pszFilename;
    vmdkFreeImage(pImage, false);
    rc = vmdkOpenImage(pImage, pszFilename, uOpenFlags);

out:
    LogFlow(("%s: returned %Vrc\n", __FUNCTION__, rc));
    return rc;
}

static int vmdkGetComment(void *pBackendData, char *pszComment, size_t cbComment)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    Assert(pImage);

    if (pImage)
    {
        const char *pszCommentEncoded = NULL;
        rc = vmdkDescDDBGetStr(pImage, &pImage->Descriptor,
                              "ddb.comment", &pszCommentEncoded);
        if (rc == VERR_VDI_VALUE_NOT_FOUND)
            pszCommentEncoded = NULL;
        else if (VBOX_FAILURE(rc))
            goto out;

        if (pszComment)
            rc = vmdkDecodeString(pszCommentEncoded, pszComment, cbComment);
        else
        {
            *pszComment = '\0';
            rc = VINF_SUCCESS;
        }
        if (pszCommentEncoded)
            RTStrFree((char *)(void *)pszCommentEncoded);
    }
    else
        rc = VERR_VDI_NOT_OPENED;

out:
    LogFlow(("%s: returned %Vrc comment='%s'\n", __FUNCTION__, rc, pszComment));
    return rc;
}

static int vmdkSetComment(void *pBackendData, const char *pszComment)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    LogFlow(("%s: comment '%s'\n", pszComment));
    Assert(pImage);

    if (pImage)
    {
        rc = vmdkSetImageComment(pImage, pszComment);
    }
    else
        rc = VERR_VDI_NOT_OPENED;

    LogFlow(("%s: returned %Vrc\n", __FUNCTION__, rc));
    return rc;
}

static int vmdkGetUuid(void *pBackendData, PRTUUID pUuid)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    Assert(pImage);

    if (pImage)
    {
        *pUuid = pImage->ImageUuid;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VDI_NOT_OPENED;
    LogFlow(("%s: returned %Vrc (%Vuuid)\n", __FUNCTION__, rc, pUuid));
    return rc;
}

static int vmdkSetUuid(void *pBackendData, PCRTUUID pUuid)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    LogFlow(("%s: %Vuuid\n", pUuid));
    Assert(pImage);

    if (pImage)
    {
        pImage->ImageUuid = *pUuid;
        rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                "ddb.uuid.image", pUuid);
        if (VBOX_FAILURE(rc))
            return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error storing image UUID in descriptor in '%s'"), pImage->pszFilename);
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VDI_NOT_OPENED;
    LogFlow(("%s: returned %Vrc\n", __FUNCTION__, rc));
    return rc;
}

static int vmdkGetModificationUuid(void *pBackendData, PRTUUID pUuid)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    Assert(pImage);

    if (pImage)
    {
        *pUuid = pImage->ModificationUuid;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VDI_NOT_OPENED;
    LogFlow(("%s: returned %Vrc (%Vuuid)\n", __FUNCTION__, rc, pUuid));
    return rc;
}

static int vmdkSetModificationUuid(void *pBackendData, PCRTUUID pUuid)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    LogFlow(("%s: %Vuuid\n", pUuid));
    Assert(pImage);

    if (pImage)
    {
        pImage->ModificationUuid = *pUuid;
        rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                "ddb.uuid.modification", pUuid);
        if (VBOX_FAILURE(rc))
            return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error storing modification UUID in descriptor in '%s'"), pImage->pszFilename);
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VDI_NOT_OPENED;
    LogFlow(("%s: returned %Vrc\n", __FUNCTION__, rc));
    return rc;
}

static int vmdkGetParentUuid(void *pBackendData, PRTUUID pUuid)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    Assert(pImage);

    if (pImage)
    {
        *pUuid = pImage->ParentUuid;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VDI_NOT_OPENED;
    LogFlow(("%s: returned %Vrc (%Vuuid)\n", __FUNCTION__, rc, pUuid));
    return rc;
}

static int vmdkSetParentUuid(void *pBackendData, PCRTUUID pUuid)
{
    PVMDKIMAGE pImage = (PVMDKIMAGE)pBackendData;
    int rc;

    LogFlow(("%s: %Vuuid\n", pUuid));
    Assert(pImage);

    if (pImage)
    {
        pImage->ParentUuid = *pUuid;
        rc = vmdkDescDDBSetUuid(pImage, &pImage->Descriptor,
                                "ddb.uuid.parent", pUuid);
        if (VBOX_FAILURE(rc))
            return vmdkError(pImage, rc, RT_SRC_POS, N_("VMDK: error storing parent image UUID in descriptor in '%s'"), pImage->pszFilename);
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VDI_NOT_OPENED;
    LogFlow(("%s: returned %Vrc\n", __FUNCTION__, rc));
    return rc;
}


VBOXHDDBACKEND g_VmdkBackend =
{
    /* pfnOpen */
    vmdkOpen,
    /* pfnCreate */
    vmdkCreate,
    /* pfnClose */
    vmdkClose,
    /* pfnRead */
    vmdkRead,
    /* pfnWrite */
    vmdkWrite,
    /* pfnFlush */
    vmdkFlush,
    /* pfnGetImageType */
    vmdkGetImageType,
    /* pfnGetSize */
    vmdkGetSize,
    /* pfnGetGeometry */
    vmdkGetGeometry,
    /* pfnSetGeometry */
    vmdkSetGeometry,
    /* pfnGetTranslation */
    vmdkGetTranslation,
    /* pfnSetTranslation */
    vmdkSetTranslation,
    /* pfnGetOpenFlags */
    vmdkGetOpenFlags,
    /* pfnSetOpenFlags */
    vmdkSetOpenFlags,
    /* pfnGetComment */
    vmdkGetComment,
    /* pfnSetComment */
    vmdkSetComment,
    /* pfnGetUuid */
    vmdkGetUuid,
    /* pfnSetUuid */
    vmdkSetUuid,
    /* pfnGetModificationUuid */
    vmdkGetModificationUuid,
    /* pfnSetModificationUuid */
    vmdkSetModificationUuid,
    /* pfnGetParentUuid */
    vmdkGetParentUuid,
    /* pfnSetParentUuid */
    vmdkSetParentUuid
};
