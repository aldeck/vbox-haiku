/** @file
 * VBoxZoneAccess - Hack that keeps vboxdrv referenced for granting zone access, Solaris hosts.
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
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
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <iprt/process.h>

#define DEVICE_NAME     "/dev/vboxdrv"

int main(int argc, char *argv[])
{
    int hDevice = -1;

    /* Check root permissions. */
    if (geteuid() != 0)
    {
        fprintf(stderr, "This program needs administrator privileges.\n");
        return -1;
    }

    /* Daemonize... */
    RTProcDaemonize(false /* fNoChDir */,
                    false /* fNoClose */,
                    NULL /* pszPidfile */);

    /* Open the device */
    hDevice = open(DEVICE_NAME, O_RDWR, 0);
    if (hDevice < 0)
    {
        fprintf(stderr, "Failed to open '%s'. errno=%d\n", DEVICE_NAME, errno);
        return errno;
    }

    /* Mark the file handle close on exec. */
    if (fcntl(hDevice, F_SETFD, FD_CLOEXEC) != 0)
    {
        fprintf(stderr, "Failed to set close on exec. errno=%d\n", errno);
        close(hDevice);
        return errno;
    }

    /* Go to interruptible sleep... */
    sleep(1000000000U);

    close(hDevice);

    return 0;
}

