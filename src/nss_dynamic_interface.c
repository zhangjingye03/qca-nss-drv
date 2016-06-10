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

#define NSS_DYNAMIC_INTERFACE_COMP_TIMEOUT 60000	/* 60 Sec */

static struct nss_dynamic_interface_pvt di;

/*
 * nss_dynamic_interface_handler()
 * 	Handle NSS -> HLOS messages for dynamic interfaces
 */
static void nss_dynamic_interface_handler(struct nss_ctx_instance *nss_ctx, struct nss_cmn_msg *ncm, __attribute__((unused))void *app_data)
{
	nss_dynamic_interface_msg_callback_t cb;
	struct nss_dynamic_interface_msg *ndim = (struct nss_dynamic_interface_msg *)ncm;
	int32_t if_num;

	BUG_ON(ncm->interface != NSS_DYNAMIC_INTERFACE);

	/*
	 * Is this a valid request/response packet?
	 */
	if (ncm->type >= NSS_DYNAMIC_INTERFACE_MAX) {
		nss_warning("%p: received invalid message %d for dynamic interface", nss_ctx, ncm->type);
		return;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_dynamic_interface_msg)) {
		nss_warning("%p: Length of message is greater than required: %d", nss_ctx, nss_cmn_get_msg_len(ncm));
		return;
	}

	/*
	 * Log failures
	 */
	nss_core_log_msg_failures(nss_ctx, ncm);

	/*
	 * Handling dynamic interface messages coming from NSS fw.
	 */
	switch (ndim->cm.type) {
	case NSS_DYNAMIC_INTERFACE_ALLOC_NODE:
		if (ncm->response == NSS_CMN_RESPONSE_ACK) {
			nss_info("%p alloc_node response ack if_num %d\n", nss_ctx, ndim->msg.alloc_node.if_num);
			di.current_if_num = ndim->msg.alloc_node.if_num;
			if_num = di.current_if_num;
			if (if_num > 0) {
				di.type[if_num - NSS_DYNAMIC_IF_START] = ndim->msg.alloc_node.type;
			} else {
				nss_warning("%p: if_num < 0\n", nss_ctx);
			}
		}

		break;

	case NSS_DYNAMIC_INTERFACE_DEALLOC_NODE:
		if (ncm->response == NSS_CMN_RESPONSE_ACK) {
			nss_info("%p dealloc_node response ack if_num %d\n", nss_ctx, ndim->msg.dealloc_node.if_num);
			di.current_if_num = ndim->msg.dealloc_node.if_num;
			if_num = di.current_if_num;
			di.type[if_num - NSS_DYNAMIC_IF_START] = NSS_DYNAMIC_INTERFACE_TYPE_NONE;
		}

		break;

	default:
		nss_warning("%p: Received response %d for type %d, interface %d",
				nss_ctx, ncm->response, ncm->type, ncm->interface);
		return;
	}

	/*
	 * Do we have a callback?
	 */
	if (!ncm->cb) {
		nss_warning("%p: nss_dynamic_interface_handler cb is NULL\n", nss_ctx);
		return;
	}

	/*
	 * Callback
	 */
	cb = (nss_dynamic_interface_msg_callback_t)ncm->cb;
	cb((void *)ncm->app_data, ncm);
}

/*
 * nss_dynamic_interface_callback
 *	Callback to handle the message response from NSS FW.
 */
static void nss_dynamic_interface_callback(void *app_data, struct nss_cmn_msg *ncm)
{
	/*
	 * Unblock the sleeping function.
	 */
	di.response = ncm->response;
	complete(&di.complete);
}

/*
 * nss_dynamic_interface_tx_sync()
 *	Send the message to NSS and wait till we get an ACK or NACK for this msg.
 */
static nss_tx_status_t nss_dynamic_interface_tx_sync(struct nss_ctx_instance *nss_ctx, struct nss_dynamic_interface_msg *ndim)
{
	nss_tx_status_t status;
	int ret;

	/*
	 * Acquring a semaphore , so that only one caller can send msg at a time.
	 */
	down(&di.sem);
	di.response = false;

	status = nss_dynamic_interface_tx(nss_ctx, ndim);
	if (status != NSS_TX_SUCCESS) {
		up(&di.sem);
		nss_warning("%p: not able to transmit msg successfully\n", nss_ctx);
		return status;
	}

	/*
	 * Blocking call, wait till we get ACK for this msg.
	 */
	ret = wait_for_completion_timeout(&di.complete, msecs_to_jiffies(NSS_DYNAMIC_INTERFACE_COMP_TIMEOUT));
	if (ret == 0) {
		up(&di.sem);
		nss_warning("%p: Waiting for ack timed out\n", nss_ctx);
		return NSS_TX_FAILURE;
	}

	up(&di.sem);
	return status;
}

/*
 * nss_dynamic_interface_tx()
 * 	Transmit a dynamic interface message to NSSFW, asynchronously.
 */
nss_tx_status_t nss_dynamic_interface_tx(struct nss_ctx_instance *nss_ctx, struct nss_dynamic_interface_msg *msg)
{
	struct nss_dynamic_interface_msg *nm;
	struct nss_cmn_msg *ncm = &msg->cm;
	struct sk_buff *nbuf;
	int32_t status;

	NSS_VERIFY_CTX_MAGIC(nss_ctx);

	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: dynamic if msg dropped as core not ready", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	/*
	 * Sanity check the message
	 */
	if (ncm->interface != NSS_DYNAMIC_INTERFACE) {
		nss_warning("%p: tx request for another interface: %d", nss_ctx, ncm->interface);
		return NSS_TX_FAILURE;
	}

	if (ncm->type > NSS_DYNAMIC_INTERFACE_MAX) {
		nss_warning("%p: message type out of range: %d", nss_ctx, ncm->type);
		return NSS_TX_FAILURE;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_dynamic_interface_msg)) {
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
	nm = (struct nss_dynamic_interface_msg *)skb_put(nbuf, sizeof(struct nss_dynamic_interface_msg));
	memcpy(nm, msg, sizeof(struct nss_dynamic_interface_msg));

	status = nss_core_send_buffer(nss_ctx, 0, nbuf, NSS_IF_CMD_QUEUE, H2N_BUFFER_CTRL, 0);
	if (status != NSS_CORE_STATUS_SUCCESS) {
		dev_kfree_skb_any(nbuf);
		nss_warning("%p: Unable to enqueue 'dynamic if message' \n", nss_ctx);
		return NSS_TX_FAILURE;
	}

	nss_hal_send_interrupt(nss_ctx->nmap, nss_ctx->h2n_desc_rings[NSS_IF_CMD_QUEUE].desc_ring.int_bit,
				NSS_REGS_H2N_INTR_STATUS_DATA_COMMAND_QUEUE);

	NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_TX_CMD_REQ]);
	return NSS_TX_SUCCESS;
}

/*
 * nss_dynamic_interface_alloc_node()
 *	Allocates node of perticular type on NSS and returns interface_num for this node or -1 in case of failure.
 *
 * Note: This function should not be called from soft_irq or interrupt context because it blocks till ACK/NACK is
 * received for the message sent to NSS.
 */
int nss_dynamic_interface_alloc_node(enum nss_dynamic_interface_type type)
{
	struct nss_ctx_instance *nss_ctx = NULL;
	struct nss_dynamic_interface_msg ndim;
	struct nss_dynamic_interface_alloc_node_msg *ndia;
	uint32_t core_id;
	nss_tx_status_t status;

	if (type >= NSS_DYNAMIC_INTERFACE_TYPE_MAX) {
		nss_warning("Dynamic if msg drooped as type is wrong %d\n", type);
		return -1;
	}

	core_id = nss_top_main.dynamic_interface_table[type];
	nss_ctx = (struct nss_ctx_instance *)&nss_top_main.nss[core_id];

	nss_dynamic_interface_msg_init(&ndim, NSS_DYNAMIC_INTERFACE, NSS_DYNAMIC_INTERFACE_ALLOC_NODE,
				sizeof(struct nss_dynamic_interface_alloc_node_msg), nss_dynamic_interface_callback, (void *)nss_ctx);

	ndia = &ndim.msg.alloc_node;
	ndia->type = type;

	/*
	 * Initialize if_num to -1. The allocated if_num is returned by the firmware
	 * in the response message.
	 */
	ndia->if_num = -1;

	/*
	 * Calling synchronous transmit function.
	 */
	status = nss_dynamic_interface_tx_sync(nss_ctx, &ndim);
	if (status != NSS_TX_SUCCESS) {
		nss_warning("%p not able to transmit alloc node msg\n", nss_ctx);
		return -1;
	}

	/*
	 * Check di.response and return -1 if its a NACK else proceed.
	 */
	if (di.response != NSS_CMN_RESPONSE_ACK) {
		nss_warning("%p Received NACK from NSS\n", nss_ctx);
		return -1;
	}

	return di.current_if_num;
}

/*
 * nss_dynamic_interface_dealloc_node()
 *	Deallocate node of particular type and if_num in NSS.
 *
 * Note: This will just mark the state of node as not active, actual memory will be freed when reference count of that node becomes 0.
 * This function should not be called from soft_irq or interrupt context because it blocks till ACK/NACK is received for the message
 * sent to NSS.
 */
nss_tx_status_t nss_dynamic_interface_dealloc_node(int if_num, enum nss_dynamic_interface_type type)
{
	struct nss_ctx_instance *nss_ctx = NULL;
	struct nss_dynamic_interface_msg ndim;
	struct nss_dynamic_interface_dealloc_node_msg *ndid;
	uint32_t core_id;
	nss_tx_status_t status;

	if (type >= NSS_DYNAMIC_INTERFACE_TYPE_MAX) {
		nss_warning("Dynamic if msg dropped as type is wrong type %d if_num %d\n", type, if_num);
		return NSS_TX_FAILURE_BAD_PARAM;
	}

	core_id = nss_top_main.dynamic_interface_table[type];
	nss_ctx = (struct nss_ctx_instance *)&nss_top_main.nss[core_id];

	if (nss_is_dynamic_interface(if_num) == false) {
		nss_warning("%p: nss_dynamic_interface if_num is not in range %d\n", nss_ctx, if_num);
		return NSS_TX_FAILURE_BAD_PARAM;
	}

	nss_dynamic_interface_msg_init(&ndim, NSS_DYNAMIC_INTERFACE, NSS_DYNAMIC_INTERFACE_DEALLOC_NODE,
				sizeof(struct nss_dynamic_interface_dealloc_node_msg), nss_dynamic_interface_callback, (void *)nss_ctx);

	ndid = &ndim.msg.dealloc_node;
	ndid->type = type;
	ndid->if_num = if_num;

	/*
	 * Calling synchronous transmit function.
	 */
	status = nss_dynamic_interface_tx_sync(nss_ctx, &ndim);
	if (status != NSS_TX_SUCCESS) {
		nss_warning("%p not able to transmit alloc node msg\n", nss_ctx);
		return status;
	}

	if (di.response != NSS_CMN_RESPONSE_ACK) {
		nss_warning("%p Received NACK from NSS\n", nss_ctx);
		return -1;
	}

	return status;
}

/*
 * nss_dynamic_interface_register_handler()
 */
void nss_dynamic_interface_register_handler(void)
{
	nss_core_register_handler(NSS_DYNAMIC_INTERFACE, nss_dynamic_interface_handler, NULL);

	sema_init(&di.sem, 1);
	init_completion(&di.complete);
	di.current_if_num = -1;
}

/*
 * nss_is_dynamic_interface()
 *	Judge it is a valid dynamic interface
 */
bool nss_is_dynamic_interface(int if_num)
{
	return (if_num >= NSS_DYNAMIC_IF_START && if_num < NSS_SPECIAL_IF_START);
}

/*
 * nss_dynamic_interface_get_type()
 *	Gets the type of dynamic interface
 */
enum nss_dynamic_interface_type nss_dynamic_interface_get_type(int if_num)
{
	if (nss_is_dynamic_interface(if_num) == false) {
		return NSS_DYNAMIC_INTERFACE_TYPE_NONE;
	}

	return di.type[if_num - NSS_DYNAMIC_IF_START];
}

/*
 * nss_dynamic_interface_msg_init()
 *	Initialize dynamic interface message.
 */
void nss_dynamic_interface_msg_init(struct nss_dynamic_interface_msg *ndm, uint16_t if_num, uint32_t type, uint32_t len,
						void *cb, void *app_data)
{
	nss_cmn_msg_init(&ndm->cm, if_num, type, len, cb, app_data);
}

EXPORT_SYMBOL(nss_dynamic_interface_alloc_node);
EXPORT_SYMBOL(nss_dynamic_interface_dealloc_node);
EXPORT_SYMBOL(nss_is_dynamic_interface);
EXPORT_SYMBOL(nss_dynamic_interface_get_type);
