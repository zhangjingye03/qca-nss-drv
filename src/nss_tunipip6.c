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

#include "nss_tx_rx_common.h"

/*
 * nss_tunipip6_handler()
 *	Handle NSS -> HLOS messages for 6rd tunnel
 */
static void nss_tunipip6_handler(struct nss_ctx_instance *nss_ctx, struct nss_cmn_msg *ncm, __attribute__((unused))void *app_data)
{
	struct nss_tunipip6_msg *ntm = (struct nss_tunipip6_msg *)ncm;
	void *ctx;
	nss_tunipip6_msg_callback_t cb;

	BUG_ON(ncm->interface != NSS_TUNIPIP6_INTERFACE);
	/*
	 * Is this a valid request/response packet?
	 */
	if (ncm->type >= NSS_TUNIPIP6_MAX) {
		nss_warning("%p: received invalid message %d for DS-Lite interface", nss_ctx, ncm->type);
		return;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_tunipip6_msg)) {
		nss_warning("%p: Length of message is greater than required: %d", nss_ctx, nss_cmn_get_msg_len(ncm));
		return;
	}

	/*
	 * Update the callback and app_data for NOTIFY messages, tun6rd sends all notify messages
	 * to the same callback/app_data.
	 */
	if (ncm->response == NSS_CMM_RESPONSE_NOTIFY) {
		ncm->cb = (uint32_t)nss_ctx->nss_top->tunipip6_msg_callback;
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
	cb = (nss_tunipip6_msg_callback_t)ncm->cb;
	ctx =  nss_ctx->nss_top->subsys_dp_register[ncm->interface].ndev;

	/*
	 * call ipip6 tunnel callback
	 */
	if (!ctx) {
		 nss_warning("%p: Event received for DS-Lite tunnel interface %d before registration", nss_ctx, ncm->interface);
		return;
	}

	cb(ctx, ntm);
}

/*
 * nss_tunipip6_tx()
 * 	Transmit a tun6rd message to NSSFW
 */
nss_tx_status_t nss_tunipip6_tx(struct nss_ctx_instance *nss_ctx, struct nss_tunipip6_msg *msg)
{
	struct nss_tunipip6_msg *nm;
	struct nss_cmn_msg *ncm = &msg->cm;
	struct sk_buff *nbuf;
	int32_t status;

	NSS_VERIFY_CTX_MAGIC(nss_ctx);
	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: tun6rd msg dropped as core not ready", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	/*
	 * Sanity check the message
	 */
	if (ncm->interface != NSS_TUNIPIP6_INTERFACE) {
		nss_warning("%p: tx request for another interface: %d", nss_ctx, ncm->interface);
		return NSS_TX_FAILURE;
	}

	if (ncm->type > NSS_TUNIPIP6_MAX) {
		nss_warning("%p: message type out of range: %d", nss_ctx, ncm->type);
		return NSS_TX_FAILURE;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_tunipip6_msg)) {
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
	nm = (struct nss_tunipip6_msg *)skb_put(nbuf, sizeof(struct nss_tunipip6_msg));
	memcpy(nm, msg, sizeof(struct nss_tunipip6_msg));

	status = nss_core_send_buffer(nss_ctx, 0, nbuf, NSS_IF_CMD_QUEUE, H2N_BUFFER_CTRL, 0);
	if (status != NSS_CORE_STATUS_SUCCESS) {
		dev_kfree_skb_any(nbuf);
		nss_warning("%p: Unable to enqueue 'tun6rd message' \n", nss_ctx);
		return NSS_TX_FAILURE;
	}

	nss_hal_send_interrupt(nss_ctx->nmap, nss_ctx->h2n_desc_rings[NSS_IF_CMD_QUEUE].desc_ring.int_bit,
				NSS_REGS_H2N_INTR_STATUS_DATA_COMMAND_QUEUE);

	NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_TX_CMD_REQ]);
	return NSS_TX_SUCCESS;
}

/*
 * **********************************
 *  Register/Unregister/Miscellaneous APIs
 * **********************************
 */

/*
 * nss_register_tunipip6_if()
 */
struct nss_ctx_instance *nss_register_tunipip6_if(uint32_t if_num,
			nss_tunipip6_callback_t tunipip6_callback,
			nss_tunipip6_msg_callback_t event_callback,
			struct net_device *netdev,
			uint32_t features)
{
	nss_assert((if_num >= NSS_MAX_VIRTUAL_INTERFACES) && (if_num < NSS_MAX_NET_INTERFACES));

	nss_top_main.tunipip6_msg_callback = event_callback;

	nss_top_main.subsys_dp_register[if_num].ndev = netdev;
	nss_top_main.subsys_dp_register[if_num].cb = tunipip6_callback;
	nss_top_main.subsys_dp_register[if_num].app_data = NULL;
	nss_top_main.subsys_dp_register[if_num].features = features;

	return (struct nss_ctx_instance *)&nss_top_main.nss[nss_top_main.tunipip6_handler_id];
}

/*
 * nss_unregister_tunipip6_if()
 */
void nss_unregister_tunipip6_if(uint32_t if_num)
{
	nss_assert((if_num >= NSS_MAX_VIRTUAL_INTERFACES) && (if_num < NSS_MAX_NET_INTERFACES));

	nss_top_main.subsys_dp_register[if_num].cb = NULL;
	nss_top_main.subsys_dp_register[if_num].ndev = NULL;
	nss_top_main.subsys_dp_register[if_num].features = 0;
	nss_top_main.tunipip6_msg_callback = NULL;
}

/*
 * nss_tunipip6_register_handler()
 */
void nss_tunipip6_register_handler()
{
	nss_core_register_handler(NSS_TUNIPIP6_INTERFACE, nss_tunipip6_handler, NULL);
}

/*
 * nss_tunipip6_msg_init()
 *      Initialize nss_tunipip6 msg.
 */
void nss_tunipip6_msg_init(struct nss_tunipip6_msg *ntm, uint16_t if_num, uint32_t type,  uint32_t len, void *cb, void *app_data)
{
	nss_cmn_msg_init(&ntm->cm, if_num, type, len, cb, app_data);
}

EXPORT_SYMBOL(nss_tunipip6_tx);
EXPORT_SYMBOL(nss_register_tunipip6_if);
EXPORT_SYMBOL(nss_unregister_tunipip6_if);
EXPORT_SYMBOL(nss_tunipip6_msg_init);
