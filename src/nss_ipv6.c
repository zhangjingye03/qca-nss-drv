/*
 **************************************************************************
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 **************************************************************************
 */

/*
 * nss_ipv6.c
 *	NSS IPv6 APIs
 */
#include "nss_tx_rx_common.h"

int nss_ipv6_conn_cfg __read_mostly = NSS_DEFAULT_NUM_CONN;
static struct  nss_conn_cfg_pvt i6cfgp;

/*
 * nss_ipv6_max_conn_count()
 *	Return the maximum number of IPv6 connections that the NSS acceleration engine supports.
 */
int nss_ipv6_max_conn_count(void)
{
	return nss_core_max_ipv6_conn_get();
}
EXPORT_SYMBOL(nss_ipv6_max_conn_count);

/*
 * nss_ipv6_driver_conn_sync_update()
 *	Update driver specific information from the messsage.
 */
static void nss_ipv6_driver_conn_sync_update(struct nss_ctx_instance *nss_ctx, struct nss_ipv6_conn_sync *nics)
{
	struct nss_top_instance *nss_top = nss_ctx->nss_top;

	/*
	 * Update statistics maintained by NSS driver
	 */
	spin_lock_bh(&nss_top->stats_lock);
	nss_top->stats_ipv6[NSS_STATS_IPV6_ACCELERATED_RX_PKTS] += nics->flow_rx_packet_count + nics->return_rx_packet_count;
	nss_top->stats_ipv6[NSS_STATS_IPV6_ACCELERATED_RX_BYTES] += nics->flow_rx_byte_count + nics->return_rx_byte_count;
	nss_top->stats_ipv6[NSS_STATS_IPV6_ACCELERATED_TX_PKTS] += nics->flow_tx_packet_count + nics->return_tx_packet_count;
	nss_top->stats_ipv6[NSS_STATS_IPV6_ACCELERATED_TX_BYTES] += nics->flow_tx_byte_count + nics->return_tx_byte_count;
	spin_unlock_bh(&nss_top->stats_lock);
}

/*
 * nss_ipv6_driver_conn_sync_many_update()
 *	Update driver specific information from the conn_sync_many messsage.
 */
static void nss_ipv6_driver_conn_sync_many_update(struct nss_ctx_instance *nss_ctx, struct nss_ipv6_conn_sync_many_msg *nicsm)
{
	uint32_t i;

	/*
	 * Sanity check for the stats count
	 */
	if (nicsm->count * sizeof(struct nss_ipv6_conn_sync) >= nicsm->size) {
		nss_warning("%p: stats sync count %u exceeds the size of this msg %u", nss_ctx, nicsm->count, nicsm->size);
		return;
	}

	for (i = 0; i < nicsm->count; i++) {
		nss_ipv6_driver_conn_sync_update(nss_ctx, &nicsm->conn_sync[i]);
	}
}

/*
 * nss_ipv6_driver_node_sync_update)
 *	Update driver specific information from the messsage.
 */
static void nss_ipv6_driver_node_sync_update(struct nss_ctx_instance *nss_ctx, struct nss_ipv6_node_sync *nins)
{
	struct nss_top_instance *nss_top = nss_ctx->nss_top;
	uint32_t i;

	/*
	 * Update statistics maintained by NSS driver
	 */
	spin_lock_bh(&nss_top->stats_lock);
	nss_top->stats_node[NSS_IPV6_RX_INTERFACE][NSS_STATS_NODE_RX_PKTS] += nins->node_stats.rx_packets;
	nss_top->stats_node[NSS_IPV6_RX_INTERFACE][NSS_STATS_NODE_RX_BYTES] += nins->node_stats.rx_bytes;
	nss_top->stats_node[NSS_IPV6_RX_INTERFACE][NSS_STATS_NODE_RX_DROPPED] += nins->node_stats.rx_dropped;
	nss_top->stats_node[NSS_IPV6_RX_INTERFACE][NSS_STATS_NODE_TX_PKTS] += nins->node_stats.tx_packets;
	nss_top->stats_node[NSS_IPV6_RX_INTERFACE][NSS_STATS_NODE_TX_BYTES] += nins->node_stats.tx_bytes;

	nss_top->stats_ipv6[NSS_STATS_IPV6_CONNECTION_CREATE_REQUESTS] += nins->ipv6_connection_create_requests;
	nss_top->stats_ipv6[NSS_STATS_IPV6_CONNECTION_CREATE_COLLISIONS] += nins->ipv6_connection_create_collisions;
	nss_top->stats_ipv6[NSS_STATS_IPV6_CONNECTION_CREATE_INVALID_INTERFACE] += nins->ipv6_connection_create_invalid_interface;
	nss_top->stats_ipv6[NSS_STATS_IPV6_CONNECTION_DESTROY_REQUESTS] += nins->ipv6_connection_destroy_requests;
	nss_top->stats_ipv6[NSS_STATS_IPV6_CONNECTION_DESTROY_MISSES] += nins->ipv6_connection_destroy_misses;
	nss_top->stats_ipv6[NSS_STATS_IPV6_CONNECTION_HASH_HITS] += nins->ipv6_connection_hash_hits;
	nss_top->stats_ipv6[NSS_STATS_IPV6_CONNECTION_HASH_REORDERS] += nins->ipv6_connection_hash_reorders;
	nss_top->stats_ipv6[NSS_STATS_IPV6_CONNECTION_FLUSHES] += nins->ipv6_connection_flushes;
	nss_top->stats_ipv6[NSS_STATS_IPV6_CONNECTION_EVICTIONS] += nins->ipv6_connection_evictions;
	nss_top->stats_ipv6[NSS_STATS_IPV6_FRAGMENTATIONS] += nins->ipv6_fragmentations;
	nss_top->stats_ipv6[NSS_STATS_IPV6_FRAG_FAILS] += nins->ipv6_frag_fails;
	nss_top->stats_ipv6[NSS_STATS_IPV6_MC_CONNECTION_CREATE_REQUESTS] += nins->ipv6_mc_connection_create_requests;
	nss_top->stats_ipv6[NSS_STATS_IPV6_MC_CONNECTION_UPDATE_REQUESTS] += nins->ipv6_mc_connection_update_requests;
	nss_top->stats_ipv6[NSS_STATS_IPV6_MC_CONNECTION_CREATE_INVALID_INTERFACE] += nins->ipv6_mc_connection_create_invalid_interface;
	nss_top->stats_ipv6[NSS_STATS_IPV6_MC_CONNECTION_DESTROY_REQUESTS] += nins->ipv6_mc_connection_destroy_requests;
	nss_top->stats_ipv6[NSS_STATS_IPV6_MC_CONNECTION_DESTROY_MISSES] += nins->ipv6_mc_connection_destroy_misses;
	nss_top->stats_ipv6[NSS_STATS_IPV6_MC_CONNECTION_FLUSHES] += nins->ipv6_mc_connection_flushes;

	for (i = 0; i < NSS_EXCEPTION_EVENT_IPV6_MAX; i++) {
		 nss_top->stats_if_exception_ipv6[i] += nins->exception_events[i];
	}
	spin_unlock_bh(&nss_top->stats_lock);
}

/*
 * nss_ipv6_rx_msg_handler()
 *	Handle NSS -> HLOS messages for IPv6 bridge/route
 */
static void nss_ipv6_rx_msg_handler(struct nss_ctx_instance *nss_ctx, struct nss_cmn_msg *ncm, __attribute__((unused))void *app_data)
{
	struct nss_ipv6_msg *nim = (struct nss_ipv6_msg *)ncm;
	nss_ipv6_msg_callback_t cb;

	BUG_ON(ncm->interface != NSS_IPV6_RX_INTERFACE);

	/*
	 * Is this a valid request/response packet?
	 */
	if (ncm->type >= NSS_IPV6_MAX_MSG_TYPES) {
		nss_warning("%p: received invalid message %d for IPv6 interface", nss_ctx, nim->cm.type);
		return;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_ipv6_msg)) {
		nss_warning("%p: message length is invalid: %d", nss_ctx, nss_cmn_get_msg_len(ncm));
		return;
	}

	/*
	 * Log failures
	 */
	nss_core_log_msg_failures(nss_ctx, ncm);

	/*
	 * Handle deprecated messages.  Eventually these messages should be removed.
	 */
	switch (nim->cm.type) {
	case NSS_IPV6_RX_NODE_STATS_SYNC_MSG:
		/*
		* Update driver statistics on node sync.
		*/
		nss_ipv6_driver_node_sync_update(nss_ctx, &nim->msg.node_stats);
		break;

	case NSS_IPV6_RX_CONN_STATS_SYNC_MSG:
		/*
		 * Update driver statistics on connection sync.
		 */
		nss_ipv6_driver_conn_sync_update(nss_ctx, &nim->msg.conn_stats);
		break;

	case NSS_IPV6_TX_CONN_STATS_SYNC_MANY_MSG:
		/*
		 * Update driver statistics on connection sync many.
		 */
		nss_ipv6_driver_conn_sync_many_update(nss_ctx, &nim->msg.conn_stats_many);
		break;
	}

	/*
	 * Update the callback and app_data for NOTIFY messages, IPv6 sends all notify messages
	 * to the same callback/app_data.
	 */
	if (nim->cm.response == NSS_CMM_RESPONSE_NOTIFY) {
		ncm->cb = (uint32_t)nss_ctx->nss_top->ipv6_callback;
		ncm->app_data = (uint32_t)nss_ctx->nss_top->ipv6_ctx;
	}

	/*
	 * Do we have a callback?
	 */
	if (!ncm->cb) {
		return;
	}

	/*
	 * Callback
	 */
	cb = (nss_ipv6_msg_callback_t)ncm->cb;
	cb((void *)ncm->app_data, nim);
}

/*
 * nss_ipv6_tx_with_size()
 *	Transmit an ipv6 message to the FW with a specified size.
 */
nss_tx_status_t nss_ipv6_tx_with_size(struct nss_ctx_instance *nss_ctx, struct nss_ipv6_msg *nim, uint32_t size)
{
	struct nss_ipv6_msg *nim2;
	struct nss_cmn_msg *ncm = &nim->cm;
	struct sk_buff *nbuf;
	int32_t status;

	NSS_VERIFY_CTX_MAGIC(nss_ctx);
	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: ipv6 msg dropped as core not ready", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	/*
	 * Sanity check the message
	 */
	if (ncm->interface != NSS_IPV6_RX_INTERFACE) {
		nss_warning("%p: tx request for another interface: %d", nss_ctx, ncm->interface);
		return NSS_TX_FAILURE;
	}

	if (ncm->type >= NSS_IPV6_MAX_MSG_TYPES) {
		nss_warning("%p: message type out of range: %d", nss_ctx, ncm->type);
		return NSS_TX_FAILURE;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_ipv6_msg)) {
		nss_warning("%p: message length is invalid: %d", nss_ctx, nss_cmn_get_msg_len(ncm));
		return NSS_TX_FAILURE;
	}

	if(size > PAGE_SIZE) {
		nss_warning("%p: tx request size too large: %u", nss_ctx, size);
		return NSS_TX_FAILURE;
	}

	nbuf = dev_alloc_skb(size);
	if (unlikely(!nbuf)) {
		NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_NBUF_ALLOC_FAILS]);
		nss_warning("%p: msg dropped as command allocation failed", nss_ctx);
		return NSS_TX_FAILURE;
	}

	/*
	 * Copy the message to our skb.
	 */
	nim2 = (struct nss_ipv6_msg *)skb_put(nbuf, sizeof(struct nss_ipv6_msg));
	memcpy(nim2, nim, sizeof(struct nss_ipv6_msg));

	status = nss_core_send_buffer(nss_ctx, 0, nbuf, NSS_IF_CMD_QUEUE, H2N_BUFFER_CTRL, 0);
	if (status != NSS_CORE_STATUS_SUCCESS) {
		dev_kfree_skb_any(nbuf);
		nss_warning("%p: Unable to enqueue 'Destroy IPv6' rule\n", nss_ctx);
		return NSS_TX_FAILURE;
	}

	nss_hal_send_interrupt(nss_ctx->nmap, nss_ctx->h2n_desc_rings[NSS_IF_CMD_QUEUE].desc_ring.int_bit,
								NSS_REGS_H2N_INTR_STATUS_DATA_COMMAND_QUEUE);

	NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_TX_CMD_REQ]);
	return NSS_TX_SUCCESS;
}

/*
 * nss_ipv6_tx()
 *	Transmit an ipv4 message to the FW.
 */
nss_tx_status_t nss_ipv6_tx(struct nss_ctx_instance *nss_ctx, struct nss_ipv6_msg *nim)
{
	return nss_ipv6_tx_with_size(nss_ctx, nim, NSS_NBUF_PAYLOAD_SIZE);
}

/*
 **********************************
 Register/Unregister/Miscellaneous APIs
 **********************************
 */

/*
 * nss_ipv6_notify_register()
 *	Register to received IPv6 events.
 *
 * NOTE: Do we want to pass an nss_ctx here so that we can register for ipv6 on any core?
 */
struct nss_ctx_instance *nss_ipv6_notify_register(nss_ipv6_msg_callback_t cb, void *app_data)
{
	/*
	 * TODO: We need to have a new array in support of the new API
	 * TODO: If we use a per-context array, we would move the array into nss_ctx based.
	 */
	nss_top_main.ipv6_callback = cb;
	nss_top_main.ipv6_ctx = app_data;
	return &nss_top_main.nss[nss_top_main.ipv6_handler_id];
}

/*
 * nss_ipv6_notify_unregister()
 *	Unregister to received IPv6 events.
 *
 * NOTE: Do we want to pass an nss_ctx here so that we can register for ipv6 on any core?
 */
void nss_ipv6_notify_unregister(void)
{
	nss_top_main.ipv6_callback = NULL;
}

/*
 * nss_ipv6_get_mgr()
 *
 * TODO: This only suppports a single ipv6, do we ever want to support more?
 */
struct nss_ctx_instance *nss_ipv6_get_mgr(void)
{
	return (void *)&nss_top_main.nss[nss_top_main.ipv6_handler_id];
}

/*
 * nss_ipv6_register_handler()
 *	Register our handler to receive messages for this interface
 */
void nss_ipv6_register_handler()
{
	if (nss_core_register_handler(NSS_IPV6_RX_INTERFACE, nss_ipv6_rx_msg_handler, NULL) != NSS_CORE_STATUS_SUCCESS) {
		nss_warning("IPv6 handler failed to register");
	}
}

/*
 * nss_ipv6_conn_cfg_process()
 *	Process request to configure number of ipv6 connections
 */
static int nss_ipv6_conn_cfg_process(struct nss_ctx_instance *nss_ctx, int conn,
				     void (*cfg_cb)(void *app_data, struct nss_ipv6_msg *nim))
{
	struct nss_ipv6_msg nim;
	struct nss_ipv6_rule_conn_cfg_msg *nirccm;
	nss_tx_status_t nss_tx_status;
	uint32_t sum_of_conn;

	/*
	 * Specifications for input
	 * 1) The input should be power of 2.
	 * 2) Input for ipv4 and ipv6 sum togther should not exceed 8k
	 * 3) Min. value should be at leat 256 connections. This is the
	 * minimum connections we will support for each of them.
	 */
	sum_of_conn = nss_ipv4_conn_cfg + conn;
	if ((conn & NSS_NUM_CONN_QUANTA_MASK) ||
		(sum_of_conn > NSS_MAX_TOTAL_NUM_CONN_IPV4_IPV6) ||
		(conn < NSS_MIN_NUM_CONN)) {
		nss_warning("%p: input supported connections (%d) does not adhere\
				specifications\n1) not power of 2,\n2) is less than \
				min val: %d, OR\n 	IPv4/6 total exceeds %d\n",
				nss_ctx,
				conn,
				NSS_MIN_NUM_CONN,
				NSS_MAX_TOTAL_NUM_CONN_IPV4_IPV6);
		return -EINVAL;
	}

	nss_info("%p: IPv6 supported connections: %d\n", nss_ctx, conn);

	memset(&nim, 0, sizeof(struct nss_ipv6_msg));
	nss_ipv6_msg_init(&nim, NSS_IPV6_RX_INTERFACE, NSS_IPV6_TX_CONN_CFG_RULE_MSG,
		sizeof(struct nss_ipv6_rule_conn_cfg_msg), cfg_cb, NULL);

	nirccm = &nim.msg.rule_conn_cfg;
	nirccm->num_conn = htonl(conn);
	nss_tx_status = nss_ipv6_tx(nss_ctx, &nim);

	if (nss_tx_status != NSS_TX_SUCCESS) {
		nss_warning("%p: nss_tx error setting IPv6 Connections: %d\n",
						nss_ctx,
						conn);
		return -EIO;
	}

	return 0;
}

/*
 * nss_ipv6_conn_cfg_callback()
 *	call back function for the ipv6 connection configuration handler
 */
static void nss_ipv6_conn_cfg_callback(void *app_data, struct nss_ipv6_msg *nim)
{
	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		/*
		 * Error, hence we are not updating the nss_ipv6_conn_cfg
		 * Restore the current_value to its previous state
		 */
		i6cfgp.response = NSS_FAILURE;
		complete(&i6cfgp.complete);
		return;
	}

	/*
	 * Sucess at NSS FW, hence updating nss_ipv6_conn_cfg, with the valid value
	 * saved at the sysctl handler.
	 */
	nss_info("IPv6 connection configuration success: %d\n", nim->cm.error);
	i6cfgp.response = NSS_SUCCESS;
	complete(&i6cfgp.complete);
}


/*
 * nss_ipv6_conn_cfg_handler()
 *	Sets the number of connections for IPv6
 */
static int nss_ipv6_conn_cfg_handler(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct nss_top_instance *nss_top = &nss_top_main;
	struct nss_ctx_instance *nss_ctx = &nss_top->nss[0];
	int ret = NSS_FAILURE;

	/*
	 * Acquiring semaphore
	 */
	down(&i6cfgp.sem);

	/*
	 *  Take a snapshot of the current value
	 */
	i6cfgp.current_value = nss_ipv6_conn_cfg;

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);
	if (ret || (!write)) {
		up(&i6cfgp.sem);
		return ret;
	}

	/*
	 * Process request to change number of IPv6 connections
	 */
	ret = nss_ipv6_conn_cfg_process(nss_ctx, nss_ipv6_conn_cfg, nss_ipv6_conn_cfg_callback);
	if (ret != 0) {
		goto failure;
	}

	/*
	 * Blocking call, wait till we get ACK for this msg.
	 */
	ret = wait_for_completion_timeout(&i6cfgp.complete, msecs_to_jiffies(NSS_CONN_CFG_TIMEOUT));
	if (ret == 0) {
		nss_warning("%p: Waiting for ack timed out\n", nss_ctx);
		goto failure;
	}

	/*
	 * ACK/NACK received from NSS FW
	 * If ACK: Callback function will update nss_ipv6_conn_cfg with
	 * i6cfgp.num_conn_valid, which holds the user input
	 */
	if (NSS_FAILURE == i6cfgp.response) {
		goto failure;
	}

	up(&i6cfgp.sem);
	return 0;

failure:
	/*
	 * Restore the current_value to its previous state
	 */
	nss_ipv6_conn_cfg = i6cfgp.current_value;
	up(&i6cfgp.sem);
	return -EINVAL;
}

/*
 * nss_ipv6_update_conn_count_cb()
 *	call back function for the ipv6 connection count update handler
 */
static void nss_ipv6_update_conn_count_cb(void *app_data, struct nss_ipv6_msg *nim)
{
	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		nss_warning("IPv6 connection count update failed with error: %d\n", nim->cm.error);
		return;
	}

	nss_warning("IPv6 connection count update success: %d\n", nim->cm.error);
}

/*
 * nss_ipv6_update_conn_count()
 *	Sets the maximum number of connections for IPv6
 */
int nss_ipv6_update_conn_count(int ipv6_num_conn)
{
	struct nss_top_instance *nss_top = &nss_top_main;
	struct nss_ctx_instance *nss_ctx = &nss_top->nss[0];
	int saved_nss_ipv6_conn_cfg = nss_ipv6_conn_cfg;
	int ret = 0;

	nss_ipv6_conn_cfg = ipv6_num_conn;

	/*
	 * Process request to change number of IPv6 connections
	 */
	ret = nss_ipv6_conn_cfg_process(nss_ctx, nss_ipv6_conn_cfg, nss_ipv6_update_conn_count_cb);
	if (ret != 0) {
		goto failure;
	}

	return 0;

failure:
	nss_ipv6_conn_cfg = saved_nss_ipv6_conn_cfg;
	return -EINVAL;
}

static struct ctl_table nss_ipv6_table[] = {
	{
		.procname		= "ipv6_conn",
		.data			= &nss_ipv6_conn_cfg,
		.maxlen			= sizeof(int),
		.mode			= 0644,
		.proc_handler   	= &nss_ipv6_conn_cfg_handler,
	},
	{ }
};

static struct ctl_table nss_ipv6_dir[] = {
	{
		.procname		= "ipv6cfg",
		.mode			= 0555,
		.child			= nss_ipv6_table,
	},
	{ }
};

static struct ctl_table nss_ipv6_root_dir[] = {
	{
		.procname		= "nss",
		.mode			= 0555,
		.child			= nss_ipv6_dir,
	},
	{ }
};

static struct ctl_table nss_ipv6_root[] = {
	{
		.procname		= "dev",
		.mode			= 0555,
		.child			= nss_ipv6_root_dir,
	},
	{ }
};

static struct ctl_table_header *nss_ipv6_header;

/*
 * nss_ipv6_register_sysctl()
 *	Register sysctl specific to ipv6
 */
void nss_ipv6_register_sysctl(void)
{
	sema_init(&i6cfgp.sem, 1);
	init_completion(&i6cfgp.complete);
	i6cfgp.current_value = nss_ipv6_conn_cfg;

	/*
	 * Register sysctl table.
	 */
	nss_ipv6_header = register_sysctl_table(nss_ipv6_root);
}

/*
 * nss_ipv6_unregister_sysctl()
 *	Unregister sysctl specific to ipv6
 */
void nss_ipv6_unregister_sysctl(void)
{
	/*
	 * Unregister sysctl table.
	 */
	if (nss_ipv6_header) {
		unregister_sysctl_table(nss_ipv6_header);
	}
}

/*
 * nss_ipv6_msg_init()
 *      Initialize IPv6 message.
 */
void nss_ipv6_msg_init(struct nss_ipv6_msg *nim, uint16_t if_num, uint32_t type, uint32_t len,
			nss_ipv6_msg_callback_t cb, void *app_data)
{
	nss_cmn_msg_init(&nim->cm, if_num, type, len, (void *)cb, app_data);
}

EXPORT_SYMBOL(nss_ipv6_tx);
EXPORT_SYMBOL(nss_ipv6_tx_with_size);
EXPORT_SYMBOL(nss_ipv6_notify_register);
EXPORT_SYMBOL(nss_ipv6_notify_unregister);
EXPORT_SYMBOL(nss_ipv6_get_mgr);
EXPORT_SYMBOL(nss_ipv6_register_handler);
EXPORT_SYMBOL(nss_ipv6_register_sysctl);
EXPORT_SYMBOL(nss_ipv6_unregister_sysctl);
EXPORT_SYMBOL(nss_ipv6_msg_init);
EXPORT_SYMBOL(nss_ipv6_update_conn_count);
