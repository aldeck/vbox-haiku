/** @file
 * IPRT - SHA1 digest creation
 */

/*
 * Copyright (C) 2009 Sun Microsystems, Inc.
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

#ifndef ___iprt_sha1_h
#define ___iprt_sha1_h

#include <iprt/cdefs.h>

RT_C_DECLS_BEGIN

/** @group grp_rt_sha1digest   RTSHA1Digest - SHA1 digest creation
 * @ingroup grp_rt
 * @{
 */

/**
 * Creates a SHA1 digest for the given file.
 *
 * @returns VBox status code.
 *
 * @param   pszFile      Filename to create a SHA1 digest for.
 * @param   ppszDigest   On success the SHA1 digest.
 */
RTR3DECL(int) RTSha1Digest(const char *pszFile, char **ppszDigest);

/** @} */

RT_C_DECLS_END

#endif /* ___iprt_sha1_h */

