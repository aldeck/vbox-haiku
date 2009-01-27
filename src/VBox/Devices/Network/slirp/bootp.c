/*
 * QEMU BOOTP/DHCP server
 *
 * Copyright (c) 2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <slirp.h>

/* XXX: only DHCP is supported */

static const uint8_t rfc1533_cookie[] = { RFC1533_COOKIE };

static BOOTPClient *get_new_addr(PNATState pData, struct in_addr *paddr)
{
    int i;

    for(i = 0; i < NB_ADDR; i++)
    {
        if (!bootp_clients[i].allocated)
        {
            BOOTPClient *bc;

            bc = &bootp_clients[i];
            bc->allocated = 1;
            paddr->s_addr = htonl(ntohl(special_addr.s_addr) | (i + START_ADDR));
            return bc;
        }
    }
    return NULL;
}

static int release_addr(PNATState pData, struct in_addr *paddr)
{
    unsigned i;

    i = ntohl(paddr->s_addr) - START_ADDR - ntohl(special_addr.s_addr);
    if (i >= NB_ADDR)
        return 0;

    memset(bootp_clients[i].macaddr, '\0', 6);
    bootp_clients[i].allocated = 0;
    return 1;
}

static BOOTPClient *find_addr(PNATState pData, struct in_addr *paddr, const uint8_t *macaddr)
{
    int i;

    for(i = 0; i < NB_ADDR; i++)
    {
        if (!memcmp(macaddr, bootp_clients[i].macaddr, 6))
        {
            BOOTPClient *bc;

            bc = &bootp_clients[i];
            bc->allocated = 1;
            paddr->s_addr = htonl(ntohl(special_addr.s_addr) | (i + START_ADDR));
            return bc;
        }
    }
    return NULL;
}

static void dhcp_decode(const uint8_t *buf, int size,
                        int *pmsg_type, struct in_addr *req_ip)
{
    const uint8_t *p, *p_end;
    int len, tag;

    *pmsg_type = 0;

    p = buf;
    p_end = buf + size;
    if (size < 5)
        return;
    if (memcmp(p, rfc1533_cookie, 4) != 0)
        return;
    p += 4;
    while (p < p_end)
    {
        tag = p[0];
        if (tag == RFC1533_PAD)
            p++;
        else if (tag == RFC1533_END)
            break;
        else
        {
            p++;
            if (p >= p_end)
                break;
            len = *p++;
            Log(("dhcp: tag=0x%02x len=%d\n", tag, len));

            switch(tag)
            {
                case RFC2132_REQ_ADDR:
                    if (len >= 4)
                        *req_ip = *(struct in_addr*)p;
                    break;
                case RFC2132_MSG_TYPE:
                    if (len >= 1)
                        *pmsg_type = p[0];
                    break;
                default:
                    break;
            }
            p += len;
        }
    }
}

static void bootp_reply(PNATState pData, struct bootp_t *bp)
{
    BOOTPClient *bc;
    struct mbuf *m;
    struct bootp_t *rbp;
    struct sockaddr_in saddr, daddr;
    struct in_addr dns_addr_dhcp;
    int dhcp_msg_type, val;
    uint8_t *q;
    struct in_addr requested_ip; /* the requested IP in DHCPREQUEST */
    int send_nak = 0;

#define FILL_BOOTP_EXT(q, tag, len, pvalue)                     \
    do {                                                        \
        struct bootp_ext *be = (struct bootp_ext *)(q);         \
        be->bpe_tag = (tag);                                    \
        be->bpe_len = (len);                                    \
        memcpy(&be[1], (pvalue), (len));                        \
        (q) = (uint8_t *)(&be[1]) + (len);                      \
    }while(0)

    /* extract exact DHCP msg type */
    requested_ip.s_addr = 0xffffffff;
    dhcp_decode(bp->bp_vend, DHCP_OPT_LEN, &dhcp_msg_type, &requested_ip);
    Log(("bootp packet op=%d msgtype=%d\n", bp->bp_op, dhcp_msg_type));

    if (dhcp_msg_type == 0)
        dhcp_msg_type = DHCPREQUEST; /* Force reply for old BOOTP clients */

    if (dhcp_msg_type == DHCPRELEASE)
    {
        int rc;
        rc = release_addr(pData, &bp->bp_ciaddr);
        LogRel(("NAT: %s %R[IP4]\n",
                rc ? "DHCP released IP address" : "Ignored DHCP release for IP address",
                &bp->bp_ciaddr));
        /* This message is not to be answered in any way. */
        return;
    }
    if (   dhcp_msg_type != DHCPDISCOVER
        && dhcp_msg_type != DHCPREQUEST)
        return;

    /* XXX: this is a hack to get the client mac address */
    memcpy(client_ethaddr, bp->bp_hwaddr, 6);

    if ((m = m_get(pData)) == NULL)
        return;
    m->m_data += if_maxlinkhdr; /*reserve ether header */
    rbp = mtod(m, struct bootp_t *);
    memset(rbp, 0, sizeof(struct bootp_t));
#ifndef VBOX_WITH_SIMPLIFIED_SLIRP_SYNC
    m->m_data += sizeof(struct udpiphdr);
#endif

    if (dhcp_msg_type == DHCPDISCOVER)
    {
        /* Do not allocate a new lease for clients that forgot that they had a lease. */
        bc = find_addr(pData, &daddr.sin_addr, bp->bp_hwaddr);
        if (!bc)
        {
    new_addr:
            bc = get_new_addr(pData, &daddr.sin_addr);
            if (!bc)
            {
                LogRel(("NAT: DHCP no IP address left\n"));
                Log(("no address left\n"));
                return;
            }
            memcpy(bc->macaddr, client_ethaddr, 6);
        }
    }
    else
    {
        bc = find_addr(pData, &daddr.sin_addr, bp->bp_hwaddr);
        if (!bc)
        {
            /* if never assigned, behaves as if it was already
               assigned (windows fix because it remembers its address) */
            goto new_addr;
        }
    }

    if (   tftp_prefix
        && RTDirExists(tftp_prefix)
        && bootp_filename)
        RTStrPrintf((char*)rbp->bp_file, sizeof(rbp->bp_file), "%s", bootp_filename);

    /* Address/port of the DHCP server. */
    saddr.sin_addr.s_addr = htonl(ntohl(special_addr.s_addr) | CTL_ALIAS);
    saddr.sin_port = htons(BOOTP_SERVER);

    daddr.sin_port = htons(BOOTP_CLIENT);

    rbp->bp_op = BOOTP_REPLY;
    rbp->bp_xid = bp->bp_xid;
    rbp->bp_htype = 1;
    rbp->bp_hlen = 6;
    memcpy(rbp->bp_hwaddr, bp->bp_hwaddr, 6);

    rbp->bp_yiaddr = daddr.sin_addr; /* Client IP address */
    rbp->bp_siaddr = pData->tftp_server; /* Next Server IP address, i.e. TFTP */

    q = rbp->bp_vend;
    memcpy(q, rfc1533_cookie, 4);
    q += 4;

    if (dhcp_msg_type == DHCPDISCOVER)
    {
        *q++ = RFC2132_MSG_TYPE;
        *q++ = 1;
        *q++ = DHCPOFFER;
    }
    else if (dhcp_msg_type == DHCPREQUEST)
    {
        *q++ = RFC2132_MSG_TYPE;
        *q++ = 1;
        if (   requested_ip.s_addr != 0xffffffff
            && requested_ip.s_addr != daddr.sin_addr.s_addr)
        {
            /* network changed */
            *q++ = DHCPNAK;
            send_nak = 1;
        }
        else
            *q++ = DHCPACK;
    }

    if (send_nak)
        LogRel(("NAT: Client requested IP address %R[IP4] -- sending NAK\n",
                &requested_ip));
    else
        LogRel(("NAT: DHCP offered IP address %R[IP4]\n",
                &daddr.sin_addr));
    if (   dhcp_msg_type == DHCPDISCOVER
        || dhcp_msg_type == DHCPREQUEST)
    {
        FILL_BOOTP_EXT(q, RFC2132_SRV_ID, 4, &saddr.sin_addr);
    }

    if (!send_nak &&
        (   dhcp_msg_type == DHCPDISCOVER
         || dhcp_msg_type == DHCPREQUEST))
    {
#ifdef VBOX_WITH_MULTI_DNS
        struct dns_entry *de = NULL;
#endif
        uint32_t lease_time = htonl(LEASE_TIME);
        uint32_t netmask = htonl(pData->netmask);

        FILL_BOOTP_EXT(q, RFC1533_NETMASK, 4, &netmask);
        FILL_BOOTP_EXT(q, RFC1533_GATEWAY, 4, &saddr.sin_addr);

#ifndef VBOX_WITH_MULTI_DNS
        dns_addr_dhcp.s_addr = htonl(ntohl(special_addr.s_addr) | CTL_DNS);
        FILL_BOOTP_EXT(q, RFC1533_DNS, 4, &dns_addr_dhcp.s_addr);
#else
        LIST_FOREACH(de, &pData->dns_list_head, de_list)
        {
            FILL_BOOTP_EXT(q, RFC1533_DNS, 4, &de->de_addr.s_addr);
        }
#endif

        FILL_BOOTP_EXT(q, RFC2132_LEASE_TIME, 4, &lease_time);

        if (*slirp_hostname)
        {
            val = (int)strlen(slirp_hostname);
            FILL_BOOTP_EXT(q, RFC1533_HOSTNAME, val, slirp_hostname);
        }

        if (pData->pszDomain && pData->fPassDomain)
        {
            val = (int)strlen(pData->pszDomain);
            FILL_BOOTP_EXT(q, RFC1533_DOMAINNAME, val, pData->pszDomain);
        }
    }
    *q++ = RFC1533_END;

    m->m_len = sizeof(struct bootp_t)
             - sizeof(struct ip)
             - sizeof(struct udphdr);
#ifdef VBOX_WITH_SIMPLIFIED_SLIRP_SYNC
    m->m_data += sizeof(struct udphdr)
             + sizeof(struct ip);
#endif
    /* Reply to the broadcast address, as some clients perform paranoid checks. */
    daddr.sin_addr.s_addr = INADDR_BROADCAST;
    udp_output2(pData, NULL, m, &saddr, &daddr, IPTOS_LOWDELAY);
}

void bootp_input(PNATState pData, struct mbuf *m)
{
    struct bootp_t *bp = mtod(m, struct bootp_t *);

    if (bp->bp_op == BOOTP_REQUEST)
        bootp_reply(pData, bp);
}
