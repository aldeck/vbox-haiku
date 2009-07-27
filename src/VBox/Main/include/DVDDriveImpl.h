/* $Id$ */

/** @file
 *
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2008 Sun Microsystems, Inc.
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

#ifndef ____H_DVDDRIVEIMPL
#define ____H_DVDDRIVEIMPL

#include "VirtualBoxBase.h"

#include "MediumImpl.h"

class Machine;

class ATL_NO_VTABLE DVDDrive :
    public VirtualBoxBase,
    public VirtualBoxSupportErrorInfoImpl<DVDDrive, IDVDDrive>,
    public VirtualBoxSupportTranslation<DVDDrive>,
    VBOX_SCRIPTABLE_IMPL(IDVDDrive)
{
public:

    struct Data
    {
        Data()
        {
            state = DriveState_NotMounted;
            passthrough = false;
        }

        bool operator== (const Data &that) const
        {
            return this == &that ||
                   (state == that.state &&
                    image.equalsTo (that.image) &&
                    hostDrive.equalsTo (that.hostDrive));
        }

        ComObjPtr<DVDImage> image;
        ComPtr <IHostDVDDrive> hostDrive;
        DriveState_T state;
        BOOL passthrough;
    };

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT (DVDDrive)

    DECLARE_NOT_AGGREGATABLE(DVDDrive)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(DVDDrive)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IDVDDrive)
        COM_INTERFACE_ENTRY(IDispatch)
    END_COM_MAP()

    NS_DECL_ISUPPORTS

    DECLARE_EMPTY_CTOR_DTOR (DVDDrive)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init (Machine *aParent);
    HRESULT init (Machine *aParent, DVDDrive *aThat);
    HRESULT initCopy (Machine *aParent, DVDDrive *aThat);
    void uninit();

    // IDVDDrive properties
    STDMETHOD(COMGETTER(State)) (DriveState_T *aState);
    STDMETHOD(COMGETTER(Passthrough)) (BOOL *aPassthrough);
    STDMETHOD(COMSETTER(Passthrough)) (BOOL aPassthrough);

    // IDVDDrive methods
    STDMETHOD(MountImage) (IN_BSTR aImageId);
    STDMETHOD(CaptureHostDrive) (IHostDVDDrive *aHostDVDDrive);
    STDMETHOD(Unmount)();
    STDMETHOD(GetImage)(IDVDImage **aDVDImage);
    STDMETHOD(GetHostDrive) (IHostDVDDrive **aHostDVDDrive);

    // public methods only for internal purposes

    HRESULT loadSettings (const settings::Key &aMachineNode);
    HRESULT saveSettings (settings::Key &aMachineNode);

    bool isModified() { AutoWriteLock alock (this); return m.isBackedUp(); }
    bool isReallyModified() { AutoWriteLock alock (this); return m.hasActualChanges(); }
    bool rollback();
    void commit();
    void copyFrom (DVDDrive *aThat);

    HRESULT unmount();

    // public methods for internal purposes only
    // (ensure there is a caller and a read lock before calling them!)

    Backupable <Data> &data() { return m; }

    // for VirtualBoxSupportErrorInfoImpl
    static const wchar_t *getComponentName() { return L"DVDDrive"; }

private:

    const ComObjPtr <Machine, ComWeakRef> mParent;
    const ComObjPtr <DVDDrive> mPeer;

    Backupable <Data> m;
};

#endif // ____H_DVDDRIVEIMPL

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
