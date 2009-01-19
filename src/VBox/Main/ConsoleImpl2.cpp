/* $Id$ */
/** @file
 * VBox Console COM Class implementation
 *
 * @remark  We've split out the code that the 64-bit VC++ v8 compiler
 *          finds problematic to optimize so we can disable optimizations
 *          and later, perhaps, find a real solution for it.
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "ConsoleImpl.h"
#include "DisplayImpl.h"
#include "VMMDev.h"

// generated header
#include "SchemaDefs.h"

#include "Logging.h"

#include <iprt/string.h>
#include <iprt/path.h>
#include <iprt/dir.h>
#include <iprt/param.h>

#include <VBox/vmapi.h>
#include <VBox/err.h>
#include <VBox/version.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#ifdef VBOX_WITH_CROGL
#include <VBox/HostServices/VBoxCrOpenGLSvc.h>
#endif
#ifdef VBOX_WITH_GUEST_PROPS
# include <VBox/HostServices/GuestPropertySvc.h>
# include <VBox/com/defs.h>
# include <VBox/com/array.h>
# include <hgcm/HGCM.h> /** @todo it should be possible to register a service
                          * extension using a VMMDev callback. */
# include <vector>
#endif /* VBOX_WITH_GUEST_PROPS */
#include <VBox/intnet.h>

#include <VBox/com/string.h>
#include <VBox/com/array.h>

#if defined(RT_OS_SOLARIS) && defined(VBOX_WITH_NETFLT)
# include <zone.h>
#endif

#if defined(RT_OS_LINUX) && defined(VBOX_WITH_NETFLT)
# include <unistd.h>
# include <sys/ioctl.h>
# include <sys/socket.h>
# include <linux/types.h>
# include <linux/if.h>
# include <linux/wireless.h>
#endif

#if defined(RT_OS_WINDOWS) && defined(VBOX_WITH_NETFLT)
# include <VBox/WinNetConfig.h>
# include <Ntddndis.h>
# include <devguid.h>
#endif


/*
 * VC++ 8 / amd64 has some serious trouble with this function.
 * As a temporary measure, we'll drop global optimizations.
 */
#if defined(_MSC_VER) && defined(RT_ARCH_AMD64)
# pragma optimize("g", off)
#endif

/**
 *  Construct the VM configuration tree (CFGM).
 *
 *  This is a callback for VMR3Create() call. It is called from CFGMR3Init()
 *  in the emulation thread (EMT). Any per thread COM/XPCOM initialization
 *  is done here.
 *
 *  @param   pVM                 VM handle.
 *  @param   pvConsole           Pointer to the VMPowerUpTask object.
 *  @return  VBox status code.
 *
 *  @note Locks the Console object for writing.
 */
DECLCALLBACK(int) Console::configConstructor(PVM pVM, void *pvConsole)
{
    LogFlowFuncEnter();
    /* Note: hardcoded assumption about number of slots; see rom bios */
    bool afPciDeviceNo[32] = {false};

#if !defined (VBOX_WITH_XPCOM)
    {
        /* initialize COM */
        HRESULT hrc = CoInitializeEx(NULL,
                                     COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE |
                                     COINIT_SPEED_OVER_MEMORY);
        LogFlow (("Console::configConstructor(): CoInitializeEx()=%08X\n", hrc));
        AssertComRCReturn (hrc, VERR_GENERAL_FAILURE);
    }
#endif

    AssertReturn (pvConsole, VERR_GENERAL_FAILURE);
    ComObjPtr <Console> pConsole = static_cast <Console *> (pvConsole);

    AutoCaller autoCaller (pConsole);
    AssertComRCReturn (autoCaller.rc(), VERR_ACCESS_DENIED);

    /* lock the console because we widely use internal fields and methods */
    AutoWriteLock alock (pConsole);

    ComPtr <IMachine> pMachine = pConsole->machine();

    int             rc;
    HRESULT         hrc;
    char           *psz = NULL;
    BSTR            str = NULL;

    Bstr            bstr; /* use this bstr when calling COM methods instead
                             of str as it manages memory! */

#define STR_CONV()  do { rc = RTUtf16ToUtf8(str, &psz); RC_CHECK(); } while (0)
#define STR_FREE()  do { if (str) { SysFreeString(str); str = NULL; } if (psz) { RTStrFree(psz); psz = NULL; } } while (0)
#define RC_CHECK()  do { if (RT_FAILURE(rc)) { AssertMsgFailed(("rc=%Rrc\n", rc)); STR_FREE(); return rc; } } while (0)
#define H()         do { if (FAILED(hrc)) { AssertMsgFailed(("hrc=%#x\n", hrc)); STR_FREE(); return VERR_GENERAL_FAILURE; } } while (0)

    /*
     * Get necessary objects and frequently used parameters.
     */
    ComPtr<IVirtualBox> virtualBox;
    hrc = pMachine->COMGETTER(Parent)(virtualBox.asOutParam());                     H();

    ComPtr<IHost> host;
    hrc = virtualBox->COMGETTER(Host)(host.asOutParam());                           H();

    ComPtr <ISystemProperties> systemProperties;
    hrc = virtualBox->COMGETTER(SystemProperties)(systemProperties.asOutParam());   H();

    ComPtr<IBIOSSettings> biosSettings;
    hrc = pMachine->COMGETTER(BIOSSettings)(biosSettings.asOutParam());             H();

    Guid uuid;
    hrc = pMachine->COMGETTER(Id)(uuid.asOutParam());                               H();
    PCRTUUID pUuid = uuid.raw();

    ULONG cRamMBs;
    hrc = pMachine->COMGETTER(MemorySize)(&cRamMBs);                                H();

    ULONG cCpus = 1;
#ifdef VBOX_WITH_SMP_GUESTS
    hrc = pMachine->COMGETTER(CPUCount)(&cCpus);                                    H();
#endif

    /*
     * Get root node first.
     * This is the only node in the tree.
     */
    PCFGMNODE pRoot = CFGMR3GetRoot(pVM);
    Assert(pRoot);

    /*
     * Set the root (and VMM) level values.
     */
    hrc = pMachine->COMGETTER(Name)(&str);                                          H();
    STR_CONV();
    rc = CFGMR3InsertString(pRoot,  "Name",                 psz);                   RC_CHECK();
    STR_FREE();
    rc = CFGMR3InsertBytes(pRoot,   "UUID", pUuid, sizeof(*pUuid));                 RC_CHECK();
    rc = CFGMR3InsertInteger(pRoot, "RamSize",              cRamMBs * _1M);         RC_CHECK();
    rc = CFGMR3InsertInteger(pRoot, "NumCPUs",              cCpus);                 RC_CHECK();
    rc = CFGMR3InsertInteger(pRoot, "TimerMillies",         10);                    RC_CHECK();
    rc = CFGMR3InsertInteger(pRoot, "RawR3Enabled",         1);     /* boolean */   RC_CHECK();
    rc = CFGMR3InsertInteger(pRoot, "RawR0Enabled",         1);     /* boolean */   RC_CHECK();
    /** @todo Config: RawR0, PATMEnabled and CASMEnabled needs attention later. */
    rc = CFGMR3InsertInteger(pRoot, "PATMEnabled",          1);     /* boolean */   RC_CHECK();
    rc = CFGMR3InsertInteger(pRoot, "CSAMEnabled",          1);     /* boolean */   RC_CHECK();

    /* hardware virtualization extensions */
    TSBool_T hwVirtExEnabled;
    BOOL fHWVirtExEnabled;
    hrc = pMachine->COMGETTER(HWVirtExEnabled)(&hwVirtExEnabled);                   H();
    if (hwVirtExEnabled == TSBool_Default)
    {
        /* check the default value */
        hrc = systemProperties->COMGETTER(HWVirtExEnabled)(&fHWVirtExEnabled);      H();
    }
    else
        fHWVirtExEnabled = (hwVirtExEnabled == TSBool_True);
#ifdef RT_OS_DARWIN
    rc = CFGMR3InsertInteger(pRoot, "HwVirtExtForced",      fHWVirtExEnabled);      RC_CHECK();
#else
    rc = CFGMR3InsertInteger(pRoot, "HwVirtExtForced",      0);                     RC_CHECK();
#endif

    PCFGMNODE pHWVirtExt;
    rc = CFGMR3InsertNode(pRoot, "HWVirtExt", &pHWVirtExt);                         RC_CHECK();
    if (fHWVirtExEnabled)
    {
        rc = CFGMR3InsertInteger(pHWVirtExt, "Enabled",     1);                     RC_CHECK();

        /* Indicate whether 64-bit guests are supported or not. */
        /** @todo This is currently only forced off on 32-bit hosts only because it
         *        makes a lof of difference there (REM and Solaris performance).
         */

        Bstr osTypeId;
        hrc = pMachine->COMGETTER(OSTypeId)(osTypeId.asOutParam());                 H();

        ComPtr <IGuestOSType> guestOSType;
        hrc = virtualBox->GetGuestOSType(osTypeId, guestOSType.asOutParam());       H();

        BOOL fSupportsLongMode = false;
        hrc = host->GetProcessorFeature(ProcessorFeature_LongMode,
                                        &fSupportsLongMode);                        H();
        BOOL fIs64BitGuest = false;
        hrc = guestOSType->COMGETTER(Is64Bit)(&fIs64BitGuest);                      H();

        if (fSupportsLongMode && fIs64BitGuest)
        {
            rc = CFGMR3InsertInteger(pHWVirtExt, "64bitEnabled", 1);                RC_CHECK();
#if ARCH_BITS == 32 /* The recompiler must use load VBoxREM64 (32-bit host only). */
            PCFGMNODE pREM;
            rc = CFGMR3InsertNode(pRoot, "REM", &pREM);                             RC_CHECK();
            rc = CFGMR3InsertInteger(pREM, "64bitEnabled", 1);                      RC_CHECK();
#endif
        }
#if ARCH_BITS == 32 /* 32-bit guests only. */
        else
        {
            rc = CFGMR3InsertInteger(pHWVirtExt, "64bitEnabled", 0);                RC_CHECK();
        }
#endif
    }

    /* Nested paging (VT-x/AMD-V) */
    BOOL fEnableNestedPaging = false;
    hrc = pMachine->COMGETTER(HWVirtExNestedPagingEnabled)(&fEnableNestedPaging);   H();
    rc = CFGMR3InsertInteger(pRoot, "EnableNestedPaging", fEnableNestedPaging);     RC_CHECK();

    /* VPID (VT-x) */
    BOOL fEnableVPID = false;
    hrc = pMachine->COMGETTER(HWVirtExVPIDEnabled)(&fEnableVPID);                   H();
    rc = CFGMR3InsertInteger(pRoot, "EnableVPID", fEnableVPID);                     RC_CHECK();

    /* Physical Address Extension (PAE) */
    BOOL fEnablePAE = false;
    hrc = pMachine->COMGETTER(PAEEnabled)(&fEnablePAE);                             H();
    rc = CFGMR3InsertInteger(pRoot, "EnablePAE", fEnablePAE);                       RC_CHECK();

    BOOL fIOAPIC;
    hrc = biosSettings->COMGETTER(IOAPICEnabled)(&fIOAPIC);                          H();

    BOOL fPXEDebug;
    hrc = biosSettings->COMGETTER(PXEDebugEnabled)(&fPXEDebug);                      H();

    /*
     * Virtual IDE controller type.
     */
    IDEControllerType_T controllerType;
    BOOL fPIIX4;
    hrc = biosSettings->COMGETTER(IDEControllerType)(&controllerType);               H();
    switch (controllerType)
    {
        case IDEControllerType_PIIX3:
            fPIIX4 = FALSE;
            break;
        case IDEControllerType_PIIX4:
            fPIIX4 = TRUE;
            break;
        default:
            AssertMsgFailed(("Invalid IDE controller type '%d'", controllerType));
            return VMSetError(pVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                              N_("Invalid IDE controller type '%d'"), controllerType);
    }

    /*
     * PDM config.
     *  Load drivers in VBoxC.[so|dll]
     */
    PCFGMNODE pPDM;
    PCFGMNODE pDrivers;
    PCFGMNODE pMod;
    rc = CFGMR3InsertNode(pRoot,    "PDM", &pPDM);                                     RC_CHECK();
    rc = CFGMR3InsertNode(pPDM,     "Drivers", &pDrivers);                             RC_CHECK();
    rc = CFGMR3InsertNode(pDrivers, "VBoxC", &pMod);                                   RC_CHECK();
#ifdef VBOX_WITH_XPCOM
    // VBoxC is located in the components subdirectory
    char szPathVBoxC[RTPATH_MAX];
    rc = RTPathAppPrivateArch(szPathVBoxC, RTPATH_MAX - sizeof("/components/VBoxC")); AssertRC(rc);
    strcat(szPathVBoxC, "/components/VBoxC");
    rc = CFGMR3InsertString(pMod,   "Path",  szPathVBoxC);                             RC_CHECK();
#else
    rc = CFGMR3InsertString(pMod,   "Path",  "VBoxC");                                 RC_CHECK();
#endif

    /*
     * Devices
     */
    PCFGMNODE pDevices = NULL;      /* /Devices */
    PCFGMNODE pDev = NULL;          /* /Devices/Dev/ */
    PCFGMNODE pInst = NULL;         /* /Devices/Dev/0/ */
    PCFGMNODE pCfg = NULL;          /* /Devices/Dev/.../Config/ */
    PCFGMNODE pLunL0 = NULL;        /* /Devices/Dev/0/LUN#0/ */
    PCFGMNODE pLunL1 = NULL;        /* /Devices/Dev/0/LUN#0/AttachedDriver/ */
    PCFGMNODE pLunL2 = NULL;        /* /Devices/Dev/0/LUN#0/AttachedDriver/Config/ */
    PCFGMNODE pIdeInst = NULL;      /* /Devices/piix3ide/0/ */
    PCFGMNODE pSataInst = NULL;     /* /Devices/ahci/0/ */
    PCFGMNODE pBiosCfg = NULL;      /* /Devices/pcbios/0/Config/ */

    rc = CFGMR3InsertNode(pRoot, "Devices", &pDevices);                             RC_CHECK();

    /*
     * PC Arch.
     */
    rc = CFGMR3InsertNode(pDevices, "pcarch", &pDev);                               RC_CHECK();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               RC_CHECK();

    /*
     * PC Bios.
     */
    rc = CFGMR3InsertNode(pDevices, "pcbios", &pDev);                               RC_CHECK();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pBiosCfg);                           RC_CHECK();
    rc = CFGMR3InsertInteger(pBiosCfg,  "RamSize",              cRamMBs * _1M);     RC_CHECK();
    rc = CFGMR3InsertInteger(pBiosCfg,  "NumCPUs",              cCpus);             RC_CHECK();
    rc = CFGMR3InsertString(pBiosCfg,   "HardDiskDevice",       "piix3ide");        RC_CHECK();
    rc = CFGMR3InsertString(pBiosCfg,   "FloppyDevice",         "i82078");          RC_CHECK();
    rc = CFGMR3InsertInteger(pBiosCfg,  "IOAPIC",               fIOAPIC);           RC_CHECK();
    rc = CFGMR3InsertInteger(pBiosCfg,  "PXEDebug",             fPXEDebug);         RC_CHECK();
    rc = CFGMR3InsertBytes(pBiosCfg,    "UUID", pUuid, sizeof(*pUuid));             RC_CHECK();

    DeviceType_T bootDevice;
    if (SchemaDefs::MaxBootPosition > 9)
    {
        AssertMsgFailed (("Too many boot devices %d\n",
                          SchemaDefs::MaxBootPosition));
        return VERR_INVALID_PARAMETER;
    }

    for (ULONG pos = 1; pos <= SchemaDefs::MaxBootPosition; pos ++)
    {
        hrc = pMachine->GetBootOrder(pos, &bootDevice);                             H();

        char szParamName[] = "BootDeviceX";
        szParamName[sizeof (szParamName) - 2] = ((char (pos - 1)) + '0');

        const char *pszBootDevice;
        switch (bootDevice)
        {
            case DeviceType_Null:
                pszBootDevice = "NONE";
                break;
            case DeviceType_HardDisk:
                pszBootDevice = "IDE";
                break;
            case DeviceType_DVD:
                pszBootDevice = "DVD";
                break;
            case DeviceType_Floppy:
                pszBootDevice = "FLOPPY";
                break;
            case DeviceType_Network:
                pszBootDevice = "LAN";
                break;
            default:
                AssertMsgFailed(("Invalid bootDevice=%d\n", bootDevice));
                return VMSetError(pVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                  N_("Invalid boot device '%d'"), bootDevice);
        }
        rc = CFGMR3InsertString(pBiosCfg, szParamName, pszBootDevice);              RC_CHECK();
    }

    /*
     * The time offset
     */
    LONG64 timeOffset;
    hrc = biosSettings->COMGETTER(TimeOffset)(&timeOffset);                         H();
    PCFGMNODE pTMNode;
    rc = CFGMR3InsertNode(pRoot, "TM", &pTMNode);                                   RC_CHECK();
    rc = CFGMR3InsertInteger(pTMNode, "UTCOffset", timeOffset * 1000000);           RC_CHECK();

    /*
     * DMA
     */
    rc = CFGMR3InsertNode(pDevices, "8237A", &pDev);                                RC_CHECK();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "Trusted", 1);                  /* boolean */   RC_CHECK();

    /*
     * PCI buses.
     */
    rc = CFGMR3InsertNode(pDevices, "pci", &pDev); /* piix3 */                      RC_CHECK();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               RC_CHECK();
    rc = CFGMR3InsertInteger(pCfg,  "IOAPIC", fIOAPIC);                             RC_CHECK();

#if 0 /* enable this to test PCI bridging */
    rc = CFGMR3InsertNode(pDevices, "pcibridge", &pDev);                            RC_CHECK();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",         14);                     RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                     RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "PCIBusNo",             0);/* -> pci[0] */      RC_CHECK();

    rc = CFGMR3InsertNode(pDev,     "1", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          1);                     RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                     RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "PCIBusNo",             1);/* ->pcibridge[0] */ RC_CHECK();

    rc = CFGMR3InsertNode(pDev,     "2", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          3);                     RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                     RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "PCIBusNo",             1);/* ->pcibridge[0] */ RC_CHECK();
#endif

    /*
     * PS/2 keyboard & mouse.
     */
    rc = CFGMR3InsertNode(pDevices, "pckbd", &pDev);                                RC_CHECK();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               RC_CHECK();

    rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                              RC_CHECK();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "KeyboardQueue");       RC_CHECK();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                               RC_CHECK();
    rc = CFGMR3InsertInteger(pCfg,  "QueueSize",            64);                    RC_CHECK();

    rc = CFGMR3InsertNode(pLunL0,   "AttachedDriver", &pLunL1);                     RC_CHECK();
    rc = CFGMR3InsertString(pLunL1, "Driver",               "MainKeyboard");        RC_CHECK();
    rc = CFGMR3InsertNode(pLunL1,   "Config", &pCfg);                               RC_CHECK();
    Keyboard *pKeyboard = pConsole->mKeyboard;
    rc = CFGMR3InsertInteger(pCfg,  "Object",     (uintptr_t)pKeyboard);            RC_CHECK();

    rc = CFGMR3InsertNode(pInst,    "LUN#1", &pLunL0);                              RC_CHECK();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "MouseQueue");          RC_CHECK();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                               RC_CHECK();
    rc = CFGMR3InsertInteger(pCfg,  "QueueSize",            128);                   RC_CHECK();

    rc = CFGMR3InsertNode(pLunL0,   "AttachedDriver", &pLunL1);                     RC_CHECK();
    rc = CFGMR3InsertString(pLunL1, "Driver",               "MainMouse");           RC_CHECK();
    rc = CFGMR3InsertNode(pLunL1,   "Config", &pCfg);                               RC_CHECK();
    Mouse *pMouse = pConsole->mMouse;
    rc = CFGMR3InsertInteger(pCfg,  "Object",     (uintptr_t)pMouse);               RC_CHECK();

    /*
     * i82078 Floppy drive controller
     */
    ComPtr<IFloppyDrive> floppyDrive;
    hrc = pMachine->COMGETTER(FloppyDrive)(floppyDrive.asOutParam());               H();
    BOOL fFdcEnabled;
    hrc = floppyDrive->COMGETTER(Enabled)(&fFdcEnabled);                            H();
    if (fFdcEnabled)
    {
        rc = CFGMR3InsertNode(pDevices, "i82078",    &pDev);                        RC_CHECK();
        rc = CFGMR3InsertNode(pDev,     "0",         &pInst);                       RC_CHECK();
        rc = CFGMR3InsertInteger(pInst, "Trusted",   1);                            RC_CHECK();
        rc = CFGMR3InsertNode(pInst,    "Config",    &pCfg);                        RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,  "IRQ",       6);                            RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,  "DMA",       2);                            RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,  "MemMapped", 0 );                           RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,  "IOBase",    0x3f0);                        RC_CHECK();

        /* Attach the status driver */
        rc = CFGMR3InsertNode(pInst,    "LUN#999", &pLunL0);                            RC_CHECK();
        rc = CFGMR3InsertString(pLunL0, "Driver",               "MainStatus");          RC_CHECK();
        rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                               RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,  "papLeds", (uintptr_t)&pConsole->mapFDLeds[0]); RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,  "First",    0);                                 RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,  "Last",     0);                                 RC_CHECK();

        rc = CFGMR3InsertNode(pInst,    "LUN#0",     &pLunL0);                          RC_CHECK();

        ComPtr<IFloppyImage2> floppyImage;
        hrc = floppyDrive->GetImage(floppyImage.asOutParam());                      H();
        if (floppyImage)
        {
            pConsole->meFloppyState = DriveState_ImageMounted;
            rc = CFGMR3InsertString(pLunL0, "Driver",    "Block");                  RC_CHECK();
            rc = CFGMR3InsertNode(pLunL0,   "Config",    &pCfg);                    RC_CHECK();
            rc = CFGMR3InsertString(pCfg,   "Type",      "Floppy 1.44");            RC_CHECK();
            rc = CFGMR3InsertInteger(pCfg,  "Mountable", 1);                        RC_CHECK();

            rc = CFGMR3InsertNode(pLunL0,   "AttachedDriver", &pLunL1);             RC_CHECK();
            rc = CFGMR3InsertString(pLunL1, "Driver",          "RawImage");         RC_CHECK();
            rc = CFGMR3InsertNode(pLunL1,   "Config", &pCfg);                       RC_CHECK();
            hrc = floppyImage->COMGETTER(Location)(&str);                           H();
            STR_CONV();
            rc = CFGMR3InsertString(pCfg,   "Path",             psz);               RC_CHECK();
            STR_FREE();
        }
        else
        {
            ComPtr<IHostFloppyDrive> hostFloppyDrive;
            hrc = floppyDrive->GetHostDrive(hostFloppyDrive.asOutParam());          H();
            if (hostFloppyDrive)
            {
                pConsole->meFloppyState = DriveState_HostDriveCaptured;
                rc = CFGMR3InsertString(pLunL0, "Driver",      "HostFloppy");       RC_CHECK();
                rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                   RC_CHECK();
                hrc = hostFloppyDrive->COMGETTER(Name)(&str);                       H();
                STR_CONV();
                rc = CFGMR3InsertString(pCfg,   "Path",         psz);               RC_CHECK();
                STR_FREE();
            }
            else
            {
                pConsole->meFloppyState = DriveState_NotMounted;
                rc = CFGMR3InsertString(pLunL0, "Driver",    "Block");              RC_CHECK();
                rc = CFGMR3InsertNode(pLunL0,   "Config",    &pCfg);                RC_CHECK();
                rc = CFGMR3InsertString(pCfg,   "Type",      "Floppy 1.44");        RC_CHECK();
                rc = CFGMR3InsertInteger(pCfg,  "Mountable", 1);                    RC_CHECK();
            }
        }
    }

    /*
     * ACPI
     */
    BOOL fACPI;
    hrc = biosSettings->COMGETTER(ACPIEnabled)(&fACPI);                             H();
    if (fACPI)
    {
        rc = CFGMR3InsertNode(pDevices, "acpi", &pDev);                             RC_CHECK();
        rc = CFGMR3InsertNode(pDev,     "0", &pInst);                               RC_CHECK();
        rc = CFGMR3InsertInteger(pInst, "Trusted", 1);              /* boolean */   RC_CHECK();
        rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                           RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,  "RamSize",          cRamMBs * _1M);         RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,  "NumCPUs",          cCpus);                 RC_CHECK();

        rc = CFGMR3InsertInteger(pCfg,  "IOAPIC", fIOAPIC);                         RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,  "FdcEnabled", fFdcEnabled);                 RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,  "HpetEnabled", 0);                          RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,  "SmcEnabled", 0);                           RC_CHECK();
        rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          7);                 RC_CHECK();
        Assert(!afPciDeviceNo[7]);
        afPciDeviceNo[7] = true;
        rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                 RC_CHECK();

        rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                          RC_CHECK();
        rc = CFGMR3InsertString(pLunL0, "Driver",               "ACPIHost");        RC_CHECK();
        rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                           RC_CHECK();
    }

    /*
     * i8254 Programmable Interval Timer And Dummy Speaker
     */
    rc = CFGMR3InsertNode(pDevices, "i8254", &pDev);                                RC_CHECK();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               RC_CHECK();
#ifdef DEBUG
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   RC_CHECK();
#endif

    /*
     * i8259 Programmable Interrupt Controller.
     */
    rc = CFGMR3InsertNode(pDevices, "i8259", &pDev);                                RC_CHECK();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               RC_CHECK();

    /*
     * Advanced Programmable Interrupt Controller.
     * SMP: Each CPU has a LAPIC, but we have a single device representing all LAPICs states,
     *      thus only single insert
     */
    rc = CFGMR3InsertNode(pDevices, "apic", &pDev);                                 RC_CHECK();
    rc = CFGMR3InsertNode(pDev, "0", &pInst);                                       RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               RC_CHECK();
    rc = CFGMR3InsertInteger(pCfg,  "IOAPIC", fIOAPIC);                             RC_CHECK();
    rc = CFGMR3InsertInteger(pCfg,  "NumCPUs", cCpus);                              RC_CHECK();

    /* SMP: @todo: IOAPIC may be required for SMP configs */
    if (fIOAPIC)
    {
        /*
         * I/O Advanced Programmable Interrupt Controller.
         */
        rc = CFGMR3InsertNode(pDevices, "ioapic", &pDev);                           RC_CHECK();
        rc = CFGMR3InsertNode(pDev,     "0", &pInst);                               RC_CHECK();
        rc = CFGMR3InsertInteger(pInst, "Trusted",          1);     /* boolean */   RC_CHECK();
        rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                           RC_CHECK();
    }

    /*
     * RTC MC146818.
     */
    rc = CFGMR3InsertNode(pDevices, "mc146818", &pDev);                             RC_CHECK();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               RC_CHECK();

    /*
     * VGA.
     */
    rc = CFGMR3InsertNode(pDevices, "vga", &pDev);                                  RC_CHECK();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          2);                     RC_CHECK();
    Assert(!afPciDeviceNo[2]);
    afPciDeviceNo[2] = true;
    rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                     RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               RC_CHECK();
    hrc = pMachine->COMGETTER(VRAMSize)(&cRamMBs);                                  H();
    rc = CFGMR3InsertInteger(pCfg,  "VRamSize",             cRamMBs * _1M);         RC_CHECK();
#ifdef VBOX_WITH_2X_4GB_ADDR_SPACE /* not safe here yet. */
    rc = CFGMR3InsertInteger(pCfg,  "R0Enabled",            fHWVirtExEnabled);      RC_CHECK();
#endif

    /*
     * BIOS logo
     */
    BOOL fFadeIn;
    hrc = biosSettings->COMGETTER(LogoFadeIn)(&fFadeIn);                            H();
    rc = CFGMR3InsertInteger(pCfg,  "FadeIn",  fFadeIn ? 1 : 0);                    RC_CHECK();
    BOOL fFadeOut;
    hrc = biosSettings->COMGETTER(LogoFadeOut)(&fFadeOut);                          H();
    rc = CFGMR3InsertInteger(pCfg,  "FadeOut", fFadeOut ? 1: 0);                    RC_CHECK();
    ULONG logoDisplayTime;
    hrc = biosSettings->COMGETTER(LogoDisplayTime)(&logoDisplayTime);               H();
    rc = CFGMR3InsertInteger(pCfg,  "LogoTime", logoDisplayTime);                   RC_CHECK();
    Bstr logoImagePath;
    hrc = biosSettings->COMGETTER(LogoImagePath)(logoImagePath.asOutParam());       H();
    rc = CFGMR3InsertString(pCfg,   "LogoFile", logoImagePath ? Utf8Str(logoImagePath) : ""); RC_CHECK();

    /*
     * Boot menu
     */
    BIOSBootMenuMode_T bootMenuMode;
    int value;
    biosSettings->COMGETTER(BootMenuMode)(&bootMenuMode);
    switch (bootMenuMode)
    {
        case BIOSBootMenuMode_Disabled:
            value = 0;
            break;
        case BIOSBootMenuMode_MenuOnly:
            value = 1;
            break;
        default:
            value = 2;
    }
    rc = CFGMR3InsertInteger(pCfg, "ShowBootMenu", value);                          RC_CHECK();

    /* Custom VESA mode list */
    unsigned cModes = 0;
    for (unsigned iMode = 1; iMode <= 16; iMode++)
    {
        char szExtraDataKey[sizeof("CustomVideoModeXX")];
        RTStrPrintf(szExtraDataKey, sizeof(szExtraDataKey), "CustomVideoMode%d", iMode);
        hrc = pMachine->GetExtraData(Bstr(szExtraDataKey), &str);                   H();
        if (!str || !*str)
            break;
        STR_CONV();
        rc = CFGMR3InsertString(pCfg, szExtraDataKey, psz);
        STR_FREE();
        cModes++;
    }
    rc = CFGMR3InsertInteger(pCfg,  "CustomVideoModes", cModes);

    /* VESA height reduction */
    ULONG ulHeightReduction;
    IFramebuffer *pFramebuffer = pConsole->getDisplay()->getFramebuffer();
    if (pFramebuffer)
    {
        hrc = pFramebuffer->COMGETTER(HeightReduction)(&ulHeightReduction);         H();
    }
    else
    {
        /* If framebuffer is not available, there is no height reduction. */
        ulHeightReduction = 0;
    }
    rc = CFGMR3InsertInteger(pCfg,  "HeightReduction", ulHeightReduction);          RC_CHECK();

    /* Attach the display. */
    rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                              RC_CHECK();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "MainDisplay");         RC_CHECK();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                               RC_CHECK();
    Display *pDisplay = pConsole->mDisplay;
    rc = CFGMR3InsertInteger(pCfg,  "Object", (uintptr_t)pDisplay);                 RC_CHECK();

    /*
     * IDE (update this when the main interface changes)
     */
    rc = CFGMR3InsertNode(pDevices, "piix3ide", &pDev); /* piix3 */                 RC_CHECK();
    rc = CFGMR3InsertNode(pDev,     "0", &pIdeInst);                                RC_CHECK();
    rc = CFGMR3InsertInteger(pIdeInst, "Trusted",              1);  /* boolean */   RC_CHECK();
    rc = CFGMR3InsertInteger(pIdeInst, "PCIDeviceNo",          1);                  RC_CHECK();
    Assert(!afPciDeviceNo[1]);
    afPciDeviceNo[1] = true;
    rc = CFGMR3InsertInteger(pIdeInst, "PCIFunctionNo",        1);                  RC_CHECK();
    rc = CFGMR3InsertNode(pIdeInst,    "Config", &pCfg);                            RC_CHECK();
    rc = CFGMR3InsertInteger(pCfg,  "PIIX4", fPIIX4);               /* boolean */   RC_CHECK();

    /* Attach the status driver */
    rc = CFGMR3InsertNode(pIdeInst,    "LUN#999", &pLunL0);                         RC_CHECK();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "MainStatus");          RC_CHECK();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                               RC_CHECK();
    rc = CFGMR3InsertInteger(pCfg,  "papLeds", (uintptr_t)&pConsole->mapIDELeds[0]);RC_CHECK();
    rc = CFGMR3InsertInteger(pCfg,  "First",    0);                                 RC_CHECK();
    rc = CFGMR3InsertInteger(pCfg,  "Last",     3);                                 RC_CHECK();

    /*
     * SATA controller
     */
    ComPtr<ISATAController> sataController;
    hrc = pMachine->COMGETTER(SATAController)(sataController.asOutParam());
    BOOL enabled = FALSE;

    if (sataController)
    {
        hrc = sataController->COMGETTER(Enabled)(&enabled);                         H();

        if (enabled)
        {
            rc = CFGMR3InsertNode(pDevices, "ahci", &pDev);                         RC_CHECK();
            rc = CFGMR3InsertNode(pDev,     "0", &pSataInst);                       RC_CHECK();
            rc = CFGMR3InsertInteger(pSataInst, "Trusted",              1);         RC_CHECK();
            rc = CFGMR3InsertInteger(pSataInst, "PCIDeviceNo",          13);        RC_CHECK();
            Assert(!afPciDeviceNo[13]);
            afPciDeviceNo[13] = true;
            rc = CFGMR3InsertInteger(pSataInst, "PCIFunctionNo",        0);         RC_CHECK();
            rc = CFGMR3InsertNode(pSataInst,    "Config", &pCfg);                   RC_CHECK();

            ULONG cPorts = 0;
            hrc = sataController->COMGETTER(PortCount)(&cPorts);                    H();
            rc = CFGMR3InsertInteger(pCfg, "PortCount", cPorts);                    RC_CHECK();

            /* Needed configuration values for the bios. */
            rc = CFGMR3InsertString(pBiosCfg, "SataHardDiskDevice", "ahci");        RC_CHECK();

            for (uint32_t i = 0; i < 4; i++)
            {
                static const char *s_apszConfig[4] =
                { "PrimaryMaster", "PrimarySlave", "SecondaryMaster", "SecondarySlave" };
                static const char *s_apszBiosConfig[4] =
                { "SataPrimaryMasterLUN", "SataPrimarySlaveLUN", "SataSecondaryMasterLUN", "SataSecondarySlaveLUN" };

                LONG lPortNumber = -1;
                hrc = sataController->GetIDEEmulationPort(i, &lPortNumber);             H();
                rc = CFGMR3InsertInteger(pCfg, s_apszConfig[i], lPortNumber);           RC_CHECK();
                rc = CFGMR3InsertInteger(pBiosCfg, s_apszBiosConfig[i], lPortNumber);   RC_CHECK();
            }

            /* Attach the status driver */
            rc = CFGMR3InsertNode(pSataInst,"LUN#999", &pLunL0);                              RC_CHECK();
            rc = CFGMR3InsertString(pLunL0, "Driver",               "MainStatus");            RC_CHECK();
            rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                                 RC_CHECK();
            AssertRelease(cPorts <= RT_ELEMENTS(pConsole->mapSATALeds));
            rc = CFGMR3InsertInteger(pCfg,  "papLeds", (uintptr_t)&pConsole->mapSATALeds[0]); RC_CHECK();
            rc = CFGMR3InsertInteger(pCfg,  "First",    0);                                   RC_CHECK();
            rc = CFGMR3InsertInteger(pCfg,  "Last",     cPorts - 1);                          RC_CHECK();
        }
    }

    /* Attach the hard disks */
    {
        com::SafeIfaceArray <IHardDisk2Attachment> atts;
        hrc = pMachine->
            COMGETTER(HardDisk2Attachments) (ComSafeArrayAsOutParam (atts));    H();

        for (size_t i = 0; i < atts.size(); ++ i)
        {
            ComPtr <IHardDisk2> hardDisk;
            hrc = atts [i]->COMGETTER(HardDisk) (hardDisk.asOutParam());        H();
            StorageBus_T enmBus;
            hrc = atts [i]->COMGETTER(Bus) (&enmBus);                           H();
            LONG lDev;
            hrc = atts [i]->COMGETTER(Device) (&lDev);                          H();
            LONG lChannel;
            hrc = atts [i]->COMGETTER(Channel) (&lChannel);                     H();

            PCFGMNODE pHardDiskCtl = NULL;
            int iLUN = 0;

            switch (enmBus)
            {
                case StorageBus_IDE:
                {
                    if (lChannel >= 2 || lChannel < 0)
                    {
                        AssertMsgFailed (("invalid controller channel number: "
                                          "%d\n", lChannel));
                        return VERR_GENERAL_FAILURE;
                    }

                    if (lDev >= 2 || lDev < 0)
                    {
                        AssertMsgFailed (("invalid controller device number: "
                                          "%d\n", lDev));
                        return VERR_GENERAL_FAILURE;
                    }

                    iLUN = 2 * lChannel + lDev;
                    pHardDiskCtl = pIdeInst;

                    break;
                }
                case StorageBus_SATA:
                {
                    iLUN = lChannel;
                    pHardDiskCtl = enabled ? pSataInst : NULL;
                    break;
                }
                default:
                {
                    AssertMsgFailed (("invalid disk controller type: "
                                      "%d\n", enmBus));
                    return VERR_GENERAL_FAILURE;
                }
            }

            /* Can be NULL if SATA controller is not enabled and current hard
             * disk is attached to SATA controller. */
            if (pHardDiskCtl == NULL)
                continue;

            char szLUN[16];
            RTStrPrintf (szLUN, sizeof(szLUN), "LUN#%d", iLUN);

            rc = CFGMR3InsertNode (pHardDiskCtl, szLUN, &pLunL0);               RC_CHECK();
            rc = CFGMR3InsertString (pLunL0, "Driver", "Block");                RC_CHECK();
            rc = CFGMR3InsertNode (pLunL0, "Config", &pCfg);                    RC_CHECK();
            rc = CFGMR3InsertString (pCfg, "Type", "HardDisk");                 RC_CHECK();
            rc = CFGMR3InsertInteger (pCfg, "Mountable", 0);                    RC_CHECK();

            rc = CFGMR3InsertNode (pLunL0, "AttachedDriver", &pLunL1);          RC_CHECK();
            rc = CFGMR3InsertString (pLunL1, "Driver", "VD");                   RC_CHECK();
            rc = CFGMR3InsertNode (pLunL1, "Config", &pCfg);                    RC_CHECK();

            hrc = hardDisk->COMGETTER(Location) (bstr.asOutParam());            H();
            rc = CFGMR3InsertString (pCfg, "Path", Utf8Str (bstr));             RC_CHECK();

            hrc = hardDisk->COMGETTER(Format) (bstr.asOutParam());              H();
            rc = CFGMR3InsertString (pCfg, "Format", Utf8Str (bstr));           RC_CHECK();

#if defined(VBOX_WITH_PDM_ASYNC_COMPLETION)
            if (bstr == L"VMDK")
            {
                /* Create cfgm nodes for async transport driver because VMDK is
                 * currently the only one which may support async I/O. This has
                 * to be made generic based on the capabiliy flags when the new
                 * HardDisk interface is merged.
                 */
                rc = CFGMR3InsertNode (pLunL1, "AttachedDriver", &pLunL2);      RC_CHECK();
                rc = CFGMR3InsertString (pLunL2, "Driver", "TransportAsync");   RC_CHECK();
                /* The async transport driver has no config options yet. */
            }
#endif
            /* Pass all custom parameters. */
            bool fHostIP = true;
            SafeArray <BSTR> names;
            SafeArray <BSTR> values;
            hrc = hardDisk->GetProperties (NULL,
                                           ComSafeArrayAsOutParam (names),
                                           ComSafeArrayAsOutParam (values));    H();

            if (names.size() != 0)
            {
                PCFGMNODE pVDC;
                rc = CFGMR3InsertNode (pCfg, "VDConfig", &pVDC);                RC_CHECK();
                for (size_t i = 0; i < names.size(); ++ i)
                {
                    if (values [i])
                    {
                        Utf8Str name = names [i];
                        Utf8Str value = values [i];
                        rc = CFGMR3InsertString (pVDC, name, value);
                        if (    !(name.compare("HostIPStack"))
                            &&  !(value.compare("0")))
                            fHostIP = false;
                    }
                }
            }

            /* Create an inversed tree of parents. */
            ComPtr <IHardDisk2> parentHardDisk = hardDisk;
            for (PCFGMNODE pParent = pCfg;;)
            {
                hrc = parentHardDisk->
                    COMGETTER(Parent) (hardDisk.asOutParam());                  H();
                if (hardDisk.isNull())
                    break;

                PCFGMNODE pCur;
                rc = CFGMR3InsertNode (pParent, "Parent", &pCur);               RC_CHECK();
                hrc = hardDisk->COMGETTER(Location) (bstr.asOutParam());        H();
                rc = CFGMR3InsertString (pCur, "Path", Utf8Str (bstr));         RC_CHECK();

                hrc = hardDisk->COMGETTER(Format) (bstr.asOutParam());          H();
                rc = CFGMR3InsertString (pCur, "Format", Utf8Str (bstr));       RC_CHECK();

                /* Pass all custom parameters. */
                SafeArray <BSTR> names;
                SafeArray <BSTR> values;
                hrc = hardDisk->GetProperties (NULL,
                                               ComSafeArrayAsOutParam (names),
                                               ComSafeArrayAsOutParam (values));H();

                if (names.size() != 0)
                {
                    PCFGMNODE pVDC;
                    rc = CFGMR3InsertNode (pCur, "VDConfig", &pVDC);            RC_CHECK();
                    for (size_t i = 0; i < names.size(); ++ i)
                    {
                        if (values [i])
                        {
                            Utf8Str name = names [i];
                            Utf8Str value = values [i];
                            rc = CFGMR3InsertString (pVDC, name, value);
                            if (    !(name.compare("HostIPStack"))
                                &&  !(value.compare("0")))
                                fHostIP = false;
                        }
                    }
                }

                /* Custom code: put marker to not use host IP stack to driver
                 * configuration node. Simplifies life of DrvVD a bit. */
                if (!fHostIP)
                {
                    rc = CFGMR3InsertInteger (pCfg, "HostIPStack", 0);          RC_CHECK();
                }

                /* next */
                pParent = pCur;
                parentHardDisk = hardDisk;
            }
        }
    }
    H();

    ComPtr<IDVDDrive> dvdDrive;
    hrc = pMachine->COMGETTER(DVDDrive)(dvdDrive.asOutParam());                     H();
    if (dvdDrive)
    {
        // ASSUME: DVD drive is always attached to LUN#2 (i.e. secondary IDE master)
        rc = CFGMR3InsertNode(pIdeInst,    "LUN#2", &pLunL0);                       RC_CHECK();
        ComPtr<IHostDVDDrive> hostDvdDrive;
        hrc = dvdDrive->GetHostDrive(hostDvdDrive.asOutParam());                    H();
        if (hostDvdDrive)
        {
            pConsole->meDVDState = DriveState_HostDriveCaptured;
            rc = CFGMR3InsertString(pLunL0, "Driver",      "HostDVD");              RC_CHECK();
            rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                       RC_CHECK();
            hrc = hostDvdDrive->COMGETTER(Name)(&str);                              H();
            STR_CONV();
            rc = CFGMR3InsertString(pCfg,   "Path",         psz);                   RC_CHECK();
            STR_FREE();
            BOOL fPassthrough;
            hrc = dvdDrive->COMGETTER(Passthrough)(&fPassthrough);                  H();
            rc = CFGMR3InsertInteger(pCfg,  "Passthrough",  !!fPassthrough);        RC_CHECK();
        }
        else
        {
            pConsole->meDVDState = DriveState_NotMounted;
            rc = CFGMR3InsertString(pLunL0, "Driver",               "Block");       RC_CHECK();
            rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                       RC_CHECK();
            rc = CFGMR3InsertString(pCfg,   "Type",                 "DVD");         RC_CHECK();
            rc = CFGMR3InsertInteger(pCfg,  "Mountable",            1);             RC_CHECK();

            ComPtr<IDVDImage2> dvdImage;
            hrc = dvdDrive->GetImage(dvdImage.asOutParam());                        H();
            if (dvdImage)
            {
                pConsole->meDVDState = DriveState_ImageMounted;
                rc = CFGMR3InsertNode(pLunL0,   "AttachedDriver", &pLunL1);         RC_CHECK();
                rc = CFGMR3InsertString(pLunL1, "Driver",          "MediaISO");     RC_CHECK();
                rc = CFGMR3InsertNode(pLunL1,   "Config", &pCfg);                   RC_CHECK();
                hrc = dvdImage->COMGETTER(Location)(&str);                          H();
                STR_CONV();
                rc = CFGMR3InsertString(pCfg,   "Path",             psz);           RC_CHECK();
                STR_FREE();
            }
        }
    }

    /*
     * Network adapters
     */
    PCFGMNODE pDevPCNet = NULL;          /* PCNet-type devices */
    rc = CFGMR3InsertNode(pDevices, "pcnet", &pDevPCNet);                           RC_CHECK();
#ifdef VBOX_WITH_E1000
    PCFGMNODE pDevE1000 = NULL;          /* E1000-type devices */
    rc = CFGMR3InsertNode(pDevices, "e1000", &pDevE1000);                           RC_CHECK();
#endif
    for (ULONG ulInstance = 0; ulInstance < SchemaDefs::NetworkAdapterCount; ulInstance++)
    {
        ComPtr<INetworkAdapter> networkAdapter;
        hrc = pMachine->GetNetworkAdapter(ulInstance, networkAdapter.asOutParam()); H();
        BOOL fEnabled = FALSE;
        hrc = networkAdapter->COMGETTER(Enabled)(&fEnabled);                        H();
        if (!fEnabled)
            continue;

        /*
         * The virtual hardware type. Create appropriate device first.
         */
        NetworkAdapterType_T adapterType;
        hrc = networkAdapter->COMGETTER(AdapterType)(&adapterType);                 H();
        switch (adapterType)
        {
            case NetworkAdapterType_Am79C970A:
            case NetworkAdapterType_Am79C973:
                pDev = pDevPCNet;
                break;
#ifdef VBOX_WITH_E1000
            case NetworkAdapterType_I82540EM:
            case NetworkAdapterType_I82543GC:
                pDev = pDevE1000;
                break;
#endif
            default:
                AssertMsgFailed(("Invalid network adapter type '%d' for slot '%d'",
                                 adapterType, ulInstance));
                return VMSetError(pVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                  N_("Invalid network adapter type '%d' for slot '%d'"),
                                  adapterType, ulInstance);
        }

        char szInstance[4]; Assert(ulInstance <= 999);
        RTStrPrintf(szInstance, sizeof(szInstance), "%lu", ulInstance);
        rc = CFGMR3InsertNode(pDev, szInstance, &pInst);                            RC_CHECK();
        rc = CFGMR3InsertInteger(pInst, "Trusted",              1); /* boolean */   RC_CHECK();
        /* the first network card gets the PCI ID 3, the next 3 gets 8..10,
         * next 4 get 16..19. */
        unsigned iPciDeviceNo = 3;
        if (ulInstance)
        {
            if (ulInstance < 4)
                iPciDeviceNo = ulInstance - 1 + 8;
            else
                iPciDeviceNo = ulInstance - 4 + 16;
        }
        rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo", iPciDeviceNo);               RC_CHECK();
        Assert(!afPciDeviceNo[iPciDeviceNo]);
        afPciDeviceNo[iPciDeviceNo] = true;
        rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                 RC_CHECK();
        rc = CFGMR3InsertNode(pInst, "Config", &pCfg);                              RC_CHECK();
#ifdef VBOX_WITH_2X_4GB_ADDR_SPACE /* not safe here yet. */
        if (pDev == pDevPCNet)
        {
            rc = CFGMR3InsertInteger(pCfg,  "R0Enabled",    false);                 RC_CHECK();
        }
#endif

        /*
         * The virtual hardware type. PCNet supports two types.
         */
        switch (adapterType)
        {
            case NetworkAdapterType_Am79C970A:
                rc = CFGMR3InsertInteger(pCfg, "Am79C973", 0);                      RC_CHECK();
                break;
            case NetworkAdapterType_Am79C973:
                rc = CFGMR3InsertInteger(pCfg, "Am79C973", 1);                      RC_CHECK();
                break;
            case NetworkAdapterType_I82540EM:
                rc = CFGMR3InsertInteger(pCfg, "AdapterType", 0);                   RC_CHECK();
                break;
            case NetworkAdapterType_I82543GC:
                rc = CFGMR3InsertInteger(pCfg, "AdapterType", 1);                   RC_CHECK();
                break;
        }

        /*
         * Get the MAC address and convert it to binary representation
         */
        Bstr macAddr;
        hrc = networkAdapter->COMGETTER(MACAddress)(macAddr.asOutParam());          H();
        Assert(macAddr);
        Utf8Str macAddrUtf8 = macAddr;
        char *macStr = (char*)macAddrUtf8.raw();
        Assert(strlen(macStr) == 12);
        RTMAC Mac;
        memset(&Mac, 0, sizeof(Mac));
        char *pMac = (char*)&Mac;
        for (uint32_t i = 0; i < 6; i++)
        {
            char c1 = *macStr++ - '0';
            if (c1 > 9)
                c1 -= 7;
            char c2 = *macStr++ - '0';
            if (c2 > 9)
                c2 -= 7;
            *pMac++ = ((c1 & 0x0f) << 4) | (c2 & 0x0f);
        }
        rc = CFGMR3InsertBytes(pCfg, "MAC", &Mac, sizeof(Mac));                     RC_CHECK();

        /*
         * Check if the cable is supposed to be unplugged
         */
        BOOL fCableConnected;
        hrc = networkAdapter->COMGETTER(CableConnected)(&fCableConnected);          H();
        rc = CFGMR3InsertInteger(pCfg, "CableConnected", fCableConnected ? 1 : 0);  RC_CHECK();

        /*
         * Line speed to report from custom drivers
         */
        ULONG ulLineSpeed;
        hrc = networkAdapter->COMGETTER(LineSpeed)(&ulLineSpeed);                   H();
        rc = CFGMR3InsertInteger(pCfg, "LineSpeed", ulLineSpeed);                   RC_CHECK();

        /*
         * Attach the status driver.
         */
        rc = CFGMR3InsertNode(pInst,    "LUN#999", &pLunL0);                        RC_CHECK();
        rc = CFGMR3InsertString(pLunL0, "Driver",               "MainStatus");      RC_CHECK();
        rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                           RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,  "papLeds", (uintptr_t)&pConsole->mapNetworkLeds[ulInstance]); RC_CHECK();

        /*
         * Enable the packet sniffer if requested.
         */
        BOOL fSniffer;
        hrc = networkAdapter->COMGETTER(TraceEnabled)(&fSniffer);                   H();
        if (fSniffer)
        {
            /* insert the sniffer filter driver. */
            rc = CFGMR3InsertNode(pInst, "LUN#0", &pLunL0);                         RC_CHECK();
            rc = CFGMR3InsertString(pLunL0, "Driver", "NetSniffer");                RC_CHECK();
            rc = CFGMR3InsertNode(pLunL0, "Config", &pCfg);                         RC_CHECK();
            hrc = networkAdapter->COMGETTER(TraceFile)(&str);                       H();
            if (str) /* check convention for indicating default file. */
            {
                STR_CONV();
                rc = CFGMR3InsertString(pCfg, "File", psz);                         RC_CHECK();
                STR_FREE();
            }
        }

        NetworkAttachmentType_T networkAttachment;
        hrc = networkAdapter->COMGETTER(AttachmentType)(&networkAttachment);        H();
        switch (networkAttachment)
        {
            case NetworkAttachmentType_Null:
                break;

            case NetworkAttachmentType_NAT:
            {
                if (fSniffer)
                {
                    rc = CFGMR3InsertNode(pLunL0, "AttachedDriver", &pLunL0);       RC_CHECK();
                }
                else
                {
                    rc = CFGMR3InsertNode(pInst, "LUN#0", &pLunL0);                 RC_CHECK();
                }
                rc = CFGMR3InsertString(pLunL0, "Driver", "NAT");                   RC_CHECK();
                rc = CFGMR3InsertNode(pLunL0, "Config", &pCfg);                     RC_CHECK();
                /* (Port forwarding goes here.) */

                /* Configure TFTP prefix and boot filename. */
                hrc = virtualBox->COMGETTER(HomeFolder)(&str);                      H();
                STR_CONV();
                if (psz && *psz)
                {
                    char *pszTFTPPrefix = NULL;
                    RTStrAPrintf(&pszTFTPPrefix, "%s%c%s", psz, RTPATH_DELIMITER, "TFTP");
                    rc = CFGMR3InsertString(pCfg, "TFTPPrefix", pszTFTPPrefix);     RC_CHECK();
                    RTStrFree(pszTFTPPrefix);
                }
                STR_FREE();
                hrc = pMachine->COMGETTER(Name)(&str);                              H();
                STR_CONV();
                char *pszBootFile = NULL;
                RTStrAPrintf(&pszBootFile, "%s.pxe", psz);
                STR_FREE();
                rc = CFGMR3InsertString(pCfg, "BootFile", pszBootFile);             RC_CHECK();
                RTStrFree(pszBootFile);

                hrc = networkAdapter->COMGETTER(NATNetwork)(&str);                  H();
                if (str)
                {
                    STR_CONV();
                    if (psz && *psz)
                        rc = CFGMR3InsertString(pCfg, "Network", psz);              RC_CHECK();
                    STR_FREE();
                }
                break;
            }

            case NetworkAttachmentType_HostInterface:
            {
                /*
                 * Perform the attachment if required (don't return on error!)
                 */
                hrc = pConsole->attachToHostInterface(networkAdapter);
                if (SUCCEEDED(hrc))
                {
#if defined(VBOX_WITH_NETFLT)
                    /*
                     * This is the new VBoxNetFlt+IntNet stuff.
                     */
                    if (fSniffer)
                    {
                        rc = CFGMR3InsertNode(pLunL0, "AttachedDriver", &pLunL0);   RC_CHECK();
                    }
                    else
                    {
                        rc = CFGMR3InsertNode(pInst, "LUN#0", &pLunL0);             RC_CHECK();
                    }

                    Bstr HifName;
                    hrc = networkAdapter->COMGETTER(HostInterface)(HifName.asOutParam());
                    if(FAILED(hrc))
                    {
                        LogRel(("NetworkAttachmentType_HostInterface: COMGETTER(HostInterface) failed, hrc (0x%x)", hrc));
                        H();
                    }

                    Utf8Str HifNameUtf8(HifName);
                    const char *pszHifName = HifNameUtf8.raw();

# if defined(RT_OS_DARWIN)
                    /* The name is on the form 'ifX: long name', chop it off at the colon. */
                    char szTrunk[8];
                    strncpy(szTrunk, pszHifName, sizeof(szTrunk));
                    char *pszColon = (char *)memchr(szTrunk, ':', sizeof(szTrunk));
                    if (!pszColon)
                    {
                        hrc = networkAdapter->Detach();                             H();
                        return VMSetError(pVM, VERR_INTERNAL_ERROR, RT_SRC_POS,
                                          N_("Malformed host interface networking name '%ls'"),
                                          HifName.raw());
                    }
                    *pszColon = '\0';
                    const char *pszTrunk = szTrunk;

# elif defined(RT_OS_SOLARIS)
                    /* The name is on the form format 'ifX[:1] - long name, chop it off at space. */
                    char szTrunk[256];
                    strlcpy(szTrunk, pszHifName, sizeof(szTrunk));
                    char *pszSpace = (char *)memchr(szTrunk, ' ', sizeof(szTrunk));

                    /*
                     * Currently don't bother about malformed names here for the sake of people using
                     * VBoxManage and setting only the NIC name from there. If there is a space we
                     * chop it off and proceed, otherwise just use whatever we've got.
                     */
                    if (pszSpace)
                        *pszSpace = '\0';

                    /* Chop it off at the colon (zone naming eg: e1000g:1 we need only the e1000g) */
                    char *pszColon = (char *)memchr(szTrunk, ':', sizeof(szTrunk));
                    if (pszColon)
                        *pszColon = '\0';

                    const char *pszTrunk = szTrunk;

# elif defined(RT_OS_WINDOWS)
                    com::SafeIfaceArray <IHostNetworkInterface> hostNetworkInterfaces;
                    hrc = host->COMGETTER(NetworkInterfaces) (ComSafeArrayAsOutParam (hostNetworkInterfaces));
                    if(FAILED(hrc))
                    {
                        LogRel(("NetworkAttachmentType_HostInterface: COMGETTER(NetworkInterfaces) failed, hrc (0x%x)", hrc));
                        H();
                    }
                    ComPtr<IHostNetworkInterface> hostInterface;
                    for (size_t i = 0; i < hostNetworkInterfaces.size(); ++i)
                    {
                        Bstr name;
                        hostNetworkInterfaces[i]->COMGETTER(Name) (name.asOutParam());
                        if (name == HifName)
                        {
                            hostInterface = hostNetworkInterfaces[i];
                            break;
                        }
                    }
                    if (hostInterface.isNull())
                    {
                        AssertBreakpoint();
                        LogRel(("NetworkAttachmentType_HostInterface: FindByName failed, rc (0x%x)", rc));
                        return VMSetError(pVM, VERR_INTERNAL_ERROR, RT_SRC_POS,
                                          N_("Inexistent host networking interface, name '%ls'"),
                                          HifName.raw());
                    }

                    Guid hostIFGuid;
                    hrc = hostInterface->COMGETTER(Id)(hostIFGuid.asOutParam());
                    if(FAILED(hrc))
                    {
                        LogRel(("NetworkAttachmentType_HostInterface: COMGETTER(Id) failed, hrc (0x%x)", hrc));
                        H();
                    }
                    char szDriverGUID[RTUUID_STR_LENGTH];
                    strcpy(szDriverGUID , hostIFGuid.toString().raw());
                    const char *pszTrunk = szDriverGUID;
# elif defined(RT_OS_LINUX)
                    /* @todo Check for malformed names. */
                    const char *pszTrunk = pszHifName;

# else
#  error "PORTME (VBOX_WITH_NETFLT)"
# endif

                    rc = CFGMR3InsertString(pLunL0, "Driver", "IntNet");            RC_CHECK();
                    rc = CFGMR3InsertNode(pLunL0, "Config", &pCfg);                 RC_CHECK();
                    rc = CFGMR3InsertString(pCfg, "Trunk", pszTrunk);               RC_CHECK();
                    rc = CFGMR3InsertInteger(pCfg, "TrunkType", kIntNetTrunkType_NetFlt); RC_CHECK();
                    char szNetwork[80];
                    RTStrPrintf(szNetwork, sizeof(szNetwork), "HostInterfaceNetworking-%s", pszHifName);
                    rc = CFGMR3InsertString(pCfg, "Network", szNetwork);            RC_CHECK();

# if defined(RT_OS_DARWIN)
                    /** @todo Come up with a better deal here. Problem is that IHostNetworkInterface is completely useless here. */
                    if (    strstr(pszHifName, "Wireless")
                        ||  strstr(pszHifName, "AirPort" ))
                    {
                        rc = CFGMR3InsertInteger(pCfg, "SharedMacOnWire", true);    RC_CHECK();
                    }
# elif defined(RT_OS_LINUX)
                    int iSock = socket(AF_INET, SOCK_DGRAM, 0);
                    if (iSock >= 0)
                    {
                        struct iwreq WRq;

                        memset(&WRq, 0, sizeof(WRq));
                        strncpy(WRq.ifr_name, pszHifName, IFNAMSIZ);
                        if (ioctl(iSock, SIOCGIWNAME, &WRq) >= 0)
                        {
                            rc = CFGMR3InsertInteger(pCfg, "SharedMacOnWire", true);    RC_CHECK();
                            Log(("Set SharedMacOnWire\n"));
                        }
                        else
                        {
                            Log(("Failed to get wireless name\n"));
                        }
                        close(iSock);
                    }
                    else
                    {
                        Log(("Failed to open wireless socket\n"));
                    }
# elif defined(RT_OS_WINDOWS)
#  define DEVNAME_PREFIX L"\\\\.\\"
                    INetCfg              *pNc;
                    LPWSTR               lpszApp;
                    HRESULT              hr;
                    int rc = VERR_INTNET_FLT_IF_NOT_FOUND;

                    /* we are getting the medium type via IOCTL_NDIS_QUERY_GLOBAL_STATS Io Control
                     * there is a pretty long way till there though since we need to obtain the symbolic link name
                     * for the adapter device we are going to query given the device Guid */
                    hr = VBoxNetCfgWinQueryINetCfg( FALSE,
                                       L"VirtualBox",
                                       &pNc,
                                       &lpszApp );
                    Assert(hr == S_OK);
                    if(hr == S_OK)
                    {
                        /* get the adapter's INetCfgComponent*/
                        INetCfgComponent *pAdaptorComponent;
                        hr = VBoxNetCfgWinGetComponentByGuid(pNc, &GUID_DEVCLASS_NET, (GUID*)hostIFGuid.ptr(), &pAdaptorComponent);
                        Assert(hr == S_OK);
                        if(hr == S_OK)
                        {
                            /* now get the bind name */
                            LPWSTR pName;
                            hr = pAdaptorComponent->GetBindName(&pName);
                            Assert(hr == S_OK);
                            if(hr == S_OK)
                            {
                                /* prepend the "\\\\.\\" to the bind name to obtain the link name */
                                wchar_t FileName[MAX_PATH];
                                wcscpy(FileName, DEVNAME_PREFIX);
                                wcscpy((wchar_t*)(((char*)FileName) + sizeof(DEVNAME_PREFIX) - sizeof(FileName[0])), pName);

                                /* open the device */
                                HANDLE hDevice = CreateFile(FileName,
                                                            GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                            NULL,
                                                            OPEN_EXISTING,
                                                            FILE_ATTRIBUTE_NORMAL,
                                                            NULL);
                                if (hDevice != INVALID_HANDLE_VALUE)
                                {
                                    /* now issue the OID_GEN_PHYSICAL_MEDIUM query */
                                    DWORD Oid = OID_GEN_PHYSICAL_MEDIUM;
                                    NDIS_PHYSICAL_MEDIUM PhMedium;
                                    DWORD cbResult;
                                    if (DeviceIoControl(hDevice, IOCTL_NDIS_QUERY_GLOBAL_STATS, &Oid, sizeof(Oid), &PhMedium, sizeof(PhMedium), &cbResult, NULL))
                                    {
                                        /* that was simple, now examine PhMedium */
                                        if(PhMedium == NdisPhysicalMediumWirelessWan
                                                || PhMedium == NdisPhysicalMediumWirelessLan
                                                || PhMedium == NdisPhysicalMediumNative802_11
                                                || PhMedium == NdisPhysicalMediumBluetooth
                                                /*|| PhMedium == NdisPhysicalMediumWiMax*/
                                                )
                                        {
                                            Log(("this is a wireles adapter"));
                                            rc = CFGMR3InsertInteger(pCfg, "SharedMacOnWire", true);    RC_CHECK();
                                                                        Log(("Set SharedMacOnWire\n"));
                                        }
                                        else
                                        {
                                            Log(("this is NOT a wireles adapter"));
                                        }
                                    }
                                    else
                                    {
                                        int winEr = GetLastError();
                                        LogRel(("Console::configConstructor: DeviceIoControl failed, err (0x%x), ignoring\n", winEr));
                                        Assert(winEr == ERROR_INVALID_PARAMETER || winEr == ERROR_NOT_SUPPORTED || winEr == ERROR_BAD_COMMAND);
                                    }

                                    CloseHandle(hDevice);
                                }
                                else
                                {
                                    int winEr = GetLastError();
                                    LogRel(("Console::configConstructor: CreateFile failed, err (0x%x), ignoring\n", winEr));
                                    AssertBreakpoint();
                                }
                                CoTaskMemFree(pName);
                            }
                            VBoxNetCfgWinReleaseRef(pAdaptorComponent);
                        }
                        VBoxNetCfgWinReleaseINetCfg( pNc, FALSE );
                    }
# else
                    /** @todo PORTME: wireless detection */
# endif

# if defined(RT_OS_SOLARIS)
#  if 0 /* bird: this is a bit questionable and might cause more trouble than its worth.  */
                    /* Zone access restriction, don't allow snopping the global zone. */
                    zoneid_t ZoneId = getzoneid();
                    if (ZoneId != GLOBAL_ZONEID)
                    {
                        rc = CFGMR3InsertInteger(pCfg, "IgnoreAllPromisc", true);   RC_CHECK();
                    }
#  endif
# endif

#elif defined(RT_OS_WINDOWS)
                    if (fSniffer)
                    {
                        rc = CFGMR3InsertNode(pLunL0, "AttachedDriver", &pLunL0);   RC_CHECK();
                    }
                    else
                    {
                        rc = CFGMR3InsertNode(pInst, "LUN#0", &pLunL0);             RC_CHECK();
                    }
                    Bstr hostInterfaceName;
                    hrc = networkAdapter->COMGETTER(HostInterface)(hostInterfaceName.asOutParam()); H();
                    com::SafeIfaceArray <IHostNetworkInterface> hostNetworkInterfaces;
                    hrc = host->COMGETTER(NetworkInterfaces) (ComSafeArrayAsOutParam (hostNetworkInterfaces)); H();
                    ComPtr<IHostNetworkInterface> hostInterface;
                    for (size_t i = 0; i < hostNetworkInterfaces.size(); ++i)
                    {
                        Bstr name;
                        hostNetworkInterfaces[i]->COMGETTER(Name) (name.asOutParam());
                        if (name == hostInterfaceName)
                        {
                            hostInterface = hostNetworkInterfaces[i];
                            break;
                        }
                    }
                    if (hostInterface.isNull())
                    {
                        AssertMsgFailed(("Cannot get GUID for host interface '%ls'\n", hostInterfaceName));
                        hrc = networkAdapter->Detach();                             H();
                    }
                    else
                    {
# ifndef VBOX_WITH_NETFLT
                        rc = CFGMR3InsertString(pLunL0, "Driver", "HostInterface");                     RC_CHECK();
                        rc = CFGMR3InsertNode(pLunL0, "Config", &pCfg);                                 RC_CHECK();
                        rc = CFGMR3InsertString(pCfg, "HostInterfaceName", Utf8Str(hostInterfaceName)); RC_CHECK();
# else
                        rc = CFGMR3InsertString(pLunL0, "Driver", "IntNet");                            RC_CHECK();
                        rc = CFGMR3InsertNode(pLunL0, "Config", &pCfg);                                 RC_CHECK();
                        rc = CFGMR3InsertString(pCfg, "Trunk", Utf8Str(hostInterfaceName));             RC_CHECK();
                        rc = CFGMR3InsertInteger(pCfg, "TrunkType", kIntNetTrunkType_NetFlt);           RC_CHECK();
# endif
                        Guid hostIFGuid;
                        hrc = hostInterface->COMGETTER(Id)(hostIFGuid.asOutParam());                    H();
                        char szDriverGUID[256] = {0};
                        /* add curly brackets */
                        szDriverGUID[0] = '{';
                        strcpy(szDriverGUID + 1, hostIFGuid.toString().raw());
                        strcat(szDriverGUID, "}");
                        rc = CFGMR3InsertBytes(pCfg, "GUID", szDriverGUID, sizeof(szDriverGUID));       RC_CHECK();
                    }
#elif defined(RT_OS_LINUX)
/// @todo aleksey: is there anything to be done here?
#elif defined(RT_OS_FREEBSD)
/** @todo FreeBSD: Check out this later (HIF networking). */
#else
# error "Port me"
#endif
                }
                else
                {
                    switch (hrc)
                    {
#ifdef RT_OS_LINUX
                        case VERR_ACCESS_DENIED:
                            return VMSetError(pVM, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,  N_(
                                             "Failed to open '/dev/net/tun' for read/write access. Please check the "
                                             "permissions of that node. Either run 'chmod 0666 /dev/net/tun' or "
                                             "change the group of that node and make yourself a member of that group. Make "
                                             "sure that these changes are permanent, especially if you are "
                                             "using udev"));
#endif /* RT_OS_LINUX */
                        default:
                            AssertMsgFailed(("Could not attach to host interface! Bad!\n"));
                            return VMSetError(pVM, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS, N_(
                                             "Failed to initialize Host Interface Networking"));
                    }
                }
                break;
            }

            case NetworkAttachmentType_Internal:
            {
                hrc = networkAdapter->COMGETTER(InternalNetwork)(&str);             H();
                if (str)
                {
                    STR_CONV();
                    if (psz && *psz)
                    {
                        if (fSniffer)
                        {
                            rc = CFGMR3InsertNode(pLunL0, "AttachedDriver", &pLunL0);   RC_CHECK();
                        }
                        else
                        {
                            rc = CFGMR3InsertNode(pInst, "LUN#0", &pLunL0);         RC_CHECK();
                        }
                        rc = CFGMR3InsertString(pLunL0, "Driver", "IntNet");        RC_CHECK();
                        rc = CFGMR3InsertNode(pLunL0, "Config", &pCfg);             RC_CHECK();
                        rc = CFGMR3InsertString(pCfg, "Network", psz);              RC_CHECK();
                    }
                    STR_FREE();
                }
                break;
            }

            default:
                AssertMsgFailed(("should not get here!\n"));
                break;
        }
    }

    /*
     * Serial (UART) Ports
     */
    rc = CFGMR3InsertNode(pDevices, "serial", &pDev);                               RC_CHECK();
    for (ULONG ulInstance = 0; ulInstance < SchemaDefs::SerialPortCount; ulInstance++)
    {
        ComPtr<ISerialPort> serialPort;
        hrc = pMachine->GetSerialPort (ulInstance, serialPort.asOutParam());        H();
        BOOL fEnabled = FALSE;
        if (serialPort)
            hrc = serialPort->COMGETTER(Enabled)(&fEnabled);                        H();
        if (!fEnabled)
            continue;

        char szInstance[4]; Assert(ulInstance <= 999);
        RTStrPrintf(szInstance, sizeof(szInstance), "%lu", ulInstance);

        rc = CFGMR3InsertNode(pDev, szInstance, &pInst);                            RC_CHECK();
        rc = CFGMR3InsertNode(pInst, "Config", &pCfg);                              RC_CHECK();

        ULONG ulIRQ, ulIOBase;
        PortMode_T HostMode;
        Bstr  path;
        BOOL  fServer;
        hrc = serialPort->COMGETTER(HostMode)(&HostMode);                           H();
        hrc = serialPort->COMGETTER(IRQ)(&ulIRQ);                                   H();
        hrc = serialPort->COMGETTER(IOBase)(&ulIOBase);                             H();
        hrc = serialPort->COMGETTER(Path)(path.asOutParam());                       H();
        hrc = serialPort->COMGETTER(Server)(&fServer);                              H();
        rc = CFGMR3InsertInteger(pCfg,   "IRQ", ulIRQ);                             RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,   "IOBase", ulIOBase);                       RC_CHECK();
        if (HostMode != PortMode_Disconnected)
        {
            rc = CFGMR3InsertNode(pInst,     "LUN#0", &pLunL0);                     RC_CHECK();
            if (HostMode == PortMode_HostPipe)
            {
                rc = CFGMR3InsertString(pLunL0,  "Driver", "Char");                 RC_CHECK();
                rc = CFGMR3InsertNode(pLunL0,    "AttachedDriver", &pLunL1);        RC_CHECK();
                rc = CFGMR3InsertString(pLunL1,  "Driver", "NamedPipe");            RC_CHECK();
                rc = CFGMR3InsertNode(pLunL1,    "Config", &pLunL2);                RC_CHECK();
                rc = CFGMR3InsertString(pLunL2,  "Location", Utf8Str(path));        RC_CHECK();
                rc = CFGMR3InsertInteger(pLunL2, "IsServer", fServer);              RC_CHECK();
            }
            else if (HostMode == PortMode_HostDevice)
            {
                rc = CFGMR3InsertString(pLunL0,  "Driver", "Host Serial");          RC_CHECK();
                rc = CFGMR3InsertNode(pLunL0,    "Config", &pLunL1);                RC_CHECK();
                rc = CFGMR3InsertString(pLunL1,  "DevicePath", Utf8Str(path));      RC_CHECK();
            }
        }
    }

    /*
     * Parallel (LPT) Ports
     */
    rc = CFGMR3InsertNode(pDevices, "parallel", &pDev);                             RC_CHECK();
    for (ULONG ulInstance = 0; ulInstance < SchemaDefs::ParallelPortCount; ulInstance++)
    {
        ComPtr<IParallelPort> parallelPort;
        hrc = pMachine->GetParallelPort (ulInstance, parallelPort.asOutParam());    H();
        BOOL fEnabled = FALSE;
        if (parallelPort)
            hrc = parallelPort->COMGETTER(Enabled)(&fEnabled);                      H();
        if (!fEnabled)
            continue;

        char szInstance[4]; Assert(ulInstance <= 999);
        RTStrPrintf(szInstance, sizeof(szInstance), "%lu", ulInstance);

        rc = CFGMR3InsertNode(pDev, szInstance, &pInst);                            RC_CHECK();
        rc = CFGMR3InsertNode(pInst, "Config", &pCfg);                              RC_CHECK();

        ULONG ulIRQ, ulIOBase;
        Bstr  DevicePath;
        hrc = parallelPort->COMGETTER(IRQ)(&ulIRQ);                                 H();
        hrc = parallelPort->COMGETTER(IOBase)(&ulIOBase);                           H();
        hrc = parallelPort->COMGETTER(Path)(DevicePath.asOutParam());               H();
        rc = CFGMR3InsertInteger(pCfg,   "IRQ", ulIRQ);                             RC_CHECK();
        rc = CFGMR3InsertInteger(pCfg,   "IOBase", ulIOBase);                       RC_CHECK();
        rc = CFGMR3InsertNode(pInst,     "LUN#0", &pLunL0);                         RC_CHECK();
        rc = CFGMR3InsertString(pLunL0,  "Driver", "HostParallel");                 RC_CHECK();
        rc = CFGMR3InsertNode(pLunL0,    "AttachedDriver", &pLunL1);                RC_CHECK();
        rc = CFGMR3InsertString(pLunL1,  "DevicePath", Utf8Str(DevicePath));        RC_CHECK();
    }

    /*
     * VMM Device
     */
    rc = CFGMR3InsertNode(pDevices, "VMMDev", &pDev);                               RC_CHECK();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   RC_CHECK();
    rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          4);                     RC_CHECK();
    Assert(!afPciDeviceNo[4]);
    afPciDeviceNo[4] = true;
    rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                     RC_CHECK();
    Bstr hwVersion;
    hrc = pMachine->COMGETTER(HardwareVersion)(hwVersion.asOutParam());             H();
    if (hwVersion.compare(Bstr("1")) == 0) /* <= 2.0.x */
    {
        CFGMR3InsertInteger(pCfg, "HeapEnabled", 0);                                RC_CHECK();
    }

    /* the VMM device's Main driver */
    rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                              RC_CHECK();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "MainVMMDev");          RC_CHECK();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                               RC_CHECK();
    VMMDev *pVMMDev = pConsole->mVMMDev;
    rc = CFGMR3InsertInteger(pCfg,  "Object", (uintptr_t)pVMMDev);                  RC_CHECK();

    /*
     * Attach the status driver.
     */
    rc = CFGMR3InsertNode(pInst,    "LUN#999", &pLunL0);                            RC_CHECK();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "MainStatus");          RC_CHECK();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                               RC_CHECK();
    rc = CFGMR3InsertInteger(pCfg,  "papLeds", (uintptr_t)&pConsole->mapSharedFolderLed); RC_CHECK();
    rc = CFGMR3InsertInteger(pCfg,  "First",    0);                                 RC_CHECK();
    rc = CFGMR3InsertInteger(pCfg,  "Last",     0);                                 RC_CHECK();

    /*
     * Audio Sniffer Device
     */
    rc = CFGMR3InsertNode(pDevices, "AudioSniffer", &pDev);                         RC_CHECK();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   RC_CHECK();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               RC_CHECK();

    /* the Audio Sniffer device's Main driver */
    rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                              RC_CHECK();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "MainAudioSniffer");    RC_CHECK();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                               RC_CHECK();
    AudioSniffer *pAudioSniffer = pConsole->mAudioSniffer;
    rc = CFGMR3InsertInteger(pCfg,  "Object", (uintptr_t)pAudioSniffer);            RC_CHECK();

    /*
     * AC'97 ICH / SoundBlaster16 audio
     */
    ComPtr<IAudioAdapter> audioAdapter;
    hrc = pMachine->COMGETTER(AudioAdapter)(audioAdapter.asOutParam());             H();
    if (audioAdapter)
        hrc = audioAdapter->COMGETTER(Enabled)(&enabled);                           H();

    if (enabled)
    {
        AudioControllerType_T audioController;
        hrc = audioAdapter->COMGETTER(AudioController)(&audioController);           H();
        switch (audioController)
        {
            case AudioControllerType_AC97:
            {
                /* default: ICH AC97 */
                rc = CFGMR3InsertNode(pDevices, "ichac97", &pDev);                  RC_CHECK();
                rc = CFGMR3InsertNode(pDev,     "0", &pInst);
                rc = CFGMR3InsertInteger(pInst, "Trusted",          1); /* bool */  RC_CHECK();
                rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",      5);             RC_CHECK();
                Assert(!afPciDeviceNo[5]);
                afPciDeviceNo[5] = true;
                rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",    0);             RC_CHECK();
                rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                   RC_CHECK();
                break;
            }
            case AudioControllerType_SB16:
            {
                /* legacy SoundBlaster16 */
                rc = CFGMR3InsertNode(pDevices, "sb16", &pDev);                     RC_CHECK();
                rc = CFGMR3InsertNode(pDev,     "0", &pInst);                       RC_CHECK();
                rc = CFGMR3InsertInteger(pInst, "Trusted",          1); /* bool */  RC_CHECK();
                rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                   RC_CHECK();
                rc = CFGMR3InsertInteger(pCfg,  "IRQ", 5);                          RC_CHECK();
                rc = CFGMR3InsertInteger(pCfg,  "DMA", 1);                          RC_CHECK();
                rc = CFGMR3InsertInteger(pCfg,  "DMA16", 5);                        RC_CHECK();
                rc = CFGMR3InsertInteger(pCfg,  "Port", 0x220);                     RC_CHECK();
                rc = CFGMR3InsertInteger(pCfg,  "Version", 0x0405);                 RC_CHECK();
                break;
            }
        }

        /* the Audio driver */
        rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                          RC_CHECK();
        rc = CFGMR3InsertString(pLunL0, "Driver",               "AUDIO");           RC_CHECK();
        rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                           RC_CHECK();

        AudioDriverType_T audioDriver;
        hrc = audioAdapter->COMGETTER(AudioDriver)(&audioDriver);                   H();
        switch (audioDriver)
        {
            case AudioDriverType_Null:
            {
                rc = CFGMR3InsertString(pCfg, "AudioDriver", "null");               RC_CHECK();
                break;
            }
#ifdef RT_OS_WINDOWS
#ifdef VBOX_WITH_WINMM
            case AudioDriverType_WinMM:
            {
                rc = CFGMR3InsertString(pCfg, "AudioDriver", "winmm");              RC_CHECK();
                break;
            }
#endif
            case AudioDriverType_DirectSound:
            {
                rc = CFGMR3InsertString(pCfg, "AudioDriver", "dsound");             RC_CHECK();
                break;
            }
#endif /* RT_OS_WINDOWS */
#ifdef RT_OS_SOLARIS
            case AudioDriverType_SolAudio:
            {
                rc = CFGMR3InsertString(pCfg, "AudioDriver", "solaudio");           RC_CHECK();
                break;
            }
#endif
#ifdef RT_OS_LINUX
            case AudioDriverType_OSS:
            {
                rc = CFGMR3InsertString(pCfg, "AudioDriver", "oss");                RC_CHECK();
                break;
            }
# ifdef VBOX_WITH_ALSA
            case AudioDriverType_ALSA:
            {
                rc = CFGMR3InsertString(pCfg, "AudioDriver", "alsa");               RC_CHECK();
                break;
            }
# endif
# ifdef VBOX_WITH_PULSE
            case AudioDriverType_Pulse:
            {
                rc = CFGMR3InsertString(pCfg, "AudioDriver", "pulse");              RC_CHECK();
                break;
            }
# endif
#endif /* RT_OS_LINUX */
#ifdef RT_OS_DARWIN
            case AudioDriverType_CoreAudio:
            {
                rc = CFGMR3InsertString(pCfg, "AudioDriver", "coreaudio");          RC_CHECK();
                break;
            }
#endif
        }
        hrc = pMachine->COMGETTER(Name)(&str);                                      H();
        STR_CONV();
        rc = CFGMR3InsertString(pCfg,  "StreamName", psz);                          RC_CHECK();
        STR_FREE();
    }

    /*
     * The USB Controller.
     */
    ComPtr<IUSBController> USBCtlPtr;
    hrc = pMachine->COMGETTER(USBController)(USBCtlPtr.asOutParam());
    if (USBCtlPtr)
    {
        BOOL fEnabled;
        hrc = USBCtlPtr->COMGETTER(Enabled)(&fEnabled);                             H();
        if (fEnabled)
        {
            rc = CFGMR3InsertNode(pDevices, "usb-ohci", &pDev);                     RC_CHECK();
            rc = CFGMR3InsertNode(pDev,     "0", &pInst);                           RC_CHECK();
            rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                       RC_CHECK();
            rc = CFGMR3InsertInteger(pInst, "Trusted",              1); /* boolean */   RC_CHECK();
            rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          6);                 RC_CHECK();
            Assert(!afPciDeviceNo[6]);
            afPciDeviceNo[6] = true;
            rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                 RC_CHECK();

            rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                          RC_CHECK();
            rc = CFGMR3InsertString(pLunL0, "Driver",               "VUSBRootHub");     RC_CHECK();
            rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                           RC_CHECK();

            /*
             * Attach the status driver.
             */
            rc = CFGMR3InsertNode(pInst,    "LUN#999", &pLunL0);                        RC_CHECK();
            rc = CFGMR3InsertString(pLunL0, "Driver",               "MainStatus");      RC_CHECK();
            rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                           RC_CHECK();
            rc = CFGMR3InsertInteger(pCfg,  "papLeds", (uintptr_t)&pConsole->mapUSBLed[0]);RC_CHECK();
            rc = CFGMR3InsertInteger(pCfg,  "First",    0);                             RC_CHECK();
            rc = CFGMR3InsertInteger(pCfg,  "Last",     0);                             RC_CHECK();

#ifdef VBOX_WITH_EHCI
            hrc = USBCtlPtr->COMGETTER(EnabledEhci)(&fEnabled);                         H();
            if (fEnabled)
            {
                rc = CFGMR3InsertNode(pDevices, "usb-ehci", &pDev);                     RC_CHECK();
                rc = CFGMR3InsertNode(pDev,     "0", &pInst);                           RC_CHECK();
                rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                       RC_CHECK();
                rc = CFGMR3InsertInteger(pInst, "Trusted",              1); /* bool */  RC_CHECK();
                rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          11);            RC_CHECK();
                Assert(!afPciDeviceNo[11]);
                afPciDeviceNo[11] = true;
                rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);             RC_CHECK();

                rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                      RC_CHECK();
                rc = CFGMR3InsertString(pLunL0, "Driver",               "VUSBRootHub"); RC_CHECK();
                rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                       RC_CHECK();

                /*
                 * Attach the status driver.
                 */
                rc = CFGMR3InsertNode(pInst,    "LUN#999", &pLunL0);                    RC_CHECK();
                rc = CFGMR3InsertString(pLunL0, "Driver",               "MainStatus");  RC_CHECK();
                rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                       RC_CHECK();
                rc = CFGMR3InsertInteger(pCfg,  "papLeds", (uintptr_t)&pConsole->mapUSBLed[1]);RC_CHECK();
                rc = CFGMR3InsertInteger(pCfg,  "First",    0);                         RC_CHECK();
                rc = CFGMR3InsertInteger(pCfg,  "Last",     0);                         RC_CHECK();
            }
            else
#endif
            {
                /*
                 * Global USB options, currently unused as we'll apply the 2.0 -> 1.1 morphing
                 * on a per device level now.
                 */
                rc = CFGMR3InsertNode(pRoot, "USB", &pCfg);                             RC_CHECK();
                rc = CFGMR3InsertNode(pCfg, "USBProxy", &pCfg);                         RC_CHECK();
                rc = CFGMR3InsertNode(pCfg, "GlobalConfig", &pCfg);                     RC_CHECK();
                // This globally enables the 2.0 -> 1.1 device morphing of proxied devies to keep windows quiet.
                //rc = CFGMR3InsertInteger(pCfg, "Force11Device", true);                RC_CHECK();
                // The following breaks stuff, but it makes MSDs work in vista. (I include it here so
                // that it's documented somewhere.) Users needing it can use:
                //      VBoxManage setextradata "myvm" "VBoxInternal/USB/USBProxy/GlobalConfig/Force11PacketSize" 1
                //rc = CFGMR3InsertInteger(pCfg, "Force11PacketSize", true);            RC_CHECK();
            }
        }
    }

    /*
     * Clipboard
     */
    {
        ClipboardMode_T mode = ClipboardMode_Disabled;
        hrc = pMachine->COMGETTER(ClipboardMode) (&mode);                               H();

        if (mode != ClipboardMode_Disabled)
        {
            /* Load the service */
            rc = pConsole->mVMMDev->hgcmLoadService ("VBoxSharedClipboard", "VBoxSharedClipboard");

            if (RT_FAILURE (rc))
            {
                LogRel(("VBoxSharedClipboard is not available. rc = %Rrc\n", rc));
                /* That is not a fatal failure. */
                rc = VINF_SUCCESS;
            }
            else
            {
                /* Setup the service. */
                VBOXHGCMSVCPARM parm;

                parm.type = VBOX_HGCM_SVC_PARM_32BIT;

                switch (mode)
                {
                    default:
                    case ClipboardMode_Disabled:
                    {
                        LogRel(("VBoxSharedClipboard mode: Off\n"));
                        parm.u.uint32 = VBOX_SHARED_CLIPBOARD_MODE_OFF;
                        break;
                    }
                    case ClipboardMode_GuestToHost:
                    {
                        LogRel(("VBoxSharedClipboard mode: Guest to Host\n"));
                        parm.u.uint32 = VBOX_SHARED_CLIPBOARD_MODE_GUEST_TO_HOST;
                        break;
                    }
                    case ClipboardMode_HostToGuest:
                    {
                        LogRel(("VBoxSharedClipboard mode: Host to Guest\n"));
                        parm.u.uint32 = VBOX_SHARED_CLIPBOARD_MODE_HOST_TO_GUEST;
                        break;
                    }
                    case ClipboardMode_Bidirectional:
                    {
                        LogRel(("VBoxSharedClipboard mode: Bidirectional\n"));
                        parm.u.uint32 = VBOX_SHARED_CLIPBOARD_MODE_BIDIRECTIONAL;
                        break;
                    }
                }

                pConsole->mVMMDev->hgcmHostCall ("VBoxSharedClipboard", VBOX_SHARED_CLIPBOARD_HOST_FN_SET_MODE, 1, &parm);

                Log(("Set VBoxSharedClipboard mode\n"));
            }
        }
    }

#ifdef VBOX_WITH_CROGL
    /*
     * crOpenGL
     */
    {
        BOOL fEnabled = false;
        hrc = pMachine->COMGETTER(Accelerate3DEnabled) (&fEnabled);                               H();

        if (fEnabled)
        {
            /* Load the service */
            rc = pConsole->mVMMDev->hgcmLoadService ("VBoxSharedCrOpenGL", "VBoxSharedCrOpenGL");
            if (RT_FAILURE(rc))
            {
                LogRel(("Failed to load Shared OpenGL service %Rrc\n", rc));
                /* That is not a fatal failure. */
                rc = VINF_SUCCESS;
            }
            else
            {
                LogRel(("Shared crOpenGL service loaded.\n"));

                /* Setup the service. */
                VBOXHGCMSVCPARM parm;
                parm.type = VBOX_HGCM_SVC_PARM_PTR;

                //parm.u.pointer.addr = static_cast <IConsole *> (pData->pVMMDev->getParent());
                parm.u.pointer.addr = pConsole->mVMMDev->getParent()->getDisplay()->getFramebuffer();
                parm.u.pointer.size = sizeof(IFramebuffer *);

                rc = pConsole->mVMMDev->hgcmHostCall("VBoxSharedCrOpenGL", SHCRGL_HOST_FN_SET_FRAMEBUFFER, 1, &parm);
                if (!RT_SUCCESS(rc))
                    AssertMsgFailed(("SHCRGL_HOST_FN_SET_FRAMEBUFFER failed with %Rrc\n", rc));
                }
        }
    }
#endif

#ifdef VBOX_WITH_GUEST_PROPS
    /*
     * Guest property service
     */
    {
        /* Load the service */
        rc = pConsole->mVMMDev->hgcmLoadService ("VBoxGuestPropSvc", "VBoxGuestPropSvc");

        if (RT_FAILURE (rc))
        {
            LogRel(("VBoxGuestPropSvc is not available. rc = %Rrc\n", rc));
            /* That is not a fatal failure. */
            rc = VINF_SUCCESS;
        }
        else
        {
            /* Pull over the properties from the server. */
            SafeArray <BSTR> namesOut;
            SafeArray <BSTR> valuesOut;
            SafeArray <ULONG64> timestampsOut;
            SafeArray <BSTR> flagsOut;
            hrc = pConsole->mControl->PullGuestProperties(ComSafeArrayAsOutParam(namesOut),
                                                ComSafeArrayAsOutParam(valuesOut),
                                                ComSafeArrayAsOutParam(timestampsOut),
                                                ComSafeArrayAsOutParam(flagsOut));         H();
            size_t cProps = namesOut.size();
            if (   valuesOut.size() != cProps
                || timestampsOut.size() != cProps
                || flagsOut.size() != cProps
               )
                rc = VERR_INVALID_PARAMETER;

            std::vector <Utf8Str> utf8Names, utf8Values, utf8Flags;
            std::vector <char *> names, values, flags;
            std::vector <ULONG64> timestamps;
            for (unsigned i = 0; i < cProps && RT_SUCCESS(rc); ++i)
                if (   !VALID_PTR(namesOut[i])
                    || !VALID_PTR(valuesOut[i])
                    || !VALID_PTR(flagsOut[i])
                   )
                    rc = VERR_INVALID_POINTER;
            for (unsigned i = 0; i < cProps && RT_SUCCESS(rc); ++i)
            {
                utf8Names.push_back(Bstr(namesOut[i]));
                utf8Values.push_back(Bstr(valuesOut[i]));
                timestamps.push_back(timestampsOut[i]);
                utf8Flags.push_back(Bstr(flagsOut[i]));
                if (   utf8Names.back().isNull()
                    || utf8Values.back().isNull()
                    || utf8Flags.back().isNull()
                   )
                    throw std::bad_alloc();
            }
            for (unsigned i = 0; i < cProps && RT_SUCCESS(rc); ++i)
            {
                names.push_back(utf8Names[i].mutableRaw());
                values.push_back(utf8Values[i].mutableRaw());
                flags.push_back(utf8Flags[i].mutableRaw());
            }
            names.push_back(NULL);
            values.push_back(NULL);
            timestamps.push_back(0);
            flags.push_back(NULL);

            /* Setup the service. */
            VBOXHGCMSVCPARM parms[4];

            parms[0].type = VBOX_HGCM_SVC_PARM_PTR;
            parms[0].u.pointer.addr = &names.front();
            parms[0].u.pointer.size = 0;  /* We don't actually care. */
            parms[1].type = VBOX_HGCM_SVC_PARM_PTR;
            parms[1].u.pointer.addr = &values.front();
            parms[1].u.pointer.size = 0;  /* We don't actually care. */
            parms[2].type = VBOX_HGCM_SVC_PARM_PTR;
            parms[2].u.pointer.addr = &timestamps.front();
            parms[2].u.pointer.size = 0;  /* We don't actually care. */
            parms[3].type = VBOX_HGCM_SVC_PARM_PTR;
            parms[3].u.pointer.addr = &flags.front();
            parms[3].u.pointer.size = 0;  /* We don't actually care. */

            pConsole->mVMMDev->hgcmHostCall ("VBoxGuestPropSvc", guestProp::SET_PROPS_HOST, 4, &parms[0]);

            /* Register the host notification callback */
            HGCMSVCEXTHANDLE hDummy;
            HGCMHostRegisterServiceExtension (&hDummy, "VBoxGuestPropSvc",
                                              Console::doGuestPropNotification,
                                              pvConsole);

            Log(("Set VBoxGuestPropSvc property store\n"));
        }
    }
#endif /* VBOX_WITH_GUEST_PROPS defined */

    /*
     * CFGM overlay handling.
     *
     * Here we check the extra data entries for CFGM values
     * and create the nodes and insert the values on the fly. Existing
     * values will be removed and reinserted. CFGM is typed, so by default
     * we will guess whether it's a string or an integer (byte arrays are
     * not currently supported). It's possible to override this autodetection
     * by adding "string:", "integer:" or "bytes:" (future).
     *
     * We first perform a run on global extra data, then on the machine
     * extra data to support global settings with local overrides.
     *
     */
    /** @todo add support for removing nodes and byte blobs. */
    Bstr strExtraDataKey;
    bool fGlobalExtraData = true;
    for (;;)
    {
        /*
         * Get the next key
         */
        Bstr strNextExtraDataKey;
        Bstr strExtraDataValue;
        if (fGlobalExtraData)
            hrc = virtualBox->GetNextExtraDataKey(strExtraDataKey, strNextExtraDataKey.asOutParam(),
                                                  strExtraDataValue.asOutParam());
        else
            hrc = pMachine->GetNextExtraDataKey(strExtraDataKey, strNextExtraDataKey.asOutParam(),
                                                strExtraDataValue.asOutParam());

        /* stop if for some reason there's nothing more to request */
        if (FAILED(hrc) || !strNextExtraDataKey)
        {
            /* if we're out of global keys, continue with machine, otherwise we're done */
            if (fGlobalExtraData)
            {
                fGlobalExtraData = false;
                strExtraDataKey.setNull();
                continue;
            }
            break;
        }
        strExtraDataKey = strNextExtraDataKey;

        /*
         * We only care about keys starting with "VBoxInternal/"
         */
        Utf8Str strExtraDataKeyUtf8(strExtraDataKey);
        char *pszExtraDataKey = (char *)strExtraDataKeyUtf8.raw();
        if (strncmp(pszExtraDataKey, "VBoxInternal/", sizeof("VBoxInternal/") - 1) != 0)
            continue;
        pszExtraDataKey += sizeof("VBoxInternal/") - 1;

        /*
         * The key will be in the format "Node1/Node2/Value" or simply "Value".
         * Split the two and get the node, delete the value and create the node
         * if necessary.
         */
        PCFGMNODE pNode;
        char *pszCFGMValueName = strrchr(pszExtraDataKey, '/');
        if (pszCFGMValueName)
        {
            /* terminate the node and advance to the value (Utf8Str might not
               offically like this but wtf) */
            *pszCFGMValueName = '\0';
            pszCFGMValueName++;

            /* does the node already exist? */
            pNode = CFGMR3GetChild(pRoot, pszExtraDataKey);
            if (pNode)
                CFGMR3RemoveValue(pNode, pszCFGMValueName);
            else
            {
                /* create the node */
                rc = CFGMR3InsertNode(pRoot, pszExtraDataKey, &pNode);
                if (RT_FAILURE(rc))
                {
                    AssertLogRelMsgRC(rc, ("failed to insert node '%s'\n", pszExtraDataKey));
                    continue;
                }
                Assert(pNode);
            }
        }
        else
        {
            /* root value (no node path). */
            pNode = pRoot;
            pszCFGMValueName = pszExtraDataKey;
            pszExtraDataKey--;
            CFGMR3RemoveValue(pNode, pszCFGMValueName);
        }

        /*
         * Now let's have a look at the value.
         * Empty strings means that we should remove the value, which we've
         * already done above.
         */
        Utf8Str strCFGMValueUtf8(strExtraDataValue);
        const char *pszCFGMValue = strCFGMValueUtf8.raw();
        if (    pszCFGMValue
            && *pszCFGMValue)
        {
            uint64_t u64Value;

            /* check for type prefix first. */
            if (!strncmp(pszCFGMValue, "string:", sizeof("string:") - 1))
                rc = CFGMR3InsertString(pNode, pszCFGMValueName, pszCFGMValue + sizeof("string:") - 1);
            else if (!strncmp(pszCFGMValue, "integer:", sizeof("integer:") - 1))
            {
                rc = RTStrToUInt64Full(pszCFGMValue + sizeof("integer:") - 1, 0, &u64Value);
                if (RT_SUCCESS(rc))
                    rc = CFGMR3InsertInteger(pNode, pszCFGMValueName, u64Value);
            }
            else if (!strncmp(pszCFGMValue, "bytes:", sizeof("bytes:") - 1))
                rc = VERR_NOT_IMPLEMENTED;
            /* auto detect type. */
            else if (RT_SUCCESS(RTStrToUInt64Full(pszCFGMValue, 0, &u64Value)))
                rc = CFGMR3InsertInteger(pNode, pszCFGMValueName, u64Value);
            else
                rc = CFGMR3InsertString(pNode, pszCFGMValueName, pszCFGMValue);
            AssertLogRelMsgRC(rc, ("failed to insert CFGM value '%s' to key '%s'\n", pszCFGMValue, pszExtraDataKey));
        }
    }

#undef H
#undef RC_CHECK
#undef STR_FREE
#undef STR_CONV

    /* Register VM state change handler */
    int rc2 = VMR3AtStateRegister (pVM, Console::vmstateChangeCallback, pConsole);
    AssertRC (rc2);
    if (RT_SUCCESS (rc))
        rc = rc2;

    /* Register VM runtime error handler */
    rc2 = VMR3AtRuntimeErrorRegister (pVM, Console::setVMRuntimeErrorCallback, pConsole);
    AssertRC (rc2);
    if (RT_SUCCESS (rc))
        rc = rc2;

    /* Save the VM pointer in the machine object */
    pConsole->mpVM = pVM;

    LogFlowFunc (("vrc = %Rrc\n", rc));
    LogFlowFuncLeave();

    return rc;
}
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
