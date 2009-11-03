/* $Id$ */
/** @file
 * VBoxVMInfo - Virtual machine (guest) information for the host.
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



/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#ifdef RT_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <Ntsecapi.h>
#else
# define __STDC_LIMIT_MACROS
# include <arpa/inet.h>
# include <errno.h>
# include <netinet/in.h>
# include <sys/ioctl.h>
# include <sys/socket.h>
# include <net/if.h>
# include <unistd.h>
# include <utmp.h>
# ifdef RT_OS_SOLARIS
#  include <sys/sockio.h>
# endif
#endif

#include <iprt/mem.h>
#include <iprt/thread.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/system.h>
#include <iprt/time.h>
#include <iprt/assert.h>
#include <VBox/version.h>
#include <VBox/VBoxGuestLib.h>
#include "VBoxServiceInternal.h"
#include "VBoxServiceUtils.h"


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** The vminfo interval (millseconds). */
uint32_t g_VMInfoInterval = 0;
/** The semaphore we're blocking on. */
static RTSEMEVENTMULTI g_VMInfoEvent = NIL_RTSEMEVENTMULTI;
/** The guest property service client ID. */
static uint32_t g_VMInfoGuestPropSvcClientID = 0;
/** Number of logged in users in OS. */
static uint32_t g_VMInfoLoggedInUsers = UINT32_MAX;
#ifdef RT_OS_WINDOWS
/** Function prototypes for dynamic loading. */
fnWTSGetActiveConsoleSessionId g_pfnWTSGetActiveConsoleSessionId = NULL;
/** External functions. */
extern int VBoxServiceWinGetAddsVersion(uint32_t uiClientID);
extern int VBoxServiceWinGetComponentVersions(uint32_t uiClientID);
#endif


/** @copydoc VBOXSERVICE::pfnPreInit */
static DECLCALLBACK(int) VBoxServiceVMInfoPreInit(void)
{
    return VINF_SUCCESS;
}


/** @copydoc VBOXSERVICE::pfnOption */
static DECLCALLBACK(int) VBoxServiceVMInfoOption(const char **ppszShort, int argc, char **argv, int *pi)
{
    int rc = -1;
    if (ppszShort)
        /* no short options */;
    else if (!strcmp(argv[*pi], "--vminfo-interval"))
        rc = VBoxServiceArgUInt32(argc, argv, "", pi,
                                  &g_VMInfoInterval, 1, UINT32_MAX - 1);
    return rc;
}


/** @copydoc VBOXSERVICE::pfnInit */
static DECLCALLBACK(int) VBoxServiceVMInfoInit(void)
{
    /*
     * If not specified, find the right interval default.
     * Then create the event sem to block on.
     */
    if (!g_VMInfoInterval)
        g_VMInfoInterval = g_DefaultInterval * 1000;
    if (!g_VMInfoInterval)
        g_VMInfoInterval = 10 * 1000;

    int rc = RTSemEventMultiCreate(&g_VMInfoEvent);
    AssertRCReturn(rc, rc);

#ifdef RT_OS_WINDOWS
    /* Get function pointers. */
    HMODULE hKernel32 = LoadLibrary("kernel32");
    if (NULL != hKernel32)
    {
        g_pfnWTSGetActiveConsoleSessionId = (fnWTSGetActiveConsoleSessionId)GetProcAddress(hKernel32, "WTSGetActiveConsoleSessionId");
        FreeLibrary(hKernel32);
    }
#endif

    rc = VbglR3GuestPropConnect(&g_VMInfoGuestPropSvcClientID);
    if (RT_SUCCESS(rc))
        VBoxServiceVerbose(3, "Property Service Client ID: %#x\n", g_VMInfoGuestPropSvcClientID);
    else
    {
        VBoxServiceError("Failed to connect to the guest property service! Error: %Rrc\n", rc);
        RTSemEventMultiDestroy(g_VMInfoEvent);
        g_VMInfoEvent = NIL_RTSEMEVENTMULTI;
    }

    return rc;
}


/** @copydoc VBOXSERVICE::pfnWorker */
DECLCALLBACK(int) VBoxServiceVMInfoWorker(bool volatile *pfShutdown)
{
    int rc = VINF_SUCCESS;

    /*
     * Tell the control thread that it can continue
     * spawning services.
     */
    RTThreadUserSignal(RTThreadSelf());

#ifdef RT_OS_WINDOWS
    /* Required for network information (must be called per thread). */
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData)) {
         VBoxServiceError("WSAStartup failed! Error: %Rrc\n", RTErrConvertFromWin32(WSAGetLastError()));
    }
#endif /* !RT_OS_WINDOWS */

    /* First get information that won't change while the OS is running. */
    char szInfo[256] = {0};
    rc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szInfo, sizeof(szInfo));
    VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/OS/Product", "%s", szInfo);

    rc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szInfo, sizeof(szInfo));
    VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/OS/Release", "%s", szInfo);

    rc = RTSystemQueryOSInfo(RTSYSOSINFO_VERSION, szInfo, sizeof(szInfo));
    VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/OS/Version", "%s", szInfo);

    rc = RTSystemQueryOSInfo(RTSYSOSINFO_SERVICE_PACK, szInfo, sizeof(szInfo));
    VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/OS/ServicePack", "%s", szInfo);

    /* Retrieve version information about Guest Additions and installed files (components). */
#ifdef RT_OS_WINDOWS
    rc = VBoxServiceWinGetAddsVersion(g_VMInfoGuestPropSvcClientID);
    rc = VBoxServiceWinGetComponentVersions(g_VMInfoGuestPropSvcClientID);
#else
    /* VBoxServiceGetAddsVersion !RT_OS_WINDOWS */
    VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestAdd/Version", "%s", VBOX_VERSION_STRING);

    char szRevision[32];
    RTStrPrintf(szRevision, sizeof(szRevision), "%u", VBOX_SVN_REV);
    VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestAdd/Revision", "%s", szRevision);
#endif

    /* Now enter the loop retrieving runtime data continuously. */
    unsigned cErrors = 0;
    for (;;)
    {
        /* Enumerate logged in users. */
        uint32_t uiUserCount = 0;
        char szUserList[4096] = {0};

#ifdef RT_OS_WINDOWS
 #ifndef TARGET_NT4
        PLUID pSessions = NULL;
        ULONG ulCount = 0;
        NTSTATUS r = 0;

        char* pszTemp = NULL;

        /* This function can report stale or orphaned interactive logon sessions of already logged
           off users (especially in Windows 2000). */
        r = ::LsaEnumerateLogonSessions(&ulCount, &pSessions);
        VBoxServiceVerbose(3, "Users: Found %ld users.\n", ulCount);

        if (r != STATUS_SUCCESS)
        {
            VBoxServiceError("LsaEnumerate failed %lu\n", LsaNtStatusToWinError(r));
            return 1;
        }

        PLUID pLuid = NULL;
        DWORD dwNumOfProcLUIDs = VBoxServiceVMInfoWinGetLUIDsFromProcesses(&pLuid);

        VBOXSERVICEVMINFOUSER userInfo;
        ZeroMemory (&userInfo, sizeof(VBOXSERVICEVMINFOUSER));

        for (int i = 0; i<(int)ulCount; i++)
        {
            if (VBoxServiceVMInfoWinIsLoggedIn(&userInfo, &pSessions[i], pLuid, dwNumOfProcLUIDs))
            {
                if (uiUserCount > 0)
                    strcat (szUserList, ",");

                uiUserCount++;

                RTUtf16ToUtf8(userInfo.szUser, &pszTemp);
                strcat(szUserList, pszTemp);
                RTMemFree(pszTemp);
            }
        }

        if (NULL != pLuid)
            ::LocalFree (pLuid);

        ::LsaFreeReturnBuffer(pSessions);
 #endif /* TARGET_NT4 */
#elif defined(RT_OS_FREEBSD)
        /** @todo FreeBSD: Port logged on user info retrival. */
#elif defined(RT_OS_OS2)
        /** @todo OS/2: Port logged on (LAN/local/whatever) user info retrival. */
#else
        utmp* ut_user;
        rc = utmpname(UTMP_FILE);
        if (rc != 0)
        {
            VBoxServiceError("Could not set  UTMP file! Error: %ld", errno);
        }
        setutent();
        while ((ut_user=getutent()))
        {
            /* Make sure we don't add user names which are not
             * part of type USER_PROCESS and don't add same users twice. */
            if (   (ut_user->ut_type == USER_PROCESS)
                && (strstr(szUserList, ut_user->ut_user) == NULL))
            {
                /** @todo Do we really want to filter out double user names? (Same user logged in twice) */
                if (uiUserCount > 0)
                    strcat(szUserList, ",");
                strcat(szUserList, ut_user->ut_user);
                uiUserCount++;
            }
        }
        endutent();
#endif /* !RT_OS_WINDOWS */

        if (uiUserCount > 0)
            VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/OS/LoggedInUsersList", "%s", szUserList);
        else
            VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/OS/LoggedInUsersList", NULL);
        VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/OS/LoggedInUsers", "%u", uiUserCount);
        if (g_VMInfoLoggedInUsers != uiUserCount || g_VMInfoLoggedInUsers == UINT32_MAX)
        {
            /* Update this property ONLY if there is a real change from no users to
             * users or vice versa. The only exception is that the initialization
             * forces an update, but only once. This ensures consistent property
             * settings even if the VM aborted previously. */
            if (uiUserCount == 0)
                VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/OS/NoLoggedInUsers", "true");
            else if (g_VMInfoLoggedInUsers == 0)
                VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/OS/NoLoggedInUsers", "false");
        }
        g_VMInfoLoggedInUsers = uiUserCount;

        /* Get network configuration. */
        /** @todo Throw this code into a separate function/module? */
       int nNumInterfaces = 0;
#ifdef RT_OS_WINDOWS
        SOCKET sd = WSASocket(AF_INET, SOCK_DGRAM, 0, 0, 0, 0);
        if (sd == SOCKET_ERROR) /* Socket invalid. */
        {
            VBoxServiceError("Failed to get a socket: Error %d\n", WSAGetLastError());
            return -1;
        }

        INTERFACE_INFO InterfaceList[20] = {0};
        unsigned long nBytesReturned = 0;
        if (WSAIoctl(sd,
                     SIO_GET_INTERFACE_LIST,
                     0,
                     0,
                     &InterfaceList,
                     sizeof(InterfaceList),
                     &nBytesReturned,
                     0,
                     0) ==  SOCKET_ERROR)
        {
            VBoxServiceError("Failed to WSAIoctl() on socket: Error: %d\n", WSAGetLastError());
            return -1;
        }
        nNumInterfaces = nBytesReturned / sizeof(INTERFACE_INFO);
#else
        int sd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sd < 0) /* Socket invalid. */
        {
            VBoxServiceError("Failed to get a socket: Error %d\n", errno);
            return -1;
        }

        ifconf ifcfg;
        char buffer[1024] = {0};
        ifcfg.ifc_len = sizeof(buffer);
        ifcfg.ifc_buf = buffer;
        if (ioctl(sd, SIOCGIFCONF, &ifcfg) < 0)
        {
            VBoxServiceError("Failed to ioctl(SIOCGIFCONF) on socket: Error %d\n", errno);
            return -1;
        }

        ifreq* ifrequest = ifcfg.ifc_req;
        ifreq *ifreqitem = NULL;
        nNumInterfaces = ifcfg.ifc_len / sizeof(ifreq);
#endif
        char szPropPath [FILENAME_MAX];
        char szTemp [FILENAME_MAX];
        int iCurIface = 0;

        VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/Net/Count", "%d",
                              nNumInterfaces > 1 ? nNumInterfaces-1 : 0);

        /** @todo Use GetAdaptersInfo() and GetAdapterAddresses (IPv4 + IPv6) for more information. */
        for (int i = 0; i < nNumInterfaces; ++i)
        {
            sockaddr_in *pAddress;
            u_long nFlags = 0;
#ifdef RT_OS_WINDOWS
            if (InterfaceList[i].iiFlags & IFF_LOOPBACK) /* Skip loopback device. */
                continue;
            nFlags = InterfaceList[i].iiFlags;
            pAddress = (sockaddr_in *)&(InterfaceList[i].iiAddress);
#else
            if (ioctl(sd, SIOCGIFFLAGS, &ifrequest[i]) < 0)
            {
                VBoxServiceError("Failed to ioctl(SIOCGIFFLAGS) on socket: Error %d\n", errno);
                return -1;
            }
            if (ifrequest[i].ifr_flags & IFF_LOOPBACK) /* Skip loopback device. */
                continue;
            nFlags = ifrequest[i].ifr_flags;
            pAddress = ((sockaddr_in *)&ifrequest[i].ifr_addr);
#endif
            Assert(pAddress);
            RTStrPrintf(szPropPath, sizeof(szPropPath), "/VirtualBox/GuestInfo/Net/%d/V4/IP", iCurIface);
            VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, szPropPath, "%s", inet_ntoa(pAddress->sin_addr));

#ifdef RT_OS_WINDOWS
            pAddress = (sockaddr_in *) & (InterfaceList[i].iiBroadcastAddress);
#else
            if (ioctl(sd, SIOCGIFBRDADDR, &ifrequest[i]) < 0)
            {
                VBoxServiceError("Failed to ioctl(SIOCGIFBRDADDR) on socket: Error %d\n", errno);
                return -1;
            }
            pAddress = (sockaddr_in *)&ifrequest[i].ifr_broadaddr;
#endif
            RTStrPrintf(szPropPath, sizeof(szPropPath), "/VirtualBox/GuestInfo/Net/%d/V4/Broadcast", iCurIface);
            VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, szPropPath, "%s", inet_ntoa(pAddress->sin_addr));

#ifdef RT_OS_WINDOWS
            pAddress = (sockaddr_in *)&(InterfaceList[i].iiNetmask);
#else
            if (ioctl(sd, SIOCGIFNETMASK, &ifrequest[i]) < 0)
            {
                VBoxServiceError("Failed to ioctl(SIOCGIFBRDADDR) on socket: Error %d\n", errno);
                return -1;
            }
 #if defined(RT_OS_FREEBSD) || defined(RT_OS_OS2) || defined(RT_OS_SOLARIS)
            pAddress = (sockaddr_in *)&ifrequest[i].ifr_addr;
 #else
            pAddress = (sockaddr_in *)&ifrequest[i].ifr_netmask;
 #endif

#endif
            RTStrPrintf(szPropPath, sizeof(szPropPath), "/VirtualBox/GuestInfo/Net/%d/V4/Netmask", iCurIface);
            VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, szPropPath, "%s", inet_ntoa(pAddress->sin_addr));

            RTStrPrintf(szPropPath, sizeof(szPropPath), "/VirtualBox/GuestInfo/Net/%d/Status", iCurIface);
            VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, szPropPath,
                                  nFlags & IFF_UP ? "Up" : "Down");

            iCurIface++;
        }
#ifdef RT_OS_WINDOWS
        if (sd) closesocket(sd);
#else
        if (sd) close(sd);
#endif /* !RT_OS_WINDOWS */

        /*
         * Block for a while.
         *
         * The event semaphore takes care of ignoring interruptions and it
         * allows us to implement service wakeup later.
         */
        if (*pfShutdown)
            break;
        int rc2 = RTSemEventMultiWait(g_VMInfoEvent, g_VMInfoInterval);
        if (*pfShutdown)
            break;
        if (rc2 != VERR_TIMEOUT && RT_FAILURE(rc2))
        {
            VBoxServiceError("RTSemEventMultiWait failed; rc2=%Rrc\n", rc2);
            rc = rc2;
            break;
        }
    }

#ifdef RT_OS_WINDOWS
    WSACleanup();
#endif /* !RT_OS_WINDOWS */

    RTSemEventMultiDestroy(g_VMInfoEvent);
    g_VMInfoEvent = NIL_RTSEMEVENTMULTI;
    return rc;
}


/** @copydoc VBOXSERVICE::pfnStop */
static DECLCALLBACK(void) VBoxServiceVMInfoStop(void)
{
    RTSemEventMultiSignal(g_VMInfoEvent);
}


/** @copydoc VBOXSERVICE::pfnTerm */
static DECLCALLBACK(void) VBoxServiceVMInfoTerm(void)
{
    int rc;

    if (g_VMInfoEvent != NIL_RTSEMEVENTMULTI)
    {
        /** @todo temporary solution: Zap all values which are not valid
         *        anymore when VM goes down (reboot/shutdown ). Needs to
         *        be replaced with "temporary properties" later.
         *
         *  @todo r=bird: This code isn't called on non-Windows systems. We need
         *        a more formal way of shutting down the service for that to work.
         */
        rc = VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/OS/LoggedInUsersList", NULL);
        rc = VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/OS/LoggedInUsers", "%d", 0);
        if (g_VMInfoLoggedInUsers > 0)
            VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/OS/NoLoggedInUsers", "true");

        const char *apszPat[1] = { "/VirtualBox/GuestInfo/Net/*" };
        rc = VbglR3GuestPropDelSet(g_VMInfoGuestPropSvcClientID, &apszPat[0], RT_ELEMENTS(apszPat));
        rc = VBoxServiceWritePropF(g_VMInfoGuestPropSvcClientID, "/VirtualBox/GuestInfo/Net/Count", "%d", 0);

        /* Disconnect from guest properties service. */
        rc = VbglR3GuestPropDisconnect(g_VMInfoGuestPropSvcClientID);
        if (RT_FAILURE(rc))
            VBoxServiceError("Failed to disconnect from guest property service! Error: %Rrc\n", rc);
        g_VMInfoGuestPropSvcClientID = 0;


        RTSemEventMultiDestroy(g_VMInfoEvent);
        g_VMInfoEvent = NIL_RTSEMEVENTMULTI;
    }
}


/**
 * The 'vminfo' service description.
 */
VBOXSERVICE g_VMInfo =
{
    /* pszName. */
    "vminfo",
    /* pszDescription. */
    "Virtual Machine Information",
    /* pszUsage. */
    "[--vminfo-interval <ms>]"
    ,
    /* pszOptions. */
    "    --vminfo-interval   Specifies the interval at which to retrieve the\n"
    "                        VM information. The default is 10000 ms.\n"
    ,
    /* methods */
    VBoxServiceVMInfoPreInit,
    VBoxServiceVMInfoOption,
    VBoxServiceVMInfoInit,
    VBoxServiceVMInfoWorker,
    VBoxServiceVMInfoStop,
    VBoxServiceVMInfoTerm
};

