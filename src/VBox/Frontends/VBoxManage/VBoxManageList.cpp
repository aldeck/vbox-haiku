/* $Id$ */
/** @file
 * VBoxManage - The 'list' command.
 */

/*
 * Copyright (C) 2006-2009 Sun Microsystems, Inc.
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
#include <VBox/com/errorprint.h>

#include <VBox/com/VirtualBox.h>

#include <VBox/log.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/getopt.h>
#include <iprt/ctype.h>

#include "VBoxManage.h"
using namespace com;

#ifdef VBOX_WITH_HOSTNETIF_API
static const char *getHostIfMediumTypeText(HostNetworkInterfaceMediumType_T enmType)
{
    switch (enmType)
    {
        case HostNetworkInterfaceMediumType_Ethernet: return "Ethernet";
        case HostNetworkInterfaceMediumType_PPP: return "PPP";
        case HostNetworkInterfaceMediumType_SLIP: return "SLIP";
    }
    return "Unknown";
}

static const char *getHostIfStatusText(HostNetworkInterfaceStatus_T enmStatus)
{
    switch (enmStatus)
    {
        case HostNetworkInterfaceStatus_Up: return "Up";
        case HostNetworkInterfaceStatus_Down: return "Down";
    }
    return "Unknown";
}
#endif

enum enOptionCodes
{
    LISTVMS = 1000,
    LISTRUNNINGVMS,
    LISTOSTYPES,
    LISTHOSTDVDS,
    LISTHOSTFLOPPIES,
    LISTBRIDGEDIFS,
#if defined(VBOX_WITH_NETFLT)
    LISTHOSTONLYIFS,
#endif
    LISTHOSTCPUIDS,
    LISTHOSTINFO,
    LISTHDDBACKENDS,
    LISTHDDS,
    LISTDVDS,
    LISTFLOPPIES,
    LISTUSBHOST,
    LISTUSBFILTERS,
    LISTSYSTEMPROPERTIES,
    LISTDHCPSERVERS
};

static const RTGETOPTDEF g_aListOptions[]
    = {
        { "--long",             'l',                    RTGETOPT_REQ_NOTHING },
        { "vms",                LISTVMS,                RTGETOPT_REQ_NOTHING },
        { "runningvms",         LISTRUNNINGVMS,         RTGETOPT_REQ_NOTHING },
        { "ostypes",            LISTOSTYPES,            RTGETOPT_REQ_NOTHING },
        { "hostdvds",           LISTHOSTDVDS,           RTGETOPT_REQ_NOTHING },
        { "hostfloppies",       LISTHOSTFLOPPIES,       RTGETOPT_REQ_NOTHING },
        { "hostifs",            LISTBRIDGEDIFS,         RTGETOPT_REQ_NOTHING }, /* backward compatibility */
        { "bridgedifs",         LISTBRIDGEDIFS,         RTGETOPT_REQ_NOTHING },
#if defined(VBOX_WITH_NETFLT)
        { "hostonlyifs",        LISTHOSTONLYIFS,        RTGETOPT_REQ_NOTHING },
#endif
        { "hostinfo",           LISTHOSTINFO,           RTGETOPT_REQ_NOTHING },
        { "hostcpuids",         LISTHOSTCPUIDS,         RTGETOPT_REQ_NOTHING },
        { "hddbackends",        LISTHDDBACKENDS,        RTGETOPT_REQ_NOTHING },
        { "hdds",               LISTHDDS,               RTGETOPT_REQ_NOTHING },
        { "dvds",               LISTDVDS,               RTGETOPT_REQ_NOTHING },
        { "floppies",           LISTFLOPPIES,           RTGETOPT_REQ_NOTHING },
        { "usbhost",            LISTUSBHOST,            RTGETOPT_REQ_NOTHING },
        { "usbfilters",         LISTUSBFILTERS,         RTGETOPT_REQ_NOTHING },
        { "systemproperties",   LISTSYSTEMPROPERTIES,   RTGETOPT_REQ_NOTHING },
        { "dhcpservers",        LISTDHCPSERVERS,        RTGETOPT_REQ_NOTHING }
      };

int handleList(HandlerArg *a)
{
    HRESULT rc = S_OK;

    bool fOptLong = false;

    int command = 0;
    int c;

    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, g_aListOptions, RT_ELEMENTS(g_aListOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'l':  // --long
                fOptLong = true;
            break;

            case LISTVMS:
            case LISTRUNNINGVMS:
            case LISTOSTYPES:
            case LISTHOSTDVDS:
            case LISTHOSTFLOPPIES:
            case LISTBRIDGEDIFS:
#if defined(VBOX_WITH_NETFLT)
            case LISTHOSTONLYIFS:
#endif
            case LISTHOSTINFO:
            case LISTHOSTCPUIDS:
            case LISTHDDBACKENDS:
            case LISTHDDS:
            case LISTDVDS:
            case LISTFLOPPIES:
            case LISTUSBHOST:
            case LISTUSBFILTERS:
            case LISTSYSTEMPROPERTIES:
            case LISTDHCPSERVERS:
                if (command)
                    return errorSyntax(USAGE_LIST, "Too many subcommands for \"list\" command.\n");

                command = c;
            break;

            case VINF_GETOPT_NOT_OPTION:
                return errorSyntax(USAGE_LIST, "Unknown subcommand \"%s\".", ValueUnion.psz);
            break;

            default:
                if (c > 0)
                {
                    if (RT_C_IS_GRAPH(c))
                        return errorSyntax(USAGE_LIST, "unhandled option: -%c", c);
                    else
                        return errorSyntax(USAGE_LIST, "unhandled option: %i", c);
                }
                else if (c == VERR_GETOPT_UNKNOWN_OPTION)
                    return errorSyntax(USAGE_LIST, "unknown option: %s", ValueUnion.psz);
                else if (ValueUnion.pDef)
                    return errorSyntax(USAGE_LIST, "%s: %Rrs", ValueUnion.pDef->pszLong, c);
                else
                    return errorSyntax(USAGE_LIST, "%Rrs", c);
        }
    }

    if (!command)
        return errorSyntax(USAGE_LIST, "Missing subcommand for \"list\" command.\n");

    /* which object? */
    switch (command)
    {
        case LISTVMS:
        {
            /*
             * Get the list of all registered VMs
             */
            com::SafeIfaceArray <IMachine> machines;
            rc = a->virtualBox->COMGETTER(Machines)(ComSafeArrayAsOutParam (machines));
            if (SUCCEEDED(rc))
            {
                /*
                 * Iterate through the collection
                 */
                for (size_t i = 0; i < machines.size(); ++ i)
                {
                    if (machines[i])
                        rc = showVMInfo(a->virtualBox,
                                        machines[i],
                                        (fOptLong) ? VMINFO_STANDARD : VMINFO_COMPACT);
                }
            }
        }
        break;

        case LISTRUNNINGVMS:
        {
            /*
             * Get the list of all _running_ VMs
             */
            com::SafeIfaceArray <IMachine> machines;
            rc = a->virtualBox->COMGETTER(Machines)(ComSafeArrayAsOutParam (machines));
            if (SUCCEEDED(rc))
            {
                /*
                 * Iterate through the collection
                 */
                for (size_t i = 0; i < machines.size(); ++ i)
                {
                    if (machines[i])
                    {
                        MachineState_T machineState;
                        rc = machines [i]->COMGETTER(State)(&machineState);
                        if (SUCCEEDED(rc))
                        {
                            switch (machineState)
                            {
                                case MachineState_Running:
                                case MachineState_Teleporting:
                                case MachineState_LiveSnapshotting:
                                case MachineState_Paused:
                                case MachineState_TeleportingPausedVM:
                                    rc = showVMInfo(a->virtualBox,
                                                    machines[i],
                                                    (fOptLong) ? VMINFO_STANDARD : VMINFO_COMPACT);
                            }
                        }
                    }
                }
            }
        }
        break;

        case LISTOSTYPES:
        {
            com::SafeIfaceArray <IGuestOSType> coll;
            rc = a->virtualBox->COMGETTER(GuestOSTypes)(ComSafeArrayAsOutParam(coll));
            if (SUCCEEDED(rc))
            {
                /*
                 * Iterate through the collection.
                 */
                for (size_t i = 0; i < coll.size(); ++ i)
                {
                    ComPtr<IGuestOSType> guestOS;
                    guestOS = coll[i];
                    Bstr guestId;
                    guestOS->COMGETTER(Id)(guestId.asOutParam());
                    RTPrintf("ID:          %lS\n", guestId.raw());
                    Bstr guestDescription;
                    guestOS->COMGETTER(Description)(guestDescription.asOutParam());
                    RTPrintf("Description: %lS\n\n", guestDescription.raw());
                }
            }
        }
        break;

        case LISTHOSTDVDS:
        {
            ComPtr<IHost> host;
            CHECK_ERROR(a->virtualBox, COMGETTER(Host)(host.asOutParam()));
            com::SafeIfaceArray <IMedium> coll;
            CHECK_ERROR(host, COMGETTER(DVDDrives)(ComSafeArrayAsOutParam(coll)));
            if (SUCCEEDED(rc))
            {
                for (size_t i = 0; i < coll.size(); ++ i)
                {
                    ComPtr<IMedium> dvdDrive = coll[i];
                    Bstr uuid;
                    dvdDrive->COMGETTER(Id)(uuid.asOutParam());
                    RTPrintf("UUID:         %s\n", Utf8Str(uuid).raw());
                    Bstr name;
                    dvdDrive->COMGETTER(Name)(name.asOutParam());
                    RTPrintf("Name:         %lS\n\n", name.raw());
                }
            }
        }
        break;

        case LISTHOSTFLOPPIES:
        {
            ComPtr<IHost> host;
            CHECK_ERROR(a->virtualBox, COMGETTER(Host)(host.asOutParam()));
            com::SafeIfaceArray <IMedium> coll;
            CHECK_ERROR(host, COMGETTER(FloppyDrives)(ComSafeArrayAsOutParam(coll)));
            if (SUCCEEDED(rc))
            {
                for (size_t i = 0; i < coll.size(); ++i)
                {
                    ComPtr<IMedium> floppyDrive = coll[i];
                    Bstr uuid;
                    floppyDrive->COMGETTER(Id)(uuid.asOutParam());
                    RTPrintf("UUID:         %s\n", Utf8Str(uuid).raw());
                    Bstr name;
                    floppyDrive->COMGETTER(Name)(name.asOutParam());
                    RTPrintf("Name:         %lS\n\n", name.raw());
                }
            }
        }
        break;

        case LISTBRIDGEDIFS:
#if defined(VBOX_WITH_NETFLT)
        case LISTHOSTONLYIFS:
#endif
        {
            ComPtr<IHost> host;
            CHECK_ERROR(a->virtualBox, COMGETTER(Host)(host.asOutParam()));
            com::SafeIfaceArray <IHostNetworkInterface> hostNetworkInterfaces;
#if defined(VBOX_WITH_NETFLT)
            if (command == LISTBRIDGEDIFS)
                CHECK_ERROR(host, FindHostNetworkInterfacesOfType(HostNetworkInterfaceType_Bridged,
                                                                  ComSafeArrayAsOutParam(hostNetworkInterfaces)));
            else
                CHECK_ERROR(host, FindHostNetworkInterfacesOfType(HostNetworkInterfaceType_HostOnly,
                                                                  ComSafeArrayAsOutParam(hostNetworkInterfaces)));
#else
            CHECK_ERROR(host, COMGETTER(NetworkInterfaces)(ComSafeArrayAsOutParam(hostNetworkInterfaces)));
#endif
            for (size_t i = 0; i < hostNetworkInterfaces.size(); ++i)
            {
                ComPtr<IHostNetworkInterface> networkInterface = hostNetworkInterfaces[i];
#ifndef VBOX_WITH_HOSTNETIF_API
                Bstr interfaceName;
                networkInterface->COMGETTER(Name)(interfaceName.asOutParam());
                RTPrintf("Name:        %lS\n", interfaceName.raw());
                Guid interfaceGuid;
                networkInterface->COMGETTER(Id)(interfaceGuid.asOutParam());
                RTPrintf("GUID:        %lS\n\n", Bstr(interfaceGuid.toString()).raw());
#else /* VBOX_WITH_HOSTNETIF_API */
                Bstr interfaceName;
                networkInterface->COMGETTER(Name)(interfaceName.asOutParam());
                RTPrintf("Name:            %lS\n", interfaceName.raw());
                Bstr interfaceGuid;
                networkInterface->COMGETTER(Id)(interfaceGuid.asOutParam());
                RTPrintf("GUID:            %lS\n", interfaceGuid.raw());
                BOOL bDhcpEnabled;
                networkInterface->COMGETTER(DhcpEnabled)(&bDhcpEnabled);
                RTPrintf("Dhcp:            %s\n", bDhcpEnabled ? "Enabled" : "Disabled");

                Bstr IPAddress;
                networkInterface->COMGETTER(IPAddress)(IPAddress.asOutParam());
                RTPrintf("IPAddress:       %lS\n", IPAddress.raw());
                Bstr NetworkMask;
                networkInterface->COMGETTER(NetworkMask)(NetworkMask.asOutParam());
                RTPrintf("NetworkMask:     %lS\n", NetworkMask.raw());
                Bstr IPV6Address;
                networkInterface->COMGETTER(IPV6Address)(IPV6Address.asOutParam());
                RTPrintf("IPV6Address:     %lS\n", IPV6Address.raw());
                ULONG IPV6NetworkMaskPrefixLength;
                networkInterface->COMGETTER(IPV6NetworkMaskPrefixLength)(&IPV6NetworkMaskPrefixLength);
                RTPrintf("IPV6NetworkMaskPrefixLength: %d\n", IPV6NetworkMaskPrefixLength);
                Bstr HardwareAddress;
                networkInterface->COMGETTER(HardwareAddress)(HardwareAddress.asOutParam());
                RTPrintf("HardwareAddress: %lS\n", HardwareAddress.raw());
                HostNetworkInterfaceMediumType_T Type;
                networkInterface->COMGETTER(MediumType)(&Type);
                RTPrintf("MediumType:      %s\n", getHostIfMediumTypeText(Type));
                HostNetworkInterfaceStatus_T Status;
                networkInterface->COMGETTER(Status)(&Status);
                RTPrintf("Status:          %s\n", getHostIfStatusText(Status));
                Bstr netName;
                networkInterface->COMGETTER(NetworkName)(netName.asOutParam());
                RTPrintf("VBoxNetworkName: %lS\n\n", netName.raw());

#endif
            }
        }
        break;

        case LISTHOSTINFO:
        {
            ComPtr<IHost> Host;
            CHECK_ERROR (a->virtualBox, COMGETTER(Host)(Host.asOutParam()));

            RTPrintf("Host Information:\n\n");

            LONG64 uTCTime = 0;
            CHECK_ERROR (Host, COMGETTER(UTCTime)(&uTCTime));
            RTTIMESPEC timeSpec;
            RTTimeSpecSetMilli(&timeSpec, uTCTime);
            char szTime[32] = {0};
            RTTimeSpecToString(&timeSpec, szTime, sizeof(szTime));
            RTPrintf("Host time: %s\n", szTime);

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
                CHECK_ERROR (Host, GetProcessorDescription(i, processorDescription.asOutParam()));
                RTPrintf("Processor#%u description: %lS\n", i, processorDescription.raw());
            }

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
        }
        break;

        case LISTHOSTCPUIDS:
        {
            ComPtr<IHost> Host;
            CHECK_ERROR(a->virtualBox, COMGETTER(Host)(Host.asOutParam()));

            RTPrintf("Host CPUIDs:\n\nLeaf no.  EAX      EBX      ECX      EDX\n");
            ULONG uCpuNo = 0; /* ASSUMES that CPU#0 is online. */
            static uint32_t const s_auCpuIdRanges[] =
            {
                UINT32_C(0x00000000), UINT32_C(0x0000007f),
                UINT32_C(0x80000000), UINT32_C(0x8000007f),
                UINT32_C(0xc0000000), UINT32_C(0xc000007f)
            };
            for (unsigned i = 0; i < RT_ELEMENTS(s_auCpuIdRanges); i += 2)
            {
                ULONG uEAX, uEBX, uECX, uEDX, cLeafs;
                CHECK_ERROR(Host, GetProcessorCpuIdLeaf(uCpuNo, s_auCpuIdRanges[i], 0, &cLeafs, &uEBX, &uECX, &uEDX));
                if (cLeafs < s_auCpuIdRanges[i] || cLeafs > s_auCpuIdRanges[i+1])
                    continue;
                cLeafs++;
                for (ULONG iLeaf = s_auCpuIdRanges[i]; iLeaf <= cLeafs; iLeaf++)
                {
                    CHECK_ERROR(Host, GetProcessorCpuIdLeaf(uCpuNo, iLeaf, 0, &uEAX, &uEBX, &uECX, &uEDX));
                    RTPrintf("%08x  %08x %08x %08x %08x\n", iLeaf, uEAX, uEBX, uECX, uEDX);
                }
            }
        }
        break;

        case LISTHDDBACKENDS:
        {
            ComPtr<ISystemProperties> systemProperties;
            CHECK_ERROR(a->virtualBox,
                        COMGETTER(SystemProperties) (systemProperties.asOutParam()));
            com::SafeIfaceArray <IMediumFormat> mediumFormats;
            CHECK_ERROR(systemProperties,
                        COMGETTER(MediumFormats) (ComSafeArrayAsOutParam (mediumFormats)));

            RTPrintf("Supported hard disk backends:\n\n");
            for (size_t i = 0; i < mediumFormats.size(); ++ i)
            {
                /* General information */
                Bstr id;
                CHECK_ERROR(mediumFormats [i],
                            COMGETTER(Id) (id.asOutParam()));

                Bstr description;
                CHECK_ERROR(mediumFormats [i],
                            COMGETTER(Id) (description.asOutParam()));

                ULONG caps;
                CHECK_ERROR(mediumFormats [i],
                            COMGETTER(Capabilities) (&caps));

                RTPrintf("Backend %u: id='%ls' description='%ls' capabilities=%#06x extensions='",
                        i, id.raw(), description.raw(), caps);

                /* File extensions */
                com::SafeArray <BSTR> fileExtensions;
                CHECK_ERROR(mediumFormats [i],
                            COMGETTER(FileExtensions) (ComSafeArrayAsOutParam (fileExtensions)));
                for (size_t j = 0; j < fileExtensions.size(); ++ j)
                {
                    RTPrintf ("%ls", Bstr (fileExtensions [j]).raw());
                    if (j != fileExtensions.size()-1)
                        RTPrintf (",");
                }
                RTPrintf ("'");

                /* Configuration keys */
                com::SafeArray <BSTR> propertyNames;
                com::SafeArray <BSTR> propertyDescriptions;
                com::SafeArray <DataType_T> propertyTypes;
                com::SafeArray <ULONG> propertyFlags;
                com::SafeArray <BSTR> propertyDefaults;
                CHECK_ERROR(mediumFormats [i],
                            DescribeProperties (ComSafeArrayAsOutParam (propertyNames),
                                                ComSafeArrayAsOutParam (propertyDescriptions),
                                                ComSafeArrayAsOutParam (propertyTypes),
                                                ComSafeArrayAsOutParam (propertyFlags),
                                                ComSafeArrayAsOutParam (propertyDefaults)));

                RTPrintf (" properties=(");
                if (propertyNames.size() > 0)
                {
                    for (size_t j = 0; j < propertyNames.size(); ++ j)
                    {
                        RTPrintf ("\n  name='%ls' desc='%ls' type=",
                                Bstr (propertyNames [j]).raw(), Bstr (propertyDescriptions [j]).raw());
                        switch (propertyTypes [j])
                        {
                            case DataType_Int32: RTPrintf ("int"); break;
                            case DataType_Int8: RTPrintf ("byte"); break;
                            case DataType_String: RTPrintf ("string"); break;
                        }
                        RTPrintf (" flags=%#04x", propertyFlags [j]);
                        RTPrintf (" default='%ls'", Bstr (propertyDefaults [j]).raw());
                        if (j != propertyNames.size()-1)
                            RTPrintf (", ");
                    }
                }
                RTPrintf (")\n");
            }
        }
        break;

        case LISTHDDS:
        {
            com::SafeIfaceArray<IMedium> hdds;
            CHECK_ERROR(a->virtualBox, COMGETTER(HardDisks)(ComSafeArrayAsOutParam (hdds)));
            for (size_t i = 0; i < hdds.size(); ++ i)
            {
                ComPtr<IMedium> hdd = hdds[i];
                Bstr uuid;
                hdd->COMGETTER(Id)(uuid.asOutParam());
                RTPrintf("UUID:       %s\n", Utf8Str(uuid).raw());
                Bstr format;
                hdd->COMGETTER(Format)(format.asOutParam());
                RTPrintf("Format:     %lS\n", format.raw());
                Bstr filepath;
                hdd->COMGETTER(Location)(filepath.asOutParam());
                RTPrintf("Location:   %lS\n", filepath.raw());
                MediumState_T enmState;
                /// @todo NEWMEDIA check accessibility of all parents
                /// @todo NEWMEDIA print the full state value
                hdd->RefreshState(&enmState);
                RTPrintf("Accessible: %s\n", enmState != MediumState_Inaccessible ? "yes" : "no");

                MediumType_T type;
                hdd->COMGETTER(Type)(&type);
                const char *typeStr = "unknown";
                switch (type)
                {
                    case MediumType_Normal:
                        typeStr = "normal";
                        break;
                    case MediumType_Immutable:
                        typeStr = "immutable";
                        break;
                    case MediumType_Writethrough:
                        typeStr = "writethrough";
                        break;
                }
                RTPrintf("Type:       %s\n", typeStr);

                com::SafeArray<BSTR> machineIds;
                hdd->COMGETTER(MachineIds)(ComSafeArrayAsOutParam(machineIds));
                for (size_t j = 0; j < machineIds.size(); ++ j)
                {
                    ComPtr<IMachine> machine;
                    CHECK_ERROR(a->virtualBox, GetMachine(machineIds[j], machine.asOutParam()));
                    ASSERT(machine);
                    Bstr name;
                    machine->COMGETTER(Name)(name.asOutParam());
                    machine->COMGETTER(Id)(uuid.asOutParam());
                    RTPrintf("%s%lS (UUID: %lS)\n",
                            j == 0 ? "Usage:      " : "            ",
                            name.raw(), machineIds[j]);
                }
                /// @todo NEWMEDIA check usage in snapshots too
                /// @todo NEWMEDIA also list children and say 'differencing' for
                /// hard disks with the parent or 'base' otherwise.
                RTPrintf("\n");
            }
        }
        break;

        case LISTDVDS:
        {
            com::SafeIfaceArray<IMedium> dvds;
            CHECK_ERROR(a->virtualBox, COMGETTER(DVDImages)(ComSafeArrayAsOutParam(dvds)));
            for (size_t i = 0; i < dvds.size(); ++ i)
            {
                ComPtr<IMedium> dvdImage = dvds[i];
                Bstr uuid;
                dvdImage->COMGETTER(Id)(uuid.asOutParam());
                RTPrintf("UUID:       %s\n", Utf8Str(uuid).raw());
                Bstr filePath;
                dvdImage->COMGETTER(Location)(filePath.asOutParam());
                RTPrintf("Path:       %lS\n", filePath.raw());
                MediumState_T enmState;
                dvdImage->RefreshState(&enmState);
                RTPrintf("Accessible: %s\n", enmState != MediumState_Inaccessible ? "yes" : "no");

                com::SafeArray<BSTR> machineIds;
                dvdImage->COMGETTER(MachineIds)(ComSafeArrayAsOutParam(machineIds));
                for (size_t j = 0; j < machineIds.size(); ++ j)
                {
                    ComPtr<IMachine> machine;
                    CHECK_ERROR(a->virtualBox, GetMachine(machineIds[j], machine.asOutParam()));
                    ASSERT(machine);
                    Bstr name;
                    machine->COMGETTER(Name)(name.asOutParam());
                    machine->COMGETTER(Id)(uuid.asOutParam());
                    RTPrintf("%s%lS (UUID: %lS)\n",
                            j == 0 ? "Usage:      " : "            ",
                            name.raw(), machineIds[j]);
                }
                RTPrintf("\n");
            }
        }
        break;

        case LISTFLOPPIES:
        {
            com::SafeIfaceArray<IMedium> floppies;
            CHECK_ERROR(a->virtualBox, COMGETTER(FloppyImages)(ComSafeArrayAsOutParam(floppies)));
            for (size_t i = 0; i < floppies.size(); ++ i)
            {
                ComPtr<IMedium> floppyImage = floppies[i];
                Bstr uuid;
                floppyImage->COMGETTER(Id)(uuid.asOutParam());
                RTPrintf("UUID:       %s\n", Utf8Str(uuid).raw());
                Bstr filePath;
                floppyImage->COMGETTER(Location)(filePath.asOutParam());
                RTPrintf("Path:       %lS\n", filePath.raw());
                MediumState_T enmState;
                floppyImage->RefreshState(&enmState);
                RTPrintf("Accessible: %s\n", enmState != MediumState_Inaccessible ? "yes" : "no");

                com::SafeArray<BSTR> machineIds;
                floppyImage->COMGETTER(MachineIds)(ComSafeArrayAsOutParam(machineIds));
                for (size_t j = 0; j < machineIds.size(); ++ j)
                {
                    ComPtr<IMachine> machine;
                    CHECK_ERROR(a->virtualBox, GetMachine(machineIds[j], machine.asOutParam()));
                    ASSERT(machine);
                    Bstr name;
                    machine->COMGETTER(Name)(name.asOutParam());
                    machine->COMGETTER(Id)(uuid.asOutParam());
                    RTPrintf("%s%lS (UUID: %lS)\n",
                            j == 0 ? "Usage:      " : "            ",
                            name.raw(), machineIds[j]);
                }
                RTPrintf("\n");
            }
        }
        break;

        case LISTUSBHOST:
        {
            ComPtr<IHost> Host;
            CHECK_ERROR_RET (a->virtualBox, COMGETTER(Host)(Host.asOutParam()), 1);

            SafeIfaceArray <IHostUSBDevice> CollPtr;
            CHECK_ERROR_RET (Host, COMGETTER(USBDevices)(ComSafeArrayAsOutParam(CollPtr)), 1);

            RTPrintf("Host USB Devices:\n\n");

            if (CollPtr.size() == 0)
            {
                RTPrintf("<none>\n\n");
            }
            else
            {
                for (size_t i = 0; i < CollPtr.size(); ++i)
                {
                    ComPtr <IHostUSBDevice> dev = CollPtr[i];

                    /* Query info. */
                    Bstr id;
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
                            Utf8Str(id).raw(),
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
                }
            }
        }
        break;

        case LISTUSBFILTERS:
        {
            RTPrintf("Global USB Device Filters:\n\n");

            ComPtr <IHost> host;
            CHECK_ERROR_RET (a->virtualBox, COMGETTER(Host) (host.asOutParam()), 1);

            SafeIfaceArray <IHostUSBDeviceFilter> coll;
            CHECK_ERROR_RET (host, COMGETTER (USBDeviceFilters)(ComSafeArrayAsOutParam(coll)), 1);

            if (coll.size() == 0)
            {
                RTPrintf("<none>\n\n");
            }
            else
            {
                for (size_t index = 0; index < coll.size(); ++index)
                {
                    ComPtr<IHostUSBDeviceFilter> flt = coll[index];

                    /* Query info. */

                    RTPrintf("Index:            %zu\n", index);

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
                }
            }
        }
        break;

        case LISTSYSTEMPROPERTIES:
        {
            ComPtr<ISystemProperties> systemProperties;
            a->virtualBox->COMGETTER(SystemProperties)(systemProperties.asOutParam());

            Bstr str;
            ULONG ulValue;
            ULONG64 ul64Value;
            BOOL flag;

            systemProperties->COMGETTER(MinGuestRAM)(&ulValue);
            RTPrintf("Minimum guest RAM size:          %u Megabytes\n", ulValue);
            systemProperties->COMGETTER(MaxGuestRAM)(&ulValue);
            RTPrintf("Maximum guest RAM size:          %u Megabytes\n", ulValue);
            systemProperties->COMGETTER(MinGuestVRAM)(&ulValue);
            RTPrintf("Minimum video RAM size:          %u Megabytes\n", ulValue);
            systemProperties->COMGETTER(MaxGuestVRAM)(&ulValue);
            RTPrintf("Maximum video RAM size:          %u Megabytes\n", ulValue);
            systemProperties->COMGETTER(MinGuestCPUCount)(&ulValue);
            RTPrintf("Minimum guest CPU count:         %u\n", ulValue);
            systemProperties->COMGETTER(MaxGuestCPUCount)(&ulValue);
            RTPrintf("Maximum guest CPU count:         %u\n", ulValue);
            systemProperties->COMGETTER(MaxVDISize)(&ul64Value);
            RTPrintf("Maximum VDI size:                %lu Megabytes\n", ul64Value);
            systemProperties->COMGETTER(NetworkAdapterCount)(&ulValue);
            RTPrintf("Maximum Network Adapter count:   %u\n", ulValue);
            systemProperties->COMGETTER(SerialPortCount)(&ulValue);
            RTPrintf("Maximum Serial Port count:       %u\n", ulValue);
            systemProperties->COMGETTER(ParallelPortCount)(&ulValue);
            RTPrintf("Maximum Parallel Port count:     %u\n", ulValue);
            systemProperties->COMGETTER(MaxBootPosition)(&ulValue);
            RTPrintf("Maximum Boot Position:           %u\n", ulValue);
            systemProperties->GetMaxInstancesOfStorageBus(StorageBus_IDE, &ulValue);
            RTPrintf("Maximum IDE Controllers:         %u\n", ulValue);
            systemProperties->GetMaxPortCountForStorageBus(StorageBus_IDE, &ulValue);
            RTPrintf("Maximum IDE Port count:          %u\n", ulValue);
            systemProperties->GetMaxDevicesPerPortForStorageBus(StorageBus_IDE, &ulValue);
            RTPrintf("Maximum Devices per IDE Port:    %u\n", ulValue);
            systemProperties->GetMaxInstancesOfStorageBus(StorageBus_SATA, &ulValue);
            RTPrintf("Maximum SATA Controllers:        %u\n", ulValue);
            systemProperties->GetMaxPortCountForStorageBus(StorageBus_SATA, &ulValue);
            RTPrintf("Maximum SATA Port count:         %u\n", ulValue);
            systemProperties->GetMaxDevicesPerPortForStorageBus(StorageBus_SATA, &ulValue);
            RTPrintf("Maximum Devices per SATA Port:   %u\n", ulValue);
            systemProperties->GetMaxInstancesOfStorageBus(StorageBus_SCSI, &ulValue);
            RTPrintf("Maximum SCSI Controllers:        %u\n", ulValue);
            systemProperties->GetMaxPortCountForStorageBus(StorageBus_SCSI, &ulValue);
            RTPrintf("Maximum SCSI Port count:         %u\n", ulValue);
            systemProperties->GetMaxDevicesPerPortForStorageBus(StorageBus_SCSI, &ulValue);
            RTPrintf("Maximum Devices per SCSI Port:   %u\n", ulValue);
            systemProperties->GetMaxInstancesOfStorageBus(StorageBus_Floppy, &ulValue);
            RTPrintf("Maximum Floppy Controllers:      %u\n", ulValue);
            systemProperties->GetMaxPortCountForStorageBus(StorageBus_Floppy, &ulValue);
            RTPrintf("Maximum Floppy Port count:       %u\n", ulValue);
            systemProperties->GetMaxDevicesPerPortForStorageBus(StorageBus_Floppy, &ulValue);
            RTPrintf("Maximum Devices per Floppy Port: %u\n", ulValue);
            systemProperties->COMGETTER(DefaultMachineFolder)(str.asOutParam());
            RTPrintf("Default machine folder:          %lS\n", str.raw());
            systemProperties->COMGETTER(DefaultHardDiskFolder)(str.asOutParam());
            RTPrintf("Default hard disk folder:        %lS\n", str.raw());
            systemProperties->COMGETTER(RemoteDisplayAuthLibrary)(str.asOutParam());
            RTPrintf("VRDP authentication library:     %lS\n", str.raw());
            systemProperties->COMGETTER(WebServiceAuthLibrary)(str.asOutParam());
            RTPrintf("Webservice auth. library:        %lS\n", str.raw());
            systemProperties->COMGETTER(LogHistoryCount)(&ulValue);
            RTPrintf("Log history count:               %u\n", ulValue);

        }
        break;
        case LISTDHCPSERVERS:
        {
            com::SafeIfaceArray<IDHCPServer> svrs;
            CHECK_ERROR(a->virtualBox, COMGETTER(DHCPServers)(ComSafeArrayAsOutParam (svrs)));
            for (size_t i = 0; i < svrs.size(); ++ i)
            {
                ComPtr<IDHCPServer> svr = svrs[i];
                Bstr netName;
                svr->COMGETTER(NetworkName)(netName.asOutParam());
                RTPrintf("NetworkName:    %lS\n", netName.raw());
                Bstr ip;
                svr->COMGETTER(IPAddress)(ip.asOutParam());
                RTPrintf("IP:             %lS\n", ip.raw());
                Bstr netmask;
                svr->COMGETTER(NetworkMask)(netmask.asOutParam());
                RTPrintf("NetworkMask:    %lS\n", netmask.raw());
                Bstr lowerIp;
                svr->COMGETTER(LowerIP)(lowerIp.asOutParam());
                RTPrintf("lowerIPAddress: %lS\n", lowerIp.raw());
                Bstr upperIp;
                svr->COMGETTER(UpperIP)(upperIp.asOutParam());
                RTPrintf("upperIPAddress: %lS\n", upperIp.raw());
                BOOL bEnabled;
                svr->COMGETTER(Enabled)(&bEnabled);
                RTPrintf("Enabled:        %s\n", bEnabled ? "Yes" : "No");
                RTPrintf("\n");
            }
        }
        break;
    } // end switch

    return SUCCEEDED(rc) ? 0 : 1;
}

#endif /* !VBOX_ONLY_DOCS */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
