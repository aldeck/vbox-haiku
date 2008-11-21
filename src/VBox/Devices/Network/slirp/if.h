/*
 * Copyright (c) 1995 Danny Gasparovski.
 * 
 * Please read the file COPYRIGHT for the 
 * terms and conditions of the copyright.
 */

#ifndef _IF_H_
#define _IF_H_

#define IF_COMPRESS     0x01    /* We want compression */
#define IF_NOCOMPRESS   0x02    /* Do not do compression */
#define IF_AUTOCOMP     0x04    /* Autodetect (default) */
#define IF_NOCIDCOMP    0x08    /* CID compression */

/* Needed for FreeBSD */
#undef if_mtu
extern int      if_mtu;
extern int      if_mru; /* MTU and MRU */
extern int      if_comp;        /* Flags for compression */
extern int      if_maxlinkhdr;
extern int      if_queued;      /* Number of packets queued so far */
extern int      if_thresh;      /* Number of packets queued before we start sending
                                 * (to prevent allocing too many mbufs) */

extern  struct mbuf if_fastq;                  /* fast queue (for interactive data) */
extern  struct mbuf if_batchq;                 /* queue for non-interactive data */
extern  struct mbuf *next_m;

#define ifs_init(ifm) ((ifm)->ifs_next = (ifm)->ifs_prev = (ifm))

#endif
