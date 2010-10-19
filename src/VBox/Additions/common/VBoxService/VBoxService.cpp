/* $Id$ */
/** @file
 * VBoxService - Guest Additions Service Skeleton.
 */

/*
 * Copyright (C) 2007-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */



/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
/** @todo LOG_GROUP*/
#ifndef _MSC_VER
# include <unistd.h>
#endif
#include <errno.h>
#ifndef RT_OS_WINDOWS
# include <signal.h>
#endif
#ifdef RT_OS_FREEBSD
# include <pthread.h>
#endif

#include "product-generated.h"
#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/initterm.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/thread.h>

#include <VBox/VBoxGuestLib.h>
#include <VBox/log.h>

#include "VBoxServiceInternal.h"


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** The program name (derived from argv[0]). */
char *g_pszProgName =  (char *)"";
/** The current verbosity level. */
int g_cVerbosity = 0;
/** The default service interval (the -i | --interval) option). */
uint32_t g_DefaultInterval = 0;
#ifdef RT_OS_WINDOWS
/** Signal shutdown to the Windows service thread. */
static bool volatile g_fWindowsServiceShutdown;
/** Event the Windows service thread waits for shutdown. */
static RTSEMEVENT g_hEvtWindowsService;
#endif

/**
 * The details of the services that has been compiled in.
 */
static struct
{
    /** Pointer to the service descriptor. */
    PCVBOXSERVICE   pDesc;
    /** The worker thread. NIL_RTTHREAD if it's the main thread. */
    RTTHREAD        Thread;
    /** Shutdown indicator. */
    bool volatile   fShutdown;
    /** Indicator set by the service thread exiting. */
    bool volatile   fStopped;
    /** Whether the service was started or not. */
    bool            fStarted;
    /** Whether the service is enabled or not. */
    bool            fEnabled;
} g_aServices[] =
{
#ifdef VBOXSERVICE_CONTROL
    { &g_Control,   NIL_RTTHREAD, false, false, false, true },
#endif
#ifdef VBOXSERVICE_TIMESYNC
    { &g_TimeSync,  NIL_RTTHREAD, false, false, false, true },
#endif
#ifdef VBOXSERVICE_CLIPBOARD
    { &g_Clipboard, NIL_RTTHREAD, false, false, false, true },
#endif
#ifdef VBOXSERVICE_VMINFO
    { &g_VMInfo,    NIL_RTTHREAD, false, false, false, true },
#endif
#ifdef VBOXSERVICE_CPUHOTPLUG
    { &g_CpuHotPlug, NIL_RTTHREAD, false, false, false, true },
#endif
#ifdef VBOXSERVICE_MANAGEMENT
# ifdef VBOX_WITH_MEMBALLOON
    { &g_MemBalloon, NIL_RTTHREAD, false, false, false, true },
# endif
    { &g_VMStatistics, NIL_RTTHREAD, false, false, false, true },
#endif
#if defined(VBOX_WITH_PAGE_SHARING) && defined(RT_OS_WINDOWS)
    { &g_PageSharing, NIL_RTTHREAD, false, false, false, true },
#endif
#ifdef VBOX_WITH_SHARED_FOLDERS
    { &g_AutoMount, NIL_RTTHREAD, false, false, false, true },
#endif
};


/**
 * Displays the program usage message.
 *
 * @returns 1.
 */
static int VBoxServiceUsage(void)
{
    RTPrintf("Usage:\n"
             " %-12s [-f|--foreground] [-v|--verbose] [-i|--interval <seconds>]\n"
             "              [--disable-<service>] [--enable-<service>] [-h|-?|--help]\n", g_pszProgName);
#ifdef RT_OS_WINDOWS
    RTPrintf("              [-r|--register] [-u|--unregister]\n");
#endif
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        if (g_aServices[j].pDesc->pszUsage)
            RTPrintf("%s\n", g_aServices[j].pDesc->pszUsage);
    RTPrintf("\n"
             "Options:\n"
             "    -i | --interval         The default interval.\n"
             "    -f | --foreground       Don't daemonzie the program. For debugging.\n"
             "    -v | --verbose          Increment the verbosity level. For debugging.\n"
             "    -h | -? | --help        Show this message and exit with status 1.\n"
             );
#ifdef RT_OS_WINDOWS
    RTPrintf("    -r | --register         Installs the service.\n"
             "    -u | --unregister       Uninstall service.\n");
#endif

    RTPrintf("\n"
             "Service-specific options:\n");
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
    {
        RTPrintf("    --enable-%-14s Enables the %s service. (default)\n", g_aServices[j].pDesc->pszName, g_aServices[j].pDesc->pszName);
        RTPrintf("    --disable-%-13s Disables the %s service.\n", g_aServices[j].pDesc->pszName, g_aServices[j].pDesc->pszName);
        if (g_aServices[j].pDesc->pszOptions)
            RTPrintf("%s", g_aServices[j].pDesc->pszOptions);
    }
    RTPrintf("\n"
             " Copyright (C) 2009-" VBOX_C_YEAR " " VBOX_VENDOR "\n");

    return 1;
}


/**
 * Displays a syntax error message.
 *
 * @returns RTEXITCODE_SYNTAX.
 * @param   pszFormat   The message text.
 * @param   ...         Format arguments.
 */
RTEXITCODE VBoxServiceSyntax(const char *pszFormat, ...)
{
    RTStrmPrintf(g_pStdErr, "%s: syntax error: ", g_pszProgName);

    va_list va;
    va_start(va, pszFormat);
    RTStrmPrintfV(g_pStdErr, pszFormat, va);
    va_end(va);

    return RTEXITCODE_SYNTAX;
}


/**
 * Displays an error message.
 *
 * @returns RTEXITCODE_FAILURE.
 * @param   pszFormat   The message text.
 * @param   ...         Format arguments.
 */
RTEXITCODE VBoxServiceError(const char *pszFormat, ...)
{
    RTStrmPrintf(g_pStdErr, "%s: error: ", g_pszProgName);

    va_list va;
    va_start(va, pszFormat);
    RTStrmPrintfV(g_pStdErr, pszFormat, va);
    va_end(va);

    va_start(va, pszFormat);
    LogRel(("%s: Error: %N", g_pszProgName, pszFormat, &va));
    va_end(va);

    return RTEXITCODE_FAILURE;
}


/**
 * Displays a verbose message.
 *
 * @returns 1
 * @param   pszFormat   The message text.
 * @param   ...         Format arguments.
 */
void VBoxServiceVerbose(int iLevel, const char *pszFormat, ...)
{
    if (iLevel <= g_cVerbosity)
    {
        RTStrmPrintf(g_pStdOut, "%s: ", g_pszProgName);
        va_list va;
        va_start(va, pszFormat);
        RTStrmPrintfV(g_pStdOut, pszFormat, va);
        va_end(va);

        va_start(va, pszFormat);
        LogRel(("%s: %N", g_pszProgName, pszFormat, &va));
        va_end(va);
    }
}


/**
 * Gets a 32-bit value argument.
 *
 * @returns 0 on success, non-zero exit code on error.
 * @param   argc    The argument count.
 * @param   argv    The argument vector
 * @param   psz     Where in *pi to start looking for the value argument.
 * @param   pi      Where to find and perhaps update the argument index.
 * @param   pu32    Where to store the 32-bit value.
 * @param   u32Min  The minimum value.
 * @param   u32Max  The maximum value.
 */
int VBoxServiceArgUInt32(int argc, char **argv, const char *psz, int *pi, uint32_t *pu32, uint32_t u32Min, uint32_t u32Max)
{
    if (*psz == ':' || *psz == '=')
        psz++;
    if (!*psz)
    {
        if (*pi + 1 >= argc)
            return VBoxServiceSyntax("Missing value for the '%s' argument\n", argv[*pi]);
        psz = argv[++*pi];
    }

    char *pszNext;
    int rc = RTStrToUInt32Ex(psz, &pszNext, 0, pu32);
    if (RT_FAILURE(rc) || *pszNext)
        return VBoxServiceSyntax("Failed to convert interval '%s' to a number.\n", psz);
    if (*pu32 < u32Min || *pu32 > u32Max)
        return VBoxServiceSyntax("The timesync interval of %RU32 secconds is out of range [%RU32..%RU32].\n",
                                 *pu32, u32Min, u32Max);
    return 0;
}


/**
 * The service thread.
 *
 * @returns Whatever the worker function returns.
 * @param   ThreadSelf      My thread handle.
 * @param   pvUser          The service index.
 */
static DECLCALLBACK(int) VBoxServiceThread(RTTHREAD ThreadSelf, void *pvUser)
{
    const unsigned i = (uintptr_t)pvUser;

#ifndef RT_OS_WINDOWS
    /*
     * Block all signals for this thread. Only the main thread will handle signals.
     */
    sigset_t signalMask;
    sigfillset(&signalMask);
    pthread_sigmask(SIG_BLOCK, &signalMask, NULL);
#endif

    int rc = g_aServices[i].pDesc->pfnWorker(&g_aServices[i].fShutdown);
    ASMAtomicXchgBool(&g_aServices[i].fShutdown, true);
    RTThreadUserSignal(ThreadSelf);
    return rc;
}


/**
 * Check if at least one service should be started.
 */
static bool VBoxServiceCheckStartedServices(void)
{
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        if (g_aServices[j].fEnabled)
            return true;

   return false;
}


/**
 * Starts the service.
 *
 * @returns VBox status code, errors are fully bitched.
 */
int VBoxServiceStartServices(void)
{
    int rc;

    /*
     * Initialize the services.
     */
    VBoxServiceVerbose(2, "Initializing services ...\n");
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        if (g_aServices[j].fEnabled)
        {
            rc = g_aServices[j].pDesc->pfnInit();
            if (RT_FAILURE(rc))
            {
                if (rc != VERR_SERVICE_DISABLED)
                {
                    VBoxServiceError("Service '%s' failed to initialize: %Rrc\n",
                                     g_aServices[j].pDesc->pszName, rc);
                    return rc;
                }
                g_aServices[j].fEnabled = false;
                VBoxServiceVerbose(0, "Service '%s' was disabled because of missing functionality\n",
                                   g_aServices[j].pDesc->pszName);

            }
        }

    /*
     * Start the service(s).
     */
    VBoxServiceVerbose(2, "Starting services ...\n");
    rc = VINF_SUCCESS;
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
    {
        if (!g_aServices[j].fEnabled)
            continue;

        VBoxServiceVerbose(2, "Starting service     '%s' ...\n", g_aServices[j].pDesc->pszName);
        rc = RTThreadCreate(&g_aServices[j].Thread, VBoxServiceThread, (void *)(uintptr_t)j, 0,
                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, g_aServices[j].pDesc->pszName);
        if (RT_FAILURE(rc))
        {
            VBoxServiceError("RTThreadCreate failed, rc=%Rrc\n", rc);
            break;
        }
        g_aServices[j].fStarted = true;

        /* wait for the thread to initialize */
        RTThreadUserWait(g_aServices[j].Thread, 60 * 1000);
        if (g_aServices[j].fShutdown)
        {
            VBoxServiceError("Service '%s' failed to start!\n", g_aServices[j].pDesc->pszName);
            rc = VERR_GENERAL_FAILURE;
        }
    }

    return rc;
}


/**
 * Stops and terminates the services.
 *
 * This should be called even when VBoxServiceStartServices fails so it can
 * clean up anything that we succeeded in starting.
 */
int VBoxServiceStopServices(void)
{
    int rc = VINF_SUCCESS;

    /*
     * Signal all the services.
     */
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        ASMAtomicWriteBool(&g_aServices[j].fShutdown, true);

    /*
     * Do the pfnStop callback on all running services.
     */
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        if (g_aServices[j].fStarted)
        {
            VBoxServiceVerbose(3, "Calling stop function for service '%s' ...\n", g_aServices[j].pDesc->pszName);
            g_aServices[j].pDesc->pfnStop();
        }

    /*
     * Wait for all the service threads to complete.
     */
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
    {
        if (!g_aServices[j].fEnabled) /* Only stop services which were started before. */
            continue;
        if (g_aServices[j].Thread != NIL_RTTHREAD)
        {
            VBoxServiceVerbose(2, "Waiting for service '%s' to stop ...\n", g_aServices[j].pDesc->pszName);
            for (int i = 0; i < 30; i++) /* Wait 30 seconds in total */
            {
                rc = RTThreadWait(g_aServices[j].Thread, 1000 /* Wait 1 second */, NULL);
                if (RT_SUCCESS(rc))
                    break;
#ifdef RT_OS_WINDOWS
                /* Notify SCM that it takes a bit longer ... */
                VBoxServiceWinSetStopPendingStatus(i + j*32);
#endif
            }
            if (RT_FAILURE(rc))
                VBoxServiceError("Service '%s' failed to stop. (%Rrc)\n", g_aServices[j].pDesc->pszName, rc);
        }
        VBoxServiceVerbose(3, "Terminating service '%s' (%d) ...\n", g_aServices[j].pDesc->pszName, j);
        g_aServices[j].pDesc->pfnTerm();
    }

#ifdef RT_OS_WINDOWS
    /*
     * Wake up and tell the main() thread that we're shutting down (it's
     * sleeping in VBoxServiceMainWait).
     */
    if (g_hEvtWindowsService != NIL_RTSEMEVENT)
    {
        VBoxServiceVerbose(3, "Stopping the main thread...\n");
        ASMAtomicWriteBool(&g_fWindowsServiceShutdown, true);
        rc = RTSemEventSignal(g_hEvtWindowsService);
        AssertRC(rc);
    }
#endif

    VBoxServiceVerbose(2, "Stopping services returned: rc=%Rrc\n", rc);
    return rc;
}


/**
 * Block the main thread until the service shuts down.
 */
void VBoxServiceMainWait(void)
{
    int rc;

    /* Report the host that we're up and running! */
    rc = VbglR3ReportAdditionsStatus(VBoxGuestStatusFacility_VBoxService,
                                     VBoxGuestStatusCurrent_Active,
                                     0); /* Flags; not used. */
    if (RT_FAILURE(rc))
        VBoxServiceError("Failed to report Guest Additions status to the host! rc=%Rrc\n", rc);

#ifdef RT_OS_WINDOWS
    /*
     * Wait for the semaphore to be signalled.
     */
    VBoxServiceVerbose(1, "Waiting in main thread\n");
    rc = RTSemEventCreate(&g_hEvtWindowsService);
    AssertRC(rc);
    while (!ASMAtomicReadBool(&g_fWindowsServiceShutdown))
    {
        rc = RTSemEventWait(g_hEvtWindowsService, RT_INDEFINITE_WAIT);
        AssertRC(rc);
    }
    RTSemEventDestroy(g_hEvtWindowsService);
    g_hEvtWindowsService = NIL_RTSEMEVENT;

#else
    /*
     * Wait explicitly for a HUP, INT, QUIT, ABRT or TERM signal, blocking
     * all important signals.
     *
     * The annoying EINTR/ERESTART loop is for the benefit of Solaris where
     * sigwait returns when we receive a SIGCHLD.  Kind of makes sense since
     */
    sigset_t signalMask;
    sigemptyset(&signalMask);
    sigaddset(&signalMask, SIGHUP);
    sigaddset(&signalMask, SIGINT);
    sigaddset(&signalMask, SIGQUIT);
    sigaddset(&signalMask, SIGABRT);
    sigaddset(&signalMask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signalMask, NULL);

    int iSignal;
    do
    {
        iSignal = -1;
        rc = sigwait(&signalMask, &iSignal);
    }
    while (   rc == EINTR
# ifdef ERESTART
           || rc == ERESTART
# endif
          );

    VBoxServiceVerbose(3, "VBoxServiceMainWait: Received signal %d (rc=%d)\n", iSignal, rc);
#endif /* !RT_OS_WINDOWS */
}


int main(int argc, char **argv)
{
    /*
     * Init globals and such.
     */
    RTR3Init();

    /*
     * Connect to the kernel part before daemonizing so we can fail and
     * complain if there is some kind of problem.  We need to initialize the
     * guest lib *before* we do the pre-init just in case one of services needs
     * do to some initial stuff with it.
     */
    VBoxServiceVerbose(2, "Calling VbgR3Init()\n");
    int rc = VbglR3Init();
    if (RT_FAILURE(rc))
        return VBoxServiceError("VbglR3Init failed with rc=%Rrc.\n", rc);

#ifdef RT_OS_WINDOWS
    /*
     * Check if we're the specially spawned VBoxService.exe process that
     * handles page fusion.  This saves an extra executable.
     */
    if (    argc == 2
        &&  !strcmp(argv[1], "--pagefusionfork"))
        return VBoxServicePageSharingInitFork();
#endif

#ifdef VBOXSERVICE_TOOLBOX
    /*
     * Run toolbox code before all other stuff, especially before checking the global
     * mutex because VBoxService might spawn itself to execute some commands.
     */
    rc = VBoxServiceToolboxMain(argc, argv);
    if (rc != VERR_NOT_FOUND) /* Internal tool found? Then bail out. */
        return rc;
#endif

    /*
     * Do pre-init of services.
     */
    g_pszProgName = RTPathFilename(argv[0]);
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
    {
        rc = g_aServices[j].pDesc->pfnPreInit();
        if (RT_FAILURE(rc))
            return VBoxServiceError("Service '%s' failed pre-init: %Rrc\n", g_aServices[j].pDesc->pszName, rc);
    }
#ifdef RT_OS_WINDOWS
    /*
     * Make sure only one instance of VBoxService runs at a time.  Create a
     * global mutex for that.  Do not use a global namespace ("Global\\") for
     * mutex name here, will blow up NT4 compatibility!
     */
    /** @todo r=bird: Use Global\\ prefix or this serves no purpose on terminal servers. */
    HANDLE hMutexAppRunning = CreateMutex(NULL, FALSE, VBOXSERVICE_NAME);
    if (   hMutexAppRunning != NULL
        && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        VBoxServiceError("%s is already running! Terminating.", g_pszProgName);

        /* Close the mutex for this application instance. */
        CloseHandle(hMutexAppRunning);
        hMutexAppRunning = NULL;

        /** @todo r=bird: How does this cause us to terminate?  Btw. Why do
         *        we do this before parsing parameters?  'VBoxService --help'
         *        and 'VBoxService --version' won't work now when the service
         *        is running... */
    }
#endif

    /*
     * Parse the arguments.
     */
    bool fDaemonize = true;
    bool fDaemonized = false;
    for (int i = 1; i < argc; i++)
    {
        const char *psz = argv[i];
        if (*psz != '-')
            return VBoxServiceSyntax("Unknown argument '%s'\n", psz);
        psz++;

        /* translate long argument to short */
        if (*psz == '-')
        {
            psz++;
            size_t cch = strlen(psz);
#define MATCHES(strconst)       (   cch == sizeof(strconst) - 1 \
                                 && !memcmp(psz, strconst, sizeof(strconst) - 1) )
            if (MATCHES("foreground"))
                psz = "f";
            else if (MATCHES("verbose"))
                psz = "v";
            else if (MATCHES("help"))
                psz = "h";
            else if (MATCHES("interval"))
                psz = "i";
#ifdef RT_OS_WINDOWS
            else if (MATCHES("register"))
                psz = "r";
            else if (MATCHES("unregister"))
                psz = "u";
#endif
            else if (MATCHES("daemonized"))
            {
                fDaemonized = true;
                continue;
            }
            else
            {
                bool fFound = false;

                if (cch > sizeof("enable-") && !memcmp(psz, "enable-", sizeof("enable-") - 1))
                    for (unsigned j = 0; !fFound && j < RT_ELEMENTS(g_aServices); j++)
                        if ((fFound = !RTStrICmp(psz + sizeof("enable-") - 1, g_aServices[j].pDesc->pszName)))
                            g_aServices[j].fEnabled = true;

                if (cch > sizeof("disable-") && !memcmp(psz, "disable-", sizeof("disable-") - 1))
                    for (unsigned j = 0; !fFound && j < RT_ELEMENTS(g_aServices); j++)
                        if ((fFound = !RTStrICmp(psz + sizeof("disable-") - 1, g_aServices[j].pDesc->pszName)))
                            g_aServices[j].fEnabled = false;

                if (!fFound)
                    for (unsigned j = 0; !fFound && j < RT_ELEMENTS(g_aServices); j++)
                    {
                        rc = g_aServices[j].pDesc->pfnOption(NULL, argc, argv, &i);
                        fFound = rc == 0;
                        if (fFound)
                            break;
                        if (rc != -1)
                            return rc;
                    }
                if (!fFound)
                    return VBoxServiceSyntax("Unknown option '%s'\n", argv[i]);
                continue;
            }
#undef MATCHES
        }

        /* handle the string of short options. */
        do
        {
            switch (*psz)
            {
                case 'i':
                    rc = VBoxServiceArgUInt32(argc, argv, psz + 1, &i,
                                              &g_DefaultInterval, 1, (UINT32_MAX / 1000) - 1);
                    if (rc)
                        return rc;
                    psz = NULL;
                    break;

                case 'f':
                    fDaemonize = false;
                    break;

                case 'v':
                    g_cVerbosity++;
                    break;

                case 'h':
                case '?':
                    return VBoxServiceUsage();

#ifdef RT_OS_WINDOWS
                case 'r':
                    return VBoxServiceWinInstall();

                case 'u':
                    return VBoxServiceWinUninstall();
#endif

                default:
                {
                    bool fFound = false;
                    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
                    {
                        rc = g_aServices[j].pDesc->pfnOption(&psz, argc, argv, &i);
                        fFound = rc == 0;
                        if (fFound)
                            break;
                        if (rc != -1)
                            return rc;
                    }
                    if (!fFound)
                        return VBoxServiceSyntax("Unknown option '%c' (%s)\n", *psz, argv[i]);
                    break;
                }
            }
        } while (psz && *++psz);
    }
    /*
     * Check that at least one service is enabled.
     */
    if (!VBoxServiceCheckStartedServices())
        return VBoxServiceSyntax("At least one service must be enabled.\n");

    VBoxServiceVerbose(0, "%s r%s started. Verbose level = %d\n",
                       RTBldCfgVersion(), RTBldCfgRevisionStr(), g_cVerbosity);

    /*
     * Daemonize if requested.
     */
    RTEXITCODE rcExit;
    if (fDaemonize && !fDaemonized)
    {
#ifdef RT_OS_WINDOWS
        VBoxServiceVerbose(2, "Starting service dispatcher ...\n");
        rcExit = VBoxServiceWinEnterCtrlDispatcher();
#else
        VBoxServiceVerbose(1, "Daemonizing...\n");
        rc = VbglR3Daemonize(false /* fNoChDir */, false /* fNoClose */);
        if (RT_FAILURE(rc))
            return VBoxServiceError("Daemon failed: %Rrc\n", rc);
        /* in-child */
#endif
    }
#ifdef RT_OS_WINDOWS
    else
#endif
    {
        /*
         * Windows: We're running the service as a console application now. Start the
         *          services, enter the main thread's run loop and stop them again
         *          when it returns.
         *
         * POSIX:   This is used for both daemons and console runs. Start all services
         *          and return immediately.
         */
        rc = VBoxServiceStartServices();
        rcExit = RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
        if (RT_SUCCESS(rc))
            VBoxServiceMainWait();
        VBoxServiceStopServices();
    }

#ifdef RT_OS_WINDOWS
    /*
     * Release instance mutex if we got it.
     */
    if (hMutexAppRunning != NULL)
    {
        ::CloseHandle(hMutexAppRunning);
        hMutexAppRunning = NULL;
    }
#endif

    VBoxServiceVerbose(0, "Ended.\n");
    return rcExit;
}

