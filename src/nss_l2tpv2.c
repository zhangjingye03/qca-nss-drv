/*
 **************************************************************************
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#include <linux/l2tp.h>
#include <net/sock.h>
#include "nss_tx_rx_common.h"

/*
 * Data structures to store l2tpv2 nss debug stats
 */
static DEFINE_SPINLOCK(nss_l2tpv2_session_debug_stats_lock);
static struct nss_stats_l2tpv2_session_debug  nss_l2tpv2_session_debug_stats[NSS_MAX_L2TPV2_DYNAMIC_INTERFACES];

/*
 * nss_l2tpv2_session_debug_stats_sync
 *	Per session debug stats for l2tpv2
 */
void nss_l2tpv2_session_debug_stats_sync(struct nss_ctx_instance *nss_ctx, struct nss_l2tpv2_sync_session_stats_msg *stats_msg, uint16_t if_num)
{
	int i;
	spin_lock_bh(&nss_l2tpv2_session_debug_stats_lock);
	for (i = 0; i < NSS_MAX_L2TPV2_DYNAMIC_INTERFACES; i++) {
		if (nss_l2tpv2_session_debug_stats[i].if_num == if_num) {
			nss_l2tpv2_session_debug_stats[i].stats[NSS_STATS_L2TPV2_SESSION_RX_PPP_LCP_PKTS] += stats_msg->debug_stats.rx_ppp_lcp_pkts;
			nss_l2tpv2_session_debug_stats[i].stats[NSS_STATS_L2TPV2_SESSION_RX_EXP_DATA_PKTS] += stats_msg->debug_stats.rx_exception_data_pkts;
			nss_l2tpv2_session_debug_stats[i].stats[NSS_STATS_L2TPV2_SESSION_ENCAP_PBUF_ALLOC_FAIL_PKTS] += stats_msg->debug_stats.encap_pbuf_alloc_fail;
			nss_l2tpv2_session_debug_stats[i].stats[NSS_STATS_L2TPV2_SESSION_DECAP_PBUF_ALLOC_FAIL_PKTS] += stats_msg->debug_stats.decap_pbuf_alloc_fail;
			break;
		}
	}
	spin_unlock_bh(&nss_l2tpv2_session_debug_stats_lock);
}

/*
 * nss_l2tpv2_global_session_stats_get()
 *	Get session l2tpv2 statitics.
 */
void nss_l2tpv2_session_debug_stats_get(void *stats_mem)
{
	struct nss_stats_l2tpv2_session_debug *stats = (struct nss_stats_l2tpv2_session_debug *)stats_mem;
	int i;

	if (!stats) {
		nss_warning("No memory to copy l2tpv2 session stats");
		return;
	}

	spin_lock_bh(&nss_l2tpv2_session_debug_stats_lock);
	for (i = 0; i < NSS_MAX_L2TPV2_DYNAMIC_INTERFACES; i++) {
		if (nss_l2tpv2_session_debug_stats[i].valid) {
			memcpy(stats, &nss_l2tpv2_session_debug_stats[i], sizeof(struct nss_stats_l2tpv2_session_debug));
			stats++;
		}
	}
	spin_unlock_bh(&nss_l2tpv2_session_debug_stats_lock);
}

/*
 * nss_l2tpv2_handler()
 *	Handle NSS -> HLOS messages for l2tpv2 tunnel
 */

static void nss_l2tpv2_handler(struct nss_ctx_instance *nss_ctx, struct nss_cmn_msg *ncm, __attribute__((unused))void *app_data)
{
	struct nss_l2tpv2_msg *ntm = (struct nss_l2tpv2_msg *)ncm;
	void *ctx;

	nss_l2tpv2_msg_callback_t cb;

	BUG_ON(!(nss_is_dynamic_interface(ncm->interface) || ncm->interface == NSS_L2TPV2_INTERFACE));

	/*
	 * Is this a valid request/response packet?
	 */
	if (ncm->type >= NSS_L2TPV2_MSG_MAX) {
		nss_warning("%p: received invalid message %d for L2TP interface", nss_ctx, ncm->type);
		return;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_l2tpv2_msg)) {
		nss_warning("%p: message length is invalid: %d", nss_ctx, nss_cmn_get_msg_len(ncm));
		return;
	}

	switch (ntm->cm.type) {

	case NSS_L2TPV2_MSG_SYNC_STATS:
		/*
		 * session debug stats embeded in session stats msg
		 */
		nss_l2tpv2_session_debug_stats_sync(nss_ctx, &ntm->msg.stats, ncm->interface);
		break;
	}

	/*
	 * Update the callback and app_data for NOTIFY messages, l2tpv2 sends all notify messages
	 * to the same callback/app_data.
	 */
	if (ncm->response == NSS_CMM_RESPONSE_NOTIFY) {
		ncm->cb = (uint32_t)nss_ctx->nss_top->l2tpv2_msg_callback;
	}

	/*
	 * Log failures
	 */
	nss_core_log_msg_failures(nss_ctx, ncm);

	/*
	 * Do we have a call back
	 */
	if (!ncm->cb) {
		return;
	}

	/*
	 * callback
	 */
	cb = (nss_l2tpv2_msg_callback_t)ncm->cb;
	ctx =  nss_ctx->nss_top->subsys_dp_register[ncm->interface].ndev;

	/*
	 * call l2tpv2 tunnel callback
	 */
	if (!ctx) {
		nss_warning("%p: Event received for l2tpv2 tunnel interface %d before registration", nss_ctx, ncm->interface);
		return;
	}

	cb(ctx, ntm);
}

/*
 * nss_l2tpv2_tx()
 *	Transmit a l2tpv2 message to NSS firmware
 */
nss_tx_status_t nss_l2tpv2_tx(struct nss_ctx_instance *nss_ctx, struct nss_l2tpv2_msg *msg)
{
	struct nss_l2tpv2_msg *nm;
	struct nss_cmn_msg *ncm = &msg->cm;
	struct sk_buff *nbuf;
	int32_t status;

	NSS_VERIFY_CTX_MAGIC(nss_ctx);
	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: l2tpv2 msg dropped as core not ready", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	/*
	 * Sanity check the message
	 */
	if (!nss_is_dynamic_interface(ncm->interface)) {
		nss_warning("%p: tx request for non dynamic interface: %d", nss_ctx, ncm->interface);
		return NSS_TX_FAILURE;
	}

	if (ncm->type > NSS_L2TPV2_MSG_MAX) {
		nss_warning("%p: message type out of range: %d", nss_ctx, ncm->type);
		return NSS_TX_FAILURE;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_l2tpv2_msg)) {
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
	nm = (struct nss_l2tpv2_msg *)skb_put(nbuf, sizeof(struct nss_l2tpv2_msg));
	memcpy(nm, msg, sizeof(struct nss_l2tpv2_msg));

	status = nss_core_send_buffer(nss_ctx, 0, nbuf, NSS_IF_CMD_QUEUE, H2N_BUFFER_CTRL, 0);
	if (status != NSS_CORE_STATUS_SUCCESS) {
		dev_kfree_skb_any(nbuf);
		nss_warning("%p: Unable to enqueue 'l2tpv2 message'\n", nss_ctx);
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

/*
 ***********************************
 * Register/Unregister/Miscellaneous APIs
 ***********************************
 */

/*
 * nss_register_l2tpv2_if()
 */
struct nss_ctx_instance *nss_register_l2tpv2_if(uint32_t if_num, nss_l2tpv2_callback_t l2tpv2_callback,
			nss_l2tpv2_msg_callback_t event_callback, struct net_device *netdev, uint32_t features)
{

	int i = 0;
	nss_assert(nss_is_dynamic_interface(if_num));

	nss_top_main.subsys_dp_register[if_num].ndev = netdev;
	nss_top_main.subsys_dp_register[if_num].cb = l2tpv2_callback;
	nss_top_main.subsys_dp_register[if_num].app_data = NULL;
	nss_top_main.subsys_dp_register[if_num].features = features;

	nss_top_main.l2tpv2_msg_callback = event_callback;

	nss_core_register_handler(if_num, nss_l2tpv2_handler, NULL);

	spin_lock_bh(&nss_l2tpv2_session_debug_stats_lock);
	for (i = 0; i < NSS_MAX_L2TPV2_DYNAMIC_INTERFACES; i++) {
		if (!nss_l2tpv2_session_debug_stats[i].valid) {
			nss_l2tpv2_session_debug_stats[i].valid = true;
			nss_l2tpv2_session_debug_stats[i].if_num = if_num;
			nss_l2tpv2_session_debug_stats[i].if_index = netdev->ifindex;
			break;
		}
	}
	spin_unlock_bh(&nss_l2tpv2_session_debug_stats_lock);

	return (struct nss_ctx_instance *)&nss_top_main.nss[nss_top_main.l2tpv2_handler_id];
}

/*
 * nss_unregister_l2tpv2_if()
 */
void nss_unregister_l2tpv2_if(uint32_t if_num)
{
	int i;
	nss_assert(nss_is_dynamic_interface(if_num));

	nss_top_main.subsys_dp_register[if_num].ndev = NULL;
	nss_top_main.subsys_dp_register[if_num].cb = NULL;
	nss_top_main.subsys_dp_register[if_num].app_data = NULL;
	nss_top_main.subsys_dp_register[if_num].features = 0;

	nss_top_main.l2tpv2_msg_callback = NULL;

	nss_core_unregister_handler(if_num);

	spin_lock_bh(&nss_l2tpv2_session_debug_stats_lock);
	for (i = 0; i < NSS_MAX_L2TPV2_DYNAMIC_INTERFACES; i++) {
		if (nss_l2tpv2_session_debug_stats[i].if_num == if_num) {
			memset(&nss_l2tpv2_session_debug_stats[i], 0, sizeof(struct nss_stats_l2tpv2_session_debug));
			break;
		}
	}
	spin_unlock_bh(&nss_l2tpv2_session_debug_stats_lock);
}

/*
 * nss_get_l2tpv2_context()
 */
struct nss_ctx_instance *nss_l2tpv2_get_context()
{
	return (struct nss_ctx_instance *)&nss_top_main.nss[nss_top_main.l2tpv2_handler_id];
}

/*
 * nss_l2tpv2_msg_init()
 *      Initialize nss_l2tpv2 msg.
 */
void nss_l2tpv2_msg_init(struct nss_l2tpv2_msg *ncm, uint16_t if_num, uint32_t type,  uint32_t len, void *cb, void *app_data)
{
	nss_cmn_msg_init(&ncm->cm, if_num, type, len, cb, app_data);
}

/* nss_l2tpv2_register_handler()
 *   debugfs stats msg handler received on static l2tpv2 interface
 */
void nss_l2tpv2_register_handler(void)
{
	nss_info("nss_l2tpv2_register_handler");
	nss_core_register_handler(NSS_L2TPV2_INTERFACE, nss_l2tpv2_handler, NULL);
}

EXPORT_SYMBOL(nss_l2tpv2_get_context);
EXPORT_SYMBOL(nss_l2tpv2_tx);
EXPORT_SYMBOL(nss_unregister_l2tpv2_if);
EXPORT_SYMBOL(nss_l2tpv2_msg_init);
EXPORT_SYMBOL(nss_register_l2tpv2_if);
