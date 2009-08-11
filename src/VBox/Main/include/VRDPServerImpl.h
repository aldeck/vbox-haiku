/* $Id$ */

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

#ifndef ____H_VRDPSERVER
#define ____H_VRDPSERVER

#include "VirtualBoxBase.h"

#include <VBox/VRDPAuth.h>

class Machine;

namespace settings
{
    struct VRDPSettings;
}

class ATL_NO_VTABLE VRDPServer :
    public VirtualBoxBase,
    public VirtualBoxSupportErrorInfoImpl<VRDPServer, IVRDPServer>,
    public VirtualBoxSupportTranslation<VRDPServer>,
    VBOX_SCRIPTABLE_IMPL(IVRDPServer)
{
public:

    struct Data
    {
        bool operator== (const Data &that) const
        {
            return this == &that ||
                   (mEnabled == that.mEnabled &&
                    mVRDPPort == that.mVRDPPort &&
                    mVRDPAddress == that.mVRDPAddress &&
                    mAuthType == that.mAuthType &&
                    mAuthTimeout == that.mAuthTimeout &&
                    mAllowMultiConnection == that.mAllowMultiConnection &&
                    mReuseSingleConnection == that.mReuseSingleConnection);
        }

        BOOL mEnabled;
        ULONG mVRDPPort;
        Bstr mVRDPAddress;
        VRDPAuthType_T mAuthType;
        ULONG mAuthTimeout;
        BOOL mAllowMultiConnection;
        BOOL mReuseSingleConnection;
    };

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT (VRDPServer)

    DECLARE_NOT_AGGREGATABLE(VRDPServer)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(VRDPServer)
        COM_INTERFACE_ENTRY  (ISupportErrorInfo)
        COM_INTERFACE_ENTRY  (IVRDPServer)
        COM_INTERFACE_ENTRY2 (IDispatch, IVRDPServer)
    END_COM_MAP()

    NS_DECL_ISUPPORTS

    DECLARE_EMPTY_CTOR_DTOR (VRDPServer)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *aParent);
    HRESULT init(Machine *aParent, VRDPServer *aThat);
    HRESULT initCopy (Machine *aParent, VRDPServer *aThat);
    void uninit();

    // IVRDPServer properties
    STDMETHOD(COMGETTER(Enabled)) (BOOL *aEnabled);
    STDMETHOD(COMSETTER(Enabled)) (BOOL aEnable);
    STDMETHOD(COMGETTER(Port)) (ULONG *aPort);
    STDMETHOD(COMSETTER(Port)) (ULONG aPort);
    STDMETHOD(COMGETTER(NetAddress)) (BSTR *aAddress);
    STDMETHOD(COMSETTER(NetAddress)) (IN_BSTR aAddress);
    STDMETHOD(COMGETTER(AuthType)) (VRDPAuthType_T *aType);
    STDMETHOD(COMSETTER(AuthType)) (VRDPAuthType_T aType);
    STDMETHOD(COMGETTER(AuthTimeout)) (ULONG *aTimeout);
    STDMETHOD(COMSETTER(AuthTimeout)) (ULONG aTimeout);
    STDMETHOD(COMGETTER(AllowMultiConnection)) (BOOL *aAllowMultiConnection);
    STDMETHOD(COMSETTER(AllowMultiConnection)) (BOOL aAllowMultiConnection);
    STDMETHOD(COMGETTER(ReuseSingleConnection)) (BOOL *aReuseSingleConnection);
    STDMETHOD(COMSETTER(ReuseSingleConnection)) (BOOL aReuseSingleConnection);

    // IVRDPServer methods

    // public methods only for internal purposes

    HRESULT loadSettings(const settings::VRDPSettings &data);
    HRESULT saveSettings(settings::VRDPSettings &data);

    bool isModified() { AutoWriteLock alock (this); return mData.isBackedUp(); }
    bool isReallyModified() { AutoWriteLock alock (this); return mData.hasActualChanges(); }
    bool rollback();
    void commit();
    void copyFrom (VRDPServer *aThat);

    // public methods for internal purposes only
    // (ensure there is a caller and a read lock before calling them!)

    const Backupable <Data> &data() const { return mData; }

    // for VirtualBoxSupportErrorInfoImpl
    static const wchar_t *getComponentName() { return L"VRDPServer"; }

private:

    const ComObjPtr<Machine, ComWeakRef> mParent;
    const ComObjPtr<VRDPServer> mPeer;

    Backupable <Data> mData;
};

#endif // ____H_VRDPSERVER
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
