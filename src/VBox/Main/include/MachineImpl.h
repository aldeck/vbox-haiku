/* $Id$ */

/** @file
 *
 * VirtualBox COM class declaration
 */

/*
 * Copyright (C) 2006-2010 Sun Microsystems, Inc.
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

#ifndef ____H_MACHINEIMPL
#define ____H_MACHINEIMPL

#include "VirtualBoxBase.h"
#include "SnapshotImpl.h"
#include "VRDPServerImpl.h"
#include "MediumAttachmentImpl.h"
#include "NetworkAdapterImpl.h"
#include "AudioAdapterImpl.h"
#include "SerialPortImpl.h"
#include "ParallelPortImpl.h"
#include "BIOSSettingsImpl.h"
#include "StorageControllerImpl.h"          // required for MachineImpl.h to compile on Windows
#include "VBox/settings.h"
#ifdef VBOX_WITH_RESOURCE_USAGE_API
#include "PerformanceImpl.h"
#endif /* VBOX_WITH_RESOURCE_USAGE_API */

// generated header
#include "SchemaDefs.h"

#include <VBox/types.h>

#include <iprt/file.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include <list>

// defines
////////////////////////////////////////////////////////////////////////////////

// helper declarations
////////////////////////////////////////////////////////////////////////////////

class Progress;
class Keyboard;
class Mouse;
class Display;
class MachineDebugger;
class USBController;
class Snapshot;
class SharedFolder;
class HostUSBDevice;
class StorageController;

class SessionMachine;

namespace settings
{
    class MachineConfigFile;
    struct Snapshot;
    struct Hardware;
    struct Storage;
    struct StorageController;
    struct MachineRegistryEntry;
}

// Machine class
////////////////////////////////////////////////////////////////////////////////

class ATL_NO_VTABLE Machine :
    public VirtualBoxBaseWithChildrenNEXT,
    public VirtualBoxSupportErrorInfoImpl<Machine, IMachine>,
    public VirtualBoxSupportTranslation<Machine>,
    VBOX_SCRIPTABLE_IMPL(IMachine)
{
    Q_OBJECT

public:

    enum InitMode { Init_New, Init_Import, Init_Registered };

    enum StateDependency
    {
        AnyStateDep = 0, MutableStateDep, MutableOrSavedStateDep
    };

    /**
     * Internal machine data.
     *
     * Only one instance of this data exists per every machine -- it is shared
     * by the Machine, SessionMachine and all SnapshotMachine instances
     * associated with the given machine using the util::Shareable template
     * through the mData variable.
     *
     * @note |const| members are persistent during lifetime so can be
     * accessed without locking.
     *
     * @note There is no need to lock anything inside init() or uninit()
     * methods, because they are always serialized (see AutoCaller).
     */
    struct Data
    {
        /**
         * Data structure to hold information about sessions opened for the
         * given machine.
         */
        struct Session
        {
            /** Control of the direct session opened by openSession() */
            ComPtr<IInternalSessionControl> mDirectControl;

            typedef std::list<ComPtr<IInternalSessionControl> > RemoteControlList;

            /** list of controls of all opened remote sessions */
            RemoteControlList mRemoteControls;

            /** openRemoteSession() and OnSessionEnd() progress indicator */
            ComObjPtr<Progress> mProgress;

            /**
             * PID of the session object that must be passed to openSession() to
             * finalize the openRemoteSession() request (i.e., PID of the
             * process created by openRemoteSession())
             */
            RTPROCESS mPid;

            /** Current session state */
            SessionState_T mState;

            /** Session type string (for indirect sessions) */
            Bstr mType;

            /** Session machine object */
            ComObjPtr<SessionMachine> mMachine;

            /**
             * Successfully locked media list. The 2nd value in the pair is true
             * if the medium is locked for writing and false if locked for
             * reading.
             */
            typedef std::list<std::pair<ComPtr<IMedium>, bool > > LockedMedia;
            LockedMedia mLockedMedia;
        };

        Data();
        ~Data();

        const Guid mUuid;
        BOOL mRegistered;
        InitMode mInitMode;

        /** Flag indicating that the config file is read-only. */
        BOOL mConfigFileReadonly;
        Utf8Str m_strConfigFile;
        Utf8Str m_strConfigFileFull;

        // machine settings XML file
        settings::MachineConfigFile *m_pMachineConfigFile;

        BOOL mAccessible;
        com::ErrorInfo mAccessError;

        MachineState_T mMachineState;
        RTTIMESPEC mLastStateChange;

        /* Note: These are guarded by VirtualBoxBase::stateLockHandle() */
        uint32_t mMachineStateDeps;
        RTSEMEVENTMULTI mMachineStateDepsSem;
        uint32_t mMachineStateChangePending;

        BOOL mCurrentStateModified;

        RTFILE mHandleCfgFile;

        Session mSession;

        ComObjPtr<Snapshot> mFirstSnapshot;
        ComObjPtr<Snapshot> mCurrentSnapshot;
    };

    /**
     *  Saved state data.
     *
     *  It's actually only the state file path string, but it needs to be
     *  separate from Data, because Machine and SessionMachine instances
     *  share it, while SnapshotMachine does not.
     *
     *  The data variable is |mSSData|.
     */
    struct SSData
    {
        Utf8Str mStateFilePath;
    };

    /**
     *  User changeable machine data.
     *
     *  This data is common for all machine snapshots, i.e. it is shared
     *  by all SnapshotMachine instances associated with the given machine
     *  using the util::Backupable template through the |mUserData| variable.
     *
     *  SessionMachine instances can alter this data and discard changes.
     *
     *  @note There is no need to lock anything inside init() or uninit()
     *  methods, because they are always serialized (see AutoCaller).
     */
    struct UserData
    {
        UserData();
        ~UserData();

        Bstr    mName;
        BOOL    mNameSync;
        Bstr    mDescription;
        Bstr    mOSTypeId;
        Bstr    mSnapshotFolder;
        Bstr    mSnapshotFolderFull;
        BOOL    mTeleporterEnabled;
        ULONG   mTeleporterPort;
        Bstr    mTeleporterAddress;
        Bstr    mTeleporterPassword;
        BOOL    mRTCUseUTC;
    };

    /**
     *  Hardware data.
     *
     *  This data is unique for a machine and for every machine snapshot.
     *  Stored using the util::Backupable template in the |mHWData| variable.
     *
     *  SessionMachine instances can alter this data and discard changes.
     */
    struct HWData
    {
        /**
         * Data structure to hold information about a guest property.
         */
        struct GuestProperty {
            /** Property name */
            Utf8Str strName;
            /** Property value */
            Utf8Str strValue;
            /** Property timestamp */
            ULONG64 mTimestamp;
            /** Property flags */
            ULONG mFlags;
        };

        HWData();
        ~HWData();

        Bstr           mHWVersion;
        Guid           mHardwareUUID;   /**< If Null, use mData.mUuid. */
        ULONG          mMemorySize;
        ULONG          mMemoryBalloonSize;
        ULONG          mStatisticsUpdateInterval;
        ULONG          mVRAMSize;
        ULONG          mMonitorCount;
        BOOL           mHWVirtExEnabled;
        BOOL           mHWVirtExExclusive;
        BOOL           mHWVirtExNestedPagingEnabled;
        BOOL           mHWVirtExVPIDEnabled;
        BOOL           mAccelerate2DVideoEnabled;
        BOOL           mPAEEnabled;
        BOOL           mSyntheticCpu;
        ULONG          mCPUCount;
        BOOL           mCPUHotPlugEnabled;
        BOOL           mAccelerate3DEnabled;

        BOOL           mCPUAttached[SchemaDefs::MaxCPUCount];

        settings::CpuIdLeaf mCpuIdStdLeafs[10];
        settings::CpuIdLeaf mCpuIdExtLeafs[10];

        DeviceType_T   mBootOrder[SchemaDefs::MaxBootPosition];

        typedef std::list< ComObjPtr<SharedFolder> > SharedFolderList;
        SharedFolderList mSharedFolders;

        ClipboardMode_T mClipboardMode;

        typedef std::list<GuestProperty> GuestPropertyList;
        GuestPropertyList mGuestProperties;
        BOOL           mPropertyServiceActive;
        Utf8Str        mGuestPropertyNotificationPatterns;

        FirmwareType_T mFirmwareType;
    };

    /**
     *  Hard disk and other media data.
     *
     *  The usage policy is the same as for HWData, but a separate structure
     *  is necessary because hard disk data requires different procedures when
     *  taking or discarding snapshots, etc.
     *
     *  The data variable is |mMediaData|.
     */
    struct MediaData
    {
        MediaData();
        ~MediaData();

        typedef std::list< ComObjPtr<MediumAttachment> > AttachmentList;
        AttachmentList mAttachments;
    };

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(Machine)

    DECLARE_NOT_AGGREGATABLE(Machine)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(Machine)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IMachine)
        COM_INTERFACE_ENTRY(IDispatch)
    END_COM_MAP()

    DECLARE_EMPTY_CTOR_DTOR(Machine)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(VirtualBox *aParent,
                 const Utf8Str &strConfigFile,
                 InitMode aMode,
                 CBSTR aName = NULL,
                 GuestOSType *aOsType = NULL,
                 BOOL aNameSync = TRUE,
                 const Guid *aId = NULL);
    void uninit();

protected:
    HRESULT initDataAndChildObjects();
    void uninitDataAndChildObjects();

public:
    // IMachine properties
    STDMETHOD(COMGETTER(Parent))(IVirtualBox **aParent);
    STDMETHOD(COMGETTER(Accessible))(BOOL *aAccessible);
    STDMETHOD(COMGETTER(AccessError))(IVirtualBoxErrorInfo **aAccessError);
    STDMETHOD(COMGETTER(Name))(BSTR *aName);
    STDMETHOD(COMSETTER(Name))(IN_BSTR aName);
    STDMETHOD(COMGETTER(Description))(BSTR *aDescription);
    STDMETHOD(COMSETTER(Description))(IN_BSTR aDescription);
    STDMETHOD(COMGETTER(Id))(BSTR *aId);
    STDMETHOD(COMGETTER(OSTypeId))(BSTR *aOSTypeId);
    STDMETHOD(COMSETTER(OSTypeId))(IN_BSTR aOSTypeId);
    STDMETHOD(COMGETTER(HardwareVersion))(BSTR *aVersion);
    STDMETHOD(COMSETTER(HardwareVersion))(IN_BSTR aVersion);
    STDMETHOD(COMGETTER(HardwareUUID))(BSTR *aUUID);
    STDMETHOD(COMSETTER(HardwareUUID))(IN_BSTR aUUID);
    STDMETHOD(COMGETTER(MemorySize))(ULONG *memorySize);
    STDMETHOD(COMSETTER(MemorySize))(ULONG memorySize);
    STDMETHOD(COMGETTER(CPUCount))(ULONG *cpuCount);
    STDMETHOD(COMSETTER(CPUCount))(ULONG cpuCount);
    STDMETHOD(COMGETTER(CPUHotPlugEnabled))(BOOL *enabled);
    STDMETHOD(COMSETTER(CPUHotPlugEnabled))(BOOL enabled);
    STDMETHOD(COMGETTER(MemoryBalloonSize))(ULONG *memoryBalloonSize);
    STDMETHOD(COMSETTER(MemoryBalloonSize))(ULONG memoryBalloonSize);
    STDMETHOD(COMGETTER(StatisticsUpdateInterval))(ULONG *statisticsUpdateInterval);
    STDMETHOD(COMSETTER(StatisticsUpdateInterval))(ULONG statisticsUpdateInterval);
    STDMETHOD(COMGETTER(VRAMSize))(ULONG *memorySize);
    STDMETHOD(COMSETTER(VRAMSize))(ULONG memorySize);
    STDMETHOD(COMGETTER(MonitorCount))(ULONG *monitorCount);
    STDMETHOD(COMSETTER(MonitorCount))(ULONG monitorCount);
    STDMETHOD(COMGETTER(Accelerate3DEnabled))(BOOL *enabled);
    STDMETHOD(COMSETTER(Accelerate3DEnabled))(BOOL enabled);
    STDMETHOD(COMGETTER(Accelerate2DVideoEnabled))(BOOL *enabled);
    STDMETHOD(COMSETTER(Accelerate2DVideoEnabled))(BOOL enabled);
    STDMETHOD(COMGETTER(BIOSSettings))(IBIOSSettings **biosSettings);
    STDMETHOD(COMGETTER(SnapshotFolder))(BSTR *aSavedStateFolder);
    STDMETHOD(COMSETTER(SnapshotFolder))(IN_BSTR aSavedStateFolder);
    STDMETHOD(COMGETTER(MediumAttachments))(ComSafeArrayOut(IMediumAttachment *, aAttachments));
    STDMETHOD(COMGETTER(VRDPServer))(IVRDPServer **vrdpServer);
    STDMETHOD(COMGETTER(AudioAdapter))(IAudioAdapter **audioAdapter);
    STDMETHOD(COMGETTER(USBController))(IUSBController * *aUSBController);
    STDMETHOD(COMGETTER(SettingsFilePath))(BSTR *aFilePath);
    STDMETHOD(COMGETTER(SettingsModified))(BOOL *aModified);
    STDMETHOD(COMGETTER(SessionState))(SessionState_T *aSessionState);
    STDMETHOD(COMGETTER(SessionType))(BSTR *aSessionType);
    STDMETHOD(COMGETTER(SessionPid))(ULONG *aSessionPid);
    STDMETHOD(COMGETTER(State))(MachineState_T *machineState);
    STDMETHOD(COMGETTER(LastStateChange))(LONG64 *aLastStateChange);
    STDMETHOD(COMGETTER(StateFilePath))(BSTR *aStateFilePath);
    STDMETHOD(COMGETTER(LogFolder))(BSTR *aLogFolder);
    STDMETHOD(COMGETTER(CurrentSnapshot))(ISnapshot **aCurrentSnapshot);
    STDMETHOD(COMGETTER(SnapshotCount))(ULONG *aSnapshotCount);
    STDMETHOD(COMGETTER(CurrentStateModified))(BOOL *aCurrentStateModified);
    STDMETHOD(COMGETTER(SharedFolders))(ComSafeArrayOut(ISharedFolder *, aSharedFolders));
    STDMETHOD(COMGETTER(ClipboardMode))(ClipboardMode_T *aClipboardMode);
    STDMETHOD(COMSETTER(ClipboardMode))(ClipboardMode_T aClipboardMode);
    STDMETHOD(COMGETTER(GuestPropertyNotificationPatterns))(BSTR *aPattern);
    STDMETHOD(COMSETTER(GuestPropertyNotificationPatterns))(IN_BSTR aPattern);
    STDMETHOD(COMGETTER(StorageControllers))(ComSafeArrayOut(IStorageController *, aStorageControllers));
    STDMETHOD(COMGETTER(TeleporterEnabled))(BOOL *aEnabled);
    STDMETHOD(COMSETTER(TeleporterEnabled))(BOOL aEnabled);
    STDMETHOD(COMGETTER(TeleporterPort))(ULONG *aPort);
    STDMETHOD(COMSETTER(TeleporterPort))(ULONG aPort);
    STDMETHOD(COMGETTER(TeleporterAddress))(BSTR *aAddress);
    STDMETHOD(COMSETTER(TeleporterAddress))(IN_BSTR aAddress);
    STDMETHOD(COMGETTER(TeleporterPassword))(BSTR *aPassword);
    STDMETHOD(COMSETTER(TeleporterPassword))(IN_BSTR aPassword);
    STDMETHOD(COMGETTER(RTCUseUTC))(BOOL *aEnabled);
    STDMETHOD(COMSETTER(RTCUseUTC))(BOOL aEnabled);

    // IMachine methods
    STDMETHOD(SetBootOrder)(ULONG aPosition, DeviceType_T aDevice);
    STDMETHOD(GetBootOrder)(ULONG aPosition, DeviceType_T *aDevice);
    STDMETHOD(AttachDevice)(IN_BSTR aControllerName, LONG aControllerPort,
                            LONG aDevice, DeviceType_T aType, IN_BSTR aId);
    STDMETHOD(DetachDevice)(IN_BSTR aControllerName, LONG aControllerPort, LONG aDevice);
    STDMETHOD(PassthroughDevice)(IN_BSTR aControllerName, LONG aControllerPort, LONG aDevice, BOOL aPassthrough);
    STDMETHOD(MountMedium)(IN_BSTR aControllerName, LONG aControllerPort,
                           LONG aDevice, IN_BSTR aId, BOOL aForce);
    STDMETHOD(GetMedium)(IN_BSTR aControllerName, LONG aControllerPort, LONG aDevice,
                         IMedium **aMedium);
    STDMETHOD(GetSerialPort)(ULONG slot, ISerialPort **port);
    STDMETHOD(GetParallelPort)(ULONG slot, IParallelPort **port);
    STDMETHOD(GetNetworkAdapter)(ULONG slot, INetworkAdapter **adapter);
    STDMETHOD(GetExtraDataKeys)(ComSafeArrayOut(BSTR, aKeys));
    STDMETHOD(GetExtraData)(IN_BSTR aKey, BSTR *aValue);
    STDMETHOD(SetExtraData)(IN_BSTR aKey, IN_BSTR aValue);
    STDMETHOD(GetCpuProperty)(CpuPropertyType_T property, BOOL *aVal);
    STDMETHOD(SetCpuProperty)(CpuPropertyType_T property, BOOL aVal);
    STDMETHOD(GetCpuIdLeaf)(ULONG id, ULONG *aValEax, ULONG *aValEbx, ULONG *aValEcx, ULONG *aValEdx);
    STDMETHOD(SetCpuIdLeaf)(ULONG id, ULONG aValEax, ULONG aValEbx, ULONG aValEcx, ULONG aValEdx);
    STDMETHOD(RemoveCpuIdLeaf)(ULONG id);
    STDMETHOD(RemoveAllCpuIdLeafs)();
    STDMETHOD(GetHWVirtExProperty)(HWVirtExPropertyType_T property, BOOL *aVal);
    STDMETHOD(SetHWVirtExProperty)(HWVirtExPropertyType_T property, BOOL aVal);
    STDMETHOD(SaveSettings)();
    STDMETHOD(DiscardSettings)();
    STDMETHOD(DeleteSettings)();
    STDMETHOD(Export)(IAppliance *aAppliance, IVirtualSystemDescription **aDescription);
    STDMETHOD(GetSnapshot)(IN_BSTR aId, ISnapshot **aSnapshot);
    STDMETHOD(FindSnapshot)(IN_BSTR aName, ISnapshot **aSnapshot);
    STDMETHOD(SetCurrentSnapshot)(IN_BSTR aId);
    STDMETHOD(CreateSharedFolder)(IN_BSTR aName, IN_BSTR aHostPath, BOOL aWritable);
    STDMETHOD(RemoveSharedFolder)(IN_BSTR aName);
    STDMETHOD(CanShowConsoleWindow)(BOOL *aCanShow);
    STDMETHOD(ShowConsoleWindow)(ULONG64 *aWinId);
    STDMETHOD(GetGuestProperty)(IN_BSTR aName, BSTR *aValue, ULONG64 *aTimestamp, BSTR *aFlags);
    STDMETHOD(GetGuestPropertyValue)(IN_BSTR aName, BSTR *aValue);
    STDMETHOD(GetGuestPropertyTimestamp)(IN_BSTR aName, ULONG64 *aTimestamp);
    STDMETHOD(SetGuestProperty)(IN_BSTR aName, IN_BSTR aValue, IN_BSTR aFlags);
    STDMETHOD(SetGuestPropertyValue)(IN_BSTR aName, IN_BSTR aValue);
    STDMETHOD(EnumerateGuestProperties)(IN_BSTR aPattern, ComSafeArrayOut(BSTR, aNames), ComSafeArrayOut(BSTR, aValues), ComSafeArrayOut(ULONG64, aTimestamps), ComSafeArrayOut(BSTR, aFlags));
    STDMETHOD(GetMediumAttachmentsOfController)(IN_BSTR aName, ComSafeArrayOut(IMediumAttachment *, aAttachments));
    STDMETHOD(GetMediumAttachment)(IN_BSTR aConstrollerName, LONG aControllerPort, LONG aDevice, IMediumAttachment **aAttachment);
    STDMETHOD(AddStorageController)(IN_BSTR aName, StorageBus_T aConnectionType, IStorageController **controller);
    STDMETHOD(RemoveStorageController(IN_BSTR aName));
    STDMETHOD(GetStorageControllerByName(IN_BSTR aName, IStorageController **storageController));
    STDMETHOD(GetStorageControllerByInstance(ULONG aInstance, IStorageController **storageController));
    STDMETHOD(COMGETTER(FirmwareType)) (FirmwareType_T *aFirmware);
    STDMETHOD(COMSETTER(FirmwareType)) (FirmwareType_T  aFirmware);

    STDMETHOD(QuerySavedThumbnailSize)(ULONG *aSize, ULONG *aWidth, ULONG *aHeight);
    STDMETHOD(ReadSavedThumbnailToArray)(BOOL aBGR, ULONG *aWidth, ULONG *aHeight, ComSafeArrayOut(BYTE, aData));
    STDMETHOD(QuerySavedScreenshotPNGSize)(ULONG *aSize, ULONG *aWidth, ULONG *aHeight);
    STDMETHOD(ReadSavedScreenshotPNGToArray)(ULONG *aWidth, ULONG *aHeight, ComSafeArrayOut(BYTE, aData));
    STDMETHOD(HotPlugCPU(ULONG aCpu));
    STDMETHOD(HotUnplugCPU(ULONG aCpu));
    STDMETHOD(GetCPUStatus(ULONG aCpu, BOOL *aCpuAttached));

    // public methods only for internal purposes

    /**
     * Simple run-time type identification without having to enable C++ RTTI.
     * The class IDs are defined in VirtualBoxBase.h.
     * @return
     */
    virtual VBoxClsID getClassID() const
    {
        return clsidMachine;
    }

    /**
     * Override of the default locking class to be used for validating lock
     * order with the standard member lock handle.
     */
    virtual VBoxLockingClass getLockingClass() const
    {
        return LOCKCLASS_MACHINEOBJECT;
    }

    /// @todo (dmik) add lock and make non-inlined after revising classes
    //  that use it. Note: they should enter Machine lock to keep the returned
    //  information valid!
    bool isRegistered() { return !!mData->mRegistered; }

    // unsafe inline public methods for internal purposes only (ensure there is
    // a caller and a read lock before calling them!)

    /**
     * Returns the VirtualBox object this machine belongs to.
     *
     * @note This method doesn't check this object's readiness. Intended to be
     * used by ready Machine children (whose readiness is bound to the parent's
     * one) or after doing addCaller() manually.
     */
    const ComObjPtr<VirtualBox, ComWeakRef>& getVirtualBox() const { return mParent; }

    /**
     * Returns this machine ID.
     *
     * @note This method doesn't check this object's readiness. Intended to be
     * used by ready Machine children (whose readiness is bound to the parent's
     * one) or after adding a caller manually.
     */
    const Guid& getId() const { return mData->mUuid; }

    /**
     * Returns the snapshot ID this machine represents or an empty UUID if this
     * instance is not SnapshotMachine.
     *
     * @note This method doesn't check this object's readiness. Intended to be
     * used by ready Machine children (whose readiness is bound to the parent's
     * one) or after adding a caller manually.
     */
    inline const Guid& getSnapshotId() const;

    /**
     * Returns this machine's full settings file path.
     *
     * @note This method doesn't lock this object or check its readiness.
     * Intended to be used only after doing addCaller() manually and locking it
     * for reading.
     */
    const Utf8Str& getSettingsFileFull() const { return mData->m_strConfigFileFull; }

    /**
     * Returns this machine name.
     *
     * @note This method doesn't lock this object or check its readiness.
     * Intended to be used only after doing addCaller() manually and locking it
     * for reading.
     */
    const Bstr& getName() const { return mUserData->mName; }

    enum
    {
        IsModified_MachineData          = 0x0001,
        IsModified_Storage              = 0x0002,
        IsModified_NetworkAdapters      = 0x0008,
        IsModified_SerialPorts          = 0x0010,
        IsModified_ParallelPorts        = 0x0020,
        IsModified_VRDPServer           = 0x0040,
        IsModified_AudioAdapter         = 0x0080,
        IsModified_USB                  = 0x0100,
        IsModified_BIOS                 = 0x0200,
        IsModified_SharedFolders        = 0x0400
    };

    void setModified(uint32_t fl);

    // callback handlers
    virtual HRESULT onNetworkAdapterChange(INetworkAdapter * /* networkAdapter */, BOOL /* changeAdapter */) { return S_OK; }
    virtual HRESULT onSerialPortChange(ISerialPort * /* serialPort */) { return S_OK; }
    virtual HRESULT onParallelPortChange(IParallelPort * /* parallelPort */) { return S_OK; }
    virtual HRESULT onVRDPServerChange() { return S_OK; }
    virtual HRESULT onUSBControllerChange() { return S_OK; }
    virtual HRESULT onStorageControllerChange() { return S_OK; }
    virtual HRESULT onCPUChange(ULONG /* aCPU */, BOOL /* aRemove */) { return S_OK; }
    virtual HRESULT onMediumChange(IMediumAttachment * /* mediumAttachment */, BOOL /* force */) { return S_OK; }
    virtual HRESULT onSharedFolderChange() { return S_OK; }

    HRESULT saveRegistryEntry(settings::MachineRegistryEntry &data);

    int calculateFullPath(const Utf8Str &strPath, Utf8Str &aResult);
    void calculateRelativePath(const Utf8Str &strPath, Utf8Str &aResult);

    void getLogFolder(Utf8Str &aLogFolder);

    HRESULT openSession(IInternalSessionControl *aControl);
    HRESULT openRemoteSession(IInternalSessionControl *aControl,
                              IN_BSTR aType, IN_BSTR aEnvironment,
                              Progress *aProgress);
    HRESULT openExistingSession(IInternalSessionControl *aControl);

#if defined(RT_OS_WINDOWS)

    bool isSessionOpen(ComObjPtr<SessionMachine> &aMachine,
                       ComPtr<IInternalSessionControl> *aControl = NULL,
                       HANDLE *aIPCSem = NULL, bool aAllowClosing = false);
    bool isSessionSpawning(RTPROCESS *aPID = NULL);

    bool isSessionOpenOrClosing(ComObjPtr<SessionMachine> &aMachine,
                                ComPtr<IInternalSessionControl> *aControl = NULL,
                                HANDLE *aIPCSem = NULL)
    { return isSessionOpen(aMachine, aControl, aIPCSem, true /* aAllowClosing */); }

#elif defined(RT_OS_OS2)

    bool isSessionOpen(ComObjPtr<SessionMachine> &aMachine,
                       ComPtr<IInternalSessionControl> *aControl = NULL,
                       HMTX *aIPCSem = NULL, bool aAllowClosing = false);

    bool isSessionSpawning(RTPROCESS *aPID = NULL);

    bool isSessionOpenOrClosing(ComObjPtr<SessionMachine> &aMachine,
                                 ComPtr<IInternalSessionControl> *aControl = NULL,
                                 HMTX *aIPCSem = NULL)
    { return isSessionOpen(aMachine, aControl, aIPCSem, true /* aAllowClosing */); }

#else

    bool isSessionOpen(ComObjPtr<SessionMachine> &aMachine,
                       ComPtr<IInternalSessionControl> *aControl = NULL,
                       bool aAllowClosing = false);
    bool isSessionSpawning();

    bool isSessionOpenOrClosing(ComObjPtr<SessionMachine> &aMachine,
                                ComPtr<IInternalSessionControl> *aControl = NULL)
    { return isSessionOpen(aMachine, aControl, true /* aAllowClosing */); }

#endif

    bool checkForSpawnFailure();

    HRESULT trySetRegistered(BOOL aRegistered);

    HRESULT getSharedFolder(CBSTR aName,
                            ComObjPtr<SharedFolder> &aSharedFolder,
                            bool aSetError = false)
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        return findSharedFolder(aName, aSharedFolder, aSetError);
    }

    HRESULT addStateDependency(StateDependency aDepType = AnyStateDep,
                               MachineState_T *aState = NULL,
                               BOOL *aRegistered = NULL);
    void releaseStateDependency();

    // for VirtualBoxSupportErrorInfoImpl
    static const wchar_t *getComponentName() { return L"Machine"; }

protected:

    HRESULT registeredInit();

    HRESULT checkStateDependency(StateDependency aDepType);

    inline Machine *getMachine();

    void ensureNoStateDependencies();

    virtual HRESULT setMachineState(MachineState_T aMachineState);

    HRESULT findSharedFolder(CBSTR aName,
                             ComObjPtr<SharedFolder> &aSharedFolder,
                             bool aSetError = false);

    HRESULT loadSettings(bool aRegistered);
    HRESULT loadSnapshot(const settings::Snapshot &data,
                         const Guid &aCurSnapshotId,
                         Snapshot *aParentSnapshot);
    HRESULT loadHardware(const settings::Hardware &data);
    HRESULT loadStorageControllers(const settings::Storage &data,
                                   bool aRegistered,
                                   const Guid *aSnapshotId = NULL);
    HRESULT loadStorageDevices(StorageController *aStorageController,
                               const settings::StorageController &data,
                               bool aRegistered,
                               const Guid *aSnapshotId = NULL);

    HRESULT findSnapshot(const Guid &aId, ComObjPtr<Snapshot> &aSnapshot,
                         bool aSetError = false);
    HRESULT findSnapshot(IN_BSTR aName, ComObjPtr<Snapshot> &aSnapshot,
                         bool aSetError = false);

    HRESULT getStorageControllerByName(const Utf8Str &aName,
                                       ComObjPtr<StorageController> &aStorageController,
                                       bool aSetError = false);

    HRESULT getMediumAttachmentsOfController(CBSTR aName,
                                             MediaData::AttachmentList &aAttachments);

    enum
    {
        /* flags for #saveSettings() */
        SaveS_ResetCurStateModified = 0x01,
        SaveS_InformCallbacksAnyway = 0x02,
        /* flags for #saveSnapshotSettings() */
        SaveSS_CurStateModified = 0x40,
        SaveSS_CurrentId = 0x80,
        /* flags for #saveStateSettings() */
        SaveSTS_CurStateModified = 0x20,
        SaveSTS_StateFilePath = 0x40,
        SaveSTS_StateTimeStamp = 0x80,
    };

    HRESULT prepareSaveSettings(bool &aRenamed, bool &aNew);
    HRESULT saveSettings(int aFlags = 0);

    HRESULT saveAllSnapshots();

    HRESULT saveHardware(settings::Hardware &data);
    HRESULT saveStorageControllers(settings::Storage &data);
    HRESULT saveStorageDevices(ComObjPtr<StorageController> aStorageController,
                               settings::StorageController &data);

    HRESULT saveStateSettings(int aFlags);

    HRESULT createImplicitDiffs(const Bstr &aFolder,
                                IProgress *aProgress,
                                ULONG aWeight,
                                bool aOnline,
                                bool *pfNeedsSaveSettings);
    HRESULT deleteImplicitDiffs(bool *pfNeedsSaveSettings);

    MediumAttachment* findAttachment(const MediaData::AttachmentList &ll,
                                     IN_BSTR aControllerName,
                                     LONG aControllerPort,
                                     LONG aDevice);
    MediumAttachment* findAttachment(const MediaData::AttachmentList &ll,
                                     ComObjPtr<Medium> pMedium);
    MediumAttachment* findAttachment(const MediaData::AttachmentList &ll,
                                     Guid &id);

    void commitMedia(bool aOnline = false);
    void rollbackMedia();

    bool isInOwnDir(Utf8Str *aSettingsDir = NULL);

    void rollback(bool aNotify);
    void commit();
    void copyFrom(Machine *aThat);

#ifdef VBOX_WITH_RESOURCE_USAGE_API
    void registerMetrics(PerformanceCollector *aCollector, Machine *aMachine, RTPROCESS pid);
    void unregisterMetrics(PerformanceCollector *aCollector, Machine *aMachine);
#endif /* VBOX_WITH_RESOURCE_USAGE_API */

    const ComObjPtr<Machine, ComWeakRef> mPeer;

    const ComObjPtr<VirtualBox, ComWeakRef> mParent;

    uint32_t                m_flModifications;

    Shareable<Data>         mData;
    Shareable<SSData>       mSSData;

    Backupable<UserData>    mUserData;
    Backupable<HWData>      mHWData;
    Backupable<MediaData>   mMediaData;

    // the following fields need special backup/rollback/commit handling,
    // so they cannot be a part of HWData

    const ComObjPtr<VRDPServer>     mVRDPServer;
    const ComObjPtr<SerialPort>     mSerialPorts[SchemaDefs::SerialPortCount];
    const ComObjPtr<ParallelPort>   mParallelPorts[SchemaDefs::ParallelPortCount];
    const ComObjPtr<AudioAdapter>   mAudioAdapter;
    const ComObjPtr<USBController>  mUSBController;
    const ComObjPtr<BIOSSettings>   mBIOSSettings;
    const ComObjPtr<NetworkAdapter> mNetworkAdapters[SchemaDefs::NetworkAdapterCount];

    typedef std::list< ComObjPtr<StorageController> > StorageControllerList;
    Backupable<StorageControllerList> mStorageControllers;

    friend class SessionMachine;
    friend class SnapshotMachine;
};

// SessionMachine class
////////////////////////////////////////////////////////////////////////////////

/**
 *  @note Notes on locking objects of this class:
 *  SessionMachine shares some data with the primary Machine instance (pointed
 *  to by the |mPeer| member). In order to provide data consistency it also
 *  shares its lock handle. This means that whenever you lock a SessionMachine
 *  instance using Auto[Reader]Lock or AutoMultiLock, the corresponding Machine
 *  instance is also locked in the same lock mode. Keep it in mind.
 */
class ATL_NO_VTABLE SessionMachine :
    public VirtualBoxSupportTranslation<SessionMachine>,
    public Machine,
    VBOX_SCRIPTABLE_IMPL(IInternalMachineControl)
{
public:

    VIRTUALBOXSUPPORTTRANSLATION_OVERRIDE(SessionMachine)

    DECLARE_NOT_AGGREGATABLE(SessionMachine)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(SessionMachine)
        COM_INTERFACE_ENTRY2(IDispatch, IMachine)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IMachine)
        COM_INTERFACE_ENTRY(IInternalMachineControl)
    END_COM_MAP()

    DECLARE_EMPTY_CTOR_DTOR(SessionMachine)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *aMachine);
    void uninit() { uninit(Uninit::Unexpected); }

    // util::Lockable interface
    RWLockHandle *lockHandle() const;

    // IInternalMachineControl methods
    STDMETHOD(SetRemoveSavedState)(BOOL aRemove);
    STDMETHOD(UpdateState)(MachineState_T machineState);
    STDMETHOD(GetIPCId)(BSTR *id);
    STDMETHOD(RunUSBDeviceFilters)(IUSBDevice *aUSBDevice, BOOL *aMatched, ULONG *aMaskedIfs);
    STDMETHOD(CaptureUSBDevice)(IN_BSTR aId);
    STDMETHOD(DetachUSBDevice)(IN_BSTR aId, BOOL aDone);
    STDMETHOD(AutoCaptureUSBDevices)();
    STDMETHOD(DetachAllUSBDevices)(BOOL aDone);
    STDMETHOD(OnSessionEnd)(ISession *aSession, IProgress **aProgress);
    STDMETHOD(BeginSavingState)(IProgress *aProgress, BSTR *aStateFilePath);
    STDMETHOD(EndSavingState)(BOOL aSuccess);
    STDMETHOD(AdoptSavedState)(IN_BSTR aSavedStateFile);
    STDMETHOD(BeginTakingSnapshot)(IConsole *aInitiator,
                                   IN_BSTR aName,
                                   IN_BSTR aDescription,
                                   IProgress *aConsoleProgress,
                                   BOOL fTakingSnapshotOnline,
                                   BSTR *aStateFilePath);
    STDMETHOD(EndTakingSnapshot)(BOOL aSuccess);
    STDMETHOD(DeleteSnapshot)(IConsole *aInitiator, IN_BSTR aId,
                              MachineState_T *aMachineState, IProgress **aProgress);
    STDMETHOD(RestoreSnapshot)(IConsole *aInitiator,
                               ISnapshot *aSnapshot,
                               MachineState_T *aMachineState,
                               IProgress **aProgress);
    STDMETHOD(PullGuestProperties)(ComSafeArrayOut(BSTR, aNames), ComSafeArrayOut(BSTR, aValues),
              ComSafeArrayOut(ULONG64, aTimestamps), ComSafeArrayOut(BSTR, aFlags));
    STDMETHOD(PushGuestProperties)(ComSafeArrayIn(IN_BSTR, aNames), ComSafeArrayIn(IN_BSTR, aValues),
              ComSafeArrayIn(ULONG64, aTimestamps), ComSafeArrayIn(IN_BSTR, aFlags));
    STDMETHOD(PushGuestProperty)(IN_BSTR aName, IN_BSTR aValue,
                                  ULONG64 aTimestamp, IN_BSTR aFlags);
    STDMETHOD(LockMedia)()   { return lockMedia(); }
    STDMETHOD(UnlockMedia)() { unlockMedia(); return S_OK; }

    // public methods only for internal purposes

    /**
     * Simple run-time type identification without having to enable C++ RTTI.
     * The class IDs are defined in VirtualBoxBase.h.
     * @return
     */
    virtual VBoxClsID getClassID() const
    {
        return clsidSessionMachine;
    }

    bool checkForDeath();

    HRESULT onNetworkAdapterChange(INetworkAdapter *networkAdapter, BOOL changeAdapter);
    HRESULT onStorageControllerChange();
    HRESULT onMediumChange(IMediumAttachment *aMediumAttachment, BOOL aForce);
    HRESULT onSerialPortChange(ISerialPort *serialPort);
    HRESULT onParallelPortChange(IParallelPort *parallelPort);
    HRESULT onCPUChange(ULONG aCPU, BOOL aRemove);
    HRESULT onVRDPServerChange();
    HRESULT onUSBControllerChange();
    HRESULT onUSBDeviceAttach(IUSBDevice *aDevice,
                              IVirtualBoxErrorInfo *aError,
                              ULONG aMaskedIfs);
    HRESULT onUSBDeviceDetach(IN_BSTR aId,
                              IVirtualBoxErrorInfo *aError);
    HRESULT onSharedFolderChange();

    bool hasMatchingUSBFilter(const ComObjPtr<HostUSBDevice> &aDevice, ULONG *aMaskedIfs);

private:

    struct SnapshotData
    {
        SnapshotData() : mLastState(MachineState_Null) {}

        MachineState_T mLastState;

        // used when taking snapshot
        ComObjPtr<Snapshot> mSnapshot;

        // used when saving state
        Guid mProgressId;
        Utf8Str mStateFilePath;
    };

    struct Uninit
    {
        enum Reason { Unexpected, Abnormal, Normal };
    };

    struct SnapshotTask;
    struct DeleteSnapshotTask;
    struct RestoreSnapshotTask;

    friend struct DeleteSnapshotTask;
    friend struct RestoreSnapshotTask;

    void uninit(Uninit::Reason aReason);

    HRESULT endSavingState(BOOL aSuccess);

    typedef std::map<ComObjPtr<Machine>, MachineState_T> AffectedMachines;

    void deleteSnapshotHandler(DeleteSnapshotTask &aTask);
    void restoreSnapshotHandler(RestoreSnapshotTask &aTask);

    HRESULT lockMedia();
    void unlockMedia();

    HRESULT setMachineState(MachineState_T aMachineState);
    HRESULT updateMachineStateOnClient();

    HRESULT mRemoveSavedState;

    SnapshotData mSnapshotData;

    /** interprocess semaphore handle for this machine */
#if defined(RT_OS_WINDOWS)
    HANDLE mIPCSem;
    Bstr mIPCSemName;
    friend bool Machine::isSessionOpen(ComObjPtr<SessionMachine> &aMachine,
                                       ComPtr<IInternalSessionControl> *aControl,
                                       HANDLE *aIPCSem, bool aAllowClosing);
#elif defined(RT_OS_OS2)
    HMTX mIPCSem;
    Bstr mIPCSemName;
    friend bool Machine::isSessionOpen(ComObjPtr<SessionMachine> &aMachine,
                                       ComPtr<IInternalSessionControl> *aControl,
                                       HMTX *aIPCSem, bool aAllowClosing);
#elif defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER)
    int mIPCSem;
# ifdef VBOX_WITH_NEW_SYS_V_KEYGEN
    Bstr mIPCKey;
# endif /*VBOX_WITH_NEW_SYS_V_KEYGEN */
#else
# error "Port me!"
#endif

    static DECLCALLBACK(int) taskHandler(RTTHREAD thread, void *pvUser);
};

// SnapshotMachine class
////////////////////////////////////////////////////////////////////////////////

/**
 *  @note Notes on locking objects of this class:
 *  SnapshotMachine shares some data with the primary Machine instance (pointed
 *  to by the |mPeer| member). In order to provide data consistency it also
 *  shares its lock handle. This means that whenever you lock a SessionMachine
 *  instance using Auto[Reader]Lock or AutoMultiLock, the corresponding Machine
 *  instance is also locked in the same lock mode. Keep it in mind.
 */
class ATL_NO_VTABLE SnapshotMachine :
    public VirtualBoxSupportTranslation<SnapshotMachine>,
    public Machine
{
public:

    VIRTUALBOXSUPPORTTRANSLATION_OVERRIDE(SnapshotMachine)

    DECLARE_NOT_AGGREGATABLE(SnapshotMachine)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(SnapshotMachine)
        COM_INTERFACE_ENTRY2(IDispatch, IMachine)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IMachine)
    END_COM_MAP()

    DECLARE_EMPTY_CTOR_DTOR(SnapshotMachine)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(SessionMachine *aSessionMachine,
                 IN_GUID aSnapshotId,
                 const Utf8Str &aStateFilePath);
    HRESULT init(Machine *aMachine,
                 const settings::Hardware &hardware,
                 const settings::Storage &storage,
                 IN_GUID aSnapshotId,
                 const Utf8Str &aStateFilePath);
    void uninit();

    // util::Lockable interface
    RWLockHandle *lockHandle() const;

    // public methods only for internal purposes

    /**
     * Simple run-time type identification without having to enable C++ RTTI.
     * The class IDs are defined in VirtualBoxBase.h.
     * @return
     */
    virtual VBoxClsID getClassID() const
    {
        return clsidSnapshotMachine;
    }

    HRESULT onSnapshotChange(Snapshot *aSnapshot);

    // unsafe inline public methods for internal purposes only (ensure there is
    // a caller and a read lock before calling them!)

    const Guid& getSnapshotId() const { return mSnapshotId; }

private:

    Guid mSnapshotId;

    friend class Snapshot;
};

// third party methods that depend on SnapshotMachine definiton

inline const Guid &Machine::getSnapshotId() const
{
    return getClassID() != clsidSnapshotMachine
                ? Guid::Empty
                : static_cast<const SnapshotMachine*>(this)->getSnapshotId();
}

////////////////////////////////////////////////////////////////////////////////

/**
 *  Returns a pointer to the Machine object for this machine that acts like a
 *  parent for complex machine data objects such as shared folders, etc.
 *
 *  For primary Machine objects and for SnapshotMachine objects, returns this
 *  object's pointer itself. For SessoinMachine objects, returns the peer
 *  (primary) machine pointer.
 */
inline Machine *Machine::getMachine()
{
    if (getClassID() == clsidSessionMachine)
        return mPeer;
    return this;
}


#endif // ____H_MACHINEIMPL
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
