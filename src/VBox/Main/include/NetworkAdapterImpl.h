/** @file
 *
 * VirtualBox COM class implementation
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

#ifndef ____H_NETWORKADAPTER
#define ____H_NETWORKADAPTER

#include "VirtualBoxBase.h"
#include "Collection.h"

class Machine;

class ATL_NO_VTABLE NetworkAdapter :
    public VirtualBoxBaseNEXT,
    public VirtualBoxSupportErrorInfoImpl <NetworkAdapter, INetworkAdapter>,
    public VirtualBoxSupportTranslation <NetworkAdapter>,
    public INetworkAdapter
{
public:

    struct Data
    {
        Data()
            : mSlot (0), mEnabled (FALSE)
            , mAttachmentType (NetworkAttachmentType_NoNetworkAttachment)
            ,  mCableConnected (TRUE), mTraceEnabled (FALSE)
#ifdef __WIN__
            , mHostInterface ("") // cannot be null
#endif
#ifdef VBOX_WITH_UNIXY_TAP_NETWORKING
            , mTAPFD (NIL_RTFILE)
#endif
            , mInternalNetwork ("") // cannot be null
        {}

        bool operator== (const Data &that) const
        {
            return this == &that ||
                   (mSlot == that.mSlot &&
                    mEnabled == that.mEnabled &&
                    mMACAddress == that.mMACAddress &&
                    mAttachmentType == that.mAttachmentType &&
                    mCableConnected == that.mCableConnected &&
                    mTraceEnabled == that.mTraceEnabled &&
                    mHostInterface == that.mHostInterface &&
#ifdef VBOX_WITH_UNIXY_TAP_NETWORKING
                    mTAPSetupApplication == that.mTAPSetupApplication &&
                    mTAPTerminateApplication == that.mTAPTerminateApplication &&
                    mTAPFD == that.mTAPFD &&
#endif
                    mInternalNetwork == that.mInternalNetwork);
        }

        NetworkAdapterType_T mAdapterType;
        ULONG mSlot;
        BOOL mEnabled;
        Bstr mMACAddress;
        NetworkAttachmentType_T mAttachmentType;
        BOOL mCableConnected;
        BOOL mTraceEnabled;
        Bstr mTraceFile;
        Bstr mHostInterface;
#ifdef VBOX_WITH_UNIXY_TAP_NETWORKING
        Bstr mTAPSetupApplication;
        Bstr mTAPTerminateApplication;
        RTFILE mTAPFD;
#endif
        Bstr mInternalNetwork;
    };

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT (NetworkAdapter)

    DECLARE_NOT_AGGREGATABLE(NetworkAdapter)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(NetworkAdapter)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(INetworkAdapter)
    END_COM_MAP()

    NS_DECL_ISUPPORTS

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
    STDMETHOD(COMSETTER(MACAddress)) (INPTR BSTR aMACAddress);
    STDMETHOD(COMGETTER(AttachmentType)) (NetworkAttachmentType_T *aAttachmentType);
    STDMETHOD(COMGETTER(HostInterface)) (BSTR *aHostInterface);
    STDMETHOD(COMSETTER(HostInterface)) (INPTR BSTR aHostInterface);
#ifdef VBOX_WITH_UNIXY_TAP_NETWORKING
    STDMETHOD(COMGETTER(TAPFileDescriptor)) (LONG *aTAPFileDescriptor);
    STDMETHOD(COMSETTER(TAPFileDescriptor)) (LONG aTAPFileDescriptor);
    STDMETHOD(COMGETTER(TAPSetupApplication)) (BSTR *aTAPSetupApplication);
    STDMETHOD(COMSETTER(TAPSetupApplication)) (INPTR BSTR aTAPSetupApplication);
    STDMETHOD(COMGETTER(TAPTerminateApplication)) (BSTR *aTAPTerminateApplication);
    STDMETHOD(COMSETTER(TAPTerminateApplication)) (INPTR BSTR aTAPTerminateApplication);
#endif
    STDMETHOD(COMGETTER(InternalNetwork)) (BSTR *aInternalNetwork);
    STDMETHOD(COMSETTER(InternalNetwork)) (INPTR BSTR aInternalNetwork);
    STDMETHOD(COMGETTER(CableConnected)) (BOOL *aConnected);
    STDMETHOD(COMSETTER(CableConnected)) (BOOL aConnected);
    STDMETHOD(COMGETTER(TraceEnabled)) (BOOL *aEnabled);
    STDMETHOD(COMSETTER(TraceEnabled)) (BOOL aEnabled);
    STDMETHOD(COMGETTER(TraceFile)) (BSTR *aTraceFile);
    STDMETHOD(COMSETTER(TraceFile)) (INPTR BSTR aTraceFile);

    // INetworkAdapter methods
    STDMETHOD(AttachToNAT)();
    STDMETHOD(AttachToHostInterface)();
    STDMETHOD(AttachToInternalNetwork)();
    STDMETHOD(Detach)();

    // public methods only for internal purposes

    bool isModified() { AutoLock alock (this); return mData.isBackedUp(); }
    bool isReallyModified() { AutoLock alock (this); return mData.hasActualChanges(); }
    bool rollback();
    void commit();
    void copyFrom (NetworkAdapter *aThat);

    // public methods for internal purposes only
    // (ensure there is a caller and a read lock before calling them!)

    const Backupable <Data> &data() const { return mData; }

    // for VirtualBoxSupportErrorInfoImpl
    static const wchar_t *getComponentName() { return L"NetworkAdapter"; }

private:

    void detach();
    void generateMACAddress();

    const ComObjPtr <Machine, ComWeakRef> mParent;
    const ComObjPtr <NetworkAdapter> mPeer;

    Backupable <Data> mData;
};

#endif // ____H_NETWORKADAPTER
