/** @file
 *
 * VBoxGuest -- VirtualBox Win32 guest support driver PnP code
 *
 * Copyright (C) 2006-2007 innotek GmbH
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
 *
 */

#ifndef __VBoxGuestPnP_h__
#define __VBoxGuestPnP_h__


#include "VBoxGuest_Internal.h"

extern "C"
{
NTSTATUS VBoxGuestPnP(PDEVICE_OBJECT pDevObj, PIRP pIrp);
NTSTATUS VBoxGuestPower(PDEVICE_OBJECT pDevObj, PIRP pIrp);
}

#endif // __VBoxGuestPnP_h__
