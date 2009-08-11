/* $Id$ */

/** @file
 *
 * VBox USBController COM Class declaration.
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

#ifndef ____H_USBCONTROLLERIMPL
#define ____H_USBCONTROLLERIMPL

#include "VirtualBoxBase.h"
#ifdef VBOX_WITH_USB
# include "USBDeviceFilterImpl.h"
#endif

#include <list>

class Machine;
class HostUSBDevice;

namespace settings
{
    struct USBController;
}

/**
 *  @note we cannot use VirtualBoxBaseWithTypedChildren <USBDeviceFilter> as a
 *  base class, because we want a quick (map-based) way of validating
 *  IUSBDeviceFilter pointers passed from outside as method parameters that
 *  VirtualBoxBaseWithChildren::getDependentChild() gives us.
 */

class ATL_NO_VTABLE USBController :
    public VirtualBoxBaseWithChildrenNEXT,
    public VirtualBoxSupportErrorInfoImpl<USBController, IUSBController>,
    public VirtualBoxSupportTranslation<USBController>,
    VBOX_SCRIPTABLE_IMPL(IUSBController)
{
private:

    struct Data
    {
        /* Constructor. */
        Data() : mEnabled (FALSE), mEnabledEhci (FALSE) { }

        bool operator== (const Data &that) const
        {
            return this == &that || (mEnabled == that.mEnabled && mEnabledEhci == that.mEnabledEhci);
        }

        /** Enabled indicator. */
        BOOL mEnabled;

        /** Enabled indicator for EHCI. */
        BOOL mEnabledEhci;
    };

public:

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT (USBController)

    DECLARE_NOT_AGGREGATABLE (USBController)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(USBController)
        COM_INTERFACE_ENTRY  (ISupportErrorInfo)
        COM_INTERFACE_ENTRY  (IUSBController)
        COM_INTERFACE_ENTRY2 (IDispatch, IUSBController)
    END_COM_MAP()

    NS_DECL_ISUPPORTS

    DECLARE_EMPTY_CTOR_DTOR (USBController)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init (Machine *aParent);
    HRESULT init (Machine *aParent, USBController *aThat);
    HRESULT initCopy (Machine *aParent, USBController *aThat);
    void uninit();

    // IUSBController properties
    STDMETHOD(COMGETTER(Enabled)) (BOOL *aEnabled);
    STDMETHOD(COMSETTER(Enabled)) (BOOL aEnabled);
    STDMETHOD(COMGETTER(EnabledEhci)) (BOOL *aEnabled);
    STDMETHOD(COMSETTER(EnabledEhci)) (BOOL aEnabled);
    STDMETHOD(COMGETTER(USBStandard)) (USHORT *aUSBStandard);
    STDMETHOD(COMGETTER(DeviceFilters)) (ComSafeArrayOut (IUSBDeviceFilter *, aDevicesFilters));

    // IUSBController methods
    STDMETHOD(CreateDeviceFilter) (IN_BSTR aName, IUSBDeviceFilter **aFilter);
    STDMETHOD(InsertDeviceFilter) (ULONG aPosition, IUSBDeviceFilter *aFilter);
    STDMETHOD(RemoveDeviceFilter) (ULONG aPosition, IUSBDeviceFilter **aFilter);

    // public methods only for internal purposes

    HRESULT loadSettings(const settings::USBController &data);
    HRESULT saveSettings(settings::USBController &data);

    bool isModified();
    bool isReallyModified();
    bool rollback();
    void commit();
    void copyFrom (USBController *aThat);

#ifdef VBOX_WITH_USB
    HRESULT onDeviceFilterChange (USBDeviceFilter *aFilter,
                                  BOOL aActiveChanged = FALSE);

    bool hasMatchingFilter (const ComObjPtr<HostUSBDevice> &aDevice, ULONG *aMaskedIfs);
    bool hasMatchingFilter (IUSBDevice *aUSBDevice, ULONG *aMaskedIfs);

    HRESULT notifyProxy (bool aInsertFilters);
#endif /* VBOX_WITH_USB */

    // public methods for internal purposes only
    // (ensure there is a caller and a read lock before calling them!)

    /** @note this doesn't require a read lock since mParent is constant. */
    const ComObjPtr<Machine, ComWeakRef> &parent() { return mParent; };

    const Backupable<Data> &data() { return mData; }

    // for VirtualBoxSupportErrorInfoImpl
    static const wchar_t *getComponentName() { return L"USBController"; }

private:

#ifdef VBOX_WITH_USB
    /** specialization for IUSBDeviceFilter */
    ComObjPtr<USBDeviceFilter> getDependentChild (IUSBDeviceFilter *aFilter)
    {
        VirtualBoxBase *child = VirtualBoxBaseWithChildrenNEXT::
                                getDependentChild (ComPtr<IUnknown> (aFilter));
        return child ? static_cast <USBDeviceFilter *> (child)
                     : NULL;
    }
#endif /* VBOX_WITH_USB */

    void printList();

    /** Parent object. */
    const ComObjPtr<Machine, ComWeakRef> mParent;
    /** Peer object. */
    const ComObjPtr<USBController> mPeer;
    /** Data. */
    Backupable <Data> mData;

#ifdef VBOX_WITH_USB
    // the following fields need special backup/rollback/commit handling,
    // so they cannot be a part of Data

    typedef std::list <ComObjPtr<USBDeviceFilter> > DeviceFilterList;
    Backupable <DeviceFilterList> mDeviceFilters;
#endif /* VBOX_WITH_USB */
};

#endif //!____H_USBCONTROLLERIMPL
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
