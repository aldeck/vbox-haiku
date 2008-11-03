/*
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#include <slirp.h>


#define ifs_init(ifm) ((ifm)->ifs_next = (ifm)->ifs_prev = (ifm))

static void ifs_insque(struct mbuf *ifm, struct mbuf *ifmhead)
{
	ifm->ifs_next = ifmhead->ifs_next;
	ifmhead->ifs_next = ifm;
	ifm->ifs_prev = ifmhead;
	ifm->ifs_next->ifs_prev = ifm;
}

static void ifs_remque(struct mbuf *ifm)
{
	ifm->ifs_prev->ifs_next = ifm->ifs_next;
	ifm->ifs_next->ifs_prev = ifm->ifs_prev;
}

void
if_init(PNATState pData)
{
#if 0
	/*
	 * Set if_maxlinkhdr to 48 because it's 40 bytes for TCP/IP,
	 * and 8 bytes for PPP, but need to have it on an 8byte boundary
	 */
#ifdef USE_PPP
	if_maxlinkhdr = 48;
#else
	if_maxlinkhdr = 40;
#endif
#else
        /* 2 for alignment, 14 for ethernet, 40 for TCP/IP */
        if_maxlinkhdr = 2 + 14 + 40;
#endif
        if_queued = 0;
        if_thresh = 10;
	if_mtu = 1500;
	if_mru = 1500;
	if_comp = IF_AUTOCOMP;
	if_fastq.ifq_next = if_fastq.ifq_prev = &if_fastq;

        VBOX_SLIRP_LOCK_CREATE(&pData->if_fastq_mutex);
        VBOX_SLIRP_LOCK_CREATE(&if_fastq.m_mutex);

	if_batchq.ifq_next = if_batchq.ifq_prev = &if_batchq;

        VBOX_SLIRP_LOCK_CREATE(&pData->if_batchq_mutex);
        VBOX_SLIRP_LOCK_CREATE(&if_batchq.m_mutex);

        /*	sl_compress_init(&comp_s); */
	next_m = &if_batchq;
}

#if 0
/*
 * This shouldn't be needed since the modem is blocking and
 * we don't expect any signals, but what the hell..
 */
inline int
writen(fd, bptr, n)
	int fd;
	char *bptr;
	int n;
{
	int ret;
	int total;

	/* This should succeed most of the time */
	ret = send(fd, bptr, n,0);
	if (ret == n || ret <= 0)
	   return ret;

	/* Didn't write everything, go into the loop */
	total = ret;
	while (n > total) {
		ret = send(fd, bptr+total, n-total,0);
		if (ret <= 0)
		   return ret;
		total += ret;
	}
	return total;
}

/*
 * if_input - read() the tty, do "top level" processing (ie: check for any escapes),
 * and pass onto (*ttyp->if_input)
 *
 * XXXXX Any zeros arriving by themselves are NOT placed into the arriving packet.
 */
#define INBUFF_SIZE 2048 /* XXX */
void
if_input(ttyp)
	struct ttys *ttyp;
{
	u_char if_inbuff[INBUFF_SIZE];
	int if_n;

	DEBUG_CALL("if_input");
	DEBUG_ARG("ttyp = %lx", (long)ttyp);

	if_n = recv(ttyp->fd, (char *)if_inbuff, INBUFF_SIZE,0);

	DEBUG_MISC((dfd, " read %d bytes\n", if_n));

	if (if_n <= 0) {
		if (if_n == 0 || (errno != EINTR && errno != EAGAIN)) {
			if (ttyp->up)
			   link_up--;
			tty_detached(ttyp, 0);
		}
		return;
	}
	if (if_n == 1) {
		if (*if_inbuff == '0') {
			ttyp->ones = 0;
			if (++ttyp->zeros >= 5)
			   slirp_exit(0);
			return;
		}
		if (*if_inbuff == '1') {
			ttyp->zeros = 0;
			if (++ttyp->ones >= 5)
			   tty_detached(ttyp, 0);
			return;
		}
	}
	ttyp->ones = ttyp->zeros = 0;

	(*ttyp->if_input)(ttyp, if_inbuff, if_n);
}
#endif

/*
 * if_output: Queue packet into an output queue.
 * There are 2 output queue's, if_fastq and if_batchq.
 * Each output queue is a doubly linked list of double linked lists
 * of mbufs, each list belonging to one "session" (socket).  This
 * way, we can output packets fairly by sending one packet from each
 * session, instead of all the packets from one session, then all packets
 * from the next session, etc.  Packets on the if_fastq get absolute
 * priority, but if one session hogs the link, it gets "downgraded"
 * to the batchq until it runs out of packets, then it'll return
 * to the fastq (eg. if the user does an ls -alR in a telnet session,
 * it'll temporarily get downgraded to the batchq)
 */
void
if_output(PNATState pData, struct socket *so, struct mbuf *ifm)
{
	struct mbuf *ifq;
#ifdef VBOX_WITH_SYNC_SLIRP
	struct mbuf *ifqprev;
#endif
	int on_fastq = 1;

	DEBUG_CALL("if_output");
	DEBUG_ARG("so = %lx", (long)so);
	DEBUG_ARG("ifm = %lx", (long)ifm);


	/*
	 * First remove the mbuf from m_usedlist,
	 * since we're gonna use m_next and m_prev ourselves
	 * XXX Shouldn't need this, gotta change dtom() etc.
	 */
        VBOX_SLIRP_LOCK(pData->m_usedlist_mutex);
        VBOX_SLIRP_LOCK(ifm->m_mutex);

	if (ifm->m_flags & M_USEDLIST) {
		remque(pData, ifm);
		ifm->m_flags &= ~M_USEDLIST;
	}
        VBOX_SLIRP_UNLOCK(pData->m_usedlist_mutex);

	/*
	 * See if there's already a batchq list for this session.
	 * This can include an interactive session, which should go on fastq,
	 * but gets too greedy... hence it'll be downgraded from fastq to batchq.
	 * We mustn't put this packet back on the fastq (or we'll send it out of order)
	 * XXX add cache here?
	 */
        VBOX_SLIRP_LOCK(pData->if_batchq_mutex);
#ifndef VBOX_WITH_SYNC_SLIRP
	for (ifq = if_batchq.ifq_prev; ifq != &if_batchq; ifq = ifq->ifq_prev) {
#else
        ifq = if_batchq.ifq_prev;
        while(1){
            if (ifq == &if_batchq) {
                VBOX_SLIRP_UNLOCK(pData->if_batchq_mutex);
                break;
            }
            ifqprev = ifq->ifq_prev;
#endif
            VBOX_SLIRP_LOCK(ifq->m_mutex);
            VBOX_SLIRP_UNLOCK(pData->if_batchq_mutex);
	    if (so == ifq->ifq_so) {
			/* A match! */
			ifm->ifq_so = so;
			ifs_insque(ifm, ifq->ifs_prev);
			goto diddit;
    	    }
            VBOX_SLIRP_UNLOCK(ifq->m_mutex);
            VBOX_SLIRP_LOCK(pData->if_batchq_mutex);
#ifdef VBOX_WITH_SYNC_SLIRP
            ifq = ifqprev;
#endif
	}

	/* No match, check which queue to put it on */
	if (so && (so->so_iptos & IPTOS_LOWDELAY)) {
                VBOX_SLIRP_LOCK(pData->if_fastq_mutex);
		ifq = if_fastq.ifq_prev;
                VBOX_SLIRP_LOCK(ifq->m_mutex);
                VBOX_SLIRP_UNLOCK(pData->if_fastq_mutex);
		on_fastq = 1;
		/*
		 * Check if this packet is a part of the last
		 * packet's session
		 */
		if (ifq->ifq_so == so) {
			ifm->ifq_so = so;
			ifs_insque(ifm, ifq->ifs_prev);
			goto diddit;
		}
	}
        else {
                VBOX_SLIRP_LOCK(pData->if_batchq_mutex);
		ifq = if_batchq.ifq_prev;
                if (ifq != &if_batchq) {
                    VBOX_SLIRP_LOCK(ifq->m_mutex);
                }
                VBOX_SLIRP_UNLOCK(pData->if_batchq_mutex);
        }

	/* Create a new doubly linked list for this session */
	ifm->ifq_so = so;
	ifs_init(ifm);
	insque(pData, ifm, ifq);

diddit:
        VBOX_SLIRP_LOCK(pData->if_queued_mutex);

	++if_queued;

        VBOX_SLIRP_UNLOCK(pData->if_queued_mutex);

	if (so) {
		/* Update *_queued */
                VBOX_SLIRP_LOCK(so->so_mutex);
		so->so_queued++;
		so->so_nqueued++;
		/*
		 * Check if the interactive session should be downgraded to
		 * the batchq.  A session is downgraded if it has queued 6
		 * packets without pausing, and at least 3 of those packets
		 * have been sent over the link
		 * (XXX These are arbitrary numbers, probably not optimal..)
		 */
		if (on_fastq && ((so->so_nqueued >= 6) &&
				 (so->so_nqueued - so->so_queued) >= 3)) {

                        VBOX_SLIRP_LOCK(pData->if_fastq_mutex);
			/* Remove from current queue... */
			remque(pData, ifm->ifs_next);

                        VBOX_SLIRP_UNLOCK(pData->if_fastq_mutex);
                        VBOX_SLIRP_LOCK(pData->if_batchq_mutex);

			/* ...And insert in the new.  That'll teach ya! */
			insque(pData, ifm->ifs_next, &if_batchq);
                        VBOX_SLIRP_UNLOCK(pData->if_batchq_mutex);
		}
                VBOX_SLIRP_UNLOCK(so->so_mutex);
	}
        VBOX_SLIRP_UNLOCK(ifq->m_mutex);
        VBOX_SLIRP_UNLOCK(ifm->m_mutex);

#ifndef FULL_BOLT
	/*
	 * This prevents us from malloc()ing too many mbufs
	 */
	if (link_up) {
		/* if_start will check towrite */
		if_start(pData);
	}
#endif
}

/*
 * Send a packet
 * We choose a packet based on it's position in the output queues;
 * If there are packets on the fastq, they are sent FIFO, before
 * everything else.  Otherwise we choose the first packet from the
 * batchq and send it.  the next packet chosen will be from the session
 * after this one, then the session after that one, and so on..  So,
 * for example, if there are 3 ftp session's fighting for bandwidth,
 * one packet will be sent from the first session, then one packet
 * from the second session, then one packet from the third, then back
 * to the first, etc. etc.
 */
void
if_start(PNATState pData)
{
	struct mbuf *ifm, *ifqt;
#ifdef VBOX_WITH_SYNC_SLIRP
        int on_fast = 0; /*required for correctness */
	struct mbuf *ifm_prev;
#endif

	DEBUG_CALL("if_start");

        VBOX_SLIRP_LOCK(pData->if_queued_mutex);
	if (if_queued <= 0) {
           VBOX_SLIRP_UNLOCK(pData->if_queued_mutex);
	   return; /* Nothing to do */
        }

 again:
        VBOX_SLIRP_UNLOCK(pData->if_queued_mutex);

        /* check if we can really output */
        if (!slirp_can_output(pData->pvUser))
            return;

	/*
	 * See which queue to get next packet from
	 * If there's something in the fastq, select it immediately
	 */
        VBOX_SLIRP_LOCK(pData->if_fastq_mutex);
	if (if_fastq.ifq_next != &if_fastq) {
		ifm = if_fastq.ifq_next;
#ifdef VBOX_WITH_SYNC_SLIRP
                on_fast = 1;
#endif
            VBOX_SLIRP_LOCK(ifm->m_mutex);
	} else {
            VBOX_SLIRP_UNLOCK(pData->if_fastq_mutex);

            VBOX_SLIRP_LOCK(pData->if_batchq_mutex);
            VBOX_SLIRP_LOCK(pData->next_m_mutex);
		/* Nothing on fastq, see if next_m is valid */
		if (next_m != &if_batchq)
		   ifm = next_m;
		else
		   ifm = if_batchq.ifq_next;

		/* Set which packet to send on next iteration */
		next_m = ifm->ifq_next;
                VBOX_SLIRP_UNLOCK(pData->next_m_mutex);
	}
#ifdef VBOX_WITH_SYNC_SLIRP
        VBOX_SLIRP_LOCK(ifm->m_mutex);
        VBOX_SLIRP_LOCK(pData->if_queued_mutex);
        if (if_queued == 0) {
            if (on_fast) {
                VBOX_SLIRP_UNLOCK(pData->if_fastq_mutex);
            }else {
                VBOX_SLIRP_UNLOCK(pData->if_batchq_mutex);
            }
            goto done;
        }
#endif
	/* Remove it from the queue */
	ifqt = ifm->ifq_prev;
	remque(pData, ifm);

	--if_queued;
#ifdef VBOX_WITH_SYNC_SLIRP
        VBOX_SLIRP_UNLOCK(pData->if_queued_mutex);
        if (on_fast == 1) {
            VBOX_SLIRP_UNLOCK(pData->if_fastq_mutex);
        }
        else {
            VBOX_SLIRP_UNLOCK(pData->if_batchq_mutex);
        }
#endif

	/* If there are more packets for this session, re-queue them */
	if (ifm->ifs_next != /* ifm->ifs_prev != */ ifm) {
		insque(pData, ifm->ifs_next, ifqt);
		ifs_remque(ifm);
	}

	/* Update so_queued */
	if (ifm->ifq_so) {
                VBOX_SLIRP_LOCK(ifm->ifq_so->so_mutex);
		if (--ifm->ifq_so->so_queued == 0)
		   /* If there's no more queued, reset nqueued */
		   ifm->ifq_so->so_nqueued = 0;
                VBOX_SLIRP_UNLOCK(ifm->ifq_so->so_mutex);
	}

	/* Encapsulate the packet for sending */
        if_encap(pData, (const uint8_t *)ifm->m_data, ifm->m_len);

        m_free(pData, ifm);

        if (ifm != NULL)VBOX_SLIRP_UNLOCK(ifm->m_mutex);
            VBOX_SLIRP_LOCK(pData->if_queued_mutex);
        /*We release if_queued_mutex after again label and before return*/

	if (if_queued > 0)
	   goto again;
        done:
        VBOX_SLIRP_UNLOCK(pData->if_queued_mutex);
}
