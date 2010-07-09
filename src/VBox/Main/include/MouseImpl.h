/* $Id$ */
/** @file
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2008 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ____H_MOUSEIMPL
#define ____H_MOUSEIMPL

#include "VirtualBoxBase.h"
#include <VBox/pdmdrv.h>

/** Maximum number of devices supported */
enum { MOUSE_MAX_DEVICES = 3 };
/** Mouse driver instance data. */
typedef struct DRVMAINMOUSE DRVMAINMOUSE, *PDRVMAINMOUSE;

class Console;

class ATL_NO_VTABLE Mouse :
    public VirtualBoxBase
#ifndef VBOXBFE_WITHOUT_COM
    , VBOX_SCRIPTABLE_IMPL(IMouse)
#endif
{
public:

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(Mouse, IMouse)

    DECLARE_NOT_AGGREGATABLE(Mouse)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(Mouse)
        COM_INTERFACE_ENTRY  (ISupportErrorInfo)
        COM_INTERFACE_ENTRY  (IMouse)
        COM_INTERFACE_ENTRY2 (IDispatch, IMouse)
    END_COM_MAP()

    DECLARE_EMPTY_CTOR_DTOR (Mouse)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Console *parent);
    void uninit();

    // IMouse properties
    STDMETHOD(COMGETTER(AbsoluteSupported)) (BOOL *absoluteSupported);
    STDMETHOD(COMGETTER(RelativeSupported)) (BOOL *relativeSupported);
    STDMETHOD(COMGETTER(NeedsHostCursor)) (BOOL *needsHostCursor);

    // IMouse methods
    STDMETHOD(PutMouseEvent)(LONG dx, LONG dy, LONG dz, LONG dw,
                             LONG buttonState);
    STDMETHOD(PutMouseEventAbsolute)(LONG x, LONG y, LONG dz, LONG dw,
                                     LONG buttonState);

    static const PDMDRVREG  DrvReg;

    Console *getParent() const
    {
        return mParent;
    }

    // for VMMDevInterface
    void onVMMDevCanAbsChange(bool)
    {
        sendMouseCapsNotifications();
    }

    void onVMMDevNeedsHostChange(bool needsHost)
    {
        fVMMDevNeedsHostCursor = needsHost;
        sendMouseCapsNotifications();
    }

private:

    static DECLCALLBACK(void *) drvQueryInterface(PPDMIBASE pInterface, const char *pszIID);
    static DECLCALLBACK(void)   mouseReportModes (PPDMIMOUSECONNECTOR pInterface, bool fRel, bool fAbs);
    static DECLCALLBACK(int)    drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);
    static DECLCALLBACK(void)   drvDestruct(PPDMDRVINS pDrvIns);

    HRESULT getVMMDevMouseCaps(uint32_t *pfCaps);
    HRESULT setVMMDevMouseCaps(uint32_t fCaps);
    HRESULT reportRelEventToMouseDev(int32_t dx, int32_t dy, int32_t dz,
                                 int32_t dw, uint32_t fButtons);
    HRESULT reportAbsEventToMouseDev(uint32_t mouseXAbs, uint32_t mouseYAbs,
                                 int32_t dz, int32_t dw, uint32_t fButtons);
    HRESULT reportAbsEventToVMMDev(uint32_t mouseXAbs, uint32_t mouseYAbs);
    HRESULT convertDisplayWidth(LONG x, uint32_t *pcX);
    HRESULT convertDisplayHeight(LONG y, uint32_t *pcY);

    void sendMouseCapsNotifications(void);

#ifdef VBOXBFE_WITHOUT_COM
    Console *mParent;
#else
    Console * const         mParent;
#endif
    /** Pointer to the associated mouse driver. */
    struct DRVMAINMOUSE    *mpDrv[MOUSE_MAX_DEVICES];

    LONG uHostCaps;
    bool fVMMDevCanAbs;
    bool fVMMDevNeedsHostCursor;
    uint32_t mLastAbsX;
    uint32_t mLastAbsY;
    uint32_t mLastButtons;
};

#ifdef VBOXBFE_WITHOUT_COM
/** @todo make this a member of Console */
extern Mouse *gMouse;

/** @todo can we get these from the API somehow? */
enum
{
    MouseButtonState_LeftButton = 1,
    MouseButtonState_RightButton = 2,
    MouseButtonState_MiddleButton = 4,
    MouseButtonState_XButton1 = 8,
    MouseButtonState_XButton2 = 16,
};
#endif

#endif // !____H_MOUSEIMPL
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
