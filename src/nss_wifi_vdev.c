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

#include "nss_tx_rx_common.h"

/*
 * nss_wifi_vdev_handler()
 * 	Handle NSS -> HLOS messages for wifi_vdev
 */
static void nss_wifi_vdev_handler(struct nss_ctx_instance *nss_ctx, struct nss_cmn_msg *ncm, __attribute__((unused))void *app_data)
{
	nss_wifi_vdev_msg_callback_t cb;

	nss_info("%p: NSS->HLOS message for wifi vdev on interface:%d", nss_ctx, ncm->interface);

	BUG_ON(((ncm->interface < NSS_DYNAMIC_IF_START) || (ncm->interface >= (NSS_DYNAMIC_IF_START + NSS_MAX_DYNAMIC_INTERFACES))));

	/*
	 * Is this a valid request/response packet?
	 */
	if (ncm->type >= NSS_WIFI_VDEV_MAX_MSG) {
		nss_warning("%p: received invalid message %d for wifi vdev interface", nss_ctx, ncm->type);
		return;
	}


	/*
	 * Log failures
	 */
	nss_core_log_msg_failures(nss_ctx, ncm);

	/*
	 * callback
	 */
	if (!nss_ctx->nss_top->subsys_dp_register[ncm->interface].ndev) {
		nss_warning("%p: Event received wifi vdev interface %d before registration", nss_ctx, ncm->interface);
		return;

	}

	if (ncm->response == NSS_CMM_RESPONSE_NOTIFY) {
		ncm->cb = (uint32_t)nss_ctx->nss_top->if_rx_msg_callback[ncm->interface];
		ncm->app_data = (uint32_t)nss_ctx->nss_top->subsys_dp_register[ncm->interface].ndev;
	}

	/*
	 * Do we have a callback?
	 */
	if (!ncm->cb) {
		return;
	}

	cb = (nss_wifi_vdev_msg_callback_t)ncm->cb;
	cb((void *)ncm->app_data, ncm);
}

/*
 * nss_wifi_vdev_msg_init()
 *	Initialize wifi message.
 */
void nss_wifi_vdev_msg_init(struct nss_wifi_vdev_msg *nim, uint16_t if_num, uint32_t type, uint32_t len,
			nss_wifi_vdev_msg_callback_t *cb, void *app_data)
{
	nss_cmn_msg_init(&nim->cm, if_num, type, len, (void *)cb, app_data);
}

/*
 * nss_wifi_vdev_tx_msg()
 * 	Transmit a wifi vdev message to NSSFW
 */
nss_tx_status_t nss_wifi_vdev_tx_msg(struct nss_ctx_instance *nss_ctx, struct nss_wifi_vdev_msg *msg)
{
	struct nss_wifi_vdev_msg *nm;
	struct nss_cmn_msg *ncm = &msg->cm;
	struct sk_buff *nbuf;
	bool status;

	nss_trace("%p: Sending wifi vdev message on interface :%d", nss_ctx, ncm->interface);

	NSS_VERIFY_CTX_MAGIC(nss_ctx);

	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: wifi vdev message dropped as core not ready", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	/*
	 * Sanity checks on the message
	 */

	/*
	 * Interface shall be of dynamic interface type
	 */
	 if ((ncm->interface < NSS_DYNAMIC_IF_START) || (ncm->interface >= (NSS_DYNAMIC_IF_START + NSS_MAX_DYNAMIC_INTERFACES))) {
		nss_warning("%p: wifi vdev tx request for invalid interface: %d", nss_ctx, ncm->interface);
		return NSS_TX_FAILURE;
	}

	if (ncm->type > NSS_WIFI_VDEV_MAX_MSG) {
		nss_warning("%p: wifi vdev message type out of range: %d", nss_ctx, ncm->type);
		return NSS_TX_FAILURE;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_wifi_vdev_msg)) {
		nss_warning("%p: wifi vdev message length is invalid: %d", nss_ctx, nss_cmn_get_msg_len(ncm));
		return NSS_TX_FAILURE;
	}

	nbuf = dev_alloc_skb(NSS_NBUF_PAYLOAD_SIZE);
	if (unlikely(!nbuf)) {
		spin_lock_bh(&nss_ctx->nss_top->stats_lock);
		NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_NBUF_ALLOC_FAILS]);
		spin_unlock_bh(&nss_ctx->nss_top->stats_lock);
		nss_warning("%p: wifi vdev message dropped as command allocation failed", nss_ctx);
		return NSS_TX_FAILURE;
	}

	/*
	 * Copy the message to our skb
	 */
	nm = (struct nss_wifi_vdev_msg *)skb_put(nbuf, sizeof(struct nss_wifi_vdev_msg));
	memcpy(nm, msg, sizeof(struct nss_wifi_vdev_msg));

	status = nss_core_send_buffer(nss_ctx, 0, nbuf, NSS_IF_CMD_QUEUE, H2N_BUFFER_CTRL, 0);
	if (status != NSS_CORE_STATUS_SUCCESS) {
		dev_kfree_skb_any(nbuf);
		nss_warning("%p: Unable to enqueue 'wifi vdev message'", nss_ctx);
		return NSS_TX_FAILURE;
	}

	nss_hal_send_interrupt(nss_ctx->nmap, nss_ctx->h2n_desc_rings[NSS_IF_CMD_QUEUE].desc_ring.int_bit,
				NSS_REGS_H2N_INTR_STATUS_DATA_COMMAND_QUEUE);

	NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_TX_CMD_REQ]);

	return status;
}

/*
 * nss_wifi_vdev_tx_msg_ext()
 * 	Send special data packet with metadata for vap processing
 */
nss_tx_status_t nss_wifi_vdev_tx_msg_ext(struct nss_ctx_instance *nss_ctx, struct sk_buff *os_buf)
{
	struct nss_wifi_vdev_msg *nm;
	struct nss_cmn_msg *ncm;
	nss_tx_status_t status;

	NSS_VERIFY_CTX_MAGIC(nss_ctx);

	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: wifi vdev message dropped as core not ready", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	nm = (struct nss_wifi_vdev_msg *) os_buf->data;
	ncm = &nm->cm;

	nss_trace("%p: Sending wifi vdev message on interface :%d", nss_ctx, ncm->interface);

	/*
	 * Interface shall be of dynamic interface type
	 */
	if ((ncm->interface < NSS_DYNAMIC_IF_START) || (ncm->interface >= (NSS_DYNAMIC_IF_START + NSS_MAX_DYNAMIC_INTERFACES))) {
		nss_warning("%p: wifi vdev tx request for invalid interface: %d", nss_ctx, ncm->interface);
		return NSS_TX_FAILURE;
	}

	if (ncm->type > NSS_WIFI_VDEV_MAX_MSG) {
		nss_warning("%p: wifi vdev message type out of range: %d", nss_ctx, ncm->type);
		return NSS_TX_FAILURE;
	}

	status = nss_core_send_buffer(nss_ctx, 0, os_buf, NSS_IF_CMD_QUEUE, H2N_BUFFER_CTRL, 0);
	if (status != NSS_CORE_STATUS_SUCCESS) {
		nss_warning("%p: Unable to enqueue 'wifi vdev message'", nss_ctx);
		return NSS_TX_FAILURE;
	}

	nss_hal_send_interrupt(nss_ctx->nmap, nss_ctx->h2n_desc_rings[NSS_IF_CMD_QUEUE].desc_ring.int_bit,
			NSS_REGS_H2N_INTR_STATUS_DATA_COMMAND_QUEUE);

	NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_TX_CMD_REQ]);

	return status;
}

/*
 * nss_wifi_vdev_tx_buf
 * 	Send data packet for vap processing
 */
nss_tx_status_t nss_wifi_vdev_tx_buf(struct nss_ctx_instance *nss_ctx, struct sk_buff *os_buf, uint32_t if_num)
{
	int32_t status;
	uint16_t int_bit = nss_ctx->h2n_desc_rings[NSS_IF_DATA_QUEUE_0].desc_ring.int_bit;

	NSS_VERIFY_CTX_MAGIC(nss_ctx);

	BUG_ON(((if_num < NSS_DYNAMIC_IF_START) || (if_num >= (NSS_DYNAMIC_IF_START + NSS_MAX_DYNAMIC_INTERFACES))));

	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: 'NSS WIFI VAP If Tx' packet dropped as core not ready", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	status = nss_core_send_buffer(nss_ctx, if_num, os_buf, NSS_IF_DATA_QUEUE_0, H2N_BUFFER_PACKET, 0);
	if (unlikely(status != NSS_CORE_STATUS_SUCCESS)) {
		nss_warning("%p: Unable to enqueue 'nss wifi vdev if tx' packet", nss_ctx);
		if (status == NSS_CORE_STATUS_FAILURE_QUEUE) {
			return NSS_TX_FAILURE_QUEUE;
		}

		return NSS_TX_FAILURE;
	}

	/*
	 * Kick the NSS awake so it can process our new entry.
	 */
	nss_hal_send_interrupt(nss_ctx->nmap, int_bit, NSS_REGS_H2N_INTR_STATUS_DATA_COMMAND_QUEUE);

	NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_TX_PACKET]);

	return NSS_TX_SUCCESS;
}

/*
 ***********************************
 * Register/Unregister/Miscellaneous APIs
 ***********************************
 */

/*
 * nss_register_wifi_vdev_if()
 */
uint32_t nss_register_wifi_vdev_if(struct nss_ctx_instance *nss_ctx,
				int32_t if_num,
				nss_wifi_vdev_callback_t vdev_data_callback,
				nss_wifi_vdev_ext_data_callback_t vdev_ext_data_callback,
				nss_wifi_vdev_msg_callback_t vdev_event_callback,
				struct net_device *netdev,
				uint32_t features)
{
	nss_assert((if_num >= NSS_DYNAMIC_IF_START) && (if_num < (NSS_DYNAMIC_IF_START + NSS_MAX_DYNAMIC_INTERFACES)));

	nss_top_main.subsys_dp_register[if_num].ndev = netdev;
	nss_top_main.subsys_dp_register[if_num].cb = vdev_data_callback;
	nss_top_main.subsys_dp_register[if_num].ext_cb = vdev_ext_data_callback;
	nss_top_main.subsys_dp_register[if_num].app_data = NULL;
	nss_top_main.subsys_dp_register[if_num].features = features;

	nss_top_main.if_rx_msg_callback[if_num] = vdev_event_callback;

	nss_core_register_handler(if_num, nss_wifi_vdev_handler, NULL);

	return NSS_CORE_STATUS_SUCCESS;
}

/*
 * nss_unregister_wifi_vdev_if()
 */
void nss_unregister_wifi_vdev_if(uint32_t if_num)
{
	nss_assert((if_num >= NSS_DYNAMIC_IF_START) && (if_num < (NSS_DYNAMIC_IF_START + NSS_MAX_DYNAMIC_INTERFACES)));

	nss_top_main.subsys_dp_register[if_num].ndev = NULL;
	nss_top_main.subsys_dp_register[if_num].cb = NULL;
	nss_top_main.subsys_dp_register[if_num].ext_cb = NULL;
	nss_top_main.subsys_dp_register[if_num].app_data = NULL;
	nss_top_main.subsys_dp_register[if_num].features = 0;

	nss_top_main.if_rx_msg_callback[if_num] = NULL;

	nss_core_unregister_handler(if_num);
}

EXPORT_SYMBOL(nss_wifi_vdev_tx_msg_ext);
EXPORT_SYMBOL(nss_wifi_vdev_msg_init);
EXPORT_SYMBOL(nss_wifi_vdev_tx_msg);
EXPORT_SYMBOL(nss_wifi_vdev_tx_buf);
EXPORT_SYMBOL(nss_register_wifi_vdev_if);
EXPORT_SYMBOL(nss_unregister_wifi_vdev_if);
