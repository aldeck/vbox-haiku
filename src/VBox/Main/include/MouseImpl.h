/** @file
 *
 * VirtualBox COM class implementation
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

#ifndef ____H_MOUSEIMPL
#define ____H_MOUSEIMPL

#include "VirtualBoxBase.h"
#include "ConsoleEvents.h"
#include "ConsoleImpl.h"
#include <VBox/pdm.h>

/** Simple mouse event class. */
class MouseEvent
{
public:
    MouseEvent() : dx(0), dy(0), dz(0), state(-1) {}
    MouseEvent(int _dx, int _dy, int _dz, int _state) :
        dx(_dx), dy(_dy), dz(_dz), state(_state) {}
    bool isValid()
    {
        return state != -1;
    }
    // not logical to be int but that's how it's defined in QEMU
    /** @todo r=bird: and what is the logical declaration then? We'll be using int32_t btw. */
    int dx, dy, dz;
    int state;
};
// template instantiation
typedef ConsoleEventBuffer<MouseEvent> MouseEventBuffer;

class ATL_NO_VTABLE Mouse :
    public VirtualBoxSupportErrorInfoImpl <Mouse, IMouse>,
    public VirtualBoxSupportTranslation <Mouse>,
    public VirtualBoxBase,
    public IMouse
{
public:

    DECLARE_NOT_AGGREGATABLE(Mouse)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(Mouse)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IMouse)
    END_COM_MAP()

    NS_DECL_ISUPPORTS

    HRESULT FinalConstruct();
    void FinalRelease();

    // public methods only for internal purposes
    HRESULT init(Console *parent);
    void uninit();

    // IMouse properties
    STDMETHOD(COMGETTER(AbsoluteSupported)) (BOOL *absoluteSupported);
    STDMETHOD(COMGETTER(NeedsHostCursor)) (BOOL *needsHostCursor);

    // IMouse methods
    STDMETHOD(PutMouseEvent)(LONG dx, LONG dy, LONG dz,
                             LONG buttonState);
    STDMETHOD(PutMouseEventAbsolute)(LONG x, LONG y, LONG dz,
                                     LONG buttonState);

    // for VirtualBoxSupportErrorInfoImpl
    static const wchar_t *getComponentName() { return L"Mouse"; }

    static const PDMDRVREG  DrvReg;

private:

    static DECLCALLBACK(void *) drvQueryInterface(PPDMIBASE pInterface, PDMINTERFACE enmInterface);
    static DECLCALLBACK(int)    drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfgHandle);
    static DECLCALLBACK(void)   drvDestruct(PPDMDRVINS pDrvIns);

    ComObjPtr <Console, ComWeakRef> mParent;
    /** Pointer to the associated mouse driver. */
    struct DRVMAINMOUSE    *mpDrv;

    LONG uHostCaps;
};

#endif // ____H_MOUSEIMPL
