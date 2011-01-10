/** @file
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

#ifndef ____H_GUESTIMPL
#define ____H_GUESTIMPL

#include "VirtualBoxBase.h"
#include <iprt/list.h>
#include <VBox/ostypes.h>

#ifdef VBOX_WITH_GUEST_CONTROL
# include <VBox/HostServices/GuestControlSvc.h>
# include "HGCM.h"
using namespace guestControl;
#endif

typedef enum
{
    GUESTSTATTYPE_CPUUSER     = 0,
    GUESTSTATTYPE_CPUKERNEL   = 1,
    GUESTSTATTYPE_CPUIDLE     = 2,
    GUESTSTATTYPE_MEMTOTAL    = 3,
    GUESTSTATTYPE_MEMFREE     = 4,
    GUESTSTATTYPE_MEMBALLOON  = 5,
    GUESTSTATTYPE_MEMCACHE    = 6,
    GUESTSTATTYPE_PAGETOTAL   = 7,
    GUESTSTATTYPE_PAGEFREE    = 8,
    GUESTSTATTYPE_MAX         = 9
} GUESTSTATTYPE;

class Console;
#ifdef VBOX_WITH_GUEST_CONTROL
class Progress;
#endif

class ATL_NO_VTABLE Guest :
    public VirtualBoxBase,
    VBOX_SCRIPTABLE_IMPL(IGuest)
{
public:
    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(Guest, IGuest)

    DECLARE_NOT_AGGREGATABLE(Guest)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(Guest)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IGuest)
        COM_INTERFACE_ENTRY(IDispatch)
    END_COM_MAP()

    DECLARE_EMPTY_CTOR_DTOR (Guest)

    HRESULT FinalConstruct();
    void FinalRelease();

    // Public initializer/uninitializer for internal purposes only
    HRESULT init (Console *aParent);
    void uninit();

    // IGuest properties
    STDMETHOD(COMGETTER(OSTypeId)) (BSTR *aOSTypeId);
    STDMETHOD(COMGETTER(AdditionsRunLevel)) (AdditionsRunLevelType_T *aRunLevel);
    STDMETHOD(COMGETTER(AdditionsVersion)) (BSTR *aAdditionsVersion);
    /** @todo Remove later by replacing it by AdditionsFeatureAvailable(). */
    STDMETHOD(COMGETTER(SupportsSeamless)) (BOOL *aSupportsSeamless);
    STDMETHOD(COMGETTER(SupportsGraphics)) (BOOL *aSupportsGraphics);
#if 0
    /** @todo Will replace SupportsSeamless, SupportsGraphics, ... */
    STDMETHOD(COMGETTER(AdditionsFeatureAvailable)) (LONG64 aFeature, BOOL *aActive, BOOL *aAvailable);
#endif
    STDMETHOD(COMGETTER(MemoryBalloonSize)) (ULONG *aMemoryBalloonSize);
    STDMETHOD(COMSETTER(MemoryBalloonSize)) (ULONG aMemoryBalloonSize);
    STDMETHOD(COMGETTER(StatisticsUpdateInterval)) (ULONG *aUpdateInterval);
    STDMETHOD(COMSETTER(StatisticsUpdateInterval)) (ULONG aUpdateInterval);

    // IGuest methods
    STDMETHOD(GetAdditionsStatus)(AdditionsRunLevelType_T aLevel, BOOL *aActive);
    STDMETHOD(SetCredentials)(IN_BSTR aUserName, IN_BSTR aPassword,
                              IN_BSTR aDomain, BOOL aAllowInteractiveLogon);
    STDMETHOD(ExecuteProcess)(IN_BSTR aCommand, ULONG aFlags,
                              ComSafeArrayIn(IN_BSTR, aArguments), ComSafeArrayIn(IN_BSTR, aEnvironment),
                              IN_BSTR aUserName, IN_BSTR aPassword,
                              ULONG aTimeoutMS, ULONG *aPID, IProgress **aProgress);
    STDMETHOD(GetProcessOutput)(ULONG aPID, ULONG aFlags, ULONG aTimeoutMS, LONG64 aSize, ComSafeArrayOut(BYTE, aData));
    STDMETHOD(SetProcessInput)(ULONG aPID, ULONG aFlags, ULONG aTimeoutMS, ComSafeArrayIn(BYTE, aData), ULONG *aBytesWritten);
    STDMETHOD(GetProcessStatus)(ULONG aPID, ULONG *aExitCode, ULONG *aFlags, ULONG *aStatus);
    STDMETHOD(CopyToGuest)(IN_BSTR aSource, IN_BSTR aDest, IN_BSTR aUserName, IN_BSTR aPassword, ULONG aFlags, IProgress **aProgress);
    STDMETHOD(CreateDirectory)(IN_BSTR aDirectory, IN_BSTR aUserName, IN_BSTR aPassword, ULONG aMode, ULONG aFlags, IProgress **aProgress);
    STDMETHOD(InternalGetStatistics)(ULONG *aCpuUser, ULONG *aCpuKernel, ULONG *aCpuIdle,
                                     ULONG *aMemTotal, ULONG *aMemFree, ULONG *aMemBalloon, ULONG *aMemShared, ULONG *aMemCache,
                                     ULONG *aPageTotal, ULONG *aMemAllocTotal, ULONG *aMemFreeTotal, ULONG *aMemBalloonTotal, ULONG *aMemSharedTotal);
    STDMETHOD(UpdateGuestAdditions)(IN_BSTR aSource, ULONG aFlags, IProgress **aProgress);

    // Public methods that are not in IDL (only called internally).
    HRESULT executeProcessInternal(IN_BSTR aCommand, ULONG aFlags,
                                   ComSafeArrayIn(IN_BSTR, aArguments), ComSafeArrayIn(IN_BSTR, aEnvironment),
                                   IN_BSTR aUserName, IN_BSTR aPassword,
                                   ULONG aTimeoutMS, ULONG *aPID, IProgress **aProgress, int *pRC);
    HRESULT createDirectoryInternal(IN_BSTR aDirectory, IN_BSTR aUserName, IN_BSTR aPassword,
                                    ULONG aMode, ULONG aFlags, IProgress **aProgress, int *pRC);
    void setAdditionsInfo(Bstr aInterfaceVersion, VBOXOSTYPE aOsType);
    void setAdditionsInfo2(Bstr aAdditionsVersion, Bstr aVersionName, Bstr aRevision);
    void setAdditionsStatus(VBoxGuestStatusFacility Facility, VBoxGuestStatusCurrent Status, ULONG ulFlags);
    void setSupportedFeatures(uint32_t fCaps, uint32_t fActive);
    HRESULT setStatistic(ULONG aCpuId, GUESTSTATTYPE enmType, ULONG aVal);
    BOOL isPageFusionEnabled();
# ifdef VBOX_WITH_GUEST_CONTROL
    /** Static callback for handling guest notifications. */
    static DECLCALLBACK(int) doGuestCtrlNotification(void *pvExtension, uint32_t u32Function, void *pvParms, uint32_t cbParms);
# endif
    static HRESULT setErrorStatic(HRESULT aResultCode,
                                  const Utf8Str &aText)
    {
        return setErrorInternal(aResultCode, getStaticClassIID(), getStaticComponentName(), aText, false, true);
    }

private:

    // Internal tasks
    struct TaskGuest; /* Worker thread helper. */
#ifdef VBOX_WITH_GUEST_CONTROL
    HRESULT taskCopyFile(TaskGuest *aTask);
    HRESULT taskUpdateGuestAdditions(TaskGuest *aTask);

    struct CallbackContext
    {
        eVBoxGuestCtrlCallbackType  mType;
        /** Pointer to user-supplied data. */
        void                       *pvData;
        /** Size of user-supplied data. */
        uint32_t                    cbData;
        /** Pointer to user-supplied IProgress. */
        ComObjPtr<Progress>         pProgress;
    };
    /*
     * The map key is the context ID.
     */
    typedef std::map< uint32_t, CallbackContext > CallbackMap;
    typedef std::map< uint32_t, CallbackContext >::iterator CallbackMapIter;
    typedef std::map< uint32_t, CallbackContext >::const_iterator CallbackMapIterConst;

    struct GuestProcess
    {
        uint32_t                    mStatus;
        uint32_t                    mFlags;
        uint32_t                    mExitCode;
    };
    /*
     * The map key is the PID (process identifier).
     */
    typedef std::map< uint32_t, GuestProcess > GuestProcessMap;
    typedef std::map< uint32_t, GuestProcess >::iterator GuestProcessMapIter;
    typedef std::map< uint32_t, GuestProcess >::const_iterator GuestProcessMapIterConst;

    int directoryEntryAppend(const char *pszPath, PRTLISTNODE pList);
    int directoryRead(const char *pszDirectory, const char *pszFilter, ULONG uFlags, ULONG *pcObjects, PRTLISTNODE pList);

    int prepareExecuteEnv(const char *pszEnv, void **ppvList, uint32_t *pcbList, uint32_t *pcEnv);
    /** Handler for guest execution control notifications. */
    int notifyCtrlClientDisconnected(uint32_t u32Function, PCALLBACKDATACLIENTDISCONNECTED pData);
    int notifyCtrlExecStatus(uint32_t u32Function, PCALLBACKDATAEXECSTATUS pData);
    int notifyCtrlExecOut(uint32_t u32Function, PCALLBACKDATAEXECOUT pData);
    int notifyCtrlExecInStatus(uint32_t u32Function, PCALLBACKDATAEXECINSTATUS pData);
    CallbackMapIter getCtrlCallbackContextByID(uint32_t u32ContextID);
    GuestProcessMapIter getProcessByPID(uint32_t u32PID);
    void notifyCtrlCallbackContext(Guest::CallbackMapIter it, const char *pszText);
    void destroyCtrlCallbackContext(CallbackMapIter it);
    uint32_t addCtrlCallbackContext(eVBoxGuestCtrlCallbackType enmType, void *pvData, uint32_t cbData, Progress* pProgress);
    HRESULT waitForProcessStatusChange(ULONG uPID, ULONG *puRetStatus, ULONG *puRetExitCode, ULONG uTimeoutMS);
# endif

    struct Data
    {
        Data() : mAdditionsRunLevel (AdditionsRunLevelType_None),
                 mSupportsSeamless (FALSE),
                 mSupportsGraphics (FALSE) {}

        Bstr                    mOSTypeId;
        AdditionsRunLevelType_T mAdditionsRunLevel;
        Bstr                    mAdditionsVersion;
        Bstr                    mInterfaceVersion;
        BOOL                    mSupportsSeamless;
        BOOL                    mSupportsGraphics;
    };

    ULONG mMemoryBalloonSize;
    ULONG mStatUpdateInterval;
    ULONG mCurrentGuestStat[GUESTSTATTYPE_MAX];
    BOOL  mfPageFusionEnabled;

    Console *mParent;
    Data mData;

# ifdef VBOX_WITH_GUEST_CONTROL
    /** General extension callback for guest control. */
    HGCMSVCEXTHANDLE  mhExtCtrl;

    volatile uint32_t mNextContextID;
    CallbackMap mCallbackMap;
    GuestProcessMap mGuestProcessMap;
# endif
};

#endif // ____H_GUESTIMPL
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
