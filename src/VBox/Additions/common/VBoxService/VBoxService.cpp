/* $Id$ */
/** @file
 * VBoxService - Guest Additions Service Skeleton.
 */

/*
 * Copyright (C) 2007-2009 Sun Microsystems, Inc.
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
/** @todo LOG_GROUP*/
#ifndef _MSC_VER
# include <unistd.h>
#endif
#include <errno.h>

#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/initterm.h>
#include <iprt/path.h>
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
/** Shutdown the main thread. (later, for signals.) */
bool volatile g_fShutdown;

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
#ifdef VBOXSERVICE_EXEC
    { &g_Exec,      NIL_RTTHREAD, false, false, false, true },
#endif
#ifdef VBOXSERVICE_CPUHOTPLUG /* Disabled by default. Use --enable-cpuhotplug to enable */
    { &g_CpuHotplug, NIL_RTTHREAD, false, false, false, false },
#endif
};


/**
 * Displays the program usage message.
 *
 * @returns 1.
 */
static int VBoxServiceUsage(void)
{
    RTPrintf("usage: %s [-f|--foreground] [-v|--verbose] [-i|--interval <seconds>]\n"
             "           [--disable-<service>] [--enable-<service>] [-h|-?|--help]\n", g_pszProgName);
#ifdef RT_OS_WINDOWS
    RTPrintf("           [-r|--register] [-u|--unregister]\n");
#endif
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        RTPrintf("           %s\n", g_aServices[j].pDesc->pszUsage);
    RTPrintf("\n"
             "Options:\n"
             "    -i | --interval          The default interval.\n"
             "    -f | --foreground        Don't daemonzie the program. For debugging.\n"
             "    -v | --verbose           Increment the verbosity level. For debugging.\n"
             "    -h | -? | --help         Show this message and exit with status 1.\n"
             );
#ifdef RT_OS_WINDOWS
    RTPrintf("    -r | --register          Installs the service.\n"
             "    -u | --unregister        Uninstall service.\n");
#endif

    RTPrintf("\n"
             "Service specific options:\n");
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
    {
        RTPrintf("    --enable-%-10s Enables the %s service. (default)\n", g_aServices[j].pDesc->pszName, g_aServices[j].pDesc->pszName);
        RTPrintf("    --disable-%-9s Disables the %s service.\n", g_aServices[j].pDesc->pszName, g_aServices[j].pDesc->pszName);
        RTPrintf("%s", g_aServices[j].pDesc->pszOptions);
    }
    RTPrintf("\n"
             " Copyright (C) 2009 Sun Microsystems, Inc.\n");

    return 1;
}


/**
 * Displays a syntax error message.
 *
 * @returns 1
 * @param   pszFormat   The message text.
 * @param   ...         Format arguments.
 */
int VBoxServiceSyntax(const char *pszFormat, ...)
{
    RTStrmPrintf(g_pStdErr, "%s: syntax error: ", g_pszProgName);

    va_list va;
    va_start(va, pszFormat);
    RTStrmPrintfV(g_pStdErr, pszFormat, va);
    va_end(va);

    return 1;
}


/**
 * Displays an error message.
 *
 * @returns 1
 * @param   pszFormat   The message text.
 * @param   ...         Format arguments.
 */
int VBoxServiceError(const char *pszFormat, ...)
{
    RTStrmPrintf(g_pStdErr, "%s: error: ", g_pszProgName);

    va_list va;
    va_start(va, pszFormat);
    RTStrmPrintfV(g_pStdErr, pszFormat, va);
    va_end(va);

    va_start(va, pszFormat);
    LogRel(("%s: Error: %N", g_pszProgName, pszFormat, &va));
    va_end(va);

    return 1;
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
    int rc = g_aServices[i].pDesc->pfnWorker(&g_aServices[i].fShutdown);
    ASMAtomicXchgBool(&g_aServices[i].fShutdown, true);
    RTThreadUserSignal(ThreadSelf);
    return rc;
}


unsigned VBoxServiceGetStartedServices(void)
{
    unsigned iMain = ~0U;
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        if (g_aServices[j].fEnabled)
        {
            iMain = j;
            break;
        }

   return iMain; /* Return the index of the main service (must always come last!). */
}

/**
 * Starts the service.
 *
 * @returns VBox status code, errors are fully bitched.
 *
 * @param   iMain           The index of the service that belongs to the main
 *                          thread. Pass ~0U if none does.
 */
int VBoxServiceStartServices(unsigned iMain)
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
                VBoxServiceError("Service '%s' failed to initialize: %Rrc\n",
                                 g_aServices[j].pDesc->pszName, rc);
                return rc;
            }
        }

    /*
     * Start the service(s).
     */
    VBoxServiceVerbose(2, "Starting services ...\n");
    rc = VINF_SUCCESS;
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
    {
        if (    !g_aServices[j].fEnabled
            ||  j == iMain)
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
    if (RT_SUCCESS(rc))
    {
        /* The final service runs in the main thread. */
        VBoxServiceVerbose(1, "Starting '%s' in the main thread\n", g_aServices[iMain].pDesc->pszName);
        rc = g_aServices[iMain].pDesc->pfnWorker(&g_fShutdown);
        if (rc != VINF_SUCCESS) /* Only complain if service returned an error. Otherwise the service is a one-timer. */
        {
            VBoxServiceError("Service '%s' stopped unexpected; rc=%Rrc\n", g_aServices[iMain].pDesc->pszName, rc);
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

    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        ASMAtomicXchgBool(&g_aServices[j].fShutdown, true);
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        if (g_aServices[j].fStarted)
            g_aServices[j].pDesc->pfnStop();
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
        if (g_aServices[j].fEnabled)
        {
            if (g_aServices[j].Thread != NIL_RTTHREAD)
            {
                int rc;
                VBoxServiceVerbose(2, "Waiting for service '%s' to stop ...\n", g_aServices[j].pDesc->pszName);
                for (int i=0; i<30; i++) /* Wait 30 seconds in total */
                {
                    rc = RTThreadWait(g_aServices[j].Thread, 1000 /* Wait 1 second */, NULL);
                    if (RT_SUCCESS(rc))
                        break;
#ifdef RT_OS_WINDOWS
                    /* Notify SCM that it takes a bit longer ... */
                    VBoxServiceWinSetStatus(SERVICE_STOP_PENDING, i);
#endif
                }
                if (RT_FAILURE(rc))
                    VBoxServiceError("Service '%s' failed to stop. (%Rrc)\n", g_aServices[j].pDesc->pszName, rc);
            }
            VBoxServiceVerbose(3, "Terminating service '%s' (%d) ...\n", g_aServices[j].pDesc->pszName, j);
            g_aServices[j].pDesc->pfnTerm();
        }

    VBoxServiceVerbose(2, "Stopping services returned: rc=%Rrc\n", rc);
    return rc;
}


int main(int argc, char **argv)
{
    int rc = VINF_SUCCESS;
    /*
     * Init globals and such.
     */
    RTR3Init();

    /*
     * Connect to the kernel part before daemonizing so we can fail
     * and complain if there is some kind of problem. We need to initialize
     * the guest lib *before* we do the pre-init just in case one of services
     * needs do to some initial stuff with it.
     */
    VBoxServiceVerbose(2, "Calling VbgR3Init()\n");
    rc = VbglR3Init();
    if (RT_FAILURE(rc))
        return VBoxServiceError("VbglR3Init failed with rc=%Rrc.\n", rc);

    /* Do pre-init of services. */
    g_pszProgName = RTPathFilename(argv[0]);
    for (unsigned j = 0; j < RT_ELEMENTS(g_aServices); j++)
    {
        rc = g_aServices[j].pDesc->pfnPreInit();
        if (RT_FAILURE(rc))
            return VBoxServiceError("Service '%s' failed pre-init: %Rrc\n", g_aServices[j].pDesc->pszName);
    }

#ifdef RT_OS_WINDOWS
    /* Make sure only one instance of VBoxService runs at a time. Create a global mutex for that.
       Do not use a global namespace ("Global\\") for mutex name here, will blow up NT4 compatibility! */
    HANDLE hMutexAppRunning = CreateMutex (NULL, FALSE, VBOXSERVICE_NAME);
    if (   hMutexAppRunning != NULL
        && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        VBoxServiceError("%s is already running! Terminating.", g_pszProgName);

        /* Close the mutex for this application instance. */
        CloseHandle(hMutexAppRunning);
        hMutexAppRunning = NULL;
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
    unsigned iMain = VBoxServiceGetStartedServices();
    if (iMain == ~0U)
        return VBoxServiceSyntax("At least one service must be enabled.\n");

    VBoxServiceVerbose(0, "%s r%s started. Verbose level = %d\n",
        RTBldCfgVersion(), RTBldCfgRevisionStr(), g_cVerbosity);

    /*
     * Daemonize if requested.
     */
    if (fDaemonize && !fDaemonized)
    {
#ifdef RT_OS_WINDOWS
        /** @todo Should do something like VBoxSVC here, OR automatically re-register
         *        the service and start it. Involving VbglR3Daemonize isn't an option
         *        here.
         *
         *        Also, the idea here, IIRC, was to map the sub service to windows
         *        services. The todo below is for mimicking windows services on
         *        non-windows systems. Not sure if this is doable or not, but in anycase
         *        this code can be moved into -win.
         *
         *        You should return when StartServiceCtrlDispatcher, btw., not
         *        continue.
         */
        VBoxServiceVerbose(2, "Starting service dispatcher ...\n");
        if (!StartServiceCtrlDispatcher(&g_aServiceTable[0]))
            return VBoxServiceError("StartServiceCtrlDispatcher: %u. Please start %s with option -f (foreground)!",
                                    GetLastError(), g_pszProgName);
        /* Service now lives in the control dispatcher registered above. */
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
    {
        /* Run the app just like a console one if not daemonized. */
#endif
        /** @todo Make the main thread responsive to signal so it can shutdown/restart the threads on non-SIGKILL signals. */

        /*
         * Start the service, enter the main threads run loop and stop them again when it returns.
         */
        rc = VBoxServiceStartServices(iMain);
        VBoxServiceStopServices();
#ifdef RT_OS_WINDOWS
    }
#endif

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
    return RT_SUCCESS(rc) ? 0 : 1;
}

