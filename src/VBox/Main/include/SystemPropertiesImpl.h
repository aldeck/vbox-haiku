/* $Id$ */

/** @file
 *
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ____H_SYSTEMPROPERTIESIMPL
#define ____H_SYSTEMPROPERTIESIMPL

#include "VirtualBoxBase.h"
#include "MediumFormatImpl.h"

#include <VBox/com/array.h>

#include <list>

namespace settings
{
    struct SystemProperties;
}

class ATL_NO_VTABLE SystemProperties :
    public VirtualBoxBase,
    VBOX_SCRIPTABLE_IMPL(ISystemProperties)
{
public:

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT (SystemProperties, ISystemProperties)

    DECLARE_NOT_AGGREGATABLE(SystemProperties)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(SystemProperties)
        COM_INTERFACE_ENTRY  (ISupportErrorInfo)
        COM_INTERFACE_ENTRY  (ISystemProperties)
        COM_INTERFACE_ENTRY2 (IDispatch, ISystemProperties)
    END_COM_MAP()

    DECLARE_EMPTY_CTOR_DTOR (SystemProperties)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init (VirtualBox *aParent);
    void uninit();

    // ISystemProperties properties
    STDMETHOD(COMGETTER(MinGuestRAM) (ULONG *minRAM));
    STDMETHOD(COMGETTER(MaxGuestRAM) (ULONG *maxRAM));
    STDMETHOD(COMGETTER(MinGuestVRAM) (ULONG *minVRAM));
    STDMETHOD(COMGETTER(MaxGuestVRAM) (ULONG *maxVRAM));
    STDMETHOD(COMGETTER(MinGuestCPUCount) (ULONG *minCPUCount));
    STDMETHOD(COMGETTER(MaxGuestCPUCount) (ULONG *maxCPUCount));
    STDMETHOD(COMGETTER(MaxGuestMonitors) (ULONG *maxMonitors));
    STDMETHOD(COMGETTER(MaxVDISize) (LONG64 *maxVDISize));
    STDMETHOD(COMGETTER(NetworkAdapterCount) (ULONG *count));
    STDMETHOD(COMGETTER(SerialPortCount) (ULONG *count));
    STDMETHOD(COMGETTER(ParallelPortCount) (ULONG *count));
    STDMETHOD(COMGETTER(MaxBootPosition) (ULONG *aMaxBootPosition));
    STDMETHOD(COMGETTER(DefaultMachineFolder)) (BSTR *aDefaultMachineFolder);
    STDMETHOD(COMSETTER(DefaultMachineFolder)) (IN_BSTR aDefaultMachineFolder);
    STDMETHOD(COMGETTER(DefaultHardDiskFolder)) (BSTR *aDefaultHardDiskFolder);
    STDMETHOD(COMSETTER(DefaultHardDiskFolder)) (IN_BSTR aDefaultHardDiskFolder);
    STDMETHOD(COMGETTER(MediumFormats)) (ComSafeArrayOut (IMediumFormat *, aMediumFormats));
    STDMETHOD(COMGETTER(DefaultHardDiskFormat)) (BSTR *aDefaultHardDiskFormat);
    STDMETHOD(COMSETTER(DefaultHardDiskFormat)) (IN_BSTR aDefaultHardDiskFormat);
    STDMETHOD(COMGETTER(FreeDiskSpaceWarning))(LONG64 *aFreeDiskSpace);
    STDMETHOD(COMSETTER(FreeDiskSpaceWarning))(LONG64 aFreeDiskSpace);
    STDMETHOD(COMGETTER(FreeDiskSpacePercentWarning))(ULONG *aFreeDiskSpacePercent);
    STDMETHOD(COMSETTER(FreeDiskSpacePercentWarning))(ULONG aFreeDiskSpacePercent);
    STDMETHOD(COMGETTER(FreeDiskSpaceError))(LONG64 *aFreeDiskSpace);
    STDMETHOD(COMSETTER(FreeDiskSpaceError))(LONG64 aFreeDiskSpace);
    STDMETHOD(COMGETTER(FreeDiskSpacePercentError))(ULONG *aFreeDiskSpacePercent);
    STDMETHOD(COMSETTER(FreeDiskSpacePercentError))(ULONG aFreeDiskSpacePercent);
    STDMETHOD(COMGETTER(RemoteDisplayAuthLibrary)) (BSTR *aRemoteDisplayAuthLibrary);
    STDMETHOD(COMSETTER(RemoteDisplayAuthLibrary)) (IN_BSTR aRemoteDisplayAuthLibrary);
    STDMETHOD(COMGETTER(WebServiceAuthLibrary)) (BSTR *aWebServiceAuthLibrary);
    STDMETHOD(COMSETTER(WebServiceAuthLibrary)) (IN_BSTR aWebServiceAuthLibrary);
    STDMETHOD(COMGETTER(LogHistoryCount)) (ULONG *count);
    STDMETHOD(COMSETTER(LogHistoryCount)) (ULONG count);
    STDMETHOD(COMGETTER(DefaultAudioDriver)) (AudioDriverType_T *aAudioDriver);

    STDMETHOD(GetMaxDevicesPerPortForStorageBus) (StorageBus_T aBus, ULONG *aMaxDevicesPerPort);
    STDMETHOD(GetMinPortCountForStorageBus) (StorageBus_T aBus, ULONG *aMinPortCount);
    STDMETHOD(GetMaxPortCountForStorageBus) (StorageBus_T aBus, ULONG *aMaxPortCount);
    STDMETHOD(GetMaxInstancesOfStorageBus)(StorageBus_T aBus, ULONG *aMaxInstances);
    STDMETHOD(GetDeviceTypesForStorageBus)(StorageBus_T aBus, ComSafeArrayOut(DeviceType_T, aDeviceTypes));
    STDMETHOD(GetDefaultIoCacheSettingForStorageController)(StorageControllerType_T aControllerType, BOOL *aEnabled);

    // public methods only for internal purposes

    HRESULT loadSettings(const settings::SystemProperties &data);
    HRESULT saveSettings(settings::SystemProperties &data);

    ComObjPtr<MediumFormat> mediumFormat(const Utf8Str &aFormat);

    // public methods for internal purposes only
    // (ensure there is a caller and a read lock before calling them!)

private:

    typedef std::list<ComObjPtr<MediumFormat> > MediumFormatList;

    HRESULT setDefaultMachineFolder(const Utf8Str &aPath);
    HRESULT setDefaultHardDiskFolder(const Utf8Str &aPath);
    HRESULT setDefaultHardDiskFormat(const Utf8Str &aFormat);

    HRESULT setRemoteDisplayAuthLibrary(const Utf8Str &aPath);
    HRESULT setWebServiceAuthLibrary(const Utf8Str &aPath);

    VirtualBox * const  mParent;

    settings::SystemProperties *m;

    Utf8Str             m_strDefaultMachineFolderFull;
    Utf8Str             m_strDefaultHardDiskFolderFull;

    MediumFormatList    m_llMediumFormats;

    friend class VirtualBox;
};

#endif // ____H_SYSTEMPROPERTIESIMPL
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
