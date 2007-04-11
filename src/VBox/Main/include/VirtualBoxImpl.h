/** @file
 *
 * VirtualBox COM class implementation
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

#ifndef ____H_VIRTUALBOXIMPL
#define ____H_VIRTUALBOXIMPL

#include "VirtualBoxBase.h"
#include "VirtualBoxXMLUtil.h"

#include "VBox/com/EventQueue.h"

#include <VBox/cfgldr.h>

#include <list>
#include <vector>
#include <map>

#ifdef __WIN__
#include "win32/resource.h"
#endif

class Machine;
class SessionMachine;
class HardDisk;
class HVirtualDiskImage;
class DVDImage;
class FloppyImage;
class MachineCollection;
class HardDiskCollection;
class DVDImageCollection;
class FloppyImageCollection;
class GuestOSType;
class GuestOSTypeCollection;
class SharedFolder;
class Progress;
class ProgressCollection;
class Host;
class SystemProperties;

#ifdef __WIN__
class SVCHlpClient;
#endif

struct VMClientWatcherData;

class ATL_NO_VTABLE VirtualBox :
    public VirtualBoxBaseWithChildrenNEXT,
    public VirtualBoxXMLUtil,
    public VirtualBoxSupportErrorInfoImpl <VirtualBox, IVirtualBox>,
    public VirtualBoxSupportTranslation <VirtualBox>,
#ifdef __WIN__
    public IDispatchImpl<IVirtualBox, &IID_IVirtualBox, &LIBID_VirtualBox,
                         kTypeLibraryMajorVersion, kTypeLibraryMinorVersion>,
    public CComCoClass<VirtualBox, &CLSID_VirtualBox>
#else
    public IVirtualBox
#endif
{

public:

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT (VirtualBox)

    typedef std::list <ComPtr <IVirtualBoxCallback> > CallbackList;
    typedef std::vector <ComPtr <IVirtualBoxCallback> > CallbackVector;

    class CallbackEvent;
    friend class CallbackEvent;

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
    STDMETHOD(COMGETTER(HomeFolder)) (BSTR *aHomeFolder);
    STDMETHOD(COMGETTER(Host)) (IHost **aHost);
    STDMETHOD(COMGETTER(SystemProperties)) (ISystemProperties **aSystemProperties);
    STDMETHOD(COMGETTER(Machines)) (IMachineCollection **aMachines);
    STDMETHOD(COMGETTER(HardDisks)) (IHardDiskCollection **aHardDisks);
    STDMETHOD(COMGETTER(DVDImages)) (IDVDImageCollection **aDVDImages);
    STDMETHOD(COMGETTER(FloppyImages)) (IFloppyImageCollection **aFloppyImages);
    STDMETHOD(COMGETTER(ProgressOperations)) (IProgressCollection **aOperations);
    STDMETHOD(COMGETTER(GuestOSTypes)) (IGuestOSTypeCollection **aGuestOSTypes);
    STDMETHOD(COMGETTER(SharedFolders)) (ISharedFolderCollection **aSharedFolders);

    /* IVirtualBox methods */

    STDMETHOD(CreateMachine) (INPTR BSTR aBaseFolder, INPTR BSTR aName,
                              IMachine **aMachine);
    STDMETHOD(CreateLegacyMachine) (INPTR BSTR aSettingsFile, INPTR BSTR aName,
                                    IMachine **aMachine);
    STDMETHOD(OpenMachine) (INPTR BSTR aSettingsFile, IMachine **aMachine);
    STDMETHOD(RegisterMachine) (IMachine *aMachine);
    STDMETHOD(GetMachine) (INPTR GUIDPARAM aId, IMachine **aMachine);
    STDMETHOD(FindMachine) (INPTR BSTR aName, IMachine **aMachine);
    STDMETHOD(UnregisterMachine) (INPTR GUIDPARAM aId, IMachine **aMachine);

    STDMETHOD(CreateHardDisk) (HardDiskStorageType_T aStorageType, IHardDisk **aHardDisk);
    STDMETHOD(OpenHardDisk) (INPTR BSTR aLocation, IHardDisk **aHardDisk);
    STDMETHOD(OpenVirtualDiskImage) (INPTR BSTR aFilePath, IVirtualDiskImage **aImage);
    STDMETHOD(RegisterHardDisk) (IHardDisk *aHardDisk);
    STDMETHOD(GetHardDisk) (INPTR GUIDPARAM aId, IHardDisk **aHardDisk);
    STDMETHOD(FindHardDisk) (INPTR BSTR aLocation, IHardDisk **aHardDisk);
    STDMETHOD(FindVirtualDiskImage) (INPTR BSTR aFilePath, IVirtualDiskImage **aImage);
    STDMETHOD(UnregisterHardDisk) (INPTR GUIDPARAM aId, IHardDisk **aHardDisk);

    STDMETHOD(OpenDVDImage) (INPTR BSTR aFilePath, INPTR GUIDPARAM aId,
                             IDVDImage **aDVDImage);
    STDMETHOD(RegisterDVDImage) (IDVDImage *aDVDImage);
    STDMETHOD(GetDVDImage) (INPTR GUIDPARAM aId, IDVDImage **aDVDImage);
    STDMETHOD(FindDVDImage) (INPTR BSTR aFilePath, IDVDImage **aDVDImage);
    STDMETHOD(GetDVDImageUsage) (INPTR GUIDPARAM aId,
                                 ResourceUsage_T aUsage,
                                 BSTR *aMachineIDs);
    STDMETHOD(UnregisterDVDImage) (INPTR GUIDPARAM aId, IDVDImage **aDVDImage);

    STDMETHOD(OpenFloppyImage) (INPTR BSTR aFilePath, INPTR GUIDPARAM aId,
                                IFloppyImage **aFloppyImage);
    STDMETHOD(RegisterFloppyImage) (IFloppyImage *aFloppyImage);
    STDMETHOD(GetFloppyImage) (INPTR GUIDPARAM id, IFloppyImage **aFloppyImage);
    STDMETHOD(FindFloppyImage) (INPTR BSTR aFilePath, IFloppyImage **aFloppyImage);
    STDMETHOD(GetFloppyImageUsage) (INPTR GUIDPARAM aId,
                                    ResourceUsage_T aUsage,
                                    BSTR *aMachineIDs);
    STDMETHOD(UnregisterFloppyImage) (INPTR GUIDPARAM aId, IFloppyImage **aFloppyImage);

    STDMETHOD(FindGuestOSType)(INPTR BSTR aId, IGuestOSType **aType);
    STDMETHOD(CreateSharedFolder)(INPTR BSTR aName, INPTR BSTR aHostPath);
    STDMETHOD(RemoveSharedFolder)(INPTR BSTR aName);
    STDMETHOD(GetNextExtraDataKey)(INPTR BSTR aKey, BSTR *aNextKey, BSTR *aNextValue);
    STDMETHOD(GetExtraData)(INPTR BSTR aKey, BSTR *aValue);
    STDMETHOD(SetExtraData)(INPTR BSTR aKey, INPTR BSTR aValue);
    STDMETHOD(OpenSession) (ISession *aSession, INPTR GUIDPARAM aMachineId);
    STDMETHOD(OpenRemoteSession) (ISession *aSession, INPTR GUIDPARAM aMachineId,
                                  INPTR BSTR aType, IProgress **aProgress);
    STDMETHOD(OpenExistingSession) (ISession *aSession, INPTR GUIDPARAM aMachineId);
    STDMETHOD(RegisterCallback)(IVirtualBoxCallback *callback);
    STDMETHOD(UnregisterCallback)(IVirtualBoxCallback *callback);

    /* public methods only for internal purposes */

    HRESULT postEvent (Event *event);

    HRESULT addProgress (IProgress *aProgress);
    HRESULT removeProgress (INPTR GUIDPARAM aId);

#ifdef __WIN__
    typedef DECLCALLBACKPTR (HRESULT, SVCHelperClientFunc)
        (SVCHlpClient *aClient, Progress *aProgress, void *aUser, int *aVrc);
    HRESULT startSVCHelperClient (bool aPrivileged,
                                  SVCHelperClientFunc aFunc,
                                  void *aUser, Progress *aProgress);
#endif

    void addProcessToReap (RTPROCESS pid);
    void updateClientWatcher();

    void onMachineStateChange (const Guid &id, MachineState_T state);
    void onMachineDataChange (const Guid &id);
    BOOL onExtraDataCanChange(const Guid &id, INPTR BSTR key, INPTR BSTR value);
    void onExtraDataChange(const Guid &id, INPTR BSTR key, INPTR BSTR value);
    void onMachineRegistered (const Guid &aId, BOOL aRegistered);
    void onSessionStateChange (const Guid &id, SessionState_T state);

    void onSnapshotTaken (const Guid &aMachineId, const Guid &aSnapshotId);
    void onSnapshotDiscarded (const Guid &aMachineId, const Guid &aSnapshotId);
    void onSnapshotChange (const Guid &aMachineId, const Guid &aSnapshotId);

    ComPtr <IGuestOSType> getUnknownOSType();

    typedef std::vector <ComObjPtr <SessionMachine> > SessionMachineVector;
    void getOpenedMachines (SessionMachineVector &aVector);

    bool isMachineIdValid (const Guid &aId)
    {
        return SUCCEEDED (findMachine (aId, false /* aSetError */, NULL));
    }

    /// @todo (dmik) remove and make findMachine() public instead
    //  after switching to VirtualBoxBaseNEXT
    HRESULT getMachine (const Guid &aId, ComObjPtr <Machine> &aMachine,
                        bool aSetError = false)
    {
        return findMachine (aId, aSetError, &aMachine);
    }

    /// @todo (dmik) remove and make findHardDisk() public instead
    //  after switching to VirtualBoxBaseNEXT
    HRESULT getHardDisk (const Guid &aId, ComObjPtr <HardDisk> &aHardDisk)
    {
        return findHardDisk (&aId, NULL, true /* aDoSetError */, &aHardDisk);
    }

    BOOL getDVDImageUsage (const Guid &id, ResourceUsage_T usage,
                           Bstr *machineIDs = NULL);
    BOOL getFloppyImageUsage (const Guid &id, ResourceUsage_T usage,
                              Bstr *machineIDs = NULL);

    const ComObjPtr <Host> &host() { return mData.mHost; }
    const ComObjPtr <SystemProperties> &systemProperties() { return mData.mSystemProperties; }

    /** Returns the VirtualBox home directory */
    const Utf8Str &homeDir() { return mData.mHomeDir; }

    void calculateRelativePath (const char *aPath, Utf8Str &aResult);

    enum RHD_Flags { RHD_Internal, RHD_External, RHD_OnStartUp };
    HRESULT registerHardDisk (HardDisk *aHardDisk, RHD_Flags aFlags);
    HRESULT unregisterHardDisk (HardDisk *aHardDisk);
    HRESULT unregisterDiffHardDisk (HardDisk *aHardDisk);

    HRESULT saveSettings() { return saveConfig(); }
    HRESULT updateSettings (const char *aOldPath, const char *aNewPath);

    const Bstr &settingsFileName() { return mData.mCfgFile.mName; }

    /* for VirtualBoxSupportErrorInfoImpl */
    static const wchar_t *getComponentName() { return L"VirtualBox"; }

private:

    typedef std::list <ComObjPtr <Machine> > MachineList;
    typedef std::list <ComObjPtr <GuestOSType> > GuestOSTypeList;
    typedef std::list <ComPtr <IProgress> > ProgressList;

    typedef std::list <ComObjPtr <HardDisk> > HardDiskList;
    typedef std::list <ComObjPtr <DVDImage> > DVDImageList;
    typedef std::list <ComObjPtr <FloppyImage> > FloppyImageList;
    typedef std::list <ComObjPtr <SharedFolder> > SharedFolderList;

    typedef std::map <Guid, ComObjPtr <HardDisk> > HardDiskMap;

    HRESULT findMachine (const Guid &aId, bool aSetError,
                         ComObjPtr <Machine> *machine = NULL);

    HRESULT findHardDisk (const Guid *aId, const BSTR aLocation,
                          bool aSetError, ComObjPtr <HardDisk> *aHardDisk = NULL);

    HRESULT findVirtualDiskImage (const Guid *aId, const BSTR aFilePathFull,
                          bool aSetError, ComObjPtr <HVirtualDiskImage> *aImage = NULL);
    HRESULT findDVDImage (const Guid *aId, const BSTR aFilePathFull,
                          bool aSetError, ComObjPtr <DVDImage> *aImage = NULL);
    HRESULT findFloppyImage (const Guid *aId, const BSTR aFilePathFull,
                             bool aSetError, ComObjPtr <FloppyImage> *aImage = NULL);

    HRESULT checkMediaForConflicts (HardDisk *aHardDisk,
                                    const Guid *aId, const BSTR aFilePathFull);

    HRESULT loadMachines (CFGNODE aGlobal);
    HRESULT loadDisks (CFGNODE aGlobal);
    HRESULT loadHardDisks (CFGNODE aNode);

    HRESULT saveConfig();
    HRESULT saveHardDisks (CFGNODE aNode);

    HRESULT registerMachine (Machine *aMachine);

    HRESULT registerDVDImage (DVDImage *aImage, bool aOnStartUp);
    HRESULT registerFloppyImage (FloppyImage *aImage, bool aOnStartUp);
    HRESULT registerGuestOSTypes();

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

        CfgFile mCfgFile;

        MachineList mMachines;
        GuestOSTypeList mGuestOSTypes;

        ProgressList mProgressOperations;
        HardDiskList mHardDisks;
        DVDImageList mDVDImages;
        FloppyImageList mFloppyImages;
        SharedFolderList mSharedFolders;

        HardDiskMap mHardDiskMap;

        CallbackList mCallbacks;
    };

    Data mData;

    /** Client watcher thread data structure */
    struct ClientWatcherData
    {
        ClientWatcherData()
#if defined(__WIN__)
            : mUpdateReq (NULL)
#else
            : mUpdateReq (NIL_RTSEMEVENT)
#endif
            , mThread (NIL_RTTHREAD) {}

        // const objects not requiring locking
#if defined(__WIN__)
        const HANDLE mUpdateReq;
#else
        const RTSEMEVENT mUpdateReq;
#endif
        const RTTHREAD mThread;

        typedef std::list <RTPROCESS> ProcessList;
        ProcessList mProcesses;
    };

    ClientWatcherData mWatcherData;

    const RTTHREAD mAsyncEventThread;
    EventQueue * const mAsyncEventQ;
    /** Lock for calling EventQueue->post() */
    AutoLock::Handle mAsyncEventQLock;

    static Bstr sVersion;

    static DECLCALLBACK(int) clientWatcher (RTTHREAD thread, void *pvUser);
    static DECLCALLBACK(int) asyncEventHandler (RTTHREAD thread, void *pvUser);

#ifdef __WIN__
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
