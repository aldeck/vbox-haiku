/* $Id$ */
/** @file
 * InnoTek Portable Runtime - Path Manipulation, POSIX.
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
#define LOG_GROUP RTLOGGROUP_PATH
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#ifdef __DARWIN__
# include <mach-o/dyld.h>
#endif

#include <iprt/path.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include "internal/path.h"
#include "internal/fs.h"

#ifdef __L4__
# include <l4/vboxserver/vboxserver.h>
#endif




RTDECL(int) RTPathReal(const char *pszPath, char *pszRealPath, unsigned cchRealPath)
{
    /*
     * Convert input.
     */
    char *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath);
    if (RT_SUCCESS(rc))
    {
        /*
         * On POSIX platforms the API doesn't take a length parameter, which makes it
         * a little bit more work.
         */
        char szTmpPath[PATH_MAX + 1];
        const char *psz = realpath(pszNativePath, szTmpPath);
        if (psz)
        {
            /*
             * Convert result and copy it to the return buffer.
             */
            char *pszUtf8RealPath;
            rc = rtPathFromNative(&pszUtf8RealPath, szTmpPath);
            if (RT_SUCCESS(rc))
            {
                size_t cch = strlen(pszUtf8RealPath) + 1;
                if (cch <= cchRealPath)
                    memcpy(pszRealPath, pszUtf8RealPath, cch);
                else
                    rc = VERR_BUFFER_OVERFLOW;
                RTStrFree(pszUtf8RealPath);
            }
        }
        else
            rc = RTErrConvertFromErrno(errno);
        RTStrFree(pszNativePath);
    }

    LogFlow(("RTPathReal(%p:{%s}, %p:{%s}, %u): returns %Rrc\n", pszPath, pszPath,
             pszRealPath, RT_SUCCESS(rc) ? pszRealPath : "<failed>",  cchRealPath));
    return rc;
}


/**
 * Cleans up a path specifier a little bit.
 * This includes removing duplicate slashes, uncessary single dots, and
 * trailing slashes.
 *
 * @returns Number of bytes in the clean path.
 * @param   pszPath     The path to cleanup.
 * @remark  Borrowed from InnoTek libc.
 */
static int fsCleanPath(char *pszPath)
{
    /*
     * Change to '/' and remove duplicates.
     */
    char   *pszSrc = pszPath;
    char   *pszTrg = pszPath;
#ifdef HAVE_UNC
    int     fUnc = 0;
    if (    RTPATH_IS_SLASH(pszPath[0])
        &&  RTPATH_IS_SLASH(pszPath[1]))
    {   /* Skip first slash in a unc path. */
        pszSrc++;
        *pszTrg++ = '/';
        fUnc = 1;
    }
#endif

    for (;;)
    {
        char ch = *pszSrc++;
        if (RTPATH_IS_SEP(ch))
        {
            *pszTrg++ = RTPATH_SLASH;
            for (;;)
            {
                do  ch = *pszSrc++;
                while (RTPATH_IS_SEP(ch));

                /* Remove '/./' and '/.'. */
                if (ch != '.' || (*pszSrc && !RTPATH_IS_SEP(*pszSrc)))
                    break;
            }
        }
        *pszTrg = ch;
        if (!ch)
            break;
        pszTrg++;
    }

    /*
     * Remove trailing slash if the path may be pointing to a directory.
     */
    int cch = pszTrg - pszPath;
    if (    cch > 1
        &&  pszTrg[-1] == RTPATH_SLASH
#ifdef HAVE_DRIVE
        &&  pszTrg[-2] != ':'
#endif
        &&  pszTrg[-2] != RTPATH_SLASH)
        pszPath[--cch] = '\0';

    return cch;
}


RTDECL(int) RTPathAbs(const char *pszPath, char *pszAbsPath, unsigned cchAbsPath)
{
    /*
     * Convert input.
     */
    char *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath);
    if (RT_FAILURE(rc))
    {
        LogFlow(("RTPathAbs(%p:{%s}, %p, %d): returns %Rrc\n", pszPath, pszPath, pszAbsPath, cchAbsPath));
        return rc;
    }

    /*
     * On POSIX platforms the API doesn't take a length parameter, which makes it
     * a little bit more work.
     */
    char szTmpPath[PATH_MAX + 1];
    char *psz = realpath(pszNativePath, szTmpPath);
    if (!psz)
    {
        if (errno == ENOENT || errno == ENOTDIR)
        {
            if (strlen(pszNativePath) <= PATH_MAX)
            {
                /*
                 * Iterate the path bit by bit an apply realpath to it.
                 */

                char szTmpSrc[PATH_MAX + 1];
                strcpy(szTmpSrc, pszNativePath);
                fsCleanPath(szTmpSrc);

                size_t cch = 0; // current resolved path length
                char *pszCur = szTmpSrc;

                if (*pszCur == RTPATH_SLASH)
                {
                    psz = szTmpPath;
                    pszCur++;
                }
                else
                {
                    /* get the cwd */
                    psz = getcwd(szTmpPath,  sizeof(szTmpPath));
                    AssertMsg(psz, ("Couldn't get cwd!\n"));
                    if (psz)
                        cch = strlen(psz);
                    else
                        rc = RTErrConvertFromErrno(errno);
                }

                if (psz)
                {
                    bool fResolveSymlinks = true;
                    char szTmpPath2[PATH_MAX + 1];

                    while (*pszCur)
                    {
                        char *pszSlash = strchr(pszCur, RTPATH_SLASH);
                        size_t cchElement = pszSlash ? pszSlash - pszCur : strlen(pszCur);
                        if (cch + cchElement + 1 > PATH_MAX)
                        {
                            rc = VERR_FILENAME_TOO_LONG;
                            break;
                        }

                        if (!strncmp(pszCur, "..", cchElement))
                        {
                            char *pszLastSlash = strrchr(psz, RTPATH_SLASH);
                            if (pszLastSlash)
                            {
                                cch = pszLastSlash - psz;
                                psz[cch] = '\0';
                            }
                            /* else: We've reached the root and the parent of the root is the root. */
                        }
                        else
                        {
                            psz[cch++] = RTPATH_SLASH;
                            memcpy(psz + cch, pszCur, cchElement);
                            cch += cchElement;
                            psz[cch] = '\0';

                            if (fResolveSymlinks)
                            {
                                /* resolve possible symlinks */
                                char *psz2 = realpath(psz, psz == szTmpPath
                                                           ? szTmpPath2
                                                           : szTmpPath);
                                if (psz2)
                                {
                                    psz = psz2;
                                    cch = strlen(psz);
                                }
                                else
                                {
                                    if (errno != ENOENT && errno != ENOTDIR)
                                    {
                                        rc = RTErrConvertFromErrno(errno);
                                        break;
                                    }

                                    /* no more need to resolve symlinks */
                                    fResolveSymlinks = false;
                                }
                            }
                        }

                        pszCur += cchElement;
                        /* skip the slash */
                        if (*pszCur)
                            ++pszCur;
                    }

                    /* if the length is zero here, then we're at the root (Not true for half-posixs stuff such as libc!) */
                    if (!cch)
                    {
                        psz[cch++] = RTPATH_SLASH;
                        psz[cch] = '\0';
                    }
                }
            }
            else
                rc = VERR_FILENAME_TOO_LONG;
        }
        else
            rc = RTErrConvertFromErrno(errno);
    }

    RTStrFree(pszNativePath);

    if (psz && RT_SUCCESS(rc))
    {
        /*
         * Convert result and copy it to the return buffer.
         */
        char *pszUtf8AbsPath;
        rc = rtPathFromNative(&pszUtf8AbsPath, psz);
        if (RT_FAILURE(rc))
        {
            LogFlow(("RTPathAbs(%p:{%s}, %p, %d): returns %Rrc\n", pszPath, pszPath, pszAbsPath, cchAbsPath));
            return rc;
        }

        unsigned cch = strlen(pszUtf8AbsPath) + 1;
        if (cch <= cchAbsPath)
            memcpy(pszAbsPath, pszUtf8AbsPath, cch);
        else
            rc = VERR_BUFFER_OVERFLOW;
        RTStrFree(pszUtf8AbsPath);
    }

    LogFlow(("RTPathAbs(%p:{%s}, %p:{%s}, %d): returns %Rrc\n", pszPath, pszPath,
             pszAbsPath, RT_SUCCESS(rc) ? pszAbsPath : "<failed>", cchAbsPath));
    return rc;
}


RTDECL(int) RTPathProgram(char *pszPath, unsigned cchPath)
{
    /*
     * First time only.
     */
    if (!g_szrtProgramPath[0])
    {
        /*
         * Linux have no API for obtaining the executable path, but provides a symbolic link
         * in the proc file system. Note that readlink is one of the weirdest Unix apis around.
         *
         * OS/2 have an api for getting the program file name.
         */
/** @todo use RTProcGetExecutableName() */
#ifdef __LINUX__
        int cchLink = readlink("/proc/self/exe", &g_szrtProgramPath[0], sizeof(g_szrtProgramPath) - 1);
        if (cchLink < 0 || cchLink == sizeof(g_szrtProgramPath) - 1)
        {
            int rc = RTErrConvertFromErrno(errno);
            AssertMsgFailed(("couldn't read /proc/self/exe. errno=%d cchLink=%d\n", errno, cchLink));
            LogFlow(("RTPathProgram(%p, %u): returns %Rrc\n", pszPath, cchPath, rc));
            return rc;
        }
        g_szrtProgramPath[cchLink] = '\0';

#elif defined(__OS2__) || defined(__L4__)
        _execname(g_szrtProgramPath, sizeof(g_szrtProgramPath));

#elif defined(__DARWIN__)
        const char *pszImageName = _dyld_get_image_name(0);
        AssertReturn(pszImageName, VERR_INTERNAL_ERROR);
        size_t cchImageName = strlen(pszImageName);
        if (cchImageName >= sizeof(g_szrtProgramPath))
            AssertReturn(pszImageName, VERR_INTERNAL_ERROR);
        memcpy(g_szrtProgramPath, pszImageName, cchImageName + 1);

#else
# error needs porting.
#endif

        /*
         * Convert to UTF-8 and strip of the filename.
         */
        char *pszTmp = NULL;
        int rc = rtPathFromNative(&pszTmp, &g_szrtProgramPath[0]);
        if (RT_FAILURE(rc))
        {
            LogFlow(("RTPathProgram(%p, %u): returns %Rrc\n", pszPath, cchPath, rc));
            return rc;
        }
        size_t cch = strlen(pszTmp);
        if (cch >= sizeof(g_szrtProgramPath))
        {
            RTStrFree(pszTmp);
            LogFlow(("RTPathProgram(%p, %u): returns %Rrc\n", pszPath, cchPath, VERR_BUFFER_OVERFLOW));
            return VERR_BUFFER_OVERFLOW;
        }
        memcpy(g_szrtProgramPath, pszTmp, cch + 1);
        RTPathStripFilename(g_szrtProgramPath);
        RTStrFree(pszTmp);
    }

    /*
     * Calc the length and check if there is space before copying.
     */
    unsigned cch = strlen(g_szrtProgramPath) + 1;
    if (cch <= cchPath)
    {
        memcpy(pszPath, g_szrtProgramPath, cch + 1);
        LogFlow(("RTPathProgram(%p:{%s}, %u): returns %Rrc\n", pszPath, pszPath, cchPath, VINF_SUCCESS));
        return VINF_SUCCESS;
    }

    AssertMsgFailed(("Buffer too small (%d < %d)\n", cchPath, cch));
    LogFlow(("RTPathProgram(%p, %u): returns %Rrc\n", pszPath, cchPath, VERR_BUFFER_OVERFLOW));
    return VERR_BUFFER_OVERFLOW;
}


RTDECL(int) RTPathUserHome(char *pszPath, unsigned cchPath)
{
    /*
     * Get HOME env. var it and validate it's existance.
     */
    int rc;
    struct stat s;
    const char *pszHome = getenv("HOME");
    if (pszHome)
    {
        if (    !stat(pszHome, &s)
            &&  S_ISDIR(s.st_mode))
        {
            /*
             * Convert it to UTF-8 and copy it to the return buffer.
             */
            char *pszUtf8Path;
            rc = rtPathFromNative(&pszUtf8Path, pszHome);
            if (RT_SUCCESS(rc))
            {
                size_t cchHome = strlen(pszUtf8Path);
                if (cchHome < cchPath)
                    memcpy(pszPath, pszUtf8Path, cchHome + 1);
                else
                    rc = VERR_BUFFER_OVERFLOW;
                RTStrFree(pszUtf8Path);
            }
        }
        else
            rc = VERR_PATH_NOT_FOUND;

    }
    else
        rc = VERR_PATH_NOT_FOUND;

    LogFlow(("RTPathUserHome(%p:{%s}, %u): returns %Rrc\n", pszPath,
             RT_SUCCESS(rc) ? pszPath : "<failed>",  cchPath, rc));
    return rc;
}


RTR3DECL(int) RTPathQueryInfo(const char *pszPath, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs)
{
    /*
     * Validate input.
     */
    AssertMsgReturn(VALID_PTR(pszPath), ("%p\n", pszPath), VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_PARAMETER);
    AssertMsgReturn(VALID_PTR(pObjInfo), ("%p\n", pszPath), VERR_INVALID_POINTER);
    AssertMsgReturn(    enmAdditionalAttribs >= RTFSOBJATTRADD_NOTHING
                    &&  enmAdditionalAttribs <= RTFSOBJATTRADD_LAST,
                    ("Invalid enmAdditionalAttribs=%p\n", enmAdditionalAttribs),
                    VERR_INVALID_PARAMETER);

    /*
     * Convert the filename.
     */
    char *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath);
    if (RT_SUCCESS(rc))
    {
        struct stat Stat;
        if (!stat(pszNativePath, &Stat))
        {
            rtFsConvertStatToObjInfo(pObjInfo, &Stat);
            switch (enmAdditionalAttribs)
            {
                case RTFSOBJATTRADD_EASIZE:
                    /** @todo Use SGI extended attribute interface to query EA info. */
                    pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_EASIZE;
                    pObjInfo->Attr.u.EASize.cb            = 0;
                    break;

                case RTFSOBJATTRADD_NOTHING:
                case RTFSOBJATTRADD_UNIX:
                    Assert(pObjInfo->Attr.enmAdditional == RTFSOBJATTRADD_UNIX);
                    break;

                default:
                    AssertMsgFailed(("Impossible!\n"));
                    return VERR_INTERNAL_ERROR;
            }
        }
        else
            rc = RTErrConvertFromErrno(errno);
    }

    LogFlow(("RTPathQueryInfo(%p:{%s}, pObjInfo=%p, %d): returns %Rrc\n",
             pszPath, pszPath, pObjInfo, enmAdditionalAttribs, rc));
    return rc;
}


RTR3DECL(int) RTPathSetTimes(const char *pszPath, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                             PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    /*
     * Validate input.
     */
    AssertMsgReturn(VALID_PTR(pszPath), ("%p\n", pszPath), VERR_INVALID_POINTER);
    AssertMsgReturn(*pszPath, ("%p\n", pszPath), VERR_INVALID_PARAMETER);
    AssertMsgReturn(!pAccessTime || VALID_PTR(pAccessTime), ("%p\n", pAccessTime), VERR_INVALID_POINTER);
    AssertMsgReturn(!pModificationTime || VALID_PTR(pModificationTime), ("%p\n", pModificationTime), VERR_INVALID_POINTER);
    AssertMsgReturn(!pChangeTime || VALID_PTR(pChangeTime), ("%p\n", pChangeTime), VERR_INVALID_POINTER);
    AssertMsgReturn(!pBirthTime || VALID_PTR(pBirthTime), ("%p\n", pBirthTime), VERR_INVALID_POINTER);

    /*
     * Convert the paths.
     */
    char *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath);
    if (RT_SUCCESS(rc))
    {
        /*
         * If it's a no-op, we'll only verify the existance of the file.
         */
        if (!pAccessTime && !pModificationTime)
        {
            struct stat Stat;
            if (!stat(pszNativePath, &Stat))
                rc = VINF_SUCCESS;
            else
            {
                rc = RTErrConvertFromErrno(errno);
                Log(("RTPathSetTimes('%s',,,,): failed with %Rrc and errno=%d\n", pszPath, rc, errno));
            }
        }
        else
        {
            /*
             * Convert the input to timeval, getting the missing one if necessary,
             * and call the API which does the change.
             */
            struct timeval aTimevals[2];
            if (pAccessTime && pModificationTime)
            {
                RTTimeSpecGetTimeval(pAccessTime,       &aTimevals[0]);
                RTTimeSpecGetTimeval(pModificationTime, &aTimevals[1]);
            }
            else
            {
                RTFSOBJINFO ObjInfo;
                int rc = RTPathQueryInfo(pszPath, &ObjInfo, RTFSOBJATTRADD_UNIX);
                if (RT_SUCCESS(rc))
                {
                    RTTimeSpecGetTimeval(pAccessTime        ? pAccessTime       : &ObjInfo.AccessTime,       &aTimevals[0]);
                    RTTimeSpecGetTimeval(pModificationTime  ? pModificationTime : &ObjInfo.ModificationTime, &aTimevals[1]);
                }
                else
                    Log(("RTPathSetTimes('%s',%p,%p,,): RTPathQueryInfo failed with %Rrc\n",
                         pszPath, pAccessTime, pModificationTime, rc));
            }
            if (RT_SUCCESS(rc))
            {
                if (utimes(pszNativePath, aTimevals))
                {
                    rc = RTErrConvertFromErrno(errno);
                    Log(("RTPathSetTimes('%s',%p,%p,,): failed with %Rrc and errno=%d\n",
                         pszPath, pAccessTime, pModificationTime, rc, errno));
                }
            }
        }
    }

    LogFlow(("RTPathSetTimes(%p:{%s}, %p:{%RDtimespec}, %p:{%RDtimespec}, %p:{%RDtimespec}, %p:{%RDtimespec}): return %Rrc\n",
             pszPath, pszPath, pAccessTime, pAccessTime, pModificationTime, pModificationTime,
             pChangeTime, pChangeTime, pBirthTime, pBirthTime));
    return rc;
}


/**
 * Checks if two files are the one and same file.
 */
static bool rtPathSame(const char *pszNativeSrc, const char *pszNativeDst)
{
    struct stat SrcStat;
    if (stat(pszNativeSrc, &SrcStat))
        return false;
    struct stat DstStat;
    if (stat(pszNativeDst, &DstStat))
        return false;
    Assert(SrcStat.st_dev && DstStat.st_dev);
    Assert(SrcStat.st_ino && DstStat.st_ino);
    if (    SrcStat.st_dev == DstStat.st_dev
        &&  SrcStat.st_ino == DstStat.st_ino
        &&  (SrcStat.st_mode & S_IFMT) == (DstStat.st_mode & S_IFMT))
        return true;
    return false;
}


/**
 * Worker for RTPathRename, RTDirRename, RTFileRename.
 *
 * @returns IPRT status code.
 * @param   pszSrc      The source path.
 * @param   pszDst      The destintation path.
 * @param   fRename     The rename flags.
 * @param   fFileType   The filetype. We use the RTFMODE filetypes here. If it's 0,
 *                      anything goes. If it's RTFS_TYPE_DIRECTORY we'll check that the
 *                      source is a directory. If Its RTFS_TYPE_FILE we'll check that it's
 *                      not a directory (we are NOT checking whether it's a file).
 */
int rtPathPosixRename(const char *pszSrc, const char *pszDst, unsigned fRename, RTFMODE fFileType)
{
    /*
     * Convert the paths.
     */
    char *pszNativeSrc;
    int rc = rtPathToNative(&pszNativeSrc, pszSrc);
    if (RT_SUCCESS(rc))
    {
        char *pszNativeDst;
        rc = rtPathToNative(&pszNativeDst, pszDst);
        if (RT_SUCCESS(rc))
        {
            /*
             * Check that the source exists and that any types that's specified matches.
             * We have to check this first to avoid getting errnous VERR_ALREADY_EXISTS
             * errors from the next step.
             *
             * There are race conditions here (perhaps unlikly ones but still), but I'm
             * afraid there is little with can do to fix that.
             */
            struct stat SrcStat;
            if (stat(pszNativeSrc, &SrcStat))
                rc = RTErrConvertFromErrno(errno);
            else if (!fFileType)
                rc = VINF_SUCCESS;
            else if (RTFS_IS_DIRECTORY(fFileType))
                rc = S_ISDIR(SrcStat.st_mode) ? VINF_SUCCESS : VERR_NOT_A_DIRECTORY;
            else
                rc = S_ISDIR(SrcStat.st_mode) ? VERR_IS_A_DIRECTORY : VINF_SUCCESS;
            if (RT_SUCCESS(rc))
            {
                bool fSameFile = false;

                /*
                 * Check if the target exists, rename is rather destructive.
                 * We'll have to make sure we don't overwrite the source!
                 * Another race condition btw.
                 */
                struct stat DstStat;
                if (stat(pszNativeDst, &DstStat))
                    rc = errno == ENOENT ? VINF_SUCCESS : RTErrConvertFromErrno(errno);
                else
                {
                    Assert(SrcStat.st_dev && DstStat.st_dev);
                    Assert(SrcStat.st_ino && DstStat.st_ino);
                    if (    SrcStat.st_dev == DstStat.st_dev
                        &&  SrcStat.st_ino == DstStat.st_ino
                        &&  (SrcStat.st_mode & S_IFMT) == (SrcStat.st_mode & S_IFMT))
                    {
                        /*
                         * It's likely that we're talking about the same file here.
                         * We should probably check paths or whatever, but for now this'll have to be enough.
                         */
                        fSameFile = true;
                    }
                    if (fSameFile)
                        rc = VINF_SUCCESS;
                    else if (S_ISDIR(DstStat.st_mode) || !(fRename & RTPATHRENAME_FLAGS_REPLACE))
                        rc = VERR_ALREADY_EXISTS;
                    else
                        rc = VINF_SUCCESS;

                }
                if (RT_SUCCESS(rc))
                {
                    if (!rename(pszNativeSrc, pszNativeDst))
                        rc = VINF_SUCCESS;
                    else if (   (fRename & RTPATHRENAME_FLAGS_REPLACE)
                             && (errno == ENOTDIR || errno == EEXIST))
                    {
                        /*
                         * Check that the destination isn't a directory.
                         * Yet another race condition.
                         */
                        if (rtPathSame(pszNativeSrc, pszNativeDst))
                        {
                            rc = VINF_SUCCESS;
                            Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): appears to be the same file... (errno=%d)\n",
                                 pszSrc, pszDst, fRename, fFileType, errno));
                        }
                        else
                        {
                            if (stat(pszNativeDst, &DstStat))
                                rc = errno != ENOENT ? RTErrConvertFromErrno(errno) : VINF_SUCCESS;
                            else if (S_ISDIR(DstStat.st_mode))
                                rc = VERR_ALREADY_EXISTS;
                            else
                                rc = VINF_SUCCESS;
                            if (RT_SUCCESS(rc))
                            {
                                if (!unlink(pszNativeDst))
                                {
                                    if (!rename(pszNativeSrc, pszNativeDst))
                                        rc = VINF_SUCCESS;
                                    else
                                    {
                                        rc = RTErrConvertFromErrno(errno);
                                        Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): rename failed rc=%Rrc errno=%d\n",
                                             pszSrc, pszDst, fRename, fFileType, rc, errno));
                                    }
                                }
                                else
                                {
                                    rc = RTErrConvertFromErrno(errno);
                                    Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): failed to unlink dst rc=%Rrc errno=%d\n",
                                         pszSrc, pszDst, fRename, fFileType, rc, errno));
                                }
                            }
                            else
                                Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): dst !dir check failed rc=%Rrc\n",
                                     pszSrc, pszDst, fRename, fFileType, rc));
                        }
                    }
                    else
                    {
                        rc = RTErrConvertFromErrno(errno);
                        if (errno == ENOTDIR)
                            rc = VERR_ALREADY_EXISTS; /* unless somebody is racing us, this is the right interpretation */
                        Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): rename failed rc=%Rrc errno=%d\n",
                             pszSrc, pszDst, fRename, fFileType, rc, errno));
                    }
                }
                else
                    Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): destination check failed rc=%Rrc errno=%d\n",
                         pszSrc, pszDst, fRename, fFileType, rc, errno));
            }
            else
                Log(("rtPathRename('%s', '%s', %#x ,%RTfmode): source type check failed rc=%Rrc errno=%d\n",
                     pszSrc, pszDst, fRename, fFileType, rc, errno));

            rtPathFreeNative(pszNativeDst);
        }
        rtPathFreeNative(pszNativeSrc);
    }
    return rc;
}


RTR3DECL(int) RTPathRename(const char *pszSrc, const char *pszDst, unsigned fRename)
{
    /*
     * Validate input.
     */
    AssertMsgReturn(VALID_PTR(pszSrc), ("%p\n", pszSrc), VERR_INVALID_POINTER);
    AssertMsgReturn(VALID_PTR(pszDst), ("%p\n", pszDst), VERR_INVALID_POINTER);
    AssertMsgReturn(*pszSrc, ("%p\n", pszSrc), VERR_INVALID_PARAMETER);
    AssertMsgReturn(*pszDst, ("%p\n", pszDst), VERR_INVALID_PARAMETER);
    AssertMsgReturn(!(fRename & ~RTPATHRENAME_FLAGS_REPLACE), ("%#x\n", fRename), VERR_INVALID_PARAMETER);

    /*
     * Hand it to the worker.
     */
    int rc = rtPathPosixRename(pszSrc, pszDst, fRename, 0);

    Log(("RTPathRename(%p:{%s}, %p:{%s}, %#x): returns %Rrc\n", pszSrc, pszSrc, pszDst, pszDst, fRename, rc));
    return rc;
}

