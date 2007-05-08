/** @file
 *
 * VirtualBox COM class declaration
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

#ifndef ____H_MACHINEIMPL
#define ____H_MACHINEIMPL

#include "VirtualBoxBase.h"
#include "VirtualBoxXMLUtil.h"
#include "ProgressImpl.h"
#include "SnapshotImpl.h"
#include "VRDPServerImpl.h"
#include "DVDDriveImpl.h"
#include "FloppyDriveImpl.h"
#include "HardDiskAttachmentImpl.h"
#include "Collection.h"
#include "NetworkAdapterImpl.h"
#include "AudioAdapterImpl.h"
#include "BIOSSettingsImpl.h"

// generated header
#include "SchemaDefs.h"

#include <VBox/types.h>
#include <VBox/cfgldr.h>
#include <iprt/file.h>
#include <iprt/thread.h>

#include <list>

// defines
////////////////////////////////////////////////////////////////////////////////

/**
 *  Checks whether the given Machine object is mutable (allows for calling setters)
 *  or not. When the machine is not mutable, sets error info and returns E_ACCESSDENIED.
 *  The translatable error message is defined in null context.
 *
 *  This macro <b>must</b> be used within setters of all Machine children
 *  (DVDDrive, NetworkAdapter, AudioAdapter, etc.).
 *
 *  @param machine  the machine object (must cast to Machine *)
 */
#define CHECK_MACHINE_MUTABILITY(machine) \
    do { \
        if (!machine->isMutable()) \
            return setError (E_ACCESSDENIED, tr ("The machine is not mutable")); \
    } while (0)
/** like CHECK_MACHINE_MUTABILITY but a saved state is ok, too */
#define CHECK_MACHINE_MUTABILITY_IGNORING_SAVED(machine) \
    do { \
        if (!machine->isMutableIgnoringSavedState()) \
            return setError (E_ACCESSDENIED, tr ("The machine is not mutable or in saved state")); \
    } while (0)


// helper declarations
////////////////////////////////////////////////////////////////////////////////

class VirtualBox;
class Progress;
class CombinedProgress;
class Keyboard;
class Mouse;
class Display;
class MachineDebugger;
class USBController;
class Snapshot;
class SharedFolder;

class SessionMachine;

// Machine class
////////////////////////////////////////////////////////////////////////////////

class ATL_NO_VTABLE Machine :
    public VirtualBoxBaseWithChildrenNEXT,
    public VirtualBoxXMLUtil,
    public VirtualBoxSupportErrorInfoImpl <Machine, IMachine>,
    public VirtualBoxSupportTranslation <Machine>,
    public IMachine
{
    Q_OBJECT

public:

    /**
     *  Internal machine data.
     *
     *  Only one instance of this data exists per every machine --
     *  it is shared by the Machine, SessionMachine and all SnapshotMachine
     *  instances associated with the given machine using the util::Shareable
     *  template through the mData variable.
     *
     *  @note |const| members are persistent during lifetime so can be
     *  accessed without locking.
     *
     *  @note There is no need to lock anything inside init() or uninit()
     *  methods, because they are always serialized (see AutoCaller).
     */
    struct Data
    {
        /**
         *  Data structure to hold information about sessions opened for the
         *  given machine.
         */
        struct Session
        {
            /** Control of the direct session opened by openSession() */
            ComPtr <IInternalSessionControl> mDirectControl;

            typedef std::list <ComPtr <IInternalSessionControl> > RemoteControlList;

            /** list of controls of all opened remote sessions */
            RemoteControlList mRemoteControls;

            /** openRemoteSession() and OnSessionEnd() progress indicator */
            ComObjPtr <Progress> mProgress;

            /**
             *  PID of the session object that must be passed to openSession()
             *  to finalize the openRemoteSession() request
             *  (i.e., PID of the process created by openRemoteSession())
             */
            RTPROCESS mPid;

            /** Current session state */
            SessionState_T mState;

            /** Session type string (for indirect sessions) */
            Bstr mType;

            /** Sesison machine object */
            ComObjPtr <SessionMachine> mMachine;
        };

        Data();
        ~Data();

        const Guid mUuid;
        BOOL mRegistered;

        Bstr mConfigFile;
        Bstr mConfigFileFull;

        BOOL mAccessible;
        com::ErrorInfo mAccessError;

        MachineState_T mMachineState;
        LONG64 mLastStateChange;

        BOOL mCurrentStateModified;

        RTFILE mHandleCfgFile;

        Session mSession;

        ComObjPtr <Snapshot> mFirstSnapshot;
        ComObjPtr <Snapshot> mCurrentSnapshot;
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
        Bstr mStateFilePath;
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

        bool operator== (const UserData &that) const
        {
            return this == &that ||
                   (mName == that.mName &&
                    mNameSync == that.mNameSync &&
                    mDescription == that.mDescription &&
                    mOSType.equalsTo (that.mOSType) &&
                    mSnapshotFolderFull == that.mSnapshotFolderFull);
        }

        Bstr    mName;
        BOOL    mNameSync;
        Bstr    mDescription;
        ComPtr <IGuestOSType> mOSType;
        Bstr    mSnapshotFolder;
        Bstr    mSnapshotFolderFull;
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
        HWData();
        ~HWData();

        bool operator== (const HWData &that) const;

        ULONG          mMemorySize;
        ULONG          mVRAMSize;
        ULONG          mMonitorCount;
        TriStateBool_T mHWVirtExEnabled;

        DeviceType_T   mBootOrder [SchemaDefs::MaxBootPosition];

        typedef std::list <ComObjPtr <SharedFolder> > SharedFolderList;
        SharedFolderList mSharedFolders;
        ClipboardMode_T mClipboardMode;
    };

    /**
     *  Hard disk data.
     *
     *  The usage policy is the same as for HWData, but a separate structure
     *  is necessarym because hard disk data requires different procedures when
     *  taking or discarding snapshots, etc.
     *
     *  The data variable is |mHWData|.
     */
    struct HDData
    {
        HDData();
        ~HDData();

        bool operator== (const HDData &that) const;

        typedef std::list <ComObjPtr <HardDiskAttachment> > HDAttachmentList;
        HDAttachmentList mHDAttachments;

        /**
         *  Right after Machine::fixupHardDisks(true): |true| if hard disks
         *  were actually changed, |false| otherwise
         */
        bool mHDAttachmentsChanged;
    };

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT (Machine)

    DECLARE_NOT_AGGREGATABLE(Machine)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(Machine)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IMachine)
    END_COM_MAP()

    NS_DECL_ISUPPORTS

    DECLARE_EMPTY_CTOR_DTOR (Machine)

    HRESULT FinalConstruct();
    void FinalRelease();

    enum InitMode { Init_New, Init_Existing, Init_Registered };

    // public initializer/uninitializer for internal purposes only
    HRESULT init (VirtualBox *aParent, const BSTR aConfigFile,
                  InitMode aMode, const BSTR aName = NULL,
                  BOOL aNameSync = TRUE, const Guid *aId = NULL);
    void uninit();

    // IMachine properties
    STDMETHOD(COMGETTER(Parent))(IVirtualBox **aParent);
    STDMETHOD(COMGETTER(Accessible)) (BOOL *aAccessible);
    STDMETHOD(COMGETTER(AccessError)) (IVirtualBoxErrorInfo **aAccessError);
    STDMETHOD(COMGETTER(Name))(BSTR *aName);
    STDMETHOD(COMSETTER(Name))(INPTR BSTR aName);
    STDMETHOD(COMGETTER(Description))(BSTR *aDescription);
    STDMETHOD(COMSETTER(Description))(INPTR BSTR aDescription);
    STDMETHOD(COMGETTER(Id))(GUIDPARAMOUT aId);
    STDMETHOD(COMGETTER(OSType)) (IGuestOSType **aOSType);
    STDMETHOD(COMSETTER(OSType)) (IGuestOSType *aOSType);
    STDMETHOD(COMGETTER(MemorySize))(ULONG *memorySize);
    STDMETHOD(COMSETTER(MemorySize))(ULONG memorySize);
    STDMETHOD(COMGETTER(VRAMSize))(ULONG *memorySize);
    STDMETHOD(COMSETTER(VRAMSize))(ULONG memorySize);
    STDMETHOD(COMGETTER(MonitorCount))(ULONG *monitorCount);
    STDMETHOD(COMSETTER(MonitorCount))(ULONG monitorCount);
    STDMETHOD(COMGETTER(BIOSSettings))(IBIOSSettings **biosSettings);
    STDMETHOD(COMGETTER(HWVirtExEnabled))(TriStateBool_T *enabled);
    STDMETHOD(COMSETTER(HWVirtExEnabled))(TriStateBool_T enabled);
    STDMETHOD(COMGETTER(SnapshotFolder))(BSTR *aSavedStateFolder);
    STDMETHOD(COMSETTER(SnapshotFolder))(INPTR BSTR aSavedStateFolder);
    STDMETHOD(COMGETTER(HardDiskAttachments))(IHardDiskAttachmentCollection **attachments);
    STDMETHOD(COMGETTER(VRDPServer))(IVRDPServer **vrdpServer);
    STDMETHOD(COMGETTER(DVDDrive))(IDVDDrive **dvdDrive);
    STDMETHOD(COMGETTER(FloppyDrive))(IFloppyDrive **floppyDrive);
    STDMETHOD(COMGETTER(AudioAdapter))(IAudioAdapter **audioAdapter);
    STDMETHOD(COMGETTER(USBController))(IUSBController * *a_ppUSBController);
    STDMETHOD(COMGETTER(SettingsFilePath))(BSTR *filePath);
    STDMETHOD(COMGETTER(SettingsModified))(BOOL *modified);
    STDMETHOD(COMGETTER(SessionState))(SessionState_T *aSessionState);
    STDMETHOD(COMGETTER(SessionType))(BSTR *aSessionType);
    STDMETHOD(COMGETTER(SessionPid))(ULONG *aSessionPid);
    STDMETHOD(COMGETTER(State))(MachineState_T *machineState);
    STDMETHOD(COMGETTER(LastStateChange))(LONG64 *aLastStateChange);
    STDMETHOD(COMGETTER(StateFilePath)) (BSTR *aStateFilePath);
    STDMETHOD(COMGETTER(CurrentSnapshot)) (ISnapshot **aCurrentSnapshot);
    STDMETHOD(COMGETTER(SnapshotCount)) (ULONG *aSnapshotCount);
    STDMETHOD(COMGETTER(CurrentStateModified))(BOOL *aCurrentStateModified);
    STDMETHOD(COMGETTER(SharedFolders)) (ISharedFolderCollection **aSharedFolders);
    STDMETHOD(COMGETTER(ClipboardMode)) (ClipboardMode_T *aClipboardMode);
    STDMETHOD(COMSETTER(ClipboardMode)) (ClipboardMode_T aClipboardMode);

    // IMachine methods
    STDMETHOD(SetBootOrder)(ULONG aPosition, DeviceType_T aDevice);
    STDMETHOD(GetBootOrder)(ULONG aPosition, DeviceType_T *aDevice);
    STDMETHOD(AttachHardDisk)(INPTR GUIDPARAM aId, DiskControllerType_T aCtl, LONG aDev);
    STDMETHOD(GetHardDisk)(DiskControllerType_T aCtl, LONG aDev, IHardDisk **aHardDisk);
    STDMETHOD(DetachHardDisk) (DiskControllerType_T aCtl, LONG aDev);
    STDMETHOD(GetNetworkAdapter) (ULONG slot, INetworkAdapter **adapter);
    STDMETHOD(GetNextExtraDataKey)(INPTR BSTR aKey, BSTR *aNextKey, BSTR *aNextValue);
    STDMETHOD(GetExtraData)(INPTR BSTR aKey, BSTR *aValue);
    STDMETHOD(SetExtraData)(INPTR BSTR aKey, INPTR BSTR aValue);
    STDMETHOD(SaveSettings)();
    STDMETHOD(DiscardSettings)();
    STDMETHOD(DeleteSettings)();
    STDMETHOD(GetSnapshot) (INPTR GUIDPARAM aId, ISnapshot **aSnapshot);
    STDMETHOD(FindSnapshot) (INPTR BSTR aName, ISnapshot **aSnapshot);
    STDMETHOD(SetCurrentSnapshot) (INPTR GUIDPARAM aId);
    STDMETHOD(CreateSharedFolder) (INPTR BSTR aName, INPTR BSTR aHostPath);
    STDMETHOD(RemoveSharedFolder) (INPTR BSTR aName);
    STDMETHOD(CanShowConsoleWindow) (BOOL *aCanShow);
    STDMETHOD(ShowConsoleWindow) (ULONG64 *aWinId);

    // public methods only for internal purposes

    /// @todo (dmik) add lock and make non-inlined after revising classes
    //  that use it (actually, the CHECK_MACHINE_MUTABILITY macro).
    //  Note: these classes should enter Machine lock to keep the returned
    //  information valid!
    bool isMutable()
    {
        return ((!mData->mRegistered) ||
                (mType == IsSessionMachine &&
                 mData->mMachineState <= MachineState_Paused &&
                 mData->mMachineState != MachineState_Saved));
    }

    /// @todo (dmik) add lock and make non-inlined after revising classes
    //  that use it (actually, the CHECK_MACHINE_MUTABILITY_IGNORING_SAVED macro).
    //  Note: these classes should enter Machine lock to keep the returned
    //  information valid!
    bool isMutableIgnoringSavedState()
    {
        return ((!mData->mRegistered) ||
                (mType == IsSessionMachine &&
                 mData->mMachineState <= MachineState_Paused));
    }

    /// @todo (dmik) add lock and make non-inlined after revising classes
    //  that use it. Note: they should enter Machine lock to keep the returned
    //  information valid!
    bool isRegistered() { return !!mData->mRegistered; }

    ComObjPtr <SessionMachine> sessionMachine();

    // Note: the below methods are intended to be called only after adding
    // a caller to the Machine instance and, when necessary, from under
    // the Machine lock in appropriate mode

    /// @todo (dmik) revise code using these methods: improving incapsulation
    //  should make them not necessary

    const ComObjPtr <VirtualBox, ComWeakRef> &virtualBox() { return mParent; }

    const Shareable <Data> &data() const { return mData; }
    const Backupable <UserData> &userData() const { return mUserData; }
    const Backupable <HDData> &hdData() const { return mHDData; }

    const Shareable <SSData> &ssData() const { return mSSData; }

    const ComObjPtr <DVDDrive> &dvdDrive() { return mDVDDrive; }
    const ComObjPtr <FloppyDrive> &floppyDrive() { return mFloppyDrive; }
    const ComObjPtr <USBController> &usbController() { return mUSBController; }

    virtual HRESULT onDVDDriveChange() { return S_OK; }
    virtual HRESULT onFloppyDriveChange() { return S_OK; }
    virtual HRESULT onNetworkAdapterChange(INetworkAdapter *networkAdapter) { return S_OK; }
    virtual HRESULT onVRDPServerChange() { return S_OK; }
    virtual HRESULT onUSBControllerChange() { return S_OK; }

    int calculateFullPath (const char *aPath, Utf8Str &aResult);
    void calculateRelativePath (const char *aPath, Utf8Str &aResult);

    void getLogFolder (Utf8Str &aLogFolder);

    HRESULT openSession (IInternalSessionControl *aControl);
    HRESULT openRemoteSession (IInternalSessionControl *aControl,
                               INPTR BSTR aType, Progress *aProgress);
    HRESULT openExistingSession (IInternalSessionControl *aControl);

    HRESULT trySetRegistered (BOOL aRegistered);

    HRESULT getSharedFolder (const BSTR aName,
                             ComObjPtr <SharedFolder> &aSharedFolder,
                             bool aSetError = false)
    {
        AutoLock alock (this);
        return findSharedFolder (aName, aSharedFolder, aSetError);
    }

    // for VirtualBoxSupportErrorInfoImpl
    static const wchar_t *getComponentName() { return L"Machine"; }

protected:

    enum InstanceType { IsMachine, IsSessionMachine, IsSnapshotMachine };

    HRESULT registeredInit();

    inline Machine *machine();

    void uninitDataAndChildObjects();

    virtual HRESULT setMachineState (MachineState_T aMachineState);

    HRESULT findSharedFolder (const BSTR aName,
                              ComObjPtr <SharedFolder> &aSharedFolder,
                              bool aSetError = false);

    HRESULT loadSettings (bool aRegistered);
    HRESULT loadSnapshot (CFGNODE aNode, const Guid &aCurSnapshotId,
                          Snapshot *aParentSnapshot);
    HRESULT loadHardware (CFGNODE aNode);
    HRESULT loadHardDisks (CFGNODE aNode, bool aRegistered,
                           const Guid *aSnapshotId = NULL);

    HRESULT openConfigLoader (CFGHANDLE *aLoader, bool aIsNew = false);
    HRESULT closeConfigLoader (CFGHANDLE aLoader, bool aSaveBeforeClose);

    HRESULT findSnapshotNode (Snapshot *aSnapshot, CFGNODE aMachineNode,
                              CFGNODE *aSnapshotsNode, CFGNODE *aSnapshotNode);

    HRESULT findSnapshot (const Guid &aId, ComObjPtr <Snapshot> &aSnapshot,
                          bool aSetError = false);
    HRESULT findSnapshot (const BSTR aName, ComObjPtr <Snapshot> &aSnapshot,
                          bool aSetError = false);

    HRESULT findHardDiskAttachment (const ComObjPtr <HardDisk> &aHd,
                                    ComObjPtr <Machine> *aMachine,
                                    ComObjPtr <Snapshot> *aSnapshot,
                                    ComObjPtr <HardDiskAttachment> *aHda);

    HRESULT prepareSaveSettings (bool &aRenamed, bool &aNew);
    HRESULT saveSettings (bool aMarkCurStateAsModified = true,
                          bool aInformCallbacksAnyway = false);

    enum
    {
        // ops for #saveSnapshotSettings()
        SaveSS_NoOp = 0x00, SaveSS_AddOp = 0x01,
        SaveSS_UpdateAttrsOp = 0x02, SaveSS_UpdateAllOp = 0x03,
        SaveSS_OpMask = 0xF,
        // flags for #saveSnapshotSettings()
        SaveSS_UpdateCurStateModified = 0x40,
        SaveSS_UpdateCurrentId = 0x80,
        // flags for #saveStateSettings()
        SaveSTS_CurStateModified = 0x20,
        SaveSTS_StateFilePath = 0x40,
        SaveSTS_StateTimeStamp = 0x80,
    };

    HRESULT saveSnapshotSettings (Snapshot *aSnapshot, int aOpFlags);
    HRESULT saveSnapshotSettingsWorker (CFGNODE aMachineNode,
                                        Snapshot *aSnapshot, int aOpFlags);

    HRESULT saveSnapshot (CFGNODE aNode, Snapshot *aSnapshot, bool aAttrsOnly);
    HRESULT saveHardware (CFGNODE aNode);
    HRESULT saveHardDisks (CFGNODE aNode);

    HRESULT saveStateSettings (int aFlags);

    HRESULT wipeOutImmutableDiffs();

    HRESULT fixupHardDisks (bool aCommit);

    HRESULT createSnapshotDiffs (const Guid *aSnapshotId,
                                 const Bstr &aFolder,
                                 const ComObjPtr <Progress> &aProgress,
                                 bool aOnline);
    HRESULT deleteSnapshotDiffs (const ComObjPtr <Snapshot> &aSnapshot);

    HRESULT lockConfig();
    HRESULT unlockConfig();

    /** @note This method is not thread safe */
    BOOL isConfigLocked()
    {
        return !!mData && mData->mHandleCfgFile != NIL_RTFILE;
    }

    bool isInOwnDir (Utf8Str *aSettingsDir = NULL);

    bool isModified();
    bool isReallyModified (bool aIgnoreUserData = false);
    void rollback (bool aNotify);
    HRESULT commit();
    void copyFrom (Machine *aThat);

    const InstanceType mType;

    const ComObjPtr <Machine, ComWeakRef> mPeer;

    const ComObjPtr <VirtualBox, ComWeakRef> mParent;

    Shareable <Data> mData;
    Shareable <SSData> mSSData;

    Backupable <UserData> mUserData;
    Backupable <HWData> mHWData;
    Backupable <HDData> mHDData;

    // the following fields need special backup/rollback/commit handling,
    // so they cannot be a part of HWData

    const ComObjPtr <VRDPServer> mVRDPServer;
    const ComObjPtr <DVDDrive> mDVDDrive;
    const ComObjPtr <FloppyDrive> mFloppyDrive;
    const ComObjPtr <AudioAdapter> mAudioAdapter;
    const ComObjPtr <USBController> mUSBController;
    const ComObjPtr <BIOSSettings> mBIOSSettings;

    const ComObjPtr <NetworkAdapter>
        mNetworkAdapters [SchemaDefs::NetworkAdapterCount];

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
    public VirtualBoxSupportTranslation <SessionMachine>,
    public Machine,
    public IInternalMachineControl
{
public:

    VIRTUALBOXSUPPORTTRANSLATION_OVERRIDE(SessionMachine)

    DECLARE_NOT_AGGREGATABLE(SessionMachine)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(SessionMachine)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IMachine)
        COM_INTERFACE_ENTRY(IInternalMachineControl)
    END_COM_MAP()

    NS_DECL_ISUPPORTS

    DECLARE_EMPTY_CTOR_DTOR (SessionMachine)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init (Machine *aMachine);
    void uninit() { uninit (Uninit::Unexpected); }

    // AutoLock::Lockable interface
    AutoLock::Handle *lockHandle() const;

    // IInternalMachineControl methods
    STDMETHOD(UpdateState)(MachineState_T machineState);
    STDMETHOD(GetIPCId)(BSTR *id);
    STDMETHOD(GetLogFolder) (BSTR *aLogFolder);
    STDMETHOD(RunUSBDeviceFilters) (IUSBDevice *aUSBDevice, BOOL *aMatched);
    STDMETHOD(CaptureUSBDevice) (INPTR GUIDPARAM aId, IUSBDevice **aHostDevice);
    STDMETHOD(ReleaseUSBDevice) (INPTR GUIDPARAM aId);
    STDMETHOD(AutoCaptureUSBDevices) (IUSBDeviceCollection **aHostDevices);
    STDMETHOD(ReleaseAllUSBDevices)();
    STDMETHOD(OnSessionEnd)(ISession *aSession, IProgress **aProgress);
    STDMETHOD(BeginSavingState) (IProgress *aProgress, BSTR *aStateFilePath);
    STDMETHOD(EndSavingState) (BOOL aSuccess);
    STDMETHOD(BeginTakingSnapshot) (IConsole *aInitiator,
                                    INPTR BSTR aName, INPTR BSTR aDescription,
                                    IProgress *aProgress, BSTR *aStateFilePath,
                                    IProgress **aServerProgress);
    STDMETHOD(EndTakingSnapshot) (BOOL aSuccess);
    STDMETHOD(DiscardSnapshot) (IConsole *aInitiator, INPTR GUIDPARAM aId,
                               MachineState_T *aMachineState, IProgress **aProgress);
    STDMETHOD(DiscardCurrentState) (
        IConsole *aInitiator, MachineState_T *aMachineState, IProgress **aProgress);
    STDMETHOD(DiscardCurrentSnapshotAndState) (
        IConsole *aInitiator, MachineState_T *aMachineState, IProgress **aProgress);

    // public methods only for internal purposes

    bool checkForDeath();
#ifdef __WIN__
    HANDLE ipcSem() { return mIPCSem; }
#endif

    HRESULT onDVDDriveChange();
    HRESULT onFloppyDriveChange();
    HRESULT onNetworkAdapterChange(INetworkAdapter *networkAdapter);
    HRESULT onVRDPServerChange();
    HRESULT onUSBControllerChange();
    HRESULT onUSBDeviceAttach (IUSBDevice *aDevice);
    HRESULT onUSBDeviceDetach (INPTR GUIDPARAM aId);

private:

    struct SnapshotData
    {
        SnapshotData() : mLastState (MachineState_InvalidMachineState) {}

        MachineState_T mLastState;

        // used when taking snapshot
        ComObjPtr <Snapshot> mSnapshot;
        ComObjPtr <Progress> mServerProgress;
        ComObjPtr <CombinedProgress> mCombinedProgress;

        // used when saving state
        Guid mProgressId;
        Bstr mStateFilePath;
    };

    struct Uninit {
        enum Reason { Unexpected, Abnormal, Normal };
    };

    struct Task;
    struct TakeSnapshotTask;
    struct DiscardSnapshotTask;
    struct DiscardCurrentStateTask;

    friend struct TakeSnapshotTask;
    friend struct DiscardSnapshotTask;
    friend struct DiscardCurrentStateTask;

    void uninit (Uninit::Reason aReason);

    HRESULT endSavingState (BOOL aSuccess);
    HRESULT endTakingSnapshot (BOOL aSuccess);

    typedef std::map <ComObjPtr <Machine>, MachineState_T> AffectedMachines;

    void takeSnapshotHandler (TakeSnapshotTask &aTask);
    void discardSnapshotHandler (DiscardSnapshotTask &aTask);
    void discardCurrentStateHandler (DiscardCurrentStateTask &aTask);

    HRESULT setMachineState (MachineState_T aMachineState);
    HRESULT updateMachineStateOnClient();

    SnapshotData mSnapshotData;

    /** interprocess semaphore handle (id) for this machine */
#if defined(__WIN__)
    HANDLE mIPCSem;
    Bstr mIPCSemName;
#elif defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER)
    int mIPCSem;
#endif

    static DECLCALLBACK(int) taskHandler (RTTHREAD thread, void *pvUser);
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
    public VirtualBoxSupportTranslation <SnapshotMachine>,
    public Machine
{
public:

    VIRTUALBOXSUPPORTTRANSLATION_OVERRIDE(SnapshotMachine)

    DECLARE_NOT_AGGREGATABLE(SnapshotMachine)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(SnapshotMachine)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IMachine)
    END_COM_MAP()

    NS_DECL_ISUPPORTS

    DECLARE_EMPTY_CTOR_DTOR (SnapshotMachine)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init (SessionMachine *aSessionMachine,
                  INPTR GUIDPARAM aSnapshotId, INPTR BSTR aStateFilePath);
    HRESULT init (Machine *aMachine, CFGNODE aHWNode, CFGNODE aHDAsNode,
                  INPTR GUIDPARAM aSnapshotId, INPTR BSTR aStateFilePath);
    void uninit();

    // AutoLock::Lockable interface
    AutoLock::Handle *lockHandle() const;

    // public methods only for internal purposes

    HRESULT onSnapshotChange (Snapshot *aSnapshot);

private:

    Guid mSnapshotId;
};

////////////////////////////////////////////////////////////////////////////////

/**
 *  Returns a pointer to the Machine object for this machine that acts like a
 *  parent for complex machine data objects such as shared folders, etc.
 *
 *  For primary Machine objects and for SnapshotMachine objects, returns this
 *  object's pointer itself. For SessoinMachine objects, returns the peer
 *  (primary) machine pointer.
 */
inline Machine *Machine::machine()
{
    if (mType == IsSessionMachine)
        return mPeer;
    return this;
}

COM_DECL_READONLY_ENUM_AND_COLLECTION (Machine)

#endif // ____H_MACHINEIMPL
