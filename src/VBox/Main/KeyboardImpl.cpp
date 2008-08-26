/** @file
 *
 * VirtualBox COM class implementation
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

#include "KeyboardImpl.h"
#include "ConsoleImpl.h"

#include "Logging.h"

#include <VBox/com/array.h>
#include <VBox/pdmdrv.h>
#include <iprt/asm.h>

// defines
////////////////////////////////////////////////////////////////////////////////

// globals
////////////////////////////////////////////////////////////////////////////////

/**
 * Keyboard driver instance data.
 */
typedef struct DRVMAINKEYBOARD
{
    /** Pointer to the keyboard object. */
    Keyboard                   *pKeyboard;
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Pointer to the keyboard port interface of the driver/device above us. */
    PPDMIKEYBOARDPORT           pUpPort;
    /** Our mouse connector interface. */
    PDMIKEYBOARDCONNECTOR       Connector;
} DRVMAINKEYBOARD, *PDRVMAINKEYBOARD;

/** Converts PDMIVMMDEVCONNECTOR pointer to a DRVMAINVMMDEV pointer. */
#define PPDMIKEYBOARDCONNECTOR_2_MAINKEYBOARD(pInterface) ( (PDRVMAINKEYBOARD) ((uintptr_t)pInterface - RT_OFFSETOF(DRVMAINKEYBOARD, Connector)) )


// constructor / destructor
////////////////////////////////////////////////////////////////////////////////

HRESULT Keyboard::FinalConstruct()
{
    mParent = NULL;
    mpDrv = NULL;
    mpVMMDev = NULL;
    mfVMMDevInited = false;
    return S_OK;
}

void Keyboard::FinalRelease()
{
    if (isReady())
        uninit();
}

// public methods
////////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the keyboard object.
 *
 * @returns COM result indicator
 * @param parent handle of our parent object
 */
HRESULT Keyboard::init (Console *parent)
{
    LogFlow(("Keyboard::init(): isReady=%d\n", isReady()));

    ComAssertRet (parent, E_INVALIDARG);

    AutoWriteLock alock (this);
    ComAssertRet (!isReady(), E_UNEXPECTED);

    mParent = parent;

    setReady (true);
    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void Keyboard::uninit()
{
    LogFlow(("Keyboard::uninit(): isReady=%d\n", isReady()));

    AutoWriteLock alock (this);
    AssertReturn (isReady(), (void) 0);

    if (mpDrv)
        mpDrv->pKeyboard = NULL;
    mpDrv = NULL;
    mpVMMDev = NULL;
    mfVMMDevInited = true;

    setReady (false);
}

/**
 * Sends a scancode to the keyboard.
 *
 * @returns COM status code
 * @param scancode The scancode to send
 */
STDMETHODIMP Keyboard::PutScancode(LONG scancode)
{
    AutoWriteLock alock (this);
    CHECK_READY();

    CHECK_CONSOLE_DRV (mpDrv);

    int rcVBox = mpDrv->pUpPort->pfnPutEvent(mpDrv->pUpPort, (uint8_t)scancode);

    if (VBOX_FAILURE (rcVBox))
        return setError (E_FAIL,
            tr ("Could not send scan code 0x%08X to the virtual keyboard (%Vrc)"),
                scancode, rcVBox);

    return S_OK;
}

/**
 * Sends a list of scancodes to the keyboard.
 *
 * @returns COM status code
 * @param scancodes   Pointer to the first scancode
 * @param count       Number of scancodes
 * @param codesStored Address of variable to store the number
 *                    of scancodes that were sent to the keyboard.
                      This value can be NULL.
 */
STDMETHODIMP Keyboard::PutScancodes(ComSafeArrayIn (LONG, scancodes),
                                    ULONG *codesStored)
{
    if (ComSafeArrayInIsNull(scancodes))
        return E_INVALIDARG;

    AutoWriteLock alock (this);
    CHECK_READY();

    CHECK_CONSOLE_DRV (mpDrv);

    com::SafeArray <LONG> keys(ComSafeArrayInArg(scancodes));
    int rcVBox = VINF_SUCCESS;

    for (uint32_t i = 0; (i < keys.size()) && VBOX_SUCCESS(rcVBox); i++)
    {
        rcVBox = mpDrv->pUpPort->pfnPutEvent(mpDrv->pUpPort, (uint8_t)keys[i]);
    }

    if (VBOX_FAILURE (rcVBox))
        return setError (E_FAIL,
            tr ("Could not send all scan codes to the virtual keyboard (%Vrc)"), rcVBox);

    /// @todo is it actually possible that not all scancodes can be transmitted?
    if (codesStored)
        *codesStored = keys.size();

    return S_OK;
}

/**
 * Sends Control-Alt-Delete to the keyboard. This could be done otherwise
 * but it's so common that we'll be nice and supply a convenience API.
 *
 * @returns COM status code
 *
 */
STDMETHODIMP Keyboard::PutCAD()
{
    static com::SafeArray<LONG> cadSequence(6);
    
    cadSequence[0] = 0x1d; // Ctrl down
    cadSequence[1] = 0x38; // Alt down
    cadSequence[2] = 0x53; // Del down
    cadSequence[3] = 0xd3; // Del up
    cadSequence[4] = 0xb8; // Alt up
    cadSequence[5] = 0x9d; // Ctrl up

    return PutScancodes (ComSafeArrayAsInParam(cadSequence), NULL);
}

//
// private methods
//

/**
 * Queries an interface to the driver.
 *
 * @returns Pointer to interface.
 * @returns NULL if the interface was not supported by the driver.
 * @param   pInterface          Pointer to this interface structure.
 * @param   enmInterface        The requested interface identification.
 */
DECLCALLBACK(void *)  Keyboard::drvQueryInterface(PPDMIBASE pInterface, PDMINTERFACE enmInterface)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVMAINKEYBOARD pDrv = PDMINS_2_DATA(pDrvIns, PDRVMAINKEYBOARD);
    switch (enmInterface)
    {
        case PDMINTERFACE_BASE:
            return &pDrvIns->IBase;
        case PDMINTERFACE_KEYBOARD_CONNECTOR:
            return &pDrv->Connector;
        default:
            return NULL;
    }
}


/**
 * Destruct a keyboard driver instance.
 *
 * @returns VBox status.
 * @param   pDrvIns     The driver instance data.
 */
DECLCALLBACK(void) Keyboard::drvDestruct(PPDMDRVINS pDrvIns)
{
    PDRVMAINKEYBOARD pData = PDMINS_2_DATA(pDrvIns, PDRVMAINKEYBOARD);
    LogFlow(("Keyboard::drvDestruct: iInstance=%d\n", pDrvIns->iInstance));
    if (pData->pKeyboard)
    {
        AutoWriteLock kbdLock (pData->pKeyboard);
        pData->pKeyboard->mpDrv = NULL;
        pData->pKeyboard->mpVMMDev = NULL;
    }
}

DECLCALLBACK(void) keyboardLedStatusChange(PPDMIKEYBOARDCONNECTOR pInterface, PDMKEYBLEDS enmLeds)
{
    PDRVMAINKEYBOARD pDrv = PPDMIKEYBOARDCONNECTOR_2_MAINKEYBOARD(pInterface);
    pDrv->pKeyboard->getParent()->onKeyboardLedsChange(!!(enmLeds & PDMKEYBLEDS_NUMLOCK),
                                                       !!(enmLeds & PDMKEYBLEDS_CAPSLOCK),
                                                       !!(enmLeds & PDMKEYBLEDS_SCROLLLOCK));
}

/**
 * Construct a keyboard driver instance.
 *
 * @returns VBox status.
 * @param   pDrvIns     The driver instance data.
 *                      If the registration structure is needed, pDrvIns->pDrvReg points to it.
 * @param   pCfgHandle  Configuration node handle for the driver. Use this to obtain the configuration
 *                      of the driver instance. It's also found in pDrvIns->pCfgHandle, but like
 *                      iInstance it's expected to be used a bit in this function.
 */
DECLCALLBACK(int) Keyboard::drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfgHandle)
{
    PDRVMAINKEYBOARD pData = PDMINS_2_DATA(pDrvIns, PDRVMAINKEYBOARD);
    LogFlow(("Keyboard::drvConstruct: iInstance=%d\n", pDrvIns->iInstance));

    /*
     * Validate configuration.
     */
    if (!CFGMR3AreValuesValid(pCfgHandle, "Object\0"))
        return VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES;
    PPDMIBASE pBaseIgnore;
    int rc = pDrvIns->pDrvHlp->pfnAttach(pDrvIns, &pBaseIgnore);
    if (rc != VERR_PDM_NO_ATTACHED_DRIVER)
    {
        AssertMsgFailed(("Configuration error: Not possible to attach anything to this driver!\n"));
        return VERR_PDM_DRVINS_NO_ATTACH;
    }

    /*
     * IBase.
     */
    pDrvIns->IBase.pfnQueryInterface        = Keyboard::drvQueryInterface;

    pData->Connector.pfnLedStatusChange     = keyboardLedStatusChange;

    /*
     * Get the IKeyboardPort interface of the above driver/device.
     */
    pData->pUpPort = (PPDMIKEYBOARDPORT)pDrvIns->pUpBase->pfnQueryInterface(pDrvIns->pUpBase, PDMINTERFACE_KEYBOARD_PORT);
    if (!pData->pUpPort)
    {
        AssertMsgFailed(("Configuration error: No keyboard port interface above!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }

    /*
     * Get the Keyboard object pointer and update the mpDrv member.
     */
    void *pv;
    rc = CFGMR3QueryPtr(pCfgHandle, "Object", &pv);
    if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: No/bad \"Object\" value! rc=%Vrc\n", rc));
        return rc;
    }
    pData->pKeyboard = (Keyboard *)pv;        /** @todo Check this cast! */
    pData->pKeyboard->mpDrv = pData;

    return VINF_SUCCESS;
}


/**
 * Keyboard driver registration record.
 */
const PDMDRVREG Keyboard::DrvReg =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szDriverName */
    "MainKeyboard",
    /* pszDescription */
    "Main keyboard driver (Main as in the API).",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_KEYBOARD,
    /* cMaxInstances */
    ~0,
    /* cbInstance */
    sizeof(DRVMAINKEYBOARD),
    /* pfnConstruct */
    Keyboard::drvConstruct,
    /* pfnDestruct */
    Keyboard::drvDestruct,
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
    /* pfnDetach */
    NULL
};
