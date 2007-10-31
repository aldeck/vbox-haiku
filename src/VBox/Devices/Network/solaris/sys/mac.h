/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_MAC_H
#define	_SYS_MAC_H

#pragma ident	"@(#)mac.h	1.21	06/12/06 SMI"

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stream.h>
#include <sys/flow.h>

#ifdef	_KERNEL
#include <sys/flow_impl.h>
#endif

/*
 * MAC Services Module
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * MAC Information (text emitted by modinfo(1m))
 */
#define	MAC_INFO	"MAC Services v1.21"

/*
 * MAC version identifier.  This is used by mac_alloc() mac_register() to
 * verify that incompatible drivers don't register.
 */
#define	MAC_VERSION	0x1

/*
 * MAC-Type version identifier.  This is used by mactype_alloc() and
 * mactype_register() to verify that incompatible MAC-Type plugins don't
 * register.
 */
#define	MACTYPE_VERSION	0x1

/*
 * Statistics
 */

#define	XCVR_UNDEFINED		0
#define	XCVR_NONE		1
#define	XCVR_10			2
#define	XCVR_100T4		3
#define	XCVR_100X		4
#define	XCVR_100T2		5
#define	XCVR_1000X		6
#define	XCVR_1000T		7

typedef enum {
	LINK_STATE_UNKNOWN = -1,
	LINK_STATE_DOWN,
	LINK_STATE_UP
} link_state_t;

typedef enum {
	LINK_DUPLEX_UNKNOWN = 0,
	LINK_DUPLEX_HALF,
	LINK_DUPLEX_FULL
} link_duplex_t;

/*
 * Maximum MAC address length
 */
#define	MAXMACADDRLEN	20

#ifdef	_KERNEL

typedef struct mac_stat_info_s {
	uint_t		msi_stat;
	char		*msi_name;
	uint_t		msi_type;	/* as defined in kstat_named_init(9F) */
	uint64_t	msi_default;
} mac_stat_info_t;

/*
 * There are three ranges of statistics values.  0 to 1 - MAC_STAT_MIN are
 * interface statistics maintained by the mac module.  MAC_STAT_MIN to 1 -
 * MACTYPE_STAT_MIN are common MAC statistics defined by the mac module and
 * maintained by each driver.  MACTYPE_STAT_MIN and above are statistics
 * defined by MAC-Type plugins and maintained by each driver.
 */
#define	MAC_STAT_MIN		1000
#define	MACTYPE_STAT_MIN	2000

#define	IS_MAC_STAT(stat)	\
	(stat >= MAC_STAT_MIN && stat < MACTYPE_STAT_MIN)
#define	IS_MACTYPE_STAT(stat)	(stat >= MACTYPE_STAT_MIN)

enum mac_driver_stat {
	/* MIB-II stats (RFC 1213 and RFC 1573) */
	MAC_STAT_IFSPEED = MAC_STAT_MIN,
	MAC_STAT_MULTIRCV,
	MAC_STAT_BRDCSTRCV,
	MAC_STAT_MULTIXMT,
	MAC_STAT_BRDCSTXMT,
	MAC_STAT_NORCVBUF,
	MAC_STAT_IERRORS,
	MAC_STAT_UNKNOWNS,
	MAC_STAT_NOXMTBUF,
	MAC_STAT_OERRORS,
	MAC_STAT_COLLISIONS,
	MAC_STAT_RBYTES,
	MAC_STAT_IPACKETS,
	MAC_STAT_OBYTES,
	MAC_STAT_OPACKETS
};

#define	MAC_NSTAT	(MAC_STAT_OPACKETS - MAC_STAT_IFSPEED + 1)

#define	MAC_STAT_ISACOUNTER(_stat) (		\
	    (_stat) == MAC_STAT_MULTIRCV ||	\
	    (_stat) == MAC_STAT_BRDCSTRCV ||	\
	    (_stat) == MAC_STAT_MULTIXMT ||	\
	    (_stat) == MAC_STAT_BRDCSTXMT ||	\
	    (_stat) == MAC_STAT_NORCVBUF ||	\
	    (_stat) == MAC_STAT_IERRORS ||	\
	    (_stat) == MAC_STAT_UNKNOWNS ||	\
	    (_stat) == MAC_STAT_NOXMTBUF ||	\
	    (_stat) == MAC_STAT_OERRORS ||	\
	    (_stat) == MAC_STAT_COLLISIONS ||	\
	    (_stat) == MAC_STAT_RBYTES ||	\
	    (_stat) == MAC_STAT_IPACKETS ||	\
	    (_stat) == MAC_STAT_OBYTES ||	\
	    (_stat) == MAC_STAT_OPACKETS)

/*
 * Immutable information. (This may not be modified after registration).
 */
typedef struct mac_info_s {
	uint_t		mi_media;
	uint_t		mi_sdu_min;
	uint_t		mi_sdu_max;
	uint_t		mi_addr_length;
	uint8_t		*mi_unicst_addr;
	uint8_t		*mi_brdcst_addr;
} mac_info_t;

/*
 * MAC layer capabilities.  These capabilities are handled by the drivers'
 * mc_capab_get() callbacks.  Some capabilities require the driver to fill
 * in a given data structure, and others are simply boolean capabilities.
 * Note that capability values must be powers of 2 so that consumers and
 * providers of this interface can keep track of which capabilities they
 * care about by keeping a bitfield of these things around somewhere.
 */
typedef enum {
	MAC_CAPAB_HCKSUM	= 0x01,	/* data is a uint32_t for the txflags */
	MAC_CAPAB_POLL		= 0x02,	/* boolean only, no data */
	MAC_CAPAB_MULTIADDRESS	= 0x04,	/* data is multiaddress_capab_t */
	MAC_CAPAB_RX_CLASSIFY	= 0x08	/* data is mac_rx_classify_t */

	/* add new capabilities here */
} mac_capab_t;

typedef int mac_addr_slot_t;

/* mma_flags values */
#define	MMAC_SLOT_USED		0x1   /* address slot used */
#define	MMAC_SLOT_UNUSED	0x2   /* free address slot */
#define	MMAC_VENDOR_ADDR	0x4   /* address returned is vendor supplied */

typedef struct mac_multi_address_s {
	mac_addr_slot_t	mma_slot;	/* slot for add/remove/get/set */
	uint_t		mma_addrlen;
	uint8_t		mma_addr[MAXMACADDRLEN];
	uint_t		mma_flags;
} mac_multi_addr_t;

typedef int	(*maddr_reserve_t)(void *, mac_multi_addr_t *);
typedef int	(*maddr_add_t)(void *, mac_multi_addr_t *);
typedef int	(*maddr_remove_t)(void *, mac_addr_slot_t);
typedef int	(*maddr_modify_t)(void *, mac_multi_addr_t *);
typedef int	(*maddr_get_t)(void *, mac_multi_addr_t *);

/* maddr_flag values */
#define	MADDR_VENDOR_ADDR	0x01	/* addr returned is vendor supplied */

/* multiple mac address: add/remove/set/get mac address */
typedef struct multiaddress_capab_s {
	int		maddr_naddr;	/* total addresses */
	int		maddr_naddrfree;	/* free address slots */
	uint_t		maddr_flag;	/* MADDR_VENDOR_ADDR bit can be set */
	/* driver entry points */
	void		*maddr_handle;	/* cookie to be used for the calls */
	maddr_reserve_t	maddr_reserve;	/* reserve a factory address */
	maddr_add_t	maddr_add;	/* add a new unicst address */
	maddr_remove_t	maddr_remove;	/* remove an added address */
	maddr_modify_t	maddr_modify;	/* modify an added address */
	maddr_get_t	maddr_get;	/* get address from specified slot */
} multiaddress_capab_t;

/*
 * MAC driver entry point types.
 */
typedef int		(*mac_getstat_t)(void *, uint_t, uint64_t *);
typedef	int		(*mac_start_t)(void *);
typedef void		(*mac_stop_t)(void *);
typedef int		(*mac_setpromisc_t)(void *, boolean_t);
typedef int		(*mac_multicst_t)(void *, boolean_t, const uint8_t *);
typedef int		(*mac_unicst_t)(void *, const uint8_t *);
typedef void		(*mac_ioctl_t)(void *, queue_t *, mblk_t *);
typedef void		(*mac_resources_t)(void *);
typedef mblk_t		*(*mac_tx_t)(void *, mblk_t *);
typedef	boolean_t	(*mac_getcapab_t)(void *, mac_capab_t, void *);

/*
 * Drivers must set all of these callbacks except for mc_resources,
 * mc_ioctl, and mc_getcapab, which are optional.  If any of these optional
 * callbacks are set, their appropriate flags must be set in mc_callbacks.
 * Any future additions to this list must also be accompanied by an
 * associated mc_callbacks flag so that the framework can grow without
 * affecting the binary compatibility of the interface.
 */
typedef struct mac_callbacks_s {
	uint_t		mc_callbacks;	/* Denotes which callbacks are set */
	mac_getstat_t	mc_getstat;	/* Get the value of a statistic */
	mac_start_t	mc_start;	/* Start the device */
	mac_stop_t	mc_stop;	/* Stop the device */
	mac_setpromisc_t mc_setpromisc;	/* Enable or disable promiscuous mode */
	mac_multicst_t	mc_multicst;	/* Enable or disable a multicast addr */
	mac_unicst_t	mc_unicst;	/* Set the unicast MAC address */
	mac_tx_t	mc_tx;		/* Transmit a packet */
	mac_resources_t	mc_resources;	/* Get the device resources */
	mac_ioctl_t	mc_ioctl;	/* Process an unknown ioctl */
	mac_getcapab_t	mc_getcapab;	/* Get capability information */
} mac_callbacks_t;


/* MAC classification capabilities */

typedef	enum {
	MAC_HW_CLASSIFIER,
	MAC_SW_CLASSIFIER,
	MAC_NO_CLASSIFIER
} mac_classify_status_t;

typedef void		(*mac_rx_func_t)(void *, mac_resource_handle_t,
			    mblk_t *);

typedef int		(*mac_rx_classify_add_t)(void *, mac_resource_handle_t,
			    flow_desc_t *, mac_rx_func_t, void *,
			    mac_resource_handle_t, mac_rule_handle_t *);

typedef int		(*mac_rx_classify_remove_t)(void *,
			    mac_resource_handle_t, mac_rule_handle_t);

typedef void		(*mac_rx_classify_update_t)(void *, mac_rx_func_t,
			    void *, mac_resource_handle_t);

typedef struct mac_rx_classify_s {
	flow_mask_t			mrc_flow_classes;
	mac_rx_classify_add_t		mrc_addrulefn;  /* adding a rule */
	mac_rx_classify_remove_t	mrc_remrulefn;	/* removing a rule */
	mac_rx_classify_update_t	mrc_updatefn;	/* Updating a ring */
} mac_rx_classify_t;

/*
 * Flags for mc_callbacks.  Requiring drivers to set the flags associated
 * with optional callbacks initialized in the structure allows the mac
 * module to add optional callbacks in the future without requiring drivers
 * to recompile.
 */
#define	MC_RESOURCES	0x001
#define	MC_IOCTL	0x002
#define	MC_GETCAPAB	0x004

typedef struct mac_register_s {
	uint_t		m_version;	/* set by mac_alloc() */
	const char	*m_type_ident;
	void		*m_driver;	/* Driver private data */
	dev_info_t	*m_dip;
	uint_t		m_instance;
	uint8_t		*m_src_addr;
	uint8_t		*m_dst_addr;
	mac_callbacks_t	*m_callbacks;
	uint_t		m_min_sdu;
	uint_t		m_max_sdu;
	void		*m_pdata;
	size_t		m_pdata_size;
} mac_register_t;

/*
 * MAC interface callback types.
 */
typedef enum {
	MAC_NOTE_LINK,
	MAC_NOTE_PROMISC,
	MAC_NOTE_UNICST,
	MAC_NOTE_TX,
	MAC_NOTE_RESOURCE,
	MAC_NOTE_DEVPROMISC,
	MAC_NOTE_FASTPATH_FLUSH,
	MAC_NOTE_VNIC,
	MAC_NNOTE	/* must be the last entry */
} mac_notify_type_t;

typedef void		(*mac_notify_t)(void *, mac_notify_type_t);
typedef void		(*mac_rx_t)(void *, mac_resource_handle_t, mblk_t *);
typedef void		(*mac_txloop_t)(void *, mblk_t *);
typedef void		(*mac_blank_t)(void *, time_t, uint_t, int);
typedef	mblk_t		*(*mac_receive_t)(void *, int);

/*
 * MAC promiscuous types
 */
typedef enum {
	MAC_PROMISC = 0x01,		/* MAC instance is promiscuous */
	MAC_DEVPROMISC = 0x02		/* Device is promiscuous */
} mac_promisc_type_t;

/*
 * MAC resource types
 */
typedef enum {
	MAC_RX_FIFO = 1
} mac_resource_type_t;

typedef struct mac_rx_fifo_s {
	mac_resource_type_t	mrf_type;	/* MAC_RX_FIFO */
	mac_blank_t		mrf_blank;
	mac_receive_t		mrf_receive;
	void			*mrf_arg;
	time_t			mrf_normal_blank_time;
	uint_t			mrf_normal_pkt_count;
	uint32_t		mrf_bw_limit;
	uint32_t		mrf_bw_guarantee;
	uint32_t		mrf_bw_shares;
	uint32_t		mrf_flow_priority;
	uint32_t		mrf_bw_control_type;

	/*
	 * The CPU this flow is to be processed on. With intrd and future
	 * things, we should know which CPU the flow needs to be processed
	 * and get a squeue assigned on that CPU.
	 */
	uint_t			mrf_cpu_id;
	flow_desc_t		mrf_flow_desc;	/* Flow desc for this ring */
} mac_rx_fifo_t;

typedef struct mac_txinfo_s {
	mac_tx_t		mt_fn;
	void			*mt_arg;
} mac_txinfo_t;

typedef union mac_resource_u {
	mac_resource_type_t	mr_type;
	mac_rx_fifo_t		mr_fifo;
} mac_resource_t;

typedef mac_resource_handle_t	(*mac_resource_add_t)(void *, mac_resource_t *);
typedef int			(*mac_resource_bind_t)(void *,
    mac_resource_handle_t, processorid_t);

typedef enum {
	MAC_ADDRTYPE_UNICAST,
	MAC_ADDRTYPE_MULTICAST,
	MAC_ADDRTYPE_BROADCAST
} mac_addrtype_t;

typedef struct mac_header_info_s {
	size_t		mhi_hdrsize;
	size_t		mhi_pktsize;
	const uint8_t	*mhi_daddr;
	const uint8_t	*mhi_saddr;
	uint32_t	mhi_origsap;
	uint32_t	mhi_bindsap;
	mac_addrtype_t	mhi_dsttype;
} mac_header_info_t;

/*
 * MAC-Type plugin interfaces
 */

typedef int		(*mtops_addr_verify_t)(const void *, void *);
typedef boolean_t	(*mtops_sap_verify_t)(uint32_t, uint32_t *, void *);
typedef mblk_t		*(*mtops_header_t)(const void *, const void *,
    uint32_t, void *, mblk_t *, size_t);
typedef int		(*mtops_header_info_t)(mblk_t *, void *,
    mac_header_info_t *);
typedef boolean_t	(*mtops_pdata_verify_t)(void *, size_t);
typedef	mblk_t		*(*mtops_header_modify_t)(mblk_t *, void *);

typedef struct mactype_ops_s {
	uint_t			mtops_ops;
	/*
	 * mtops_unicst_verify() returns 0 if the given address is a valid
	 * unicast address, or a non-zero errno otherwise.
	 */
	mtops_addr_verify_t	mtops_unicst_verify;
	/*
	 * mtops_multicst_verify() returns 0 if the given address is a
	 * valid multicast address, or a non-zero errno otherwise.  If the
	 * media doesn't support multicast, ENOTSUP should be returned (for
	 * example).
	 */
	mtops_addr_verify_t	mtops_multicst_verify;
	/*
	 * mtops_sap_verify() returns B_TRUE if the given SAP is a valid
	 * SAP value, or B_FALSE otherwise.
	 */
	mtops_sap_verify_t	mtops_sap_verify;
	/*
	 * mtops_header() is used to allocate and construct a MAC header.
	 */
	mtops_header_t		mtops_header;
	/*
	 * mtops_header_info() is used to gather information on a given MAC
	 * header.
	 */
	mtops_header_info_t	mtops_header_info;
	/*
	 * mtops_pdata_verify() is used to verify the validity of MAC
	 * plugin data.  It is called by mac_register() if the driver has
	 * supplied MAC plugin data, and also by mac_pdata_update() when
	 * drivers update the data.
	 */
	mtops_pdata_verify_t	mtops_pdata_verify;
	/*
	 * mtops_header_cook() is an optional callback that converts (or
	 * "cooks") the given raw header (as sent by a raw DLPI consumer)
	 * into one that is appropriate to send down to the MAC driver.
	 * Following the example above, an Ethernet header sent down by a
	 * DLPI consumer would be converted to whatever header the MAC
	 * driver expects.
	 */
	mtops_header_modify_t	mtops_header_cook;
	/*
	 * mtops_header_uncook() is an optional callback that does the
	 * opposite of mtops_header_cook().  It "uncooks" a given MAC
	 * header (as received from the driver) for consumption by raw DLPI
	 * consumers.  For example, for a non-Ethernet plugin that wants
	 * raw DLPI consumers to be fooled into thinking that the device
	 * provides Ethernet access, this callback would modify the given
	 * mblk_t such that the MAC header is converted to an Ethernet
	 * header.
	 */
	mtops_header_modify_t	mtops_header_uncook;
} mactype_ops_t;

/*
 * mtops_ops exists for the plugin to enumerate the optional callback
 * entrypoints it has defined.  This allows the mac module to define
 * additional plugin entrypoints in mactype_ops_t without breaking backward
 * compatibility with old plugins.
 */
#define	MTOPS_PDATA_VERIFY	0x001
#define	MTOPS_HEADER_COOK	0x002
#define	MTOPS_HEADER_UNCOOK	0x004

typedef struct mactype_register_s {
	uint_t		mtr_version;	/* set by mactype_alloc() */
	const char	*mtr_ident;
	mactype_ops_t	*mtr_ops;
	uint_t		mtr_mactype;
	uint_t		mtr_addrlen;
	uint8_t		*mtr_brdcst_addr;
	mac_stat_info_t	*mtr_stats;
	size_t		mtr_statcount;
} mactype_register_t;

/*
 * MAC classification
 */

typedef	struct mac_rxclassify_fn_t {
	mac_rx_classify_t	mcf_classify;	/* classif'n capabilities */
	void			*mcf_arg;	/* rule add func first arg */
} mac_rxclassify_fn_t;

#define	mcf_flow_classes	mcf_classify.mrc_flow_classes
#define	mcf_addrulefn		mcf_classify.mrc_addrulefn
#define	mcf_remrulefn		mcf_classify.mrc_remrulefn
#define	mcf_updatefn		mcf_classify.mrc_updatefn

/*
 * Structure describing the receive ring. It has:
 * . the type of classification to dispatch traffic for this ring,
 * . the driver's ring mac_resource_t with the driver's ring handle and its
 *   polling/blanking access functions.
 * . handle to the stack's ring's client (e.g ill_ring)
 * . pointer to the flow entry controlling this ring.
 * XXX This needs REHHOLD()'ing.
 */
struct mac_rx_ring_desc_s {
	mac_classify_status_t	mrd_classify_type;
	mac_resource_t		mrd_driver_rx;
	mac_resource_handle_t	mrd_client_handle;
	flow_entry_t		*mrd_flow_entry;
	void			*mrd_sringp;
};

/*
 * Client interface functions.
 */
extern int			mac_open(const char *, uint_t, mac_handle_t *);
extern void			mac_close(mac_handle_t);
extern const mac_info_t		*mac_info(mac_handle_t);
extern processorid_t		mac_intr_cpuid(mac_handle_t);
extern boolean_t		mac_info_get(const char *, mac_info_t *);
extern uint64_t			mac_stat_get(mac_handle_t, uint_t);
extern int			mac_start(mac_handle_t);
extern void			mac_stop(mac_handle_t);
extern int			mac_promisc_set(mac_handle_t, boolean_t,
    mac_promisc_type_t);
extern boolean_t		mac_promisc_get(mac_handle_t,
    mac_promisc_type_t);
extern int 			mac_multicst_add(mac_handle_t, const uint8_t *);
extern int 			mac_multicst_remove(mac_handle_t,
    const uint8_t *);
extern boolean_t		mac_unicst_verify(mac_handle_t,
    const uint8_t *, uint_t);
extern int			mac_unicst_set(mac_handle_t, const uint8_t *);
extern void			mac_unicst_get(mac_handle_t, uint8_t *);
extern void			mac_dest_get(mac_handle_t, uint8_t *);
extern void			mac_resources(mac_handle_t);
extern void			mac_ioctl(mac_handle_t, queue_t *, mblk_t *);
extern const mac_txinfo_t	*mac_tx_get(mac_handle_t);
extern const mac_txinfo_t	*mac_vnic_tx_get(mac_handle_t);
extern link_state_t		mac_link_get(mac_handle_t);
extern mac_notify_handle_t	mac_notify_add(mac_handle_t, mac_notify_t,
    void *);
extern void			mac_notify_remove(mac_handle_t,
    mac_notify_handle_t);
extern void			mac_notify(mac_handle_t);
extern mac_rx_handle_t		mac_rx_add(mac_handle_t, mac_rx_t, void *);
extern mac_rx_handle_t		mac_active_rx_add(mac_handle_t, mac_rx_t,
    void *);
extern void			mac_rx_remove(mac_handle_t, mac_rx_handle_t,
    boolean_t);
extern void			mac_rx_remove_wait(mac_handle_t);
extern mblk_t			*mac_txloop(void *, mblk_t *);
extern mac_txloop_handle_t	mac_txloop_add(mac_handle_t, mac_txloop_t,
    void *);
extern void			mac_txloop_remove(mac_handle_t,
    mac_txloop_handle_t);
extern boolean_t		mac_active_set(mac_handle_t);
extern boolean_t		mac_active_shareable_set(mac_handle_t);
extern void			mac_active_clear(mac_handle_t);
extern void			mac_active_rx(void *, mac_resource_handle_t,
    mblk_t *);
extern boolean_t		mac_vnic_set(mac_handle_t, mac_txinfo_t *,
    mac_getcapab_t, void *);
extern void			mac_vnic_clear(mac_handle_t);
extern void			mac_resource_set(mac_handle_t,
    mac_resource_add_t, mac_resource_bind_t, void *);
extern void			mac_get_cpus(mac_handle_t, net_bind_cpu_t *);
extern void			mac_set_cpus(mac_handle_t, net_bind_cpu_t *);
mac_rx_ring_desc_t		*mac_get_ring_desc(mac_handle_t);
extern dev_info_t		*mac_devinfo_get(mac_handle_t);
extern boolean_t		mac_capab_get(mac_handle_t, mac_capab_t,
    void *);
extern boolean_t		mac_vnic_capab_get(mac_handle_t, mac_capab_t,
    void *);
extern boolean_t		mac_sap_verify(mac_handle_t, uint32_t,
    uint32_t *);
extern mblk_t			*mac_header(mac_handle_t, const uint8_t *,
    uint32_t, mblk_t *, size_t);
extern int			mac_header_info(mac_handle_t, mblk_t *,
    mac_header_info_t *);
extern mblk_t			*mac_header_cook(mac_handle_t, mblk_t *);
extern mblk_t			*mac_header_uncook(mac_handle_t, mblk_t *);

/*
 * Driver interface functions.
 */
extern mac_register_t		*mac_alloc(uint_t);
extern void			mac_free(mac_register_t *);
extern int			mac_register(mac_register_t *, mac_handle_t *);
extern int  			mac_unregister(mac_handle_t);
extern int  			mac_disable(mac_handle_t);
extern void 			mac_rx(mac_handle_t, mac_resource_handle_t,
    mblk_t *);
extern void 			mac_rx_default(mac_handle_t,
    mac_resource_handle_t, mblk_t *, boolean_t);
extern void 			mac_link_update(mac_handle_t, link_state_t);
extern void 			mac_unicst_update(mac_handle_t,
    const uint8_t *);
extern void			mac_tx_update(mac_handle_t);
extern void			mac_resource_update(mac_handle_t);
extern mac_resource_handle_t	mac_client_resource_add(mac_handle_t,
    mac_resource_t *);
extern mac_resource_handle_t	mac_resource_add(mac_handle_t,
    mac_resource_t *);
extern int			mac_resource_bind(mac_handle_t,
    mac_resource_handle_t, processorid_t);
extern int			mac_pdata_update(mac_handle_t, void *,
    size_t);
extern void			mac_multicst_refresh(mac_handle_t,
    mac_multicst_t, void *, boolean_t);
extern void			mac_unicst_refresh(mac_handle_t, mac_unicst_t,
    void *);
extern void			mac_promisc_refresh(mac_handle_t,
    mac_setpromisc_t, void *);
extern void			mac_init_ops(struct dev_ops *, const char *);
extern void			mac_fini_ops(struct dev_ops *);
extern mactype_register_t	*mactype_alloc(uint_t);
extern void			mactype_free(mactype_register_t *);
extern int			mactype_register(mactype_register_t *);
extern int			mactype_unregister(const char *);
extern mac_classify_status_t	mac_rx_classify_flow_add(mac_handle_t,
    flow_entry_t *, boolean_t, mac_rx_func_t, void *,
    mac_resource_handle_t, net_bind_cpu_t *);

extern int			mac_rx_classify_flow_rem(mac_handle_t,
    flow_entry_t *);
extern int			mac_rx_classify_flow_update(mac_handle_t,
    flow_entry_t *, mac_rx_func_t, void *, mac_resource_handle_t, boolean_t);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_MAC_H */
