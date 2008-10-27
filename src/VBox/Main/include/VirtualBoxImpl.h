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

#ifndef ____H_VIRTUALBOXIMPL
#define ____H_VIRTUALBOXIMPL

#include "VirtualBoxBase.h"

#include "VBox/com/EventQueue.h"

#include <list>
#include <vector>
#include <map>

#ifdef RT_OS_WINDOWS
# include "win/resource.h"
#endif

#ifdef VBOX_WITH_RESOURCE_USAGE_API
#include "PerformanceImpl.h"
#endif /* VBOX_WITH_RESOURCE_USAGE_API */


class Machine;
class SessionMachine;
class HardDisk2;
class DVDImage2;
class FloppyImage2;
class MachineCollection;
class GuestOSType;
class GuestOSTypeCollection;
class SharedFolder;
class Progress;
class ProgressCollection;
class Host;
class SystemProperties;

#ifdef RT_OS_WINDOWS
class SVCHlpClient;
#endif

struct VMClientWatcherData;

class ATL_NO_VTABLE VirtualBox :
    public VirtualBoxBaseWithChildrenNEXT,
    public VirtualBoxSupportErrorInfoImpl <VirtualBox, IVirtualBox>,
    public VirtualBoxSupportTranslation <VirtualBox>,
#ifdef RT_OS_WINDOWS
    public IDispatchImpl<IVirtualBox, &IID_IVirtualBox, &LIBID_VirtualBox,
                         kTypeLibraryMajorVersion, kTypeLibraryMinorVersion>,
    public CComCoClass<VirtualBox, &CLSID_VirtualBox>
#else
    public IVirtualBox
#endif
{

public:

    typedef std::list <ComPtr <IVirtualBoxCallback> > CallbackList;
    typedef std::vector <ComPtr <IVirtualBoxCallback> > CallbackVector;

    typedef std::vector <ComObjPtr <SessionMachine> > SessionMachineVector;
    typedef std::vector <ComObjPtr <Machine> > MachineVector;

    class CallbackEvent;
    friend class CallbackEvent;

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT (VirtualBox)

    DECLARE_CLASSFACTORY_SINGLETON(VirtualBox)

    DECLARE_REGISTRY_RESOURCEID(IDR_VIRTUALBOX)
    DECLARE_NOT_AGGREGATABLE(VirtualBox)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(VirtualBox)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IVirtualBox)
    END_COM_MAP()

    NS_DECL_ISUPPORTS

    /* to postpone generation of the default ctor/dtor */
    VirtualBox();
    ~VirtualBox();

    HRESULT FinalConstruct();
    void FinalRelease();

    /* public initializer/uninitializer for internal purposes only */
    HRESULT init();
    void uninit();

    /* IVirtualBox properties */
    STDMETHOD(COMGETTER(Version)) (BSTR *aVersion);
    STDMETHOD(COMGETTER(Revision)) (ULONG *aRevision);
    STDMETHOD(COMGETTER(PackageType)) (BSTR *aPackageType);
    STDMETHOD(COMGETTER(HomeFolder)) (BSTR *aHomeFolder);
    STDMETHOD(COMGETTER(SettingsFilePath)) (BSTR *aSettingsFilePath);
    STDMETHOD(COMGETTER(SettingsFileVersion)) (BSTR *aSettingsFileVersion);
    STDMETHOD(COMGETTER(SettingsFormatVersion)) (BSTR *aSettingsFormatVersion);
    STDMETHOD(COMGETTER(Host)) (IHost **aHost);
    STDMETHOD(COMGETTER(SystemProperties)) (ISystemProperties **aSystemProperties);
    STDMETHOD(COMGETTER(Machines2)) (ComSafeArrayOut (IMachine *, aMachines));
    STDMETHOD(COMGETTER(HardDisks2)) (ComSafeArrayOut (IHardDisk2 *, aHardDisks));
    STDMETHOD(COMGETTER(DVDImages)) (ComSafeArrayOut (IDVDImage2 *, aDVDImages));
    STDMETHOD(COMGETTER(FloppyImages)) (ComSafeArrayOut (IFloppyImage2 *, aFloppyImages));
    STDMETHOD(COMGETTER(ProgressOperations)) (IProgressCollection **aOperations);
    STDMETHOD(COMGETTER(GuestOSTypes)) (IGuestOSTypeCollection **aGuestOSTypes);
    STDMETHOD(COMGETTER(SharedFolders)) (ISharedFolderCollection **aSharedFolders);
    STDMETHOD(COMGETTER(PerformanceCollector)) (IPerformanceCollector **aPerformanceCollector);

    /* IVirtualBox methods */

    STDMETHOD(CreateMachine) (INPTR BSTR aBaseFolder, INPTR BSTR aName,
                              INPTR GUIDPARAM aId, IMachine **aMachine);
    STDMETHOD(CreateLegacyMachine) (INPTR BSTR aSettingsFile, INPTR BSTR aName,
                                    INPTR GUIDPARAM aId, IMachine **aMachine);
    STDMETHOD(OpenMachine) (INPTR BSTR aSettingsFile, IMachine **aMachine);
    STDMETHOD(RegisterMachine) (IMachine *aMachine);
    STDMETHOD(GetMachine) (INPTR GUIDPARAM aId, IMachine **aMachine);
    STDMETHOD(FindMachine) (INPTR BSTR aName, IMachine **aMachine);
    STDMETHOD(UnregisterMachine) (INPTR GUIDPARAM aId, IMachine **aMachine);

    STDMETHOD(CreateHardDisk2) (INPTR BSTR aFormat, INPTR BSTR aLocation,
                                IHardDisk2 **aHardDisk);
    STDMETHOD(OpenHardDisk2) (INPTR BSTR aLocation, IHardDisk2 **aHardDisk);
    STDMETHOD(GetHardDisk2) (INPTR GUIDPARAM aId, IHardDisk2 **aHardDisk);
    STDMETHOD(FindHardDisk2) (INPTR BSTR aLocation, IHardDisk2 **aHardDisk);

    STDMETHOD(OpenDVDImage) (INPTR BSTR aLocation, INPTR GUIDPARAM aId,
                             IDVDImage2 **aDVDImage);
    STDMETHOD(GetDVDImage) (INPTR GUIDPARAM aId, IDVDImage2 **aDVDImage);
    STDMETHOD(FindDVDImage) (INPTR BSTR aLocation, IDVDImage2 **aDVDImage);

    STDMETHOD(OpenFloppyImage) (INPTR BSTR aLocation, INPTR GUIDPARAM aId,
                                IFloppyImage2 **aFloppyImage);
    STDMETHOD(GetFloppyImage) (INPTR GUIDPARAM aId, IFloppyImage2 **aFloppyImage);
    STDMETHOD(FindFloppyImage) (INPTR BSTR aLocation, IFloppyImage2 **aFloppyImage);

    STDMETHOD(GetGuestOSType) (INPTR BSTR aId, IGuestOSType **aType);
    STDMETHOD(CreateSharedFolder) (INPTR BSTR aName, INPTR BSTR aHostPath, BOOL aWritable);
    STDMETHOD(RemoveSharedFolder) (INPTR BSTR aName);
    STDMETHOD(GetNextExtraDataKey) (INPTR BSTR aKey, BSTR *aNextKey, BSTR *aNextValue);
    STDMETHOD(GetExtraData) (INPTR BSTR aKey, BSTR *aValue);
    STDMETHOD(SetExtraData) (INPTR BSTR aKey, INPTR BSTR aValue);
    STDMETHOD(OpenSession) (ISession *aSession, INPTR GUIDPARAM aMachineId);
    STDMETHOD(OpenRemoteSession) (ISession *aSession, INPTR GUIDPARAM aMachineId,
                                  INPTR BSTR aType, INPTR BSTR aEnvironment,
                                  IProgress **aProgress);
    STDMETHOD(OpenExistingSession) (ISession *aSession, INPTR GUIDPARAM aMachineId);

    STDMETHOD(RegisterCallback) (IVirtualBoxCallback *aCallback);
    STDMETHOD(UnregisterCallback) (IVirtualBoxCallback *aCallback);

    STDMETHOD(WaitForPropertyChange) (INPTR BSTR aWhat, ULONG aTimeout,
                                      BSTR *aChanged, BSTR *aValues);

    STDMETHOD(SaveSettings)();
    STDMETHOD(SaveSettingsWithBackup) (BSTR *aBakFileName);

    /* public methods only for internal purposes */

    HRESULT postEvent (Event *event);

    HRESULT addProgress (IProgress *aProgress);
    HRESULT removeProgress (INPTR GUIDPARAM aId);

#ifdef RT_OS_WINDOWS
    typedef DECLCALLBACKPTR (HRESULT, SVCHelperClientFunc)
        (SVCHlpClient *aClient, Progress *aProgress, void *aUser, int *aVrc);
    HRESULT startSVCHelperClient (bool aPrivileged,
                                  SVCHelperClientFunc aFunc,
                                  void *aUser, Progress *aProgress);
#endif

    void addProcessToReap (RTPROCESS pid);
    void updateClientWatcher();

    void onMachineStateChange (const Guid &aId, MachineState_T aState);
    void onMachineDataChange (const Guid &aId);
    BOOL onExtraDataCanChange(const Guid &aId, INPTR BSTR aKey, INPTR BSTR aValue,
                              Bstr &aError);
    void onExtraDataChange(const Guid &aId, INPTR BSTR aKey, INPTR BSTR aValue);
    void onMachineRegistered (const Guid &aId, BOOL aRegistered);
    void onSessionStateChange (const Guid &aId, SessionState_T aState);

    void onSnapshotTaken (const Guid &aMachineId, const Guid &aSnapshotId);
    void onSnapshotDiscarded (const Guid &aMachineId, const Guid &aSnapshotId);
    void onSnapshotChange (const Guid &aMachineId, const Guid &aSnapshotId);
    void onGuestPropertyChange (const Guid &aMachineId, INPTR BSTR aName, INPTR BSTR aValue,
                                INPTR BSTR aFlags);

    ComObjPtr <GuestOSType> getUnknownOSType();

    void getOpenedMachines (SessionMachineVector &aVector);

    bool isMachineIdValid (const Guid &aId)
    {
        return SUCCEEDED (findMachine (aId, false /* aSetError */, NULL));
    }

    HRESULT findMachine (const Guid &aId, bool aSetError,
                         ComObjPtr <Machine> *machine = NULL);

    HRESULT findHardDisk2 (const Guid *aId, const BSTR aLocation,
                           bool aSetError, ComObjPtr <HardDisk2> *aHardDisk = NULL);
    HRESULT findDVDImage2 (const Guid *aId, const BSTR aLocation,
                           bool aSetError, ComObjPtr <DVDImage2> *aImage = NULL);
    HRESULT findFloppyImage2 (const Guid *aId, const BSTR aLocation,
                              bool aSetError, ComObjPtr <FloppyImage2> *aImage = NULL);

    const ComObjPtr <Host> &host() { return mData.mHost; }
    const ComObjPtr <SystemProperties> &systemProperties()
        { return mData.mSystemProperties; }
#ifdef VBOX_WITH_RESOURCE_USAGE_API
    const ComObjPtr <PerformanceCollector> &performanceCollector()
        { return mData.mPerformanceCollector; }
#endif /* VBOX_WITH_RESOURCE_USAGE_API */


    /** Returns the VirtualBox home directory */
    const Utf8Str &homeDir() { return mData.mHomeDir; }

    int calculateFullPath (const char *aPath, Utf8Str &aResult);
    void calculateRelativePath (const char *aPath, Utf8Str &aResult);

    HRESULT registerHardDisk2 (HardDisk2 *aHardDisk, bool aSaveRegistry = true);
    HRESULT unregisterHardDisk2 (HardDisk2 *aHardDisk, bool aSaveRegistry = true);

    HRESULT registerDVDImage (DVDImage2 *aImage, bool aSaveRegistry = true);
    HRESULT unregisterDVDImage (DVDImage2 *aImage, bool aSaveRegistry = true);

    HRESULT registerFloppyImage (FloppyImage2 *aImage, bool aSaveRegistry = true);
    HRESULT unregisterFloppyImage (FloppyImage2 *aImage, bool aSaveRegistry = true);

    HRESULT cast (IHardDisk2 *aFrom, ComObjPtr <HardDisk2> &aTo);

    HRESULT saveSettings();
    HRESULT updateSettings (const char *aOldPath, const char *aNewPath);

    const Bstr &settingsFileName() { return mData.mCfgFile.mName; }

    static HRESULT ensureFilePathExists (const char *aFileName);

    class SettingsTreeHelper : public settings::XmlTreeBackend::InputResolver
                             , public settings::XmlTreeBackend::AutoConverter
    {
    public:

        // InputResolver interface
        settings::Input *resolveEntity (const char *aURI, const char *aID);

        // AutoConverter interface
        bool needsConversion (const settings::Key &aRoot, char **aOldVersion) const;
        const char *templateUri() const;
    };

    static HRESULT loadSettingsTree (settings::XmlTreeBackend &aTree,
                                     settings::File &aFile,
                                     bool aValidate,
                                     bool aCatchLoadErrors,
                                     bool aAddDefaults,
                                     Utf8Str *aFormatVersion = NULL);

    /**
     * Shortcut to loadSettingsTree (aTree, aFile, true, true, true).
     *
     * Used when the settings file is to be loaded for the first time for the
     * given object in order to recreate it from the stored settings.
     *
     * @param aFormatVersion Where to store the current format version of the
     *                       loaded settings tree.
     */
    static HRESULT loadSettingsTree_FirstTime (settings::XmlTreeBackend &aTree,
                                               settings::File &aFile,
                                               Utf8Str &aFormatVersion)
    {
        return loadSettingsTree (aTree, aFile, true, true, true,
                                 &aFormatVersion);
    }

    /**
     * Shortcut to loadSettingsTree (aTree, aFile, true, false, true).
     *
     * Used when the settings file is loaded again (after it has been fully
     * checked and validated by #loadSettingsTree_FirstTime()) in order to
     * look at settings that don't have any representation within object's
     * data fields.
     */
    static HRESULT loadSettingsTree_Again (settings::XmlTreeBackend &aTree,
                                           settings::File &aFile)
    {
        return loadSettingsTree (aTree, aFile, true, false, true);
    }

    /**
     * Shortcut to loadSettingsTree (aTree, aFile, true, false, false).
     *
     * Used when the settings file is loaded again (after it has been fully
     * checked and validated by #loadSettingsTree_FirstTime()) in order to
     * update some settings and then save them back.
     */
    static HRESULT loadSettingsTree_ForUpdate (settings::XmlTreeBackend &aTree,
                                               settings::File &aFile)
    {
        return loadSettingsTree (aTree, aFile, true, false, false);
    }

    static HRESULT saveSettingsTree (settings::TreeBackend &aTree,
                                     settings::File &aFile,
                                     Utf8Str &aFormatVersion);

    static HRESULT backupSettingsFile (const Bstr &aFileName,
                                       const Utf8Str &aOldFormat,
                                       Bstr &aBakFileName);

    static HRESULT handleUnexpectedExceptions (RT_SRC_POS_DECL);

    /**
     * Returns a lock handle used to protect changes to the hard disk hierarchy
     * (e.g. changing HardDisk2::mParent fields and adding/removing children).
     */
    RWLockHandle *hardDiskTreeHandle() { return &mHardDiskTreeHandle; }

    /* for VirtualBoxSupportErrorInfoImpl */
    static const wchar_t *getComponentName() { return L"VirtualBox"; }

private:

    typedef std::list <ComObjPtr <Machine> > MachineList;
    typedef std::list <ComObjPtr <GuestOSType> > GuestOSTypeList;

    typedef std::map <Guid, ComPtr <IProgress> > ProgressMap;

    typedef std::list <ComObjPtr <HardDisk2> > HardDisk2List;
    typedef std::list <ComObjPtr <DVDImage2> > DVDImage2List;
    typedef std::list <ComObjPtr <FloppyImage2> > FloppyImage2List;
    typedef std::list <ComObjPtr <SharedFolder> > SharedFolderList;

    typedef std::map <Guid, ComObjPtr <HardDisk2> > HardDisk2Map;

    HRESULT checkMediaForConflicts2 (const Guid &aId, const Bstr &aLocation,
                                     Utf8Str &aConflictType);

    HRESULT loadMachines (const settings::Key &aGlobal);
    HRESULT loadMedia (const settings::Key &aGlobal);

    HRESULT registerMachine (Machine *aMachine);

    HRESULT lockConfig();
    HRESULT unlockConfig();

    /** @note This method is not thread safe */
    bool isConfigLocked() { return mData.mCfgFile.mHandle != NIL_RTFILE; }

    /**
     *  Main VirtualBox data structure.
     *  @note |const| members are persistent during lifetime so can be accessed
     *  without locking.
     */
    struct Data
    {
        Data();

        struct CfgFile
        {
            CfgFile() : mHandle (NIL_RTFILE) {}

            const Bstr mName;
            RTFILE mHandle;
        };

        // const data members not requiring locking
        const Utf8Str mHomeDir;

        // const objects not requiring locking
        const ComObjPtr <Host> mHost;
        const ComObjPtr <SystemProperties> mSystemProperties;
#ifdef VBOX_WITH_RESOURCE_USAGE_API
        const ComObjPtr <PerformanceCollector> mPerformanceCollector;
#endif /* VBOX_WITH_RESOURCE_USAGE_API */

        CfgFile mCfgFile;

        Utf8Str mSettingsFileVersion;

        MachineList mMachines;
        GuestOSTypeList mGuestOSTypes;

        ProgressMap mProgressOperations;

        HardDisk2List mHardDisks2;
        DVDImage2List mDVDImages2;
        FloppyImage2List mFloppyImages2;
        SharedFolderList mSharedFolders;

        /// @todo NEWMEDIA do we really need this map? Used only in
        /// find() it seems
        HardDisk2Map mHardDisk2Map;

        CallbackList mCallbacks;
    };

    Data mData;

    /** Client watcher thread data structure */
    struct ClientWatcherData
    {
        ClientWatcherData()
#if defined(RT_OS_WINDOWS)
            : mUpdateReq (NULL)
#elif defined(RT_OS_OS2)
            : mUpdateReq (NIL_RTSEMEVENT)
#elif defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER)
            : mUpdateReq (NIL_RTSEMEVENT)
#else
# error "Port me!"
#endif
            , mThread (NIL_RTTHREAD) {}

        // const objects not requiring locking
#if defined(RT_OS_WINDOWS)
        const HANDLE mUpdateReq;
#elif defined(RT_OS_OS2)
        const RTSEMEVENT mUpdateReq;
#elif defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER)
        const RTSEMEVENT mUpdateReq;
#else
# error "Port me!"
#endif
        const RTTHREAD mThread;

        typedef std::list <RTPROCESS> ProcessList;
        ProcessList mProcesses;
    };

    ClientWatcherData mWatcherData;

    const RTTHREAD mAsyncEventThread;
    EventQueue * const mAsyncEventQ;

    /**
     * "Safe" lock. May only be used if guaranteed that no other locks are
     * requested while holding it and no functions that may do so are called.
     * Currently, protects the following:
     *
     * - mProgressOperations
     */
    RWLockHandle mSafeLock;

    RWLockHandle mHardDiskTreeHandle;

    static Bstr sVersion;
    static ULONG sRevision;
    static Bstr sPackageType;
    static Bstr sSettingsFormatVersion;

    static DECLCALLBACK(int) ClientWatcher (RTTHREAD thread, void *pvUser);
    static DECLCALLBACK(int) AsyncEventHandler (RTTHREAD thread, void *pvUser);

#ifdef RT_OS_WINDOWS
    static DECLCALLBACK(int) SVCHelperClientThread (RTTHREAD aThread, void *aUser);
#endif
};

////////////////////////////////////////////////////////////////////////////////

/**
 *  Abstract callback event class to asynchronously call VirtualBox callbacks
 *  on a dedicated event thread. Subclasses reimplement #handleCallback()
 *  to call appropriate IVirtualBoxCallback methods depending on the event
 *  to be dispatched.
 *
 *  @note The VirtualBox instance passed to the constructor is strongly
 *  referenced, so that the VirtualBox singleton won't be released until the
 *  event gets handled by the event thread.
 */
class VirtualBox::CallbackEvent : public Event
{
public:

    CallbackEvent (VirtualBox *aVirtualBox) : mVirtualBox (aVirtualBox)
    {
        Assert (aVirtualBox);
    }

    void *handler();

    virtual void handleCallback (const ComPtr <IVirtualBoxCallback> &aCallback) = 0;

private:

    /*
     *  Note that this is a weak ref -- the CallbackEvent handler thread
     *  is bound to the lifetime of the VirtualBox instance, so it's safe.
     */
    ComObjPtr <VirtualBox, ComWeakRef> mVirtualBox;
};

#endif // ____H_VIRTUALBOXIMPL
