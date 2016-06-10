/*
 **************************************************************************
 * Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
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
 * nss_pppoe.c
 *	NSS PPPoE APIs
 */

#include "nss_tx_rx_common.h"
#include <linux/if_pppox.h>

/*
 * nss_pppoe_tx()
 *	Transmit an PPPoe message to the FW.
 */
nss_tx_status_t nss_pppoe_tx(struct nss_ctx_instance *nss_ctx, struct nss_pppoe_msg *nim)
{
	struct nss_pppoe_msg *nim2;
	struct nss_cmn_msg *ncm = &nim->cm;
	struct sk_buff *nbuf;
	int32_t status;

	NSS_VERIFY_CTX_MAGIC(nss_ctx);

	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: pppoe msg dropped as core not ready", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	/*
	 * Sanity check the message
	 */
	if (ncm->interface != NSS_PPPOE_RX_INTERFACE) {
		nss_warning("%p: tx request for another interface: %d", nss_ctx, ncm->interface);
		return NSS_TX_FAILURE;
	}

	if (ncm->type > NSS_PPPOE_MAX) {
		nss_warning("%p: message type out of range: %d", nss_ctx, ncm->type);
		return NSS_TX_FAILURE;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_pppoe_msg)) {
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
	 * Copy the message to our skb.
	 */
	nim2 = (struct nss_pppoe_msg *)skb_put(nbuf, sizeof(struct nss_pppoe_msg));
	memcpy(nim2, nim, sizeof(struct nss_pppoe_msg));

	status = nss_core_send_buffer(nss_ctx, 0, nbuf, NSS_IF_CMD_QUEUE, H2N_BUFFER_CTRL, 0);
	if (status != NSS_CORE_STATUS_SUCCESS) {
		dev_kfree_skb_any(nbuf);
		nss_warning("%p: unable to enqueue PPPoE msg\n", nss_ctx);
		return NSS_TX_FAILURE;
	}

	nss_hal_send_interrupt(nss_ctx->nmap, nss_ctx->h2n_desc_rings[NSS_IF_CMD_QUEUE].desc_ring.int_bit,
								NSS_REGS_H2N_INTR_STATUS_DATA_COMMAND_QUEUE);

	NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_TX_CMD_REQ]);

	return NSS_TX_SUCCESS;
}

/*
 **********************************
 Rx APIs
 **********************************
 */

/*
 * nss_pppoe_session_reset()
 * 	Reset PPPoE session when session is destroyed.
 */
static void nss_pppoe_session_reset(struct nss_ctx_instance *nss_ctx, struct nss_pppoe_session_reset_msg *npsr)
{
	uint32_t i;
	uint32_t interface = npsr->interface;
	uint32_t session_index = npsr->session_index;

	/*
	 * Reset the PPPoE statistics for this specific session.
	 */
	spin_lock_bh(&nss_ctx->nss_top->stats_lock);
	for (i = 0; i < NSS_PPPOE_EXCEPTION_EVENT_MAX; i++) {
		nss_ctx->nss_top->stats_if_exception_pppoe[interface][session_index][i] = 0;
	}
	spin_unlock_bh(&nss_ctx->nss_top->stats_lock);
}

/*
 * nss_pppoe_exception_stats_sync()
 *	Handle the syncing of PPPoE exception statistics.
 */
static void nss_pppoe_exception_stats_sync(struct nss_ctx_instance *nss_ctx, struct nss_pppoe_conn_stats_sync_msg *npess)
{
	struct nss_top_instance *nss_top = nss_ctx->nss_top;
	uint32_t index = npess->index;
	uint32_t interface_num = npess->interface_num;
	uint32_t i;

	spin_lock_bh(&nss_top->stats_lock);

	if (interface_num >= NSS_MAX_PHYSICAL_INTERFACES) {
		spin_unlock_bh(&nss_top->stats_lock);
		nss_warning("%p: Incorrect interface number %d for PPPoE exception stats", nss_ctx, interface_num);
		return;
	}

	/*
	 * pppoe exception stats
	 */
	for (i = 0; i < NSS_PPPOE_EXCEPTION_EVENT_MAX; i++) {
		nss_top->stats_if_exception_pppoe[interface_num][index][i] += npess->exception_events_pppoe[i];
	}

	spin_unlock_bh(&nss_top->stats_lock);
}

/*
 * nss_pppoe_node_stats_sync()
 *	Handle the syncing of PPPoE node statistics.
 */
static void nss_pppoe_node_stats_sync(struct nss_ctx_instance *nss_ctx, struct nss_pppoe_node_stats_sync_msg *npess)
{
	struct nss_top_instance *nss_top = nss_ctx->nss_top;

	spin_lock_bh(&nss_top->stats_lock);

	nss_top->stats_node[NSS_PPPOE_RX_INTERFACE][NSS_STATS_NODE_RX_PKTS] += npess->node_stats.rx_packets;
	nss_top->stats_node[NSS_PPPOE_RX_INTERFACE][NSS_STATS_NODE_RX_BYTES] += npess->node_stats.rx_bytes;
	nss_top->stats_node[NSS_PPPOE_RX_INTERFACE][NSS_STATS_NODE_RX_DROPPED] += npess->node_stats.rx_dropped;
	nss_top->stats_node[NSS_PPPOE_RX_INTERFACE][NSS_STATS_NODE_TX_PKTS] += npess->node_stats.tx_packets;
	nss_top->stats_node[NSS_PPPOE_RX_INTERFACE][NSS_STATS_NODE_TX_BYTES] += npess->node_stats.tx_bytes;

	nss_top->stats_pppoe[NSS_STATS_PPPOE_SESSION_CREATE_REQUESTS] += npess->pppoe_session_create_requests;
	nss_top->stats_pppoe[NSS_STATS_PPPOE_SESSION_CREATE_FAILURES] += npess->pppoe_session_create_failures;
	nss_top->stats_pppoe[NSS_STATS_PPPOE_SESSION_DESTROY_REQUESTS] += npess->pppoe_session_destroy_requests;
	nss_top->stats_pppoe[NSS_STATS_PPPOE_SESSION_DESTROY_REQUESTS] += npess->pppoe_session_destroy_requests;

	spin_unlock_bh(&nss_top->stats_lock);
}

/*
 * nss_pppoe_rx_msg_handler()
 *	Handle NSS -> HLOS messages for PPPoE
 */
static void nss_pppoe_rx_msg_handler(struct nss_ctx_instance *nss_ctx, struct nss_cmn_msg *ncm, __attribute__((unused))void *app_data)
{
	struct nss_pppoe_msg *nim = (struct nss_pppoe_msg *)ncm;

	BUG_ON(ncm->interface != NSS_PPPOE_RX_INTERFACE);

	/*
	 * Sanity check the message type
	 */
	if (ncm->type > NSS_PPPOE_MAX) {
		nss_warning("%p: message type out of range: %d", nss_ctx, ncm->type);
		return;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_pppoe_msg)) {
		nss_warning("%p: message length is invalid: %d", nss_ctx, nss_cmn_get_msg_len(ncm));
		return;
	}

	/*
	 * Log failures
	 */
	nss_core_log_msg_failures(nss_ctx, ncm);

	/*
	 * Handling PPPoE messages coming from NSS fw.
	 */
	switch (nim->cm.type) {
	case NSS_PPPOE_RX_NODE_STATS_SYNC:
		nss_pppoe_node_stats_sync(nss_ctx, &nim->msg.pppoe_node_stats_sync);
		break;
	case NSS_PPPOE_RX_CONN_STATS_SYNC:
		nss_pppoe_exception_stats_sync(nss_ctx, &nim->msg.pppoe_conn_stats_sync);
		break;
	case NSS_PPPOE_RX_SESSION_RESET:
		nss_pppoe_session_reset(nss_ctx, &nim->msg.pppoe_session_reset);
		break;
	default:
		nss_warning("%p: Received response %d for type %d, interface %d",
				nss_ctx, ncm->response, ncm->type, ncm->interface);
	}
}

/*
 * nss_pppoe_register_handler()
 */
void nss_pppoe_register_handler()
{
	nss_core_register_handler(NSS_PPPOE_RX_INTERFACE, nss_pppoe_rx_msg_handler, NULL);
}

/*
 * nss_pppoe_msg_init()
 *	Initialize pppoe message.
 */
void nss_pppoe_msg_init(struct nss_pppoe_msg *npm, uint16_t if_num, uint32_t type, uint32_t len,
                         void *cb, void *app_data)
{
	nss_cmn_msg_init(&npm->cm, if_num, type, len, (void *)cb, app_data);
}

