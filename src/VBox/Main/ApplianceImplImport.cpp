/* $Id$ */
/** @file
 *
 * IAppliance and IVirtualSystem COM class implementations.
 */

/*
 * Copyright (C) 2008-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include <iprt/path.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/s3.h>
#include <iprt/sha.h>
#include <iprt/manifest.h>
#include <iprt/tar.h>
#include <iprt/stream.h>

#include <VBox/com/array.h>

#include "ApplianceImpl.h"
#include "VirtualBoxImpl.h"
#include "GuestOSTypeImpl.h"
#include "ProgressImpl.h"
#include "MachineImpl.h"

#include "AutoCaller.h"
#include "Logging.h"

#include "ApplianceImplPrivate.h"

#include <VBox/param.h>
#include <VBox/version.h>
#include <VBox/settings.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////
//
// IAppliance public methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Public method implementation. This opens the OVF with ovfreader.cpp.
 * Thread implementation is in Appliance::readImpl().
 *
 * @param path
 * @return
 */
STDMETHODIMP Appliance::Read(IN_BSTR path, IProgress **aProgress)
{
    if (!path) return E_POINTER;
    CheckComArgOutPointerValid(aProgress);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (!isApplianceIdle())
        return E_ACCESSDENIED;

    if (m->pReader)
    {
        delete m->pReader;
        m->pReader = NULL;
    }

    // see if we can handle this file; for now we insist it has an ".ovf" extension
    Utf8Str strPath (path);
    if (!(   strPath.endsWith(".ovf", Utf8Str::CaseInsensitive)
          || strPath.endsWith(".ova", Utf8Str::CaseInsensitive)))
        return setError(VBOX_E_FILE_ERROR,
                        tr("Appliance file must have .ovf extension"));

    ComObjPtr<Progress> progress;
    HRESULT rc = S_OK;
    try
    {
        /* Parse all necessary info out of the URI */
        parseURI(strPath, m->locInfo);
        rc = readImpl(m->locInfo, progress);
    }
    catch (HRESULT aRC)
    {
        rc = aRC;
    }

    if (SUCCEEDED(rc))
        /* Return progress to the caller */
        progress.queryInterfaceTo(aProgress);

    return S_OK;
}

/**
 * Public method implementation. This looks at the output of ovfreader.cpp and creates
 * VirtualSystemDescription instances.
 * @return
 */
STDMETHODIMP Appliance::Interpret()
{
    // @todo:
    //  - don't use COM methods but the methods directly (faster, but needs appropriate locking of that objects itself (s. HardDisk))
    //  - Appropriate handle errors like not supported file formats
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (!isApplianceIdle())
        return E_ACCESSDENIED;

    HRESULT rc = S_OK;

    /* Clear any previous virtual system descriptions */
    m->virtualSystemDescriptions.clear();

    Utf8Str strDefaultHardDiskFolder;
    rc = getDefaultHardDiskFolder(strDefaultHardDiskFolder);
    if (FAILED(rc)) return rc;

    if (!m->pReader)
        return setError(E_FAIL,
                        tr("Cannot interpret appliance without reading it first (call read() before interpret())"));

    // Change the appliance state so we can safely leave the lock while doing time-consuming
    // disk imports; also the below method calls do all kinds of locking which conflicts with
    // the appliance object lock
    m->state = Data::ApplianceImporting;
    alock.release();

    /* Try/catch so we can clean up on error */
    try
    {
        list<ovf::VirtualSystem>::const_iterator it;
        /* Iterate through all virtual systems */
        for (it = m->pReader->m_llVirtualSystems.begin();
             it != m->pReader->m_llVirtualSystems.end();
             ++it)
        {
            const ovf::VirtualSystem &vsysThis = *it;

            ComObjPtr<VirtualSystemDescription> pNewDesc;
            rc = pNewDesc.createObject();
            if (FAILED(rc)) DebugBreakThrow(rc);
            rc = pNewDesc->init();
            if (FAILED(rc)) DebugBreakThrow(rc);

            // if the virtual system in OVF had a <vbox:Machine> element, have the
            // VirtualBox settings code parse that XML now
            if (vsysThis.pelmVboxMachine)
                pNewDesc->importVboxMachineXML(*vsysThis.pelmVboxMachine);

            /* Guest OS type */
            Utf8Str strOsTypeVBox,
                    strCIMOSType = Utf8StrFmt("%RI32", (uint32_t)vsysThis.cimos);
            convertCIMOSType2VBoxOSType(strOsTypeVBox, vsysThis.cimos, vsysThis.strCimosDesc);
            pNewDesc->addEntry(VirtualSystemDescriptionType_OS,
                               "",
                               strCIMOSType,
                               strOsTypeVBox);

            /* VM name */
            /* If the there isn't any name specified create a default one out of
             * the OS type */
            Utf8Str nameVBox = vsysThis.strName;
            if (nameVBox.isEmpty())
                nameVBox = strOsTypeVBox;
            searchUniqueVMName(nameVBox);
            pNewDesc->addEntry(VirtualSystemDescriptionType_Name,
                               "",
                               vsysThis.strName,
                               nameVBox);

            /* VM Product */
            if (!vsysThis.strProduct.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_Product,
                                    "",
                                    vsysThis.strProduct,
                                    vsysThis.strProduct);

            /* VM Vendor */
            if (!vsysThis.strVendor.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_Vendor,
                                    "",
                                    vsysThis.strVendor,
                                    vsysThis.strVendor);

            /* VM Version */
            if (!vsysThis.strVersion.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_Version,
                                    "",
                                    vsysThis.strVersion,
                                    vsysThis.strVersion);

            /* VM ProductUrl */
            if (!vsysThis.strProductUrl.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_ProductUrl,
                                    "",
                                    vsysThis.strProductUrl,
                                    vsysThis.strProductUrl);

            /* VM VendorUrl */
            if (!vsysThis.strVendorUrl.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_VendorUrl,
                                    "",
                                    vsysThis.strVendorUrl,
                                    vsysThis.strVendorUrl);

            /* VM description */
            if (!vsysThis.strDescription.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_Description,
                                    "",
                                    vsysThis.strDescription,
                                    vsysThis.strDescription);

            /* VM license */
            if (!vsysThis.strLicenseText.isEmpty())
                pNewDesc->addEntry(VirtualSystemDescriptionType_License,
                                    "",
                                    vsysThis.strLicenseText,
                                    vsysThis.strLicenseText);

            /* Now that we know the OS type, get our internal defaults based on that. */
            ComPtr<IGuestOSType> pGuestOSType;
            rc = mVirtualBox->GetGuestOSType(Bstr(strOsTypeVBox), pGuestOSType.asOutParam());
            if (FAILED(rc)) DebugBreakThrow(rc);

            /* CPU count */
            ULONG cpuCountVBox = vsysThis.cCPUs;
            /* Check for the constrains */
            if (cpuCountVBox > SchemaDefs::MaxCPUCount)
            {
                addWarning(tr("The virtual system \"%s\" claims support for %u CPU's, but VirtualBox has support for max %u CPU's only."),
                           vsysThis.strName.c_str(), cpuCountVBox, SchemaDefs::MaxCPUCount);
                cpuCountVBox = SchemaDefs::MaxCPUCount;
            }
            if (vsysThis.cCPUs == 0)
                cpuCountVBox = 1;
            pNewDesc->addEntry(VirtualSystemDescriptionType_CPU,
                               "",
                               Utf8StrFmt("%RI32", (uint32_t)vsysThis.cCPUs),
                               Utf8StrFmt("%RI32", (uint32_t)cpuCountVBox));

            /* RAM */
            uint64_t ullMemSizeVBox = vsysThis.ullMemorySize / _1M;
            /* Check for the constrains */
            if (    ullMemSizeVBox != 0
                 && (    ullMemSizeVBox < MM_RAM_MIN_IN_MB
                      || ullMemSizeVBox > MM_RAM_MAX_IN_MB
                    )
               )
            {
                addWarning(tr("The virtual system \"%s\" claims support for %llu MB RAM size, but VirtualBox has support for min %u & max %u MB RAM size only."),
                              vsysThis.strName.c_str(), ullMemSizeVBox, MM_RAM_MIN_IN_MB, MM_RAM_MAX_IN_MB);
                ullMemSizeVBox = RT_MIN(RT_MAX(ullMemSizeVBox, MM_RAM_MIN_IN_MB), MM_RAM_MAX_IN_MB);
            }
            if (vsysThis.ullMemorySize == 0)
            {
                /* If the RAM of the OVF is zero, use our predefined values */
                ULONG memSizeVBox2;
                rc = pGuestOSType->COMGETTER(RecommendedRAM)(&memSizeVBox2);
                if (FAILED(rc)) DebugBreakThrow(rc);
                /* VBox stores that in MByte */
                ullMemSizeVBox = (uint64_t)memSizeVBox2;
            }
            pNewDesc->addEntry(VirtualSystemDescriptionType_Memory,
                               "",
                               Utf8StrFmt("%RI64", (uint64_t)vsysThis.ullMemorySize),
                               Utf8StrFmt("%RI64", (uint64_t)ullMemSizeVBox));

            /* Audio */
            if (!vsysThis.strSoundCardType.isEmpty())
                /* Currently we set the AC97 always.
                   @todo: figure out the hardware which could be possible */
                pNewDesc->addEntry(VirtualSystemDescriptionType_SoundCard,
                                   "",
                                   vsysThis.strSoundCardType,
                                   Utf8StrFmt("%RI32", (uint32_t)AudioControllerType_AC97));

#ifdef VBOX_WITH_USB
            /* USB Controller */
            if (vsysThis.fHasUsbController)
                pNewDesc->addEntry(VirtualSystemDescriptionType_USBController, "", "", "");
#endif /* VBOX_WITH_USB */

            /* Network Controller */
            size_t cEthernetAdapters = vsysThis.llEthernetAdapters.size();
            if (cEthernetAdapters > 0)
            {
                /* Check for the constrains */
                if (cEthernetAdapters > SchemaDefs::NetworkAdapterCount)
                    addWarning(tr("The virtual system \"%s\" claims support for %zu network adapters, but VirtualBox has support for max %u network adapter only."),
                                  vsysThis.strName.c_str(), cEthernetAdapters, SchemaDefs::NetworkAdapterCount);

                /* Get the default network adapter type for the selected guest OS */
                NetworkAdapterType_T defaultAdapterVBox = NetworkAdapterType_Am79C970A;
                rc = pGuestOSType->COMGETTER(AdapterType)(&defaultAdapterVBox);
                if (FAILED(rc)) DebugBreakThrow(rc);

                ovf::EthernetAdaptersList::const_iterator itEA;
                /* Iterate through all abstract networks. We support 8 network
                 * adapters at the maximum, so the first 8 will be added only. */
                size_t a = 0;
                for (itEA = vsysThis.llEthernetAdapters.begin();
                     itEA != vsysThis.llEthernetAdapters.end() && a < SchemaDefs::NetworkAdapterCount;
                     ++itEA, ++a)
                {
                    const ovf::EthernetAdapter &ea = *itEA; // logical network to connect to
                    Utf8Str strNetwork = ea.strNetworkName;
                    // make sure it's one of these two
                    if (    (strNetwork.compare("Null", Utf8Str::CaseInsensitive))
                         && (strNetwork.compare("NAT", Utf8Str::CaseInsensitive))
                         && (strNetwork.compare("Bridged", Utf8Str::CaseInsensitive))
                         && (strNetwork.compare("Internal", Utf8Str::CaseInsensitive))
                         && (strNetwork.compare("HostOnly", Utf8Str::CaseInsensitive))
                       )
                        strNetwork = "Bridged";     // VMware assumes this is the default apparently

                    /* Figure out the hardware type */
                    NetworkAdapterType_T nwAdapterVBox = defaultAdapterVBox;
                    if (!ea.strAdapterType.compare("PCNet32", Utf8Str::CaseInsensitive))
                    {
                        /* If the default adapter is already one of the two
                         * PCNet adapters use the default one. If not use the
                         * Am79C970A as fallback. */
                        if (!(defaultAdapterVBox == NetworkAdapterType_Am79C970A ||
                              defaultAdapterVBox == NetworkAdapterType_Am79C973))
                            nwAdapterVBox = NetworkAdapterType_Am79C970A;
                    }
#ifdef VBOX_WITH_E1000
                    /* VMWare accidentally write this with VirtualCenter 3.5,
                       so make sure in this case always to use the VMWare one */
                    else if (!ea.strAdapterType.compare("E10000", Utf8Str::CaseInsensitive))
                        nwAdapterVBox = NetworkAdapterType_I82545EM;
                    else if (!ea.strAdapterType.compare("E1000", Utf8Str::CaseInsensitive))
                    {
                        /* Check if this OVF was written by VirtualBox */
                        if (Utf8Str(vsysThis.strVirtualSystemType).contains("virtualbox", Utf8Str::CaseInsensitive))
                        {
                            /* If the default adapter is already one of the three
                             * E1000 adapters use the default one. If not use the
                             * I82545EM as fallback. */
                            if (!(defaultAdapterVBox == NetworkAdapterType_I82540EM ||
                                  defaultAdapterVBox == NetworkAdapterType_I82543GC ||
                                  defaultAdapterVBox == NetworkAdapterType_I82545EM))
                            nwAdapterVBox = NetworkAdapterType_I82540EM;
                        }
                        else
                            /* Always use this one since it's what VMware uses */
                            nwAdapterVBox = NetworkAdapterType_I82545EM;
                    }
#endif /* VBOX_WITH_E1000 */

                    pNewDesc->addEntry(VirtualSystemDescriptionType_NetworkAdapter,
                                       "",      // ref
                                       ea.strNetworkName,      // orig
                                       Utf8StrFmt("%RI32", (uint32_t)nwAdapterVBox),   // conf
                                       0,
                                       Utf8StrFmt("type=%s", strNetwork.c_str()));       // extra conf
                }
            }

            /* Floppy Drive */
            if (vsysThis.fHasFloppyDrive)
                pNewDesc->addEntry(VirtualSystemDescriptionType_Floppy, "", "", "");

            /* CD Drive */
            if (vsysThis.fHasCdromDrive)
                pNewDesc->addEntry(VirtualSystemDescriptionType_CDROM, "", "", "");

            /* Hard disk Controller */
            uint16_t cIDEused = 0;
            uint16_t cSATAused = 0; NOREF(cSATAused);
            uint16_t cSCSIused = 0; NOREF(cSCSIused);
            ovf::ControllersMap::const_iterator hdcIt;
            /* Iterate through all hard disk controllers */
            for (hdcIt = vsysThis.mapControllers.begin();
                 hdcIt != vsysThis.mapControllers.end();
                 ++hdcIt)
            {
                const ovf::HardDiskController &hdc = hdcIt->second;
                Utf8Str strControllerID = Utf8StrFmt("%RI32", (uint32_t)hdc.idController);

                switch (hdc.system)
                {
                    case ovf::HardDiskController::IDE:
                        /* Check for the constrains */
                        if (cIDEused < 4)
                        {
                            // @todo: figure out the IDE types
                            /* Use PIIX4 as default */
                            Utf8Str strType = "PIIX4";
                            if (!hdc.strControllerType.compare("PIIX3", Utf8Str::CaseInsensitive))
                                strType = "PIIX3";
                            else if (!hdc.strControllerType.compare("ICH6", Utf8Str::CaseInsensitive))
                                strType = "ICH6";
                            pNewDesc->addEntry(VirtualSystemDescriptionType_HardDiskControllerIDE,
                                               strControllerID,         // strRef
                                               hdc.strControllerType,   // aOvfValue
                                               strType);                // aVboxValue
                        }
                        else
                            /* Warn only once */
                            if (cIDEused == 2)
                                addWarning(tr("The virtual \"%s\" system requests support for more than two IDE controller channels, but VirtualBox supports only two."),
                                            vsysThis.strName.c_str());

                        ++cIDEused;
                    break;

                    case ovf::HardDiskController::SATA:
                        /* Check for the constrains */
                        if (cSATAused < 1)
                        {
                            // @todo: figure out the SATA types
                            /* We only support a plain AHCI controller, so use them always */
                            pNewDesc->addEntry(VirtualSystemDescriptionType_HardDiskControllerSATA,
                                                strControllerID,
                                                hdc.strControllerType,
                                                "AHCI");
                        }
                        else
                        {
                            /* Warn only once */
                            if (cSATAused == 1)
                                addWarning(tr("The virtual system \"%s\" requests support for more than one SATA controller, but VirtualBox has support for only one"),
                                            vsysThis.strName.c_str());

                        }
                        ++cSATAused;
                    break;

                    case ovf::HardDiskController::SCSI:
                        /* Check for the constrains */
                        if (cSCSIused < 1)
                        {
                            VirtualSystemDescriptionType_T vsdet = VirtualSystemDescriptionType_HardDiskControllerSCSI;
                            Utf8Str hdcController = "LsiLogic";
                            if (!hdc.strControllerType.compare("lsilogicsas", Utf8Str::CaseInsensitive))
                            {
                                // OVF considers SAS a variant of SCSI but VirtualBox considers it a class of its own
                                vsdet = VirtualSystemDescriptionType_HardDiskControllerSAS;
                                hdcController = "LsiLogicSas";
                            }
                            else if (!hdc.strControllerType.compare("BusLogic", Utf8Str::CaseInsensitive))
                                hdcController = "BusLogic";
                            pNewDesc->addEntry(vsdet,
                                               strControllerID,
                                               hdc.strControllerType,
                                               hdcController);
                        }
                        else
                            addWarning(tr("The virtual system \"%s\" requests support for an additional SCSI controller of type \"%s\" with ID %s, but VirtualBox presently supports only one SCSI controller."),
                                        vsysThis.strName.c_str(),
                                        hdc.strControllerType.c_str(),
                                        strControllerID.c_str());
                        ++cSCSIused;
                    break;
                }
            }

            /* Hard disks */
            if (vsysThis.mapVirtualDisks.size() > 0)
            {
                ovf::VirtualDisksMap::const_iterator itVD;
                /* Iterate through all hard disks ()*/
                for (itVD = vsysThis.mapVirtualDisks.begin();
                     itVD != vsysThis.mapVirtualDisks.end();
                     ++itVD)
                {
                    const ovf::VirtualDisk &hd = itVD->second;
                    /* Get the associated disk image */
                    const ovf::DiskImage &di = m->pReader->m_mapDisks[hd.strDiskId];

                    // @todo:
                    //  - figure out all possible vmdk formats we also support
                    //  - figure out if there is a url specifier for vhd already
                    //  - we need a url specifier for the vdi format
                    if (   di.strFormat.compare("http://www.vmware.com/specifications/vmdk.html#sparse", Utf8Str::CaseInsensitive)
                        || di.strFormat.compare("http://www.vmware.com/interfaces/specifications/vmdk.html#streamOptimized", Utf8Str::CaseInsensitive)
                        || di.strFormat.compare("http://www.vmware.com/specifications/vmdk.html#compressed", Utf8Str::CaseInsensitive)
                        || di.strFormat.compare("http://www.vmware.com/interfaces/specifications/vmdk.html#compressed", Utf8Str::CaseInsensitive)
                       )
                    {
                        /* If the href is empty use the VM name as filename */
                        Utf8Str strFilename = di.strHref;
                        if (!strFilename.length())
                            strFilename = Utf8StrFmt("%s.vmdk", nameVBox.c_str());
                        /* Construct a unique target path */
                        Utf8StrFmt strPath("%s%c%s",
                                           strDefaultHardDiskFolder.c_str(),
                                           RTPATH_DELIMITER,
                                           strFilename.c_str());
                        searchUniqueDiskImageFilePath(strPath);

                        /* find the description for the hard disk controller
                         * that has the same ID as hd.idController */
                        const VirtualSystemDescriptionEntry *pController;
                        if (!(pController = pNewDesc->findControllerFromID(hd.idController)))
                            DebugBreakThrow(setError(E_FAIL,
                                           tr("Cannot find hard disk controller with OVF instance ID %RI32 to which disk \"%s\" should be attached"),
                                           hd.idController,
                                           di.strHref.c_str()));

                        /* controller to attach to, and the bus within that controller */
                        Utf8StrFmt strExtraConfig("controller=%RI16;channel=%RI16",
                                                  pController->ulIndex,
                                                  hd.ulAddressOnParent);
                        pNewDesc->addEntry(VirtualSystemDescriptionType_HardDiskImage,
                                           hd.strDiskId,
                                           di.strHref,
                                           strPath,
                                           di.ulSuggestedSizeMB,
                                           strExtraConfig);
                    }
                    else
                        DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                       tr("Unsupported format for virtual disk image in OVF: \"%s\"", di.strFormat.c_str())));
                }
            }

            m->virtualSystemDescriptions.push_back(pNewDesc);
        }
    }
    catch (HRESULT aRC)
    {
        /* On error we clear the list & return */
        m->virtualSystemDescriptions.clear();
        rc = aRC;
    }

    // reset the appliance state
    alock.acquire();
    m->state = Data::ApplianceIdle;

    return rc;
}

/**
 * Public method implementation. This creates one or more new machines according to the
 * VirtualSystemScription instances created by Appliance::Interpret().
 * Thread implementation is in Appliance::importImpl().
 * @param aProgress
 * @return
 */
STDMETHODIMP Appliance::ImportMachines(IProgress **aProgress)
{
    CheckComArgOutPointerValid(aProgress);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    // do not allow entering this method if the appliance is busy reading or writing
    if (!isApplianceIdle())
        return E_ACCESSDENIED;

    if (!m->pReader)
        return setError(E_FAIL,
                        tr("Cannot import machines without reading it first (call read() before importMachines())"));

    ComObjPtr<Progress> progress;
    HRESULT rc = S_OK;
    try
    {
        rc = importImpl(m->locInfo, progress);
    }
    catch (HRESULT aRC)
    {
        rc = aRC;
    }

    if (SUCCEEDED(rc))
        /* Return progress to the caller */
        progress.queryInterfaceTo(aProgress);

    return rc;
}

////////////////////////////////////////////////////////////////////////////////
//
// Appliance private methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Implementation for reading an OVF. This starts a new thread which will call
 * Appliance::taskThreadImportOrExport() which will then call readFS() or readS3().
 * This will then open the OVF with ovfreader.cpp.
 *
 * This is in a separate private method because it is used from three locations:
 *
 * 1) from the public Appliance::Read().
 *
 * 2) in a second worker thread; in that case, Appliance::ImportMachines() called Appliance::importImpl(), which
 *    called Appliance::readFSOVA(), which called Appliance::importImpl(), which then called this again.
 *
 * 3) from Appliance::readS3(), which got called from a previous instance of Appliance::taskThreadImportOrExport().
 *
 * @param aLocInfo
 * @param aProgress
 * @return
 */
HRESULT Appliance::readImpl(const LocationInfo &aLocInfo, ComObjPtr<Progress> &aProgress)
{
    BstrFmt bstrDesc = BstrFmt(tr("Reading appliance '%s'"),
                               aLocInfo.strPath.c_str());
    HRESULT rc;
    /* Create the progress object */
    aProgress.createObject();
    if (aLocInfo.storageType == VFSType_File)
        /* 1 operation only */
        rc = aProgress->init(mVirtualBox, static_cast<IAppliance*>(this),
                             bstrDesc,
                             TRUE /* aCancelable */);
    else
        /* 4/5 is downloading, 1/5 is reading */
        rc = aProgress->init(mVirtualBox, static_cast<IAppliance*>(this),
                             bstrDesc,
                             TRUE /* aCancelable */,
                             2, // ULONG cOperations,
                             5, // ULONG ulTotalOperationsWeight,
                             BstrFmt(tr("Download appliance '%s'"),
                                     aLocInfo.strPath.c_str()), // CBSTR bstrFirstOperationDescription,
                             4); // ULONG ulFirstOperationWeight,
    if (FAILED(rc)) DebugBreakThrow(rc);

    /* Initialize our worker task */
    std::auto_ptr<TaskOVF> task(new TaskOVF(this, TaskOVF::Read, aLocInfo, aProgress));

    rc = task->startThread();
    if (FAILED(rc)) DebugBreakThrow(rc);

    /* Don't destruct on success */
    task.release();

    return rc;
}

/**
 * Actual worker code for reading an OVF from disk. This is called from Appliance::taskThreadImportOrExport()
 * and therefore runs on the OVF read worker thread. This opens the OVF with ovfreader.cpp.
 *
 * This runs in two contexts:
 *
 * 1) in a first worker thread; in that case, Appliance::Read() called Appliance::readImpl();
 *
 * 2) in a second worker thread; in that case, Appliance::Read() called Appliance::readImpl(), which
 *    called Appliance::readS3(), which called Appliance::readImpl(), which then called this.
 *
 * @param pTask
 * @return
 */
HRESULT Appliance::readFS(const LocationInfo &locInfo, ComObjPtr<Progress> &pProgress)
{
    if (locInfo.strPath.endsWith(".ovf", Utf8Str::CaseInsensitive))
        return readFSOVF(locInfo, pProgress);
    else
        return readFSOVA(locInfo, pProgress);
}

HRESULT Appliance::readFSOVF(const LocationInfo &locInfo, ComObjPtr<Progress> & /* pProgress */)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT rc = S_OK;

    try
    {
        /* Read & parse the XML structure of the OVF file */
        m->pReader = new ovf::OVFReader(locInfo.strPath);
        /* Create the SHA1 sum of the OVF file for later validation */
        char *pszDigest;
        int vrc = RTSha1Digest(locInfo.strPath.c_str(), &pszDigest, NULL, NULL);
        if (RT_FAILURE(vrc))
            DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                           tr("Couldn't calculate SHA1 digest for file '%s' (%Rrc)"),
                           RTPathFilename(locInfo.strPath.c_str()), vrc));
        m->strOVFSHA1Digest = pszDigest;
        RTStrFree(pszDigest);
    }
    catch (iprt::Error &x)      // includes all XML exceptions
    {
        rc = setError(VBOX_E_FILE_ERROR,
                      x.what());
    }
    catch (HRESULT aRC)
    {
        rc = aRC;
    }

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return rc;
}

HRESULT Appliance::readFSOVA(const LocationInfo &locInfo, ComObjPtr<Progress> &pProgress)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT rc = S_OK;
    int vrc = VINF_SUCCESS;
    char szOSTmpDir[RTPATH_MAX];
    RTPathTemp(szOSTmpDir, sizeof(szOSTmpDir));
    /* The template for the temporary directory created below */
    char *pszTmpDir;
    RTStrAPrintf(&pszTmpDir, "%s"RTPATH_SLASH_STR"vbox-ovf-XXXXXX", szOSTmpDir);
    list< pair<Utf8Str, ULONG> > filesList;
    Utf8Str strTmpOvf;

    try
    {
        /* Extract the path */
        Utf8Str tmpPath = locInfo.strPath;
        /* Remove the ova extension */
        tmpPath.stripExt();
        tmpPath += ".ovf";

        /* We need a temporary directory which we can put the OVF file & all
         * disk images in */
        vrc = RTDirCreateTemp(pszTmpDir);
        if (RT_FAILURE(vrc))
            DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                           tr("Cannot create temporary directory '%s' (%Rrc)"), pszTmpDir, vrc));

        /* The temporary name of the target OVF file */
        strTmpOvf = Utf8StrFmt("%s/%s", pszTmpDir, RTPathFilename(tmpPath.c_str()));

        /* Next we have to download the OVF */
        char *papszFile = RTPathFilename(strTmpOvf.c_str());
        vrc = RTTarExtractFiles(locInfo.strPath.c_str(), pszTmpDir, &papszFile, 1, 0, 0);
        if (RT_FAILURE(vrc))
        {
            if (vrc == VERR_FILE_NOT_FOUND)
                DebugBreakThrow(setError(VBOX_E_IPRT_ERROR,
                                         tr("Can't find ovf file '%s' in archive '%s' (%Rrc)"), papszFile, locInfo.strPath.c_str(), vrc));
            else
                DebugBreakThrow(setError(VBOX_E_IPRT_ERROR,
                                         tr("Can't unpack the archive file '%s' (%Rrc)"), locInfo.strPath.c_str(), vrc));
        }

        // todo: check this out
//        pTask->pProgress->SetNextOperation(Bstr(tr("Reading")), 1);

        /* Prepare the temporary reading of the OVF */
        ComObjPtr<Progress> progress;
        LocationInfo li;
        li.strPath = strTmpOvf;
        /* Start the reading from the fs */
        rc = readImpl(li, progress);
        if (FAILED(rc)) DebugBreakThrow(rc);

        /* Unlock the appliance for the reading thread */
        appLock.release();
        /* Wait until the reading is done, but report the progress back to the
           caller */
        ComPtr<IProgress> progressInt(progress);
        waitForAsyncProgress(pProgress, progressInt); /* Any errors will be thrown */

        /* Again lock the appliance for the next steps */
        appLock.acquire();
    }
    catch(HRESULT aRC)
    {
        rc = aRC;
    }
    /* Delete all files which where temporary created */
    if (RTPathExists(strTmpOvf.c_str()))
    {
        vrc = RTFileDelete(strTmpOvf.c_str());
        if (RT_FAILURE(vrc))
            rc = setError(VBOX_E_FILE_ERROR,
                          tr("Cannot delete file '%s' (%Rrc)"), strTmpOvf.c_str(), vrc);
    }
    /* Delete the temporary directory */
    if (RTPathExists(pszTmpDir))
    {
        vrc = RTDirRemove(pszTmpDir);
        if (RT_FAILURE(vrc))
            rc = setError(VBOX_E_FILE_ERROR,
                          tr("Cannot delete temporary directory '%s' (%Rrc)"), pszTmpDir, vrc);
    }
    if (pszTmpDir)
        RTStrFree(pszTmpDir);

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return rc;
}

/**
 * Worker code for reading OVF from the cloud. This is called from Appliance::taskThreadImportOrExport()
 * in S3 mode and therefore runs on the OVF read worker thread. This then starts a second worker
 * thread to create temporary files (see Appliance::readFS()).
 *
 * @param pTask
 * @return
 */
HRESULT Appliance::readS3(TaskOVF *pTask)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT rc = S_OK;
    int vrc = VINF_SUCCESS;
    RTS3 hS3 = NIL_RTS3;
    char szOSTmpDir[RTPATH_MAX];
    RTPathTemp(szOSTmpDir, sizeof(szOSTmpDir));
    /* The template for the temporary directory created below */
    char *pszTmpDir;
    RTStrAPrintf(&pszTmpDir, "%s"RTPATH_SLASH_STR"vbox-ovf-XXXXXX", szOSTmpDir);
    list< pair<Utf8Str, ULONG> > filesList;
    Utf8Str strTmpOvf;

    try
    {
        /* Extract the bucket */
        Utf8Str tmpPath = pTask->locInfo.strPath;
        Utf8Str bucket;
        parseBucket(tmpPath, bucket);

        /* We need a temporary directory which we can put the OVF file & all
         * disk images in */
        vrc = RTDirCreateTemp(pszTmpDir);
        if (RT_FAILURE(vrc))
            DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                           tr("Cannot create temporary directory '%s'"), pszTmpDir));

        /* The temporary name of the target OVF file */
        strTmpOvf = Utf8StrFmt("%s/%s", pszTmpDir, RTPathFilename(tmpPath.c_str()));

        /* Next we have to download the OVF */
        vrc = RTS3Create(&hS3, pTask->locInfo.strUsername.c_str(), pTask->locInfo.strPassword.c_str(), pTask->locInfo.strHostname.c_str(), "virtualbox-agent/"VBOX_VERSION_STRING);
        if (RT_FAILURE(vrc))
            DebugBreakThrow(setError(VBOX_E_IPRT_ERROR,
                           tr("Cannot create S3 service handler")));
        RTS3SetProgressCallback(hS3, pTask->updateProgress, &pTask);

        /* Get it */
        char *pszFilename = RTPathFilename(strTmpOvf.c_str());
        vrc = RTS3GetKey(hS3, bucket.c_str(), pszFilename, strTmpOvf.c_str());
        if (RT_FAILURE(vrc))
        {
            if (vrc == VERR_S3_CANCELED)
                throw S_OK; /* todo: !!!!!!!!!!!!! */
            else if (vrc == VERR_S3_ACCESS_DENIED)
                DebugBreakThrow(setError(E_ACCESSDENIED,
                               tr("Cannot download file '%s' from S3 storage server (Access denied). Make sure that your credentials are right."
                                  "Also check that your host clock is properly synced"),
                               pszFilename));
            else if (vrc == VERR_S3_NOT_FOUND)
                DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                               tr("Cannot download file '%s' from S3 storage server (File not found)"), pszFilename));
            else
                DebugBreakThrow(setError(VBOX_E_IPRT_ERROR,
                               tr("Cannot download file '%s' from S3 storage server (%Rrc)"), pszFilename, vrc));
        }

        /* Close the connection early */
        RTS3Destroy(hS3);
        hS3 = NIL_RTS3;

        pTask->pProgress->SetNextOperation(Bstr(tr("Reading")), 1);

        /* Prepare the temporary reading of the OVF */
        ComObjPtr<Progress> progress;
        LocationInfo li;
        li.strPath = strTmpOvf;
        /* Start the reading from the fs */
        rc = readImpl(li, progress);
        if (FAILED(rc)) DebugBreakThrow(rc);

        /* Unlock the appliance for the reading thread */
        appLock.release();
        /* Wait until the reading is done, but report the progress back to the
           caller */
        ComPtr<IProgress> progressInt(progress);
        waitForAsyncProgress(pTask->pProgress, progressInt); /* Any errors will be thrown */

        /* Again lock the appliance for the next steps */
        appLock.acquire();
    }
    catch(HRESULT aRC)
    {
        rc = aRC;
    }
    /* Cleanup */
    RTS3Destroy(hS3);
    /* Delete all files which where temporary created */
    if (RTPathExists(strTmpOvf.c_str()))
    {
        vrc = RTFileDelete(strTmpOvf.c_str());
        if (RT_FAILURE(vrc))
            rc = setError(VBOX_E_FILE_ERROR,
                          tr("Cannot delete file '%s' (%Rrc)"), strTmpOvf.c_str(), vrc);
    }
    /* Delete the temporary directory */
    if (RTPathExists(pszTmpDir))
    {
        vrc = RTDirRemove(pszTmpDir);
        if (RT_FAILURE(vrc))
            rc = setError(VBOX_E_FILE_ERROR,
                          tr("Cannot delete temporary directory '%s' (%Rrc)"), pszTmpDir, vrc);
    }
    if (pszTmpDir)
        RTStrFree(pszTmpDir);

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return rc;
}

/**
 * Helper that converts VirtualSystem attachment values into VirtualBox attachment values.
 * Throws HRESULT values on errors!
 *
 * @param hdc in: the HardDiskController structure to attach to.
 * @param ulAddressOnParent in: the AddressOnParent parameter from OVF.
 * @param controllerType out: the name of the hard disk controller to attach to (e.g. "IDE Controller").
 * @param lControllerPort out: the channel (controller port) of the controller to attach to.
 * @param lDevice out: the device number to attach to.
 */
void Appliance::convertDiskAttachmentValues(const ovf::HardDiskController &hdc,
                                            uint32_t ulAddressOnParent,
                                            Bstr &controllerType,
                                            int32_t &lControllerPort,
                                            int32_t &lDevice)
{
    Log(("Appliance::convertDiskAttachmentValues: hdc.system=%d, hdc.fPrimary=%d, ulAddressOnParent=%d\n", hdc.system, hdc.fPrimary, ulAddressOnParent));

    switch (hdc.system)
    {
        case ovf::HardDiskController::IDE:
            // For the IDE bus, the port parameter can be either 0 or 1, to specify the primary
            // or secondary IDE controller, respectively. For the primary controller of the IDE bus,
            // the device number can be either 0 or 1, to specify the master or the slave device,
            // respectively. For the secondary IDE controller, the device number is always 1 because
            // the master device is reserved for the CD-ROM drive.
            controllerType = Bstr("IDE Controller");
            switch (ulAddressOnParent)
            {
                case 0: // master
                    if (!hdc.fPrimary)
                    {
                        // secondary master
                        lControllerPort = (long)1;
                        lDevice = (long)0;
                    }
                    else // primary master
                    {
                        lControllerPort = (long)0;
                        lDevice = (long)0;
                    }
                break;

                case 1: // slave
                    if (!hdc.fPrimary)
                    {
                        // secondary slave
                        lControllerPort = (long)1;
                        lDevice = (long)1;
                    }
                    else // primary slave
                    {
                        lControllerPort = (long)0;
                        lDevice = (long)1;
                    }
                break;

                // used by older VBox exports
                case 2:     // interpret this as secondary master
                    lControllerPort = (long)1;
                    lDevice = (long)0;
                break;

                // used by older VBox exports
                case 3:     // interpret this as secondary slave
                    lControllerPort = (long)1;
                    lDevice = (long)1;
                break;

                default:
                    DebugBreakThrow(setError(VBOX_E_NOT_SUPPORTED,
                                             tr("Invalid channel %RI16 specified; IDE controllers support only 0, 1 or 2"),
                                             ulAddressOnParent));
                break;
            }
        break;

        case ovf::HardDiskController::SATA:
            controllerType = Bstr("SATA Controller");
            lControllerPort = (long)ulAddressOnParent;
            lDevice = (long)0;
        break;

        case ovf::HardDiskController::SCSI:
            controllerType = Bstr("SCSI Controller");
            lControllerPort = (long)ulAddressOnParent;
            lDevice = (long)0;
        break;

        default: break;
    }

    Log(("=> lControllerPort=%d, lDevice=%d\n", lControllerPort, lDevice));
}

/**
 * Implementation for importing OVF data into VirtualBox. This starts a new thread which will call
 * Appliance::taskThreadImportOrExport().
 *
 * This creates one or more new machines according to the VirtualSystemScription instances created by
 * Appliance::Interpret().
 *
 * This is in a separate private method because it is used from two locations:
 *
 * 1) from the public Appliance::ImportMachines().
 * 2) from Appliance::importS3(), which got called from a previous instance of Appliance::taskThreadImportOrExport().
 *
 * @param aLocInfo
 * @param aProgress
 * @return
 */
HRESULT Appliance::importImpl(const LocationInfo &locInfo,
                              ComObjPtr<Progress> &progress)
{
    HRESULT rc = S_OK;

    SetUpProgressMode mode;
    if (locInfo.storageType == VFSType_File)
    {
        mode = ImportFileNoManifest;
        Utf8Str strMfFile = manifestFileName(locInfo.strPath);
        if (!locInfo.strPath.endsWith(".ova", Utf8Str::CaseInsensitive))
        {
            if (RTPathExists(strMfFile.c_str()))
                mode = ImportFileWithManifest;
        }
        else
        {
            if (RTTarQueryFileExists(locInfo.strPath.c_str(), RTPathFilename(strMfFile.c_str())) == VINF_SUCCESS)
                mode = ImportFileWithManifest;
        }
    }
    else
         mode = ImportS3;

    rc = setUpProgress(locInfo,
                       progress,
                       BstrFmt(tr("Importing appliance '%s'"), locInfo.strPath.c_str()),
                       mode);
    if (FAILED(rc)) DebugBreakThrow(rc);

    /* Initialize our worker task */
    std::auto_ptr<TaskOVF> task(new TaskOVF(this, TaskOVF::Import, locInfo, progress));

    rc = task->startThread();
    if (FAILED(rc)) DebugBreakThrow(rc);

    /* Don't destruct on success */
    task.release();

    return rc;
}

/**
 * Checks if a manifest file exists in the given location and, if so, verifies
 * that the relevant files (the OVF XML and the disks referenced by it, as
 * represented by the VirtualSystemDescription instances contained in this appliance)
 * match it. Requires a previous read() and interpret().
 *
 * @param locInfo
 * @param reader
 * @return
 */
HRESULT Appliance::manifestVerify(const LocationInfo &locInfo,
                                  const ovf::OVFReader &reader,
                                  ComObjPtr<Progress> &pProgress)
{
    HRESULT rc = S_OK;

    Utf8Str strManifestFile = manifestFileName(locInfo.strPath);
    if (!strManifestFile.isEmpty())
    {
        const char *pcszManifestFileOnly = RTPathFilename(strManifestFile.c_str());
        pProgress->SetNextOperation(BstrFmt(tr("Verifying manifest file '%s'"), pcszManifestFileOnly),
                                    m->ulWeightForManifestOperation);     // operation's weight, as set up with the IProgress originally

        list<Utf8Str> filesList;
        Utf8Str strSrcDir(locInfo.strPath);
        strSrcDir.stripFilename();
        // add every disks of every virtual system to an internal list
        list< ComObjPtr<VirtualSystemDescription> >::const_iterator it;
        for (it = m->virtualSystemDescriptions.begin();
             it != m->virtualSystemDescriptions.end();
             ++it)
        {
            ComObjPtr<VirtualSystemDescription> vsdescThis = (*it);
            std::list<VirtualSystemDescriptionEntry*> avsdeHDs = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskImage);
            std::list<VirtualSystemDescriptionEntry*>::const_iterator itH;
            for (itH = avsdeHDs.begin();
                 itH != avsdeHDs.end();
                 ++itH)
            {
                VirtualSystemDescriptionEntry *vsdeHD = *itH;
                // find the disk from the OVF's disk list
                ovf::DiskImagesMap::const_iterator itDiskImage = reader.m_mapDisks.find(vsdeHD->strRef);
                const ovf::DiskImage &di = itDiskImage->second;
                Utf8StrFmt strSrcFilePath("%s%c%s", strSrcDir.c_str(), RTPATH_DELIMITER, di.strHref.c_str());
                filesList.push_back(strSrcFilePath);
            }
        }

        // create the test list
        PRTMANIFESTTEST pTestList = (PRTMANIFESTTEST)RTMemAllocZ(sizeof(RTMANIFESTTEST) * (filesList.size() + 1));
        pTestList[0].pszTestFile = (char*)locInfo.strPath.c_str();
        pTestList[0].pszTestDigest = (char*)m->strOVFSHA1Digest.c_str();
        int vrc = VINF_SUCCESS;
        size_t i = 1;
        list<Utf8Str>::const_iterator it1;
        for (it1 = filesList.begin();
             it1 != filesList.end();
             ++it1, ++i)
        {
            char* pszDigest;
            vrc = RTSha1Digest((*it1).c_str(), &pszDigest, NULL, NULL);
            pTestList[i].pszTestFile = (char*)(*it1).c_str();
            pTestList[i].pszTestDigest = pszDigest;
        }

        // this call can take a very long time
        size_t cIndexOnError;
        vrc = RTManifestVerify(strManifestFile.c_str(),
                               pTestList,
                               filesList.size() + 1,
                               &cIndexOnError);

        if (vrc == VERR_MANIFEST_DIGEST_MISMATCH)
            rc = setError(VBOX_E_FILE_ERROR,
                          tr("The SHA1 digest of '%s' does not match the one in '%s'"),
                          RTPathFilename(pTestList[cIndexOnError].pszTestFile),
                          pcszManifestFileOnly);
        else if (RT_FAILURE(vrc))
            rc = setError(VBOX_E_FILE_ERROR,
                          tr("Could not verify the content of '%s' against the available files (%Rrc)"),
                          pcszManifestFileOnly,
                          vrc);

        // clean up
        for (size_t j = 1;
             j < filesList.size();
             ++j)
            RTStrFree(pTestList[j].pszTestDigest);
        RTMemFree(pTestList);
    }

    return rc;
}

/**
 * Actual worker code for importing OVF data into VirtualBox. This is called from Appliance::taskThreadImportOrExport()
 * and therefore runs on the OVF import worker thread. This creates one or more new machines according to the
 * VirtualSystemScription instances created by Appliance::Interpret().
 *
 * This runs in three contexts:
 *
 * 1) in a first worker thread; in that case, Appliance::ImportMachines() called Appliance::importImpl();
 *
 * 2) in a second worker thread; in that case, Appliance::ImportMachines() called Appliance::importImpl(), which
 *    called Appliance::importFSOVA(), which called Appliance::importImpl(), which then called this again.
 *
 * 3) in a second worker thread; in that case, Appliance::ImportMachines() called Appliance::importImpl(), which
 *    called Appliance::importS3(), which called Appliance::importImpl(), which then called this again.
 *
 * @param pTask
 * @return
 */
HRESULT Appliance::importFS(TaskOVF *pTask)
{
    if (!Utf8Str(RTPathExt(pTask->locInfo.strPath.c_str())).compare(".ovf", Utf8Str::CaseInsensitive))
        return importFSOVF(pTask);
    else
        return importFSOVA(pTask);
}

HRESULT Appliance::importFSOVF(TaskOVF *pTask)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    Assert(!pTask->pProgress.isNull());

    // Change the appliance state so we can safely leave the lock while doing time-consuming
    // disk imports; also the below method calls do all kinds of locking which conflicts with
    // the appliance object lock
    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);
    if (!isApplianceIdle())
        return E_ACCESSDENIED;
    m->state = Data::ApplianceImporting;
    appLock.release();

    HRESULT rc = S_OK;

    const ovf::OVFReader &reader = *m->pReader;
    // this is safe to access because this thread only gets started
    // if pReader != NULL

    // rollback for errors:
    ImportStack stack(pTask->locInfo, reader.m_mapDisks, pTask->pProgress);

    // clear the list of imported machines, if any
    m->llGuidsMachinesCreated.clear();

    try
    {
        // if a manifest file exists, verify the content; we then need all files which are referenced by the OVF & the OVF itself
        rc = manifestVerify(pTask->locInfo, reader, pTask->pProgress);
        if (FAILED(rc)) DebugBreakThrow(rc);

        // create a session for the machine + disks we manipulate below
        rc = stack.pSession.createInprocObject(CLSID_Session);
        if (FAILED(rc)) DebugBreakThrow(rc);

        list<ovf::VirtualSystem>::const_iterator it;
        list< ComObjPtr<VirtualSystemDescription> >::const_iterator it1;
        /* Iterate through all virtual systems of that appliance */
        size_t i = 0;
        for (it = reader.m_llVirtualSystems.begin(),
                it1 = m->virtualSystemDescriptions.begin();
             it != reader.m_llVirtualSystems.end();
             ++it, ++it1, ++i)
        {
            const ovf::VirtualSystem &vsysThis = *it;
            ComObjPtr<VirtualSystemDescription> vsdescThis = (*it1);

            ComPtr<IMachine> pNewMachine;

            // there are two ways in which we can create a vbox machine from OVF:
            // -- either this OVF was written by vbox 3.2 or later, in which case there is a <vbox:Machine> element
            //    in the <VirtualSystem>; then the VirtualSystemDescription::Data has a settings::MachineConfigFile
            //    with all the machine config pretty-parsed;
            // -- or this is an OVF from an older vbox or an external source, and then we need to translate the
            //    VirtualSystemDescriptionEntry and do import work

            // Even for the vbox:Machine case, there are a number of configuration items that will be taken from
            // the OVF because otherwise the "override import parameters" mechanism in the GUI won't work.

            // VM name
            std::list<VirtualSystemDescriptionEntry*> vsdeName = vsdescThis->findByType(VirtualSystemDescriptionType_Name);
            if (vsdeName.size() < 1)
                DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                         tr("Missing VM name")));
            stack.strNameVBox = vsdeName.front()->strVboxCurrent;

            // guest OS type
            std::list<VirtualSystemDescriptionEntry*> vsdeOS;
            vsdeOS = vsdescThis->findByType(VirtualSystemDescriptionType_OS);
            if (vsdeOS.size() < 1)
                DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                         tr("Missing guest OS type")));
            stack.strOsTypeVBox = vsdeOS.front()->strVboxCurrent;

            // CPU count
            std::list<VirtualSystemDescriptionEntry*> vsdeCPU = vsdescThis->findByType(VirtualSystemDescriptionType_CPU);
            if (vsdeCPU.size() != 1)
                DebugBreakThrow(setError(VBOX_E_FILE_ERROR, tr("CPU count missing")));

            const Utf8Str &cpuVBox = vsdeCPU.front()->strVboxCurrent;
            stack.cCPUs = (uint32_t)RTStrToUInt64(cpuVBox.c_str());
            // We need HWVirt & IO-APIC if more than one CPU is requested
            if (stack.cCPUs > 1)
            {
                stack.fForceHWVirt = true;
                stack.fForceIOAPIC = true;
            }

            // RAM
            std::list<VirtualSystemDescriptionEntry*> vsdeRAM = vsdescThis->findByType(VirtualSystemDescriptionType_Memory);
            if (vsdeRAM.size() != 1)
                DebugBreakThrow(setError(VBOX_E_FILE_ERROR, tr("RAM size missing")));
            const Utf8Str &memoryVBox = vsdeRAM.front()->strVboxCurrent;
            stack.ulMemorySizeMB = (uint32_t)RTStrToUInt64(memoryVBox.c_str());

#ifdef VBOX_WITH_USB
            // USB controller
            std::list<VirtualSystemDescriptionEntry*> vsdeUSBController = vsdescThis->findByType(VirtualSystemDescriptionType_USBController);
            // USB support is enabled if there's at least one such entry; to disable USB support,
            // the type of the USB item would have been changed to "ignore"
            stack.fUSBEnabled = vsdeUSBController.size() > 0;
#endif
            // audio adapter
            std::list<VirtualSystemDescriptionEntry*> vsdeAudioAdapter = vsdescThis->findByType(VirtualSystemDescriptionType_SoundCard);
            /* @todo: we support one audio adapter only */
            if (vsdeAudioAdapter.size() > 0)
                stack.strAudioAdapter = vsdeAudioAdapter.front()->strVboxCurrent;

            // for the description of the new machine, always use the OVF entry, the user may have changed it in the import config
            std::list<VirtualSystemDescriptionEntry*> vsdeDescription = vsdescThis->findByType(VirtualSystemDescriptionType_Description);
            if (vsdeDescription.size())
                stack.strDescription = vsdeDescription.front()->strVboxCurrent;

            // import vbox:machine or OVF now
            if (vsdescThis->m->pConfig)
                // vbox:Machine config
                importVBoxMachine(vsdescThis, pNewMachine, stack);
            else
                // generic OVF config
                importMachineGeneric(vsysThis, vsdescThis, pNewMachine, stack);

        } // for (it = pAppliance->m->llVirtualSystems.begin() ...
    }
    catch (HRESULT rc2)
    {
        rc = rc2;
    }

    if (FAILED(rc))
    {
        // with _whatever_ error we've had, do a complete roll-back of
        // machines and disks we've created

        for (list<Guid>::iterator itID = m->llGuidsMachinesCreated.begin();
             itID != m->llGuidsMachinesCreated.end();
             ++itID)
        {
            Guid guid = *itID;
            Bstr bstrGuid = guid.toUtf16();
            ComPtr<IMachine> failedMachine;
            HRESULT rc2 = mVirtualBox->GetMachine(bstrGuid, failedMachine.asOutParam());
            if (SUCCEEDED(rc2))
            {
                SafeIfaceArray<IMedium> aMedia;
                rc2 = failedMachine->Unregister(CleanupMode_DetachAllReturnHardDisksOnly, ComSafeArrayAsOutParam(aMedia));
                ComPtr<IProgress> pProgress2;
                rc2 = failedMachine->Delete(ComSafeArrayAsInParam(aMedia), pProgress2.asOutParam());
                pProgress2->WaitForCompletion(-1);
            }
        }
    }

    // restore the appliance state
    appLock.acquire();
    m->state = Data::ApplianceIdle;
    appLock.release();

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return rc;
}

HRESULT Appliance::importFSOVA(TaskOVF *pTask)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    int vrc = VINF_SUCCESS;
    char szOSTmpDir[RTPATH_MAX];
    RTPathTemp(szOSTmpDir, sizeof(szOSTmpDir));
    /* The template for the temporary directory created below */
    char *pszTmpDir;
    RTStrAPrintf(&pszTmpDir, "%s"RTPATH_SLASH_STR"vbox-ovf-XXXXXX", szOSTmpDir);
    list< pair<Utf8Str, ULONG> > filesList;
    const char** paFiles = 0;

    HRESULT rc = S_OK;
    try
    {
        /* Extract the path */
        Utf8Str tmpPath = pTask->locInfo.strPath;
        /* Remove the ova extension */
        tmpPath.stripExt();
        tmpPath += ".ovf";

        /* We need a temporary directory which we can put the all disk images
         * in */
        vrc = RTDirCreateTemp(pszTmpDir);
        if (RT_FAILURE(vrc))
            DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                     tr("Cannot create temporary directory '%s' (%Rrc)"), pszTmpDir, vrc));

        /* Provide a OVF file (haven't to exist) so the import routine can
         * figure out where the disk images/manifest file are located. */
        Utf8StrFmt strTmpOvf("%s/%s", pszTmpDir, RTPathFilename(tmpPath.c_str()));
        /* Add the manifest file to the list of files to extract, but only if
           one is in the archive. */
        Utf8Str strManifestFile = manifestFileName(strTmpOvf);
        vrc = RTTarQueryFileExists(pTask->locInfo.strPath.c_str(), RTPathFilename(strManifestFile.c_str()));
        if (RT_SUCCESS(vrc))
            filesList.push_back(pair<Utf8Str, ULONG>(strManifestFile.c_str(), 1));

        ULONG ulWeight = m->ulWeightForXmlOperation;
        /* Add every disks of every virtual system to an internal list */
        list< ComObjPtr<VirtualSystemDescription> >::const_iterator it;
        for (it = m->virtualSystemDescriptions.begin();
             it != m->virtualSystemDescriptions.end();
             ++it)
        {
            ComObjPtr<VirtualSystemDescription> vsdescThis = (*it);
            std::list<VirtualSystemDescriptionEntry*> avsdeHDs = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskImage);
            std::list<VirtualSystemDescriptionEntry*>::const_iterator itH;
            for (itH = avsdeHDs.begin();
                 itH != avsdeHDs.end();
                 ++itH)
            {
                const Utf8Str &strTargetFile = (*itH)->strOvf;
                if (!strTargetFile.isEmpty())
                {
                    /* The temporary name of the target disk file */
                    Utf8StrFmt strTmpDisk("%s/%s", pszTmpDir, RTPathFilename(strTargetFile.c_str()));
                    filesList.push_back(pair<Utf8Str, ULONG>(strTmpDisk, (*itH)->ulSizeMB));
                    ulWeight += (*itH)->ulSizeMB;
                }
            }
        }

        /* Download all files */
        paFiles = (const char**)RTMemAlloc(sizeof(char*) * filesList.size());
        int i = 0;
        for (list< pair<Utf8Str, ULONG> >::const_iterator it1 = filesList.begin(); it1 != filesList.end(); ++it1, ++i)
            paFiles[i] = RTPathFilename((*it1).first.c_str());
        if (!pTask->pProgress.isNull())
            pTask->pProgress->SetNextOperation(BstrFmt(tr("Unpacking file '%s'"), RTPathFilename(pTask->locInfo.strPath.c_str())), ulWeight);
        vrc = RTTarExtractFiles(pTask->locInfo.strPath.c_str(), pszTmpDir, paFiles, filesList.size(), pTask->updateProgress, &pTask);
        if (RT_FAILURE(vrc))
            throw setError(VBOX_E_FILE_ERROR,
                           tr("Cannot unpack archive file '%s' (%Rrc)"), pTask->locInfo.strPath.c_str(), vrc);

//        if (!pTask->pProgress.isNull())
//            pTask->pProgress->SetNextOperation(BstrFmt(tr("Importing appliance")), m->ulWeightForXmlOperation);

        ComObjPtr<Progress> progress;
        /* Import the whole temporary OVF & the disk images */
        LocationInfo li;
        li.strPath = strTmpOvf;
        rc = importImpl(li, progress);
        if (FAILED(rc)) DebugBreakThrow(rc);

        /* Unlock the appliance for the fs import thread */
        appLock.release();
        /* Wait until the import is done, but report the progress back to the
           caller */
        ComPtr<IProgress> progressInt(progress);
        waitForAsyncProgress(pTask->pProgress, progressInt); /* Any errors will be thrown */

        /* Again lock the appliance for the next steps */
        appLock.acquire();
    }
    catch(HRESULT aRC)
    {
        rc = aRC;
    }
    /* Delete the temporary files list */
    if (paFiles)
        RTMemFree(paFiles);
    /* Delete all files which where temporary created */
    for (list< pair<Utf8Str, ULONG> >::const_iterator it1 = filesList.begin(); it1 != filesList.end(); ++it1)
    {
        const char *pszFilePath = (*it1).first.c_str();
        if (RTPathExists(pszFilePath))
        {
            vrc = RTFileDelete(pszFilePath);
            if (RT_FAILURE(vrc))
                rc = setError(VBOX_E_FILE_ERROR,
                              tr("Cannot delete file '%s' (%Rrc)"), pszFilePath, vrc);
        }
    }
    /* Delete the temporary directory */
    if (RTPathExists(pszTmpDir))
    {
        vrc = RTDirRemove(pszTmpDir);
        if (RT_FAILURE(vrc))
            rc = setError(VBOX_E_FILE_ERROR,
                          tr("Cannot delete temporary directory '%s' (%Rrc)"), pszTmpDir, vrc);
    }
    if (pszTmpDir)
        RTStrFree(pszTmpDir);

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return rc;
}

/**
 * Imports one disk image. This is common code shared between
 *  --  importMachineGeneric() for the OVF case; in that case the information comes from
 *      the OVF virtual systems;
 *  --  importVBoxMachine(); in that case, the information comes from the <vbox:Machine>
 *      tag.
 *
 * Both ways of describing machines use the OVF disk references section, so in both cases
 * the caller needs to pass in the ovf::DiskImage structure from ovfreader.cpp.
 *
 * As a result, in both cases, if di.strHref is empty, we create a new disk as per the OVF
 * spec, even though this cannot really happen in the vbox:Machine case since such data
 * would never have been exported.
 *
 * This advances stack.pProgress by one operation with the disk's weight.
 *
 * @param di ovfreader.cpp structure describing the disk image from the OVF that is to be imported
 * @param ulSizeMB Size of the disk image (for progress reporting)
 * @param strTargetPath Where to create the target image.
 * @param pTargetHD out: The newly created target disk. This also gets pushed on stack.llHardDisksCreated for cleanup.
 * @param stack
 */
void Appliance::importOneDiskImage(const ovf::DiskImage &di,
                                   const Utf8Str &strTargetPath,
                                   ComPtr<IMedium> &pTargetHD,
                                   ImportStack &stack)
{
    ComPtr<IMedium> pSourceHD;
    bool fSourceHdNeedsClosing = false;

    try
    {
        // destination file must not exist
        if (    strTargetPath.isEmpty()
             || RTPathExists(strTargetPath.c_str())
           )
            DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                     tr("Destination file '%s' exists"),
                                     strTargetPath.c_str()));

        const Utf8Str &strSourceOVF = di.strHref;

        // Make sure target directory exists
        HRESULT rc = VirtualBox::ensureFilePathExists(strTargetPath.c_str());
        if (FAILED(rc)) DebugBreakThrow(rc);

        // subprogress object for hard disk
        ComPtr<IProgress> pProgress2;

        /* If strHref is empty we have to create a new file */
        if (strSourceOVF.isEmpty())
        {
            // which format to use?
            Bstr srcFormat = L"VDI";
            if (   di.strFormat.compare("http://www.vmware.com/specifications/vmdk.html#sparse", Utf8Str::CaseInsensitive)
                || di.strFormat.compare("http://www.vmware.com/interfaces/specifications/vmdk.html#streamOptimized", Utf8Str::CaseInsensitive)
                || di.strFormat.compare("http://www.vmware.com/specifications/vmdk.html#compressed", Utf8Str::CaseInsensitive)
                || di.strFormat.compare("http://www.vmware.com/interfaces/specifications/vmdk.html#compressed", Utf8Str::CaseInsensitive)
               )
                srcFormat = L"VMDK";
            // create an empty hard disk
            rc = mVirtualBox->CreateHardDisk(srcFormat,
                                             Bstr(strTargetPath),
                                             pTargetHD.asOutParam());
            if (FAILED(rc)) DebugBreakThrow(rc);

            // create a dynamic growing disk image with the given capacity
            rc = pTargetHD->CreateBaseStorage(di.iCapacity / _1M, MediumVariant_Standard, pProgress2.asOutParam());
            if (FAILED(rc)) DebugBreakThrow(rc);

            // advance to the next operation
            stack.pProgress->SetNextOperation(BstrFmt(tr("Creating disk image '%s'"), strTargetPath.c_str()),
                                              di.ulSuggestedSizeMB);     // operation's weight, as set up with the IProgress originally
        }
        else
        {
            // construct source file path
            Utf8StrFmt strSrcFilePath("%s%c%s", stack.strSourceDir.c_str(), RTPATH_DELIMITER, strSourceOVF.c_str());
            // source path must exist
            if (!RTPathExists(strSrcFilePath.c_str()))
                DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                         tr("Source virtual disk image file '%s' doesn't exist"),
                                         strSrcFilePath.c_str()));

            // Clone the disk image (this is necessary cause the id has
            // to be recreated for the case the same hard disk is
            // attached already from a previous import)

            // First open the existing disk image
            rc = mVirtualBox->OpenMedium(Bstr(strSrcFilePath),
                                         DeviceType_HardDisk,
                                         AccessMode_ReadOnly,
                                         pSourceHD.asOutParam());
            if (FAILED(rc)) DebugBreakThrow(rc);
            fSourceHdNeedsClosing = true;

            /* We need the format description of the source disk image */
            Bstr srcFormat;
            rc = pSourceHD->COMGETTER(Format)(srcFormat.asOutParam());
            if (FAILED(rc)) DebugBreakThrow(rc);
            /* Create a new hard disk interface for the destination disk image */
            rc = mVirtualBox->CreateHardDisk(srcFormat,
                                             Bstr(strTargetPath),
                                             pTargetHD.asOutParam());
            if (FAILED(rc)) DebugBreakThrow(rc);
            /* Clone the source disk image */
            rc = pSourceHD->CloneTo(pTargetHD, MediumVariant_Standard, NULL, pProgress2.asOutParam());
            if (FAILED(rc)) DebugBreakThrow(rc);

            /* Advance to the next operation */
            stack.pProgress->SetNextOperation(BstrFmt(tr("Importing virtual disk image '%s'"), RTPathFilename(strSrcFilePath.c_str())),
                                              di.ulSuggestedSizeMB);     // operation's weight, as set up with the IProgress originally);
        }

        // now wait for the background disk operation to complete; this throws HRESULTs on error
        waitForAsyncProgress(stack.pProgress, pProgress2);

        if (fSourceHdNeedsClosing)
        {
            rc = pSourceHD->Close();
            if (FAILED(rc)) DebugBreakThrow(rc);
            fSourceHdNeedsClosing = false;
        }

        stack.llHardDisksCreated.push_back(pTargetHD);
    }
    catch (...)
    {
        if (fSourceHdNeedsClosing)
            pSourceHD->Close();

        throw;
    }
}

/**
 * Imports one OVF virtual system (described by the given ovf::VirtualSystem and VirtualSystemDescription)
 * into VirtualBox by creating an IMachine instance, which is returned.
 *
 * This throws HRESULT error codes for anything that goes wrong, in which case the caller must clean
 * up any leftovers from this function. For this, the given ImportStack instance has received information
 * about what needs cleaning up (to support rollback).
 *
 * @param vsysThis OVF virtual system (machine) to import.
 * @param vsdescThis  Matching virtual system description (machine) to import.
 * @param pNewMachine out: Newly created machine.
 * @param stack Cleanup stack for when this throws.
 */
void Appliance::importMachineGeneric(const ovf::VirtualSystem &vsysThis,
                                     ComObjPtr<VirtualSystemDescription> &vsdescThis,
                                     ComPtr<IMachine> &pNewMachine,
                                     ImportStack &stack)
{
    HRESULT rc;

    // Get the instance of IGuestOSType which matches our string guest OS type so we
    // can use recommended defaults for the new machine where OVF doesen't provice any
    ComPtr<IGuestOSType> osType;
    rc = mVirtualBox->GetGuestOSType(Bstr(stack.strOsTypeVBox), osType.asOutParam());
    if (FAILED(rc)) DebugBreakThrow(rc);

    /* Create the machine */
    rc = mVirtualBox->CreateMachine(Bstr(stack.strNameVBox),
                                    Bstr(stack.strOsTypeVBox),
                                    NULL,
                                    NULL,
                                    FALSE,
                                    pNewMachine.asOutParam());
    if (FAILED(rc)) DebugBreakThrow(rc);

    // set the description
    if (!stack.strDescription.isEmpty())
    {
        rc = pNewMachine->COMSETTER(Description)(Bstr(stack.strDescription));
        if (FAILED(rc)) DebugBreakThrow(rc);
    }

    // CPU count
    rc = pNewMachine->COMSETTER(CPUCount)(stack.cCPUs);
    if (FAILED(rc)) DebugBreakThrow(rc);

    if (stack.fForceHWVirt)
    {
        rc = pNewMachine->SetHWVirtExProperty(HWVirtExPropertyType_Enabled, TRUE);
        if (FAILED(rc)) DebugBreakThrow(rc);
    }

    // RAM
    rc = pNewMachine->COMSETTER(MemorySize)(stack.ulMemorySizeMB);
    if (FAILED(rc)) DebugBreakThrow(rc);

    /* VRAM */
    /* Get the recommended VRAM for this guest OS type */
    ULONG vramVBox;
    rc = osType->COMGETTER(RecommendedVRAM)(&vramVBox);
    if (FAILED(rc)) DebugBreakThrow(rc);

    /* Set the VRAM */
    rc = pNewMachine->COMSETTER(VRAMSize)(vramVBox);
    if (FAILED(rc)) DebugBreakThrow(rc);

    // I/O APIC: Generic OVF has no setting for this. Enable it if we
    // import a Windows VM because if if Windows was installed without IOAPIC,
    // it will not mind finding an one later on, but if Windows was installed
    // _with_ an IOAPIC, it will bluescreen if it's not found
    if (!stack.fForceIOAPIC)
    {
        Bstr bstrFamilyId;
        rc = osType->COMGETTER(FamilyId)(bstrFamilyId.asOutParam());
        if (FAILED(rc)) DebugBreakThrow(rc);
        if (bstrFamilyId == "Windows")
            stack.fForceIOAPIC = true;
    }

    if (stack.fForceIOAPIC)
    {
        ComPtr<IBIOSSettings> pBIOSSettings;
        rc = pNewMachine->COMGETTER(BIOSSettings)(pBIOSSettings.asOutParam());
        if (FAILED(rc)) DebugBreakThrow(rc);

        rc = pBIOSSettings->COMSETTER(IOAPICEnabled)(TRUE);
        if (FAILED(rc)) DebugBreakThrow(rc);
    }

    if (!stack.strAudioAdapter.isEmpty())
        if (stack.strAudioAdapter.compare("null", Utf8Str::CaseInsensitive) != 0)
        {
            uint32_t audio = RTStrToUInt32(stack.strAudioAdapter.c_str());       // should be 0 for AC97
            ComPtr<IAudioAdapter> audioAdapter;
            rc = pNewMachine->COMGETTER(AudioAdapter)(audioAdapter.asOutParam());
            if (FAILED(rc)) DebugBreakThrow(rc);
            rc = audioAdapter->COMSETTER(Enabled)(true);
            if (FAILED(rc)) DebugBreakThrow(rc);
            rc = audioAdapter->COMSETTER(AudioController)(static_cast<AudioControllerType_T>(audio));
            if (FAILED(rc)) DebugBreakThrow(rc);
        }

#ifdef VBOX_WITH_USB
    /* USB Controller */
    ComPtr<IUSBController> usbController;
    rc = pNewMachine->COMGETTER(USBController)(usbController.asOutParam());
    if (FAILED(rc)) DebugBreakThrow(rc);
    rc = usbController->COMSETTER(Enabled)(stack.fUSBEnabled);
    if (FAILED(rc)) DebugBreakThrow(rc);
#endif /* VBOX_WITH_USB */

    /* Change the network adapters */
    std::list<VirtualSystemDescriptionEntry*> vsdeNW = vsdescThis->findByType(VirtualSystemDescriptionType_NetworkAdapter);
    if (vsdeNW.size() == 0)
    {
        /* No network adapters, so we have to disable our default one */
        ComPtr<INetworkAdapter> nwVBox;
        rc = pNewMachine->GetNetworkAdapter(0, nwVBox.asOutParam());
        if (FAILED(rc)) DebugBreakThrow(rc);
        rc = nwVBox->COMSETTER(Enabled)(false);
        if (FAILED(rc)) DebugBreakThrow(rc);
    }
    else if (vsdeNW.size() > SchemaDefs::NetworkAdapterCount)
        DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                 tr("Too many network adapters: OVF requests %d network adapters, but VirtualBox only supports %d"),
                                 vsdeNW.size(), SchemaDefs::NetworkAdapterCount));
    else
    {
        list<VirtualSystemDescriptionEntry*>::const_iterator nwIt;
        size_t a = 0;
        for (nwIt = vsdeNW.begin();
             nwIt != vsdeNW.end();
             ++nwIt, ++a)
        {
            const VirtualSystemDescriptionEntry* pvsys = *nwIt;

            const Utf8Str &nwTypeVBox = pvsys->strVboxCurrent;
            uint32_t tt1 = RTStrToUInt32(nwTypeVBox.c_str());
            ComPtr<INetworkAdapter> pNetworkAdapter;
            rc = pNewMachine->GetNetworkAdapter((ULONG)a, pNetworkAdapter.asOutParam());
            if (FAILED(rc)) DebugBreakThrow(rc);
            /* Enable the network card & set the adapter type */
            rc = pNetworkAdapter->COMSETTER(Enabled)(true);
            if (FAILED(rc)) DebugBreakThrow(rc);
            rc = pNetworkAdapter->COMSETTER(AdapterType)(static_cast<NetworkAdapterType_T>(tt1));
            if (FAILED(rc)) DebugBreakThrow(rc);

            // default is NAT; change to "bridged" if extra conf says so
            if (!pvsys->strExtraConfigCurrent.compare("type=Bridged", Utf8Str::CaseInsensitive))
            {
                /* Attach to the right interface */
                rc = pNetworkAdapter->AttachToBridgedInterface();
                if (FAILED(rc)) DebugBreakThrow(rc);
                ComPtr<IHost> host;
                rc = mVirtualBox->COMGETTER(Host)(host.asOutParam());
                if (FAILED(rc)) DebugBreakThrow(rc);
                com::SafeIfaceArray<IHostNetworkInterface> nwInterfaces;
                rc = host->COMGETTER(NetworkInterfaces)(ComSafeArrayAsOutParam(nwInterfaces));
                if (FAILED(rc)) DebugBreakThrow(rc);
                // We search for the first host network interface which
                // is usable for bridged networking
                for (size_t j = 0;
                     j < nwInterfaces.size();
                     ++j)
                {
                    HostNetworkInterfaceType_T itype;
                    rc = nwInterfaces[j]->COMGETTER(InterfaceType)(&itype);
                    if (FAILED(rc)) DebugBreakThrow(rc);
                    if (itype == HostNetworkInterfaceType_Bridged)
                    {
                        Bstr name;
                        rc = nwInterfaces[j]->COMGETTER(Name)(name.asOutParam());
                        if (FAILED(rc)) DebugBreakThrow(rc);
                        /* Set the interface name to attach to */
                        pNetworkAdapter->COMSETTER(HostInterface)(name);
                        if (FAILED(rc)) DebugBreakThrow(rc);
                        break;
                    }
                }
            }
            /* Next test for host only interfaces */
            else if (!pvsys->strExtraConfigCurrent.compare("type=HostOnly", Utf8Str::CaseInsensitive))
            {
                /* Attach to the right interface */
                rc = pNetworkAdapter->AttachToHostOnlyInterface();
                if (FAILED(rc)) DebugBreakThrow(rc);
                ComPtr<IHost> host;
                rc = mVirtualBox->COMGETTER(Host)(host.asOutParam());
                if (FAILED(rc)) DebugBreakThrow(rc);
                com::SafeIfaceArray<IHostNetworkInterface> nwInterfaces;
                rc = host->COMGETTER(NetworkInterfaces)(ComSafeArrayAsOutParam(nwInterfaces));
                if (FAILED(rc)) DebugBreakThrow(rc);
                // We search for the first host network interface which
                // is usable for host only networking
                for (size_t j = 0;
                     j < nwInterfaces.size();
                     ++j)
                {
                    HostNetworkInterfaceType_T itype;
                    rc = nwInterfaces[j]->COMGETTER(InterfaceType)(&itype);
                    if (FAILED(rc)) DebugBreakThrow(rc);
                    if (itype == HostNetworkInterfaceType_HostOnly)
                    {
                        Bstr name;
                        rc = nwInterfaces[j]->COMGETTER(Name)(name.asOutParam());
                        if (FAILED(rc)) DebugBreakThrow(rc);
                        /* Set the interface name to attach to */
                        pNetworkAdapter->COMSETTER(HostInterface)(name);
                        if (FAILED(rc)) DebugBreakThrow(rc);
                        break;
                    }
                }
            }
        }
    }

    // IDE Hard disk controller
    std::list<VirtualSystemDescriptionEntry*> vsdeHDCIDE = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskControllerIDE);
    // In OVF (at least VMware's version of it), an IDE controller has two ports, so VirtualBox's single IDE controller
    // with two channels and two ports each counts as two OVF IDE controllers -- so we accept one or two such IDE controllers
    uint32_t cIDEControllers = vsdeHDCIDE.size();
    if (cIDEControllers > 2)
        DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                 tr("Too many IDE controllers in OVF; import facility only supports two")));
    if (vsdeHDCIDE.size() > 0)
    {
        // one or two IDE controllers present in OVF: add one VirtualBox controller
        ComPtr<IStorageController> pController;
        rc = pNewMachine->AddStorageController(Bstr("IDE Controller"), StorageBus_IDE, pController.asOutParam());
        if (FAILED(rc)) DebugBreakThrow(rc);

        const char *pcszIDEType = vsdeHDCIDE.front()->strVboxCurrent.c_str();
        if (!strcmp(pcszIDEType, "PIIX3"))
            rc = pController->COMSETTER(ControllerType)(StorageControllerType_PIIX3);
        else if (!strcmp(pcszIDEType, "PIIX4"))
            rc = pController->COMSETTER(ControllerType)(StorageControllerType_PIIX4);
        else if (!strcmp(pcszIDEType, "ICH6"))
            rc = pController->COMSETTER(ControllerType)(StorageControllerType_ICH6);
        else
            DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                     tr("Invalid IDE controller type \"%s\""),
                                     pcszIDEType));
        if (FAILED(rc)) DebugBreakThrow(rc);
    }

    /* Hard disk controller SATA */
    std::list<VirtualSystemDescriptionEntry*> vsdeHDCSATA = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskControllerSATA);
    if (vsdeHDCSATA.size() > 1)
        DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                 tr("Too many SATA controllers in OVF; import facility only supports one")));
    if (vsdeHDCSATA.size() > 0)
    {
        ComPtr<IStorageController> pController;
        const Utf8Str &hdcVBox = vsdeHDCSATA.front()->strVboxCurrent;
        if (hdcVBox == "AHCI")
        {
            rc = pNewMachine->AddStorageController(Bstr("SATA Controller"), StorageBus_SATA, pController.asOutParam());
            if (FAILED(rc)) DebugBreakThrow(rc);
        }
        else
            DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                     tr("Invalid SATA controller type \"%s\""),
                                     hdcVBox.c_str()));
    }

    /* Hard disk controller SCSI */
    std::list<VirtualSystemDescriptionEntry*> vsdeHDCSCSI = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskControllerSCSI);
    if (vsdeHDCSCSI.size() > 1)
        DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                 tr("Too many SCSI controllers in OVF; import facility only supports one")));
    if (vsdeHDCSCSI.size() > 0)
    {
        ComPtr<IStorageController> pController;
        Bstr bstrName(L"SCSI Controller");
        StorageBus_T busType = StorageBus_SCSI;
        StorageControllerType_T controllerType;
        const Utf8Str &hdcVBox = vsdeHDCSCSI.front()->strVboxCurrent;
        if (hdcVBox == "LsiLogic")
            controllerType = StorageControllerType_LsiLogic;
        else if (hdcVBox == "LsiLogicSas")
        {
            // OVF treats LsiLogicSas as a SCSI controller but VBox considers it a class of its own
            bstrName = L"SAS Controller";
            busType = StorageBus_SAS;
            controllerType = StorageControllerType_LsiLogicSas;
        }
        else if (hdcVBox == "BusLogic")
            controllerType = StorageControllerType_BusLogic;
        else
            DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                     tr("Invalid SCSI controller type \"%s\""),
                                     hdcVBox.c_str()));

        rc = pNewMachine->AddStorageController(bstrName, busType, pController.asOutParam());
        if (FAILED(rc)) DebugBreakThrow(rc);
        rc = pController->COMSETTER(ControllerType)(controllerType);
        if (FAILED(rc)) DebugBreakThrow(rc);
    }

    /* Hard disk controller SAS */
    std::list<VirtualSystemDescriptionEntry*> vsdeHDCSAS = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskControllerSAS);
    if (vsdeHDCSAS.size() > 1)
        DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                 tr("Too many SAS controllers in OVF; import facility only supports one")));
    if (vsdeHDCSAS.size() > 0)
    {
        ComPtr<IStorageController> pController;
        rc = pNewMachine->AddStorageController(Bstr(L"SAS Controller"), StorageBus_SAS, pController.asOutParam());
        if (FAILED(rc)) DebugBreakThrow(rc);
        rc = pController->COMSETTER(ControllerType)(StorageControllerType_LsiLogicSas);
        if (FAILED(rc)) DebugBreakThrow(rc);
    }

    /* Now its time to register the machine before we add any hard disks */
    rc = mVirtualBox->RegisterMachine(pNewMachine);
    if (FAILED(rc)) DebugBreakThrow(rc);

    // store new machine for roll-back in case of errors
    Bstr bstrNewMachineId;
    rc = pNewMachine->COMGETTER(Id)(bstrNewMachineId.asOutParam());
    if (FAILED(rc)) DebugBreakThrow(rc);
    Guid uuidNewMachine(bstrNewMachineId);
    m->llGuidsMachinesCreated.push_back(uuidNewMachine);

    // Add floppies and CD-ROMs to the appropriate controllers.
    std::list<VirtualSystemDescriptionEntry*> vsdeFloppy = vsdescThis->findByType(VirtualSystemDescriptionType_Floppy);
    if (vsdeFloppy.size() > 1)
        DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                 tr("Too many floppy controllers in OVF; import facility only supports one")));
    std::list<VirtualSystemDescriptionEntry*> vsdeCDROM = vsdescThis->findByType(VirtualSystemDescriptionType_CDROM);
    if (    (vsdeFloppy.size() > 0)
         || (vsdeCDROM.size() > 0)
       )
    {
        // If there's an error here we need to close the session, so
        // we need another try/catch block.

        try
        {
            // to attach things we need to open a session for the new machine
            rc = pNewMachine->LockMachine(stack.pSession, LockType_Write);
            if (FAILED(rc)) DebugBreakThrow(rc);
            stack.fSessionOpen = true;

            ComPtr<IMachine> sMachine;
            rc = stack.pSession->COMGETTER(Machine)(sMachine.asOutParam());
            if (FAILED(rc)) DebugBreakThrow(rc);

            // floppy first
            if (vsdeFloppy.size() == 1)
            {
                ComPtr<IStorageController> pController;
                rc = sMachine->AddStorageController(Bstr("Floppy Controller"), StorageBus_Floppy, pController.asOutParam());
                if (FAILED(rc)) DebugBreakThrow(rc);

                Bstr bstrName;
                rc = pController->COMGETTER(Name)(bstrName.asOutParam());
                if (FAILED(rc)) DebugBreakThrow(rc);

                // this is for rollback later
                MyHardDiskAttachment mhda;
                mhda.pMachine = pNewMachine;
                mhda.controllerType = bstrName;
                mhda.lControllerPort = 0;
                mhda.lDevice = 0;

                Log(("Attaching floppy\n"));

                rc = sMachine->AttachDevice(mhda.controllerType,
                                            mhda.lControllerPort,
                                            mhda.lDevice,
                                            DeviceType_Floppy,
                                            NULL);
                if (FAILED(rc)) DebugBreakThrow(rc);

                stack.llHardDiskAttachments.push_back(mhda);
            }

            // CD-ROMs next
            for (std::list<VirtualSystemDescriptionEntry*>::const_iterator jt = vsdeCDROM.begin();
                 jt != vsdeCDROM.end();
                 ++jt)
            {
                // for now always attach to secondary master on IDE controller;
                // there seems to be no useful information in OVF where else to
                // attach it (@todo test with latest versions of OVF software)

                // find the IDE controller
                const ovf::HardDiskController *pController = NULL;
                for (ovf::ControllersMap::const_iterator kt = vsysThis.mapControllers.begin();
                     kt != vsysThis.mapControllers.end();
                     ++kt)
                {
                    if (kt->second.system == ovf::HardDiskController::IDE)
                    {
                        pController = &kt->second;
                        break;
                    }
                }

                if (!pController)
                    DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                             tr("OVF wants a CD-ROM drive but cannot find IDE controller, which is required in this version of VirtualBox")));

                // this is for rollback later
                MyHardDiskAttachment mhda;
                mhda.pMachine = pNewMachine;

                convertDiskAttachmentValues(*pController,
                                            2,     // interpreted as secondary master
                                            mhda.controllerType,        // Bstr
                                            mhda.lControllerPort,
                                            mhda.lDevice);

                Log(("Attaching CD-ROM to port %d on device %d\n", mhda.lControllerPort, mhda.lDevice));

                rc = sMachine->AttachDevice(mhda.controllerType,
                                            mhda.lControllerPort,
                                            mhda.lDevice,
                                            DeviceType_DVD,
                                            NULL);
                if (FAILED(rc)) DebugBreakThrow(rc);

                stack.llHardDiskAttachments.push_back(mhda);
            } // end for (itHD = avsdeHDs.begin();

            rc = sMachine->SaveSettings();
            if (FAILED(rc)) DebugBreakThrow(rc);

            // only now that we're done with all disks, close the session
            rc = stack.pSession->UnlockMachine();
            if (FAILED(rc)) DebugBreakThrow(rc);
            stack.fSessionOpen = false;
        }
        catch(HRESULT /* aRC */)
        {
            if (stack.fSessionOpen)
                stack.pSession->UnlockMachine();

            throw;
        }
    }

    // create the hard disks & connect them to the appropriate controllers
    std::list<VirtualSystemDescriptionEntry*> avsdeHDs = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskImage);
    if (avsdeHDs.size() > 0)
    {
        // If there's an error here we need to close the session, so
        // we need another try/catch block.
        try
        {
            // to attach things we need to open a session for the new machine
            rc = pNewMachine->LockMachine(stack.pSession, LockType_Write);
            if (FAILED(rc)) DebugBreakThrow(rc);
            stack.fSessionOpen = true;

            /* Iterate over all given disk images */
            list<VirtualSystemDescriptionEntry*>::const_iterator itHD;
            for (itHD = avsdeHDs.begin();
                 itHD != avsdeHDs.end();
                 ++itHD)
            {
                VirtualSystemDescriptionEntry *vsdeHD = *itHD;

                // vsdeHD->strRef contains the disk identifier (e.g. "vmdisk1"), which should exist
                // in the virtual system's disks map under that ID and also in the global images map
                ovf::VirtualDisksMap::const_iterator itVirtualDisk = vsysThis.mapVirtualDisks.find(vsdeHD->strRef);
                // and find the disk from the OVF's disk list
                ovf::DiskImagesMap::const_iterator itDiskImage = stack.mapDisks.find(vsdeHD->strRef);
                if (    (itVirtualDisk == vsysThis.mapVirtualDisks.end())
                     || (itDiskImage == stack.mapDisks.end())
                   )
                    DebugBreakThrow(setError(E_FAIL,
                                             tr("Internal inconsistency looking up disk image '%s'"),
                                             vsdeHD->strRef.c_str()));

                const ovf::DiskImage &ovfDiskImage = itDiskImage->second;
                const ovf::VirtualDisk &ovfVdisk = itVirtualDisk->second;

                ComPtr<IMedium> pTargetHD;
                importOneDiskImage(ovfDiskImage,
                                   vsdeHD->strVboxCurrent,
                                   pTargetHD,
                                   stack);

                // now use the new uuid to attach the disk image to our new machine
                ComPtr<IMachine> sMachine;
                rc = stack.pSession->COMGETTER(Machine)(sMachine.asOutParam());
                if (FAILED(rc)) DebugBreakThrow(rc);

                // find the hard disk controller to which we should attach
                ovf::HardDiskController hdc = (*vsysThis.mapControllers.find(ovfVdisk.idController)).second;

                // this is for rollback later
                MyHardDiskAttachment mhda;
                mhda.pMachine = pNewMachine;

                convertDiskAttachmentValues(hdc,
                                            ovfVdisk.ulAddressOnParent,
                                            mhda.controllerType,        // Bstr
                                            mhda.lControllerPort,
                                            mhda.lDevice);

                Log(("Attaching disk %s to port %d on device %d\n", vsdeHD->strVboxCurrent.c_str(), mhda.lControllerPort, mhda.lDevice));

                rc = sMachine->AttachDevice(mhda.controllerType,    // wstring name
                                            mhda.lControllerPort,          // long controllerPort
                                            mhda.lDevice,           // long device
                                            DeviceType_HardDisk,    // DeviceType_T type
                                            pTargetHD);
                if (FAILED(rc)) DebugBreakThrow(rc);

                stack.llHardDiskAttachments.push_back(mhda);

                rc = sMachine->SaveSettings();
                if (FAILED(rc)) DebugBreakThrow(rc);
            } // end for (itHD = avsdeHDs.begin();

            // only now that we're done with all disks, close the session
            rc = stack.pSession->UnlockMachine();
            if (FAILED(rc)) DebugBreakThrow(rc);
            stack.fSessionOpen = false;
        }
        catch(HRESULT /* aRC */)
        {
            if (stack.fSessionOpen)
                stack.pSession->UnlockMachine();

            throw;
        }
    }
}

/**
 * Imports one OVF virtual system (described by a vbox:Machine tag represented by the given config
 * structure) into VirtualBox by creating an IMachine instance, which is returned.
 *
 * This throws HRESULT error codes for anything that goes wrong, in which case the caller must clean
 * up any leftovers from this function. For this, the given ImportStack instance has received information
 * about what needs cleaning up (to support rollback).
 *
 * The machine config stored in the settings::MachineConfigFile structure contains the UUIDs of
 * the disk attachments used by the machine when it was exported. We also add vbox:uuid attributes
 * to the OVF disks sections so we can look them up. While importing these UUIDs into a second host
 * will most probably work, reimporting them into the same host will cause conflicts, so we always
 * generate new ones on import. This involves the following:
 *
 *  1)  Scan the machine config for disk attachments.
 *
 *  2)  For each disk attachment found, look up the OVF disk image from the disk references section
 *      and import the disk into VirtualBox, which creates a new UUID for it. In the machine config,
 *      replace the old UUID with the new one.
 *
 *  3)  Change the machine config according to the OVF virtual system descriptions, in case the
 *      caller has modified them using setFinalValues().
 *
 *  4)  Create the VirtualBox machine with the modfified machine config.
 *
 * @param config
 * @param pNewMachine
 * @param stack
 */
void Appliance::importVBoxMachine(ComObjPtr<VirtualSystemDescription> &vsdescThis,
                                  ComPtr<IMachine> &pReturnNewMachine,
                                  ImportStack &stack)
{
    Assert(vsdescThis->m->pConfig);

    settings::MachineConfigFile &config = *vsdescThis->m->pConfig;

    Utf8Str strDefaultHardDiskFolder;
    HRESULT rc = getDefaultHardDiskFolder(strDefaultHardDiskFolder);
    if (FAILED(rc)) DebugBreakThrow(rc);

    /*
     *
     * step 1): modify machine config according to OVF config, in case the user
     * has modified them using setFinalValues()
     *
     */

    config.machineUserData.strDescription = stack.strDescription;

    config.hardwareMachine.cCPUs = stack.cCPUs;
    config.hardwareMachine.ulMemorySizeMB = stack.ulMemorySizeMB;
    if (stack.fForceIOAPIC)
        config.hardwareMachine.fHardwareVirt = true;
    if (stack.fForceIOAPIC)
        config.hardwareMachine.biosSettings.fIOAPICEnabled = true;

/*
    <const name="HardDiskControllerIDE" value="14" />
    <const name="HardDiskControllerSATA" value="15" />
    <const name="HardDiskControllerSCSI" value="16" />
    <const name="HardDiskControllerSAS" value="17" />
    <const name="HardDiskImage" value="18" />
    <const name="Floppy" value="19" />
    <const name="CDROM" value="20" />
    <const name="NetworkAdapter" value="21" />
*/

#ifdef VBOX_WITH_USB
    // disable USB if user disabled USB
    config.hardwareMachine.usbController.fEnabled = stack.fUSBEnabled;
#endif

    // audio adapter: only config is turning it off presently
    if (stack.strAudioAdapter.isEmpty())
        config.hardwareMachine.audioAdapter.fEnabled = false;

    /*
     *
     * step 2: scan the machine config for media attachments
     *
     */

    // for each storage controller...
    for (settings::StorageControllersList::iterator sit = config.storageMachine.llStorageControllers.begin();
         sit != config.storageMachine.llStorageControllers.end();
         ++sit)
    {
        settings::StorageController &sc = *sit;

        // find the OVF virtual system description entry for this storage controller
        switch (sc.storageBus)
        {
            case StorageBus_SATA:
            break;

            case StorageBus_SCSI:
            break;

            case StorageBus_IDE:
            break;

            case StorageBus_SAS:
            break;
        }

        // for each medium attachment to this controller...
        for (settings::AttachedDevicesList::iterator dit = sc.llAttachedDevices.begin();
             dit != sc.llAttachedDevices.end();
             ++dit)
        {
            settings::AttachedDevice &d = *dit;

            if (d.uuid.isEmpty())
                // empty DVD and floppy media
                continue;

            // convert the Guid to string
            Utf8Str strUuid = d.uuid.toString();

            // there must be an image in the OVF disk structs with the same UUID
            bool fFound = false;
            for (ovf::DiskImagesMap::const_iterator oit = stack.mapDisks.begin();
                 oit != stack.mapDisks.end();
                 ++oit)
            {
                const ovf::DiskImage &di = oit->second;

                if (di.uuidVbox == strUuid)
                {
                    Utf8Str strTargetPath(strDefaultHardDiskFolder);
                    strTargetPath.append(RTPATH_DELIMITER);
                    strTargetPath.append(di.strHref);
                    searchUniqueDiskImageFilePath(strTargetPath);

                    /*
                     *
                     * step 3: import disk
                     *
                     */
                    ComPtr<IMedium> pTargetHD;
                    importOneDiskImage(di,
                                       strTargetPath,
                                       pTargetHD,
                                       stack);

                    // ... and replace the old UUID in the machine config with the one of
                    // the imported disk that was just created
                    Bstr hdId;
                    rc = pTargetHD->COMGETTER(Id)(hdId.asOutParam());
                    if (FAILED(rc)) DebugBreakThrow(rc);

                    d.uuid = hdId;

                    fFound = true;
                    break;
                }
            }

            // no disk with such a UUID found:
            if (!fFound)
                DebugBreakThrow(setError(E_FAIL,
                                         tr("<vbox:Machine> element in OVF contains a medium attachment for the disk image %s but the OVF describes no such image"),
                                         strUuid.c_str()));
        } // for (settings::AttachedDevicesList::const_iterator dit = sc.llAttachedDevices.begin();
    } // for (settings::StorageControllersList::const_iterator sit = config.storageMachine.llStorageControllers.begin();

    /*
     *
     * step 4): create the machine and have it import the config
     *
     */

    ComObjPtr<Machine> pNewMachine;
    rc = pNewMachine.createObject();
    if (FAILED(rc)) DebugBreakThrow(rc);

    // this magic constructor fills the new machine object with the MachineConfig
    // instance that we created from the vbox:Machine
    rc = pNewMachine->init(mVirtualBox,
                           stack.strNameVBox,       // name from OVF preparations; can be suffixed to avoid duplicates, or changed by user
                           config);                 // the whole machine config
    if (FAILED(rc)) DebugBreakThrow(rc);

    // return the new machine as an IMachine
    IMachine *p;
    rc = pNewMachine.queryInterfaceTo(&p);
    if (FAILED(rc)) DebugBreakThrow(rc);
    pReturnNewMachine = p;

    // and register it
    rc = mVirtualBox->RegisterMachine(pNewMachine);
    if (FAILED(rc)) DebugBreakThrow(rc);

    // store new machine for roll-back in case of errors
    Bstr bstrNewMachineId;
    rc = pNewMachine->COMGETTER(Id)(bstrNewMachineId.asOutParam());
    if (FAILED(rc)) DebugBreakThrow(rc);
    m->llGuidsMachinesCreated.push_back(Guid(bstrNewMachineId));
}

/**
 * Worker code for importing OVF from the cloud. This is called from Appliance::taskThreadImportOrExport()
 * in S3 mode and therefore runs on the OVF import worker thread. This then starts a second worker
 * thread to import from temporary files (see Appliance::importFS()).
 * @param pTask
 * @return
 */
HRESULT Appliance::importS3(TaskOVF *pTask)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    int vrc = VINF_SUCCESS;
    RTS3 hS3 = NIL_RTS3;
    char szOSTmpDir[RTPATH_MAX];
    RTPathTemp(szOSTmpDir, sizeof(szOSTmpDir));
    /* The template for the temporary directory created below */
    char *pszTmpDir;
    RTStrAPrintf(&pszTmpDir, "%s"RTPATH_SLASH_STR"vbox-ovf-XXXXXX", szOSTmpDir);
    list< pair<Utf8Str, ULONG> > filesList;

    HRESULT rc = S_OK;
    try
    {
        /* Extract the bucket */
        Utf8Str tmpPath = pTask->locInfo.strPath;
        Utf8Str bucket;
        parseBucket(tmpPath, bucket);

        /* We need a temporary directory which we can put the all disk images
         * in */
        vrc = RTDirCreateTemp(pszTmpDir);
        if (RT_FAILURE(vrc))
            DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                     tr("Cannot create temporary directory '%s' (%Rrc)"), pszTmpDir, vrc));

        /* Add every disks of every virtual system to an internal list */
        list< ComObjPtr<VirtualSystemDescription> >::const_iterator it;
        for (it = m->virtualSystemDescriptions.begin();
             it != m->virtualSystemDescriptions.end();
             ++it)
        {
            ComObjPtr<VirtualSystemDescription> vsdescThis = (*it);
            std::list<VirtualSystemDescriptionEntry*> avsdeHDs = vsdescThis->findByType(VirtualSystemDescriptionType_HardDiskImage);
            std::list<VirtualSystemDescriptionEntry*>::const_iterator itH;
            for (itH = avsdeHDs.begin();
                 itH != avsdeHDs.end();
                 ++itH)
            {
                const Utf8Str &strTargetFile = (*itH)->strOvf;
                if (!strTargetFile.isEmpty())
                {
                    /* The temporary name of the target disk file */
                    Utf8StrFmt strTmpDisk("%s/%s", pszTmpDir, RTPathFilename(strTargetFile.c_str()));
                    filesList.push_back(pair<Utf8Str, ULONG>(strTmpDisk, (*itH)->ulSizeMB));
                }
            }
        }

        /* Next we have to download the disk images */
        vrc = RTS3Create(&hS3, pTask->locInfo.strUsername.c_str(), pTask->locInfo.strPassword.c_str(), pTask->locInfo.strHostname.c_str(), "virtualbox-agent/"VBOX_VERSION_STRING);
        if (RT_FAILURE(vrc))
            DebugBreakThrow(setError(VBOX_E_IPRT_ERROR,
                                     tr("Cannot create S3 service handler")));
        RTS3SetProgressCallback(hS3, pTask->updateProgress, &pTask);

        /* Download all files */
        for (list< pair<Utf8Str, ULONG> >::const_iterator it1 = filesList.begin(); it1 != filesList.end(); ++it1)
        {
            const pair<Utf8Str, ULONG> &s = (*it1);
            const Utf8Str &strSrcFile = s.first;
            /* Construct the source file name */
            char *pszFilename = RTPathFilename(strSrcFile.c_str());
            /* Advance to the next operation */
            if (!pTask->pProgress.isNull())
                pTask->pProgress->SetNextOperation(BstrFmt(tr("Downloading file '%s'"), pszFilename), s.second);

            vrc = RTS3GetKey(hS3, bucket.c_str(), pszFilename, strSrcFile.c_str());
            if (RT_FAILURE(vrc))
            {
                if (vrc == VERR_S3_CANCELED)
                    throw S_OK; /* todo: !!!!!!!!!!!!! */
                else if (vrc == VERR_S3_ACCESS_DENIED)
                    DebugBreakThrow(setError(E_ACCESSDENIED,
                                             tr("Cannot download file '%s' from S3 storage server (Access denied). "
                                                "Make sure that your credentials are right. Also check that your host clock is properly synced"),
                                             pszFilename));
                else if (vrc == VERR_S3_NOT_FOUND)
                    DebugBreakThrow(setError(VBOX_E_FILE_ERROR,
                                             tr("Cannot download file '%s' from S3 storage server (File not found)"),
                                             pszFilename));
                else
                    DebugBreakThrow(setError(VBOX_E_IPRT_ERROR,
                                             tr("Cannot download file '%s' from S3 storage server (%Rrc)"),
                                             pszFilename, vrc));
            }
        }

        /* Provide a OVF file (haven't to exist) so the import routine can
         * figure out where the disk images/manifest file are located. */
        Utf8StrFmt strTmpOvf("%s/%s", pszTmpDir, RTPathFilename(tmpPath.c_str()));
        /* Now check if there is an manifest file. This is optional. */
        Utf8Str strManifestFile = manifestFileName(strTmpOvf);
        char *pszFilename = RTPathFilename(strManifestFile.c_str());
        if (!pTask->pProgress.isNull())
            pTask->pProgress->SetNextOperation(BstrFmt(tr("Downloading file '%s'"), pszFilename), 1);

        /* Try to download it. If the error is VERR_S3_NOT_FOUND, it isn't fatal. */
        vrc = RTS3GetKey(hS3, bucket.c_str(), pszFilename, strManifestFile.c_str());
        if (RT_SUCCESS(vrc))
            filesList.push_back(pair<Utf8Str, ULONG>(strManifestFile, 0));
        else if (RT_FAILURE(vrc))
        {
            if (vrc == VERR_S3_CANCELED)
                throw S_OK; /* todo: !!!!!!!!!!!!! */
            else if (vrc == VERR_S3_NOT_FOUND)
                vrc = VINF_SUCCESS; /* Not found is ok */
            else if (vrc == VERR_S3_ACCESS_DENIED)
                DebugBreakThrow(setError(E_ACCESSDENIED,
                                         tr("Cannot download file '%s' from S3 storage server (Access denied)."
                                            "Make sure that your credentials are right. Also check that your host clock is properly synced"),
                                         pszFilename));
            else
                DebugBreakThrow(setError(VBOX_E_IPRT_ERROR,
                                         tr("Cannot download file '%s' from S3 storage server (%Rrc)"),
                                         pszFilename, vrc));
        }

        /* Close the connection early */
        RTS3Destroy(hS3);
        hS3 = NIL_RTS3;

        pTask->pProgress->SetNextOperation(BstrFmt(tr("Importing appliance")), m->ulWeightForXmlOperation);

        ComObjPtr<Progress> progress;
        /* Import the whole temporary OVF & the disk images */
        LocationInfo li;
        li.strPath = strTmpOvf;
        rc = importImpl(li, progress);
        if (FAILED(rc)) DebugBreakThrow(rc);

        /* Unlock the appliance for the fs import thread */
        appLock.release();
        /* Wait until the import is done, but report the progress back to the
           caller */
        ComPtr<IProgress> progressInt(progress);
        waitForAsyncProgress(pTask->pProgress, progressInt); /* Any errors will be thrown */

        /* Again lock the appliance for the next steps */
        appLock.acquire();
    }
    catch(HRESULT aRC)
    {
        rc = aRC;
    }
    /* Cleanup */
    RTS3Destroy(hS3);
    /* Delete all files which where temporary created */
    for (list< pair<Utf8Str, ULONG> >::const_iterator it1 = filesList.begin(); it1 != filesList.end(); ++it1)
    {
        const char *pszFilePath = (*it1).first.c_str();
        if (RTPathExists(pszFilePath))
        {
            vrc = RTFileDelete(pszFilePath);
            if (RT_FAILURE(vrc))
                rc = setError(VBOX_E_FILE_ERROR,
                              tr("Cannot delete file '%s' (%Rrc)"), pszFilePath, vrc);
        }
    }
    /* Delete the temporary directory */
    if (RTPathExists(pszTmpDir))
    {
        vrc = RTDirRemove(pszTmpDir);
        if (RT_FAILURE(vrc))
            rc = setError(VBOX_E_FILE_ERROR,
                          tr("Cannot delete temporary directory '%s' (%Rrc)"), pszTmpDir, vrc);
    }
    if (pszTmpDir)
        RTStrFree(pszTmpDir);

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return rc;
}

