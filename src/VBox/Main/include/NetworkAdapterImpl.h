/* $Id$ */

/** @file
 *
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2009 Sun Microsystems, Inc.
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

#ifndef ____H_NETWORKADAPTER
#define ____H_NETWORKADAPTER

#include "VirtualBoxBase.h"
#include "NATEngineImpl.h"

class GuestOSType;

namespace settings
{
    struct NetworkAdapter;
}

class ATL_NO_VTABLE NetworkAdapter :
    public VirtualBoxBase,
    public VirtualBoxSupportErrorInfoImpl<NetworkAdapter, INetworkAdapter>,
    public VirtualBoxSupportTranslation<NetworkAdapter>,
    VBOX_SCRIPTABLE_IMPL(INetworkAdapter)
{
public:

    struct Data
    {
        Data() : mSlot(0), mEnabled(FALSE),
                 mAttachmentType(NetworkAttachmentType_Null),
                 mCableConnected(TRUE), mLineSpeed(0), mTraceEnabled(FALSE),
                 mHostInterface("") /* cannot be null */,
                 mNATNetwork("") /* cannot be null */
        {}

        NetworkAdapterType_T mAdapterType;
        ULONG mSlot;
        BOOL mEnabled;
        Bstr mMACAddress;
        NetworkAttachmentType_T mAttachmentType;
        BOOL mCableConnected;
        ULONG mLineSpeed;
        BOOL mTraceEnabled;
        Bstr mTraceFile;
        Bstr mHostInterface;
        Bstr mInternalNetwork;
        Bstr mNATNetwork;
        ULONG mBootPriority;
    };

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT (NetworkAdapter)

    DECLARE_NOT_AGGREGATABLE(NetworkAdapter)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(NetworkAdapter)
        COM_INTERFACE_ENTRY  (ISupportErrorInfo)
        COM_INTERFACE_ENTRY  (INetworkAdapter)
        COM_INTERFACE_ENTRY2 (IDispatch, INetworkAdapter)
    END_COM_MAP()

    DECLARE_EMPTY_CTOR_DTOR (NetworkAdapter)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init (Machine *aParent, ULONG aSlot);
    HRESULT init (Machine *aParent, NetworkAdapter *aThat);
    HRESULT initCopy (Machine *aParent, NetworkAdapter *aThat);
    void uninit();

    // INetworkAdapter properties
    STDMETHOD(COMGETTER(AdapterType))(NetworkAdapterType_T *aAdapterType);
    STDMETHOD(COMSETTER(AdapterType))(NetworkAdapterType_T aAdapterType);
    STDMETHOD(COMGETTER(Slot)) (ULONG *aSlot);
    STDMETHOD(COMGETTER(Enabled)) (BOOL *aEnabled);
    STDMETHOD(COMSETTER(Enabled)) (BOOL aEnabled);
    STDMETHOD(COMGETTER(MACAddress)) (BSTR *aMACAddress);
    STDMETHOD(COMSETTER(MACAddress)) (IN_BSTR aMACAddress);
    STDMETHOD(COMGETTER(AttachmentType)) (NetworkAttachmentType_T *aAttachmentType);
    STDMETHOD(COMGETTER(HostInterface)) (BSTR *aHostInterface);
    STDMETHOD(COMSETTER(HostInterface)) (IN_BSTR aHostInterface);
    STDMETHOD(COMGETTER(InternalNetwork)) (BSTR *aInternalNetwork);
    STDMETHOD(COMSETTER(InternalNetwork)) (IN_BSTR aInternalNetwork);
    STDMETHOD(COMGETTER(NATNetwork)) (BSTR *aNATNetwork);
    STDMETHOD(COMSETTER(NATNetwork)) (IN_BSTR aNATNetwork);
    STDMETHOD(COMGETTER(CableConnected)) (BOOL *aConnected);
    STDMETHOD(COMSETTER(CableConnected)) (BOOL aConnected);
    STDMETHOD(COMGETTER(TraceEnabled)) (BOOL *aEnabled);
    STDMETHOD(COMSETTER(TraceEnabled)) (BOOL aEnabled);
    STDMETHOD(COMGETTER(LineSpeed)) (ULONG *aSpeed);
    STDMETHOD(COMSETTER(LineSpeed)) (ULONG aSpeed);
    STDMETHOD(COMGETTER(TraceFile)) (BSTR *aTraceFile);
    STDMETHOD(COMSETTER(TraceFile)) (IN_BSTR aTraceFile);
    STDMETHOD(COMGETTER(NatDriver)) (INATEngine **aNatDriver);
    STDMETHOD(COMGETTER(BootPriority)) (ULONG *aBootPriority);
    STDMETHOD(COMSETTER(BootPriority)) (ULONG aBootPriority);

    // INetworkAdapter methods
    STDMETHOD(AttachToNAT)();
    STDMETHOD(AttachToBridgedInterface)();
    STDMETHOD(AttachToInternalNetwork)();
    STDMETHOD(AttachToHostOnlyInterface)();
    STDMETHOD(Detach)();

    // public methods only for internal purposes

    HRESULT loadSettings(const settings::NetworkAdapter &data);
    HRESULT saveSettings(settings::NetworkAdapter &data);

    bool isModified();
    void rollback();
    void commit();
    void copyFrom (NetworkAdapter *aThat);
    void applyDefaults (GuestOSType *aOsType);

    // for VirtualBoxSupportErrorInfoImpl
    static const wchar_t *getComponentName() { return L"NetworkAdapter"; }

private:

    void detach();
    void generateMACAddress();

    Machine * const     mParent;
    const ComObjPtr<NetworkAdapter> mPeer;
    const ComObjPtr<NATEngine> mNATEngine;

    bool                m_fModified;
    Backupable<Data>    mData;
};

#endif // ____H_NETWORKADAPTER
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
