/* $Id$ */
/** @file
 * NAT - mbuf handling (declarations/defines).
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

/*
 * This code is based on:
 *
 * Copyright (c) 1982, 1986, 1988, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)mbuf.h      8.3 (Berkeley) 1/21/94
 * mbuf.h,v 1.9 1994/11/14 13:54:20 bde Exp
 */

#ifndef _MBUF_H_
#define _MBUF_H_

#ifndef VBOX_WITH_SLIRP_BSD_MBUF
/* #define M_BUF_DEBUG */

#define m_freem m_free


#define MINCSIZE 4096   /* Amount to increase mbuf if too small */

/*
 * Macros for type conversion
 * mtod(m,t) -  convert mbuf pointer to data pointer of correct type
 * dtom(x) -    convert data pointer within mbuf to mbuf pointer (XXX)
 */
#define mtod(m,t)       ((t)(m)->m_data)
/* #define      dtom(x)         ((struct mbuf *)((int)(x) & ~(M_SIZE-1))) */

/* XXX About mbufs for slirp:
 * Only one mbuf is ever used in a chain, for each "cell" of data.
 * m_nextpkt points to the next packet, if fragmented.
 * If the data is too large, the M_EXT is used, and a larger block
 * is alloced.  Therefore, m_free[m] must check for M_EXT and if set
 * free the m_ext.  This is inefficient memory-wise, but who cares.
 */

/* XXX should union some of these! */
/* header at beginning of each mbuf: */
struct m_hdr
{
    struct  mbuf *mh_next;     /* Linked list of mbufs */
    struct  mbuf *mh_prev;
    struct  mbuf *mh_nextpkt;  /* Next packet in queue/record */
    struct  mbuf *mh_prevpkt;  /* Flags aren't used in the output queue */
    int     mh_flags;          /* Misc flags */

    int     mh_size;           /* Size of data */
    struct  socket *mh_so;

    caddr_t mh_data;           /* Location of data */
    int     mh_len;            /* Amount of data in this mbuf */
    struct libalias *mh_la;     /*Real freebsd store hocksin similar way*/
#ifdef M_BUF_DEBUG
    int mh_id;
    char *mh_allocation_at_file;
    int mh_allocation_at_line;
#endif
};

/*
 * How much room is in the mbuf, from m_data to the end of the mbuf
 */
#define M_ROOM(m) ((m->m_flags & M_EXT)? \
                        (((m)->m_ext + (m)->m_size) - (m)->m_data) \
                   : \
                        (((m)->m_dat + (m)->m_size) - (m)->m_data))

/*
 * How much free room there is
 */
#define M_FREEROOM(m) (M_ROOM(m) - (m)->m_len)
#define M_TRAILINGSPACE M_FREEROOM

struct mbuf
{
    struct  m_hdr m_hdr;
    union M_dat
    {
        char    m_dat_[1]; /* ANSI don't like 0 sized arrays */
        char    *m_ext_;
    } M_dat;
};

#define m_next          m_hdr.mh_next
#define m_prev          m_hdr.mh_prev
#define m_nextpkt       m_hdr.mh_nextpkt
#define m_prevpkt       m_hdr.mh_prevpkt
#define m_flags         m_hdr.mh_flags
#define m_len           m_hdr.mh_len
#define m_data          m_hdr.mh_data
#define m_size          m_hdr.mh_size
#define m_dat           M_dat.m_dat_
#define m_ext           M_dat.m_ext_
#define m_so            m_hdr.mh_so
#define m_la            m_hdr.mh_la

#define ifq_prev m_prev
#define ifq_next m_next
#define ifs_prev m_prevpkt
#define ifs_next m_nextpkt
#define ifq_so m_so

#define M_EXT                   0x01    /* m_ext points to more (malloced) data */
#define M_FREELIST              0x02    /* mbuf is on free list */
#define M_USEDLIST              0x04    /* XXX mbuf is on used list (for dtom()) */
#define M_DOFREE                0x08    /* when m_free is called on the mbuf, free()
                                         * it rather than putting it on the free list */
#define M_FRAG                0x0800    /* packet is a fragment of a larger packet */
#define M_FIRSTFRAG           0x1000    /* paket is first fragment */
#define M_LASTFRAG            0x2000    /* paket is last fragment */

extern int mbuf_alloced;
extern struct mbuf m_freelist, m_usedlist;
extern int mbuf_max;

void m_init (PNATState);
void m_fini(PNATState pData);
void msize_init (PNATState);
struct mbuf * m_get (PNATState);
void m_free (PNATState, struct mbuf *);
void m_cat (PNATState, register struct mbuf *, register struct mbuf *);
void m_inc (struct mbuf *, int);
void m_adj (struct mbuf *, int);
int m_copy (struct mbuf *, struct mbuf *, int, int);
struct mbuf * dtom (PNATState, void *);

/*
 * this macro should be used for validation and copying of Ethernet header where it really requred
 */
#define MBUF_HEAD(m) ((caddr_t)(((m)->m_flags & M_EXT) ? (m)->m_ext : (m)->m_dat))

#define MBUF_IP_HEADER(m) (caddr_t)(MBUF_HEAD(m) + if_maxlinkhdr)

#else /* VBOX_WITH_SLIRP_BSD_MBUF */
# include "bsd/sys/mbuf.h"
#endif /* VBOX_WITH_SLIRP_BSD_MBUF */

#endif /* _MBUF_H_ */

#if defined(M_BUF_DEBUG) && !defined(RT_OS_WINDOWS)
# define m_get(x)                                                                                               \
({                                                                                                              \
    struct mbuf *mdb = m_get((x));                                                                              \
    if(mdb)                                                                                                     \
    {                                                                                                           \
        mdb->m_hdr.mh_allocation_at_file = __FILE__;                                                            \
        mdb->m_hdr.mh_allocation_at_line = __LINE__;                                                            \
    }                                                                                                           \
    LogRel(("NAT:m(id:%d, ptr: %p) allocated at: %s:%d\n", (mdb?mdb->m_hdr.mh_id:-1), mdb, __FILE__, __LINE__));\
    mdb;                                                                                                        \
})

# define m_free(x, m)                                                                               \
({                                                                                                  \
    (m)->m_hdr.mh_allocation_at_file = NULL;                                                        \
    (m)->m_hdr.mh_allocation_at_line = 0;                                                           \
    LogRel(("NAT:m(id:%d, ptr: %p) freed at: %s:%d\n", (m)->m_hdr.mh_id, (m), __FILE__, __LINE__)); \
    m_free((x),(m));                                                                                \
})
#endif
