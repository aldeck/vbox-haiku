/* $Id$ */
/** @file
 * VBoxManage - The appliance-related commands.
 */

/*
 * Copyright (C) 2009 Sun Microsystems, Inc.
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
#ifndef VBOX_ONLY_DOCS
#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint2.h>
#include <VBox/com/EventQueue.h>

#include <VBox/com/VirtualBox.h>

#include <list>
#include <map>
#endif /* !VBOX_ONLY_DOCS */

#include <iprt/stream.h>
#include <iprt/getopt.h>

#include <VBox/log.h>

#include "VBoxManage.h"
using namespace com;


// funcs
///////////////////////////////////////////////////////////////////////////////

typedef std::map<Utf8Str, Utf8Str> ArgsMap;                 // pairs of strings like "-vmname" => "newvmname"
typedef std::map<uint32_t, ArgsMap> ArgsMapsMap;            // map of maps, one for each virtual system, sorted by index

typedef std::map<uint32_t, bool> IgnoresMap;                // pairs of numeric description entry indices
typedef std::map<uint32_t, IgnoresMap> IgnoresMapsMap;      // map of maps, one for each virtual system, sorted by index

static bool findArgValue(Utf8Str &strOut,
                         ArgsMap *pmapArgs,
                         const Utf8Str &strKey)
{
    if (pmapArgs)
    {
        ArgsMap::iterator it;
        it = pmapArgs->find(strKey);
        if (it != pmapArgs->end())
        {
            strOut = it->second;
            pmapArgs->erase(it);
            return true;
        }
    }

    return false;
}

int handleImportAppliance(HandlerArg *a)
{
    HRESULT rc = S_OK;

    Utf8Str strOvfFilename;
    bool fExecute = true;                  // if true, then we actually do the import

    uint32_t ulCurVsys = (uint32_t)-1;

    // for each -vsys X command, maintain a map of command line items
    // (we'll parse them later after interpreting the OVF, when we can
    // actually check whether they make sense semantically)
    ArgsMapsMap mapArgsMapsPerVsys;
    IgnoresMapsMap mapIgnoresMapsPerVsys;

    for (int i = 0;
         i < a->argc;
         ++i)
    {
        bool fIsIgnore = false;
        Utf8Str strThisArg(a->argv[i]);
        if (    (strThisArg == "--dry-run")
             || (strThisArg == "-dry-run")
             || (strThisArg == "-n")
           )
            fExecute = false;
        else if (strThisArg == "-vsys")
        {
            if (++i < a->argc)
            {
                uint32_t ulVsys;
                if (VINF_SUCCESS != (rc = Utf8Str(a->argv[i]).toInt(ulVsys)))       // don't use SUCCESS() macro, fail even on warnings
                    return errorSyntax(USAGE_IMPORTAPPLIANCE, "Argument to -vsys option must be a non-negative number.");

                ulCurVsys = ulVsys;
            }
            else
                return errorSyntax(USAGE_IMPORTAPPLIANCE, "Missing argument to -vsys option.");
        }
        else if (    (strThisArg == "-ostype")
                  || (strThisArg == "-vmname")
                  || (strThisArg == "-memory")
                  || (fIsIgnore = (strThisArg == "-ignore"))
                  || (strThisArg.substr(0, 5) == "-type")
                  || (strThisArg.substr(0, 11) == "-controller")
                )
        {
            if (ulCurVsys == (uint32_t)-1)
                return errorSyntax(USAGE_IMPORTAPPLIANCE, "Option \"%s\" requires preceding -vsys argument.", strThisArg.c_str());

            if (++i < a->argc)
                if (fIsIgnore)
                {
                    uint32_t ulItem;
                    if (VINF_SUCCESS != Utf8Str(a->argv[i]).toInt(ulItem))
                        return errorSyntax(USAGE_IMPORTAPPLIANCE, "Argument to -vsys option must be a non-negative number.");

                    mapIgnoresMapsPerVsys[ulCurVsys][ulItem] = true;
                }
                else
                {
                    // store both this arg and the next one in the strings map for later parsing
                    mapArgsMapsPerVsys[ulCurVsys][strThisArg] = Utf8Str(a->argv[i]);
                }
            else
                return errorSyntax(USAGE_IMPORTAPPLIANCE, "Missing argument to \"%s\" option.", strThisArg.c_str());
        }
        else if (strThisArg[0] == '-')
            return errorSyntax(USAGE_IMPORTAPPLIANCE, "Unknown option \"%s\".", strThisArg.c_str());
        else if (!strOvfFilename)
            strOvfFilename = strThisArg;
        else
            return errorSyntax(USAGE_IMPORTAPPLIANCE, "Too many arguments for \"import\" command.");
    }

    if (!strOvfFilename)
        return errorSyntax(USAGE_IMPORTAPPLIANCE, "Not enough arguments for \"import\" command.");

    do
    {
        Bstr bstrOvfFilename(strOvfFilename);
        ComPtr<IAppliance> pAppliance;
        CHECK_ERROR_BREAK(a->virtualBox, CreateAppliance(pAppliance.asOutParam()));

        CHECK_ERROR_BREAK(pAppliance, Read(bstrOvfFilename));

        RTPrintf("Interpreting %s... ", strOvfFilename.c_str());
        CHECK_ERROR_BREAK(pAppliance, Interpret());
        RTPrintf("OK.\n");

        // fetch all disks
        com::SafeArray<BSTR> retDisks;
        CHECK_ERROR_BREAK(pAppliance,
                          COMGETTER(Disks)(ComSafeArrayAsOutParam(retDisks)));
        if (retDisks.size() > 0)
        {
            RTPrintf("Disks:");
            for (unsigned i = 0; i < retDisks.size(); i++)
                RTPrintf("  %ls", retDisks[i]);
            RTPrintf("\n");
        }

        // fetch virtual system descriptions
        com::SafeIfaceArray<IVirtualSystemDescription> aVirtualSystemDescriptions;
        CHECK_ERROR_BREAK(pAppliance,
                          COMGETTER(VirtualSystemDescriptions)(ComSafeArrayAsOutParam(aVirtualSystemDescriptions)));

        uint32_t cVirtualSystemDescriptions = aVirtualSystemDescriptions.size();

        // match command line arguments with virtual system descriptions;
        // this is only to sort out invalid indices at this time
        ArgsMapsMap::const_iterator it;
        for (it = mapArgsMapsPerVsys.begin();
             it != mapArgsMapsPerVsys.end();
             ++it)
        {
            uint32_t ulVsys = it->first;
            if (ulVsys >= cVirtualSystemDescriptions)
                return errorSyntax(USAGE_IMPORTAPPLIANCE,
                                   "Invalid index %RI32 with -vsys option; the OVF contains only %RI32 virtual system(s).",
                                   ulVsys, cVirtualSystemDescriptions);
        }

        // dump virtual system descriptions and match command-line arguments
        if (cVirtualSystemDescriptions > 0)
        {
            for (unsigned i = 0; i < cVirtualSystemDescriptions; ++i)
            {
                com::SafeArray<VirtualSystemDescriptionType_T> retTypes;
                com::SafeArray<BSTR> aRefs;
                com::SafeArray<BSTR> aOrigValues;
                com::SafeArray<BSTR> aConfigValues;
                com::SafeArray<BSTR> aExtraConfigValues;
                CHECK_ERROR_BREAK(aVirtualSystemDescriptions[i],
                                  GetDescription(ComSafeArrayAsOutParam(retTypes),
                                                 ComSafeArrayAsOutParam(aRefs),
                                                 ComSafeArrayAsOutParam(aOrigValues),
                                                 ComSafeArrayAsOutParam(aConfigValues),
                                                 ComSafeArrayAsOutParam(aExtraConfigValues)));

                RTPrintf("Virtual system %i:\n", i);

                // look up the corresponding command line options, if any
                ArgsMap *pmapArgs = NULL;
                ArgsMapsMap::iterator itm = mapArgsMapsPerVsys.find(i);
                if (itm != mapArgsMapsPerVsys.end())
                    pmapArgs = &itm->second;

                // this collects the final values for setFinalValues()
                com::SafeArray<BOOL> aEnabled(retTypes.size());
                com::SafeArray<BSTR> aFinalValues(retTypes.size());

                for (unsigned a = 0; a < retTypes.size(); ++a)
                {
                    VirtualSystemDescriptionType_T t = retTypes[a];

                    Utf8Str strOverride;

                    Bstr bstrFinalValue = aConfigValues[a];

                    bool fIgnoreThis = mapIgnoresMapsPerVsys[i][a];

                    aEnabled[a] = true;

                    switch (t)
                    {
                        case VirtualSystemDescriptionType_Name:
                            if (findArgValue(strOverride, pmapArgs, "-vmname"))
                            {
                                bstrFinalValue = strOverride;
                                RTPrintf("%2d: VM name specified with -vmname: \"%ls\"\n",
                                        a, bstrFinalValue.raw());
                            }
                            else
                                RTPrintf("%2d: Suggested VM name \"%ls\""
                                        "\n    (change with \"-vsys %d -vmname <name>\")\n",
                                        a, bstrFinalValue.raw(), i);
                        break;

                        case VirtualSystemDescriptionType_OS:
                            if (findArgValue(strOverride, pmapArgs, "-ostype"))
                            {
                                bstrFinalValue = strOverride;
                                RTPrintf("%2d: OS type specified with -ostype: \"%ls\"\n",
                                        a, bstrFinalValue.raw());
                            }
                            else
                                RTPrintf("%2d: Suggested OS type: \"%ls\""
                                        "\n    (change with \"-vsys %d -ostype <type>\"; use \"list ostypes\" to list all)\n",
                                        a, bstrFinalValue.raw(), i);
                        break;

                        case VirtualSystemDescriptionType_CPU:
                            RTPrintf("%2d: Number of CPUs (ignored): %ls\n",
                                     a, aConfigValues[a]);
                        break;

                        case VirtualSystemDescriptionType_Memory:
                        {
                            if (findArgValue(strOverride, pmapArgs, "-memory"))
                            {
                                uint32_t ulMemMB;
                                if (VINF_SUCCESS == strOverride.toInt(ulMemMB))
                                {
                                    bstrFinalValue = strOverride;
                                    RTPrintf("%2d: Guest memory specified with -memory: %ls MB\n",
                                             a, bstrFinalValue.raw());
                                }
                                else
                                    return errorSyntax(USAGE_IMPORTAPPLIANCE,
                                                       "Argument to -memory option must be a non-negative number.");
                            }
                            else
                                RTPrintf("%2d: Guest memory: %ls MB\n    (change with \"-vsys %d -memory <MB>\")\n",
                                         a, bstrFinalValue.raw(), i);
                        }
                        break;

                        case VirtualSystemDescriptionType_HardDiskControllerIDE:
                            if (fIgnoreThis)
                            {
                                RTPrintf("%2d: IDE controller, type %ls -- disabled\n",
                                         a,
                                         aConfigValues[a]);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf("%2d: IDE controller, type %ls"
                                         "\n    (disable with \"-vsys %d -ignore %d\")\n",
                                         a,
                                         aConfigValues[a],
                                         i, a);
                        break;

                        case VirtualSystemDescriptionType_HardDiskControllerSATA:
                            if (fIgnoreThis)
                            {
                                RTPrintf("%2d: SATA controller, type %ls -- disabled\n",
                                         a,
                                         aConfigValues[a]);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf("%2d: SATA controller, type %ls"
                                        "\n    (disable with \"-vsys %d -ignore %d\")\n",
                                        a,
                                        aConfigValues[a],
                                        i, a);
                        break;

                        case VirtualSystemDescriptionType_HardDiskControllerSCSI:
                            if (fIgnoreThis)
                            {
                                RTPrintf("%2d: SCSI controller, type %ls -- disabled\n",
                                         a,
                                         aConfigValues[a]);
                                aEnabled[a] = false;
                            }
                            else
                            {
                                Utf8StrFmt strTypeArg("-type%RI16", a);
                                if (findArgValue(strOverride, pmapArgs, strTypeArg))
                                {
                                    bstrFinalValue = strOverride;
                                    RTPrintf("%2d: SCSI controller, type set with -type%d: %ls\n",
                                            a,
                                            a,
                                            bstrFinalValue.raw());
                                }
                                else
                                    RTPrintf("%2d: SCSI controller, type %ls"
                                            "\n    (change with \"-vsys %d -type%d {BusLogic|LsiLogic}\";"
                                            "\n    disable with \"-vsys %d -ignore %d\")\n",
                                            a,
                                            aConfigValues[a],
                                            i, a, i, a);
                            }
                        break;

                        case VirtualSystemDescriptionType_HardDiskImage:
                            if (fIgnoreThis)
                            {
                                RTPrintf("%2d: Hard disk image: source image=%ls -- disabled\n",
                                         a,
                                         aOrigValues[a]);
                                aEnabled[a] = false;
                            }
                            else
                            {
                                Utf8StrFmt strTypeArg("-controller%RI16", a);
                                if (findArgValue(strOverride, pmapArgs, strTypeArg))
                                {
                                    // strOverride now has the controller index as a number, but we
                                    // need a "controller=X" format string
                                    strOverride = Utf8StrFmt("controller=%s", strOverride.c_str());
                                    Bstr bstrExtraConfigValue = strOverride;
                                    bstrExtraConfigValue.detachTo(&aExtraConfigValues[a]);
                                    RTPrintf("%2d: Hard disk image: source image=%ls, target path=%ls, %ls\n",
                                            a,
                                            aOrigValues[a],
                                            aConfigValues[a],
                                            aExtraConfigValues[a]);
                                }
                                else
                                    RTPrintf("%2d: Hard disk image: source image=%ls, target path=%ls, %ls"
                                            "\n    (change controller with \"-vsys %d -controller%d <id>\";"
                                            "\n    disable with \"-vsys %d -ignore %d\")\n",
                                            a,
                                            aOrigValues[a],
                                            aConfigValues[a],
                                            aExtraConfigValues[a],
                                            i, a, i, a);
                            }
                        break;

                        case VirtualSystemDescriptionType_CDROM:
                            if (fIgnoreThis)
                            {
                                RTPrintf("%2d: CD-ROM -- disabled\n",
                                         a);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf("%2d: CD-ROM"
                                        "\n    (disable with \"-vsys %d -ignore %d\")\n",
                                        a, i, a);
                        break;

                        case VirtualSystemDescriptionType_Floppy:
                            if (fIgnoreThis)
                            {
                                RTPrintf("%2d: Floppy -- disabled\n",
                                         a);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf("%2d: Floppy"
                                        "\n    (disable with \"-vsys %d -ignore %d\")\n",
                                        a, i, a);
                        break;

                        case VirtualSystemDescriptionType_NetworkAdapter:
                            RTPrintf("%2d: Network adapter: orig %ls, config %ls, extra %ls\n",   // @todo implement once we have a plan for the back-end
                                     a,
                                     aOrigValues[a],
                                     aConfigValues[a],
                                     aExtraConfigValues[a]);
                        break;

                        case VirtualSystemDescriptionType_USBController:
                            if (fIgnoreThis)
                            {
                                RTPrintf("%2d: USB controller -- disabled\n",
                                         a);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf("%2d: USB controller"
                                        "\n    (disable with \"-vsys %d -ignore %d\")\n",
                                        a, i, a);
                        break;

                        case VirtualSystemDescriptionType_SoundCard:
                            if (fIgnoreThis)
                            {
                                RTPrintf("%2d: Sound card \"%ls\" -- disabled\n",
                                         a,
                                         aOrigValues[a]);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf("%2d: Sound card (appliance expects \"%ls\", can change on import)"
                                        "\n    (disable with \"-vsys %d -ignore %d\")\n",
                                        a,
                                        aOrigValues[a],
                                        i,
                                        a);
                        break;
                    }

                    bstrFinalValue.detachTo(&aFinalValues[a]);
                }

                if (fExecute)
                    CHECK_ERROR_BREAK(aVirtualSystemDescriptions[i],
                                      SetFinalValues(ComSafeArrayAsInParam(aEnabled),
                                                     ComSafeArrayAsInParam(aFinalValues),
                                                     ComSafeArrayAsInParam(aExtraConfigValues)));

            } // for (unsigned i = 0; i < cVirtualSystemDescriptions; ++i)

            if (fExecute)
            {
                ComPtr<IProgress> progress;
                CHECK_ERROR_BREAK(pAppliance,
                                  ImportMachines(progress.asOutParam()));

                showProgress(progress);

                if (SUCCEEDED(rc))
                    progress->COMGETTER(ResultCode)(&rc);

                if (FAILED(rc))
                {
                    com::ProgressErrorInfo info(progress);
                    com::GluePrintErrorInfo(info);
                    com::GluePrintErrorContext("ImportAppliance", __FILE__, __LINE__);
                }
                else
                    RTPrintf("Successfully imported the appliance.\n");
            }
        } // end if (aVirtualSystemDescriptions.size() > 0)
    } while (0);

    return SUCCEEDED(rc) ? 0 : 1;
}

static const RTGETOPTDEF g_aExportOptions[]
    = {
        { "--output",             'o', RTGETOPT_REQ_STRING },
      };

int handleExportAppliance(HandlerArg *a)
{
    HRESULT rc = S_OK;

    Utf8Str strOutputFile;
    std::list< ComPtr<IMachine> > llMachines;

    do
    {
        int c;

        RTGETOPTUNION ValueUnion;
        RTGETOPTSTATE GetState;
        RTGetOptInit(&GetState,
                     a->argc,
                     a->argv,
                     g_aExportOptions,
                     RT_ELEMENTS(g_aExportOptions),
                     0, // start at 0 even though arg 1 was "list" because main() has hacked both the argc and argv given to us
                     0 /* fFlags */);
        while ((c = RTGetOpt(&GetState, &ValueUnion)))
        {
            switch (c)
            {
                case 'o':   // --output
                    if (strOutputFile.length())
                        return errorSyntax(USAGE_EXPORTAPPLIANCE, "You can only specify --output once.");
                    else
                        strOutputFile = ValueUnion.psz;
                break;

                case VINF_GETOPT_NOT_OPTION:
                {
                    Utf8Str strMachine(ValueUnion.psz);
                    // must be machine: try UUID or name
                    ComPtr<IMachine> machine;
                    /* assume it's a UUID */
                    rc = a->virtualBox->GetMachine(Guid(strMachine), machine.asOutParam());
                    if (FAILED(rc) || !machine)
                    {
                        /* must be a name */
                        CHECK_ERROR_BREAK(a->virtualBox, FindMachine(Bstr(strMachine), machine.asOutParam()));
                    }

                    if (machine)
                        llMachines.push_back(machine);
                }
                break;

                default:
                    if (c > 0)
                        return errorSyntax(USAGE_LIST, "missing case: %c\n", c);
                    else if (ValueUnion.pDef)
                        return errorSyntax(USAGE_LIST, "%s: %Rrs", ValueUnion.pDef->pszLong, c);
                    else
                        return errorSyntax(USAGE_LIST, "%Rrs", c);
            }
        }

        if (FAILED(rc))
            break;

        if (llMachines.size() == 0)
            return errorSyntax(USAGE_EXPORTAPPLIANCE, "At least one machine must be specified with the export command.");
        if (!strOutputFile.length())
            return errorSyntax(USAGE_EXPORTAPPLIANCE, "Missing --output argument with export command.");

        ComPtr<IAppliance> pAppliance;
        CHECK_ERROR_BREAK(a->virtualBox, CreateAppliance(pAppliance.asOutParam()));

        std::list< ComPtr<IMachine> >::iterator itM;
        for (itM = llMachines.begin();
             itM != llMachines.end();
             ++itM)
        {
            ComPtr<IMachine> pMachine = *itM;
            CHECK_ERROR_BREAK(pMachine, Export(pAppliance));
        }

        if (FAILED(rc))
            break;

        CHECK_ERROR_BREAK(pAppliance, Write(Bstr(strOutputFile)));

    } while (0);

    return SUCCEEDED(rc) ? 0 : 1;
}

#endif /* !VBOX_ONLY_DOCS */
