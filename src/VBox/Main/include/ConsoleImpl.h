/* $Id$ */
/** @file
 * VBox Console COM Class definition
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

#ifndef ____H_CONSOLEIMPL
#define ____H_CONSOLEIMPL

#include "VirtualBoxBase.h"
#include "SchemaDefs.h"
#include "VBox/com/array.h"
#include "EventImpl.h"

class Guest;
class Keyboard;
class Mouse;
class Display;
class MachineDebugger;
class TeleporterStateSrc;
class OUSBDevice;
class RemoteUSBDevice;
class SharedFolder;
class VRDEServerInfo;
class AudioSniffer;
class ConsoleVRDPServer;
class VMMDev;
class Progress;
COM_STRUCT_OR_CLASS(IEventListener);
#ifdef VBOX_WITH_EXTPACK
class ExtPackManager;
#endif

#include <VBox/RemoteDesktop/VRDE.h>
#include <VBox/pdmdrv.h>
#ifdef VBOX_WITH_GUEST_PROPS
# include <VBox/HostServices/GuestPropertySvc.h>  /* For the property notification callback */
#endif

#ifdef RT_OS_WINDOWS
# include "win/VBoxComEvents.h"
#endif

struct VUSBIRHCONFIG;
typedef struct VUSBIRHCONFIG *PVUSBIRHCONFIG;

#include <list>

// defines
///////////////////////////////////////////////////////////////////////////////

/**
 *  Checks the availability of the underlying VM device driver corresponding
 *  to the COM interface (IKeyboard, IMouse, IDisplay, etc.). When the driver is
 *  not available (NULL), sets error info and returns returns E_ACCESSDENIED.
 *  The translatable error message is defined in null context.
 *
 *  Intended to used only within Console children (i.e. Keyboard, Mouse,
 *  Display, etc.).
 *
 *  @param drv  driver pointer to check (compare it with NULL)
 */
#define CHECK_CONSOLE_DRV(drv) \
    do { \
        if (!(drv)) \
            return setError(E_ACCESSDENIED, tr("The console is not powered up")); \
    } while (0)

// Console
///////////////////////////////////////////////////////////////////////////////

/** IConsole implementation class */
class ATL_NO_VTABLE Console :
    public VirtualBoxBase,
    VBOX_SCRIPTABLE_IMPL(IConsole)
#ifdef RT_OS_WINDOWS
    , public CComCoClass<Console, &CLSID_Console>
#endif
{
    Q_OBJECT

public:

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(Console, IConsole)

    DECLARE_NOT_AGGREGATABLE(Console)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(Console)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IConsole)
        COM_INTERFACE_ENTRY(IDispatch)
    END_COM_MAP()

    Console();
    ~Console();

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializers/uninitializers for internal purposes only
    HRESULT init(IMachine *aMachine, IInternalMachineControl *aControl);
    void uninit();

    // IConsole properties
    STDMETHOD(COMGETTER(Machine))(IMachine **aMachine);
    STDMETHOD(COMGETTER(State))(MachineState_T *aMachineState);
    STDMETHOD(COMGETTER(Guest))(IGuest **aGuest);
    STDMETHOD(COMGETTER(Keyboard))(IKeyboard **aKeyboard);
    STDMETHOD(COMGETTER(Mouse))(IMouse **aMouse);
    STDMETHOD(COMGETTER(Display))(IDisplay **aDisplay);
    STDMETHOD(COMGETTER(Debugger))(IMachineDebugger **aDebugger);
    STDMETHOD(COMGETTER(USBDevices))(ComSafeArrayOut(IUSBDevice *, aUSBDevices));
    STDMETHOD(COMGETTER(RemoteUSBDevices))(ComSafeArrayOut(IHostUSBDevice *, aRemoteUSBDevices));
    STDMETHOD(COMGETTER(VRDEServerInfo))(IVRDEServerInfo **aVRDEServerInfo);
    STDMETHOD(COMGETTER(SharedFolders))(ComSafeArrayOut(ISharedFolder *, aSharedFolders));
    STDMETHOD(COMGETTER(EventSource)) (IEventSource ** aEventSource);

    // IConsole methods
    STDMETHOD(PowerUp)(IProgress **aProgress);
    STDMETHOD(PowerUpPaused)(IProgress **aProgress);
    STDMETHOD(PowerDown)(IProgress **aProgress);
    STDMETHOD(Reset)();
    STDMETHOD(Pause)();
    STDMETHOD(Resume)();
    STDMETHOD(PowerButton)();
    STDMETHOD(SleepButton)();
    STDMETHOD(GetPowerButtonHandled)(BOOL *aHandled);
    STDMETHOD(GetGuestEnteredACPIMode)(BOOL *aEntered);
    STDMETHOD(SaveState)(IProgress **aProgress);
    STDMETHOD(AdoptSavedState)(IN_BSTR aSavedStateFile);
    STDMETHOD(DiscardSavedState)(BOOL aRemoveFile);
    STDMETHOD(GetDeviceActivity)(DeviceType_T aDeviceType,
                                DeviceActivity_T *aDeviceActivity);
    STDMETHOD(AttachUSBDevice)(IN_BSTR aId);
    STDMETHOD(DetachUSBDevice)(IN_BSTR aId, IUSBDevice **aDevice);
    STDMETHOD(FindUSBDeviceByAddress)(IN_BSTR aAddress, IUSBDevice **aDevice);
    STDMETHOD(FindUSBDeviceById)(IN_BSTR aId, IUSBDevice **aDevice);
    STDMETHOD(CreateSharedFolder)(IN_BSTR aName, IN_BSTR aHostPath, BOOL aWritable, BOOL aAutoMount);
    STDMETHOD(RemoveSharedFolder)(IN_BSTR aName);
    STDMETHOD(TakeSnapshot)(IN_BSTR aName, IN_BSTR aDescription,
                            IProgress **aProgress);
    STDMETHOD(DeleteSnapshot)(IN_BSTR aId, IProgress **aProgress);
    STDMETHOD(RestoreSnapshot)(ISnapshot *aSnapshot, IProgress **aProgress);
    STDMETHOD(Teleport)(IN_BSTR aHostname, ULONG aPort, IN_BSTR aPassword, ULONG aMaxDowntime, IProgress **aProgress);

    // public methods for internal purposes only

    /*
     *  Note: the following methods do not increase refcount. intended to be
     *  called only by the VM execution thread.
     */

    Guest *getGuest() const { return mGuest; }
    Keyboard *getKeyboard() const { return mKeyboard; }
    Mouse *getMouse() const { return mMouse; }
    Display *getDisplay() const { return mDisplay; }
    MachineDebugger *getMachineDebugger() const { return mDebugger; }

    const ComPtr<IMachine> &machine() const { return mMachine; }

    /** Method is called only from ConsoleVRDPServer */
    IVRDEServer *getVRDEServer() const { return mVRDEServer; }

    ConsoleVRDPServer *consoleVRDPServer() const { return mConsoleVRDPServer; }

    HRESULT updateMachineState(MachineState_T aMachineState);

    // events from IInternalSessionControl
    HRESULT onNetworkAdapterChange(INetworkAdapter *aNetworkAdapter, BOOL changeAdapter);
    HRESULT onSerialPortChange(ISerialPort *aSerialPort);
    HRESULT onParallelPortChange(IParallelPort *aParallelPort);
    HRESULT onStorageControllerChange();
    HRESULT onMediumChange(IMediumAttachment *aMediumAttachment, BOOL aForce);
    HRESULT onCPUChange(ULONG aCPU, BOOL aRemove);
    HRESULT onCPUExecutionCapChange(ULONG aExecutionCap);
    HRESULT onVRDEServerChange(BOOL aRestart);
    HRESULT onUSBControllerChange();
    HRESULT onSharedFolderChange(BOOL aGlobal);
    HRESULT onUSBDeviceAttach(IUSBDevice *aDevice, IVirtualBoxErrorInfo *aError, ULONG aMaskedIfs);
    HRESULT onUSBDeviceDetach(IN_BSTR aId, IVirtualBoxErrorInfo *aError);
    HRESULT getGuestProperty(IN_BSTR aKey, BSTR *aValue, LONG64 *aTimestamp, BSTR *aFlags);
    HRESULT setGuestProperty(IN_BSTR aKey, IN_BSTR aValue, IN_BSTR aFlags);
    HRESULT enumerateGuestProperties(IN_BSTR aPatterns,
                                     ComSafeArrayOut(BSTR, aNames),
                                     ComSafeArrayOut(BSTR, aValues),
                                     ComSafeArrayOut(LONG64, aTimestamps),
                                     ComSafeArrayOut(BSTR, aFlags));
    HRESULT onlineMergeMedium(IMediumAttachment *aMediumAttachment,
                              ULONG aSourceIdx, ULONG aTargetIdx,
                              IMedium *aSource, IMedium *aTarget,
                              BOOL aMergeForward, IMedium *aParentForTarget,
                              ComSafeArrayIn(IMedium *, aChildrenToReparent),
                              IProgress *aProgress);
    VMMDev *getVMMDev() { return m_pVMMDev; }
    AudioSniffer *getAudioSniffer() { return mAudioSniffer; }
#ifdef VBOX_WITH_EXTPACK
    ExtPackManager *getExtPackManager();
#endif

    int VRDPClientLogon(uint32_t u32ClientId, const char *pszUser, const char *pszPassword, const char *pszDomain);
    void VRDPClientConnect(uint32_t u32ClientId);
    void VRDPClientDisconnect(uint32_t u32ClientId, uint32_t fu32Intercepted);
    void VRDPInterceptAudio(uint32_t u32ClientId);
    void VRDPInterceptUSB(uint32_t u32ClientId, void **ppvIntercept);
    void VRDPInterceptClipboard(uint32_t u32ClientId);

    void processRemoteUSBDevices(uint32_t u32ClientId, VRDEUSBDEVICEDESC *pDevList, uint32_t cbDevList);

    // callback callers (partly; for some events console callbacks are notified
    // directly from IInternalSessionControl event handlers declared above)
    void onMousePointerShapeChange(bool fVisible, bool fAlpha,
                                   uint32_t xHot, uint32_t yHot,
                                   uint32_t width, uint32_t height,
                                   ComSafeArrayIn(uint8_t, aShape));
    void onMouseCapabilityChange(BOOL supportsAbsolute, BOOL supportsRelative, BOOL needsHostCursor);
    void onStateChange(MachineState_T aMachineState);
    void onAdditionsStateChange();
    void onAdditionsOutdated();
    void onKeyboardLedsChange(bool fNumLock, bool fCapsLock, bool fScrollLock);
    void onUSBDeviceStateChange(IUSBDevice *aDevice, bool aAttached,
                                IVirtualBoxErrorInfo *aError);
    void onRuntimeError(BOOL aFatal, IN_BSTR aErrorID, IN_BSTR aMessage);
    HRESULT onShowWindow(BOOL aCheck, BOOL *aCanShow, LONG64 *aWinId);
    void onVRDEServerInfoChange();

    static const PDMDRVREG DrvStatusReg;

    static HRESULT setErrorStatic(HRESULT aResultCode, const char *pcsz, ...);
    HRESULT setInvalidMachineStateError();

    static HRESULT handleUnexpectedExceptions(RT_SRC_POS_DECL);

    static const char *convertControllerTypeToDev(StorageControllerType_T enmCtrlType);
    static HRESULT convertBusPortDeviceToLun(StorageBus_T enmBus, LONG port, LONG device, unsigned &uLun);
    // Called from event listener
    HRESULT onNATRedirectRuleChange(ULONG ulInstance, BOOL aNatRuleRemove,
                                 NATProtocol_T aProto, IN_BSTR aHostIp, LONG aHostPort, IN_BSTR aGuestIp, LONG aGuestPort);

private:

    /**
     *  Base template for AutoVMCaller and SaveVMPtr. Template arguments
     *  have the same meaning as arguments of Console::addVMCaller().
     */
    template <bool taQuiet = false, bool taAllowNullVM = false>
    class AutoVMCallerBase
    {
    public:
        AutoVMCallerBase(Console *aThat) : mThat(aThat), mRC(S_OK)
        {
            Assert(aThat);
            mRC = aThat->addVMCaller(taQuiet, taAllowNullVM);
        }
        ~AutoVMCallerBase()
        {
            if (SUCCEEDED(mRC))
                mThat->releaseVMCaller();
        }
        /** Decreases the number of callers before the instance is destroyed. */
        void release()
        {
            AssertReturnVoid(SUCCEEDED(mRC));
            mThat->releaseVMCaller();
            mRC = E_FAIL;
        }
        /** Restores the number of callers after by #release(). #rc() must be
         *  rechecked to ensure the operation succeeded. */
        void add()
        {
            AssertReturnVoid(!SUCCEEDED(mRC));
            mRC = mThat->addVMCaller(taQuiet, taAllowNullVM);
        }
        /** Returns the result of Console::addVMCaller() */
        HRESULT rc() const { return mRC; }
        /** Shortcut to SUCCEEDED(rc()) */
        bool isOk() const { return SUCCEEDED(mRC); }
    protected:
        Console *mThat;
        HRESULT mRC;
    private:
        DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(AutoVMCallerBase)
        DECLARE_CLS_NEW_DELETE_NOOP(AutoVMCallerBase)
    };

    /**
     *  Helper class that protects sections of code using the mpVM pointer by
     *  automatically calling addVMCaller() on construction and
     *  releaseVMCaller() on destruction. Intended for Console methods dealing
     *  with mpVM. The usage pattern is:
     *  <code>
     *      AutoVMCaller autoVMCaller(this);
     *      if (FAILED(autoVMCaller.rc())) return autoVMCaller.rc();
     *      ...
     *      VMR3ReqCall (mpVM, ...
     *  </code>
     *
     *  @note Temporarily locks the argument for writing.
     *
     *  @sa SafeVMPtr, SafeVMPtrQuiet
     */
    typedef AutoVMCallerBase <false, false> AutoVMCaller;

    /**
     *  Same as AutoVMCaller but doesn't set extended error info on failure.
     *
     *  @note Temporarily locks the argument for writing.
     */
    typedef AutoVMCallerBase <true, false> AutoVMCallerQuiet;

    /**
     *  Same as AutoVMCaller but allows a null VM pointer (to trigger an error
     *  instead of assertion).
     *
     *  @note Temporarily locks the argument for writing.
     */
    typedef AutoVMCallerBase <false, true> AutoVMCallerWeak;

    /**
     *  Same as AutoVMCaller but doesn't set extended error info on failure
     *  and allows a null VM pointer (to trigger an error instead of
     *  assertion).
     *
     *  @note Temporarily locks the argument for writing.
     */
    typedef AutoVMCallerBase <true, true> AutoVMCallerQuietWeak;

    /**
     *  Base template for SaveVMPtr and SaveVMPtrQuiet.
     */
    template <bool taQuiet = false>
    class SafeVMPtrBase : public AutoVMCallerBase <taQuiet, true>
    {
        typedef AutoVMCallerBase <taQuiet, true> Base;
    public:
        SafeVMPtrBase(Console *aThat) : Base(aThat), mpVM(NULL)
        {
            if (SUCCEEDED(Base::mRC))
                mpVM = aThat->mpVM;
        }
        /** Smart SaveVMPtr to PVM cast operator */
        operator PVM() const { return mpVM; }
        /** Direct PVM access for printf()-like functions */
        PVM raw() const { return mpVM; }
    private:
        PVM mpVM;
        DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(SafeVMPtrBase)
        DECLARE_CLS_NEW_DELETE_NOOP(SafeVMPtrBase)
    };

public:

    /**
     *  Helper class that safely manages the Console::mpVM pointer
     *  by calling addVMCaller() on construction and releaseVMCaller() on
     *  destruction. Intended for Console children. The usage pattern is:
     *  <code>
     *      Console::SaveVMPtr pVM(mParent);
     *      if (FAILED(pVM.rc())) return pVM.rc();
     *      ...
     *      VMR3ReqCall(pVM, ...
     *      ...
     *      printf("%p\n", pVM.raw());
     *  </code>
     *
     *  @note Temporarily locks the argument for writing.
     *
     *  @sa SafeVMPtrQuiet, AutoVMCaller
     */
    typedef SafeVMPtrBase <false> SafeVMPtr;

    /**
     *  A deviation of SaveVMPtr that doesn't set the error info on failure.
     *  Intended for pieces of code that don't need to return the VM access
     *  failure to the caller. The usage pattern is:
     *  <code>
     *      Console::SaveVMPtrQuiet pVM(mParent);
     *      if (pVM.rc())
     *          VMR3ReqCall(pVM, ...
     *      return S_OK;
     *  </code>
     *
     *  @note Temporarily locks the argument for writing.
     *
     *  @sa SafeVMPtr, AutoVMCaller
     */
    typedef SafeVMPtrBase <true> SafeVMPtrQuiet;

    class SharedFolderData
    {
    public:
        SharedFolderData() {}
        SharedFolderData(Bstr aHostPath, BOOL aWritable, BOOL aAutoMount)
           : mHostPath(aHostPath)
           , mWritable(aWritable)
           , mAutoMount(aAutoMount) {}
        SharedFolderData(const SharedFolderData& aThat)
           : mHostPath(aThat.mHostPath)
           , mWritable(aThat.mWritable)
           , mAutoMount(aThat.mAutoMount) {}
        Bstr mHostPath;
        BOOL mWritable;
        BOOL mAutoMount;
    };
    typedef std::map <Bstr, ComObjPtr<SharedFolder> > SharedFolderMap;
    typedef std::map <Bstr, SharedFolderData> SharedFolderDataMap;

private:

    typedef std::list <ComObjPtr<OUSBDevice> > USBDeviceList;
    typedef std::list <ComObjPtr<RemoteUSBDevice> > RemoteUSBDeviceList;

    HRESULT addVMCaller(bool aQuiet = false, bool aAllowNullVM = false);
    void releaseVMCaller();

    HRESULT consoleInitReleaseLog(const ComPtr<IMachine> aMachine);

    HRESULT powerUp(IProgress **aProgress, bool aPaused);
    HRESULT powerDown(Progress *aProgress = NULL);

    HRESULT callTapSetupApplication(bool isStatic, RTFILE tapFD, Bstr &tapDevice,
                                    Bstr &tapSetupApplication);
#if ((defined(RT_OS_LINUX) && !defined(VBOX_WITH_NETFLT)) || defined(RT_OS_FREEBSD))
    HRESULT attachToTapInterface(INetworkAdapter *networkAdapter);
    HRESULT detachFromTapInterface(INetworkAdapter *networkAdapter);
#endif
    HRESULT powerDownHostInterfaces();

    HRESULT setMachineState(MachineState_T aMachineState, bool aUpdateServer = true);
    HRESULT setMachineStateLocally(MachineState_T aMachineState)
    {
        return setMachineState(aMachineState, false /* aUpdateServer */);
    }

    HRESULT findSharedFolder(CBSTR aName,
                             ComObjPtr<SharedFolder> &aSharedFolder,
                             bool aSetError = false);

    HRESULT fetchSharedFolders(BOOL aGlobal);
    bool findOtherSharedFolder(IN_BSTR aName,
                               SharedFolderDataMap::const_iterator &aIt);

    HRESULT createSharedFolder(CBSTR aName, SharedFolderData aData);
    HRESULT removeSharedFolder(CBSTR aName);

    static DECLCALLBACK(int) configConstructor(PVM pVM, void *pvConsole);

    int configMediumAttachment(PCFGMNODE pCtlInst,
                               const char *pcszDevice,
                               unsigned uInstance,
                               StorageBus_T enmBus,
                               bool fUseHostIOCache,
                               bool fSetupMerge,
                               unsigned uMergeSource,
                               unsigned uMergeTarget,
                               IMediumAttachment *pMediumAtt,
                               MachineState_T aMachineState,
                               HRESULT *phrc,
                               bool fAttachDetach,
                               bool fForceUnmount,
                               PVM pVM,
                               DeviceType_T *paLedDevType);
    int configMedium(PCFGMNODE pLunL0,
                     bool fPassthrough,
                     DeviceType_T enmType,
                     bool fUseHostIOCache,
                     bool fSetupMerge,
                     unsigned uMergeSource,
                     unsigned uMergeTarget,
                     IMedium *pMedium,
                     MachineState_T aMachineState,
                     HRESULT *phrc);
    static DECLCALLBACK(int) reconfigureMediumAttachment(Console *pConsole,
                                                         PVM pVM,
                                                         const char *pcszDevice,
                                                         unsigned uInstance,
                                                         StorageBus_T enmBus,
                                                         bool fUseHostIOCache,
                                                         bool fSetupMerge,
                                                         unsigned uMergeSource,
                                                         unsigned uMergeTarget,
                                                         IMediumAttachment *aMediumAtt,
                                                         MachineState_T aMachineState,
                                                         HRESULT *phrc);
    static DECLCALLBACK(int) changeRemovableMedium(Console *pThis,
                                                   const char *pcszDevice,
                                                   unsigned uInstance,
                                                   StorageBus_T enmBus,
                                                   bool fUseHostIOCache,
                                                   IMediumAttachment *aMediumAtt,
                                                   bool fForce);

    int configNetwork(const char *pszDevice, unsigned uInstance, unsigned uLun,
                      INetworkAdapter *aNetworkAdapter, PCFGMNODE pCfg,
                      PCFGMNODE pLunL0, PCFGMNODE pInst, bool fAttachDetach);

    static DECLCALLBACK(int) configGuestProperties(void *pvConsole);
    static DECLCALLBACK(int) configGuestControl(void *pvConsole);
    static DECLCALLBACK(void) vmstateChangeCallback(PVM aVM, VMSTATE aState,
                                                    VMSTATE aOldState, void *aUser);
    static DECLCALLBACK(int) unplugCpu(Console *pThis, unsigned uCpu);
    static DECLCALLBACK(int) plugCpu(Console *pThis, unsigned uCpu);
    HRESULT doMediumChange(IMediumAttachment *aMediumAttachment, bool fForce);
    HRESULT doCPURemove(ULONG aCpu);
    HRESULT doCPUAdd(ULONG aCpu);

    HRESULT doNetworkAdapterChange(const char *pszDevice, unsigned uInstance,
                                   unsigned uLun, INetworkAdapter *aNetworkAdapter);
    static DECLCALLBACK(int) changeNetworkAttachment(Console *pThis, const char *pszDevice,
                                                     unsigned uInstance, unsigned uLun,
                                                     INetworkAdapter *aNetworkAdapter);

#ifdef VBOX_WITH_USB
    HRESULT attachUSBDevice(IUSBDevice *aHostDevice, ULONG aMaskedIfs);
    HRESULT detachUSBDevice(USBDeviceList::iterator &aIt);

    static DECLCALLBACK(int) usbAttachCallback(Console *that, IUSBDevice *aHostDevice, PCRTUUID aUuid,
                       bool aRemote, const char *aAddress, ULONG aMaskedIfs);
    static DECLCALLBACK(int) usbDetachCallback(Console *that, USBDeviceList::iterator *aIt, PCRTUUID aUuid);
#endif

    static DECLCALLBACK(int)    fntTakeSnapshotWorker(RTTHREAD Thread, void *pvUser);

    static DECLCALLBACK(int)    stateProgressCallback(PVM pVM, unsigned uPercent, void *pvUser);

    static DECLCALLBACK(void)   genericVMSetErrorCallback(PVM pVM, void *pvUser, int rc, RT_SRC_POS_DECL,
                                                          const char *pszErrorFmt, va_list va);

    static DECLCALLBACK(void) setVMRuntimeErrorCallbackF(PVM pVM, void *pvUser, uint32_t fFatal,
                               const char *pszErrorId,
                               const char *pszFormat, ...);
    static DECLCALLBACK(void) setVMRuntimeErrorCallback(PVM pVM, void *pvUser, uint32_t fFatal,
                               const char *pszErrorId,
                               const char *pszFormat, va_list va);

    HRESULT                     captureUSBDevices(PVM pVM);
    void                        detachAllUSBDevices(bool aDone);

    static DECLCALLBACK(int)   powerUpThread(RTTHREAD Thread, void *pvUser);
    static DECLCALLBACK(int)   saveStateThread(RTTHREAD Thread, void *pvUser);
    static DECLCALLBACK(int)   powerDownThread(RTTHREAD Thread, void *pvUser);

    static DECLCALLBACK(int)    vmm2User_SaveState(PCVMM2USERMETHODS pThis, PVM pVM);

    static DECLCALLBACK(void *) drvStatus_QueryInterface(PPDMIBASE pInterface, const char *pszIID);
    static DECLCALLBACK(void)   drvStatus_UnitChanged(PPDMILEDCONNECTORS pInterface, unsigned iLUN);
    static DECLCALLBACK(void)   drvStatus_Destruct(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(int)    drvStatus_Construct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);

    int mcAudioRefs;
    volatile uint32_t mcVRDPClients;
    uint32_t mu32SingleRDPClientId; /* The id of a connected client in the single connection mode. */
    volatile  bool mcGuestCredentialsProvided;

    static const char *sSSMConsoleUnit;
    static uint32_t sSSMConsoleVer;

    HRESULT loadDataFromSavedState();
    int loadStateFileExecInternal(PSSMHANDLE pSSM, uint32_t u32Version);

    static DECLCALLBACK(void)   saveStateFileExec(PSSMHANDLE pSSM, void *pvUser);
    static DECLCALLBACK(int)    loadStateFileExec(PSSMHANDLE pSSM, void *pvUser, uint32_t uVersion, uint32_t uPass);

#ifdef VBOX_WITH_GUEST_PROPS
    static DECLCALLBACK(int)    doGuestPropNotification(void *pvExtension, uint32_t, void *pvParms, uint32_t cbParms);
    HRESULT                     doEnumerateGuestProperties(CBSTR aPatterns,
                                                           ComSafeArrayOut(BSTR, aNames),
                                                           ComSafeArrayOut(BSTR, aValues),
                                                           ComSafeArrayOut(LONG64, aTimestamps),
                                                           ComSafeArrayOut(BSTR, aFlags));

    bool enabledGuestPropertiesVRDP(void);
    void updateGuestPropertiesVRDPLogon(uint32_t u32ClientId, const char *pszUser, const char *pszDomain);
    void updateGuestPropertiesVRDPDisconnect(uint32_t u32ClientId);
#endif

    /** @name Teleporter support
     * @{ */
    static DECLCALLBACK(int)    teleporterSrcThreadWrapper(RTTHREAD hThread, void *pvUser);
    HRESULT                     teleporterSrc(TeleporterStateSrc *pState);
    HRESULT                     teleporterSrcReadACK(TeleporterStateSrc *pState, const char *pszWhich, const char *pszNAckMsg = NULL);
    HRESULT                     teleporterSrcSubmitCommand(TeleporterStateSrc *pState, const char *pszCommand, bool fWaitForAck = true);
    HRESULT                     teleporterTrg(PVM pVM, IMachine *pMachine, Utf8Str *pErrorMsg, bool fStartPaused,
                                              Progress *pProgress, bool *pfPowerOffOnFailure);
    static DECLCALLBACK(int)    teleporterTrgServeConnection(RTSOCKET Sock, void *pvUser);
    /** @} */

    bool mSavedStateDataLoaded : 1;

    const ComPtr<IMachine> mMachine;
    const ComPtr<IInternalMachineControl> mControl;

    const ComPtr<IVRDEServer> mVRDEServer;

    ConsoleVRDPServer * const mConsoleVRDPServer;

    const ComObjPtr<Guest> mGuest;
    const ComObjPtr<Keyboard> mKeyboard;
    const ComObjPtr<Mouse> mMouse;
    const ComObjPtr<Display> mDisplay;
    const ComObjPtr<MachineDebugger> mDebugger;
    const ComObjPtr<VRDEServerInfo> mVRDEServerInfo;
    const ComObjPtr<EventSource> mEventSource;
#ifdef VBOX_WITH_EXTPACK
    const ComObjPtr<ExtPackManager> mptrExtPackManager;
#endif

    USBDeviceList mUSBDevices;
    RemoteUSBDeviceList mRemoteUSBDevices;

    SharedFolderMap mSharedFolders;
    SharedFolderDataMap mMachineSharedFolders;
    SharedFolderDataMap mGlobalSharedFolders;

    /** The VM instance handle. */
    PVM mpVM;
    /** Holds the number of "readonly" mpVM callers (users) */
    uint32_t mVMCallers;
    /** Semaphore posted when the number of mpVM callers drops to zero */
    RTSEMEVENT mVMZeroCallersSem;
    /** true when Console has entered the mpVM destruction phase */
    bool mVMDestroying : 1;
    /** true when power down is initiated by vmstateChangeCallback (EMT) */
    bool mVMPoweredOff : 1;
    /** true when vmstateChangeCallback shouldn't initiate a power down.  */
    bool mVMIsAlreadyPoweringOff : 1;
    /** true if we already showed the snapshot folder size warning. */
    bool mfSnapshotFolderSizeWarningShown : 1;
    /** true if we already showed the snapshot folder ext4/xfs bug warning. */
    bool mfSnapshotFolderExt4WarningShown : 1;

    /** Pointer to the VMM -> User (that's us) callbacks.
     * This structure is followed by a pointer to the Console object. */
    PCVMM2USERMETHODS mpVmm2UserMethods;

    /** The current network attachment type in the VM.
     * This doesn't have to match the network attachment type maintained in the
     * NetworkAdapter. This is needed to change the network attachment
     * dynamically.
     */
    NetworkAttachmentType_T meAttachmentType[SchemaDefs::NetworkAdapterCount];

    VMMDev * m_pVMMDev;
    AudioSniffer * const mAudioSniffer;

    enum
    {
        iLedFloppy  = 0,
        cLedFloppy  = 1,
        iLedIde     = iLedFloppy + cLedFloppy,
        cLedIde     = 4,
        iLedSata    = iLedIde + cLedIde,
        cLedSata    = 30,
        iLedScsi    = iLedSata + cLedSata,
        cLedScsi    = 16,
        iLedSas     = iLedScsi + cLedScsi,
        cLedSas     = 8,
        cLedStorage = cLedFloppy + cLedIde + cLedSata + cLedScsi + cLedSas
    };
    DeviceType_T maStorageDevType[cLedStorage];
    PPDMLED      mapStorageLeds[cLedStorage];
    PPDMLED      mapNetworkLeds[SchemaDefs::NetworkAdapterCount];
    PPDMLED      mapSharedFolderLed;
    PPDMLED      mapUSBLed[2];
#if ((defined(RT_OS_LINUX) && !defined(VBOX_WITH_NETFLT)) || defined(RT_OS_FREEBSD))
    Utf8Str      maTAPDeviceName[8];
    RTFILE       maTapFD[8];
#endif

    bool mVMStateChangeCallbackDisabled;

    /** Local machine state value. */
    MachineState_T mMachineState;

    /** Pointer to the progress object of a live cancelable task.
     *
     * This is currently only used by Console::Teleport(), but is intended to later
     * be used by the live snapshot code path as well.  Actions like
     * Console::PowerDown, which automatically cancels out the running snapshot /
     * teleportation operation, will cancel the teleportation / live snapshot
     * operation before starting. */
    ComObjPtr<Progress> mptrCancelableProgress;

    struct
    {
        /** OnMousePointerShapeChange() cache */
        struct
        {
            bool valid;
            bool visible;
            bool alpha;
            uint32_t xHot;
            uint32_t yHot;
            uint32_t width;
            uint32_t height;
            com::SafeArray<BYTE> shape;
        }
        mpsc;

        /** OnMouseCapabilityChange() cache */
        struct
        {
            bool valid;
            BOOL supportsAbsolute;
            BOOL supportsRelative;
            BOOL needsHostCursor;
        }
        mcc;

        /** OnKeyboardLedsChange() cache */
        struct
        {
            bool valid;
            bool numLock;
            bool capsLock;
            bool scrollLock;
        }
        klc;

        void clear()
        {
            /* We cannot do memset() on mpsc to avoid cleaning shape's vtable */
            mpsc.shape.setNull();
            mpsc.valid = mpsc.visible = mpsc.alpha = false;
            mpsc.xHot = mpsc.yHot = mpsc.width = mpsc.height = 0;
            ::memset(&mcc, 0, sizeof mcc);
            ::memset(&klc, 0, sizeof klc);
        }
    }
    mCallbackData;
    COM_STRUCT_OR_CLASS(IEventListener) *mVmListner;

    friend struct VMTask;
};

#endif // !____H_CONSOLEIMPL
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
