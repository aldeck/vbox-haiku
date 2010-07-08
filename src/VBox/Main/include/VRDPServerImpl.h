/* $Id$ */

/** @file
 *
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2007 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ____H_VRDPSERVER
#define ____H_VRDPSERVER

#include "VirtualBoxBase.h"

#include <VBox/VRDPAuth.h>

namespace settings
{
    struct VRDPSettings;
}

class ATL_NO_VTABLE VRDPServer :
    public VirtualBoxBase,
    VBOX_SCRIPTABLE_IMPL(IVRDPServer)
{
public:

    struct Data
    {
        BOOL mEnabled;
        Bstr mVRDPPorts;
        Bstr mVRDPAddress;
        VRDPAuthType_T mAuthType;
        ULONG mAuthTimeout;
        BOOL mAllowMultiConnection;
        BOOL mReuseSingleConnection;
        BOOL mVideoChannel;
        ULONG mVideoChannelQuality;
    };

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(VRDPServer, IVRDPServer)

    DECLARE_NOT_AGGREGATABLE(VRDPServer)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(VRDPServer)
        COM_INTERFACE_ENTRY  (ISupportErrorInfo)
        COM_INTERFACE_ENTRY  (IVRDPServer)
        COM_INTERFACE_ENTRY2 (IDispatch, IVRDPServer)
    END_COM_MAP()

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
    STDMETHOD(COMGETTER(Ports)) (BSTR *aPorts);
    STDMETHOD(COMSETTER(Ports)) (IN_BSTR aPorts);
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
    STDMETHOD(COMGETTER(VideoChannel)) (BOOL *aVideoChannel);
    STDMETHOD(COMSETTER(VideoChannel)) (BOOL aVideoChannel);
    STDMETHOD(COMGETTER(VideoChannelQuality)) (ULONG *aVideoChannelQuality);
    STDMETHOD(COMSETTER(VideoChannelQuality)) (ULONG aVideoChannelQuality);

    // IVRDPServer methods

    // public methods only for internal purposes

    HRESULT loadSettings(const settings::VRDPSettings &data);
    HRESULT saveSettings(settings::VRDPSettings &data);

    void rollback();
    void commit();
    void copyFrom (VRDPServer *aThat);

private:

    Machine * const     mParent;
    const ComObjPtr<VRDPServer> mPeer;

    Backupable<Data>    mData;
};

#endif // ____H_VRDPSERVER
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
