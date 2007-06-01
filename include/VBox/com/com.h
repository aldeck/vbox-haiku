/** @file
 *
 * MS COM / XPCOM Abstraction Layer:
 * COM initialization / shutdown
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

#ifndef __VBox_com_com_h__
#define __VBox_com_com_h__

#include "VBox/com/defs.h"
#include "VBox/com/string.h"

namespace com
{

/**
 *  Initializes the COM runtime.
 *  Must be called on every thread that uses COM, before any COM activity.
 *
 *  @return COM result code
 */
HRESULT Initialize();

/**
 *  Shuts down the COM runtime.
 *  Must be called on every thread before termination.
 *  No COM calls may be made after this method returns.
 */
HRESULT Shutdown();

/**
 *  Resolves a given interface ID to a string containing the interface name.
 *  If, for some reason, the given IID cannot be resolved to a name, a NULL
 *  string is returned. A non-NULL string returned by this funciton must be
 *  freed using SysFreeString().
 *
 *  @param aIID     ID of the interface to get a name for
 *  @param aName    Resolved interface name or @c NULL on error
 */
void GetInterfaceNameByIID (const GUID &aIID, BSTR *aName);

/** 
 *  Returns the VirtualBox user home directory.
 *
 *  On failure, this function will return a path that caused a failure (or a
 *  null string if the faiulre is not path-related).
 *
 *  On success, this function will try to create the returned directory if it
 *  doesn't exist yet. This may also fail with the corresponding status code.
 * 
 *  @param aDir     Where to return the directory to.
 *  @return         VBox status code.
 */
int GetVBoxUserHomeDirectory (Utf8Str &aDir);

}; // namespace com

#endif // __VBox_com_com_h__

