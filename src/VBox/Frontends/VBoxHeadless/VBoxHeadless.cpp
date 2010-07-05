/* $Id$ */
/** @file
 * VBoxHeadless - The VirtualBox Headless frontend for running VMs on servers.
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

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/array.h>
#include <VBox/com/Guid.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/EventQueue.h>

#include <VBox/com/VirtualBox.h>

using namespace com;

#define LOG_GROUP LOG_GROUP_GUI

#include <VBox/log.h>
#include <VBox/version.h>
#ifdef VBOX_WITH_VRDP
# include <VBox/vrdpapi.h>
#endif
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/ldr.h>
#include <iprt/getopt.h>
#include <iprt/env.h>
#include <VBox/err.h>
#include <VBox/VBoxVideo.h>

#ifdef VBOX_FFMPEG
#include <cstdlib>
#include <cerrno>
#include "VBoxHeadless.h"
#include <iprt/env.h>
#include <iprt/param.h>
#include <iprt/process.h>
#include <VBox/sup.h>
#endif

//#define VBOX_WITH_SAVESTATE_ON_SIGNAL
#ifdef VBOX_WITH_SAVESTATE_ON_SIGNAL
#include <signal.h>
#endif

#ifdef VBOX_WITH_VRDP
# include "Framebuffer.h"
#endif
#ifdef VBOX_WITH_VNC
# include "FramebufferVNC.h"
#endif


////////////////////////////////////////////////////////////////////////////////

#define LogError(m,rc) \
    do { \
        Log(("VBoxHeadless: ERROR: " m " [rc=0x%08X]\n", rc)); \
        RTPrintf("%s\n", m); \
    } while (0)

////////////////////////////////////////////////////////////////////////////////

/* global weak references (for event handlers) */
static ISession *gSession = NULL;
static IConsole *gConsole = NULL;
static EventQueue *gEventQ = NULL;

#ifdef VBOX_WITH_VNC
static VNCFB *g_pFramebufferVNC;
#endif


////////////////////////////////////////////////////////////////////////////////

/**
 *  State change event.
 */
class StateChangeEvent : public Event
{
public:
    StateChangeEvent(MachineState_T state) : mState(state) {}
protected:
    void *handler()
    {
        LogFlow(("VBoxHeadless: StateChangeEvent: %d\n", mState));
        /* post the termination event if the machine has been PoweredDown/Saved/Aborted */
        if (mState < MachineState_Running)
            gEventQ->postEvent(NULL);
        return 0;
    }
private:
    MachineState_T mState;
};

/**
 *  Handler for global events.
 */
class VirtualBoxEventListener :
  VBOX_SCRIPTABLE_IMPL(IEventListener)
{
public:
    VirtualBoxEventListener()
    {
#ifndef VBOX_WITH_XPCOM
        refcnt = 0;
#endif
        mfNoLoggedInUsers = true;
    }

    virtual ~VirtualBoxEventListener()
    {
    }

#ifndef VBOX_WITH_XPCOM
    STDMETHOD_(ULONG, AddRef)()
    {
        return ::InterlockedIncrement(&refcnt);
    }
    STDMETHOD_(ULONG, Release)()
    {
        long cnt = ::InterlockedDecrement(&refcnt);
        if (cnt == 0)
            delete this;
        return cnt;
    }
#endif
    VBOX_SCRIPTABLE_DISPATCH_IMPL(IEventListener)

    NS_DECL_ISUPPORTS

    STDMETHOD(HandleEvent)(IEvent * aEvent)
    {
        VBoxEventType_T aType = VBoxEventType_Invalid;

        aEvent->COMGETTER(Type)(&aType);
        switch (aType)
        {
            case VBoxEventType_OnGuestPropertyChange:
            {
                ComPtr<IGuestPropertyChangeEvent> gpcev = aEvent;
                Assert(gpcev);

                Bstr aKey;
                gpcev->COMGETTER(Name)(aKey.asOutParam());

                if (aKey == Bstr("/VirtualBox/GuestInfo/OS/NoLoggedInUsers"))
                {
                    /* Check if this is our machine and the "disconnect on logout feature" is enabled. */
                    BOOL fProcessDisconnectOnGuestLogout = FALSE;
                    ComPtr <IMachine> machine;
                    HRESULT hrc = S_OK;

                    if (gConsole)
                    {
                        hrc = gConsole->COMGETTER(Machine)(machine.asOutParam());
                        if (SUCCEEDED(hrc) && machine)
                        {
                            Bstr id, machineId;
                            hrc = machine->COMGETTER(Id)(id.asOutParam());
                            gpcev->COMGETTER(MachineId)(machineId.asOutParam());
                            if (id == machineId)
                            {
                                Bstr value1;
                                hrc = machine->GetExtraData(Bstr("VRDP/DisconnectOnGuestLogout"), value1.asOutParam());
                                if (SUCCEEDED(hrc) && value1 == "1")
                                {
                                    fProcessDisconnectOnGuestLogout = TRUE;
                                }
                            }
                        }
                    }

                    if (fProcessDisconnectOnGuestLogout)
                    {
                        Bstr value;
                        gpcev->COMGETTER(Value)(value.asOutParam());
                        Utf8Str utf8Value = value;
                        if (utf8Value == "true")
                        {
                            if (!mfNoLoggedInUsers) /* Only if the property really changes. */
                            {
                                mfNoLoggedInUsers = true;

                                /* If there is a VRDP connection, drop it. */
                                ComPtr<IRemoteDisplayInfo> info;
                                hrc = gConsole->COMGETTER(RemoteDisplayInfo)(info.asOutParam());
                                if (SUCCEEDED(hrc) && info)
                                {
                                    ULONG cClients = 0;
                                    hrc = info->COMGETTER(NumberOfClients)(&cClients);
                                    if (SUCCEEDED(hrc) && cClients > 0)
                                    {
                                        ComPtr <IVRDPServer> vrdpServer;
                                        hrc = machine->COMGETTER(VRDPServer)(vrdpServer.asOutParam());
                                        if (SUCCEEDED(hrc) && vrdpServer)
                                        {
                                            vrdpServer->COMSETTER(Enabled)(FALSE);
                                            vrdpServer->COMSETTER(Enabled)(TRUE);
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            mfNoLoggedInUsers = false;
                        }
                    }
                }
                break;
            }
            default:
                AssertFailed();
        }

        return S_OK;
    }

private:
#ifndef VBOX_WITH_XPCOM
    long refcnt;
#endif

    bool mfNoLoggedInUsers;
};


/**
 *  Handler for machine events.
 */
class ConsoleEventListener :
  VBOX_SCRIPTABLE_IMPL(IEventListener)
{
public:
    ConsoleEventListener()
    {
#ifndef VBOX_WITH_XPCOM
        refcnt = 0;
#endif
        mLastVRDPPort = -1;
    }

    virtual ~ConsoleEventListener()
    {
    }

#ifndef VBOX_WITH_XPCOM
    STDMETHOD_(ULONG, AddRef)()
    {
        return ::InterlockedIncrement(&refcnt);
    }
    STDMETHOD_(ULONG, Release)()
    {
        long cnt = ::InterlockedDecrement(&refcnt);
        if (cnt == 0)
            delete this;
        return cnt;
    }
#endif
    VBOX_SCRIPTABLE_DISPATCH_IMPL(IEventListener)

    NS_DECL_ISUPPORTS

    STDMETHOD(HandleEvent)(IEvent * aEvent)
    {
        VBoxEventType_T aType = VBoxEventType_Invalid;

        aEvent->COMGETTER(Type)(&aType);
        switch (aType)
        {
            case VBoxEventType_OnMouseCapabilityChange:
            {

                ComPtr<IMouseCapabilityChangeEvent> mccev = aEvent;
                Assert(mccev);

                BOOL fSupportsAbsolute = false;
                mccev->COMGETTER(SupportsAbsolute)(&fSupportsAbsolute);

                /* Emit absolute mouse event to actually enable the host mouse cursor. */
                if (fSupportsAbsolute && gConsole)
                {
                    ComPtr<IMouse> mouse;
                    gConsole->COMGETTER(Mouse)(mouse.asOutParam());
                    if (mouse)
                    {
                        mouse->PutMouseEventAbsolute(-1, -1, 0, 0 /* Horizontal wheel */, 0);
                    }
                }
#ifdef VBOX_WITH_VNC
                if (g_pFramebufferVNC)
                    g_pFramebufferVNC->enableAbsMouse(fSupportsAbsolute);
#endif
                break;
            }
            case VBoxEventType_OnStateChange:
            {
                ComPtr<IStateChangeEvent> scev = aEvent;
                Assert(scev);

                MachineState_T machineState;
                scev->COMGETTER(State)(&machineState);

                gEventQ->postEvent(new StateChangeEvent(machineState));
                break;
            }
            case VBoxEventType_OnRemoteDisplayInfoChange:
            {
                ComPtr<IRemoteDisplayInfoChangeEvent> rdicev = aEvent;
                Assert(rdicev);

#ifdef VBOX_WITH_VRDP
                if (gConsole)
                {
                    ComPtr<IRemoteDisplayInfo> info;
                    gConsole->COMGETTER(RemoteDisplayInfo)(info.asOutParam());
                    if (info)
                    {
                        LONG port;
                        info->COMGETTER(Port)(&port);
                        if (port != mLastVRDPPort)
                        {
                            if (port == -1)
                                RTPrintf("VRDP server is inactive.\n");
                            else if (port == 0)
                                RTPrintf("VRDP server failed to start.\n");
                            else
                                RTPrintf("Listening on port %d.\n", port);

                            mLastVRDPPort = port;
                        }
                    }
                }
#endif
                break;
            }
            case VBoxEventType_OnCanShowWindow:
            {
                ComPtr<ICanShowWindowEvent> cswev = aEvent;
                Assert(cswev);
                cswev->AddVeto(NULL);
                break;
            }
            case VBoxEventType_OnShowWindow:
            {
                ComPtr<IShowWindowEvent> swev = aEvent;
                Assert(swev);
                swev->COMSETTER(WinId)(0);
                break;
            }
            default:
                AssertFailed();
        }
        return S_OK;
    }

private:

#ifndef VBOX_WITH_XPCOM
    long refcnt;
#endif
    long mLastVRDPPort;
};

#ifdef VBOX_WITH_XPCOM
NS_DECL_CLASSINFO(VirtualBoxEventListener)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(VirtualBoxEventListener, IEventListener)
NS_DECL_CLASSINFO(ConsoleEventListener)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(ConsoleEventListener, IEventListener)
#endif

#ifdef VBOX_WITH_SAVESTATE_ON_SIGNAL
static void SaveState(int sig)
{
    ComPtr <IProgress> progress = NULL;

/** @todo Deal with nested signals, multithreaded signal dispatching (esp. on windows),
 * and multiple signals (both SIGINT and SIGTERM in some order).
 * Consider processing the signal request asynchronously since there are lots of things
 * which aren't safe (like RTPrintf and printf IIRC) in a signal context. */

    RTPrintf("Signal received, saving state.\n");

    HRESULT rc = gConsole->SaveState(progress.asOutParam());
    if (FAILED(S_OK))
    {
        RTPrintf("Error saving state! rc = 0x%x\n", rc);
        return;
    }
    Assert(progress);
    LONG cPercent = 0;

    RTPrintf("0%%");
    RTStrmFlush(g_pStdOut);
    for (;;)
    {
        BOOL fCompleted = false;
        rc = progress->COMGETTER(Completed)(&fCompleted);
        if (FAILED(rc) || fCompleted)
            break;
        ULONG cPercentNow;
        rc = progress->COMGETTER(Percent)(&cPercentNow);
        if (FAILED(rc))
            break;
        if ((cPercentNow / 10) != (cPercent / 10))
        {
            cPercent = cPercentNow;
            RTPrintf("...%d%%", cPercentNow);
            RTStrmFlush(g_pStdOut);
        }

        /* wait */
        rc = progress->WaitForCompletion(100);
    }

    HRESULT lrc;
    rc = progress->COMGETTER(ResultCode)(&lrc);
    if (FAILED(rc))
        lrc = ~0;
    if (!lrc)
    {
        RTPrintf(" -- Saved the state successfully.\n");
        RTThreadYield();
    }
    else
        RTPrintf("-- Error saving state, lrc=%d (%#x)\n", lrc, lrc);

}
#endif /* VBOX_WITH_SAVESTATE_ON_SIGNAL */

////////////////////////////////////////////////////////////////////////////////

static void show_usage()
{
    RTPrintf("Usage:\n"
             "   -s, -startvm, --startvm <name|uuid>   Start given VM (required argument)\n"
#ifdef VBOX_WITH_VNC
             "   -n, --vnc                             Enable the built in VNC server\n"
             "   -m, --vncport <port>                  TCP port number to use for the VNC server\n"
             "   -o, --vncpass <pw>                    Set the VNC server password\n"
#endif
#ifdef VBOX_WITH_VRDP
             "   -v, -vrdp, --vrdp on|off|config       Enable (default) or disable the VRDP\n"
             "                                         server or don't change the setting\n"
             "   -p, -vrdpport, --vrdpport <ports>     Comma-separated list of ports the VRDP\n"
             "                                         server can bind to. Use a dash between\n"
             "                                         two port numbers to specify a range\n"
             "   -a, -vrdpaddress, --vrdpaddress <ip>  Interface IP the VRDP will bind to \n"
#endif
#ifdef VBOX_FFMPEG
             "   -c, -capture, --capture               Record the VM screen output to a file\n"
             "   -w, --width                           Frame width when recording\n"
             "   -h, --height                          Frame height when recording\n"
             "   -r, --bitrate                         Recording bit rate when recording\n"
             "   -f, --filename                        File name when recording.  The codec\n"
             "                                         used will be chosen based on the\n"
             "                                         file extension\n"
#endif
             "\n");
}

#ifdef VBOX_FFMPEG
/**
 * Parse the environment for variables which can influence the FFMPEG settings.
 * purely for backwards compatibility.
 * @param pulFrameWidth may be updated with a desired frame width
 * @param pulFrameHeight may be updated with a desired frame height
 * @param pulBitRate may be updated with a desired bit rate
 * @param ppszFileName may be updated with a desired file name
 */
static void parse_environ(unsigned long *pulFrameWidth, unsigned long *pulFrameHeight,
                          unsigned long *pulBitRate, const char **ppszFileName)
{
    const char *pszEnvTemp;

    if ((pszEnvTemp = RTEnvGet("VBOX_CAPTUREWIDTH")) != 0)
    {
        errno = 0;
        unsigned long ulFrameWidth = strtoul(pszEnvTemp, 0, 10);
        if (errno != 0)
            LogError("VBoxHeadless: ERROR: invalid VBOX_CAPTUREWIDTH environment variable", 0);
        else
            *pulFrameWidth = ulFrameWidth;
    }
    if ((pszEnvTemp = RTEnvGet("VBOX_CAPTUREHEIGHT")) != 0)
    {
        errno = 0;
        unsigned long ulFrameHeight = strtoul(pszEnvTemp, 0, 10);
        if (errno != 0)
            LogError("VBoxHeadless: ERROR: invalid VBOX_CAPTUREHEIGHT environment variable", 0);
        else
            *pulFrameHeight = ulFrameHeight;
    }
    if ((pszEnvTemp = RTEnvGet("VBOX_CAPTUREBITRATE")) != 0)
    {
        errno = 0;
        unsigned long ulBitRate = strtoul(pszEnvTemp, 0, 10);
        if (errno != 0)
            LogError("VBoxHeadless: ERROR: invalid VBOX_CAPTUREBITRATE environment variable", 0);
        else
            *pulBitRate = ulBitRate;
    }
    if ((pszEnvTemp = RTEnvGet("VBOX_CAPTUREFILE")) != 0)
        *ppszFileName = pszEnvTemp;
}
#endif /* VBOX_FFMPEG defined */

/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
#ifdef VBOX_WITH_VRDP
    const char *vrdpPort = NULL;
    const char *vrdpAddress = NULL;
    const char *vrdpEnabled = NULL;
#endif
#ifdef VBOX_WITH_VNC
    bool        fVNCEnable      = false;
    unsigned    uVNCPort        = 0;          /* default port */
    char const *pszVNCPassword  = NULL;       /* no password */
#endif
    unsigned fRawR0 = ~0U;
    unsigned fRawR3 = ~0U;
    unsigned fPATM  = ~0U;
    unsigned fCSAM  = ~0U;
#ifdef VBOX_FFMPEG
    unsigned fFFMPEG = 0;
    unsigned long ulFrameWidth = 800;
    unsigned long ulFrameHeight = 600;
    unsigned long ulBitRate = 300000;
    char pszMPEGFile[RTPATH_MAX];
    const char *pszFileNameParam = "VBox-%d.vob";
#endif /* VBOX_FFMPEG */

    /* Make sure that DISPLAY is unset, so that X11 bits do not get initialised
     * on X11-using OSes. */
    /** @todo this should really be taken care of in Main. */
    RTEnvUnset("DISPLAY");

    LogFlow (("VBoxHeadless STARTED.\n"));
    RTPrintf (VBOX_PRODUCT " Headless Interface " VBOX_VERSION_STRING "\n"
              "(C) 2008-" VBOX_C_YEAR " " VBOX_VENDOR "\n"
              "All rights reserved.\n\n");

    Bstr id;
    /* the below cannot be Bstr because on Linux Bstr doesn't work until XPCOM (nsMemory) is initialized */
    const char *name = NULL;

#ifdef VBOX_FFMPEG
    /* Parse the environment */
    parse_environ(&ulFrameWidth, &ulFrameHeight, &ulBitRate, &pszFileNameParam);
#endif

    enum eHeadlessOptions
    {
        OPT_RAW_R0 = 0x100,
        OPT_NO_RAW_R0,
        OPT_RAW_R3,
        OPT_NO_RAW_R3,
        OPT_PATM,
        OPT_NO_PATM,
        OPT_CSAM,
        OPT_NO_CSAM,
        OPT_COMMENT
    };

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "-startvm", 's', RTGETOPT_REQ_STRING },
        { "--startvm", 's', RTGETOPT_REQ_STRING },
#ifdef VBOX_WITH_VRDP
        { "-vrdpport", 'p', RTGETOPT_REQ_STRING },
        { "--vrdpport", 'p', RTGETOPT_REQ_STRING },
        { "-vrdpaddress", 'a', RTGETOPT_REQ_STRING },
        { "--vrdpaddress", 'a', RTGETOPT_REQ_STRING },
        { "-vrdp", 'v', RTGETOPT_REQ_STRING },
        { "--vrdp", 'v', RTGETOPT_REQ_STRING },
#endif /* VBOX_WITH_VRDP defined */
#ifdef VBOX_WITH_VNC
        { "--vncport", 'm', RTGETOPT_REQ_INT32 },
        { "--vncpass", 'o', RTGETOPT_REQ_STRING },
        { "--vnc", 'n', 0 },
#endif /* VBOX_WITH_VNC */
        { "-rawr0", OPT_RAW_R0, 0 },
        { "--rawr0", OPT_RAW_R0, 0 },
        { "-norawr0", OPT_NO_RAW_R0, 0 },
        { "--norawr0", OPT_NO_RAW_R0, 0 },
        { "-rawr3", OPT_RAW_R3, 0 },
        { "--rawr3", OPT_RAW_R3, 0 },
        { "-norawr3", OPT_NO_RAW_R3, 0 },
        { "--norawr3", OPT_NO_RAW_R3, 0 },
        { "-patm", OPT_PATM, 0 },
        { "--patm", OPT_PATM, 0 },
        { "-nopatm", OPT_NO_PATM, 0 },
        { "--nopatm", OPT_NO_PATM, 0 },
        { "-csam", OPT_CSAM, 0 },
        { "--csam", OPT_CSAM, 0 },
        { "-nocsam", OPT_NO_CSAM, 0 },
        { "--nocsam", OPT_NO_CSAM, 0 },
#ifdef VBOX_FFMPEG
        { "-capture", 'c', 0 },
        { "--capture", 'c', 0 },
        { "--width", 'w', RTGETOPT_REQ_UINT32 },
        { "--height", 'h', RTGETOPT_REQ_UINT32 }, /* great choice of short option! */
        { "--bitrate", 'r', RTGETOPT_REQ_UINT32 },
        { "--filename", 'f', RTGETOPT_REQ_STRING },
#endif /* VBOX_FFMPEG defined */
        { "-comment", OPT_COMMENT, RTGETOPT_REQ_STRING },
        { "--comment", OPT_COMMENT, RTGETOPT_REQ_STRING }
    };

    // parse the command line
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch(ch)
        {
            case 's':
                id = asGuidStr(ValueUnion.psz);
                /* If the argument was not a UUID, then it must be a name. */
                if (id.isEmpty())
                    name = ValueUnion.psz;
                break;
#ifdef VBOX_WITH_VRDP
            case 'p':
                vrdpPort = ValueUnion.psz;
                break;
            case 'a':
                vrdpAddress = ValueUnion.psz;
                break;
            case 'v':
                vrdpEnabled = ValueUnion.psz;
                break;
#endif /* VBOX_WITH_VRDP defined */
#ifdef VBOX_WITH_VNC
            case 'n':
                fVNCEnable = true;
                break;
            case 'm':
                uVNCPort = ValueUnion.i32;
                break;
            case 'o':
                pszVNCPassword = ValueUnion.psz;
                break;
#endif /* VBOX_WITH_VNC */
            case OPT_RAW_R0:
                fRawR0 = true;
                break;
            case OPT_NO_RAW_R0:
                fRawR0 = false;
                break;
            case OPT_RAW_R3:
                fRawR3 = true;
                break;
            case OPT_NO_RAW_R3:
                fRawR3 = false;
                break;
            case OPT_PATM:
                fPATM = true;
                break;
            case OPT_NO_PATM:
                fPATM = false;
                break;
            case OPT_CSAM:
                fCSAM = true;
                break;
            case OPT_NO_CSAM:
                fCSAM = false;
                break;
#ifdef VBOX_FFMPEG
            case 'c':
                fFFMPEG = true;
                break;
            case 'w':
                ulFrameWidth = ValueUnion.u32;
                break;
            case 'r':
                ulBitRate = ValueUnion.u32;
                break;
            case 'f':
                pszFileNameParam = ValueUnion.psz;
                break;
#endif /* VBOX_FFMPEG defined */
            case 'h':
#ifdef VBOX_FFMPEG
                if ((GetState.pDef->fFlags & RTGETOPT_REQ_MASK) != RTGETOPT_REQ_NOTHING)
                {
                    ulFrameHeight = ValueUnion.u32;
                    break;
                }
#endif
                show_usage();
                return 0;
            case OPT_COMMENT:
                /* nothing to do */
                break;
            case 'V':
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                return 0;
            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                show_usage();
                return ch;
        }
    }

#ifdef VBOX_FFMPEG
    if (ulFrameWidth < 512 || ulFrameWidth > 2048 || ulFrameWidth % 2)
    {
        LogError("VBoxHeadless: ERROR: please specify an even frame width between 512 and 2048", 0);
        return 1;
    }
    if (ulFrameHeight < 384 || ulFrameHeight > 1536 || ulFrameHeight % 2)
    {
        LogError("VBoxHeadless: ERROR: please specify an even frame height between 384 and 1536", 0);
        return 1;
    }
    if (ulBitRate < 300000 || ulBitRate > 1000000)
    {
        LogError("VBoxHeadless: ERROR: please specify an even bitrate between 300000 and 1000000", 0);
        return 1;
    }
    /* Make sure we only have %d or %u (or none) in the file name specified */
    char *pcPercent = (char*)strchr(pszFileNameParam, '%');
    if (pcPercent != 0 && *(pcPercent + 1) != 'd' && *(pcPercent + 1) != 'u')
    {
        LogError("VBoxHeadless: ERROR: Only %%d and %%u are allowed in the capture file name.", -1);
        return 1;
    }
    /* And no more than one % in the name */
    if (pcPercent != 0 && strchr(pcPercent + 1, '%') != 0)
    {
        LogError("VBoxHeadless: ERROR: Only one format modifier is allowed in the capture file name.", -1);
        return 1;
    }
    RTStrPrintf(&pszMPEGFile[0], RTPATH_MAX, pszFileNameParam, RTProcSelf());
#endif /* defined VBOX_FFMPEG */

    if (id.isEmpty() && !name)
    {
        show_usage();
        return 1;
    }

    HRESULT rc;

    rc = com::Initialize();
    if (FAILED(rc))
    {
        RTPrintf("VBoxHeadless: ERROR: failed to initialize COM!\n");
        return 1;
    }

    ComPtr<IVirtualBox> virtualBox;
    ComPtr<ISession> session;
    bool fSessionOpened = false;
    IEventListener *vboxListener = NULL, *consoleListener = NULL;

    do
    {
        rc = virtualBox.createLocalObject(CLSID_VirtualBox);
        if (FAILED(rc))
            RTPrintf("VBoxHeadless: ERROR: failed to create the VirtualBox object!\n");
        else
        {
            rc = session.createInprocObject(CLSID_Session);
            if (FAILED(rc))
                RTPrintf("VBoxHeadless: ERROR: failed to create a session object!\n");
        }

        if (FAILED(rc))
        {
            com::ErrorInfo info;
            if (!info.isFullAvailable() && !info.isBasicAvailable())
            {
                com::GluePrintRCMessage(rc);
                RTPrintf("Most likely, the VirtualBox COM server is not running or failed to start.\n");
            }
            else
                GluePrintErrorInfo(info);
            break;
        }

        /* find ID by name */
        if (id.isEmpty())
        {
            ComPtr <IMachine> m;
            rc = virtualBox->FindMachine(Bstr(name), m.asOutParam());
            if (FAILED(rc))
            {
                LogError("Invalid machine name!\n", rc);
                break;
            }
            m->COMGETTER(Id)(id.asOutParam());
            AssertComRC(rc);
            if (FAILED(rc))
                break;
        }

        Log(("VBoxHeadless: Opening a session with machine (id={%s})...\n",
              Utf8Str(id).raw()));

        // open a session
        CHECK_ERROR_BREAK(virtualBox, OpenSession(session, id));
        fSessionOpened = true;

        /* get the console */
        ComPtr <IConsole> console;
        CHECK_ERROR_BREAK(session, COMGETTER(Console)(console.asOutParam()));

        /* get the machine */
        ComPtr <IMachine> machine;
        CHECK_ERROR_BREAK(console, COMGETTER(Machine)(machine.asOutParam()));

        ComPtr <IDisplay> display;
        CHECK_ERROR_BREAK(console, COMGETTER(Display)(display.asOutParam()));

#ifdef VBOX_FFMPEG
        IFramebuffer *pFramebuffer = 0;
        RTLDRMOD hLdrFFmpegFB;
        PFNREGISTERFFMPEGFB pfnRegisterFFmpegFB;

        if (fFFMPEG)
        {
            int rrc = VINF_SUCCESS, rcc = S_OK;

            Log2(("VBoxHeadless: loading VBoxFFmpegFB shared library\n"));
            rrc = SUPR3HardenedLdrLoadAppPriv("VBoxFFmpegFB", &hLdrFFmpegFB);

            if (RT_SUCCESS(rrc))
            {
                Log2(("VBoxHeadless: looking up symbol VBoxRegisterFFmpegFB\n"));
                rrc = RTLdrGetSymbol(hLdrFFmpegFB, "VBoxRegisterFFmpegFB",
                                     reinterpret_cast<void **>(&pfnRegisterFFmpegFB));
                if (RT_FAILURE(rrc))
                    LogError("Failed to load the video capture extension, possibly due to a damaged file\n", rrc);
            }
            else
                LogError("Failed to load the video capture extension\n", rrc);
            if (RT_SUCCESS(rrc))
            {
                Log2(("VBoxHeadless: calling pfnRegisterFFmpegFB\n"));
                rcc = pfnRegisterFFmpegFB(ulFrameWidth, ulFrameHeight, ulBitRate,
                                         pszMPEGFile, &pFramebuffer);
                if (rcc != S_OK)
                    LogError("Failed to initialise video capturing - make sure that the file format\n"
                             "you wish to use is supported on your system\n", rcc);
            }
            if (RT_SUCCESS(rrc) && (rcc == S_OK))
            {
                Log2(("VBoxHeadless: Registering framebuffer\n"));
                pFramebuffer->AddRef();
                display->SetFramebuffer(VBOX_VIDEO_PRIMARY_SCREEN, pFramebuffer);
            }
            if (!RT_SUCCESS(rrc) || (rcc != S_OK))
                rc = E_FAIL;
        }
        if (rc != S_OK)
        {
            break;
        }
#endif /* defined(VBOX_FFMPEG) */
#ifdef VBOX_WITH_VNC
        if (fVNCEnable)
        {
            Bstr name;
            machine->COMGETTER(Name)(name.asOutParam());
            g_pFramebufferVNC = new VNCFB(console, uVNCPort, pszVNCPassword);
            rc = g_pFramebufferVNC->init(name ? Utf8Str(name).raw() : "");
            if (rc != S_OK)
            {
                LogError("Failed to load the vnc server extension, possibly due to a damaged file\n", rc);
                delete g_pFramebufferVNC;
                break;
            }

            Log2(("VBoxHeadless: Registering VNC framebuffer\n"));
            g_pFramebufferVNC->AddRef();
            display->SetFramebuffer(VBOX_VIDEO_PRIMARY_SCREEN, g_pFramebufferVNC);
        }
        if (rc != S_OK)
            break;
#endif
        ULONG cMonitors = 1;
        machine->COMGETTER(MonitorCount)(&cMonitors);

#ifdef VBOX_WITH_VRDP
        unsigned uScreenId;
        for (uScreenId = 0; uScreenId < cMonitors; uScreenId++)
        {
# ifdef VBOX_FFMPEG
            if (fFFMPEG && uScreenId == 0)
            {
                /* Already registered. */
                continue;
            }
# endif
# ifdef VBOX_WITH_VNC
            if (fVNCEnable && uScreenId == 0)
            {
                /* Already registered. */
                continue;
            }
# endif
            VRDPFramebuffer *pVRDPFramebuffer = new VRDPFramebuffer();
            if (!pVRDPFramebuffer)
            {
                RTPrintf("Error: could not create framebuffer object %d\n", uScreenId);
                break;
            }
            pVRDPFramebuffer->AddRef();
            display->SetFramebuffer(uScreenId, pVRDPFramebuffer);
        }
        if (uScreenId < cMonitors)
        {
            break;
        }
#endif

        /* get the machine debugger (isn't necessarily available) */
        ComPtr <IMachineDebugger> machineDebugger;
        console->COMGETTER(Debugger)(machineDebugger.asOutParam());
        if (machineDebugger)
        {
            Log(("Machine debugger available!\n"));
        }

        if (fRawR0 != ~0U)
        {
            if (!machineDebugger)
            {
                RTPrintf("Error: No debugger object; -%srawr0 cannot be executed!\n", fRawR0 ? "" : "no");
                break;
            }
            machineDebugger->COMSETTER(RecompileSupervisor)(!fRawR0);
        }
        if (fRawR3 != ~0U)
        {
            if (!machineDebugger)
            {
                RTPrintf("Error: No debugger object; -%srawr3 cannot be executed!\n", fRawR0 ? "" : "no");
                break;
            }
            machineDebugger->COMSETTER(RecompileUser)(!fRawR3);
        }
        if (fPATM != ~0U)
        {
            if (!machineDebugger)
            {
                RTPrintf("Error: No debugger object; -%spatm cannot be executed!\n", fRawR0 ? "" : "no");
                break;
            }
            machineDebugger->COMSETTER(PATMEnabled)(fPATM);
        }
        if (fCSAM != ~0U)
        {
            if (!machineDebugger)
            {
                RTPrintf("Error: No debugger object; -%scsam cannot be executed!\n", fRawR0 ? "" : "no");
                break;
            }
            machineDebugger->COMSETTER(CSAMEnabled)(fCSAM);
        }

        /* initialize global references */
        gSession = session;
        gConsole = console;
        gEventQ = com::EventQueue::getMainEventQueue();

        /* Console events registration. */
        {
            ComPtr<IEventSource> es;
            CHECK_ERROR(console, COMGETTER(EventSource)(es.asOutParam()));
            consoleListener = new ConsoleEventListener();
            consoleListener->AddRef();
            com::SafeArray <VBoxEventType_T> eventTypes;
            eventTypes.push_back(VBoxEventType_OnMouseCapabilityChange);
            eventTypes.push_back(VBoxEventType_OnStateChange);
            eventTypes.push_back(VBoxEventType_OnRemoteDisplayInfoChange);
            eventTypes.push_back(VBoxEventType_OnCanShowWindow);
            eventTypes.push_back(VBoxEventType_OnShowWindow);
            CHECK_ERROR(es, RegisterListener(consoleListener, ComSafeArrayAsInParam(eventTypes), true));
        }

#ifdef VBOX_WITH_VRDP
        /* default is to enable the RDP server (backward compatibility) */
        BOOL fVRDPEnable = true;
        BOOL fVRDPEnabled;
        ComPtr <IVRDPServer> vrdpServer;
        CHECK_ERROR_BREAK(machine, COMGETTER(VRDPServer)(vrdpServer.asOutParam()));
        CHECK_ERROR_BREAK(vrdpServer, COMGETTER(Enabled)(&fVRDPEnabled));

        if (vrdpEnabled != NULL)
        {
            /* -vrdp on|off|config */
            if (!strcmp(vrdpEnabled, "off") || !strcmp(vrdpEnabled, "disable"))
                fVRDPEnable = false;
            else if (!strcmp(vrdpEnabled, "config"))
            {
                if (!fVRDPEnabled)
                    fVRDPEnable = false;
            }
            else if (strcmp(vrdpEnabled, "on") && strcmp(vrdpEnabled, "enable"))
            {
                RTPrintf("-vrdp requires an argument (on|off|config)\n");
                break;
            }
        }

        if (fVRDPEnable)
        {
            Log(("VBoxHeadless: Enabling VRDP server...\n"));

            /* set VRDP port if requested by the user */
            if (vrdpPort != NULL)
            {
                Bstr bstr = vrdpPort;
                CHECK_ERROR_BREAK(vrdpServer, COMSETTER(Ports)(bstr));
            }
            /* set VRDP address if requested by the user */
            if (vrdpAddress != NULL)
            {
                CHECK_ERROR_BREAK(vrdpServer, COMSETTER(NetAddress)(Bstr(vrdpAddress)));
            }
            /* enable VRDP server (only if currently disabled) */
            if (!fVRDPEnabled)
            {
                CHECK_ERROR_BREAK(vrdpServer, COMSETTER(Enabled)(TRUE));
            }
        }
        else
        {
            /* disable VRDP server (only if currently enabled */
            if (fVRDPEnabled)
            {
                CHECK_ERROR_BREAK(vrdpServer, COMSETTER(Enabled)(FALSE));
            }
        }
#endif
        Log(("VBoxHeadless: Powering up the machine...\n"));

        ComPtr <IProgress> progress;
        CHECK_ERROR_BREAK(console, PowerUp(progress.asOutParam()));

        /* wait for result because there can be errors */
        if (SUCCEEDED(progress->WaitForCompletion(-1)))
        {
            LONG progressRc;
            progress->COMGETTER(ResultCode)(&progressRc);
            rc = progressRc;
            if (FAILED(progressRc))
            {
                com::ProgressErrorInfo info(progress);
                if (info.isBasicAvailable())
                {
                    RTPrintf("Error: failed to start machine. Error message: %lS\n", info.getText().raw());
                }
                else
                {
                    RTPrintf("Error: failed to start machine. No error message available!\n");
                }
            }
        }

        /* VirtualBox events registration. */
        {
            ComPtr<IEventSource> es;
            CHECK_ERROR(virtualBox, COMGETTER(EventSource)(es.asOutParam()));
            vboxListener = new VirtualBoxEventListener();
            vboxListener->AddRef();
            com::SafeArray <VBoxEventType_T> eventTypes;
            eventTypes.push_back(VBoxEventType_OnGuestPropertyChange);
            CHECK_ERROR(es, RegisterListener(vboxListener, ComSafeArrayAsInParam(eventTypes), true));
        }

#ifdef VBOX_WITH_SAVESTATE_ON_SIGNAL
        signal(SIGINT, SaveState);
        signal(SIGTERM, SaveState);
#endif

        Log(("VBoxHeadless: Waiting for PowerDown...\n"));

        Event *e;

        while (gEventQ->waitForEvent(&e) && e)
          gEventQ->handleEvent(e);

        Log(("VBoxHeadless: event loop has terminated...\n"));

#ifdef VBOX_FFMPEG
        if (pFramebuffer)
        {
            pFramebuffer->Release();
            Log(("Released framebuffer\n"));
            pFramebuffer = NULL;
        }
#endif /* defined(VBOX_FFMPEG) */

        /* we don't have to disable VRDP here because we don't save the settings of the VM */
    }
    while (0);

    /* VirtualBox callback unregistration. */
    if (vboxListener)
    {
        ComPtr<IEventSource> es;
        CHECK_ERROR(virtualBox, COMGETTER(EventSource)(es.asOutParam()));
        CHECK_ERROR(es, UnregisterListener(vboxListener));
        vboxListener->Release();
    }

    /* Console callback unregistration. */
    if (consoleListener)
    {
        ComPtr<IEventSource> es;
        CHECK_ERROR(gConsole, COMGETTER(EventSource)(es.asOutParam()));
        CHECK_ERROR(es, UnregisterListener(consoleListener));
        consoleListener->Release();
    }

    /* No more access to the 'console' object, which will be uninitialized by the next session->Close call. */
    gConsole = NULL;

    if (fSessionOpened)
    {
        /*
         * Close the session. This will also uninitialize the console and
         * unregister the callback we've registered before.
         */
        Log(("VBoxHeadless: Closing the session...\n"));
        session->Close();
    }

    /* Must be before com::Shutdown */
    session.setNull();
    virtualBox.setNull();

    com::Shutdown();

    LogFlow(("VBoxHeadless FINISHED.\n"));

    return FAILED(rc) ? 1 : 0;
}


#ifndef VBOX_WITH_HARDENING
/**
 * Main entry point.
 */
int main(int argc, char **argv, char **envp)
{
    // initialize VBox Runtime
    int rc = RTR3InitAndSUPLib();
    if (RT_FAILURE(rc))
    {
        RTPrintf("VBoxHeadless: Runtime Error:\n"
                 " %Rrc -- %Rrf\n", rc, rc);
        switch (rc)
        {
            case VERR_VM_DRIVER_NOT_INSTALLED:
                RTPrintf("Cannot access the kernel driver. Make sure the kernel module has been \n"
                        "loaded successfully. Aborting ...\n");
                break;
            default:
                break;
        }
        return 1;
    }

    return TrustedMain(argc, argv, envp);
}
#endif /* !VBOX_WITH_HARDENING */
