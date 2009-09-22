/** @file
 *
 * VBox Client Session COM Class definition
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

#ifndef ____H_SESSIONIMPL
#define ____H_SESSIONIMPL

#include "VirtualBoxBase.h"
#include "ConsoleImpl.h"

#ifdef RT_OS_WINDOWS
# include "win/resource.h"
#endif

/** @def VBOX_WITH_SYS_V_IPC_SESSION_WATCHER
 *  Use SYS V IPC for watching a session.
 *  This is defined in the Makefile since it's also used by MachineImpl.h/cpp.
 *
 *  @todo Dmitry, feel free to completely change this (and/or write a better description).
 *        (The same goes for the other darwin changes.)
 */
#ifdef __DOXYGEN__
# define VBOX_WITH_SYS_V_IPC_SESSION_WATCHER
#endif

class ATL_NO_VTABLE Session :
    public VirtualBoxBase,
    public VirtualBoxSupportErrorInfoImpl<Session, ISession>,
    public VirtualBoxSupportTranslation<Session>,
    VBOX_SCRIPTABLE_IMPL(ISession),
    VBOX_SCRIPTABLE_IMPL(IInternalSessionControl)
#ifdef RT_OS_WINDOWS
    , public CComCoClass<Session, &CLSID_Session>
#endif
{
public:

    DECLARE_CLASSFACTORY()

    DECLARE_REGISTRY_RESOURCEID(IDR_VIRTUALBOX)
    DECLARE_NOT_AGGREGATABLE(Session)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(Session)
        COM_INTERFACE_ENTRY2(IDispatch, ISession)
        COM_INTERFACE_ENTRY2(IDispatch, IInternalSessionControl)
        COM_INTERFACE_ENTRY(IInternalSessionControl)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(ISession)
    END_COM_MAP()

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializers/uninitializers only for internal purposes
    HRESULT init();
    void uninit (bool aFinalRelease);

    // ISession properties
    STDMETHOD(COMGETTER(State)) (SessionState_T *aState);
    STDMETHOD(COMGETTER(Type)) (SessionType_T *aType);
    STDMETHOD(COMGETTER(Machine)) (IMachine **aMachine);
    STDMETHOD(COMGETTER(Console)) (IConsole **aConsole);

    // ISession methods
    STDMETHOD(Close)();

    // IInternalSessionControl methods
    STDMETHOD(GetPID) (ULONG *aPid);
    STDMETHOD(GetRemoteConsole) (IConsole **aConsole);
    STDMETHOD(AssignMachine) (IMachine *aMachine);
    STDMETHOD(AssignRemoteMachine) (IMachine *aMachine, IConsole *aConsole);
    STDMETHOD(UpdateMachineState) (MachineState_T aMachineState);
    STDMETHOD(Uninitialize)();
    STDMETHOD(OnNetworkAdapterChange)(INetworkAdapter *networkAdapter, BOOL changeAdapter);
    STDMETHOD(OnSerialPortChange)(ISerialPort *serialPort);
    STDMETHOD(OnParallelPortChange)(IParallelPort *parallelPort);
    STDMETHOD(OnStorageControllerChange)();
    STDMETHOD(OnMediumChange)(IMediumAttachment *aMediumAttachment);
    STDMETHOD(OnVRDPServerChange)();
    STDMETHOD(OnUSBControllerChange)();
    STDMETHOD(OnSharedFolderChange) (BOOL aGlobal);
    STDMETHOD(OnUSBDeviceAttach) (IUSBDevice *aDevice, IVirtualBoxErrorInfo *aError, ULONG aMaskedIfs);
    STDMETHOD(OnUSBDeviceDetach) (IN_BSTR aId, IVirtualBoxErrorInfo *aError);
    STDMETHOD(OnShowWindow) (BOOL aCheck, BOOL *aCanShow, ULONG64 *aWinId);
    STDMETHOD(AccessGuestProperty) (IN_BSTR aName, IN_BSTR aValue, IN_BSTR aFlags,
                                    BOOL aIsSetter, BSTR *aRetValue, ULONG64 *aRetTimestamp, BSTR *aRetFlags);
    STDMETHOD(EnumerateGuestProperties) (IN_BSTR aPatterns,
                                         ComSafeArrayOut(BSTR, aNames),
                                         ComSafeArrayOut(BSTR, aValues),
                                         ComSafeArrayOut(ULONG64, aTimestamps),
                                         ComSafeArrayOut(BSTR, aFlags));

    // for VirtualBoxSupportErrorInfoImpl
    static const wchar_t *getComponentName() { return L"Session"; }

private:

    HRESULT close (bool aFinalRelease, bool aFromServer);
    HRESULT grabIPCSemaphore();
    void releaseIPCSemaphore();

    SessionState_T mState;
    SessionType_T mType;

    ComPtr<IInternalMachineControl> mControl;

    ComObjPtr<Console> mConsole;

    ComPtr<IMachine> mRemoteMachine;
    ComPtr<IConsole> mRemoteConsole;

    ComPtr<IVirtualBox> mVirtualBox;

    /* interprocess semaphore handle (id) for the opened machine */
#if defined(RT_OS_WINDOWS)
    HANDLE mIPCSem;
    HANDLE mIPCThreadSem;
#elif defined(RT_OS_OS2)
    RTTHREAD mIPCThread;
    RTSEMEVENT mIPCThreadSem;
#elif defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER)
    int mIPCSem;
#else
# error "Port me!"
#endif
};

#endif // ____H_SESSIONIMPL
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
