/* $Id$ */
/** @file
 * Implemenation of IHost.
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

#ifndef ____H_HOSTIMPL
#define ____H_HOSTIMPL

#include "VirtualBoxBase.h"
#ifdef VBOX_WITH_USB
# include "HostUSBDeviceImpl.h"
# include "USBDeviceFilterImpl.h"
# include "USBProxyService.h"
# include "VirtualBoxImpl.h"
#else
class USBProxyService;
#endif
#include "HostPower.h"

#ifdef RT_OS_WINDOWS
# include "win/svchlp.h"
#endif

#ifdef VBOX_WITH_RESOURCE_USAGE_API
# include "PerformanceImpl.h"
#endif /* VBOX_WITH_RESOURCE_USAGE_API */

class VirtualBox;
class SessionMachine;
class HostDVDDrive;
class HostFloppyDrive;
class Progress;

#include <list>

class ATL_NO_VTABLE Host :
    public VirtualBoxBaseWithChildren,
    public VirtualBoxSupportErrorInfoImpl <Host, IHost>,
    public VirtualBoxSupportTranslation <Host>,
    public IHost
{
public:

    DECLARE_NOT_AGGREGATABLE(Host)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(Host)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IHost)
    END_COM_MAP()

    NS_DECL_ISUPPORTS

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init (VirtualBox *aParent);
    void uninit();

    // IHost properties
    STDMETHOD(COMGETTER(DVDDrives))(IHostDVDDriveCollection **drives);
    STDMETHOD(COMGETTER(FloppyDrives))(IHostFloppyDriveCollection **drives);
    STDMETHOD(COMGETTER(USBDevices))(IHostUSBDeviceCollection **aUSBDevices);
    STDMETHOD(COMGETTER(USBDeviceFilters))(IHostUSBDeviceFilterCollection ** aUSBDeviceFilters);
    STDMETHOD(COMGETTER(NetworkInterfaces))(IHostNetworkInterfaceCollection **networkInterfaces);
    STDMETHOD(COMGETTER(ProcessorCount))(ULONG *count);
    STDMETHOD(COMGETTER(ProcessorOnlineCount))(ULONG *count);
    STDMETHOD(GetProcessorSpeed)(ULONG cpuId, ULONG *speed);
    STDMETHOD(GetProcessorDescription)(ULONG cpuId, BSTR *description);
    STDMETHOD(GetProcessorFeature) (ProcessorFeature_T feature, BOOL *supported);
    STDMETHOD(COMGETTER(MemorySize))(ULONG *size);
    STDMETHOD(COMGETTER(MemoryAvailable))(ULONG *available);
    STDMETHOD(COMGETTER(OperatingSystem))(BSTR *os);
    STDMETHOD(COMGETTER(OSVersion))(BSTR *version);
    STDMETHOD(COMGETTER(UTCTime))(LONG64 *aUTCTime);

    // IHost methods
#ifdef RT_OS_WINDOWS
    STDMETHOD(CreateHostNetworkInterface) (INPTR BSTR aName,
                                           IHostNetworkInterface **aHostNetworkInterface,
                                           IProgress **aProgress);
    STDMETHOD(RemoveHostNetworkInterface) (INPTR GUIDPARAM aId,
                                           IHostNetworkInterface **aHostNetworkInterface,
                                           IProgress **aProgress);
#endif
    STDMETHOD(CreateUSBDeviceFilter) (INPTR BSTR aName, IHostUSBDeviceFilter **aFilter);
    STDMETHOD(InsertUSBDeviceFilter) (ULONG aPosition, IHostUSBDeviceFilter *aFilter);
    STDMETHOD(RemoveUSBDeviceFilter) (ULONG aPosition, IHostUSBDeviceFilter **aFilter);

    // public methods only for internal purposes

    HRESULT loadSettings (const settings::Key &aGlobal);
    HRESULT saveSettings (settings::Key &aGlobal);

#ifdef VBOX_WITH_USB
    typedef std::list <ComObjPtr <HostUSBDeviceFilter> > USBDeviceFilterList;

    /** Must be called from under this object's lock. */
    USBProxyService *usbProxyService() { return mUSBProxyService; }

    HRESULT onUSBDeviceFilterChange (HostUSBDeviceFilter *aFilter, BOOL aActiveChanged = FALSE);
    void getUSBFilters(USBDeviceFilterList *aGlobalFiltes, VirtualBox::SessionMachineVector *aMachines);
    HRESULT checkUSBProxyService();
#endif /* !VBOX_WITH_USB */

#ifdef RT_OS_WINDOWS
    static int networkInterfaceHelperServer (SVCHlpClient *aClient,
                                             SVCHlpMsg::Code aMsgCode);
#endif

    // for VirtualBoxSupportErrorInfoImpl
    static const wchar_t *getComponentName() { return L"Host"; }

private:

#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
# if defined(VBOX_WITH_LIBHAL) || defined(VBOX_USE_LIBHAL)
    bool getDVDInfoFromHal(std::list <ComObjPtr <HostDVDDrive> > &list);
    bool getFloppyInfoFromHal(std::list <ComObjPtr <HostFloppyDrive> > &list);
# endif
    void parseMountTable(char *mountTable, std::list <ComObjPtr <HostDVDDrive> > &list);
    bool validateDevice(const char *deviceNode, bool isCDROM);
#endif

#ifdef VBOX_WITH_USB
    /** specialization for IHostUSBDeviceFilter */
    ComObjPtr <HostUSBDeviceFilter> getDependentChild (IHostUSBDeviceFilter *aFilter)
    {
        VirtualBoxBase *child = VirtualBoxBaseWithChildren::
                                getDependentChild (ComPtr <IUnknown> (aFilter));
        return child ? dynamic_cast <HostUSBDeviceFilter *> (child)
                     : NULL;
    }
#endif /* VBOX_WITH_USB */

#ifdef RT_OS_WINDOWS
    static int createNetworkInterface (SVCHlpClient *aClient,
                                       const Utf8Str &aName,
                                       Guid &aGUID, Utf8Str &aErrMsg);
    static int removeNetworkInterface (SVCHlpClient *aClient,
                                       const Guid &aGUID,
                                       Utf8Str &aErrMsg);
    static HRESULT networkInterfaceHelperClient (SVCHlpClient *aClient,
                                                 Progress *aProgress,
                                                 void *aUser, int *aVrc);
#endif

#ifdef VBOX_WITH_RESOURCE_USAGE_API
    void registerMetrics (PerformanceCollector *aCollector);
    void unregisterMetrics (PerformanceCollector *aCollector);
#endif /* VBOX_WITH_RESOURCE_USAGE_API */

    ComObjPtr <VirtualBox, ComWeakRef> mParent;

#ifdef VBOX_WITH_USB
    USBDeviceFilterList mUSBDeviceFilters;

    /** Pointer to the USBProxyService object. */
    USBProxyService *mUSBProxyService;
#endif /* VBOX_WITH_USB */

    /* Features that can be queried with GetProcessorFeature */
    BOOL fVTxAMDVSupported, fLongModeSupported, fPAESupported;

    HostPowerService *mHostPowerService;
};

#endif // ____H_HOSTIMPL
