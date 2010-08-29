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

#include "SystemPropertiesImpl.h"
#include "VirtualBoxImpl.h"
#include "MachineImpl.h"
#include "AutoCaller.h"
#include "Logging.h"

// generated header
#include "SchemaDefs.h"

#include <iprt/path.h>
#include <iprt/dir.h>
#include <iprt/cpp/utils.h>

#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/settings.h>
#include <VBox/VBoxHDD.h>

// defines
/////////////////////////////////////////////////////////////////////////////

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

SystemProperties::SystemProperties()
    : mParent(NULL),
      m(new settings::SystemProperties)
{
}

SystemProperties::~SystemProperties()
{
    delete m;
}


HRESULT SystemProperties::FinalConstruct()
{
    return S_OK;
}

void SystemProperties::FinalRelease()
{
    uninit();
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the system information object.
 *
 * @returns COM result indicator
 */
HRESULT SystemProperties::init(VirtualBox *aParent)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent, E_FAIL);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;

    setDefaultMachineFolder(Utf8Str::Empty);
    setDefaultHardDiskFolder(Utf8Str::Empty);
    setDefaultHardDiskFormat(Utf8Str::Empty);

    setRemoteDisplayAuthLibrary(Utf8Str::Empty);

    m->ulLogHistoryCount = 3;

    HRESULT rc = S_OK;

    /* Fetch info of all available hd backends. */

    /// @todo NEWMEDIA VDBackendInfo needs to be improved to let us enumerate
    /// any number of backends

    VDBACKENDINFO aVDInfo[100];
    unsigned cEntries;
    int vrc = VDBackendInfo(RT_ELEMENTS(aVDInfo), aVDInfo, &cEntries);
    AssertRC(vrc);
    if (RT_SUCCESS(vrc))
    {
        for (unsigned i = 0; i < cEntries; ++ i)
        {
            ComObjPtr<MediumFormat> hdf;
            rc = hdf.createObject();
            if (FAILED(rc)) break;

            rc = hdf->init(&aVDInfo[i]);
            if (FAILED(rc)) break;

            m_llMediumFormats.push_back(hdf);
        }
    }

    /* Confirm a successful initialization */
    if (SUCCEEDED(rc))
        autoInitSpan.setSucceeded();

    return rc;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void SystemProperties::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(mParent) = NULL;
}

// ISystemProperties properties
/////////////////////////////////////////////////////////////////////////////


STDMETHODIMP SystemProperties::COMGETTER(MinGuestRAM)(ULONG *minRAM)
{
    CheckComArgOutPointerValid(minRAM);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    AssertCompile(MM_RAM_MIN_IN_MB >= SchemaDefs::MinGuestRAM);
    *minRAM = MM_RAM_MIN_IN_MB;

    return S_OK;
}

STDMETHODIMP SystemProperties::COMGETTER(MaxGuestRAM)(ULONG *maxRAM)
{
    CheckComArgOutPointerValid(maxRAM);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    AssertCompile(MM_RAM_MAX_IN_MB <= SchemaDefs::MaxGuestRAM);
    ULONG maxRAMSys = MM_RAM_MAX_IN_MB;
    ULONG maxRAMArch = maxRAMSys;
    *maxRAM = RT_MIN(maxRAMSys, maxRAMArch);

    return S_OK;
}

STDMETHODIMP SystemProperties::COMGETTER(MinGuestVRAM)(ULONG *minVRAM)
{
    CheckComArgOutPointerValid(minVRAM);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    *minVRAM = SchemaDefs::MinGuestVRAM;

    return S_OK;
}

STDMETHODIMP SystemProperties::COMGETTER(MaxGuestVRAM)(ULONG *maxVRAM)
{
    CheckComArgOutPointerValid(maxVRAM);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    *maxVRAM = SchemaDefs::MaxGuestVRAM;

    return S_OK;
}

STDMETHODIMP SystemProperties::COMGETTER(MinGuestCPUCount)(ULONG *minCPUCount)
{
    CheckComArgOutPointerValid(minCPUCount);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    *minCPUCount = SchemaDefs::MinCPUCount; // VMM_MIN_CPU_COUNT

    return S_OK;
}

STDMETHODIMP SystemProperties::COMGETTER(MaxGuestCPUCount)(ULONG *maxCPUCount)
{
    CheckComArgOutPointerValid(maxCPUCount);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    *maxCPUCount = SchemaDefs::MaxCPUCount; // VMM_MAX_CPU_COUNT

    return S_OK;
}

STDMETHODIMP SystemProperties::COMGETTER(MaxGuestMonitors)(ULONG *maxMonitors)
{
    CheckComArgOutPointerValid(maxMonitors);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    *maxMonitors = SchemaDefs::MaxGuestMonitors;

    return S_OK;
}

STDMETHODIMP SystemProperties::COMGETTER(MaxVDISize)(LONG64 *maxVDISize)
{
    CheckComArgOutPointerValid(maxVDISize);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /** The BIOS supports currently 32 bit LBA numbers (implementing the full
     * 48 bit range is in theory trivial, but the crappy compiler makes things
     * more difficult). This translates to almost 2 TBytes (to be on the safe
     * side, the reported limit is 1 MiByte less than that, as the total number
     * of sectors should fit in 32 bits, too), which should bei enough for
     * the moment. The virtual ATA disks support complete LBA48 (although for
     * example iSCSI is also currently limited to 32 bit LBA), so the
     * theoretical maximum disk size is 128 PiByte. The user interface cannot
     * cope with this in a reasonable way yet. */
    /* no need to lock, this is const */
    *maxVDISize = 2048 * 1024 - 1;

    return S_OK;
}

STDMETHODIMP SystemProperties::COMGETTER(NetworkAdapterCount)(ULONG *count)
{
    CheckComArgOutPointerValid(count);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    *count = SchemaDefs::NetworkAdapterCount;

    return S_OK;
}

STDMETHODIMP SystemProperties::COMGETTER(SerialPortCount)(ULONG *count)
{
    CheckComArgOutPointerValid(count);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    *count = SchemaDefs::SerialPortCount;

    return S_OK;
}

STDMETHODIMP SystemProperties::COMGETTER(ParallelPortCount)(ULONG *count)
{
    CheckComArgOutPointerValid(count);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    *count = SchemaDefs::ParallelPortCount;

    return S_OK;
}

STDMETHODIMP SystemProperties::COMGETTER(MaxBootPosition)(ULONG *aMaxBootPosition)
{
    CheckComArgOutPointerValid(aMaxBootPosition);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    *aMaxBootPosition = SchemaDefs::MaxBootPosition;

    return S_OK;
}

STDMETHODIMP SystemProperties::GetMaxDevicesPerPortForStorageBus(StorageBus_T aBus,
                                                                 ULONG *aMaxDevicesPerPort)
{
    CheckComArgOutPointerValid(aMaxDevicesPerPort);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    switch (aBus)
    {
        case StorageBus_SATA:
        case StorageBus_SCSI:
        case StorageBus_SAS:
        {
            /* SATA and both SCSI controllers only support one device per port. */
            *aMaxDevicesPerPort = 1;
            break;
        }
        case StorageBus_IDE:
        case StorageBus_Floppy:
        {
            /* The IDE and Floppy controllers support 2 devices. One as master
             * and one as slave (or floppy drive 0 and 1). */
            *aMaxDevicesPerPort = 2;
            break;
        }
        default:
            AssertMsgFailed(("Invalid bus type %d\n", aBus));
    }

    return S_OK;
}

STDMETHODIMP SystemProperties::GetMinPortCountForStorageBus(StorageBus_T aBus,
                                                            ULONG *aMinPortCount)
{
    CheckComArgOutPointerValid(aMinPortCount);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    switch (aBus)
    {
        case StorageBus_SATA:
        {
            *aMinPortCount = 1;
            break;
        }
        case StorageBus_SCSI:
        {
            *aMinPortCount = 16;
            break;
        }
        case StorageBus_IDE:
        {
            *aMinPortCount = 2;
            break;
        }
        case StorageBus_Floppy:
        {
            *aMinPortCount = 1;
            break;
        }
        case StorageBus_SAS:
        {
            *aMinPortCount = 8;
            break;
        }
        default:
            AssertMsgFailed(("Invalid bus type %d\n", aBus));
    }

    return S_OK;
}

STDMETHODIMP SystemProperties::GetMaxPortCountForStorageBus(StorageBus_T aBus,
                                                            ULONG *aMaxPortCount)
{
    CheckComArgOutPointerValid(aMaxPortCount);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    switch (aBus)
    {
        case StorageBus_SATA:
        {
            *aMaxPortCount = 30;
            break;
        }
        case StorageBus_SCSI:
        {
            *aMaxPortCount = 16;
            break;
        }
        case StorageBus_IDE:
        {
            *aMaxPortCount = 2;
            break;
        }
        case StorageBus_Floppy:
        {
            *aMaxPortCount = 1;
            break;
        }
        case StorageBus_SAS:
        {
            *aMaxPortCount = 8;
            break;
        }
        default:
            AssertMsgFailed(("Invalid bus type %d\n", aBus));
    }

    return S_OK;
}

STDMETHODIMP SystemProperties::GetMaxInstancesOfStorageBus(StorageBus_T aBus,
                                                           ULONG *aMaxInstances)
{
    CheckComArgOutPointerValid(aMaxInstances);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    switch (aBus)
    {
        case StorageBus_SATA:
        case StorageBus_SCSI:
        case StorageBus_IDE:
        case StorageBus_SAS:
        case StorageBus_Floppy:
        {
            /** @todo raise the limits ASAP, per bus type */
            *aMaxInstances = 1;
            break;
        }
        default:
            AssertMsgFailed(("Invalid bus type %d\n", aBus));
    }

    return S_OK;
}

STDMETHODIMP SystemProperties::GetDeviceTypesForStorageBus(StorageBus_T aBus,
                                                           ComSafeArrayOut(DeviceType_T, aDeviceTypes))
{
    CheckComArgOutSafeArrayPointerValid(aDeviceTypes);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    switch (aBus)
    {
        case StorageBus_IDE:
        case StorageBus_SATA:
        {
            com::SafeArray<DeviceType_T> saDeviceTypes(2);
            saDeviceTypes[0] = DeviceType_DVD;
            saDeviceTypes[1] = DeviceType_HardDisk;
            saDeviceTypes.detachTo(ComSafeArrayOutArg(aDeviceTypes));
            break;
        }
        case StorageBus_SCSI:
        case StorageBus_SAS:
        {
            com::SafeArray<DeviceType_T> saDeviceTypes(1);
            saDeviceTypes[0] = DeviceType_HardDisk;
            saDeviceTypes.detachTo(ComSafeArrayOutArg(aDeviceTypes));
            break;
        }
        case StorageBus_Floppy:
        {
            com::SafeArray<DeviceType_T> saDeviceTypes(1);
            saDeviceTypes[0] = DeviceType_Floppy;
            saDeviceTypes.detachTo(ComSafeArrayOutArg(aDeviceTypes));
            break;
        }
        default:
            AssertMsgFailed(("Invalid bus type %d\n", aBus));
    }

    return S_OK;
}

STDMETHODIMP SystemProperties::GetDefaultIoCacheSettingForStorageController(StorageControllerType_T aControllerType, BOOL *aEnabled)
{
    CheckComArgOutPointerValid(aEnabled);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, this is const */
    switch (aControllerType)
    {
        case StorageControllerType_LsiLogic:
        case StorageControllerType_BusLogic:
        case StorageControllerType_IntelAhci:
        case StorageControllerType_LsiLogicSas:
            *aEnabled = false;
            break;
        case StorageControllerType_PIIX3:
        case StorageControllerType_PIIX4:
        case StorageControllerType_ICH6:
        case StorageControllerType_I82078:
            *aEnabled = true;
            break;
        default:
            AssertMsgFailed(("Invalid controller type %d\n", aControllerType));
    }
    return S_OK;
}

STDMETHODIMP SystemProperties::COMGETTER(DefaultMachineFolder)(BSTR *aDefaultMachineFolder)
{
    CheckComArgOutPointerValid(aDefaultMachineFolder);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    m_strDefaultMachineFolderFull.cloneTo(aDefaultMachineFolder);

    return S_OK;
}

STDMETHODIMP SystemProperties::COMSETTER(DefaultMachineFolder)(IN_BSTR aDefaultMachineFolder)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    HRESULT rc = setDefaultMachineFolder(aDefaultMachineFolder);
    alock.release();

    if (SUCCEEDED(rc))
    {
        // VirtualBox::saveSettings() needs vbox write lock
        AutoWriteLock vboxLock(mParent COMMA_LOCKVAL_SRC_POS);
        rc = mParent->saveSettings();
    }

    return rc;
}

STDMETHODIMP SystemProperties::COMGETTER(DefaultHardDiskFolder)(BSTR *aDefaultHardDiskFolder)
{
    CheckComArgOutPointerValid(aDefaultHardDiskFolder);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    m_strDefaultHardDiskFolderFull.cloneTo(aDefaultHardDiskFolder);

    return S_OK;
}

STDMETHODIMP SystemProperties::COMSETTER(DefaultHardDiskFolder)(IN_BSTR aDefaultHardDiskFolder)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    HRESULT rc = setDefaultHardDiskFolder(aDefaultHardDiskFolder);
    alock.release();

    if (SUCCEEDED(rc))
    {
        // VirtualBox::saveSettings() needs vbox write lock
        AutoWriteLock vboxLock(mParent COMMA_LOCKVAL_SRC_POS);
        rc = mParent->saveSettings();
    }

    return rc;
}

STDMETHODIMP SystemProperties::COMGETTER(MediumFormats)(ComSafeArrayOut(IMediumFormat *, aMediumFormats))
{
    CheckComArgOutSafeArrayPointerValid(aMediumFormats);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    SafeIfaceArray<IMediumFormat> mediumFormats(m_llMediumFormats);
    mediumFormats.detachTo(ComSafeArrayOutArg(aMediumFormats));

    return S_OK;
}

STDMETHODIMP SystemProperties::COMGETTER(DefaultHardDiskFormat)(BSTR *aDefaultHardDiskFormat)
{
    CheckComArgOutPointerValid(aDefaultHardDiskFormat);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->strDefaultHardDiskFormat.cloneTo(aDefaultHardDiskFormat);

    return S_OK;
}

STDMETHODIMP SystemProperties::COMSETTER(DefaultHardDiskFormat)(IN_BSTR aDefaultHardDiskFormat)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    HRESULT rc = setDefaultHardDiskFormat(aDefaultHardDiskFormat);
    alock.release();

    if (SUCCEEDED(rc))
    {
        // VirtualBox::saveSettings() needs vbox write lock
        AutoWriteLock vboxLock(mParent COMMA_LOCKVAL_SRC_POS);
        rc = mParent->saveSettings();
    }

    return rc;
}

STDMETHODIMP SystemProperties::COMGETTER(FreeDiskSpaceWarning)(LONG64 *aFreeSpace)
{
    CheckComArgOutPointerValid(aFreeSpace);

    ReturnComNotImplemented();
}

STDMETHODIMP SystemProperties::COMSETTER(FreeDiskSpaceWarning)(LONG64 /* aFreeSpace */)
{
    ReturnComNotImplemented();
}

STDMETHODIMP SystemProperties::COMGETTER(FreeDiskSpacePercentWarning)(ULONG *aFreeSpacePercent)
{
    CheckComArgOutPointerValid(aFreeSpacePercent);

    ReturnComNotImplemented();
}

STDMETHODIMP SystemProperties::COMSETTER(FreeDiskSpacePercentWarning)(ULONG /* aFreeSpacePercent */)
{
    ReturnComNotImplemented();
}

STDMETHODIMP SystemProperties::COMGETTER(FreeDiskSpaceError)(LONG64 *aFreeSpace)
{
    CheckComArgOutPointerValid(aFreeSpace);

    ReturnComNotImplemented();
}

STDMETHODIMP SystemProperties::COMSETTER(FreeDiskSpaceError)(LONG64 /* aFreeSpace */)
{
    ReturnComNotImplemented();
}

STDMETHODIMP SystemProperties::COMGETTER(FreeDiskSpacePercentError)(ULONG *aFreeSpacePercent)
{
    CheckComArgOutPointerValid(aFreeSpacePercent);

    ReturnComNotImplemented();
}

STDMETHODIMP SystemProperties::COMSETTER(FreeDiskSpacePercentError)(ULONG /* aFreeSpacePercent */)
{
    ReturnComNotImplemented();
}

STDMETHODIMP SystemProperties::COMGETTER(RemoteDisplayAuthLibrary)(BSTR *aRemoteDisplayAuthLibrary)
{
    CheckComArgOutPointerValid(aRemoteDisplayAuthLibrary);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->strRemoteDisplayAuthLibrary.cloneTo(aRemoteDisplayAuthLibrary);

    return S_OK;
}

STDMETHODIMP SystemProperties::COMSETTER(RemoteDisplayAuthLibrary)(IN_BSTR aRemoteDisplayAuthLibrary)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    HRESULT rc = setRemoteDisplayAuthLibrary(aRemoteDisplayAuthLibrary);
    alock.release();

    if (SUCCEEDED(rc))
    {
        // VirtualBox::saveSettings() needs vbox write lock
        AutoWriteLock vboxLock(mParent COMMA_LOCKVAL_SRC_POS);
        rc = mParent->saveSettings();
    }

    return rc;
}

STDMETHODIMP SystemProperties::COMGETTER(WebServiceAuthLibrary)(BSTR *aWebServiceAuthLibrary)
{
    CheckComArgOutPointerValid(aWebServiceAuthLibrary);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->strWebServiceAuthLibrary.cloneTo(aWebServiceAuthLibrary);

    return S_OK;
}

STDMETHODIMP SystemProperties::COMSETTER(WebServiceAuthLibrary)(IN_BSTR aWebServiceAuthLibrary)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    HRESULT rc = setWebServiceAuthLibrary(aWebServiceAuthLibrary);
    alock.release();

    if (SUCCEEDED(rc))
    {
        // VirtualBox::saveSettings() needs vbox write lock
        AutoWriteLock vboxLock(mParent COMMA_LOCKVAL_SRC_POS);
        rc = mParent->saveSettings();
    }

    return rc;
}

STDMETHODIMP SystemProperties::COMGETTER(LogHistoryCount)(ULONG *count)
{
    CheckComArgOutPointerValid(count);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *count = m->ulLogHistoryCount;

    return S_OK;
}

STDMETHODIMP SystemProperties::COMSETTER(LogHistoryCount)(ULONG count)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->ulLogHistoryCount = count;
    alock.release();

    // VirtualBox::saveSettings() needs vbox write lock
    AutoWriteLock vboxLock(mParent COMMA_LOCKVAL_SRC_POS);
    HRESULT rc = mParent->saveSettings();

    return rc;
}

STDMETHODIMP SystemProperties::COMGETTER(DefaultAudioDriver)(AudioDriverType_T *aAudioDriver)
{
    CheckComArgOutPointerValid(aAudioDriver);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aAudioDriver = settings::MachineConfigFile::getHostDefaultAudioDriver();

    return S_OK;
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

HRESULT SystemProperties::loadSettings(const settings::SystemProperties &data)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT rc = S_OK;

    rc = setDefaultMachineFolder(data.strDefaultMachineFolder);
    if (FAILED(rc)) return rc;

    rc = setDefaultHardDiskFolder(data.strDefaultHardDiskFolder);
    if (FAILED(rc)) return rc;

    rc = setDefaultHardDiskFormat(data.strDefaultHardDiskFormat);
    if (FAILED(rc)) return rc;

    rc = setRemoteDisplayAuthLibrary(data.strRemoteDisplayAuthLibrary);
    if (FAILED(rc)) return rc;

    rc = setWebServiceAuthLibrary(data.strWebServiceAuthLibrary);
    if (FAILED(rc)) return rc;

    m->ulLogHistoryCount = data.ulLogHistoryCount;

    return S_OK;
}

HRESULT SystemProperties::saveSettings(settings::SystemProperties &data)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    data = *m;

    return S_OK;
}

/**
 * Returns a medium format object corresponding to the given format
 * identifier or null if no such format.
 *
 * @param aFormat   Format identifier.
 *
 * @return ComObjPtr<MediumFormat>
 */
ComObjPtr<MediumFormat> SystemProperties::mediumFormat(const Utf8Str &aFormat)
{
    ComObjPtr<MediumFormat> format;

    AutoCaller autoCaller(this);
    AssertComRCReturn (autoCaller.rc(), format);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    for (MediumFormatList::const_iterator it = m_llMediumFormats.begin();
         it != m_llMediumFormats.end();
         ++ it)
    {
        /* MediumFormat is all const, no need to lock */

        if ((*it)->getId().compare(aFormat, Utf8Str::CaseInsensitive) == 0)
        {
            format = *it;
            break;
        }
    }

    return format;
}

// private methods
/////////////////////////////////////////////////////////////////////////////

HRESULT SystemProperties::setDefaultMachineFolder(const Utf8Str &aPath)
{
    Utf8Str path(aPath);
    if (path.isEmpty())
        path = "Machines";

    /* get the full file name */
    Utf8Str folder;
    int vrc = mParent->calculateFullPath(path, folder);
    if (RT_FAILURE(vrc))
        return setError(E_FAIL,
                        tr("Invalid default machine folder '%s' (%Rrc)"),
                        path.c_str(),
                        vrc);

    m->strDefaultMachineFolder = path;
    m_strDefaultMachineFolderFull = folder;

    return S_OK;
}

HRESULT SystemProperties::setDefaultHardDiskFolder(const Utf8Str &aPath)
{
    Utf8Str path(aPath);
    if (path.isEmpty())
        path = "HardDisks";

    /* get the full file name */
    Utf8Str folder;
    int vrc = mParent->calculateFullPath(path, folder);
    if (RT_FAILURE(vrc))
        return setError(E_FAIL,
                        tr("Invalid default hard disk folder '%s' (%Rrc)"),
                        path.c_str(),
                        vrc);

    m->strDefaultHardDiskFolder = path;
    m_strDefaultHardDiskFolderFull = folder;

    return S_OK;
}

HRESULT SystemProperties::setDefaultHardDiskFormat(const Utf8Str &aFormat)
{
    if (!aFormat.isEmpty())
        m->strDefaultHardDiskFormat = aFormat;
    else
        m->strDefaultHardDiskFormat = "VDI";

    return S_OK;
}

HRESULT SystemProperties::setRemoteDisplayAuthLibrary(const Utf8Str &aPath)
{
    if (!aPath.isEmpty())
        m->strRemoteDisplayAuthLibrary = aPath;
    else
        m->strRemoteDisplayAuthLibrary = "VRDPAuth";

    return S_OK;
}

HRESULT SystemProperties::setWebServiceAuthLibrary(const Utf8Str &aPath)
{
    if (!aPath.isEmpty())
        m->strWebServiceAuthLibrary = aPath;
    else
        m->strWebServiceAuthLibrary = "VRDPAuth";

    return S_OK;
}
