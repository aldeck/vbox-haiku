/* $Id$ */
/** @file
 * innotek Portable Runtime - RTFileMove, Generic.
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

#define LOG_GROUP RTLOGGROUP_FILE
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/log.h>


RTDECL(int) RTFileMove(const char *pszSrc, const char *pszDst, unsigned fMove)
{
    /*
     * Validate input.
     */
    AssertMsgReturn(VALID_PTR(pszSrc), ("%p\n", pszSrc), VERR_INVALID_POINTER);
    AssertMsgReturn(VALID_PTR(pszDst), ("%p\n", pszDst), VERR_INVALID_POINTER);
    AssertMsgReturn(*pszSrc, ("%p\n", pszSrc), VERR_INVALID_PARAMETER);
    AssertMsgReturn(*pszDst, ("%p\n", pszDst), VERR_INVALID_PARAMETER);
    AssertMsgReturn(!(fMove & ~RTFILEMOVE_FLAGS_REPLACE), ("%#x\n", fMove), VERR_INVALID_PARAMETER);

    /*
     * Try RTFileRename first.
     */
    Assert(RTPATHRENAME_FLAGS_REPLACE == RTFILEMOVE_FLAGS_REPLACE);
    unsigned fRename = fMove;
    int rc = RTFileRename(pszSrc, pszDst, fRename);
    if (rc == VERR_NOT_SAME_DEVICE)
    {
        const char *pszDelete = NULL;

        /*
         * The source and target are not on the same device, darn.
         * We'll try open both ends and perform a copy.
         */
        RTFILE FileSrc;
        rc = RTFileOpen(&FileSrc, pszSrc, RTFILE_O_READ | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN);
        if (RT_SUCCESS(rc))
        {
            RTFILE FileDst;
            rc = RTFileOpen(&FileDst, pszDst, RTFILE_O_WRITE | RTFILE_O_DENY_ALL | RTFILE_O_CREATE_REPLACE);
            if (RT_SUCCESS(rc))
            {
                rc = RTFileCopyByHandles(FileSrc, FileDst);
                if (RT_SUCCESS(rc))
                    pszDelete = pszSrc;
                else
                {
                    pszDelete = pszDst;
                    Log(("RTFileMove('%s', '%s', %#x): copy failed, rc=%Rrc\n",
                         pszSrc, pszDst, fMove, rc));
                }

                /* try delete without closing, and could perhaps avoid some trouble */
                int rc2 = RTFileDelete(pszDelete);
                if (RT_SUCCESS(rc2))
                    pszDelete = NULL;
                RTFileClose(FileDst);
            }
            else
                Log(("RTFileMove('%s', '%s', %#x): failed to create destination, rc=%Rrc\n",
                     pszSrc, pszDst, fMove, rc));
            RTFileClose(FileSrc);
        }
        else
            Log(("RTFileMove('%s', '%s', %#x): failed to open source, rc=%Rrc\n",
                 pszSrc, pszDst, fMove, rc));

        /* if we failed to close it while open, close it now */
        if (pszDelete)
        {
            int rc2 = RTFileDelete(pszDelete);
            if (RT_FAILURE(rc2))
                Log(("RTFileMove('%s', '%s', %#x): failed to delete '%s', rc2=%Rrc (rc=%Rrc)\n",
                     pszSrc, pszDst, fMove, pszDelete, rc2, rc));
        }
    }

    LogFlow(("RTDirRename(%p:{%s}, %p:{%s}, %#x): returns %Rrc\n",
             pszSrc, pszSrc, pszDst, pszDst, fMove, rc));
    return rc;
}

