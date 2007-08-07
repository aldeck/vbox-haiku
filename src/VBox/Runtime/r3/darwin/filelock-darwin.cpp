/* $Id$ */
/** @file
 * innotek Portable Runtime - File Locking, POSIX.
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
#define LOG_GROUP RTLOGGROUP_FILE

#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

#include <iprt/file.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include "internal/file.h"
#include "internal/fs.h"




RTR3DECL(int)  RTFileLock(RTFILE File, unsigned fLock, int64_t offLock, uint64_t cbLock)
{
    Assert(offLock >= 0);

    /* Check arguments. */
    if (fLock & ~RTFILE_LOCK_MASK)
    {
        AssertMsgFailed(("Invalid fLock=%08X\n", fLock));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Validate offset.
     */
    if (    sizeof(off_t) < sizeof(cbLock)
        &&  (    (offLock >> 32) != 0
             ||  (cbLock >> 32) != 0
             ||  ((offLock + cbLock) >> 32) != 0))
    {
        AssertMsgFailed(("64-bit file i/o not supported! offLock=%lld cbLock=%lld\n", offLock, cbLock));
        return VERR_NOT_SUPPORTED;
    }

    /* Prepare flock structure. */
    struct flock fl;
    Assert(RTFILE_LOCK_WRITE);
    fl.l_type   = (fLock & RTFILE_LOCK_WRITE) ? F_WRLCK : F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = (off_t)offLock;
    fl.l_len    = (off_t)cbLock;
    fl.l_pid    = 0;

    Assert(RTFILE_LOCK_WAIT);
    if (fcntl(File, (fLock & RTFILE_LOCK_WAIT) ? F_SETLKW : F_SETLK, &fl) >= 0)
        return VINF_SUCCESS;
    int iErr = errno;
    if (iErr == ENOTSUP)
    {
        /*
         * This is really bad hack for getting VDIs to work somewhat
         * safely on SMB mounts.
         */
        /** @todo we need to keep track of these locks really. Anyone requiring to lock more
         * than one part of a file will have to fix this. */
        unsigned f = 0;
        Assert(RTFILE_LOCK_WAIT);
        if (fLock & RTFILE_LOCK_WAIT)
            f |= LOCK_NB;
        if (fLock & RTFILE_LOCK_WRITE)
            f |= LOCK_EX;
        else
            f |= LOCK_SH;
        if (!flock(File, f))
            return VINF_SUCCESS;
        iErr = errno;
        if (iErr == EWOULDBLOCK)
            return VERR_FILE_LOCK_VIOLATION;
    }

    if (    iErr == EAGAIN
        ||  iErr == EACCES)
        return VERR_FILE_LOCK_VIOLATION;

    return RTErrConvertFromErrno(iErr);
}


RTR3DECL(int)  RTFileChangeLock(RTFILE File, unsigned fLock, int64_t offLock, uint64_t cbLock)
{
    /** @todo We never returns VERR_FILE_NOT_LOCKED for now. */
    return RTFileLock(File, fLock, offLock, cbLock);
}


RTR3DECL(int)  RTFileUnlock(RTFILE File, int64_t offLock, uint64_t cbLock)
{
    Assert(offLock >= 0);

    /*
     * Validate offset.
     */
    if (    sizeof(off_t) < sizeof(cbLock)
        &&  (    (offLock >> 32) != 0
             ||  (cbLock >> 32) != 0
             ||  ((offLock + cbLock) >> 32) != 0))
    {
        AssertMsgFailed(("64-bit file i/o not supported! offLock=%lld cbLock=%lld\n", offLock, cbLock));
        return VERR_NOT_SUPPORTED;
    }

    /* Prepare flock structure. */
    struct flock fl;
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = (off_t)offLock;
    fl.l_len    = (off_t)cbLock;
    fl.l_pid    = 0;

    if (fcntl(File, F_SETLK, &fl) >= 0)
        return VINF_SUCCESS;

    int iErr = errno;
    if (iErr == ENOTSUP)
    {
        /* A SMB hack, see RTFileLock. */
        if (!flock(File, LOCK_UN))
            return VINF_SUCCESS;
    }

    /** @todo check error codes for non existing lock. */
    if (    iErr == EAGAIN
        ||  iErr == EACCES)
        return VERR_FILE_LOCK_VIOLATION;

    return RTErrConvertFromErrno(iErr);
}

