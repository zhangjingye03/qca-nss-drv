/*
 **************************************************************************
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#include "nss_tx_rx_common.h"

#define NSS_MAP_T_TX_TIMEOUT 3000 /* 3 Seconds */

/*
 * Private data structure
 */
static struct {
	struct semaphore sem;
	struct completion complete;
	int response;
	void *cb;
	void *app_data;
} nss_map_t_pvt;

/*
 * Data structures to store map_t nss debug stats
 */
static DEFINE_SPINLOCK(nss_map_t_debug_stats_lock);
static struct nss_stats_map_t_instance_debug nss_map_t_debug_stats[NSS_MAX_MAP_T_DYNAMIC_INTERFACES];

/*
 * nss_map_t_verify_if_num()
 *	Verify if_num passed to us.
 */
static bool nss_map_t_verify_if_num(uint32_t if_num)
{
	if (nss_is_dynamic_interface(if_num) == false) {
		return false;
	}

	if (nss_dynamic_interface_get_type(if_num)
	    != NSS_DYNAMIC_INTERFACE_TYPE_MAP_T) {
		return false;
	}

	return true;
}

/*
 * nss_map_t_instance_debug_stats_sync
 *	debug stats for map_t
 */
void nss_map_t_instance_debug_stats_sync(struct nss_ctx_instance *nss_ctx, struct nss_map_t_sync_stats_msg *stats_msg, uint16_t if_num)
{
	int i;
	spin_lock_bh(&nss_map_t_debug_stats_lock);
	for (i = 0; i < NSS_MAX_MAP_T_DYNAMIC_INTERFACES; i++) {
		if (nss_map_t_debug_stats[i].if_num == if_num) {
			nss_map_t_debug_stats[i].stats[NSS_STATS_MAP_T_V4_TO_V6_PBUF_EXCEPTION] += stats_msg->debug_stats.v4_to_v6.exception_pkts;
			nss_map_t_debug_stats[i].stats[NSS_STATS_MAP_T_V4_TO_V6_PBUF_NO_MATCHING_RULE] += stats_msg->debug_stats.v4_to_v6.no_matching_rule;
			nss_map_t_debug_stats[i].stats[NSS_STATS_MAP_T_V4_TO_V6_PBUF_NOT_TCP_OR_UDP] += stats_msg->debug_stats.v4_to_v6.not_tcp_or_udp;
			nss_map_t_debug_stats[i].stats[NSS_STATS_MAP_T_V4_TO_V6_RULE_ERR_LOCAL_PSID_MISMATCH] += stats_msg->debug_stats.v4_to_v6.rule_err_local_psid_mismatch;
			nss_map_t_debug_stats[i].stats[NSS_STATS_MAP_T_V4_TO_V6_RULE_ERR_LOCAL_IPV6] += stats_msg->debug_stats.v4_to_v6.rule_err_local_ipv6;
			nss_map_t_debug_stats[i].stats[NSS_STATS_MAP_T_V4_TO_V6_RULE_ERR_REMOTE_PSID] += stats_msg->debug_stats.v4_to_v6.rule_err_remote_psid;
			nss_map_t_debug_stats[i].stats[NSS_STATS_MAP_T_V4_TO_V6_RULE_ERR_REMOTE_EA_BITS] += stats_msg->debug_stats.v4_to_v6.rule_err_remote_ea_bits;
			nss_map_t_debug_stats[i].stats[NSS_STATS_MAP_T_V4_TO_V6_RULE_ERR_REMOTE_IPV6] += stats_msg->debug_stats.v4_to_v6.rule_err_remote_ipv6;

			nss_map_t_debug_stats[i].stats[NSS_STATS_MAP_T_V6_TO_V4_PBUF_EXCEPTION] += stats_msg->debug_stats.v6_to_v4.exception_pkts;
			nss_map_t_debug_stats[i].stats[NSS_STATS_MAP_T_V6_TO_V4_PBUF_NO_MATCHING_RULE] += stats_msg->debug_stats.v6_to_v4.no_matching_rule;
			nss_map_t_debug_stats[i].stats[NSS_STATS_MAP_T_V6_TO_V4_PBUF_NOT_TCP_OR_UDP] += stats_msg->debug_stats.v6_to_v4.not_tcp_or_udp;
			nss_map_t_debug_stats[i].stats[NSS_STATS_MAP_T_V6_TO_V4_RULE_ERR_LOCAL_IPV4] += stats_msg->debug_stats.v6_to_v4.rule_err_local_ipv4;
			nss_map_t_debug_stats[i].stats[NSS_STATS_MAP_T_V6_TO_V4_RULE_ERR_REMOTE_IPV4] += stats_msg->debug_stats.v6_to_v4.rule_err_remote_ipv4;
			break;
		}
	}
	spin_unlock_bh(&nss_map_t_debug_stats_lock);
}

/*
 * nss_map_t_instance_stats_get()
 *	Get map_t statitics.
 */
void nss_map_t_instance_debug_stats_get(void *stats_mem)
{
	struct nss_stats_map_t_instance_debug *stats = (struct nss_stats_map_t_instance_debug *)stats_mem;
	int i;

	if (!stats) {
		nss_warning("No memory to copy map_t stats");
		return;
	}

	spin_lock_bh(&nss_map_t_debug_stats_lock);
	for (i = 0; i < NSS_MAX_MAP_T_DYNAMIC_INTERFACES; i++) {
		if (nss_map_t_debug_stats[i].valid) {
			memcpy(stats, &nss_map_t_debug_stats[i], sizeof(struct nss_stats_map_t_instance_debug));
			stats++;
		}
	}
	spin_unlock_bh(&nss_map_t_debug_stats_lock);
}

/*
 * nss_map_t_handler()
 *	Handle NSS -> HLOS messages for map_t tunnel
 */
static void nss_map_t_handler(struct nss_ctx_instance *nss_ctx, struct nss_cmn_msg *ncm, __attribute__((unused))void *app_data)
{
	struct nss_map_t_msg *ntm = (struct nss_map_t_msg *)ncm;
	void *ctx;

	nss_map_t_msg_callback_t cb;

	NSS_VERIFY_CTX_MAGIC(nss_ctx);
	BUG_ON(!nss_map_t_verify_if_num(ncm->interface));

	/*
	 * Is this a valid request/response packet?
	 */
	if (ncm->type >= NSS_MAP_T_MSG_MAX) {
		nss_warning("%p: received invalid message %d for MAP-T interface", nss_ctx, ncm->type);
		return;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_map_t_msg)) {
		nss_warning("%p: tx request for another interface: %d", nss_ctx, ncm->interface);
		return;
	}

	switch (ntm->cm.type) {
	case NSS_MAP_T_MSG_SYNC_STATS:
		/*
		 * debug stats embedded in stats msg
		 */
		nss_map_t_instance_debug_stats_sync(nss_ctx, &ntm->msg.stats, ncm->interface);
		break;
	}

	/*
	 * Update the callback and app_data for NOTIFY messages, map_t sends all notify messages
	 * to the same callback/app_data.
	 */
	if (ncm->response == NSS_CMM_RESPONSE_NOTIFY) {
		ncm->cb = (uint32_t)nss_ctx->nss_top->map_t_msg_callback;
		ncm->app_data = (uint32_t)nss_ctx->nss_top->subsys_dp_register[ncm->interface].app_data;
	}

	/*
	 * Log failures
	 */
	nss_core_log_msg_failures(nss_ctx, ncm);

	/*
	 * callback
	 */
	cb = (nss_map_t_msg_callback_t)ncm->cb;
	ctx = (void *)ncm->app_data;

	/*
	 * call map-t callback
	 */
	if (!cb) {
		nss_warning("%p: No callback for map-t interface %d",
			    nss_ctx, ncm->interface);
		return;
	}

	cb(ctx, ntm);
}

/*
 * nss_map_t_callback()
 *	Callback to handle the completion of NSS->HLOS messages.
 */
static void nss_map_t_callback(void *app_data, struct nss_map_t_msg *nim)
{
	nss_map_t_msg_callback_t callback = (nss_map_t_msg_callback_t)nss_map_t_pvt.cb;
	void *data = nss_map_t_pvt.app_data;

	nss_map_t_pvt.cb = NULL;
	nss_map_t_pvt.app_data = NULL;

	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		nss_warning("map_t Error response %d\n", nim->cm.response);
		nss_map_t_pvt.response = NSS_TX_FAILURE;
	} else {
		nss_map_t_pvt.response = NSS_TX_SUCCESS;
	}

	if (callback) {
		callback(data, nim);
	}

	complete(&nss_map_t_pvt.complete);
}

/*
 * nss_map_t_tx()
 *	Transmit a map_t message to NSS firmware
 */
nss_tx_status_t nss_map_t_tx(struct nss_ctx_instance *nss_ctx, struct nss_map_t_msg *msg)
{
	struct nss_map_t_msg *nm;
	struct nss_cmn_msg *ncm = &msg->cm;
	struct sk_buff *nbuf;
	int32_t status;

	NSS_VERIFY_CTX_MAGIC(nss_ctx);
	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: map_t msg dropped as core not ready", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	/*
	 * Sanity check the message
	 */
	if (!nss_is_dynamic_interface(ncm->interface)) {
		nss_warning("%p: tx request for non dynamic interface: %d", nss_ctx, ncm->interface);
		return NSS_TX_FAILURE;
	}

	if (ncm->type > NSS_MAP_T_MSG_MAX) {
		nss_warning("%p: message type out of range: %d", nss_ctx, ncm->type);
		return NSS_TX_FAILURE;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_map_t_msg)) {
		nss_warning("%p: message length is invalid: %d", nss_ctx, nss_cmn_get_msg_len(ncm));
		return NSS_TX_FAILURE;
	}

	nbuf = dev_alloc_skb(NSS_NBUF_PAYLOAD_SIZE);
	if (unlikely(!nbuf)) {
		NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_NBUF_ALLOC_FAILS]);
		nss_warning("%p: msg dropped as command allocation failed", nss_ctx);
		return NSS_TX_FAILURE;
	}

	/*
	 * Copy the message to our skb
	 */
	nm = (struct nss_map_t_msg *)skb_put(nbuf, sizeof(struct nss_map_t_msg));
	memcpy(nm, msg, sizeof(struct nss_map_t_msg));

	status = nss_core_send_buffer(nss_ctx, 0, nbuf, NSS_IF_CMD_QUEUE, H2N_BUFFER_CTRL, 0);
	if (status != NSS_CORE_STATUS_SUCCESS) {
		dev_kfree_skb_any(nbuf);
		nss_warning("%p: Unable to enqueue 'map_t message'\n", nss_ctx);
		if (status == NSS_CORE_STATUS_FAILURE_QUEUE) {
			return NSS_TX_FAILURE_QUEUE;
		}
		return NSS_TX_FAILURE;
	}

	nss_hal_send_interrupt(nss_ctx->nmap, nss_ctx->h2n_desc_rings[NSS_IF_CMD_QUEUE].desc_ring.int_bit,
				NSS_REGS_H2N_INTR_STATUS_DATA_COMMAND_QUEUE);

	NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_TX_CMD_REQ]);
	return NSS_TX_SUCCESS;
}
EXPORT_SYMBOL(nss_map_t_tx);

/*
 * nss_map_t_tx_sync()
 *	Transmit a MAP-T message to NSS firmware synchronously.
 */
nss_tx_status_t nss_map_t_tx_sync(struct nss_ctx_instance *nss_ctx, struct nss_map_t_msg *msg)
{
	nss_tx_status_t status;
	int ret = 0;

	down(&nss_map_t_pvt.sem);
	nss_map_t_pvt.cb = (void *)msg->cm.cb;
	nss_map_t_pvt.app_data = (void *)msg->cm.app_data;

	msg->cm.cb = (uint32_t)nss_map_t_callback;
	msg->cm.app_data = (uint32_t)NULL;

	status = nss_map_t_tx(nss_ctx, msg);
	if (status != NSS_TX_SUCCESS) {
		nss_warning("%p: map_t_tx_msg failed\n", nss_ctx);
		up(&nss_map_t_pvt.sem);
		return status;
	}
	ret = wait_for_completion_timeout(&nss_map_t_pvt.complete, msecs_to_jiffies(NSS_MAP_T_TX_TIMEOUT));

	if (!ret) {
		nss_warning("%p: MAP-T tx sync failed due to timeout\n", nss_ctx);
		nss_map_t_pvt.response = NSS_TX_FAILURE;
	}

	status = nss_map_t_pvt.response;
	up(&nss_map_t_pvt.sem);
	return status;
}
EXPORT_SYMBOL(nss_map_t_tx_sync);

/*
 ***********************************
 * Register/Unregister/Miscellaneous APIs
 ***********************************
 */

/*
 * nss_map_t_register_if()
 */
struct nss_ctx_instance *nss_map_t_register_if(uint32_t if_num, nss_map_t_callback_t map_t_callback,
			nss_map_t_msg_callback_t event_callback, struct net_device *netdev, uint32_t features)
{
	int i = 0;
	nss_assert(nss_is_dynamic_interface(if_num));

	nss_top_main.subsys_dp_register[if_num].ndev = netdev;
	nss_top_main.subsys_dp_register[if_num].cb = map_t_callback;
	nss_top_main.subsys_dp_register[if_num].app_data = netdev;
	nss_top_main.subsys_dp_register[if_num].features = features;

	nss_top_main.map_t_msg_callback = event_callback;

	nss_core_register_handler(if_num, nss_map_t_handler, NULL);

	spin_lock_bh(&nss_map_t_debug_stats_lock);
	for (i = 0; i < NSS_MAX_MAP_T_DYNAMIC_INTERFACES; i++) {
		if (!nss_map_t_debug_stats[i].valid) {
			nss_map_t_debug_stats[i].valid = true;
			nss_map_t_debug_stats[i].if_num = if_num;
			nss_map_t_debug_stats[i].if_index = netdev->ifindex;
			break;
		}
	}
	spin_unlock_bh(&nss_map_t_debug_stats_lock);

	return (struct nss_ctx_instance *)&nss_top_main.nss[nss_top_main.map_t_handler_id];
}
EXPORT_SYMBOL(nss_map_t_register_if);

/*
 * nss_map_t_unregister_if()
 */
void nss_map_t_unregister_if(uint32_t if_num)
{
	int i;
	nss_assert(nss_is_dynamic_interface(if_num));

	nss_top_main.subsys_dp_register[if_num].ndev = NULL;
	nss_top_main.subsys_dp_register[if_num].cb = NULL;
	nss_top_main.subsys_dp_register[if_num].app_data = NULL;
	nss_top_main.subsys_dp_register[if_num].features = 0;

	nss_top_main.map_t_msg_callback = NULL;

	nss_core_unregister_handler(if_num);

	spin_lock_bh(&nss_map_t_debug_stats_lock);
	for (i = 0; i < NSS_MAX_MAP_T_DYNAMIC_INTERFACES; i++) {
		if (nss_map_t_debug_stats[i].if_num == if_num) {
			memset(&nss_map_t_debug_stats[i], 0, sizeof(struct nss_stats_map_t_instance_debug));
			break;
		}
	}
	spin_unlock_bh(&nss_map_t_debug_stats_lock);
}
EXPORT_SYMBOL(nss_map_t_unregister_if);

/*
 * nss_get_map_t_context()
 */
struct nss_ctx_instance *nss_map_t_get_context()
{
	return (struct nss_ctx_instance *)&nss_top_main.nss[nss_top_main.map_t_handler_id];
}
EXPORT_SYMBOL(nss_map_t_get_context);

/*
 * nss_map_t_msg_init()
 *	Initialize nss_map_t msg.
 */
void nss_map_t_msg_init(struct nss_map_t_msg *ncm, uint16_t if_num, uint32_t type,  uint32_t len, void *cb, void *app_data)
{
	nss_cmn_msg_init(&ncm->cm, if_num, type, len, cb, app_data);
}
EXPORT_SYMBOL(nss_map_t_msg_init);

/*
 * nss_map_t_register_handler()
 *	debugfs stats msg handler received on static map_t interface
 */
void nss_map_t_register_handler(void)
{
	nss_info("nss_map_t_register_handler");
	sema_init(&nss_map_t_pvt.sem, 1);
	init_completion(&nss_map_t_pvt.complete);
	nss_core_register_handler(NSS_MAP_T_INTERFACE, nss_map_t_handler, NULL);
}
