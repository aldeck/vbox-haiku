/** @file
 * innotek Portable Runtime - File I/O.
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___iprt_file_h
#define ___iprt_file_h

#include <iprt/cdefs.h>
#include <iprt/types.h>
#ifdef IN_RING3
# include <iprt/fs.h>
#endif

/** @todo rename this file to iprt/file.h */

__BEGIN_DECLS

/** @defgroup grp_rt_fileio     RTFile - File I/O
 * @ingroup grp_rt
 * @{
 */

/** Platform specific text line break.
 * @deprecated Use text I/O streams and '\\n'. See iprt/stream.h. */
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
# define RTFILE_LINEFEED    "\r\n"
#else
# define RTFILE_LINEFEED    "\n"
#endif


#ifdef IN_RING3

/** @name Open flags
 * @{ */
/** Open the file with read access. */
#define RTFILE_O_READ               0x00000001
/** Open the file with write access. */
#define RTFILE_O_WRITE              0x00000002
/** Open the file with read & write access. */
#define RTFILE_O_READWRITE          0x00000003
/** The file access mask.
 * @remark The value 0 is invalid. */
#define RTFILE_O_ACCESS_MASK        0x00000003

/** Sharing mode: deny none (the default mode). */
#define RTFILE_O_DENY_NONE          0x00000000
/** Sharing mode: deny read. */
#define RTFILE_O_DENY_READ          0x00000010
/** Sharing mode: deny write. */
#define RTFILE_O_DENY_WRITE         0x00000020
/** Sharing mode: deny read and write. */
#define RTFILE_O_DENY_READWRITE     0x00000030
/** Sharing mode: deny all. */
#define RTFILE_O_DENY_ALL           RTFILE_O_DENY_READWRITE
/** Sharing mode: do NOT deny delete (NT).
 * @remark  This might not be implemented on all platforms,
 *          and will be defaulted & ignored on those.
 */
#define RTFILE_O_DENY_NOT_DELETE    0x00000040
/** Sharing mode mask. */
#define RTFILE_O_DENY_MASK          0x00000070

/** Action: Open an existing file (the default action). */
#define RTFILE_O_OPEN               0x00000000
/** Action: Create a new file or open an existing one. */
#define RTFILE_O_OPEN_CREATE        0x00000100
/** Action: Create a new a file. */
#define RTFILE_O_CREATE             0x00000200
/** Action: Create a new file or replace an existing one. */
#define RTFILE_O_CREATE_REPLACE     0x00000300
/** Action mask. */
#define RTFILE_O_ACTION_MASK        0x00000300

/** Turns off indexing of files on Windows hosts, *CREATE* only.
 * @remark  This might not be implemented on all platforms,
 *          and will be ignored on those.
 */
#define RTFILE_O_NOT_CONTENT_INDEXED 0x00000800
/** Truncate the file.
 * @remark  This will not truncate files opened for read-only.
 * @remark  The trunction doesn't have to be atomically, so anyone
 *          else opening the file may be racing us. The caller is
 *          responsible for not causing this race. */
#define RTFILE_O_TRUNCATE           0x00001000
/** Make the handle inheritable on RTProcessCreate(/exec). */
#define RTFILE_O_INHERIT            0x00002000
/** Open file in non-blocking mode - non-portable.
 * @remark  This flag may not be supported on all platforms, in which
 *          case it's considered an invalid parameter.
 */
#define RTFILE_O_NON_BLOCK          0x00004000
/** Write through directly to disk. Workaround to avoid iSCSI
 * initiator deadlocks on Windows hosts.
 * @remark  This might not be implemented on all platforms,
 *          and will be ignored on those.
 */
#define RTFILE_O_WRITE_THROUGH      0x00008000
/** Mask of all valid flags.
 * @remark  This doesn't validate the access mode properly.
 */
#define RTFILE_O_VALID_MASK         0x0000FB73

/** @} */


/**
 * Force the use of open flags for all files opened after the setting is
 * changed. The caller is responsible for not causing races with RTFileOpen().
 *
 * @returns iprt status code.
 * @param   fOpenForAccess  Access mode to which the set/mask settings apply.
 * @param   fSet            Open flags to be forced set.
 * @param   fMask           Open flags to be masked out.
 */
RTR3DECL(int)  RTFileSetForceFlags(unsigned fOpenForAccess, unsigned fSet, unsigned fMask);

/**
 * Open a file.
 *
 * @returns iprt status code.
 * @param   pFile           Where to store the handle to the opened file.
 * @param   pszFilename     Path to the file which is to be opened. (UTF-8)
 * @param   fOpen           Open flags, i.e a combination of the RTFILE_O_* defines.
 */
RTR3DECL(int)  RTFileOpen(PRTFILE pFile, const char *pszFilename, unsigned fOpen);

/**
 * Close a file opened by RTFileOpen().
 *
 * @returns iprt status code.
 * @param   File            The file handle to close.
 */
RTR3DECL(int)  RTFileClose(RTFILE File);

/**
 * Delete a file.
 *
 * @returns iprt status code.
 * @param   pszFilename     Path to the file which is to be deleted. (UTF-8)
 * @todo    This is a RTPath api!
 */
RTR3DECL(int)  RTFileDelete(const char *pszFilename);

/** @name Seek flags.
 * @{ */
/** Seek from the start of the file. */
#define RTFILE_SEEK_BEGIN     0x00
/** Seek from the current file position. */
#define RTFILE_SEEK_CURRENT   0x01
/** Seek from the end of the file. */
#define RTFILE_SEEK_END       0x02
/** @internal */
#define RTFILE_SEEK_FIRST     RTFILE_SEEK_BEGIN
/** @internal */
#define RTFILE_SEEK_LAST      RTFILE_SEEK_END
/** @} */


/**
 * Changes the read & write position in a file.
 *
 * @returns iprt status code.
 * @param   File        Handle to the file.
 * @param   offSeek     Offset to seek.
 * @param   uMethod     Seek method, i.e. one of the RTFILE_SEEK_* defines.
 * @param   poffActual  Where to store the new file position.
 *                      NULL is allowed.
 */
RTR3DECL(int)  RTFileSeek(RTFILE File, int64_t offSeek, unsigned uMethod, uint64_t *poffActual);

/**
 * Read bytes from a file.
 *
 * @returns iprt status code.
 * @param   File        Handle to the file.
 * @param   pvBuf       Where to put the bytes we read.
 * @param   cbToRead    How much to read.
 * @param   *pcbRead    How much we actually read .
 *                      If NULL an error will be returned for a partial read.
 */
RTR3DECL(int)  RTFileRead(RTFILE File, void *pvBuf, size_t cbToRead, size_t *pcbRead);

/**
 * Read bytes from a file at a given offset.
 * This function may modify the file position.
 *
 * @returns iprt status code.
 * @param   File        Handle to the file.
 * @param   off         Where to read.
 * @param   pvBuf       Where to put the bytes we read.
 * @param   cbToRead    How much to read.
 * @param   *pcbRead    How much we actually read .
 *                      If NULL an error will be returned for a partial read.
 */
RTR3DECL(int)  RTFileReadAt(RTFILE File, RTFOFF off, void *pvBuf, size_t cbToRead, size_t *pcbRead);

/**
 * Write bytes to a file.
 *
 * @returns iprt status code.
 * @param   File        Handle to the file.
 * @param   pvBuf       What to write.
 * @param   cbToWrite   How much to write.
 * @param   *pcbWritten How much we actually wrote.
 *                      If NULL an error will be returned for a partial write.
 */
RTR3DECL(int)  RTFileWrite(RTFILE File, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten);

/**
 * Write bytes to a file at a given offset.
 * This function may modify the file position.
 *
 * @returns iprt status code.
 * @param   File        Handle to the file.
 * @param   off         Where to write.
 * @param   pvBuf       What to write.
 * @param   cbToWrite   How much to write.
 * @param   *pcbWritten How much we actually wrote.
 *                      If NULL an error will be returned for a partial write.
 */
RTR3DECL(int)  RTFileWriteAt(RTFILE File, RTFOFF off, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten);

/**
 * Flushes the buffers for the specified file.
 *
 * @returns iprt status code.
 * @param   File        Handle to the file.
 */
RTR3DECL(int)  RTFileFlush(RTFILE File);

/**
 * Set the size of the file.
 *
 * @returns iprt status code.
 * @param   File        Handle to the file.
 * @param   cbSize      The new file size.
 */
RTR3DECL(int)  RTFileSetSize(RTFILE File, uint64_t cbSize);

/**
 * Query the size of the file.
 *
 * @returns iprt status code.
 * @param   File        Handle to the file.
 * @param   pcbSize     Where to store the filesize.
 */
RTR3DECL(int)  RTFileGetSize(RTFILE File, uint64_t *pcbSize);

/**
 * Determine the maximum file size.
 *
 * @returns The max size of the file.
 *          -1 on failure, the file position is undefined.
 * @param   File        Handle to the file.
 * @see     RTFileGetMaxSizeEx.
 */
RTR3DECL(RTFOFF) RTFileGetMaxSize(RTFILE File);

/**
 * Determine the maximum file size.
 *
 * @returns IPRT status code.
 * @param   File        Handle to the file.
 * @param   pcbMax      Where to store the max file size.
 * @see     RTFileGetMaxSize.
 */
RTR3DECL(int) RTFileGetMaxSizeEx(RTFILE File, PRTFOFF pcbMax);

/**
 * Determine the maximum file size depending on the file system the file is stored on.
 *
 * @returns The max size of the file.
 *          -1 on failure.
 * @param   File        Handle to the file.
 */
RTR3DECL(RTFOFF) RTFileGetMaxSize(RTFILE File);

/**
 * Gets the current file position.
 *
 * @returns File offset.
 * @returns ~0UUL on failure.
 * @param   File        Handle to the file.
 */
RTDECL(uint64_t)  RTFileTell(RTFILE File);

/**
 * Checks if the supplied handle is valid.
 *
 * @returns true if valid.
 * @returns false if invalid.
 * @param   File        The file handle
 */
RTR3DECL(bool) RTFileIsValid(RTFILE File);

/**
 * Copies a file.
 *
 * @returns VERR_ALREADY_EXISTS if the destination file exists.
 * @returns VBox Status code.
 *
 * @param   pszSrc      The path to the source file.
 * @param   pszDst      The path to the destination file.
 *                      This file will be created.
 */
RTDECL(int) RTFileCopy(const char *pszSrc, const char *pszDst);

/**
 * Copies a file given the handles to both files.
 *
 * @returns VBox Status code.
 *
 * @param   FileSrc     The source file. The file position is unaltered.
 * @param   FileDst     The destination file.
 *                      On successful returns the file position is at the end of the file.
 *                      On failures the file position and size is undefined.
 */
RTDECL(int) RTFileCopyByHandles(RTFILE FileSrc, RTFILE FileDst);

/**
 * Copies a file.
 *
 * @returns VERR_ALREADY_EXISTS if the destination file exists.
 * @returns VBox Status code.
 *
 * @param   pszSrc      The path to the source file.
 * @param   pszDst      The path to the destination file.
 *                      This file will be created.
 * @param   pfnProgress Pointer to callback function for reporting progress.
 * @param   pvUser      User argument to pass to pfnProgress along with the completion precentage.
 */
RTDECL(int) RTFileCopyEx(const char *pszSrc, const char *pszDst, PFNRTPROGRESS pfnProgress, void *pvUser);

/**
 * Copies a file given the handles to both files and
 * provide progress callbacks.
 *
 * @returns IPRT status code.
 *
 * @param   FileSrc     The source file. The file position is unaltered.
 * @param   FileDst     The destination file.
 *                      On successful returns the file position is at the end of the file.
 *                      On failures the file position and size is undefined.
 * @param   pfnProgress Pointer to callback function for reporting progress.
 * @param   pvUser      User argument to pass to pfnProgress along with the completion precentage.
 */
RTDECL(int) RTFileCopyByHandlesEx(RTFILE FileSrc, RTFILE FileDst, PFNRTPROGRESS pfnProgress, void *pvUser);

/**
 * Renames a file.
 *
 * Identical to RTPathRename except that it will ensure that the source is not a directory.
 *
 * @returns IPRT status code.
 * @returns VERR_ALREADY_EXISTS if the destination file exists.
 *
 * @param   pszSrc      The path to the source file.
 * @param   pszDst      The path to the destination file.
 *                      This file will be created.
 * @param   fRename     See RTPathRename.
 */
RTDECL(int) RTFileRename(const char *pszSrc, const char *pszDst, unsigned fRename);


/** @name RTFileMove flags (bit masks).
 * @{ */
/** Replace destination file if present. */
#define RTFILEMOVE_FLAGS_REPLACE    0x1
/** @} */

/**
 * Moves a file.
 *
 * RTFileMove differs from RTFileRename in that it works across volumes.
 *
 * @returns IPRT status code.
 * @returns VERR_ALREADY_EXISTS if the destination file exists.
 *
 * @param   pszSrc      The path to the source file.
 * @param   pszDst      The path to the destination file.
 *                      This file will be created.
 * @param   fMove       A combination of the RTFILEMOVE_* flags.
 */
RTDECL(int) RTFileMove(const char *pszSrc, const char *pszDst, unsigned fMove);


/** @page   pg_rt_filelock      RT File locking API description
 *
 * File locking general rules:
 *
 * Region to lock or unlock can be located beyond the end of file, this can be used for
 * growing files.
 * Read (or Shared) locks can be acquired held by an unlimited number of processes at the
 * same time, but a Write (or Exclusive) lock can only be acquired by one process, and
 * cannot coexist with a Shared lock. To acquire a Read lock, a process must wait until
 * there are no processes holding any Write locks. To acquire a Write lock, a process must
 * wait until there are no processes holding either kind of lock.
 * By default, RTFileLock and RTFileChangeLock calls returns error immediately if the lock
 * can't be acquired due to conflict with other locks, however they can be called in wait mode.
 *
 * Differences in implementation:
 *
 * Win32, OS/2: Locking is mandatory, since locks are enforced by the operating system.
 * I.e. when file region is locked in Read mode, any write in it will fail; in case of Write
 * lock - region can be readed and writed only by lock's owner.
 *
 * Win32: File size change (RTFileSetSize) is not controlled by locking at all (!) in the
 * operation system. Also see comments to RTFileChangeLock API call.
 *
 * Linux/Posix: By default locks in Unixes are advisory. This means that cooperating processes
 * may use locks to coordonate access to a file between themselves, but programs are also free
 * to ignore locks and access the file in any way they choose to.
 *
 * Additional reading:
 *     http://en.wikipedia.org/wiki/File_locking
 *     http://unixhelp.ed.ac.uk/CGI/man-cgi?fcntl+2
 *     http://msdn.microsoft.com/library/default.asp?url=/library/en-us/fileio/fs/lockfileex.asp
 */

/** @name Lock flags (bit masks).
 * @{ */
/** Read access, can be shared with others. */
#define RTFILE_LOCK_READ            0x00
/** Write access, one at a time. */
#define RTFILE_LOCK_WRITE           0x01
/** Don't wait for other locks to be released. */
#define RTFILE_LOCK_IMMEDIATELY     0x00
/** Wait till conflicting locks have been released. */
#define RTFILE_LOCK_WAIT            0x02
/** Valid flags mask */
#define RTFILE_LOCK_MASK            0x03
/** @} */


/**
 * Locks a region of file for read (shared) or write (exclusive) access.
 *
 * @returns iprt status code.
 * @returns VERR_FILE_LOCK_VIOLATION if lock can't be acquired.
 * @param   File        Handle to the file.
 * @param   fLock       Lock method and flags, see RTFILE_LOCK_* defines.
 * @param   offLock     Offset of lock start.
 * @param   cbLock      Length of region to lock, may overlap the end of file.
 */
RTR3DECL(int)  RTFileLock(RTFILE File, unsigned fLock, int64_t offLock, uint64_t cbLock);

/**
 * Changes a lock type from read to write or from write to read.
 * The region to type change must correspond exactly to an existing locked region.
 * If change can't be done due to locking conflict and non-blocking mode is used, error is
 * returned and lock keeps its state (see next warning).
 *
 * WARNING: win32 implementation of this call is not atomic, it transforms to a pair of
 * calls RTFileUnlock and RTFileLock. Potentially the previously acquired lock can be
 * lost, i.e. function is called in non-blocking mode, previous lock is freed, new lock can't
 * be acquired, and old lock (previous state) can't be acquired back too. This situation
 * may occurs _only_ if the other process is acquiring a _write_ lock in blocking mode or
 * in race condition with the current call.
 * In this very bad case special error code VERR_FILE_LOCK_LOST will be returned.
 *
 * @returns iprt status code.
 * @returns VERR_FILE_NOT_LOCKED if region was not locked.
 * @returns VERR_FILE_LOCK_VIOLATION if lock type can't be changed, lock remains its type.
 * @returns VERR_FILE_LOCK_LOST if lock was lost, we haven't this lock anymore :(
 * @param   File        Handle to the file.
 * @param   fLock       Lock method and flags, see RTFILE_LOCK_* defines.
 * @param   offLock     Offset of lock start.
 * @param   cbLock      Length of region to lock, may overlap the end of file.
 */
RTR3DECL(int)  RTFileChangeLock(RTFILE File, unsigned fLock, int64_t offLock, uint64_t cbLock);

/**
 * Unlocks previously locked region of file.
 * The region to unlock must correspond exactly to an existing locked region.
 *
 * @returns iprt status code.
 * @returns VERR_FILE_NOT_LOCKED if region was not locked.
 * @param   File        Handle to the file.
 * @param   offLock     Offset of lock start.
 * @param   cbLock      Length of region to unlock, may overlap the end of file.
 */
RTR3DECL(int)  RTFileUnlock(RTFILE File, int64_t offLock, uint64_t cbLock);


/**
 * Query information about an open file.
 *
 * @returns iprt status code.
 *
 * @param   File                    Handle to the file.
 * @param   pObjInfo                Object information structure to be filled on successful return.
 * @param   enmAdditionalAttribs    Which set of additional attributes to request.
 *                                  Use RTFSOBJATTRADD_NOTHING if this doesn't matter.
 */
RTR3DECL(int) RTFileQueryInfo(RTFILE File, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs);

/**
 * Changes one or more of the timestamps associated of file system object.
 *
 * @returns iprt status code.
 * @returns VERR_NOT_SUPPORTED is returned if the operation isn't supported by the OS.
 *
 * @param   File                Handle to the file.
 * @param   pAccessTime         Pointer to the new access time. NULL if not to be changed.
 * @param   pModificationTime   Pointer to the new modifcation time. NULL if not to be changed.
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
RTR3DECL(int) RTFileSetTimes(RTFILE File, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                             PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime);

/**
 * Gets one or more of the timestamps associated of file system object.
 *
 * @returns iprt status code.
 * @param   File                Handle to the file.
 * @param   pAccessTime         Where to store the access time. NULL is ok.
 * @param   pModificationTime   Where to store the modifcation time. NULL is ok.
 * @param   pChangeTime         Where to store the change time. NULL is ok.
 * @param   pBirthTime          Where to store the time of birth. NULL is ok.
 *
 * @remark  This is wrapper around RTFileQueryInfo() and exists to complement RTFileSetTimes().
 */
RTR3DECL(int) RTFileGetTimes(RTFILE File, PRTTIMESPEC pAccessTime, PRTTIMESPEC pModificationTime,
                             PRTTIMESPEC pChangeTime, PRTTIMESPEC pBirthTime);

/**
 * Changes the mode flags of an open file.
 *
 * The API requires at least one of the mode flag sets (Unix/Dos) to
 * be set. The type is ignored.
 *
 * @returns iprt status code.
 * @param   File        Handle to the file.
 * @param   fMode       The new file mode, see @ref grp_rt_fs for details.
 */
RTR3DECL(int) RTFileSetMode(RTFILE File, RTFMODE fMode);

/**
 * Gets the mode flags of an open file.
 *
 * @returns iprt status code.
 * @param   File        Handle to the file.
 * @param   pfMode      Where to store the file mode, see @ref grp_rt_fs for details.
 *
 * @remark  This is wrapper around RTFileQueryInfo()
 *          and exists to complement RTFileSetMode().
 */
RTR3DECL(int) RTFileGetMode(RTFILE File, uint32_t *pfMode);

/**
 * Changes the owner and/or group of an open file.
 *
 * @returns iprt status code.
 * @param   File        Handle to the file.
 * @param   uid         The new file owner user id. Use -1 (or ~0) to leave this unchanged.
 * @param   gid         The new group id. Use -1 (or ~0) to leave this unchanged.
 */
RTR3DECL(int) RTFileSetOwner(RTFILE File, uint32_t uid, uint32_t gid);

/**
 * Gets the owner and/or group of an open file.
 *
 * @returns iprt status code.
 * @param   File        Handle to the file.
 * @param   pUid        Where to store the owner user id. NULL is ok.
 * @param   pGid        Where to store the group id. NULL is ok.
 *
 * @remark  This is wrapper around RTFileQueryInfo() and exists to complement RTFileGetOwner().
 */
RTR3DECL(int) RTFileGetOwner(RTFILE File, uint32_t *pUid, uint32_t *pGid);

/**
 * Executes an IOCTL on a file descriptor.
 *
 * This function is currently only available in L4 and posix environments.
 * Attemps at calling it from code shared with any other platforms will break things!
 *
 * The rational for defining this API is to simplify L4 porting of audio drivers,
 * and to remove some of the assumptions on RTFILE being a file descriptor on
 * platforms using the posix file implementation.
 *
 * @returns iprt status code.
 * @param   File        Handle to the file.
 * @param   iRequest    IOCTL request to carry out.
 * @param   pvData      IOCTL data.
 * @param   cbData      Size of the IOCTL data.
 * @param   piRet       Return value of the IOCTL request.
 */
RTR3DECL(int) RTFileIoCtl(RTFILE File, int iRequest, void *pvData, unsigned cbData, int *piRet);

#endif /* IN_RING3 */

/** @} */

__END_DECLS

#endif

