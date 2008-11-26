/* $Id$ */
/** @file
 * VBoxManage - VirtualBox's command-line interface.
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

#ifndef VBOX_ONLY_DOCS

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>

#include <VBox/com/VirtualBox.h>

#include <VBox/log.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/time.h>

#include "VBoxManage.h"
using namespace com;


int handleList(int argc, char *argv[],
               ComPtr<IVirtualBox> virtualBox, ComPtr<ISession> session)
{
    HRESULT rc = S_OK;

    /* exactly one option: the object */
    if (argc != 1)
        return errorSyntax(USAGE_LIST, "Incorrect number of parameters");

    /* which object? */
    if (strcmp(argv[0], "vms") == 0)
    {
        /*
         * Get the list of all registered VMs
         */
        com::SafeIfaceArray <IMachine> machines;
        rc = virtualBox->COMGETTER(Machines2)(ComSafeArrayAsOutParam (machines));
        if (SUCCEEDED(rc))
        {
            /*
             * Iterate through the collection
             */
            for (size_t i = 0; i < machines.size(); ++ i)
            {
                if (machines [i])
                    rc = showVMInfo(virtualBox, machines [i]);
            }
        }
    }
    else
    if (strcmp(argv[0], "runningvms") == 0)
    {
        /*
         * Get the list of all _running_ VMs
         */
        com::SafeIfaceArray <IMachine> machines;
        rc = virtualBox->COMGETTER(Machines2)(ComSafeArrayAsOutParam (machines));
        if (SUCCEEDED(rc))
        {
            /*
             * Iterate through the collection
             */
            for (size_t i = 0; i < machines.size(); ++ i)
            {
                if (machines [i])
                {
                    MachineState_T machineState;
                    rc = machines [i]->COMGETTER(State)(&machineState);
                    if (SUCCEEDED(rc))
                    {
                        switch (machineState)
                        {
                            case MachineState_Running:
                            case MachineState_Paused:
                                {
                                    Guid uuid;
                                    rc = machines [i]->COMGETTER(Id) (uuid.asOutParam());
                                    if (SUCCEEDED(rc))
                                        RTPrintf ("%s\n", uuid.toString().raw());
                                    break;
                                }
                        }
                    }
                }
            }
        }
    }
    else
    if (strcmp(argv[0], "ostypes") == 0)
    {
        ComPtr<IGuestOSTypeCollection> coll;
        ComPtr<IGuestOSTypeEnumerator> enumerator;
        CHECK_ERROR(virtualBox, COMGETTER(GuestOSTypes)(coll.asOutParam()));
        if (SUCCEEDED(rc) && coll)
        {
            CHECK_ERROR(coll, Enumerate(enumerator.asOutParam()));
            BOOL hasMore;
            while (SUCCEEDED(enumerator->HasMore(&hasMore)) && hasMore)
            {
                ComPtr<IGuestOSType> guestOS;
                CHECK_RC_BREAK(enumerator->GetNext(guestOS.asOutParam()));
                Bstr guestId;
                guestOS->COMGETTER(Id)(guestId.asOutParam());
                RTPrintf("ID:          %lS\n", guestId.raw());
                Bstr guestDescription;
                guestOS->COMGETTER(Description)(guestDescription.asOutParam());
                RTPrintf("Description: %lS\n\n", guestDescription.raw());
            }
        }
    }
    else
    if (strcmp(argv[0], "hostdvds") == 0)
    {
        ComPtr<IHost> host;
        CHECK_ERROR(virtualBox, COMGETTER(Host)(host.asOutParam()));
        ComPtr<IHostDVDDriveCollection> coll;
        ComPtr<IHostDVDDriveEnumerator> enumerator;
        CHECK_ERROR(host, COMGETTER(DVDDrives)(coll.asOutParam()));
        if (SUCCEEDED(rc) && coll)
        {
            CHECK_ERROR(coll, Enumerate(enumerator.asOutParam()));
            BOOL hasMore;
            while (SUCCEEDED(enumerator->HasMore(&hasMore)) && hasMore)
            {
                ComPtr<IHostDVDDrive> dvdDrive;
                CHECK_RC_BREAK(enumerator->GetNext(dvdDrive.asOutParam()));
                Bstr name;
                dvdDrive->COMGETTER(Name)(name.asOutParam());
                RTPrintf("Name:        %lS\n\n", name.raw());
            }
        }
    }
    else
    if (strcmp(argv[0], "hostfloppies") == 0)
    {
        ComPtr<IHost> host;
        CHECK_ERROR(virtualBox, COMGETTER(Host)(host.asOutParam()));
        ComPtr<IHostFloppyDriveCollection> coll;
        ComPtr<IHostFloppyDriveEnumerator> enumerator;
        CHECK_ERROR(host, COMGETTER(FloppyDrives)(coll.asOutParam()));
        if (SUCCEEDED(rc) && coll)
        {
            CHECK_ERROR(coll, Enumerate(enumerator.asOutParam()));
            BOOL hasMore;
            while (SUCCEEDED(enumerator->HasMore(&hasMore)) && hasMore)
            {
                ComPtr<IHostFloppyDrive> floppyDrive;
                CHECK_RC_BREAK(enumerator->GetNext(floppyDrive.asOutParam()));
                Bstr name;
                floppyDrive->COMGETTER(Name)(name.asOutParam());
                RTPrintf("Name:        %lS\n\n", name.raw());
            }
        }
    }
    else
    if (strcmp(argv[0], "hostifs") == 0)
    {
        ComPtr<IHost> host;
        CHECK_ERROR(virtualBox, COMGETTER(Host)(host.asOutParam()));
        ComPtr<IHostNetworkInterfaceCollection> coll;
        ComPtr<IHostNetworkInterfaceEnumerator> enumerator;
        CHECK_ERROR(host, COMGETTER(NetworkInterfaces)(coll.asOutParam()));
        if (SUCCEEDED(rc) && coll)
        {
            CHECK_ERROR(coll, Enumerate(enumerator.asOutParam()));
            BOOL hasMore;
            while (SUCCEEDED(enumerator->HasMore(&hasMore)) && hasMore)
            {
                ComPtr<IHostNetworkInterface> networkInterface;
                CHECK_RC_BREAK(enumerator->GetNext(networkInterface.asOutParam()));
                Bstr interfaceName;
                networkInterface->COMGETTER(Name)(interfaceName.asOutParam());
                RTPrintf("Name:        %lS\n", interfaceName.raw());
                Guid interfaceGuid;
                networkInterface->COMGETTER(Id)(interfaceGuid.asOutParam());
                RTPrintf("GUID:        %lS\n\n", Bstr(interfaceGuid.toString()).raw());
            }
        }
    }
    else
    if (strcmp(argv[0], "hostinfo") == 0)
    {
        ComPtr<IHost> Host;
        CHECK_ERROR (virtualBox, COMGETTER(Host)(Host.asOutParam()));

        RTPrintf("Host Information:\n\n");

        LONG64 uTCTime = 0;
        CHECK_ERROR (Host, COMGETTER(UTCTime)(&uTCTime));
        RTTIMESPEC timeSpec;
        RTTimeSpecSetMilli(&timeSpec, uTCTime);
        char pszTime[30] = {0};
        RTTimeSpecToString(&timeSpec, pszTime, sizeof(pszTime));
        RTPrintf("Host time: %s\n", pszTime);

        ULONG processorOnlineCount = 0;
        CHECK_ERROR (Host, COMGETTER(ProcessorOnlineCount)(&processorOnlineCount));
        RTPrintf("Processor online count: %lu\n", processorOnlineCount);
        ULONG processorCount = 0;
        CHECK_ERROR (Host, COMGETTER(ProcessorCount)(&processorCount));
        RTPrintf("Processor count: %lu\n", processorCount);
        ULONG processorSpeed = 0;
        Bstr processorDescription;
        for (ULONG i = 0; i < processorCount; i++)
        {
            CHECK_ERROR (Host, GetProcessorSpeed(i, &processorSpeed));
            if (processorSpeed)
                RTPrintf("Processor#%u speed: %lu MHz\n", i, processorSpeed);
            else
                RTPrintf("Processor#%u speed: unknown\n", i, processorSpeed);
    #if 0 /* not yet implemented in Main */
            CHECK_ERROR (Host, GetProcessorDescription(i, processorDescription.asOutParam()));
            RTPrintf("Processor#%u description: %lS\n", i, processorDescription.raw());
    #endif
        }

    #if 0 /* not yet implemented in Main */
        ULONG memorySize = 0;
        CHECK_ERROR (Host, COMGETTER(MemorySize)(&memorySize));
        RTPrintf("Memory size: %lu MByte\n", memorySize);

        ULONG memoryAvailable = 0;
        CHECK_ERROR (Host, COMGETTER(MemoryAvailable)(&memoryAvailable));
        RTPrintf("Memory available: %lu MByte\n", memoryAvailable);

        Bstr operatingSystem;
        CHECK_ERROR (Host, COMGETTER(OperatingSystem)(operatingSystem.asOutParam()));
        RTPrintf("Operating system: %lS\n", operatingSystem.raw());

        Bstr oSVersion;
        CHECK_ERROR (Host, COMGETTER(OSVersion)(oSVersion.asOutParam()));
        RTPrintf("Operating system version: %lS\n", oSVersion.raw());
    #endif
    }
    else
    if (strcmp(argv[0], "hddbackends") == 0)
    {
        ComPtr<ISystemProperties> systemProperties;
        CHECK_ERROR(virtualBox,
                    COMGETTER(SystemProperties) (systemProperties.asOutParam()));
        com::SafeIfaceArray <IHardDiskFormat> hardDiskFormats;
        CHECK_ERROR(systemProperties,
                    COMGETTER(HardDiskFormats) (ComSafeArrayAsOutParam (hardDiskFormats)));

        RTPrintf("Supported hard disk backends:\n\n");
        for (size_t i = 0; i < hardDiskFormats.size(); ++ i)
        {
            /* General information */
            Bstr id;
            CHECK_ERROR(hardDiskFormats [i],
                        COMGETTER(Id) (id.asOutParam()));

            Bstr description;
            CHECK_ERROR(hardDiskFormats [i],
                        COMGETTER(Id) (description.asOutParam()));

            ULONG caps;
            CHECK_ERROR(hardDiskFormats [i],
                        COMGETTER(Capabilities) (&caps));

            RTPrintf("Backend %u: id='%ls' description='%ls' capabilities=%#06x extensions='",
                     i, id.raw(), description.raw(), caps);

            /* File extensions */
            com::SafeArray <BSTR> fileExtensions;
            CHECK_ERROR(hardDiskFormats [i],
                        COMGETTER(FileExtensions) (ComSafeArrayAsOutParam (fileExtensions)));
            for (size_t a = 0; a < fileExtensions.size(); ++ a)
            {
                RTPrintf ("%ls", Bstr (fileExtensions [a]).raw());
                if (a != fileExtensions.size()-1)
                    RTPrintf (",");
            }
            RTPrintf ("'");

            /* Configuration keys */
            com::SafeArray <BSTR> propertyNames;
            com::SafeArray <BSTR> propertyDescriptions;
            com::SafeArray <DataType_T> propertyTypes;
            com::SafeArray <ULONG> propertyFlags;
            com::SafeArray <BSTR> propertyDefaults;
            CHECK_ERROR(hardDiskFormats [i],
                        DescribeProperties (ComSafeArrayAsOutParam (propertyNames),
                                            ComSafeArrayAsOutParam (propertyDescriptions),
                                            ComSafeArrayAsOutParam (propertyTypes),
                                            ComSafeArrayAsOutParam (propertyFlags),
                                            ComSafeArrayAsOutParam (propertyDefaults)));

            RTPrintf (" properties=(");
            if (propertyNames.size() > 0)
            {
                for (size_t a = 0; a < propertyNames.size(); ++ a)
                {
                    RTPrintf ("\n  name='%ls' desc='%ls' type=",
                              Bstr (propertyNames [a]).raw(), Bstr (propertyDescriptions [a]).raw());
                    switch (propertyTypes [a])
                    {
                        case DataType_Int32: RTPrintf ("int"); break;
                        case DataType_Int8: RTPrintf ("byte"); break;
                        case DataType_String: RTPrintf ("string"); break;
                    }
                    RTPrintf (" flags=%#04x", propertyFlags [a]);
                    RTPrintf (" default='%ls'", Bstr (propertyDefaults [a]).raw());
                    if (a != propertyNames.size()-1)
                        RTPrintf (", ");
                }
            }
            RTPrintf (")\n");
        }
    }
    else
    if (strcmp(argv[0], "hdds") == 0)
    {
        com::SafeIfaceArray <IHardDisk2> hdds;
        CHECK_ERROR(virtualBox, COMGETTER(HardDisks2)(ComSafeArrayAsOutParam (hdds)));
        for (size_t i = 0; i < hdds.size(); ++ i)
        {
            ComPtr<IHardDisk2> hdd = hdds[i];
            Guid uuid;
            hdd->COMGETTER(Id)(uuid.asOutParam());
            RTPrintf("UUID:         %s\n", uuid.toString().raw());
            Bstr format;
            hdd->COMGETTER(Format)(format.asOutParam());
            RTPrintf("Format:       %lS\n", format.raw());
            Bstr filepath;
            hdd->COMGETTER(Location)(filepath.asOutParam());
            RTPrintf("Location:     %lS\n", filepath.raw());
            MediaState_T enmState;
            /// @todo NEWMEDIA check accessibility of all parents
            /// @todo NEWMEDIA print the full state value
            hdd->COMGETTER(State)(&enmState);
            RTPrintf("Accessible:   %s\n", enmState != MediaState_Inaccessible ? "yes" : "no");
            com::SafeGUIDArray machineIds;
            hdd->COMGETTER(MachineIds)(ComSafeArrayAsOutParam(machineIds));
            for (size_t j = 0; j < machineIds.size(); ++ j)
            {
                ComPtr<IMachine> machine;
                CHECK_ERROR(virtualBox, GetMachine(machineIds[j], machine.asOutParam()));
                ASSERT(machine);
                Bstr name;
                machine->COMGETTER(Name)(name.asOutParam());
                machine->COMGETTER(Id)(uuid.asOutParam());
                RTPrintf("%s%lS (UUID: %RTuuid)\n",
                         j == 0 ? "Usage:        " : "              ",
                         name.raw(), &machineIds[j]);
            }
            /// @todo NEWMEDIA check usage in snapshots too
            /// @todo NEWMEDIA also list children and say 'differencing' for
            /// hard disks with the parent or 'base' otherwise.
            RTPrintf("\n");
        }
    }
    else
    if (strcmp(argv[0], "dvds") == 0)
    {
        com::SafeIfaceArray<IDVDImage2> dvds;
        CHECK_ERROR(virtualBox, COMGETTER(DVDImages)(ComSafeArrayAsOutParam(dvds)));
        for (size_t i = 0; i < dvds.size(); ++ i)
        {
            ComPtr<IDVDImage2> dvdImage = dvds[i];
            Guid uuid;
            dvdImage->COMGETTER(Id)(uuid.asOutParam());
            RTPrintf("UUID:       %s\n", uuid.toString().raw());
            Bstr filePath;
            dvdImage->COMGETTER(Location)(filePath.asOutParam());
            RTPrintf("Path:       %lS\n", filePath.raw());
            MediaState_T enmState;
            dvdImage->COMGETTER(State)(&enmState);
            RTPrintf("Accessible: %s\n", enmState != MediaState_Inaccessible ? "yes" : "no");
            /** @todo usage */
            RTPrintf("\n");
        }
    }
    else
    if (strcmp(argv[0], "floppies") == 0)
    {
        com::SafeIfaceArray<IFloppyImage2> floppies;
        CHECK_ERROR(virtualBox, COMGETTER(FloppyImages)(ComSafeArrayAsOutParam(floppies)));
        for (size_t i = 0; i < floppies.size(); ++ i)
        {
            ComPtr<IFloppyImage2> floppyImage = floppies[i];
            Guid uuid;
            floppyImage->COMGETTER(Id)(uuid.asOutParam());
            RTPrintf("UUID:       %s\n", uuid.toString().raw());
            Bstr filePath;
            floppyImage->COMGETTER(Location)(filePath.asOutParam());
            RTPrintf("Path:       %lS\n", filePath.raw());
            MediaState_T enmState;
            floppyImage->COMGETTER(State)(&enmState);
            RTPrintf("Accessible: %s\n", enmState != MediaState_Inaccessible ? "yes" : "no");
            /** @todo usage */
            RTPrintf("\n");
        }
    }
    else
    if (strcmp(argv[0], "usbhost") == 0)
    {
        ComPtr<IHost> Host;
        CHECK_ERROR_RET (virtualBox, COMGETTER(Host)(Host.asOutParam()), 1);

        ComPtr<IHostUSBDeviceCollection> CollPtr;
        CHECK_ERROR_RET (Host, COMGETTER(USBDevices)(CollPtr.asOutParam()), 1);

        ComPtr<IHostUSBDeviceEnumerator> EnumPtr;
        CHECK_ERROR_RET (CollPtr, Enumerate(EnumPtr.asOutParam()), 1);

        RTPrintf("Host USB Devices:\n\n");

        BOOL fMore = FALSE;
        rc = EnumPtr->HasMore (&fMore);
        ASSERT_RET (SUCCEEDED (rc), 1);

        if (!fMore)
        {
            RTPrintf("<none>\n\n");
        }
        else
        while (fMore)
        {
            ComPtr <IHostUSBDevice> dev;
            rc = EnumPtr->GetNext (dev.asOutParam());
            ASSERT_RET (SUCCEEDED (rc), 1);

            /* Query info. */
            Guid id;
            CHECK_ERROR_RET (dev, COMGETTER(Id)(id.asOutParam()), 1);
            USHORT usVendorId;
            CHECK_ERROR_RET (dev, COMGETTER(VendorId)(&usVendorId), 1);
            USHORT usProductId;
            CHECK_ERROR_RET (dev, COMGETTER(ProductId)(&usProductId), 1);
            USHORT bcdRevision;
            CHECK_ERROR_RET (dev, COMGETTER(Revision)(&bcdRevision), 1);

            RTPrintf("UUID:               %S\n"
                     "VendorId:           0x%04x (%04X)\n"
                     "ProductId:          0x%04x (%04X)\n"
                     "Revision:           %u.%u (%02u%02u)\n",
                     id.toString().raw(),
                     usVendorId, usVendorId, usProductId, usProductId,
                     bcdRevision >> 8, bcdRevision & 0xff,
                     bcdRevision >> 8, bcdRevision & 0xff);

            /* optional stuff. */
            Bstr bstr;
            CHECK_ERROR_RET (dev, COMGETTER(Manufacturer)(bstr.asOutParam()), 1);
            if (!bstr.isEmpty())
                RTPrintf("Manufacturer:       %lS\n", bstr.raw());
            CHECK_ERROR_RET (dev, COMGETTER(Product)(bstr.asOutParam()), 1);
            if (!bstr.isEmpty())
                RTPrintf("Product:            %lS\n", bstr.raw());
            CHECK_ERROR_RET (dev, COMGETTER(SerialNumber)(bstr.asOutParam()), 1);
            if (!bstr.isEmpty())
                RTPrintf("SerialNumber:       %lS\n", bstr.raw());
            CHECK_ERROR_RET (dev, COMGETTER(Address)(bstr.asOutParam()), 1);
            if (!bstr.isEmpty())
                RTPrintf("Address:            %lS\n", bstr.raw());

            /* current state  */
            USBDeviceState_T state;
            CHECK_ERROR_RET (dev, COMGETTER(State)(&state), 1);
            const char *pszState = "?";
            switch (state)
            {
                case USBDeviceState_NotSupported:
                    pszState = "Not supported"; break;
                case USBDeviceState_Unavailable:
                    pszState = "Unavailable"; break;
                case USBDeviceState_Busy:
                    pszState = "Busy"; break;
                case USBDeviceState_Available:
                    pszState = "Available"; break;
                case USBDeviceState_Held:
                    pszState = "Held"; break;
                case USBDeviceState_Captured:
                    pszState = "Captured"; break;
                default:
                    ASSERT (false);
                    break;
            }
            RTPrintf("Current State:      %s\n\n", pszState);

            rc = EnumPtr->HasMore (&fMore);
            ASSERT_RET (SUCCEEDED (rc), rc);
        }
    }
    else
    if (strcmp(argv[0], "usbfilters") == 0)
    {
        RTPrintf("Global USB Device Filters:\n\n");

        ComPtr <IHost> host;
        CHECK_ERROR_RET (virtualBox, COMGETTER(Host) (host.asOutParam()), 1);

        ComPtr<IHostUSBDeviceFilterCollection> coll;
        CHECK_ERROR_RET (host, COMGETTER (USBDeviceFilters)(coll.asOutParam()), 1);

        ComPtr<IHostUSBDeviceFilterEnumerator> en;
        CHECK_ERROR_RET (coll, Enumerate(en.asOutParam()), 1);

        ULONG index = 0;
        BOOL more = FALSE;
        rc = en->HasMore (&more);
        ASSERT_RET (SUCCEEDED (rc), 1);

        if (!more)
        {
            RTPrintf("<none>\n\n");
        }
        else
        while (more)
        {
            ComPtr<IHostUSBDeviceFilter> flt;
            rc = en->GetNext (flt.asOutParam());
            ASSERT_RET (SUCCEEDED (rc), 1);

            /* Query info. */

            RTPrintf("Index:            %lu\n", index);

            BOOL active = FALSE;
            CHECK_ERROR_RET (flt, COMGETTER (Active) (&active), 1);
            RTPrintf("Active:           %s\n", active ? "yes" : "no");

            USBDeviceFilterAction_T action;
            CHECK_ERROR_RET (flt, COMGETTER (Action) (&action), 1);
            const char *pszAction = "<invalid>";
            switch (action)
            {
                case USBDeviceFilterAction_Ignore:
                    pszAction = "Ignore";
                    break;
                case USBDeviceFilterAction_Hold:
                    pszAction = "Hold";
                    break;
                default:
                    break;
            }
            RTPrintf("Action:           %s\n", pszAction);

            Bstr bstr;
            CHECK_ERROR_RET (flt, COMGETTER (Name) (bstr.asOutParam()), 1);
            RTPrintf("Name:             %lS\n", bstr.raw());
            CHECK_ERROR_RET (flt, COMGETTER (VendorId) (bstr.asOutParam()), 1);
            RTPrintf("VendorId:         %lS\n", bstr.raw());
            CHECK_ERROR_RET (flt, COMGETTER (ProductId) (bstr.asOutParam()), 1);
            RTPrintf("ProductId:        %lS\n", bstr.raw());
            CHECK_ERROR_RET (flt, COMGETTER (Revision) (bstr.asOutParam()), 1);
            RTPrintf("Revision:         %lS\n", bstr.raw());
            CHECK_ERROR_RET (flt, COMGETTER (Manufacturer) (bstr.asOutParam()), 1);
            RTPrintf("Manufacturer:     %lS\n", bstr.raw());
            CHECK_ERROR_RET (flt, COMGETTER (Product) (bstr.asOutParam()), 1);
            RTPrintf("Product:          %lS\n", bstr.raw());
            CHECK_ERROR_RET (flt, COMGETTER (SerialNumber) (bstr.asOutParam()), 1);
            RTPrintf("Serial Number:    %lS\n\n", bstr.raw());

            rc = en->HasMore (&more);
            ASSERT_RET (SUCCEEDED (rc), 1);

            index ++;
        }
    }
    else if (strcmp(argv[0], "systemproperties") == 0)
    {
        ComPtr<ISystemProperties> systemProperties;
        virtualBox->COMGETTER(SystemProperties)(systemProperties.asOutParam());

        Bstr str;
        ULONG ulValue;
        ULONG64 ul64Value;
        BOOL flag;

        systemProperties->COMGETTER(MinGuestRAM)(&ulValue);
        RTPrintf("Minimum guest RAM size:      %u Megabytes\n", ulValue);
        systemProperties->COMGETTER(MaxGuestRAM)(&ulValue);
        RTPrintf("Maximum guest RAM size:      %u Megabytes\n", ulValue);
        systemProperties->COMGETTER(MaxGuestVRAM)(&ulValue);
        RTPrintf("Maximum video RAM size:      %u Megabytes\n", ulValue);
        systemProperties->COMGETTER(MaxVDISize)(&ul64Value);
        RTPrintf("Maximum VDI size:            %lu Megabytes\n", ul64Value);
        systemProperties->COMGETTER(DefaultHardDiskFolder)(str.asOutParam());
        RTPrintf("Default hard disk folder:    %lS\n", str.raw());
        systemProperties->COMGETTER(DefaultMachineFolder)(str.asOutParam());
        RTPrintf("Default machine folder:      %lS\n", str.raw());
        systemProperties->COMGETTER(RemoteDisplayAuthLibrary)(str.asOutParam());
        RTPrintf("VRDP authentication library: %lS\n", str.raw());
        systemProperties->COMGETTER(WebServiceAuthLibrary)(str.asOutParam());
        RTPrintf("Webservice auth. library:    %lS\n", str.raw());
        systemProperties->COMGETTER(HWVirtExEnabled)(&flag);
        RTPrintf("Hardware virt. extensions:   %s\n", flag ? "yes" : "no");
        systemProperties->COMGETTER(LogHistoryCount)(&ulValue);
        RTPrintf("Log history count:           %u\n", ulValue);

    }
    else
        return errorSyntax(USAGE_LIST, "Invalid parameter '%s'", Utf8Str(argv[0]).raw());

    return SUCCEEDED(rc) ? 0 : 1;
}

#endif /* !VBOX_ONLY_DOCS */

