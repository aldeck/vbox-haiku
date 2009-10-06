/* $Id$ */
/** @file
 * VBoxServiceUtils - Guest Additions Services (Utilities).
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
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef ___VBoxServiceUtils_h
#define ___VBoxServiceUtils_h

#include "VBoxServiceInternal.h"

#ifdef VBOX_WITH_GUEST_PROPS
int VBoxServiceWritePropF(uint32_t u32ClientId, const char *pszName, const char *pszValueFormat, ...);
#endif
#ifdef RT_OS_WINDOWS
/** Gets a pre-formatted version string from the VS_FIXEDFILEINFO table. */
BOOL VBoxServiceGetFileVersionString(const char* pszPath, const char* pszFileName, char* pszVersion, UINT uiSize);
#endif

#endif

