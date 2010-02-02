/* $Id$ */
/** @file
 * VBox frontends: Basic Frontend (BFE):
 * Implementation of Mouse class
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
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifdef VBOXBFE_WITHOUT_COM
# include "COMDefs.h"
#else
# include <VBox/com/defs.h>
#endif
#include <VBox/pdm.h>
#include <VBox/cfgm.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/uuid.h>
#include <VBox/VMMDev.h>
#include "MouseImpl.h"
#include "DisplayImpl.h"
#include "VMMDevInterface.h"

/**
 * Mouse driver instance data.
 */
typedef struct DRVMAINMOUSE
{
    /** Pointer to the associated mouse driver. */
    Mouse                      *mpDrv;
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Pointer to the mouse port interface of the driver/device above us. */
    PPDMIMOUSEPORT              pUpPort;
    /** Our mouse connector interface. */
    PDMIMOUSECONNECTOR          Connector;
} DRVMAINMOUSE, *PDRVMAINMOUSE;


// IMouse methods
/////////////////////////////////////////////////////////////////////////////

int Mouse::setAbsoluteCoordinates(bool a_fAbsolute)
{
    this->fAbsolute = a_fAbsolute;
    return S_OK;
}

int Mouse::setNeedsHostCursor(bool a_fNeedsHostCursor)
{
    this->fNeedsHostCursor = a_fNeedsHostCursor;
    return S_OK;
}

int Mouse::setHostCursor(bool enable)
{
    uHostCaps = enable ? 0 : VMMDEV_MOUSE_HOST_CANNOT_HWPOINTER;
    gVMMDev->SetMouseCapabilities(uHostCaps);
    return S_OK;
}

/**
 * Send a mouse event.
 *
 * @returns COM status code
 * @param dx          X movement
 * @param dy          Y movement
 * @param dz          Z movement
 * @param buttonState The mouse button state
 */
int Mouse::PutMouseEvent(LONG dx, LONG dy, LONG dz, LONG buttonState)
{
    uint32_t mouseCaps;
    gVMMDev->QueryMouseCapabilities(&mouseCaps);

    /*
     * This method being called implies that the host no
     * longer wants to use absolute coordinates. If the VMM
     * device isn't aware of that yet, tell it.
     */
    if (mouseCaps & VMMDEV_MOUSE_HOST_CAN_ABSOLUTE)
    {
        gVMMDev->SetMouseCapabilities(uHostCaps);
    }

    uint32_t fButtons = 0;
    if (buttonState & PDMIMOUSEPORT_BUTTON_LEFT)
        fButtons |= PDMIMOUSEPORT_BUTTON_LEFT;
    if (buttonState & PDMIMOUSEPORT_BUTTON_RIGHT)
        fButtons |= PDMIMOUSEPORT_BUTTON_RIGHT;
    if (buttonState & PDMIMOUSEPORT_BUTTON_MIDDLE)
        fButtons |= PDMIMOUSEPORT_BUTTON_MIDDLE;

    int vrc = mpDrv->pUpPort->pfnPutEvent(mpDrv->pUpPort, dx, dy, dz, 0 /* Horizontal wheel */, fButtons);
    if (RT_FAILURE (vrc))
        return E_FAIL;

    return S_OK;
}

/**
 * Send an absolute mouse event to the VM. This only works
 * when the required guest support has been installed.
 *
 * @returns COM status code
 * @param x          X position (pixel)
 * @param y          Y position (pixel)
 * @param dz         Z movement
 * @param buttonState The mouse button state
 */
int Mouse::PutMouseEventAbsolute(LONG x, LONG y, LONG dz, LONG buttonState)
{
    uint32_t mouseCaps;
    gVMMDev->QueryMouseCapabilities(&mouseCaps);

    /*
     * This method being called implies that the host no
     * longer wants to use absolute coordinates. If the VMM
     * device isn't aware of that yet, tell it.
     */
    if (!(mouseCaps & VMMDEV_MOUSE_HOST_CAN_ABSOLUTE))
    {
        gVMMDev->SetMouseCapabilities(uHostCaps | VMMDEV_MOUSE_HOST_CAN_ABSOLUTE);
    }

    ULONG displayWidth;
    ULONG displayHeight;
    displayHeight = gDisplay->getHeight();
    displayWidth  = gDisplay->getWidth();

    uint32_t mouseXAbs = (x * 0xFFFF) / displayWidth;
    uint32_t mouseYAbs = (y * 0xFFFF) / displayHeight;

    /*
     * Send the absolute mouse position to the VMM device
     */
    int vrc = gVMMDev->SetAbsoluteMouse(mouseXAbs, mouseYAbs);
    AssertRC(vrc);

    // check if the guest actually wants absolute mouse positions
    if (mouseCaps & VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE)
    {
        uint32_t fButtons = 0;
        if (buttonState & PDMIMOUSEPORT_BUTTON_LEFT)
            fButtons |= PDMIMOUSEPORT_BUTTON_LEFT;
        if (buttonState & PDMIMOUSEPORT_BUTTON_RIGHT)
            fButtons |= PDMIMOUSEPORT_BUTTON_RIGHT;
        if (buttonState & PDMIMOUSEPORT_BUTTON_MIDDLE)
            fButtons |= PDMIMOUSEPORT_BUTTON_MIDDLE;

        vrc = mpDrv->pUpPort->pfnPutEvent(mpDrv->pUpPort, 1, 1, dz, 0 /* Horizontal wheel */, fButtons);
        if (RT_FAILURE (vrc))
            return E_FAIL;
    }

    return S_OK;
}

/////////////////////////////////////////////////////////////////////////////

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
DECLCALLBACK(void *)  Mouse::drvQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVMAINMOUSE pDrv = PDMINS_2_DATA(pDrvIns, PDRVMAINMOUSE);
    if (RTUuidCompare2Strs(pszIID, PDMIBASE_IID) == 0)
        return &pDrvIns->IBase;
    if (RTUuidCompare2Strs(pszIID, PDMIMOUSECONNECTOR_IID) == 0)
        return &pDrv->Connector;
    return NULL;
}


/**
 * Destruct a mouse driver instance.
 *
 * @returns VBox status.
 * @param   pDrvIns     The driver instance data.
 */
DECLCALLBACK(void) Mouse::drvDestruct(PPDMDRVINS pDrvIns)
{
    //PDRVMAINMOUSE pData = PDMINS_2_DATA(pDrvIns, PDRVMAINMOUSE);
    LogFlow(("Mouse::drvDestruct: iInstance=%d\n", pDrvIns->iInstance));
}


/**
 * Construct a mouse driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
DECLCALLBACK(int) Mouse::drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfgHandle, uint32_t fFlags)
{
    PDRVMAINMOUSE pData = PDMINS_2_DATA(pDrvIns, PDRVMAINMOUSE);
    LogFlow(("drvMainMouse_Construct: iInstance=%d\n", pDrvIns->iInstance));

    /*
     * Validate configuration.
     */
    if (!CFGMR3AreValuesValid(pCfgHandle, "Object\0"))
        return VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES;
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * IBase.
     */
    pDrvIns->IBase.pfnQueryInterface        = Mouse::drvQueryInterface;

    /*
     * Get the IMousePort interface of the above driver/device.
     */
        pData->pUpPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMOUSEPORT);
    if (!pData->pUpPort)
    {
        AssertMsgFailed(("Configuration error: No mouse port interface above!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }

    /*
     * Get the Mouse object pointer and update the mpDrv member.
     */
    void *pv;
    int rc = CFGMR3QueryPtr(pCfgHandle, "Object", &pv);
    if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: No/bad \"Object\" value! rc=%Rrc\n", rc));
        return rc;
    }

    pData->mpDrv = (Mouse*)pv;        /** @todo Check this cast! */
    pData->mpDrv->mpDrv = pData;

    return VINF_SUCCESS;
}


/**
 * Main mouse driver registration record.
 */
const PDMDRVREG Mouse::DrvReg =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "MainMouse",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Main mouse driver (Main as in the API).",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_MOUSE,
    /* cMaxInstances */
    ~0,
    /* cbInstance */
    sizeof(DRVMAINMOUSE),
    /* pfnConstruct */
    Mouse::drvConstruct,
    /* pfnDestruct */
    Mouse::drvDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

