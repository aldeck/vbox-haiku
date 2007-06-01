/** @file
 *
 * Main driver registration.
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
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/

#include "MouseImpl.h"
#include "KeyboardImpl.h"
#include "DisplayImpl.h"
#include "VMMDev.h"
#include "AudioSnifferInterface.h"
#include "ConsoleImpl.h"

#include "Logging.h"

#include <VBox/pdm.h>

#include <VBox/version.h>
#include <VBox/err.h>

/**
 * Register the main drivers.
 *
 * @returns VBox status code.
 * @param   pCallbacks      Pointer to the callback table.
 * @param   u32Version      VBox version number.
 */
extern "C" DECLEXPORT(int) VBoxDriversRegister(PCPDMDRVREGCB pCallbacks, uint32_t u32Version)
{
    LogFlow(("VBoxDriversRegister: u32Version=%#x\n", u32Version));
    AssertReleaseMsg(u32Version == VBOX_VERSION, ("u32Version=%#x VBOX_VERSION=%#x\n", u32Version, VBOX_VERSION));

    int rc = pCallbacks->pfnRegister(pCallbacks, &Mouse::DrvReg);
    if (VBOX_FAILURE(rc))
        return rc;

    rc = pCallbacks->pfnRegister(pCallbacks, &Keyboard::DrvReg);
    if (VBOX_FAILURE(rc))
        return rc;

    rc = pCallbacks->pfnRegister(pCallbacks, &Display::DrvReg);
    if (VBOX_FAILURE(rc))
        return rc;

    rc = pCallbacks->pfnRegister(pCallbacks, &VMMDev::DrvReg);
    if (VBOX_FAILURE(rc))
        return rc;

    rc = pCallbacks->pfnRegister(pCallbacks, &AudioSniffer::DrvReg);
    if (VBOX_FAILURE(rc))
        return rc;

    rc = pCallbacks->pfnRegister(pCallbacks, &Console::DrvStatusReg);
    if (VBOX_FAILURE(rc))
        return rc;
    return VINF_SUCCESS;
}

