/* $Id$ */
/** @file
 * InnoTek Portable Runtime - File System, Generic Stubs.
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
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP RTLOGGROUP_FS

#include <iprt/fs.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include "internal/fs.h"



RTR3DECL(int) RTFsQuerySizes(const char *pszFsPath, RTFOFF *pcbTotal, RTFOFF *pcbFree,
                             uint32_t *pcbBlock, uint32_t *pcbSector)
{
    if (pcbTotal)
        *pcbTotal = _2G;
    if (pcbFree)
        *pcbFree = _1G;
    if (pcbBlock)
        *pcbBlock = _4K;
    if (pcbSector)
        *pcbSector = 512;
    LogFlow(("RTFsQuerySizes: success stub!\n"));
    return VINF_SUCCESS;
}


RTR3DECL(int) RTFsQuerySerial(const char *pszFsPath, uint32_t *pu32Serial)
{
    if (pu32Serial)
        *pu32Serial = 0xc0ffee;
    LogFlow(("RTFsQuerySerial: success stub!\n"));
    return VINF_SUCCESS;
}


RTR3DECL(int) RTFsQueryProperties(const char *pszFsPath, PRTFSPROPERTIES pProperties)
{
    pProperties->cbMaxComponent = 255;
    pProperties->fCaseSensitive = true;
    pProperties->fCompressed = false;
    pProperties->fFileCompression = false;
    pProperties->fReadOnly = false;
    pProperties->fRemote = false;
    pProperties->fSupportsUnicode = true;
    LogFlow(("RTFsQueryProperties: success stub!\n"));
    return VINF_SUCCESS;
}

