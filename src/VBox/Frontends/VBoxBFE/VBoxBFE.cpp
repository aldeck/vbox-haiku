/** @file
 *
 * VBox frontends: Basic Frontend (BFE):
 * VBoxBFE main routines
 *
 * VBoxBFE is a limited frontend that sits directly on the Virtual Machine
 * Manager (VMM) and does _not_ use COM to communicate.
 * On Linux and Windows, VBoxBFE is based on SDL; on L4 it's based on the
 * L4 console. Much of the code has been copied over from the other frontends
 * in VBox/Main/ and src/Frontends/VBoxSDL/.
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_GUI

#ifndef VBOXBFE_WITHOUT_COM
# include <VBox/com/Guid.h>
# include <VBox/com/string.h>
using namespace com;
#endif

#include <VBox/types.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/pdm.h>
#include <VBox/version.h>
#ifdef VBOXBFE_WITH_USB
# include <VBox/vusb.h>
#endif
#include <VBox/log.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/runtime.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>
#include <iprt/file.h>
#include <iprt/alloca.h>
#include <iprt/ctype.h>

#include "VBoxBFE.h"

#include <stdio.h>
#include <stdlib.h> /* putenv */
#include <errno.h>

#if defined(__LINUX__) || defined(__L4__)
#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/if_tun.h>
#endif

#ifndef __L4ENV__
#include <vector>
#endif

#include "ConsoleImpl.h"
#include "DisplayImpl.h"
#include "MouseImpl.h"
#include "KeyboardImpl.h"
#include "VMMDevInterface.h"
#include "StatusImpl.h"
#include "Framebuffer.h"
#include "MachineDebuggerImpl.h"
#ifdef VBOXBFE_WITH_USB
# include "HostUSBImpl.h"
#endif

#if defined(USE_SDL) && ! defined(__L4__)
#include "SDLConsole.h"
#include "SDLFramebuffer.h"
#endif

#ifdef __L4__
#include "L4Console.h"
#include "L4Framebuffer.h"
#endif

#ifdef __L4ENV__
# ifndef L4API_l4v2onv4
#  include <l4/sys/ktrace.h>
# endif
# include <l4/vboxserver/file.h>
#endif

/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/

#define VBOXSDL_ADVANCED_OPTIONS


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static DECLCALLBACK(int) cfgmR3CreateDefault(PVM pVM, void *pvUser);
static DECLCALLBACK(void) vmstateChangeCallback(PVM pVM, VMSTATE enmState, VMSTATE enmOldState, void *pvUser);
static DECLCALLBACK(void) setVMErrorCallback(PVM pVM, void *pvUser, int rc, RT_SRC_POS_DECL,
                                             const char *pszFormat, va_list args);
static DECLCALLBACK(int) VMPowerUpThread(RTTHREAD Thread, void *pvUser);


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/

PVM              pVM              = NULL;
Mouse           *gMouse           = NULL;
VMDisplay       *gDisplay         = NULL;
Keyboard        *gKeyboard        = NULL;
VMMDev          *gVMMDev          = NULL;
Framebuffer     *gFramebuffer     = NULL;
MachineDebugger *gMachineDebugger = NULL;
VMStatus        *gStatus          = NULL;
Console         *gConsole         = NULL;
#ifdef VBOXBFE_WITH_USB
HostUSB         *gHostUSB         = NULL;
#endif

VMSTATE machineState = VMSTATE_CREATING;

PPDMLED     mapFDLeds[2]      = {0};
PPDMLED     mapIDELeds[4]     = {0};

/** flag whether keyboard/mouse events are grabbed */
#ifdef __L4__
/** see <l4/input/macros.h> for key definitions */
int gHostKey; /* not used */
int gHostKeySym = KEY_RIGHTCTRL;
#elif defined (DEBUG_dmik)
// my mini kbd doesn't have RCTRL...
int gHostKey    = KMOD_RSHIFT;
int gHostKeySym = SDLK_RSHIFT;
#else
int gHostKey    = KMOD_RCTRL;
int gHostKeySym = SDLK_RCTRL;
#endif
bool gfAllowFullscreenToggle = true;

static bool g_fIOAPIC = false;
static bool fACPI = true;
static bool fAudio = false;
#ifdef VBOXBFE_WITH_USB
static bool fUSB = false;
#endif
//static bool fPacketSniffer = false;
static char *hdaFile   = NULL;
static char *cdromFile = NULL;
static char *fdaFile   = NULL;
static char *pszBootDevice = "IDE";
static uint32_t memorySize = 128;
static uint32_t vramSize = 4;
#ifdef VBOXSDL_ADVANCED_OPTIONS
static unsigned fRawR0 = ~0U;
static unsigned fRawR3 = ~0U;
static unsigned fPATM  = ~0U;
static unsigned fCSAM  = ~0U;
#endif
static bool g_fReleaseLog = true; /**< Set if we should open the release. */


/**
 * Network device config info.
 */
typedef struct BFENetworkDevice
{
    enum
    {
        NOT_CONFIGURED = 0,
        NONE,
        NAT,
        HIF,
        INTNET
    }           enmType;    /**< The type of network driver. */
    bool        fSniff;     /**< Set if the network sniffer should be installed. */
    const char *pszSniff;   /**< Output file for the network sniffer. */
    PDMMAC      Mac;        /**< The mac address for the device. */
    const char *pszName;     /**< The device name of a HIF device. The name of the internal network. */
#if 1//defined(__LINUX__)
    bool        fHaveFd;    /**< Set if fd is valid. */
    int32_t     fd;         /**< The file descriptor of a HIF device.*/
#endif
} BFENETDEV, *PBFENETDEV;

/** Array of network device configurations. */
static BFENETDEV g_aNetDevs[NetworkAdapterCount];


/** @todo currently this is only set but never read. */
static char szError[512];


/**
 * Converts the passed in network option
 *
 * @returns Index into g_aNetDevs on success. (positive)
 * @returns VERR_INVALID_PARAMETER on failure. (negative)
 * @param   pszArg          The argument.
 * @param   cchRoot         The length of the argument root.
 */
static int networkArg2Index(const char *pszArg, int cchRoot)
{
    uint32_t n;
    int rc = RTStrToUInt32Ex(&pszArg[cchRoot], NULL, 10, &n);
    if (VBOX_FAILURE(rc))
    {
        RTPrintf("Error: invalid network device option (rc=%Vrc): %s\n", rc, pszArg);
        return -1;
    }
    if (n < 1 || n > NetworkAdapterCount)
    {
        RTPrintf("Error: The network device number is out of range: %RU32 (1 <= 0 <= %u) (%s)\n",
                 n, NetworkAdapterCount, pszArg);
        return -1;
    }
    return n;
}


/**
 * Print a syntax error.
 *
 * @returns return value for main().
 * @param   pszMsg  The message format string.
 * @param   ...     Format arguments.
 */
static int SyntaxError(const char *pszMsg, ...)
{
    va_list va;
    RTPrintf("error: ");
    va_start(va, pszMsg);
    RTPrintfV(pszMsg, va);
    va_end(va);
    return 1;
}


/**
 * Print a fatal error.
 *
 * @returns return value for main().
 * @param   pszMsg  The message format string.
 * @param   ...     Format arguments.
 */
static int FatalError(const char *pszMsg, ...)
{
    va_list va;
    RTPrintf("fatal error: ");
    va_start(va, pszMsg);
    RTPrintfV(pszMsg, va);
    va_end(va);
    return 1;
}


/**
 * Print program usage.
 */
static void show_usage()
{
    RTPrintf("Usage:\n"
             "  -hda <file>        Set first hard disk to file\n"
             "  -fda <file>        Set first floppy disk to file\n"
             "  -cdrom <file>      Set CDROM to file/device ('none' to unmount)\n"
             "  -boot <a|c|d>      Set boot device (a = floppy, c = first hard disk, d = DVD)\n"
             "  -m <size>          Set memory size in megabytes (default 128MB)\n"
             "  -vram <size>       Set size of video memory in megabytes\n"
             "  -fullscreen        Start VM in fullscreen mode\n"
             "  -nofstoggle        Forbid switching to/from fullscreen mode\n"
             "  -nohostkey         Disable hostkey\n"
             "  -[no]acpi          Enable or disable ACPI (default: enabled)\n"
             "  -[no]ioapic        Enable or disable the IO-APIC (default: disabled)\n"
             "  -audio             Enable audio\n"
             "  -natdev<1-N>       Configure NAT for network device N\n"
             "  -hifdev<1-N> <dev> <mac> Use existing Host Interface Network Device with the given name and MAC address\n"
#if 0
             "  -netsniff<1-N>     Enable packet sniffer\n"
#endif
#ifdef __LINUX__
             "  -tapfd<1-N> <fd>   Use existing TAP device, don't allocate\n"
#endif
#ifdef VBOX_VRDP
             "  -vrdp [port]       Listen for VRDP connections on port (default if not specified)\n"
#endif
#ifdef VBOX_SECURELABEL
             "  -securelabel       Display a secure VM label at the top of the screen\n"
             "  -seclabelfnt       TrueType (.ttf) font file for secure session label\n"
             "  -seclabelsiz       Font point size for secure session label (default 12)\n"
#endif
             "  -[no]rellog        Enable or disable the release log './VBoxBFE.log' (default: enabled)\n"
#ifdef VBOXSDL_ADVANCED_OPTIONS
             "  -[no]rawr0         Enable or disable raw ring 3\n"
             "  -[no]rawr3         Enable or disable raw ring 0\n"
             "  -[no]patm          Enable or disable PATM\n"
             "  -[no]csam          Enable or disable CSAM\n"
#endif
#ifdef __L4ENV__
             "  -env <var=value>   Set the given environment variable to \"value\"\n"
#endif
             "\n");
}


/** entry point */
int main(int argc, char **argv)
{
#ifdef __L4ENV__
#ifndef L4API_l4v2onv4
    /* clear Fiasco kernel trace buffer */
    fiasco_tbuf_clear();
#endif
    /* set the environment.  Must be done before the runtime is
       initialised.  Yes, it really must. */
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], "-env") == 0)
        {
            if (++i >= argc)
                return SyntaxError("missing argument to -env (format: var=value)!\n");
            /* add it to the environment */
            if (putenv(argv[i]) != 0)
                return SyntaxError("Error setting environment string %s.\n", argv[i]);
        }
#endif /* __L4ENV__ */

    /*
     * Before we do *anything*, we initialize the runtime.
     */
    int rc = RTR3Init();
    if (VBOX_FAILURE(rc))
        return FatalError("RTR3Init failed rc=%Vrc\n", rc);


    bool fFullscreen = false;
#ifdef VBOX_VRDP
    int32_t portVRDP = -1;
#endif
#ifdef VBOX_SECURELABEL
    bool fSecureLabel = false;
    uint32_t secureLabelPointSize = 12;
    char *secureLabelFontFile = NULL;
#endif
    RTPrintf("VirtualBox Simple SDL GUI built %s %s\n", __DATE__, __TIME__);

    // less than one parameter is not possible
    if (argc < 2)
    {
        show_usage();
        return 1;
    }

    /*
     * Parse the command line arguments.
     */
    for (int curArg = 1; curArg < argc; curArg++)
    {
        const char * const pszArg = argv[curArg];
        if (strcmp(pszArg, "-boot") == 0)
        {
            if (++curArg >= argc)
                return SyntaxError("missing argument for boot drive!\n");
            if (strlen(argv[curArg]) != 1)
                return SyntaxError("invalid argument for boot drive! (%s)\n", argv[curArg]);
            rc = VINF_SUCCESS;
            switch (argv[curArg][0])
            {
                case 'a':
                {
                    pszBootDevice = "FLOPPY";
                    break;
                }

                case 'c':
                {
                    pszBootDevice = "IDE";
                    break;
                }

                case 'd':
                {
                    pszBootDevice = "DVD";
                    break;
                }

                default:
                    return SyntaxError("wrong argument for boot drive! (%s)\n", argv[curArg]);
            }
        }
        else if (strcmp(pszArg, "-m") == 0)
        {
            if (++curArg >= argc)
                return SyntaxError("missing argument for memory size!\n");
            rc = RTStrToUInt32Ex(argv[curArg], NULL, 0, &memorySize);
            if (VBOX_FAILURE(rc))
                return SyntaxError("cannot grok the memory size: %s (%Vrc)\n",
                                   argv[curArg], rc);
        }
        else if (strcmp(pszArg, "-vram") == 0)
        {
            if (++curArg >= argc)
                return SyntaxError("missing argument for vram size!\n");
            rc = RTStrToUInt32Ex(argv[curArg], NULL, 0, &vramSize);
            if (VBOX_FAILURE(rc))
                return SyntaxError("cannot grok the vram size: %s (%Vrc)\n",
                                   argv[curArg], rc);
        }
        else if (strcmp(pszArg, "-fullscreen") == 0)
        {
            fFullscreen = true;
        }
        else if (strcmp(pszArg, "-nofstoggle") == 0)
        {
            gfAllowFullscreenToggle = false;
        }
        else if (strcmp(pszArg, "-nohostkey") == 0)
        {
            gHostKey = 0;
            gHostKeySym = 0;
        }
        else if (strcmp(pszArg, "-acpi") == 0)
        {
            fACPI = true;
        }
        else if (strcmp(pszArg, "-noacpi") == 0)
        {
            fACPI = false;
        }
        else if (strcmp(pszArg, "-ioapic") == 0)
        {
            g_fIOAPIC = true;
        }
        else if (strcmp(pszArg, "-noioapic") == 0)
        {
            g_fIOAPIC = false;
        }
        else if (strcmp(pszArg, "-audio") == 0)
        {
            fAudio = true;
        }
#ifdef VBOXBFE_WITH_USB
        else if (strcmp(pszArg, "-usb") == 0)
        {
            fUSB = true;
        }
#endif
        else if (strcmp(pszArg, "-hda") == 0)
        {
            if (++curArg >= argc)
                return SyntaxError("missing file name for first hard disk!\n");

            /* resolve it. */
            hdaFile = RTPathRealDup(argv[curArg]);
            if (!hdaFile)
                return SyntaxError("The path to the specified harddisk, '%s', could not be resolved.\n", argv[curArg]);
        }
        else if (strcmp(pszArg, "-fda") == 0)
        {
            if (++curArg >= argc)
                return SyntaxError("missing file/device name for first floppy disk!\n");

            /* resolve it. */
            fdaFile = RTPathRealDup(argv[curArg]);
            if (!fdaFile)
                return SyntaxError("The path to the specified floppy disk, '%s', could not be resolved.\n", argv[curArg]);
        }
        else if (strcmp(pszArg, "-cdrom") == 0)
        {
            if (++curArg >= argc)
                return SyntaxError("missing file/device name for first hard disk!\n");

            /* resolve it. */
            cdromFile = RTPathRealDup(argv[curArg]);
            if (!cdromFile)
                return SyntaxError("The path to the specified cdrom, '%s', could not be resolved.\n", argv[curArg]);
        }
        else if (   strncmp(pszArg, "-natdev", 7) == 0
                 || strncmp(pszArg, "-hifdev", 7) == 0
                 || strncmp(pszArg, "-nonetd", 7) == 0
                 || strncmp(pszArg, "-intnet", 7) == 0)
        {
            int i = networkArg2Index(pszArg, 7);
            if (i < 0)
                return 1;
            g_aNetDevs[i].enmType = !strncmp(pszArg, "-natdev", 7)
                                  ? BFENETDEV::NAT
                                  : !strncmp(pszArg, "-hifdev", 7)
                                  ? BFENETDEV::HIF
                                  : !strncmp(pszArg, "-intnet", 7)
                                  ? BFENETDEV::INTNET
                                  : BFENETDEV::NONE;

            /* The HIF device name / The Internal Network name. */
            g_aNetDevs[i].pszName = NULL;
            if (    g_aNetDevs[i].enmType == BFENETDEV::HIF
                ||  g_aNetDevs[i].enmType == BFENETDEV::INTNET)
            {
                if (curArg + 1 >= argc)
                    return SyntaxError(g_aNetDevs[i].enmType == BFENETDEV::HIF
                                       ? "The TAP network device name is missing! (%s)\n"
                                       : "The internal network name is missing! (%s)\n"
                                       , pszArg);
                g_aNetDevs[i].pszName = argv[++curArg];
            }

            /* The MAC address. */
            if (++curArg >= argc)
                return SyntaxError("The network MAC address is missing! (%s)\n", pszArg);
            if (strlen(argv[curArg]) != 12)
                return SyntaxError("The network MAC address has an invalid length: %s (%s)\n", argv[curArg], pszArg);
            const char *pszMac = argv[curArg];
            for (unsigned j = 0; j < RT_ELEMENTS(g_aNetDevs[i].Mac.au8); j++)
            {
                char c1 = toupper(*pszMac++) - '0';
                if (c1 > 9)
                    c1 -= 7;
                char c2 = toupper(*pszMac++) - '0';
                if (c2 > 9)
                    c2 -= 7;
                if (c2 > 16 || c1 > 16)
                    return SyntaxError("Invalid MAC address: %s\n", argv[curArg]);
                g_aNetDevs[i].Mac.au8[j] = ((c1 & 0x0f) << 4) | (c2 & 0x0f);
            }
        }
        else if (strncmp(pszArg, "-netsniff", 9) == 0)
        {
            int i = networkArg2Index(pszArg, 7);
            if (rc < 0)
                return 1;
            g_aNetDevs[i].fSniff = true;
            /** @todo filename */
        }
#ifdef __LINUX__
        else if (strncmp(pszArg, "-tapfd", 6) == 0)
        {
            int i = networkArg2Index(pszArg, 7);
            if (++curArg >= argc)
                return SyntaxError("missing argument for %s!\n", pszArg);
            rc = RTStrToInt32Ex(argv[curArg], NULL, 0, &g_aNetDevs[i].fd);
            if (VBOX_FAILURE(rc))
                return SyntaxError("cannot grok tap fd: %s (%VRc)\n", argv[curArg], rc);
            g_aNetDevs[i].fHaveFd = true;
        }
#endif /* __LINUX__ */
#ifdef VBOX_VRDP
        else if (strcmp(pszArg, "-vrdp") == 0)
        {
            // -vrdp might take a port number (positive).
            portVRDP = 0;       // indicate that it was encountered.
            if (curArg + 1 < argc && argv[curArg + 1][0] != '-')
            {
                rc = RTStrToInt32Ex(argv[curArg], NULL, 0, &portVRDP);
                if (VBOX_FAILURE(rc))
                    return SyntaxError("cannot vrpd port: %s (%VRc)\n", argv[curArg], rc);
                if (portVRDP < 0 || portVRDP >= 0x10000)
                    return SyntaxError("vrdp port number is out of range: %RI32\n", portVRDP);
            }
        }
#endif /* VBOX_VRDP */
#ifdef VBOX_SECURELABEL
        else if (strcmp(pszArg, "-securelabel") == 0)
        {
            fSecureLabel = true;
            LogFlow(("Secure labelling turned on\n"));
        }
        else if (strcmp(pszArg, "-seclabelfnt") == 0)
        {
            if (++curArg >= argc)
                return SyntaxError("missing font file name for secure label!\n");
            secureLabelFontFile = argv[curArg];
        }
        else if (strcmp(pszArg, "-seclabelsiz") == 0)
        {
            if (++curArg >= argc)
                return SyntaxError("missing font point size for secure label!\n");
            secureLabelPointSize = atoi(argv[curArg]);
        }
#endif
        else if (strcmp(pszArg, "-rellog") == 0)
        {
            g_fReleaseLog = true;
        }
        else if (strcmp(pszArg, "-norellog") == 0)
        {
            g_fReleaseLog = false;
        }
#ifdef VBOXSDL_ADVANCED_OPTIONS
        else if (strcmp(pszArg, "-rawr0") == 0)
        {
            fRawR0 = true;
        }
        else if (strcmp(pszArg, "-norawr0") == 0)
        {
            fRawR0 = false;
        }
        else if (strcmp(pszArg, "-rawr3") == 0)
        {
            fRawR3 = true;
        }
        else if (strcmp(pszArg, "-norawr3") == 0)
        {
            fRawR3 = false;
        }
        else if (strcmp(pszArg, "-patm") == 0)
        {
            fPATM = true;
        }
        else if (strcmp(pszArg, "-nopatm") == 0)
        {
            fPATM = false;
        }
        else if (strcmp(pszArg, "-csam") == 0)
        {
            fCSAM = true;
        }
        else if (strcmp(pszArg, "-nocsam") == 0)
        {
            fCSAM = false;
        }
#endif /* VBOXSDL_ADVANCED_OPTIONS */
#ifdef __L4__
        else if (strcmp(pszArg, "-env") == 0)
            ++curArg;
#endif /* __L4__ */
        /* just show the help screen */
        else
        {
            SyntaxError("unrecognized argument '%s'\n", pszArg);
            show_usage();
            return 1;
        }
    }

    gMachineDebugger = new MachineDebugger();
    gStatus = new VMStatus();
    gKeyboard = new Keyboard();
    gMouse = new Mouse();
    gVMMDev = new VMMDev();
    gDisplay = new VMDisplay();
#if defined(USE_SDL)
    /* First console, then framebuffer!! */
    gConsole = new SDLConsole();
    gFramebuffer = new SDLFramebuffer();
#elif defined(__L4ENV__)
    gConsole = new L4Console();
    gFramebuffer = new L4Framebuffer();
#else
#error "todo"
#endif
    if (!gConsole->initialized())
        goto leave;
    gDisplay->RegisterExternalFramebuffer(gFramebuffer);

    /* start with something in the titlebar */
    gConsole->updateTitlebar();

    /*
     * Start the VM execution thread. This has to be done
     * asynchronously as powering up can take some time
     * (accessing devices such as the host DVD drive). In
     * the meantime, we have to service the SDL event loop.
     */

    RTTHREAD thread;
    rc = RTThreadCreate(&thread, VMPowerUpThread, 0, 0, RTTHREADTYPE_MAIN_WORKER, 0, "PowerUp");
    if (VBOX_FAILURE(rc))
    {
        RTPrintf("Error: Thread creation failed with %d\n", rc);
        return -1;
    }

    /* loop until the powerup processing is done */
    do
    {
#if defined(__LINUX__) && defined(USE_SDL)
        if (   machineState == VMSTATE_CREATING
            || machineState == VMSTATE_LOADING)
        {
            int event = gConsole->eventWait();

            switch (event)
            {
            case CONEVENT_USR_SCREENRESIZE:
                LogFlow(("CONEVENT_USR_SCREENRESIZE\n"));
                gFramebuffer->resize();
                /* notify the display that the resize has been completed */
                gDisplay->ResizeCompleted();
                break;

            case CONEVENT_USR_QUIT:
                RTPrintf("Error: failed to power up VM! No error text available.\n");
                goto leave;
            }
        }
        else
#endif
            RTThreadSleep(1000);
    }
    while (   machineState == VMSTATE_CREATING
           || machineState == VMSTATE_LOADING);

    if (machineState == VMSTATE_TERMINATED)
        goto leave;

    /* did the power up succeed? */
    if (machineState != VMSTATE_RUNNING)
    {
        RTPrintf("Error: failed to power up VM! No error text available (rc = 0x%x state = %d)\n", rc, machineState);
        goto leave;
    }

    gConsole->updateTitlebar();

    /*
     * Main event loop
     */
    LogFlow(("VBoxSDL: Entering big event loop\n"));

    while (1)
    {
        int event = gConsole->eventWait();

        switch (event)
        {
        case CONEVENT_NONE:
            /* Handled internally */
            break;

        case CONEVENT_QUIT:
        case CONEVENT_USR_QUIT:
            goto leave;

        case CONEVENT_SCREENUPDATE:
            /// @todo that somehow doesn't seem to work!
            gFramebuffer->repaint();
            break;

        case CONEVENT_USR_TITLEBARUPDATE:
            gConsole->updateTitlebar();
            break;

        case CONEVENT_USR_SCREENRESIZE:
        {
            LogFlow(("CONEVENT_USR_SCREENRESIZE\n"));
            gFramebuffer->resize();
            /* notify the display that the resize has been completed */
            gDisplay->ResizeCompleted();
            break;
        }

#ifdef VBOX_SECURELABEL
        case CONEVENT_USR_SECURELABELUPDATE:
        {
           /*
             * Query the new label text
             */
            Bstr key = VBOXSDL_SECURELABEL_EXTRADATA;
            Bstr label;
            gMachine->COMGETTER(ExtraData)(key, label.asOutParam());
            Utf8Str labelUtf8 = label;
            /*
             * Now update the label
             */
            gFramebuffer->setSecureLabelText(labelUtf8.raw());
            break;
        }
#endif /* VBOX_SECURELABEL */

        }

    }

leave:
    LogFlow(("Returning from main()!\n"));

    if (pVM)
    {
        /*
         * If get here because the guest terminated using ACPI off we don't have to
         * switch off the VM because we were notified via vmstateChangeCallback()
         * that this already happened. In any other case stop the VM before killing her.
         */
        if (machineState != VMSTATE_OFF)
        {
            /* Power off VM */
            PVMREQ pReq;
            rc = VMR3ReqCall(pVM, &pReq, RT_INDEFINITE_WAIT, (PFNRT)VMR3PowerOff, 1, pVM);
        }

        /* And destroy it */
        rc = VMR3Destroy(pVM);
        AssertRC(rc);
    }

    delete gFramebuffer;
    delete gConsole;
    delete gDisplay;
    delete gKeyboard;
    delete gMouse;
    delete gStatus;
    delete gMachineDebugger;

    RTLogFlush(NULL);
    return VBOX_FAILURE (rc) ? 1 : 0;
}



/**
 * VM state callback function. Called by the VMM
 * using its state machine states.
 *
 * Primarily used to handle VM initiated power off, suspend and state saving,
 * but also for doing termination completed work (VMSTATE_TERMINATE).
 *
 * In general this function is called in the context of the EMT.
 *
 * @todo machineState is set to VMSTATE_RUNNING before all devices have received power on events
 *       this can prematurely allow the main thread to enter the event loop
 *
 * @param   pVM         The VM handle.
 * @param   enmState    The new state.
 * @param   enmOldState The old state.
 * @param   pvUser      The user argument.
 */
static DECLCALLBACK(void) vmstateChangeCallback(PVM pVM, VMSTATE enmState, VMSTATE enmOldState, void *pvUser)
{
    LogFlow(("vmstateChangeCallback: changing state from %d to %d\n", enmOldState, enmState));
    machineState = enmState;

    switch (enmState)
    {
        /*
         * The VM has terminated
         */
        case VMSTATE_OFF:
        {
            gConsole->eventQuit();
            break;
        }

        /*
         * The VM has been completely destroyed.
         *
         * Note: This state change can happen at two points:
         *       1) At the end of VMR3Destroy() if it was not called from EMT.
         *       2) At the end of vmR3EmulationThread if VMR3Destroy() was called by EMT.
         */
        case VMSTATE_TERMINATED:
        {
            break;
        }

        default: /* shut up gcc */
            break;
    }
}


/**
 * VM error callback function. Called by the various VM components.
 *
 * @param   pVM         The VM handle.
 * @param   pvUser      The user argument.
 * @param   rc          VBox status code.
 * @param   pszError    Error message format string.
 * @param   args        Error message arguments.
 * @thread EMT.
 */
DECLCALLBACK(void) setVMErrorCallback(PVM pVM, void *pvUser, int rc, RT_SRC_POS_DECL,
                                      const char *pszFormat, va_list args)
{
    /** @todo accessing shared resource without any kind of synchronization */
    if (VBOX_SUCCESS(rc))
        szError[0] = '\0';
    else
        RTStrPrintfV(szError, sizeof(szError), pszFormat, args);
}


/** VM asynchronous operations thread */
DECLCALLBACK(int) VMPowerUpThread(RTTHREAD Thread, void *pvUser)
{
    int rc = VINF_SUCCESS;
    int rc2;

    /*
     * Setup the release log instance in current directory.
     */
    if (g_fReleaseLog)
    {
        static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
        PRTLOGGER pLogger;
        rc2 = RTLogCreate(&pLogger, RTLOGFLAGS_PREFIX_TIME_PROG, "all",
                          "VBOX_RELEASE_LOG", ELEMENTS(s_apszGroups), s_apszGroups,
                          RTLOGDEST_FILE, "./VBoxBFE.log");
        if (VBOX_SUCCESS(rc2))
        {
            /* some introductory information */
            RTTIMESPEC TimeSpec;
            char szNowUct[64];
            RTTimeSpecToString(RTTimeNow(&TimeSpec), szNowUct, sizeof(szNowUct));
            RTLogRelLogger(pLogger, 0, ~0U,
                           "VBoxBFE %d.%d.%d (%s %s) release log\n"
                           "Log opened %s\n",
                           VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR, VBOX_VERSION_BUILD,
                           __DATE__, __TIME__,
                           szNowUct);

            /* register this logger as the release logger */
            RTLogRelSetDefaultInstance(pLogger);
        }
    }

    /*
     * Start VM (also from saved state) and track progress
     */
    LogFlow(("VMPowerUp\n"));

    /*
     * Create empty VM.
     */
    rc = VMR3Create(setVMErrorCallback, NULL, cfgmR3CreateDefault, NULL, &pVM);
    if (VBOX_FAILURE(rc))
    {
        RTPrintf("Error: VM creation failed with %Vrc.\n", rc);
        goto failure;
    }


    /*
     * Register VM state change handler
     */
    rc = VMR3AtStateRegister(pVM, vmstateChangeCallback, NULL);
    if (VBOX_FAILURE(rc))
    {
        RTPrintf("Error: VMR3AtStateRegister failed with %Vrc.\n", rc);
        goto failure;
    }

#ifdef VBOXBFE_WITH_USB
    /*
     * Capture USB devices.
     */
    if (fUSB)
    {
        gHostUSB = new HostUSB();
        gHostUSB->init(pVM);
    }
#endif /* VBOXBFE_WITH_USB */

#ifdef __L4ENV__
    /* L4 console cannot draw a host cursor */
    gMouse->setHostCursor(false);
#else
    gMouse->setHostCursor(true);
#endif

    /*
     * Power on the VM (i.e. start executing).
     */
    if (VBOX_SUCCESS(rc))
    {
        PVMREQ pReq;
        rc = VMR3ReqCall(pVM, &pReq, RT_INDEFINITE_WAIT, (PFNRT)VMR3PowerOn, 1, pVM);
        if (VBOX_SUCCESS(rc))
        {
            rc = pReq->iStatus;
            AssertRC(rc);
            VMR3ReqFree(pReq);
        }
        else
            AssertMsgFailed(("VMR3PowerOn failed, rc=%Vrc\n", rc));
    }

    /*
     * On failure destroy the VM.
     */
    if (VBOX_FAILURE(rc))
    {
        goto failure;
    }
    return 0;


failure:
    if (pVM)
    {
        rc2 = VMR3Destroy(pVM);
        AssertRC(rc2);
        pVM = NULL;
    }
    machineState = VMSTATE_TERMINATED;
    return 0;
}

/**
 * Register the main drivers.
 *
 * @returns VBox status code.
 * @param   pCallbacks      Pointer to the callback table.
 * @param   u32Version      VBox version number.
 */
DECLCALLBACK(int) VBoxDriversRegister(PCPDMDRVREGCB pCallbacks, uint32_t u32Version)
{
    int rc;

    LogFlow(("VBoxDriversRegister: u32Version=%#x\n", u32Version));
    AssertReleaseMsg(u32Version == VBOX_VERSION, ("u32Version=%#x VBOX_VERSION=%#x\n", u32Version, VBOX_VERSION));

    rc = pCallbacks->pfnRegister(pCallbacks, &Mouse::DrvReg);
    AssertRC(rc);
    if (VBOX_FAILURE(rc))
        return rc;
    rc = pCallbacks->pfnRegister(pCallbacks, &Keyboard::DrvReg);
    AssertRC(rc);
    if (VBOX_FAILURE(rc))
        return rc;

    rc = pCallbacks->pfnRegister(pCallbacks, &VMDisplay::DrvReg);
    AssertRC(rc);
    if (VBOX_FAILURE(rc))
        return rc;
    rc = pCallbacks->pfnRegister(pCallbacks, &VMMDev::DrvReg);
    AssertRC(rc);
    if (VBOX_FAILURE(rc))
        return rc;

    rc = pCallbacks->pfnRegister(pCallbacks, &VMStatus::DrvReg);
    if (VBOX_FAILURE(rc))
        return rc;

    return VINF_SUCCESS;
}


/**
 * Creates the default configuration.
 * This assumes an empty tree.
 *
 * @returns VBox status code.
 * @param   pVM     VM handle.
 */
static DECLCALLBACK(int) cfgmR3CreateDefault(PVM pVM, void *pvUser)
{
    int rcAll = VINF_SUCCESS;
    int rc;

#define UPDATERC() do { if (VBOX_FAILURE(rc) && VBOX_SUCCESS(rcAll)) rcAll = rc; } while (0)
#undef CHECK_RC                         /** @todo r=bird: clashes with VBox/com/Assert.h.  */
#define CHECK_RC()  UPDATERC()

    /*
     * Create VM default values.
     */
    PCFGMNODE pRoot = CFGMR3GetRoot(pVM);
    rc = CFGMR3InsertString(pRoot,  "Name",                 "Default VM");
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "RamSize",              memorySize * _1M);
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "TimerMillies",         10);
    UPDATERC();
#ifdef VBOXSDL_ADVANCED_OPTIONS
    rc = CFGMR3InsertInteger(pRoot, "RawR3Enabled",         (fRawR3 != ~0U) ? fRawR3 : 1);
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "RawR0Enabled",         (fRawR0 != ~0U) ? fRawR0 : 1);
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "PATMEnabled",          (fPATM != ~0U) ? fPATM : 1);
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "CSAMEnabled",          (fCSAM != ~0U) ? fCSAM : 1);
#else
    rc = CFGMR3InsertInteger(pRoot, "RawR3Enabled",         1);
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "RawR0Enabled",         1);
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "PATMEnabled",          1);
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "CSAMEnabled",          1);
#endif
    UPDATERC();

    /*
     * PDM.
     */
    rc = PDMR3RegisterDrivers(pVM, VBoxDriversRegister);
    UPDATERC();

    /*
     * Devices
     */
    PCFGMNODE pDevices = NULL;
    rc = CFGMR3InsertNode(pRoot, "Devices", &pDevices);
    UPDATERC();
    /* device */
    PCFGMNODE pDev = NULL;
    PCFGMNODE pInst = NULL;
    PCFGMNODE pCfg = NULL;
    PCFGMNODE pLunL0 = NULL;
    PCFGMNODE pLunL1 = NULL;

    /*
     * PC Arch.
     */
    rc = CFGMR3InsertNode(pDevices, "pcarch", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();

    /*
     * PC Bios.
     */
    rc = CFGMR3InsertNode(pDevices, "pcbios", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "RamSize",              memorySize * _1M);
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "BootDevice0",          pszBootDevice);
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "BootDevice1",          "NONE");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "BootDevice2",          "NONE");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "BootDevice3",          "NONE");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "HardDiskDevice",       "piix3ide");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "FloppyDevice",         "i82078");
    UPDATERC();

    /* Default: no bios logo. */
    rc = CFGMR3InsertInteger(pCfg,  "FadeIn",               1);
    UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "FadeOut",              0);
    UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "LogoTime",             0);
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "LogoFile",             "");
    UPDATERC();

    /*
     * ACPI
     */
    if (fACPI)
    {
        rc = CFGMR3InsertNode(pDevices, "acpi", &pDev);                             CHECK_RC();
        rc = CFGMR3InsertNode(pDev,     "0", &pInst);                               CHECK_RC();
        rc = CFGMR3InsertInteger(pInst, "Trusted", 1);              /* boolean */   CHECK_RC();
        rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                           CHECK_RC();
        rc = CFGMR3InsertInteger(pCfg,  "RamSize", memorySize * _1M);               CHECK_RC();
        rc = CFGMR3InsertInteger(pCfg,  "IOAPIC", g_fIOAPIC);                       CHECK_RC();
        rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          7);                 CHECK_RC();
        rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                 CHECK_RC();

        rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                          CHECK_RC();
        rc = CFGMR3InsertString(pLunL0, "Driver",               "ACPIHost");        CHECK_RC();
        rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                           CHECK_RC();
    }

    /*
     * PCI bus.
     */
    rc = CFGMR3InsertNode(pDevices, "pci", &pDev); /* piix3 */
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "IOAPIC", g_fIOAPIC);                       CHECK_RC();

    /*
     * DMA
     */
    rc = CFGMR3InsertNode(pDevices, "8237A", &pDev);                                CHECK_RC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "Trusted", 1);                  /* boolean */   CHECK_RC();

    /*
     * PCI bus.
     */
    rc = CFGMR3InsertNode(pDevices, "pci", &pDev); /* piix3 */                      CHECK_RC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   CHECK_RC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               CHECK_RC();

    /*
     * PS/2 keyboard & mouse.
     */
    rc = CFGMR3InsertNode(pDevices, "pckbd", &pDev);                                CHECK_RC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   CHECK_RC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               CHECK_RC();

    rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                              CHECK_RC();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "KeyboardQueue");       CHECK_RC();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                               CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "QueueSize",            64);                    CHECK_RC();

    rc = CFGMR3InsertNode(pLunL0,   "AttachedDriver", &pLunL1);                     CHECK_RC();
    rc = CFGMR3InsertString(pLunL1, "Driver",               "MainKeyboard");        CHECK_RC();
    rc = CFGMR3InsertNode(pLunL1,   "Config", &pCfg);                               CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "Object",     (uintptr_t)gKeyboard);            CHECK_RC();

    rc = CFGMR3InsertNode(pInst,    "LUN#1", &pLunL0);                              CHECK_RC();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "MouseQueue");          CHECK_RC();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                               CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "QueueSize",            128);                   CHECK_RC();

    rc = CFGMR3InsertNode(pLunL0,   "AttachedDriver", &pLunL1);                     CHECK_RC();
    rc = CFGMR3InsertString(pLunL1, "Driver",               "MainMouse");           CHECK_RC();
    rc = CFGMR3InsertNode(pLunL1,   "Config", &pCfg);                               CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "Object",     (uintptr_t)gMouse);               CHECK_RC();


    /*
     * i82078 Floppy drive controller
     */
    rc = CFGMR3InsertNode(pDevices, "i82078",    &pDev);                            CHECK_RC();
    rc = CFGMR3InsertNode(pDev,     "0",         &pInst);                           CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",   1);                                CHECK_RC();
    rc = CFGMR3InsertNode(pInst,    "Config",    &pCfg);                            CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "IRQ",       6);                                CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "DMA",       2);                                CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "MemMapped", 0 );                               CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "IOBase",    0x3f0);                            CHECK_RC();

    /* Attach the status driver */
    rc = CFGMR3InsertNode(pInst,    "LUN#999", &pLunL0);                            CHECK_RC();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "MainStatus");          CHECK_RC();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                               CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "papLeds", (uintptr_t)&mapFDLeds[0]);           CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "First",    0);                                 CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "Last",     0);                                 CHECK_RC();

    if (fdaFile)
    {
        rc = CFGMR3InsertNode(pInst,    "LUN#0",     &pLunL0);                      CHECK_RC();
        rc = CFGMR3InsertString(pLunL0, "Driver",    "Block");                      CHECK_RC();
        rc = CFGMR3InsertNode(pLunL0,   "Config",    &pCfg);                        CHECK_RC();
        rc = CFGMR3InsertString(pCfg,   "Type",      "Floppy 1.44");                CHECK_RC();
        rc = CFGMR3InsertInteger(pCfg,  "Mountable", 1);                            CHECK_RC();

        rc = CFGMR3InsertNode(pLunL0,   "AttachedDriver", &pLunL1);                 CHECK_RC();
        rc = CFGMR3InsertString(pLunL1, "Driver",          "RawImage");             CHECK_RC();
        rc = CFGMR3InsertNode(pLunL1,   "Config", &pCfg);                           CHECK_RC();
        rc = CFGMR3InsertString(pCfg,   "Path",         fdaFile);                   CHECK_RC();
    }

    /*
     * i8254 Programmable Interval Timer And Dummy Speaker
     */
    rc = CFGMR3InsertNode(pDevices, "i8254", &pDev);                                CHECK_RC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   CHECK_RC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               CHECK_RC();
#ifdef DEBUG
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   CHECK_RC();
#endif

    /*
     * i8259 Programmable Interrupt Controller.
     */
    rc = CFGMR3InsertNode(pDevices, "i8259", &pDev);                                CHECK_RC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   CHECK_RC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               CHECK_RC();

    /*
     * Advanced Programmable Interrupt Controller.
     */
    rc = CFGMR3InsertNode(pDevices, "apic", &pDev);                                 CHECK_RC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   CHECK_RC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               CHECK_RC();

    /*
     * I/O Advanced Programmable Interrupt Controller.
     */
    if (g_fIOAPIC)
    {
        rc = CFGMR3InsertNode(pDevices, "ioapic", &pDev);                           CHECK_RC();
        rc = CFGMR3InsertNode(pDev,     "0", &pInst);                               CHECK_RC();
        rc = CFGMR3InsertInteger(pInst, "Trusted",          1);     /* boolean */   CHECK_RC();
        rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                           CHECK_RC();
    }

    /*
     * RTC MC146818.
     */
    rc = CFGMR3InsertNode(pDevices, "mc146818", &pDev);                             CHECK_RC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   CHECK_RC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               CHECK_RC();

    /*
     * Serial ports
     */
    rc = CFGMR3InsertNode(pDevices, "serial", &pDev);                               CHECK_RC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   CHECK_RC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "IRQ",       4);                                CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "IOBase",    0x3f8);                            CHECK_RC();

    rc = CFGMR3InsertNode(pDev,     "1", &pInst);                                   CHECK_RC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "IRQ",       3);                                CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "IOBase",    0x2f8);                            CHECK_RC();

    /*
     * VGA.
     */
    rc = CFGMR3InsertNode(pDevices, "vga", &pDev);                                  CHECK_RC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          2);                     CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                     CHECK_RC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "VRamSize",             vramSize * _1M);        CHECK_RC();

#ifdef __L4ENV__
    /* XXX hard-coded */
    rc = CFGMR3InsertInteger(pCfg,  "HeightReduction", 18);                         CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "CustomVideoModes", 1);                         CHECK_RC();
    char szBuf[64];
    /* Tell the guest which is the ideal video mode to use */
    RTStrPrintf(szBuf, sizeof(szBuf), "%dx%dx%d",
                gFramebuffer->getHostXres(),
                gFramebuffer->getHostYres(),
                gFramebuffer->getHostBitsPerPixel());
    rc = CFGMR3InsertString(pCfg,   "CustomVideoMode1", szBuf);                     CHECK_RC();
#endif

    rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                              CHECK_RC();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "MainDisplay");         CHECK_RC();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                               CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "Object",     (uintptr_t)gDisplay);             CHECK_RC();

    /*
     * IDE (update this when the main interface changes)
     */
    rc = CFGMR3InsertNode(pDevices, "piix3ide", &pDev); /* piix3 */                 CHECK_RC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          1);                     CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        1);                     CHECK_RC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               CHECK_RC();

    if (hdaFile)
    {
        rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                          CHECK_RC();
        rc = CFGMR3InsertString(pLunL0, "Driver",              "Block");            CHECK_RC();
        rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                           CHECK_RC();
        rc = CFGMR3InsertString(pCfg,   "Type",                "HardDisk");         CHECK_RC();
        rc = CFGMR3InsertInteger(pCfg,  "Mountable",            0);                 CHECK_RC();

        rc = CFGMR3InsertNode(pLunL0,   "AttachedDriver", &pLunL1);                 CHECK_RC();
        rc = CFGMR3InsertString(pLunL1, "Driver",              "VBoxHDD");          CHECK_RC();
        rc = CFGMR3InsertNode(pLunL1,   "Config", &pCfg);                           CHECK_RC();
        rc = CFGMR3InsertString(pCfg,   "Path",                 hdaFile);           CHECK_RC();
    }

    if (cdromFile)
    {
        // ASSUME: DVD drive is always attached to LUN#2 (i.e. secondary IDE master)
        rc = CFGMR3InsertNode(pInst,    "LUN#2", &pLunL0);                          CHECK_RC();
        rc = CFGMR3InsertString(pLunL0, "Driver",               "Block");           CHECK_RC();
        rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                           CHECK_RC();
        rc = CFGMR3InsertString(pCfg,   "Type",                 "DVD");             CHECK_RC();
        rc = CFGMR3InsertInteger(pCfg,  "Mountable",            1);                 CHECK_RC();

        rc = CFGMR3InsertNode(pLunL0,   "AttachedDriver", &pLunL1);                 CHECK_RC();
        rc = CFGMR3InsertString(pLunL1, "Driver",          "MediaISO");             CHECK_RC();
        rc = CFGMR3InsertNode(pLunL1,   "Config", &pCfg);                           CHECK_RC();
        rc = CFGMR3InsertString(pCfg,   "Path",             cdromFile);             CHECK_RC();
    }

    /*
     * Network adapters
     */
    rc = CFGMR3InsertNode(pDevices, "pcnet", &pDev);                                CHECK_RC();
    for (ULONG ulInstance = 0; ulInstance < NetworkAdapterCount; ulInstance++)
    {
        if (g_aNetDevs[ulInstance].enmType != BFENETDEV::NOT_CONFIGURED)
        {
            char szInstance[4];
            RTStrPrintf(szInstance, sizeof(szInstance), "%lu", ulInstance);
            rc = CFGMR3InsertNode(pDev, szInstance, &pInst);                        CHECK_RC();
            rc = CFGMR3InsertInteger(pInst, "Trusted", 1);                          CHECK_RC();
            rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",
                                            !ulInstance ? 3 : ulInstance - 1 + 8);  CHECK_RC();
            rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo", 0);                    CHECK_RC();
            rc = CFGMR3InsertNode(pInst, "Config", &pCfg);                          CHECK_RC();
            rc = CFGMR3InsertBytes(pCfg, "MAC", &g_aNetDevs[ulInstance].Mac, sizeof(PDMMAC));
                                                                                    CHECK_RC();

            /*
             * Enable the packet sniffer if requested.
             */
            if (g_aNetDevs[ulInstance].fSniff)
            {
                /* insert the sniffer filter driver. */
                rc = CFGMR3InsertNode(pInst, "LUN#0", &pLunL0);                     CHECK_RC();
                rc = CFGMR3InsertString(pLunL0, "Driver", "NetSniffer");            CHECK_RC();
                rc = CFGMR3InsertNode(pLunL0, "Config", &pCfg);                     CHECK_RC();
                if (g_aNetDevs[ulInstance].pszSniff)
                {
                    rc = CFGMR3InsertString(pCfg, "File", g_aNetDevs[ulInstance].pszSniff);  CHECK_RC();
                }
            }

            /*
             * Create the driver config (if any).
             */
            if (g_aNetDevs[ulInstance].enmType != BFENETDEV::NONE)
            {
                if (g_aNetDevs[ulInstance].fSniff)
                {
                    rc = CFGMR3InsertNode(pLunL0, "AttachedDriver", &pLunL0);       CHECK_RC();
                }
                else
                {
                    rc = CFGMR3InsertNode(pInst, "LUN#0", &pLunL0);                 CHECK_RC();
                }
                rc = CFGMR3InsertNode(pLunL0, "Config", &pCfg);                     CHECK_RC();
            }

            /*
             * Configure the driver.
             */
            if (g_aNetDevs[ulInstance].enmType == BFENETDEV::NAT)
            {
                rc = CFGMR3InsertString(pLunL0, "Driver", "NAT");                   CHECK_RC();
                rc = CFGMR3InsertNode(pLunL0, "Config", &pCfg);                     CHECK_RC();
                /* (Port forwarding goes here.) */
            }
            else if (g_aNetDevs[ulInstance].enmType == BFENETDEV::HIF)
            {
                rc = CFGMR3InsertString(pLunL0, "Driver", "HostInterface");         CHECK_RC();
                rc = CFGMR3InsertNode(pLunL0, "Config", &pCfg);                     CHECK_RC();

#if defined(__LINUX__)
                if (g_aNetDevs[ulInstance].fHaveFd)
                {
                    rc = CFGMR3InsertString(pCfg, "Device", g_aNetDevs[ulInstance].pszName);        CHECK_RC();
                    rc = CFGMR3InsertInteger(pCfg, "FileHandle", g_aNetDevs[ulInstance].fd);        CHECK_RC();
                }
                else
#endif
                {
#if defined (__LINUX__) || defined (__L4__)
                    /*
                     * Create/Open the TAP the device.
                     */
                    RTFILE tapFD;
                    rc = RTFileOpen(&tapFD, "/dev/net/tun",
                                    RTFILE_O_READWRITE | RTFILE_O_OPEN |
                                    RTFILE_O_DENY_NONE | RTFILE_O_INHERIT);
                    if (VBOX_FAILURE(rc))
                    {
                        FatalError("Failed to open /dev/net/tun: %Vrc\n", rc);
                        return rc;
                    }

                    struct ifreq IfReq;
                    memset(&IfReq, 0, sizeof(IfReq));
                    if (g_aNetDevs[ulInstance].pszName && g_aNetDevs[ulInstance].pszName[0])
                    {
                        size_t cch = strlen(g_aNetDevs[ulInstance].pszName);
                        if (cch >= sizeof(IfReq.ifr_name))
                        {
                            FatalError("HIF name too long for device #%d: %s\n",
                                       ulInstance + 1, g_aNetDevs[ulInstance].pszName);
                            return VERR_BUFFER_OVERFLOW;
                        }
                        memcpy(IfReq.ifr_name, g_aNetDevs[ulInstance].pszName, cch + 1);
                    }
                    else
                        strcpy(IfReq.ifr_name, "tun%d");
                    IfReq.ifr_flags = IFF_TAP | IFF_NO_PI;
                    rc = ioctl(tapFD, TUNSETIFF, &IfReq);
                    if (rc)
                    {
                        int rc2 = RTErrConvertFromErrno(errno);
                        FatalError("ioctl TUNSETIFF '%s' failed: errno=%d rc=%d (%Vrc)\n",
                                   IfReq.ifr_name, errno, rc, rc2);
                        return rc2;
                    }

                    rc = fcntl(tapFD, F_SETFL, O_NONBLOCK);
                    if (rc)
                    {
                        int rc2 = RTErrConvertFromErrno(errno);
                        FatalError("fcntl F_SETFL/O_NONBLOCK '%s' failed: errno=%d rc=%d (%Vrc)\n",
                                   IfReq.ifr_name, errno, rc, rc2);
                        return rc2;
                    }

                    rc = CFGMR3InsertString(pCfg, "Device", g_aNetDevs[ulInstance].pszName);        CHECK_RC();
                    rc = CFGMR3InsertInteger(pCfg, "FileHandle", (RTFILE)tapFD);                    CHECK_RC();

#elif defined(__WIN__)
                    /*
                     * We need the GUID too here...
                     */
                    rc = CFGMR3InsertString(pCfg, "Device", g_aNetDevs[ulInstance].pszName);            CHECK_RC();
                    rc = CFGMR3InsertString(pCfg, "HostInterfaceName", g_aNetDevs[ulInstance].pszName); CHECK_RC();
                    rc = CFGMR3InsertString(pCfg, "GUID", g_aNetDevs[ulInstance].pszName /*pszGUID*/);  CHECK_RC();


#else /* !__LINUX__ && !__L4__ */
                    FatalError("Name based HIF devices not implemented yet for this host platform\n");
                    return VERR_NOT_IMPLEMENTED;
#endif
                }
            }
            else if (g_aNetDevs[ulInstance].enmType == BFENETDEV::INTNET)
            {
                /*
                 * Internal networking.
                 */
                rc = CFGMR3InsertString(pCfg, "Network", g_aNetDevs[ulInstance].pszName); CHECK_RC();
            }
        }
    }

    /*
     * VMM Device
     */
    rc = CFGMR3InsertNode(pDevices, "VMMDev", &pDev);                               CHECK_RC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);                                   CHECK_RC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                               CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);     /* boolean */   CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",          4);                     CHECK_RC();
    rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",        0);                     CHECK_RC();

    /* the VMM device's Main driver */
    rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                              CHECK_RC();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "MainVMMDev");          CHECK_RC();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                               CHECK_RC();
    rc = CFGMR3InsertInteger(pCfg,  "Object",     (uintptr_t)gVMMDev);              CHECK_RC();

    /*
     * AC'97 ICH audio
     */
    if (fAudio)
    {
        rc = CFGMR3InsertNode(pDevices, "ichac97", &pDev);
        rc = CFGMR3InsertNode(pDev,     "0", &pInst);
        rc = CFGMR3InsertInteger(pInst, "Trusted",          1);     /* boolean */   CHECK_RC();
        rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",      5);                     CHECK_RC();
        rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",    0);                     CHECK_RC();
        rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);

        /* the Audio driver */
        rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                          CHECK_RC();
        rc = CFGMR3InsertString(pLunL0, "Driver",               "AUDIO");           CHECK_RC();
        rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                           CHECK_RC();
#ifdef __WIN32__
        rc = CFGMR3InsertString(pCfg, "AudioDriver", "winmm");                      CHECK_RC();
#else /* !__WIN32__ */
        rc = CFGMR3InsertString(pCfg, "AudioDriver", "oss");                        CHECK_RC();
#endif /* !__WIN32__ */
    }

#ifdef VBOXBFE_WITH_USB
    /*
     * The USB Controller.
     */
    if (fUSB)
    {
        rc = CFGMR3InsertNode(pDevices, "usb-ohci", &pDev);                         CHECK_RC();
        rc = CFGMR3InsertNode(pDev,     "0", &pInst);                               CHECK_RC();
        rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);                           CHECK_RC();
        rc = CFGMR3InsertInteger(pInst, "Trusted",          1);     /* boolean */   CHECK_RC();
        rc = CFGMR3InsertInteger(pInst, "PCIDeviceNo",      6);                     CHECK_RC();
        rc = CFGMR3InsertInteger(pInst, "PCIFunctionNo",    0);                     CHECK_RC();

        rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);                          CHECK_RC();
        rc = CFGMR3InsertString(pLunL0, "Driver",               "VUSBRootHub");     CHECK_RC();
        rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);                           CHECK_RC();
    }
#endif /* VBOXBFE_WITH_USB */

#undef UPDATERC
#undef CHECK_RC

    return rc;
}
