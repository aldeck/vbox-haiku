/** @file
 *
 * InnoTek Portable Runtime - Path Manipulation.
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

#ifndef __iprt_path_h__
#define __iprt_path_h__

#include <iprt/cdefs.h>
#include <iprt/types.h>
#ifdef IN_RING3
# include <iprt/fs.h>
#endif



__BEGIN_DECLS

/** @defgroup grp_rt_path   RTPath - Path Manipulation
 * @ingroup grp_rt
 * @{
 */


/** @def RTPATH_SLASH
 * The prefered slash character.
 *
 * @remark IPRT will always accept unix slashes. So, normally you would
 *         never have to use this define.
 */
#if defined(__OS2__) || defined(__WIN__)
# define RTPATH_SLASH       '\\'
#else
# define RTPATH_SLASH       '/'
#endif

/** @deprecated Use '/'! */
#define RTPATH_DELIMITER    RTPATH_SLASH


/** @def RTPATH_SLASH_STR
 * The prefered slash character as a string, handy for concatenations
 * with other strings.
 *
 * @remark IPRT will always accept unix slashes. So, normally you would
 *         never have to use this define.
 */
#if defined(__OS2__) || defined(__WIN__)
# define RTPATH_SLASH_STR   "\\"
#else
# define RTPATH_SLASH_STR   "/"
#endif


/** @def RTPATH_IS_SLASH
 * Checks if a character is a slash.
 *
 * @returns true if it's a slash and false if not.
 * @returns @param      ch      Char to check.
 */
#if defined(__OS2__) || defined(__WIN__)
# define RTPATH_IS_SLASH(ch)    ( (ch) == '\\' || (ch) == '/' )
#else
# define RTPATH_IS_SLASH(ch)    ( (ch) == '/' )
#endif


/** @def RTPATH_IS_VOLSEP
 * Checks if a character marks the end of the volume specification.
 *
 * @remark  This is sufficent for the drive letter consept on PC.
 *          However it might be insufficient on other platforms
 *          and even on PC a UNC volume spec won't be detected this way.
 *          Use the RTPath<too be created>() instead.
 *
 * @returns true if it is and false if it isn't.
 * @returns @param      ch      Char to check.
 */
#if defined(__OS2__) || defined(__WIN__)
# define RTPATH_IS_VOLSEP(ch)   ( (ch) == ':' )
#else
# define RTPATH_IS_VOLSEP(ch)   (false)
#endif


/** @def RTPATH_IS_SEP
 * Checks if a character is path component separator
 *
 * @returns true if it is and false if it isn't.
 * @returns @param      ch      Char to check.
 * @
 */
#define RTPATH_IS_SEP(ch)       ( RTPATH_IS_SLASH(ch) || RTPATH_IS_VOLSEP(ch) )


/**
 * Get the real path (no symlinks, no . or .. components), must exist.
 *
 * @returns iprt status code.
 * @param   pszPath         The path to resolve.
 * @param   pszRealPath     Where to store the real path.
 * @param   cchRealPath     Size of the buffer.
 */
RTDECL(int) RTPathReal(const char *pszPath, char *pszRealPath, unsigned cchRealPath);

/**
 * Same as RTPathReal only the result is RTStrDup()'ed.
 *
 * @returns Pointer to real path. Use RTStrFree() to free this string.
 * @returns NULL if RTPathReal() or RTStrDup() fails.
 * @param   pszPath         The path to resolve.
 */
RTDECL(char *) RTPathRealDup(const char *pszPath);

/**
 * Get the absolute path (no symlinks, no . or .. components), doesn't have to exist.
 *
 * @returns iprt status code.
 * @param   pszPath         The path to resolve.
 * @param   pszAbsPath      Where to store the absolute path.
 * @param   cchAbsPath      Size of the buffer.
 */
RTDECL(int) RTPathAbs(const char *pszPath, char *pszAbsPath, unsigned cchAbsPath);

/**
 * Same as RTPathAbs only the result is RTStrDup()'ed.
 *
 * @returns Pointer to the absolute path. Use RTStrFree() to free this string.
 * @returns NULL if RTPathAbs() or RTStrDup() fails.
 * @param   pszPath         The path to resolve.
 */
RTDECL(char *) RTPathAbsDup(const char *pszPath);

/**
 * Get the absolute path (no symlinks, no . or .. components), assuming the
 * given base path as the current directory. The resulting path doesn't have
 * to exist.
 *
 * @returns iprt status code.
 * @param   pszBase         The base path to act like a current directory.
 *                          When NULL, the actual cwd is used (i.e. the call
 *                          is equivalent to RTPathAbs(pszPath, ...).
 * @param   pszPath         The path to resolve.
 * @param   pszAbsPath      Where to store the absolute path.
 * @param   cchAbsPath      Size of the buffer.
 */
RTDECL(int) RTPathAbsEx(const char *pszBase, const char *pszPath, char *pszAbsPath, unsigned cchAbsPath);

/**
 * Same as RTPathAbsEx only the result is RTStrDup()'ed.
 *
 * @returns Pointer to the absolute path. Use RTStrFree() to free this string.
 * @returns NULL if RTPathAbsEx() or RTStrDup() fails.
 * @param   pszBase         The base path to act like a current directory.
 *                          When NULL, the actual cwd is used (i.e. the call
 *                          is equivalent to RTPathAbs(pszPath, ...).
 * @param   pszPath         The path to resolve.
 */
RTDECL(char *) RTPathAbsExDup(const char *pszBase, const char *pszPath);

/**
 * Strips the filename from a path.
 *
 * @param   pszPath     Path which filename should be stripped.
 *                      If only filename in the string a '.' will be returned.
 */
RTDECL(void) RTPathStripFilename(char *pszPath);

/**
 * Strips the extension from a path.
 *
 * @param   pszPath     Path which extension should be stripped.
 */
RTDECL(void) RTPathStripExt(char *pszPath);

/**
 * Strips the trailing slashes of a path name.
 *
 * @param   pszPath     Path to strip.
 */
RTDECL(void) RTPathStripTrailingSlash(char *pszPath);

/**
 * Finds the filename in a path.
 *
 * @returns Pointer to filename within pszPath.
 * @returns NULL if no filename (i.e. empty string or ends with a slash).
 * @param   pszPath     Path to find filename in.
 */
RTDECL(char *) RTPathFilename(const char *pszPath);

/**
 * Finds the extension part of in a path.
 *
 * @returns Pointer to extension within pszPath.
 * @returns NULL if no extension.
 * @param   pszPath     Path to find extension in.
 */
RTDECL(char *) RTPathExt(const char *pszPath);

/**
 * Checks if a path have an extension.
 *
 * @returns true if extension present.
 * @returns false if no extension.
 * @param   pszPath     Path to check.
 */
RTDECL(bool) RTPathHaveExt(const char *pszPath);

/**
 * Checks if a path includes more than a filename.
 *
 * @returns true if path present.
 * @returns false if no path.
 * @param   pszPath     Path to check.
 */
RTDECL(bool) RTPathHavePath(const char *pszPath);

/**
 * Compares two paths.
 *
 * The comparison takes platform-dependent details into account,
 * such as:
 * <ul>
 * <li>On DOS-like platforms, both |\| and |/| separator chars are considered
 *     to be equal.
 * <li>On platforms with case-insensitive file systems, mismatching characters
 *     are uppercased and compared again.
 * </ul>
 *
 * File system details are currently ignored. This means that you won't get
 * case-insentive compares on unix systems when a path goes into a case-insensitive
 * filesystem like FAT, HPFS, HFS, NTFS, JFS, or similar. For NT, OS/2 and similar
 * you'll won't get case-sensitve compares on a case-sensitive file system.
 *
 * @param   pszPath1    Path to compare (must be an absolute path).
 * @param   pszPath2    Path to compare (must be an absolute path).
 *
 * @returns < 0 if the first path less than the second path.
 * @returns 0 if the first path identical to the second path.
 * @returns > 0 if the first path greater than the second path.
 */
RTDECL(int) RTPathCompare(const char *pszPath1, const char *pszPath2);

/**
 * Checks if a path starts with the given parent path.
 *
 * This means that either the path and the parent path matches completely, or that
 * the path is to some file or directory residing in the tree given by the parent
 * directory.
 *
 * The path comparison takes platform-dependent details into account,
 * see RTPathCompare() for details.
 *
 * @param   pszPath         Path to check, must be an absolute path.
 * @param   pszParentPath   Parent path, must be an absolute path.
 *                          No trailing directory slash!
 *
 * @returns |true| when \a pszPath starts with \a pszParentPath (or when they
 *          are identical), or |false| otherwise.
 *
 * @remark  This API doesn't currently handle root directory compares in a manner
 *          consistant with the other APIs. RTPathStartsWith(pszSomePath, "/") will
 *          not work if pszSomePath isn't "/".
 */
RTDECL(bool) RTPathStartsWith(const char *pszPath, const char *pszParentPath);


#ifdef IN_RING3

/**
 * Gets the program path.
 *
 * @returns iprt status code.
 * @param   pszPath     Buffer where to store the path.
 * @param   cchPath     Buffer size in bytes.
 */
RTDECL(int) RTPathProgram(char *pszPath, unsigned cchPath);

/**
 * Gets the user home directory.
 *
 * @returns iprt status code.
 * @param   pszPath     Buffer where to store the path.
 * @param   cchPath     Buffer size in bytes.
 */
RTDECL(int) RTPathUserHome(char *pszPath, unsigned cchPath);

/**
 * Query information about a file system object.
 *
 * This API will not resolve symbolic links in the last component (just
 * like unix lstat()).
 *
 * @returns VINF_SUCCESS if the object exists, information returned.
 * @returns VERR_PATH_NOT_FOUND if any but the last component in the specified
 *          path was not found or was not a directory.
 * @returns VERR_FILE_NOT_FOUND if the object does not exist (but path to the
 *          parent directory exists).
 * @returns some other iprt status code.
 *
 * @param   pszPath     Path to the file system object.
 * @param   pObjInfo    Object information structure to be filled on successful return.
 * @param   enmAdditionalAttribs
 *                      Which set of additional attributes to request.
 *                      Use RTFSOBJATTRADD_NOTHING if this doesn't matter.
 */
RTR3DECL(int) RTPathQueryInfo(const char *pszPath, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs);

/**
 * Changes the mode flags of a file system object.
 *
 * The API requires at least one of the mode flag sets (Unix/Dos) to
 * be set. The type is ignored.
 *
 * This API will resolve symbolic links in the last component since
 * mode isn't important for symbolic links.
 *
 * @returns iprt status code.
 * @param   pszPath     Path to the file system object.
 * @param   fMode       The new file mode, see @ref grp_rt_fs for details.
 */
RTR3DECL(int) RTPathSetMode(const char *pszPath, RTFMODE fMode);

/**
 * Gets the mode flags of a file system object.
 *
 * @returns iprt status code.
 * @param   pszPath     Path to the file system object.
 * @param   pfMode      Where to store the file mode, see @ref grp_rt_fs for details.
 *
 * @remark  This is wrapper around RTPathReal() + RTPathQueryInfo()
 *          and exists to complement RTPathSetMode().
 */
RTR3DECL(int) RTPathGetMode(const char *pszPath, PRTFMODE pfMode);

/**
 * Changes one or more of the timestamps associated of file system object.
 *
 * This API will not resolve symbolic links in the last component (just
 * like unix lutimes()).
 *
 * @returns iprt status code.
 * @param   pszPath             Path to the file system object.
 * @param   pAccessTime         Pointer to the new access time.
 * @param   pModificationTime   Pointer to the new modifcation time.
 * @param   pChangeTime         Pointer to the new change time. NULL if not to be changed.
 * @param   pBirthTime          Pointer to the new time of birth. NULL if not to be changed.
 *
 * @remark  The file system might not implement all these time attributes,
 *          the API will ignore the ones which aren't supported.
 *
 * @remark  The file system might not implement the time resolution
 *          employed by this interface, the time will be chopped to fit.
 *
 * @remark  The file system may update the change time even if it's
 *          not specified.
 *
 * @remark  POSIX can only set Access & Modification and will always set both.
 */
RTR3DECL(int) RTPathSetTimes(const char *pszPath, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                             PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime);

/**
 * Gets one or more of the timestamps associated of file system object.
 *
 * @returns iprt status code.
 * @param   pszPath             Path to the file system object.
 * @param   pAccessTime         Where to store the access time. NULL is ok.
 * @param   pModificationTime   Where to store the modifcation time. NULL is ok.
 * @param   pChangeTime         Where to store the change time. NULL is ok.
 * @param   pBirthTime          Where to store the creation time. NULL is ok.
 *
 * @remark  This is wrapper around RTPathQueryInfo() and exists to complement RTPathSetTimes().
 */
RTR3DECL(int) RTPathGetTimes(const char *pszPath, PRTTIMESPEC pAccessTime, PRTTIMESPEC pModificationTime,
                             PRTTIMESPEC pChangeTime, PRTTIMESPEC pBirthTime);

/**
 * Changes the owner and/or group of a file system object.
 *
 * This API will not resolve symbolic links in the last component (just
 * like unix lchown()).
 *
 * @returns iprt status code.
 * @param   pszPath     Path to the file system object.
 * @param   uid         The new file owner user id. Use -1 (or ~0) to leave this unchanged.
 * @param   gid         The new group id. Use -1 (or ~0) to leave this unchanged.
 */
RTR3DECL(int) RTPathSetOwner(const char *pszPath, uint32_t uid, uint32_t gid);

/**
 * Gets the owner and/or group of a file system object.
 *
 * @returns iprt status code.
 * @param   pszPath     Path to the file system object.
 * @param   pUid        Where to store the owner user id. NULL is ok.
 * @param   pGid        Where to store the group id. NULL is ok.
 *
 * @remark  This is wrapper around RTPathQueryInfo() and exists to complement RTPathGetOwner().
 */
RTR3DECL(int) RTPathGetOwner(const char *pszPath, uint32_t *pUid, uint32_t *pGid);


/** @name RTPathRename, RTDirRename & RTFileRename flags.
 * @{ */
/** This will replace attempt any target which isn't a directory. */
#define RTPATHRENAME_FLAGS_REPLACE      BIT(0)
/** @} */

/**
 * Renames a path within a filesystem.
 *
 * @returns IPRT status code.
 * @param   pszSrc      The source path.
 * @param   pszDst      The destination path.
 * @param   fRename     Rename flags, RTPATHRENAME_FLAGS_*.
 */
RTR3DECL(int) RTPathRename(const char *pszSrc,  const char *pszDst, unsigned fRename);

#endif /* IN_RING3 */

/** @} */

__END_DECLS

#endif

