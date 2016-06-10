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

/*
 * nss_wifi_if.c
 *	NSS wifi/redirect handler APIs
 */

#include "nss_tx_rx_common.h"
#include <net/arp.h>

#define NSS_WIFI_IF_TX_TIMEOUT			3000 /* 3 Seconds */
#define NSS_WIFI_IF_GET_INDEX(if_num)	(if_num-NSS_DYNAMIC_IF_START)

extern int nss_ctl_redirect;

/*
 * Data structure that holds the wifi interface context.
 */
static struct nss_wifi_if_handle *wifi_handle[NSS_MAX_DYNAMIC_INTERFACES];

/*
 * Spinlock to protect the global data structure wifi_handle.
 */
DEFINE_SPINLOCK(wifi_if_lock);

/*
 * nss_wifi_if_stats_sync()
 *	Sync stats from the NSS FW
 */
static void nss_wifi_if_stats_sync(struct nss_wifi_if_handle *handle,
					struct nss_wifi_if_stats *nwis)
{
	struct nss_wifi_if_stats *stats = &handle->stats;

	stats->node_stats.rx_packets += nwis->node_stats.rx_packets;
	stats->node_stats.rx_bytes += nwis->node_stats.rx_bytes;
	stats->node_stats.rx_dropped += nwis->node_stats.rx_dropped;
	stats->node_stats.tx_packets += nwis->node_stats.tx_packets;
	stats->node_stats.tx_bytes += nwis->node_stats.tx_bytes;
	stats->tx_enqueue_failed += nwis->tx_enqueue_failed;
	stats->shaper_enqueue_failed += nwis->shaper_enqueue_failed;
}

/*
 * nss_wifi_if_msg_handler()
 *	Handle NSS -> HLOS messages for wifi interface
 */
static void nss_wifi_if_msg_handler(struct nss_ctx_instance *nss_ctx,
					struct nss_cmn_msg *ncm,
					__attribute__((unused))void *app_data)
{
	struct nss_wifi_if_msg *nwim = (struct nss_wifi_if_msg *)ncm;
	int32_t if_num;

	nss_wifi_if_msg_callback_t cb;
	struct nss_wifi_if_handle *handle = NULL;

	/*
	 * Sanity check the message type
	 */
	if (ncm->type >= NSS_WIFI_IF_MAX_MSG_TYPES) {
		nss_warning("%p: message type out of range: %d",
						nss_ctx, ncm->type);
		return;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_wifi_if_msg)) {
		nss_warning("%p: Length of message is greater than required: %d", nss_ctx, nss_cmn_get_msg_len(ncm));
		return;
	}

	if (!NSS_IS_IF_TYPE(DYNAMIC, ncm->interface)) {
		nss_warning("%p: response for another interface: %d", nss_ctx, ncm->interface);
		return;
	}

	/*
	 * Log failures
	 */
	nss_core_log_msg_failures(nss_ctx, ncm);

	if_num = NSS_WIFI_IF_GET_INDEX(ncm->interface);

	spin_lock_bh(&wifi_if_lock);
	if (!wifi_handle[if_num]) {
		spin_unlock_bh(&wifi_if_lock);
		nss_warning("%p: wifi_if handle is NULL\n", nss_ctx);
		return;
	}

	handle = wifi_handle[if_num];
	spin_unlock_bh(&wifi_if_lock);

	switch (nwim->cm.type) {
	case NSS_WIFI_IF_STATS_SYNC_MSG:
		nss_wifi_if_stats_sync(handle, &nwim->msg.stats);
		break;
	}

	/*
	 * Update the callback and app_data for NOTIFY messages.
	 */
	if (nwim->cm.response == NSS_CMM_RESPONSE_NOTIFY) {
		ncm->cb = (uint32_t)handle->cb;
		ncm->app_data = (uint32_t)handle->app_data;
	}

	/*
	 * Do we have a callback?
	 */
	if (!ncm->cb) {
		nss_warning("cb is NULL\n");
		return;
	}

	/*
	 * Callback
	 */
	cb = (nss_wifi_if_msg_callback_t)ncm->cb;
	cb((void *)ncm->app_data, ncm);
}

/*
 * nss_wifi_if_register_handler()
 *	Register the message handler & initialize semaphore & completion for the * interface if_num
 */
static uint32_t nss_wifi_if_register_handler(struct nss_wifi_if_handle *handle)
{
	uint32_t ret;
	struct nss_wifi_if_pvt *nwip = NULL;
	int32_t if_num = handle->if_num;

	ret = nss_core_register_handler(if_num, nss_wifi_if_msg_handler, NULL);

	if (ret != NSS_CORE_STATUS_SUCCESS) {
		nss_warning("%d: Message handler failed to be registered for interface ret %d\n", if_num, ret);
		return NSS_WIFI_IF_CORE_FAILURE;
	}

	nwip = handle->pvt;
	if (!nwip->sem_init_done) {
		sema_init(&nwip->sem, 1);
		init_completion(&nwip->complete);
		nwip->sem_init_done = 1;
	}

	return NSS_WIFI_IF_SUCCESS;
}

/*
 * nss_wifi_if_callback
 *	Callback to handle the completion of NSS->HLOS messages.
 */
static void nss_wifi_if_callback(void *app_data, struct nss_cmn_msg *ncm)
{
	struct nss_wifi_if_handle *handle = (struct nss_wifi_if_handle *)app_data;
	struct nss_wifi_if_pvt *nwip = handle->pvt;

	if (ncm->response != NSS_CMN_RESPONSE_ACK) {
		nss_warning("%p: wifi_if Error response %d\n",
						handle->nss_ctx, ncm->response);
		nwip->response = NSS_TX_FAILURE;
		complete(&nwip->complete);
		return;
	}

	nwip->response = NSS_TX_SUCCESS;
	complete(&nwip->complete);
}

/*
 * nss_wifi_if_tx_msg()
 *	Send a message from HLOS to NSS asynchronously.
 */
nss_tx_status_t nss_wifi_if_tx_msg(struct nss_ctx_instance *nss_ctx, struct nss_wifi_if_msg *nwim)
{
	int32_t status;
	struct sk_buff *nbuf;
	struct nss_cmn_msg *ncm = &nwim->cm;
	struct nss_wifi_if_msg *nwim2;

	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: Interface could not be created as core not ready\n", nss_ctx);
		return NSS_TX_FAILURE;
	}

	if (ncm->type > NSS_WIFI_IF_MAX_MSG_TYPES) {
		nss_warning("%p: message type out of range: %d\n", nss_ctx, ncm->type);
		return NSS_TX_FAILURE;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_wifi_if_msg)) {
		nss_warning("%p: invalid length: %d. Length of wifi msg is %d\n",
				nss_ctx, nss_cmn_get_msg_len(ncm), sizeof(struct nss_wifi_if_msg));
		return NSS_TX_FAILURE;
	}

	nbuf = dev_alloc_skb(NSS_NBUF_PAYLOAD_SIZE);
	if (unlikely(!nbuf)) {
		NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_NBUF_ALLOC_FAILS]);
		nss_warning("%p: wifi interface %d: command allocation failed\n", nss_ctx, ncm->interface);
		return NSS_TX_FAILURE;
	}

	nwim2 = (struct nss_wifi_if_msg *)skb_put(nbuf, sizeof(struct nss_wifi_if_msg));
	memcpy(nwim2, nwim, sizeof(struct nss_wifi_if_msg));

	status = nss_core_send_buffer(nss_ctx, 0, nbuf, NSS_IF_CMD_QUEUE, H2N_BUFFER_CTRL, 0);
	if (status != NSS_CORE_STATUS_SUCCESS) {
		dev_kfree_skb_any(nbuf);
		nss_warning("%p: Unable to enqueue 'virtual interface' command\n", nss_ctx);
		return NSS_TX_FAILURE;
	}

	nss_hal_send_interrupt(nss_ctx->nmap,
				nss_ctx->h2n_desc_rings[NSS_IF_CMD_QUEUE].desc_ring.int_bit,
				NSS_REGS_H2N_INTR_STATUS_DATA_COMMAND_QUEUE);

	/*
	 * The context returned is the virtual interface # which is, essentially, the index into the if_ctx
	 * array that is holding the net_device pointer
	 */
	return NSS_TX_SUCCESS;
}

/*
 * nss_wifi_if_tx_msg_sync
 *	Send a message from HLOS to NSS synchronously.
 */
static nss_tx_status_t nss_wifi_if_tx_msg_sync(struct nss_wifi_if_handle *handle,
							struct nss_wifi_if_msg *nwim)
{
	nss_tx_status_t status;
	int ret = 0;
	struct nss_wifi_if_pvt *nwip = handle->pvt;
	struct nss_ctx_instance *nss_ctx = handle->nss_ctx;

	down(&nwip->sem);

	status = nss_wifi_if_tx_msg(nss_ctx, nwim);
	if (status != NSS_TX_SUCCESS) {
		nss_warning("%p: nss_wifi_if_msg failed\n", nss_ctx);
		up(&nwip->sem);
		return status;
	}

	ret = wait_for_completion_timeout(&nwip->complete,
						msecs_to_jiffies(NSS_WIFI_IF_TX_TIMEOUT));
	if (!ret) {
		nss_warning("%p: wifi_if tx failed due to timeout\n", nss_ctx);
		nwip->response = NSS_TX_FAILURE;
	}

	status = nwip->response;
	up(&nwip->sem);

	return status;
}

/*
 * nss_wifi_if_handle_destroy()
 *	Destroy the wifi handle either due to request from WLAN or due to error.
 */
static int nss_wifi_if_handle_destroy(struct nss_wifi_if_handle *handle)
{
	int32_t if_num;
	int32_t index;
	struct nss_ctx_instance *nss_ctx;
	nss_tx_status_t status;

	if (!handle) {
		nss_warning("Destroy failed as wifi_if handle is NULL\n");
		return NSS_TX_FAILURE_BAD_PARAM;
	}

	if_num = handle->if_num;
	index = NSS_WIFI_IF_GET_INDEX(if_num);
	nss_ctx = handle->nss_ctx;

	spin_lock_bh(&wifi_if_lock);
	wifi_handle[index] = NULL;
	spin_unlock_bh(&wifi_if_lock);

	status = nss_dynamic_interface_dealloc_node(if_num, NSS_DYNAMIC_INTERFACE_TYPE_WIFI);
	if (status != NSS_TX_SUCCESS) {
		nss_warning("%p: Dynamic interface destroy failed status %d\n", nss_ctx, status);
		return status;
	}

	kfree(handle->pvt);
	kfree(handle);

	return status;
}

/*
 * nss_wifi_if_handle_init()
 *	Initialize wifi handle which holds the if_num and stats per interface.
 */
static struct nss_wifi_if_handle *nss_wifi_if_handle_create(struct nss_ctx_instance *nss_ctx,
										int32_t *cmd_rsp)
{
	int32_t index;
	int32_t if_num = 0;
	struct nss_wifi_if_handle *handle;

	if_num = nss_dynamic_interface_alloc_node(NSS_DYNAMIC_INTERFACE_TYPE_WIFI);
	if (if_num < 0) {
		nss_warning("%p:failure allocating wifi if\n", nss_ctx);
		*cmd_rsp = NSS_WIFI_IF_DYNAMIC_IF_FAILURE;
		return NULL;
	}

	index = NSS_WIFI_IF_GET_INDEX(if_num);

	handle = (struct nss_wifi_if_handle *)kzalloc(sizeof(struct nss_wifi_if_handle),
									GFP_KERNEL);
	if (!handle) {
		nss_warning("%p: handle memory alloc failed\n", nss_ctx);
		*cmd_rsp = NSS_WIFI_IF_ALLOC_FAILURE;
		goto error1;
	}

	handle->nss_ctx = nss_ctx;
	handle->if_num = if_num;
	handle->pvt = (struct nss_wifi_if_pvt *)kzalloc(sizeof(struct nss_wifi_if_pvt),
								GFP_KERNEL);
	if (!handle->pvt) {
		nss_warning("%p: failure allocating memory for nss_wifi_if_pvt\n", nss_ctx);
		*cmd_rsp = NSS_WIFI_IF_ALLOC_FAILURE;
		goto error2;
	}

	handle->cb = NULL;
	handle->app_data = NULL;

	spin_lock_bh(&wifi_if_lock);
	wifi_handle[index] = handle;
	spin_unlock_bh(&wifi_if_lock);

	*cmd_rsp = NSS_WIFI_IF_SUCCESS;

	return handle;

error2:
	kfree(handle);
error1:
	nss_dynamic_interface_dealloc_node(if_num, NSS_DYNAMIC_INTERFACE_TYPE_WIFI);
	return NULL;
}

/* nss_wifi_if_msg_init()
 *	Initialize wifi specific message structure.
 */
static void nss_wifi_if_msg_init(struct nss_wifi_if_msg *nwim,
					uint16_t if_num,
					uint32_t type,
					uint32_t len,
					nss_wifi_if_msg_callback_t cb,
					struct nss_wifi_if_handle *app_data)
{
	nss_cmn_msg_init(&nwim->cm, if_num, type, len, (void *)cb, (void *)app_data);
}

/*
 * nss_wifi_if_create()
 *	Create a wifi interface and associate it with the netdev
 */
struct nss_wifi_if_handle *nss_wifi_if_create(struct net_device *netdev)
{
	struct nss_ctx_instance *nss_ctx = &nss_top_main.nss[nss_top_main.wlan_handler_id];
	struct nss_wifi_if_msg nwim;
	struct nss_wifi_if_create_msg *nwcm;
	uint32_t ret;
	struct nss_wifi_if_handle *handle = NULL;

	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: Interface could not be created as core not ready\n", nss_ctx);
		return NULL;
	}

	handle = nss_wifi_if_handle_create(nss_ctx, &ret);
	if (!handle) {
		nss_warning("%p:wifi_if handle creation failed ret %d\n", nss_ctx, ret);
		return NULL;
	}

	/* Initializes the semaphore and also sets the msg handler for if_num */
	ret = nss_wifi_if_register_handler(handle);
	if (ret != NSS_WIFI_IF_SUCCESS) {
		nss_warning("%p: Registration handler failed reason: %d\n", nss_ctx, ret);
		goto error;
	}

	nss_wifi_if_msg_init(&nwim, handle->if_num, NSS_WIFI_IF_TX_CREATE_MSG,
				sizeof(struct nss_wifi_if_create_msg), nss_wifi_if_callback, handle);

	nwcm = &nwim.msg.create;
	nwcm->flags = 0;
	memcpy(nwcm->mac_addr, netdev->dev_addr, ETH_ALEN);

	ret = nss_wifi_if_tx_msg_sync(handle, &nwim);
	if (ret != NSS_TX_SUCCESS) {
		nss_warning("%p: nss_wifi_if_tx_msg_sync failed %u\n", nss_ctx, ret);
		goto error;
	}

	spin_lock_bh(&nss_top_main.lock);
	if (!nss_top_main.subsys_dp_register[handle->if_num].ndev) {
		nss_top_main.subsys_dp_register[handle->if_num].ndev = netdev;
	}
	spin_unlock_bh(&nss_top_main.lock);

	/*
	 * Hold a reference to the net_device
	 */
	dev_hold(netdev);

	/*
	 * The context returned is the interface # which is, essentially, the index into the if_ctx
	 * array that is holding the net_device pointer
	 */

	return handle;

error:
	nss_wifi_if_handle_destroy(handle);
	return NULL;
}
EXPORT_SYMBOL(nss_wifi_if_create);

/*
 * nss_wifi_if_destroy()
 *	Destroy the wifi interface associated with the interface number.
 */
nss_tx_status_t nss_wifi_if_destroy(struct nss_wifi_if_handle *handle)
{
	nss_tx_status_t status;
	struct net_device *dev;
	int32_t if_num = handle->if_num;
	struct nss_ctx_instance *nss_ctx = handle->nss_ctx;

	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: Interface could not be destroyed as core not ready\n", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	spin_lock_bh(&nss_top_main.lock);
	if (!nss_top_main.subsys_dp_register[if_num].ndev) {
		spin_unlock_bh(&nss_top_main.lock);
		nss_warning("%p: Unregister wifi interface %d: no context\n", nss_ctx, if_num);
		return NSS_TX_FAILURE_BAD_PARAM;
	}

	dev = nss_top_main.subsys_dp_register[if_num].ndev;
	nss_top_main.subsys_dp_register[if_num].ndev = NULL;
	spin_unlock_bh(&nss_top_main.lock);
	dev_put(dev);

	status = nss_wifi_if_handle_destroy(handle);
	return status;
}
EXPORT_SYMBOL(nss_wifi_if_destroy);

/*
 * nss_wifi_if_register()
 *	Register cb, netdev associated with the if_num to the nss data plane
 * to receive data packets.
 */
void nss_wifi_if_register(struct nss_wifi_if_handle *handle,
				nss_wifi_if_data_callback_t rx_callback,
				struct net_device *netdev)
{
	int32_t if_num;

	if (!handle) {
		nss_warning("nss_wifi_if_register handle is NULL\n");
		return;
	}

	if_num = handle->if_num;
	nss_assert(NSS_IS_IF_TYPE(DYNAMIC, if_num));

	nss_top_main.subsys_dp_register[if_num].ndev = netdev;
	nss_top_main.subsys_dp_register[if_num].cb = rx_callback;
	nss_top_main.subsys_dp_register[if_num].app_data = NULL;
	nss_top_main.subsys_dp_register[if_num].features = netdev->features;
}
EXPORT_SYMBOL(nss_wifi_if_register);

/*
 * nss_wifi_if_unregister()
 *	Unregister the cb, netdev associated with the if_num.
 */
void nss_wifi_if_unregister(struct nss_wifi_if_handle *handle)
{
	int32_t if_num;

	if (!handle) {
		nss_warning("nss_wifi_if_unregister handle is NULL\n");
		return;
	}

	if_num = handle->if_num;

	nss_top_main.subsys_dp_register[if_num].ndev = NULL;
	nss_top_main.subsys_dp_register[if_num].cb = NULL;
	nss_top_main.subsys_dp_register[if_num].app_data = NULL;
	nss_top_main.subsys_dp_register[if_num].features = 0;
}
EXPORT_SYMBOL(nss_wifi_if_unregister);

/*
 * nss_wifi_if_tx_buf()
 *	HLOS interface has received a packet which we redirect to the NSS.
 */
nss_tx_status_t nss_wifi_if_tx_buf(struct nss_wifi_if_handle *handle,
						struct sk_buff *skb)
{
	int32_t status;
	struct nss_ctx_instance *nss_ctx;
	int32_t if_num;

	if (!handle) {
		nss_warning("nss_wifi_if_tx_buf handle is NULL\n");
		return NSS_TX_FAILURE;
	}

	nss_ctx = handle->nss_ctx;
	if_num = handle->if_num;

	/*
	 * redirect should be turned on in /proc/
	 */
	if (unlikely(nss_ctl_redirect == 0)) {
		return NSS_TX_FAILURE_NOT_ENABLED;
	}

	if (unlikely(skb->vlan_tci)) {
		return NSS_TX_FAILURE_NOT_SUPPORTED;
	}

	nss_assert(NSS_IS_IF_TYPE(DYNAMIC, if_num));

	/*
	 * Get the NSS context that will handle this packet and check that it is initialised and ready
	 */
	NSS_VERIFY_CTX_MAGIC(nss_ctx);
	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: wifi_if packet dropped as core not ready", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	/*
	 * Sanity check the SKB to ensure that it's suitable for us
	 */
	if (unlikely(skb->len <= ETH_HLEN)) {
		nss_warning("%p: Rx packet: %p too short", nss_ctx, skb);
		return NSS_TX_FAILURE_TOO_SHORT;
	}

	/*
	 * Direct the buffer to the NSS
	 */
	status = nss_core_send_buffer(nss_ctx,
					if_num, skb,
					NSS_IF_DATA_QUEUE_0,
					H2N_BUFFER_PACKET,
					H2N_BIT_FLAG_VIRTUAL_BUFFER);
	if (unlikely(status != NSS_CORE_STATUS_SUCCESS)) {
		nss_warning("%p: Rx packet unable to enqueue\n", nss_ctx);

		return NSS_TX_FAILURE_QUEUE;
	}

	/*
	 * Kick the NSS awake so it can process our new entry.
	 */
	nss_hal_send_interrupt(nss_ctx->nmap,
				nss_ctx->h2n_desc_rings[NSS_IF_DATA_QUEUE_0].desc_ring.int_bit,
				NSS_REGS_H2N_INTR_STATUS_DATA_COMMAND_QUEUE);
	NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_TX_PACKET]);
	return NSS_TX_SUCCESS;
}
EXPORT_SYMBOL(nss_wifi_if_tx_buf);

/*
 * nss_wifi_if_copy_stats()
 *	Copy the stats from wifi handle to buffer(line) for if_num.
 */
int32_t nss_wifi_if_copy_stats(int32_t if_num, int i, char *line)
{
	int32_t bytes = 0;
	struct nss_wifi_if_stats *stats;
	int32_t ifnum;
	uint32_t len = 80;
	struct nss_wifi_if_handle *handle = NULL;

	ifnum = NSS_WIFI_IF_GET_INDEX(if_num);

	spin_lock_bh(&wifi_if_lock);
	if (!wifi_handle[ifnum]) {
		spin_unlock_bh(&wifi_if_lock);
		goto end;
	}

	handle = wifi_handle[ifnum];
	spin_unlock_bh(&wifi_if_lock);

	stats = &handle->stats;

	switch (i) {
	case 0:
		bytes = scnprintf(line, len, "rx_packets=%d\n",
					stats->node_stats.rx_packets);
		break;

	case 1:
		bytes = scnprintf(line, len, "rx_bytes=%d\n",
					stats->node_stats.rx_bytes);
		break;

	case 2:
		bytes = scnprintf(line, len, "rx_dropped=%d\n",
					stats->node_stats.rx_dropped);
		break;

	case 3:
		bytes = scnprintf(line, len, "tx_packets=%d\n",
					stats->node_stats.tx_packets);
		break;

	case 4:
		bytes = scnprintf(line, len, "tx_bytes=%d\n",
					stats->node_stats.tx_bytes);
		break;

	case 5:
		bytes = scnprintf(line, len, "tx_enqueue_failed=%d\n",
					stats->tx_enqueue_failed);
		break;

	case 6:
		bytes = scnprintf(line, len, "shaper_enqueue_failed=%d\n",
					stats->shaper_enqueue_failed);
		break;
	}

end:
	return bytes;
}
