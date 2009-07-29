#include "slirp.h"
#ifdef RT_OS_OS2
# include <paths.h>
#endif

#include <VBox/err.h>
#include <VBox/pdmdrv.h>
#include <iprt/assert.h>
#ifndef RT_OS_WINDOWS
# include <sys/ioctl.h>
# include <poll.h>
#else
# include <Winnls.h>
# define _WINSOCK2API_
# include <IPHlpApi.h>
#endif
#ifdef VBOX_WITH_SLIRP_ALIAS
# include <alias.h>
#endif

#if !defined(RT_OS_WINDOWS)

#  define DO_ENGAGE_EVENT1(so, fdset, label)                        \
    do {                                                            \
        if(    so->so_poll_index != -1                              \
            && so->s == polls[so->so_poll_index].fd) {              \
            polls[so->so_poll_index].events |= N_(fdset ## _poll);  \
            break; /* out of this loop */                           \
        }                                                           \
        AssertRelease(poll_index < (nfds));                         \
        AssertRelease(poll_index >= 0 && poll_index < (nfds));      \
        polls[poll_index].fd = (so)->s;                             \
        (so)->so_poll_index = poll_index;                           \
        polls[poll_index].events = N_(fdset ## _poll);              \
        polls[poll_index].revents = 0;                              \
        poll_index++;                                               \
    } while(0)


#  define DO_ENGAGE_EVENT2(so, fdset1, fdset2, label)           \
    do {                                                        \
        if(    so->so_poll_index != -1                          \
            && so->s == polls[so->so_poll_index].fd) {          \
            polls[so->so_poll_index].events |=                  \
                N_(fdset1 ## _poll) | N_(fdset1 ## _poll);      \
            break; /* out of this loop */                       \
        }                                                       \
        AssertRelease(poll_index < (nfds));                     \
        polls[poll_index].fd = (so)->s;                         \
        (so)->so_poll_index = poll_index;                       \
        polls[poll_index].events =                              \
            N_(fdset1 ## _poll) | N_(fdset1 ## _poll);          \
        poll_index++;                                           \
    } while(0)

#  define DO_POLL_EVENTS(rc, error, so, events, label) do {} while (0)

#  define DO_CHECK_FD_SET(so, events, fdset) (  ((so)->so_poll_index != -1)                                     \
                                                && ((so)->so_poll_index <= ndfs)                                \
                                                && ((so)->s == polls[so->so_poll_index].fd)                     \
                                                && (polls[(so)->so_poll_index].revents & N_(fdset ## _poll)))
#  define DO_UNIX_CHECK_FD_SET(so, events, fdset ) DO_CHECK_FD_SET((so), (events), fdset) /*specific for Unix API */
#  define DO_WIN_CHECK_FD_SET(so, events, fdset ) 0 /* specific for Windows Winsock API */

# ifndef RT_OS_WINDOWS

#  ifndef RT_OS_LINUX
#   define readfds_poll (POLLRDNORM)
#   define writefds_poll (POLLWRNORM)
#   define xfds_poll (POLLRDBAND|POLLWRBAND|POLLPRI)
#  else
#   define readfds_poll (POLLIN)
#   define writefds_poll (POLLOUT)
#   define xfds_poll (POLLPRI)
#  endif
#  define rderr_poll (POLLERR)
#  define rdhup_poll (POLLHUP)
#  define nval_poll (POLLNVAL)

#  define ICMP_ENGAGE_EVENT(so, fdset)              \
    do {                                            \
        if (pData->icmp_socket.s != -1)             \
            DO_ENGAGE_EVENT1((so), fdset, ICMP);    \
    } while (0)
# else /* !RT_OS_WINDOWS */
#  define DO_WIN_CHECK_FD_SET(so, events, fdset ) DO_CHECK_FD_SET((so), (events), fdset)
#  define ICMP_ENGAGE_EVENT(so, fdset) do {} while(0)
#endif /* RT_OS_WINDOWS */

#else /* defined(RT_OS_WINDOWS) */

/*
 * On Windows, we will be notified by IcmpSendEcho2() when the response arrives.
 * So no call to WSAEventSelect necessary.
 */
# define ICMP_ENGAGE_EVENT(so, fdset)                do {} while(0)

# define DO_ENGAGE_EVENT1(so, fdset1, label)                                                    \
    do {                                                                                        \
        rc = WSAEventSelect((so)->s, VBOX_SOCKET_EVENT, FD_ALL_EVENTS);                         \
        if (rc == SOCKET_ERROR)                                                                 \
        {                                                                                       \
            /* This should not happen */                                                        \
            error = WSAGetLastError();                                                          \
            LogRel(("WSAEventSelect (" #label ") error %d (so=%x, socket=%s, event=%x)\n",      \
                        error, (so), (so)->s, VBOX_SOCKET_EVENT));                              \
        }                                                                                       \
    } while(0);                                                                                 \
    CONTINUE(label)

# define DO_ENGAGE_EVENT2(so, fdset1, fdset2, label) \
    DO_ENGAGE_EVENT1((so), (fdset1), label)

# define DO_POLL_EVENTS(rc, error, so, events, label)                       \
    (rc) = WSAEnumNetworkEvents((so)->s, VBOX_SOCKET_EVENT, (events));      \
    if ((rc) == SOCKET_ERROR)                                               \
    {                                                                       \
        (error) = WSAGetLastError();                                        \
        LogRel(("WSAEnumNetworkEvents " #label " error %d\n", (error)));    \
        CONTINUE(label);                                                    \
    }

# define acceptds_win FD_ACCEPT
# define acceptds_win_bit FD_ACCEPT_BIT

# define readfds_win FD_READ
# define readfds_win_bit FD_READ_BIT

# define writefds_win FD_WRITE
# define writefds_win_bit FD_WRITE_BIT

# define xfds_win FD_OOB
# define xfds_win_bit FD_OOB_BIT

# define DO_CHECK_FD_SET(so, events, fdset)  \
    (((events).lNetworkEvents & fdset ## _win) && ((events).iErrorCode[fdset ## _win_bit] == 0))

# define DO_WIN_CHECK_FD_SET(so, events, fdset ) DO_CHECK_FD_SET((so), (events), fdset)
# define DO_UNIX_CHECK_FD_SET(so, events, fdset ) 1 /*specific for Unix API */

#endif /* defined(RT_OS_WINDOWS) */

#define TCP_ENGAGE_EVENT1(so, fdset) \
    DO_ENGAGE_EVENT1((so), fdset, tcp)

#define TCP_ENGAGE_EVENT2(so, fdset1, fdset2) \
    DO_ENGAGE_EVENT2((so), fdset1, fdset2, tcp)

#define UDP_ENGAGE_EVENT(so, fdset) \
    DO_ENGAGE_EVENT1((so), fdset, udp)

#define POLL_TCP_EVENTS(rc, error, so, events) \
    DO_POLL_EVENTS((rc), (error), (so), (events), tcp)

#define POLL_UDP_EVENTS(rc, error, so, events) \
    DO_POLL_EVENTS((rc), (error), (so), (events), udp)

#define CHECK_FD_SET(so, events, set)           \
    (DO_CHECK_FD_SET((so), (events), set))

#define WIN_CHECK_FD_SET(so, events, set)           \
    (DO_WIN_CHECK_FD_SET((so), (events), set))
#define UNIX_CHECK_FD_SET(so, events, set) \
    (DO_UNIX_CHECK_FD_SET(so, events, set))

/*
 * Loging macros
 */
#if VBOX_WITH_DEBUG_NAT_SOCKETS
# if defined(RT_OS_WINDOWS)
#  define  DO_LOG_NAT_SOCK(so, proto, winevent, r_fdset, w_fdset, x_fdset)             \
   do {                                                                                \
       LogRel(("  " #proto " %R[natsock] %R[natwinnetevents]\n", (so), (winevent)));   \
   } while (0)
# else /* RT_OS_WINDOWS */
#  define  DO_LOG_NAT_SOCK(so, proto, winevent, r_fdset, w_fdset, x_fdset)         \
   do {                                                                            \
           LogRel(("  " #proto " %R[natsock] %s %s %s er: %s, %s, %s\n", (so),     \
                    CHECK_FD_SET(so, ign ,r_fdset) ? "READ":"",                    \
                    CHECK_FD_SET(so, ign, w_fdset) ? "WRITE":"",                   \
                    CHECK_FD_SET(so, ign, x_fdset) ? "OOB":"",                     \
                    CHECK_FD_SET(so, ign, rderr) ? "RDERR":"",                     \
                    CHECK_FD_SET(so, ign, rdhup) ? "RDHUP":"",                     \
                    CHECK_FD_SET(so, ign, nval) ? "RDNVAL":""));                   \
   } while (0)
# endif /* !RT_OS_WINDOWS */
#else /* VBOX_WITH_DEBUG_NAT_SOCKETS */
# define DO_LOG_NAT_SOCK(so, proto, winevent, r_fdset, w_fdset, x_fdset) do {} while (0)
#endif /* !VBOX_WITH_DEBUG_NAT_SOCKETS */

#define LOG_NAT_SOCK(so, proto, winevent, r_fdset, w_fdset, x_fdset) DO_LOG_NAT_SOCK((so), proto, (winevent), r_fdset, w_fdset, x_fdset)

static const uint8_t special_ethaddr[6] =
{
    0x52, 0x54, 0x00, 0x12, 0x35, 0x00
};

static const uint8_t broadcast_ethaddr[6] =
{
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

const uint8_t zerro_ethaddr[6] =
{
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0
};

#ifdef RT_OS_WINDOWS
static int get_dns_addr_domain(PNATState pData, bool fVerbose,
                               struct in_addr *pdns_addr,
                               const char **ppszDomain)
{
    /* Get amount of memory required for operation */
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX; /*GAA_FLAG_INCLUDE_ALL_INTERFACES;*/ /* all interfaces registered in NDIS */
    PIP_ADAPTER_ADDRESSES addresses = NULL;
    PIP_ADAPTER_ADDRESSES addr = NULL;
    PIP_ADAPTER_DNS_SERVER_ADDRESS dns = NULL;
    ULONG size = 0;
    int wlen = 0;
    char *suffix;
    struct dns_entry *da = NULL;
    struct dns_domain_entry *dd = NULL;
    ULONG ret = ERROR_SUCCESS;

    /* @todo add SKIPing flags to get only required information */

    ret = pData->pfGetAdaptersAddresses(AF_INET, 0, NULL /* reserved */, addresses, &size);
    if (ret != ERROR_BUFFER_OVERFLOW)
    {
        LogRel(("NAT: error %lu occurred on capacity detection operation\n", ret));
        return -1;
    }

    if (size == 0)
    {
        LogRel(("NAT: Win socket API returns non capacity\n"));
        return -1;
    }

    addresses = RTMemAllocZ(size);
    if (addresses == NULL)
    {
        LogRel(("NAT: No memory available \n"));
        return -1;
    }

    ret = pData->pfGetAdaptersAddresses(AF_INET, 0, NULL /* reserved */, addresses, &size);
    if (ret != ERROR_SUCCESS)
    {
        LogRel(("NAT: error %lu occurred on fetching adapters info\n", ret));
        RTMemFree(addresses);
        return -1;
    }
    addr = addresses;
    while(addr != NULL)
    {
        int found;
        if (addr->OperStatus != IfOperStatusUp)
            goto next;
        dns = addr->FirstDnsServerAddress;
        while (dns != NULL)
        {
            struct sockaddr *saddr = dns->Address.lpSockaddr;
            if (saddr->sa_family != AF_INET)
                goto next_dns;
            /* add dns server to list */
            da = RTMemAllocZ(sizeof(struct dns_entry));
            if (da == NULL)
            {
                LogRel(("NAT: Can't allocate buffer for DNS entry\n"));
                RTMemFree(addresses);
                return VERR_NO_MEMORY;
            }
            LogRel(("NAT: adding %R[IP4] to DNS server list\n", &((struct sockaddr_in *)saddr)->sin_addr));
            if ((((struct sockaddr_in *)saddr)->sin_addr.s_addr & htonl(IN_CLASSA_NET)) == ntohl(INADDR_LOOPBACK & IN_CLASSA_NET)) {
                da->de_addr.s_addr = htonl(ntohl(special_addr.s_addr) | CTL_ALIAS);
            }
            else
            {
                da->de_addr.s_addr = ((struct sockaddr_in *)saddr)->sin_addr.s_addr;
            }
            TAILQ_INSERT_HEAD(&pData->dns_list_head, da, de_list);

            if (addr->DnsSuffix == NULL)
                goto next_dns;

            /*uniq*/
            RTUtf16ToUtf8(addr->DnsSuffix, &suffix);

            if (!suffix || strlen(suffix) == 0) {
                RTStrFree(suffix);
                goto next_dns;
            }

            found = 0;
            LIST_FOREACH(dd, &pData->dns_domain_list_head, dd_list)
            {
                if (   dd->dd_pszDomain != NULL
                    && strcmp(dd->dd_pszDomain, suffix) == 0)
                {
                    found = 1;
                    RTStrFree(suffix);
                    break;
                }
            }
            if (found == 0)
            {
                dd = RTMemAllocZ(sizeof(struct dns_domain_entry));
                if (dd == NULL)
                {
                    LogRel(("NAT: not enough memory\n"));
                    RTStrFree(suffix);
                    RTMemFree(addresses);
                    return VERR_NO_MEMORY;
                }
                dd->dd_pszDomain = suffix;
                LogRel(("NAT: adding domain name %s to search list\n", dd->dd_pszDomain));
                LIST_INSERT_HEAD(&pData->dns_domain_list_head, dd, dd_list);
            }
        next_dns:
            dns = dns->Next;
        }
    next:
        addr = addr->Next;
    }
    RTMemFree(addresses);
    return 0;
}

#else /* !RT_OS_WINDOWS */

static int get_dns_addr_domain(PNATState pData, bool fVerbose,
                               struct in_addr *pdns_addr,
                               const char **ppszDomain)
{
    char buff[512];
    char buff2[256];
    FILE *f = NULL;
    int found = 0;
    struct in_addr tmp_addr;

#ifdef RT_OS_OS2
    /* Try various locations. */
    char *etc = getenv("ETC");
    if (etc)
    {
        snprintf(buff, sizeof(buff), "%s/RESOLV2", etc);
        f = fopen(buff, "rt");
    }
    if (!f)
    {
        snprintf(buff, sizeof(buff), "%s/RESOLV2", _PATH_ETC);
        f = fopen(buff, "rt");
    }
    if (!f)
    {
        snprintf(buff, sizeof(buff), "%s/resolv.conf", _PATH_ETC);
        f = fopen(buff, "rt");
    }
#else
#ifndef DEBUG_vvl
    f = fopen("/etc/resolv.conf", "r");
#else
    char *home = getenv("HOME");
    snprintf(buff, sizeof(buff), "%s/resolv.conf", home);
    f = fopen(buff, "r");
    if (f != NULL)
    {
        Log(("NAT: DNS we're using %s\n", buff));
    }
    else
    {
        f = fopen("/etc/resolv.conf", "r");
        Log(("NAT: DNS we're using %s\n", buff));
    }
#endif
#endif
    if (!f)
        return -1;

    if (ppszDomain)
        *ppszDomain = NULL;
    Log(("nat: DNS Servers:\n"));
    while (fgets(buff, 512, f) != NULL)
    {
        struct dns_entry *da = NULL;
        if (sscanf(buff, "nameserver%*[ \t]%256s", buff2) == 1)
        {
            if (!inet_aton(buff2, &tmp_addr))
                continue;
            /*localhost mask */
            da = RTMemAllocZ(sizeof (struct dns_entry));
            if (da == NULL)
            {
                LogRel(("can't alloc memory for DNS entry\n"));
                return -1;
            }
            /*check */
            da->de_addr.s_addr = tmp_addr.s_addr;
            if ((da->de_addr.s_addr & htonl(IN_CLASSA_NET)) == ntohl(INADDR_LOOPBACK & IN_CLASSA_NET)) {
                da->de_addr.s_addr = htonl(ntohl(special_addr.s_addr) | CTL_ALIAS);
            }
            TAILQ_INSERT_HEAD(&pData->dns_list_head, da, de_list);
            found++;
        }
        if ((!strncmp(buff, "domain", 6) || !strncmp(buff, "search", 6)))
        {
            char *tok;
            char *saveptr;
            struct dns_domain_entry *dd = NULL;
            int found = 0;
            tok = strtok_r(&buff[6], " \t\n", &saveptr);
            LIST_FOREACH(dd, &pData->dns_domain_list_head, dd_list)
            {
                if(    tok != NULL
                    && strcmp(tok, dd->dd_pszDomain) == 0)
                {
                    found = 1;
                    break;
                }
            }
            if (tok != NULL && found == 0) {
                dd = RTMemAllocZ(sizeof(struct dns_domain_entry));
                if (dd == NULL)
                {
                    LogRel(("NAT: not enought memory to add domain list\n"));
                    return VERR_NO_MEMORY;
                }
                dd->dd_pszDomain = RTStrDup(tok);
                LogRel(("NAT: adding domain name %s to search list\n", dd->dd_pszDomain));
                LIST_INSERT_HEAD(&pData->dns_domain_list_head, dd, dd_list);
            }
        }
    }
    fclose(f);
    if (!found)
        return -1;
    return 0;
}

#endif

static int slirp_init_dns_list(PNATState pData)
{
    TAILQ_INIT(&pData->dns_list_head);
    LIST_INIT(&pData->dns_domain_list_head);
    return get_dns_addr_domain(pData, true, NULL, NULL);
}

static void slirp_release_dns_list(PNATState pData)
{
    struct dns_entry *de = NULL;
    struct dns_domain_entry *dd = NULL;
    while(!TAILQ_EMPTY(&pData->dns_list_head)) {
        de = TAILQ_FIRST(&pData->dns_list_head);
        TAILQ_REMOVE(&pData->dns_list_head, de, de_list);
        RTMemFree(de);
    }
    while(!LIST_EMPTY(&pData->dns_domain_list_head)) {
        dd = LIST_FIRST(&pData->dns_domain_list_head);
        LIST_REMOVE(dd, dd_list);
        if (dd->dd_pszDomain != NULL)
            RTStrFree(dd->dd_pszDomain);
        RTMemFree(dd);
    }
}

int get_dns_addr(PNATState pData, struct in_addr *pdns_addr)
{
    return get_dns_addr_domain(pData, false, pdns_addr, NULL);
}

#ifndef VBOX_WITH_NAT_SERVICE
int slirp_init(PNATState *ppData, const char *pszNetAddr, uint32_t u32Netmask,
               bool fPassDomain, void *pvUser)
#else
int slirp_init(PNATState *ppData, uint32_t u32NetAddr, uint32_t u32Netmask,
               bool fPassDomain, void *pvUser)
#endif
{
    int fNATfailed = 0;
    int rc;
    PNATState pData = RTMemAllocZ(sizeof(NATState));
    *ppData = pData;
    if (!pData)
        return VERR_NO_MEMORY;
    if (u32Netmask & 0x1f)
        /* CTL is x.x.x.15, bootp passes up to 16 IPs (15..31) */
        return VERR_INVALID_PARAMETER;
    pData->fPassDomain = fPassDomain;
    pData->pvUser = pvUser;
    pData->netmask = u32Netmask;

    /* sockets & TCP defaults */
    pData->socket_rcv = 64 * _1K;
    pData->socket_snd = 64 * _1K;
    tcp_sndspace = 64 * _1K;
    tcp_rcvspace = 64 * _1K;

#ifdef RT_OS_WINDOWS
    {
        WSADATA Data;
        WSAStartup(MAKEWORD(2, 0), &Data);
    }
    pData->phEvents[VBOX_SOCKET_EVENT_INDEX] = CreateEvent(NULL, FALSE, FALSE, NULL);
#endif
#ifdef VBOX_WITH_SLIRP_MT
    QSOCKET_LOCK_CREATE(tcb);
    QSOCKET_LOCK_CREATE(udb);
    rc = RTReqCreateQueue(&pData->pReqQueue);
    AssertReleaseRC(rc);
#endif

    link_up = 1;

    debug_init();
    if_init(pData);
    ip_init(pData);
    icmp_init(pData);

    /* Initialise mbufs *after* setting the MTU */
    m_init(pData);

#ifndef VBOX_WITH_NAT_SERVICE
    inet_aton(pszNetAddr, &special_addr);
#else
    special_addr.s_addr = u32NetAddr;
#endif
#ifdef VBOX_WITHOUT_SLIRP_CLIENT_ETHER
    pData->slirp_ethaddr = &special_ethaddr[0];
#endif
    alias_addr.s_addr = special_addr.s_addr | htonl(CTL_ALIAS);
    /* @todo: add ability to configure this staff */

    /* set default addresses */
    inet_aton("127.0.0.1", &loopback_addr);
    if (slirp_init_dns_list(pData) < 0)
        fNATfailed = 1;

    dnsproxy_init(pData);

    getouraddr(pData);

#ifdef VBOX_WITH_SLIRP_ALIAS
    {
        int flags = 0;
        struct in_addr proxy_addr;
        pData->proxy_alias = LibAliasInit(pData, NULL);
        if (pData->proxy_alias == NULL)
        {
            LogRel(("NAT: LibAlias default rule wasn't initialized\n"));
            AssertMsgFailed(("NAT: LibAlias default rule wasn't initialized\n"));
        }
        flags = LibAliasSetMode(pData->proxy_alias, 0, 0);
        flags |= PKT_ALIAS_LOG; /* set logging */
        flags = LibAliasSetMode(pData->proxy_alias, flags, ~0);
        proxy_addr.s_addr = htonl(ntohl(special_addr.s_addr) | CTL_ALIAS);
        LibAliasSetAddress(pData->proxy_alias, proxy_addr);
        ftp_alias_load(pData);
        nbt_alias_load(pData);

    }
#endif
    return fNATfailed ? VINF_NAT_DNS : VINF_SUCCESS;
}

/**
 * Register statistics.
 */
void slirp_register_statistics(PNATState pData, PPDMDRVINS pDrvIns)
{
#ifdef VBOX_WITH_STATISTICS
# define COUNTER(name, type, units, dsc)                            \
    do {                                                            \
        PDMDrvHlpSTAMRegisterF(pDrvIns,                             \
                               &pData->Stat ## name,                \
                               type,                                \
                               STAMVISIBILITY_ALWAYS,               \
                               units,                               \
                               dsc,                                 \
                               "/Drivers/NAT%u/" #name,             \
                               pDrvIns->iInstance);                 \
    } while (0)

# define PROFILE_COUNTER(name, dsc)     COUNTER(name, STAMTYPE_PROFILE, STAMUNIT_TICKS_PER_CALL, dsc)
# define COUNTING_COUNTER(name, dsc)    COUNTER(name, STAMTYPE_COUNTER, STAMUNIT_COUNT,          dsc)

# include "counters.h"

# undef COUNTER
# undef PROFILE_COUNTER
# undef COUNTING_COUNTER
/** @todo register statistics for the variables dumped by:
 *  ipstats(pData); tcpstats(pData); udpstats(pData); icmpstats(pData);
 *  mbufstats(pData); sockstats(pData); */
#endif /* VBOX_WITH_STATISTICS */
}

/**
 * Deregister statistics.
 */
void slirp_deregister_statistics(PNATState pData, PPDMDRVINS pDrvIns)
{
#ifdef VBOX_WITH_STATISTICS
# define PROFILE_COUNTER(name, dsc)     PDMDrvHlpSTAMDeregister(pDrvIns, &pData->Stat ## name)
# define COUNTING_COUNTER(name, dsc)    PDMDrvHlpSTAMDeregister(pDrvIns, &pData->Stat ## name)

# include "counters.h"

# undef COUNTING_COUNTER
# undef PROFILE_COUNTER
#endif /* VBOX_WITH_STATISTICS */
}

/**
 * Marks the link as up, making it possible to establish new connections.
 */
void slirp_link_up(PNATState pData)
{
    link_up = 1;
}

/**
 * Marks the link as down and cleans up the current connections.
 */
void slirp_link_down(PNATState pData)
{
    struct socket *so;

    while ((so = tcb.so_next) != &tcb)
    {
        if (so->so_state & SS_NOFDREF || so->s == -1)
            sofree(pData, so);
        else
            tcp_drop(pData, sototcpcb(so), 0);
    }

    while ((so = udb.so_next) != &udb)
        udp_detach(pData, so);

    link_up = 0;
}

/**
 * Terminates the slirp component.
 */
void slirp_term(PNATState pData)
{
#ifdef RT_OS_WINDOWS
    pData->pfIcmpCloseHandle(pData->icmp_socket.sh);
    FreeLibrary(pData->hmIcmpLibrary);
    RTMemFree(pData->pvIcmpBuffer);
#else
    closesocket(pData->icmp_socket.s);
#endif

    slirp_link_down(pData);
    slirp_release_dns_list(pData);
    ftp_alias_unload(pData);
    nbt_alias_unload(pData);
#ifdef VBOX_WITH_SLIRP_ALIAS
    while(!LIST_EMPTY(&instancehead)) {
        struct libalias *la = LIST_FIRST(&instancehead);
        /* libalias do all clean up */
        LibAliasUninit(la);
    }
#endif
#ifdef RT_OS_WINDOWS
    WSACleanup();
#endif
#ifdef LOG_ENABLED
    Log(("\n"
         "NAT statistics\n"
         "--------------\n"
         "\n"));
    ipstats(pData);
    tcpstats(pData);
    udpstats(pData);
    icmpstats(pData);
    mbufstats(pData);
    sockstats(pData);
    Log(("\n"
         "\n"
         "\n"));
#endif
    RTMemFree(pData);
}


#define CONN_CANFSEND(so) (((so)->so_state & (SS_FCANTSENDMORE|SS_ISFCONNECTED)) == SS_ISFCONNECTED)
#define CONN_CANFRCV(so)  (((so)->so_state & (SS_FCANTRCVMORE|SS_ISFCONNECTED)) == SS_ISFCONNECTED)

/*
 * curtime kept to an accuracy of 1ms
 */
static void updtime(PNATState pData)
{
#ifdef RT_OS_WINDOWS
    struct _timeb tb;

    _ftime(&tb);
    curtime  = (u_int)tb.time * (u_int)1000;
    curtime += (u_int)tb.millitm;
#else
    gettimeofday(&tt, 0);

    curtime  = (u_int)tt.tv_sec  * (u_int)1000;
    curtime += (u_int)tt.tv_usec / (u_int)1000;

    if ((tt.tv_usec % 1000) >= 500)
        curtime++;
#endif
}

#ifdef RT_OS_WINDOWS
void slirp_select_fill(PNATState pData, int *pnfds)
#else /* RT_OS_WINDOWS */
void slirp_select_fill(PNATState pData, int *pnfds, struct pollfd *polls)
#endif /* !RT_OS_WINDOWS */
{
    struct socket *so, *so_next;
    int nfds;
#if defined(RT_OS_WINDOWS)
    int rc;
    int error;
#else
    int poll_index = 0;
#endif
    int i;

    STAM_PROFILE_START(&pData->StatFill, a);

    nfds = *pnfds;

    /*
     * First, TCP sockets
     */
    do_slowtimo = 0;
    if (!link_up)
        goto done;
    /*
     * *_slowtimo needs calling if there are IP fragments
     * in the fragment queue, or there are TCP connections active
     */
    /* XXX:
     * triggering of fragment expiration should be the same but use new macroses
     */
    do_slowtimo = (tcb.so_next != &tcb);
    if (!do_slowtimo)
    {
        for (i = 0; i < IPREASS_NHASH; i++)
        {
            if (!TAILQ_EMPTY(&ipq[i]))
            {
                do_slowtimo = 1;
                break;
            }
        }
    }
    ICMP_ENGAGE_EVENT(&pData->icmp_socket, readfds);

    STAM_COUNTER_RESET(&pData->StatTCP);
    STAM_COUNTER_RESET(&pData->StatTCPHot);

    QSOCKET_FOREACH(so, so_next, tcp)
    /* { */
#if !defined(RT_OS_WINDOWS)
        so->so_poll_index = -1;
#endif
        STAM_COUNTER_INC(&pData->StatTCP);

        /*
         * See if we need a tcp_fasttimo
         */
        if (    time_fasttimo == 0
                && so->so_tcpcb != NULL
                && so->so_tcpcb->t_flags & TF_DELACK)
            time_fasttimo = curtime; /* Flag when we want a fasttimo */

        /*
         * NOFDREF can include still connecting to local-host,
         * newly socreated() sockets etc. Don't want to select these.
         */
        if (so->so_state & SS_NOFDREF || so->s == -1)
            CONTINUE(tcp);

        /*
         * Set for reading sockets which are accepting
         */
        if (so->so_state & SS_FACCEPTCONN)
        {
            STAM_COUNTER_INC(&pData->StatTCPHot);
            TCP_ENGAGE_EVENT1(so, readfds);
            CONTINUE(tcp);
        }

        /*
         * Set for writing sockets which are connecting
         */
        if (so->so_state & SS_ISFCONNECTING)
        {
            Log2(("connecting %R[natsock] engaged\n",so));
            STAM_COUNTER_INC(&pData->StatTCPHot);
            TCP_ENGAGE_EVENT1(so, writefds);
        }

        /*
         * Set for writing if we are connected, can send more, and
         * we have something to send
         */
        if (CONN_CANFSEND(so) && so->so_rcv.sb_cc)
        {
            STAM_COUNTER_INC(&pData->StatTCPHot);
            TCP_ENGAGE_EVENT1(so, writefds);
        }

        /*
         * Set for reading (and urgent data) if we are connected, can
         * receive more, and we have room for it XXX /2 ?
         */
        if (CONN_CANFRCV(so) && (so->so_snd.sb_cc < (so->so_snd.sb_datalen/2)))
        {
            STAM_COUNTER_INC(&pData->StatTCPHot);
            TCP_ENGAGE_EVENT2(so, readfds, xfds);
        }
        LOOP_LABEL(tcp, so, so_next);
    }

    /*
     * UDP sockets
     */
    STAM_COUNTER_RESET(&pData->StatUDP);
    STAM_COUNTER_RESET(&pData->StatUDPHot);

    QSOCKET_FOREACH(so, so_next, udp)
    /* { */

        STAM_COUNTER_INC(&pData->StatUDP);
#if !defined(RT_OS_WINDOWS)
        so->so_poll_index = -1;
#endif

        /*
         * See if it's timed out
         */
        if (so->so_expire)
        {
            if (so->so_expire <= curtime)
            {
                Log2(("NAT: %R[natsock] expired\n", so));
                if (so->so_timeout != NULL)
                {
                    so->so_timeout(pData, so, so->so_timeout_arg);
                }
#ifdef VBOX_WITH_SLIRP_MT
                    /* we need so_next for continue our cycle*/
                so_next = so->so_next;
#endif
                UDP_DETACH(pData, so, so_next);
                CONTINUE_NO_UNLOCK(udp);
            }
            else
                do_slowtimo = 1; /* Let socket expire */
        }

        /*
         * When UDP packets are received from over the link, they're
         * sendto()'d straight away, so no need for setting for writing
         * Limit the number of packets queued by this session to 4.
         * Note that even though we try and limit this to 4 packets,
         * the session could have more queued if the packets needed
         * to be fragmented.
         *
         * (XXX <= 4 ?)
         */
        if ((so->so_state & SS_ISFCONNECTED) && so->so_queued <= 4)
        {
            STAM_COUNTER_INC(&pData->StatUDPHot);
            UDP_ENGAGE_EVENT(so, readfds);
        }
        LOOP_LABEL(udp, so, so_next);
    }
done:

#if defined(RT_OS_WINDOWS)
    *pnfds = VBOX_EVENT_COUNT;
#else /* RT_OS_WINDOWS */
    AssertRelease(poll_index <= *pnfds);
    *pnfds = poll_index;
#endif /* !RT_OS_WINDOWS */

    STAM_PROFILE_STOP(&pData->StatFill, a);
}

#if defined(RT_OS_WINDOWS)
void slirp_select_poll(PNATState pData, int fTimeout, int fIcmp)
#else /* RT_OS_WINDOWS */
void slirp_select_poll(PNATState pData, struct pollfd *polls, int ndfs)
#endif /* !RT_OS_WINDOWS */
{
    struct socket *so, *so_next;
    int ret;
#if defined(RT_OS_WINDOWS)
    WSANETWORKEVENTS NetworkEvents;
    int rc;
    int error;
#else
    int poll_index = 0;
#endif

    STAM_PROFILE_START(&pData->StatPoll, a);

    /* Update time */
    updtime(pData);

    /*
     * See if anything has timed out
     */
    if (link_up)
    {
        if (time_fasttimo && ((curtime - time_fasttimo) >= 2))
        {
            STAM_PROFILE_START(&pData->StatFastTimer, a);
            tcp_fasttimo(pData);
            time_fasttimo = 0;
            STAM_PROFILE_STOP(&pData->StatFastTimer, a);
        }
        if (do_slowtimo && ((curtime - last_slowtimo) >= 499))
        {
            STAM_PROFILE_START(&pData->StatSlowTimer, a);
            ip_slowtimo(pData);
            tcp_slowtimo(pData);
            last_slowtimo = curtime;
            STAM_PROFILE_STOP(&pData->StatSlowTimer, a);
        }
    }
#if defined(RT_OS_WINDOWS)
    if (fTimeout)
        return; /* only timer update */
#endif

    /*
     * Check sockets
     */
    if (!link_up)
        goto done;
#if defined(RT_OS_WINDOWS)
    /*XXX: before renaming please make see define
     * fIcmp in slirp_state.h
     */
    if (fIcmp)
        sorecvfrom(pData, &pData->icmp_socket);
#else
    if (   (pData->icmp_socket.s != -1)
        && CHECK_FD_SET(&pData->icmp_socket, ignored, readfds))
        sorecvfrom(pData, &pData->icmp_socket);
#endif
    /*
     * Check TCP sockets
     */
    QSOCKET_FOREACH(so, so_next, tcp)
    /* { */

#ifdef VBOX_WITH_SLIRP_MT
        if (   so->so_state & SS_NOFDREF
            && so->so_deleted == 1)
        {
            struct socket *son, *sop = NULL;
            QSOCKET_LOCK(tcb);
            if (so->so_next != NULL)
            {
                if (so->so_next != &tcb)
                    SOCKET_LOCK(so->so_next);
                son = so->so_next;
            }
            if (    so->so_prev != &tcb
                && so->so_prev != NULL)
            {
                SOCKET_LOCK(so->so_prev);
                sop = so->so_prev;
            }
            QSOCKET_UNLOCK(tcb);
            remque(pData, so);
            NSOCK_DEC();
            SOCKET_UNLOCK(so);
            SOCKET_LOCK_DESTROY(so);
            RTMemFree(so);
            so_next = son;
            if (sop != NULL)
                SOCKET_UNLOCK(sop);
            CONTINUE_NO_UNLOCK(tcp);
        }
#endif
        /*
         * FD_ISSET is meaningless on these sockets
         * (and they can crash the program)
         */
        if (so->so_state & SS_NOFDREF || so->s == -1)
            CONTINUE(tcp);

        POLL_TCP_EVENTS(rc, error, so, &NetworkEvents);

        LOG_NAT_SOCK(so, TCP, &NetworkEvents, readfds, writefds, xfds);


        /*
         * Check for URG data
         * This will soread as well, so no need to
         * test for readfds below if this succeeds
         */

        /* out-of-band data */
        if (CHECK_FD_SET(so, NetworkEvents, xfds))
        {
            sorecvoob(pData, so);
        }

        /*
         * Check sockets for reading
         */
        else if (   CHECK_FD_SET(so, NetworkEvents, readfds)
                 || WIN_CHECK_FD_SET(so, NetworkEvents, acceptds))
        {
            /*
             * Check for incoming connections
             */
            if (so->so_state & SS_FACCEPTCONN)
            {
                TCP_CONNECT(pData, so);
#if defined(RT_OS_WINDOWS)
                if (!(NetworkEvents.lNetworkEvents & FD_CLOSE))
#endif
                    CONTINUE(tcp);
            }

            ret = soread(pData, so);
            /* Output it if we read something */
            if (RT_LIKELY(ret > 0))
                TCP_OUTPUT(pData, sototcpcb(so));
        }

#if defined(RT_OS_WINDOWS)
        /*
         * Check for FD_CLOSE events.
         * in some cases once FD_CLOSE engaged on socket it could be flashed latter (for some reasons)
         */
        if (    (NetworkEvents.lNetworkEvents & FD_CLOSE)
            ||  (so->so_close == 1))
        {
            so->so_close = 1; /* mark it */
            /*
             * drain the socket
             */
            for (;;)
            {
                ret = soread(pData, so);
                if (ret > 0)
                    TCP_OUTPUT(pData, sototcpcb(so));
                else
                    break;
            }
            CONTINUE(tcp);
        }
#endif

        /*
         * Check sockets for writing
         */
        if (CHECK_FD_SET(so, NetworkEvents, writefds))
        {
            /*
             * Check for non-blocking, still-connecting sockets
             */
            if (so->so_state & SS_ISFCONNECTING)
            {
                Log2(("connecting %R[natsock] catched\n", so));
                /* Connected */
                so->so_state &= ~SS_ISFCONNECTING;

                /*
                 * This should be probably guarded by PROBE_CONN too. Anyway,
                 * we disable it on OS/2 because the below send call returns
                 * EFAULT which causes the opened TCP socket to close right
                 * after it has been opened and connected.
                 */
#ifndef RT_OS_OS2
                ret = send(so->s, (const char *)&ret, 0, 0);
                if (ret < 0)
                {
                    /* XXXXX Must fix, zero bytes is a NOP */
                    if (   errno == EAGAIN
                        || errno == EWOULDBLOCK
                        || errno == EINPROGRESS
                        || errno == ENOTCONN)
                        CONTINUE(tcp);

                    /* else failed */
                    so->so_state = SS_NOFDREF;
                }
                /* else so->so_state &= ~SS_ISFCONNECTING; */
#endif

                /*
                 * Continue tcp_input
                 */
                TCP_INPUT(pData, (struct mbuf *)NULL, sizeof(struct ip), so);
                /* continue; */
            }
            else
                SOWRITE(ret, pData, so);
            /*
             * XXX If we wrote something (a lot), there could be the need
             * for a window update. In the worst case, the remote will send
             * a window probe to get things going again.
             */
        }

        /*
         * Probe a still-connecting, non-blocking socket
         * to check if it's still alive
         */
#ifdef PROBE_CONN
        if (so->so_state & SS_ISFCONNECTING)
        {
            ret = recv(so->s, (char *)&ret, 0, 0);

            if (ret < 0)
            {
                /* XXX */
                if (   errno == EAGAIN
                    || errno == EWOULDBLOCK
                    || errno == EINPROGRESS
                    || errno == ENOTCONN)
                {
                    CONTINUE(tcp); /* Still connecting, continue */
                }

                /* else failed */
                so->so_state = SS_NOFDREF;

                /* tcp_input will take care of it */
            }
            else
            {
                ret = send(so->s, &ret, 0, 0);
                if (ret < 0)
                {
                    /* XXX */
                    if (   errno == EAGAIN
                        || errno == EWOULDBLOCK
                        || errno == EINPROGRESS
                        || errno == ENOTCONN)
                    {
                        CONTINUE(tcp);
                    }
                    /* else failed */
                    so->so_state = SS_NOFDREF;
                }
                else
                    so->so_state &= ~SS_ISFCONNECTING;

            }
            TCP_INPUT((struct mbuf *)NULL, sizeof(struct ip),so);
        } /* SS_ISFCONNECTING */
#endif
#ifndef RT_OS_WINDOWS
        if (   UNIX_CHECK_FD_SET(so, NetworkEvents, rdhup)
            || UNIX_CHECK_FD_SET(so, NetworkEvents, rderr))
        {
            int err;
            int inq, outq;
            int status;
            socklen_t optlen = sizeof(int);
            inq = outq = 0;
            status = getsockopt(so->s, SOL_SOCKET, SO_ERROR, &err, &optlen);
            if (status != 0)
                Log(("NAT: can't get error status from %R[natsock]\n", so));
#ifndef RT_OS_SOLARIS
            status = ioctl(so->s, FIONREAD, &inq); /* tcp(7) recommends SIOCINQ which is Linux specific */
            if (status != 0 || status != EINVAL)
            {
                /* EINVAL returned if socket in listen state tcp(7)*/
                Log(("NAT: can't get depth of IN queue status from %R[natsock]\n", so));
            }
            status = ioctl(so->s, TIOCOUTQ, &outq); /* SIOCOUTQ see previous comment */
            if (status != 0)
                Log(("NAT: can't get depth of OUT queue from %R[natsock]\n", so));
#else
                /*
                 * Solaris has bit different ioctl commands and its handlings
                 * hint: streamio(7) I_NREAD
                 */
#endif
            if (   so->so_state & SS_ISFCONNECTING
                || UNIX_CHECK_FD_SET(so, NetworkEvents, readfds))
            {
                /**
                 * Check if we need here take care about gracefull connection
                 * @todo try with proxy server
                 */
                if (UNIX_CHECK_FD_SET(so, NetworkEvents, readfds))
                {
                    /*
                     * Never meet inq != 0 or outq != 0, anyway let it stay for a while
                     * in case it happens we'll able to detect it.
                     * Give TCP/IP stack wait or expire the socket.
                     */
                    Log(("NAT: %R[natsock] err(%d:%s) s(in:%d,out:%d)happens on read I/O, "
                        "other side close connection \n", so, err, strerror(err), inq, outq));
                    CONTINUE(tcp);
                }
                goto tcp_input_close;
            }
            if (   !UNIX_CHECK_FD_SET(so, NetworkEvents, readfds)
                && !UNIX_CHECK_FD_SET(so, NetworkEvents, writefds)
                && !UNIX_CHECK_FD_SET(so, NetworkEvents, xfds))
            {
                Log(("NAT: system expires the socket %R[natsock] err(%d:%s) s(in:%d,out:%d) happens on non-I/O. ",
                        so, err, strerror(err), inq, outq));
                goto tcp_input_close;
            }
            Log(("NAT: %R[natsock] we've met(%d:%s) s(in:%d, out:%d) unhandled combination hup (%d) "
                "rederr(%d) on (r:%d, w:%d, x:%d)\n",
                    so, err, strerror(err),
                    inq, outq,
                    UNIX_CHECK_FD_SET(so, ign, rdhup),
                    UNIX_CHECK_FD_SET(so, ign, rderr),
                    UNIX_CHECK_FD_SET(so, ign, readfds),
                    UNIX_CHECK_FD_SET(so, ign, writefds),
                    UNIX_CHECK_FD_SET(so, ign, xfds)));
            /*
             * Give OS's TCP/IP stack a chance to resolve an issue or expire the socket.
             */
            CONTINUE(tcp);
tcp_input_close:
            so->so_state = SS_NOFDREF; /*cause connection valid tcp connection termination and socket closing */
            TCP_INPUT(pData, (struct mbuf *)NULL, sizeof(struct ip), so);
            CONTINUE(tcp);
        }
#endif
        LOOP_LABEL(tcp, so, so_next);
    }

    /*
     * Now UDP sockets.
     * Incoming packets are sent straight away, they're not buffered.
     * Incoming UDP data isn't buffered either.
     */
     QSOCKET_FOREACH(so, so_next, udp)
     /* { */
#ifdef VBOX_WITH_SLIRP_MT
        if (   so->so_state & SS_NOFDREF
            && so->so_deleted == 1)
        {
            struct socket *son, *sop = NULL;
            QSOCKET_LOCK(udb);
            if (so->so_next != NULL)
            {
                if (so->so_next != &udb)
                    SOCKET_LOCK(so->so_next);
                son = so->so_next;
            }
            if (   so->so_prev != &udb
                && so->so_prev != NULL)
            {
                SOCKET_LOCK(so->so_prev);
                sop = so->so_prev;
            }
            QSOCKET_UNLOCK(udb);
            remque(pData, so);
            NSOCK_DEC();
            SOCKET_UNLOCK(so);
            SOCKET_LOCK_DESTROY(so);
            RTMemFree(so);
            so_next = son;
            if (sop != NULL)
                SOCKET_UNLOCK(sop);
            CONTINUE_NO_UNLOCK(udp);
        }
#endif
        POLL_UDP_EVENTS(rc, error, so, &NetworkEvents);

        LOG_NAT_SOCK(so, UDP, &NetworkEvents, readfds, writefds, xfds);

        if (so->s != -1 && CHECK_FD_SET(so, NetworkEvents, readfds))
        {
            SORECVFROM(pData, so);
        }
        LOOP_LABEL(udp, so, so_next);
    }

done:
#ifndef VBOX_WITH_SLIRP_MT
    /*
     * See if we can start outputting
     */
    if (if_queued && link_up)
        if_start(pData);
#endif

    STAM_PROFILE_STOP(&pData->StatPoll, a);
}

#ifndef VBOX_WITHOUT_SLIRP_CLIENT_ETHER
#define ETH_ALEN        6
#define ETH_HLEN        14

#define ARPOP_REQUEST   1               /* ARP request                  */
#define ARPOP_REPLY     2               /* ARP reply                    */

struct ethhdr
{
    unsigned char   h_dest[ETH_ALEN];           /* destination eth addr */
    unsigned char   h_source[ETH_ALEN];         /* source ether addr    */
    unsigned short  h_proto;                    /* packet type ID field */
};
AssertCompileSize(struct ethhdr, 14);
#endif

struct arphdr
{
    unsigned short  ar_hrd;             /* format of hardware address   */
    unsigned short  ar_pro;             /* format of protocol address   */
    unsigned char   ar_hln;             /* length of hardware address   */
    unsigned char   ar_pln;             /* length of protocol address   */
    unsigned short  ar_op;              /* ARP opcode (command)         */

    /*
     *      Ethernet looks like this : This bit is variable sized however...
     */
    unsigned char   ar_sha[ETH_ALEN];   /* sender hardware address      */
    unsigned char   ar_sip[4];          /* sender IP address            */
    unsigned char   ar_tha[ETH_ALEN];   /* target hardware address      */
    unsigned char   ar_tip[4];          /* target IP address            */
};
AssertCompileSize(struct arphdr, 28);

static void arp_input(PNATState pData, struct mbuf *m)
{
    struct ethhdr *eh;
    struct ethhdr *reh;
    struct arphdr *ah;
    struct arphdr *rah;
    int ar_op;
    struct ex_list *ex_ptr;
    uint32_t htip;
    uint32_t tip;
    struct mbuf *mr;
    eh = mtod(m, struct ethhdr *);
    ah = (struct arphdr *)&eh[1];
    htip = ntohl(*(uint32_t*)ah->ar_tip);
    tip = *(uint32_t*)ah->ar_tip;

    mr = m_get(pData);
#ifdef VBOX_WITHOUT_SLIRP_CLIENT_ETHER
    reh = mtod(mr, struct ethhdr *);
    memcpy(reh->h_source, eh->h_source, ETH_ALEN); /* XXX: if_encap will swap src and dst*/
    Log4(("NAT: arp:%R[ether]->%R[ether]\n",
        reh->h_source, reh->h_dest));
    Log4(("NAT: arp: %R[IP4]\n", &tip));
#endif
    mr->m_data += if_maxlinkhdr;
    mr->m_len = sizeof(struct arphdr);
    rah = mtod(mr, struct arphdr *);

    ar_op = ntohs(ah->ar_op);
    switch(ar_op)
    {
        case ARPOP_REQUEST:
#ifdef VBOX_WITH_NAT_SERVICE
            if (tip == special_addr.s_addr) goto arp_ok;
#endif
            if ((htip & pData->netmask) == ntohl(special_addr.s_addr))
            {
                if (   CTL_CHECK(htip, CTL_DNS)
                    || CTL_CHECK(htip, CTL_ALIAS)
                    || CTL_CHECK(htip, CTL_TFTP))
                    goto arp_ok;
                for (ex_ptr = exec_list; ex_ptr; ex_ptr = ex_ptr->ex_next)
                {
                    if ((htip & ~pData->netmask) == ex_ptr->ex_addr)
                    {
                        goto arp_ok;
                    }
                }
                return;
        arp_ok:
                rah->ar_hrd = htons(1);
                rah->ar_pro = htons(ETH_P_IP);
                rah->ar_hln = ETH_ALEN;
                rah->ar_pln = 4;
                rah->ar_op = htons(ARPOP_REPLY);
                memcpy(rah->ar_sha, special_ethaddr, ETH_ALEN);

                switch (htip & ~pData->netmask)
                {
                    case CTL_DNS:
                    case CTL_ALIAS:
                        rah->ar_sha[5] = (uint8_t)(htip & ~pData->netmask);
                        break;
                    default:;
                }

                memcpy(rah->ar_sip, ah->ar_tip, 4);
                memcpy(rah->ar_tha, ah->ar_sha, ETH_ALEN);
                memcpy(rah->ar_tip, ah->ar_sip, 4);
                if_encap(pData, ETH_P_ARP, mr);
                m_free(pData, m);
            }
            break;
        default:
            break;
    }
}

void slirp_input(PNATState pData, const uint8_t *pkt, int pkt_len)
{
    struct mbuf *m;
    int proto;
    static bool fWarnedIpv6;
    struct ethhdr *eh = (struct ethhdr*)pkt;

    Log2(("NAT: slirp_input %d\n", pkt_len));
    if (pkt_len < ETH_HLEN)
    {
        LogRel(("NAT: packet having size %d has been ingnored\n", pkt_len));
        return;
    }
    Log4(("NAT: in:%R[ether]->%R[ether]\n", &eh->h_source, &eh->h_dest));
#ifdef VBOX_WITHOUT_SLIRP_CLIENT_ETHER
    if (memcmp(eh->h_source, special_ethaddr, ETH_ALEN) == 0)
    {
        /* @todo vasily: add ether logging routine in debug.c */
        Log(("NAT: packet was addressed to other MAC\n"));
        RTMemFree((void *)pkt);
        return;
    }
#endif

    m = m_get(pData);
    if (!m)
    {
        LogRel(("NAT: can't allocate new mbuf\n"));
        return;
    }

    /* Note: we add to align the IP header */

    if (M_FREEROOM(m) < pkt_len)
       m_inc(m, pkt_len);

    m->m_len = pkt_len ;
    memcpy(m->m_data, pkt, pkt_len);

    proto = ntohs(*(uint16_t *)(pkt + 12));
    switch(proto)
    {
        case ETH_P_ARP:
            arp_input(pData, m);
            break;
        case ETH_P_IP:
            /* Update time. Important if the network is very quiet, as otherwise
             * the first outgoing connection gets an incorrect timestamp. */
            updtime(pData);
            m_adj(m, ETH_HLEN);
            ip_input(pData, m);
            break;
        case ETH_P_IPV6:
            m_free(pData, m);
            if (!fWarnedIpv6)
            {
                LogRel(("NAT: IPv6 not supported\n"));
                fWarnedIpv6 = true;
            }
            break;
        default:
            Log(("NAT: Unsupported protocol %x\n", proto));
            m_free(pData, m);
            break;
    }
    RTMemFree((void *)pkt);
}

/* output the IP packet to the ethernet device */
void if_encap(PNATState pData, uint16_t eth_proto, struct mbuf *m)
{
    struct ethhdr *eh;
    uint8_t *buf = RTMemAlloc(1600);
    STAM_PROFILE_START(&pData->StatIF_encap, a);

    m->m_data -= if_maxlinkhdr;
    m->m_len += ETH_HLEN;
    eh = mtod(m, struct ethhdr *);

    if(MBUF_HEAD(m) != m->m_data)
    {
        LogRel(("NAT: ethernet detects corruption of the packet"));
        AssertMsgFailed(("!!Ethernet frame corrupted!!"));
    }

#ifndef VBOX_WITHOUT_SLIRP_CLIENT_ETHER
    memcpy(eh->h_dest, client_ethaddr, ETH_ALEN);
    memcpy(eh->h_source, special_ethaddr, ETH_ALEN - 1);
    /* XXX: not correct */
    eh->h_source[5] = CTL_ALIAS;
#else
    if (memcmp(eh->h_source, special_ethaddr, ETH_ALEN) != 0)
    {
        memcpy(eh->h_dest, eh->h_source, ETH_ALEN);
        memcpy(eh->h_source, special_ethaddr, ETH_ALEN);
        Assert(memcmp(eh->h_dest, special_ethaddr, ETH_ALEN) != 0);
        if (memcmp(eh->h_dest, zerro_ethaddr, ETH_ALEN) == 0)
        {
            /* don't do anything */
            goto done;
        }
    }
#endif
    eh->h_proto = htons(eth_proto);
#if 0
    slirp_output(pData->pvUser, m, mtod(m, uint8_t *), m->m_len);
#else
    memcpy(buf, mtod(m, uint8_t *), m->m_len);
    slirp_output(pData->pvUser, NULL, buf, m->m_len);
done:
    STAM_PROFILE_STOP(&pData->StatIF_encap, a);
    m_free(pData, m);
#endif
}

int slirp_redir(PNATState pData, int is_udp, struct in_addr host_addr, int host_port,
                struct in_addr guest_addr, int guest_port)
{
    struct socket *so;
#ifdef VBOX_WITH_SLIRP_ALIAS
    struct alias_link *link;
    struct libalias *lib;
    int flags;
    struct sockaddr sa;
    struct sockaddr_in *psin;
    socklen_t socketlen;
    struct in_addr alias;
    int rc;
#endif
    Log2(("NAT: set redirect %s hp:%d gp:%d\n", (is_udp?"UDP":"TCP"), host_port, guest_port));
    if (is_udp)
    {
        so = udp_listen(pData, host_addr.s_addr, htons(host_port), guest_addr.s_addr,
                        htons(guest_port), 0);
    }
    else
    {
        so = solisten(pData, host_addr.s_addr, htons(host_port), guest_addr.s_addr,
                      htons(guest_port), 0);
    }
    if (so == NULL)
    {   
        return -1;
    }
#ifndef VBOX_WITH_SLIRP_ALIAS
    Log2(("NAT: redirecting socket %R[natsock]\n", so));
    return (so != NULL ? 0 : -1);
#else

    psin = (struct sockaddr_in *)&sa;
    psin->sin_family = AF_INET;
    psin->sin_port = 0;
    psin->sin_addr.s_addr = INADDR_ANY;
    socketlen = sizeof(struct sockaddr);

    rc = getsockname(so->s, &sa, &socketlen);
    if (rc < 0 || sa.sa_family != AF_INET)
    {
        Log(("NAT: can't get socket's name\n"));
        return 1;
    }

    psin = (struct sockaddr_in *)&sa;

#if 1
    lib = LibAliasInit(pData, NULL);
    flags = LibAliasSetMode(lib, 0, 0);
    flags |= PKT_ALIAS_LOG; /* set logging */
    flags |= PKT_ALIAS_REVERSE; /* set logging */
    flags = LibAliasSetMode(lib, flags, ~0);
#else
    lib = LIST_FIRST(&instancehead);
#endif

    alias.s_addr =  htonl(ntohl(guest_addr.s_addr) | CTL_ALIAS);
    link = LibAliasRedirectPort(lib, psin->sin_addr, htons(host_port),
        alias, htons(guest_port),
        special_addr,  -1, /* not very clear for now*/
        (is_udp ? IPPROTO_UDP : IPPROTO_TCP));
    if (link == NULL)
    {
        Log(("NAT: can't create redirect\n"));
        return 1;
    }
    so->so_la = lib;

    return 0;
#endif
}

int slirp_add_exec(PNATState pData, int do_pty, const char *args, int addr_low_byte,
                  int guest_port)
{
    return add_exec(&exec_list, do_pty, (char *)args,
                    addr_low_byte, htons(guest_port));
}

void slirp_set_ethaddr(PNATState pData, const uint8_t *ethaddr)
{
#ifndef VBOX_WITHOUT_SLIRP_CLIENT_ETHER
    memcpy(client_ethaddr, ethaddr, ETH_ALEN);
#endif
}

#if defined(RT_OS_WINDOWS)
HANDLE *slirp_get_events(PNATState pData)
{
        return pData->phEvents;
}
void slirp_register_external_event(PNATState pData, HANDLE hEvent, int index)
{
        pData->phEvents[index] = hEvent;
}
#endif

unsigned int slirp_get_timeout_ms(PNATState pData)
{
    if (link_up)
    {
        if (time_fasttimo)
            return 2;
        if (do_slowtimo)
            return 500; /* see PR_SLOWHZ */
    }
    return 0;
}

#ifndef RT_OS_WINDOWS
int slirp_get_nsock(PNATState pData)
{
    return pData->nsock;
}
#endif

/*
 * this function called from NAT thread
 */
void slirp_post_sent(PNATState pData, void *pvArg)
{
    struct socket *so = 0;
    struct tcpcb *tp = 0;
    struct mbuf *m = (struct mbuf *)pvArg;
    m_free(pData, m);
}
#ifdef VBOX_WITH_SLIRP_MT
void slirp_process_queue(PNATState pData)
{
     RTReqProcess(pData->pReqQueue, RT_INDEFINITE_WAIT);
}
void *slirp_get_queue(PNATState pData)
{
    return pData->pReqQueue;
}
#endif

uint16_t slirp_get_service(int proto, uint16_t dport, uint16_t sport)
{
    uint16_t hdport, hsport, service;
    hdport = ntohs(dport);
    hsport = ntohs(sport);
    Log2(("proto: %d, dport: %d sport: %d\n", proto, hdport, hsport));
    service = 0;
#if 0
    /* Always return 0 here */
    switch (hdport)
    {
        case 500:
                if (hsport != 500) /* vpnc by default try operate in src:500/dst:500 mode*/
                /* Not sure why this make Cisco VPN client's connection more stable,
                 * at least on some servers
                 */
                    service = sport;
        break;
    }
#endif
    Log2(("service : %d\n", service));
    return htons(service);
}

void slirp_set_dhcp_TFTP_prefix(PNATState pData, const char *tftpPrefix)
{
    Log2(("tftp_prefix:%s\n", tftpPrefix));
    tftp_prefix = tftpPrefix;
}

void slirp_set_dhcp_TFTP_bootfile(PNATState pData, const char *bootFile)
{
    Log2(("bootFile:%s\n", bootFile));
    bootp_filename = bootFile;
}

void slirp_set_dhcp_next_server(PNATState pData, const char *next_server)
{
    Log2(("next_server:%s\n", next_server));
    if (next_server == NULL)
        pData->tftp_server.s_addr = htonl(ntohl(special_addr.s_addr) | CTL_TFTP);
    else
        inet_aton(next_server, &pData->tftp_server);
}

int slirp_set_binding_address(PNATState pData, char *addr)
{
    if (addr == NULL || (inet_aton(addr, &pData->bindIP) == 0))
    {
        pData->bindIP.s_addr = INADDR_ANY;
        return 1;
    }
    return 0;
}

void slirp_set_dhcp_dns_proxy(PNATState pData, bool fDNSProxy)
{
    Log2(("NAT: DNS proxy switched %s\n", (fDNSProxy ? "on" : "off")));
    pData->use_dns_proxy = fDNSProxy;
}

#define CHECK_ARG(name, val, lim_min, lim_max)                                  \
do {                                                                            \
    if ((val) < (lim_min) || (val) > (lim_max))                                 \
    {                                                                           \
        LogRel(("NAT: (" #name ":%d) has been ignored, "                        \
            "because out of range (%d, %d)\n", (val), (lim_min), (lim_max)));   \
        return;                                                                 \
    }                                                                           \
    else                                                                        \
    {                                                                           \
        LogRel(("NAT: (" #name ":%d)\n", (val)));                               \
    }                                                                           \
} while (0)

/* don't allow user set less 8kB and more than 1M values */
#define _8K_1M_CHECK_ARG(name, val) CHECK_ARG(name, (val), 8, 1024)
void slirp_set_rcvbuf(PNATState pData, int kilobytes)
{
    _8K_1M_CHECK_ARG("SOCKET_RCVBUF", kilobytes);
    pData->socket_rcv = kilobytes;
}
void slirp_set_sndbuf(PNATState pData, int kilobytes)
{
    _8K_1M_CHECK_ARG("SOCKET_SNDBUF", kilobytes);
    pData->socket_snd = kilobytes * _1K;
}
void slirp_set_tcp_rcvspace(PNATState pData, int kilobytes)
{
    _8K_1M_CHECK_ARG("TCP_RCVSPACE", kilobytes);
    tcp_rcvspace = kilobytes * _1K;
}
void slirp_set_tcp_sndspace(PNATState pData, int kilobytes)
{
    _8K_1M_CHECK_ARG("TCP_SNDSPACE", kilobytes);
    tcp_sndspace = kilobytes * _1K;
}

