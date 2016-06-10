/*
 **************************************************************************
 * Copyright (c) 2014 - 2016, The Linux Foundation. All rights reserved.
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

#include "nss_data_plane.h"
#include "nss_phys_if.h"
#include "nss_core.h"
#include "nss_tx_rx_common.h"
#include <nss_gmac_api_if.h>

extern int nss_skip_nw_process;

#define NSS_DP_SUPPORTED_FEATURES NETIF_F_HIGHDMA | NETIF_F_HW_CSUM | NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_FRAGLIST | (NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_UFO)

static struct delayed_work nss_data_plane_work;
static struct workqueue_struct *nss_data_plane_workqueue;

struct nss_data_plane_param nss_data_plane_params[NSS_MAX_PHYSICAL_INTERFACES];

/*
 * nss_data_plane_open()
 *	Called by gmac to notify open to nss-fw
 */
static int nss_data_plane_open(void *arg, uint32_t tx_desc_ring, uint32_t rx_desc_ring, uint32_t mode)
{
	struct nss_data_plane_param *dp = (struct nss_data_plane_param *)arg;

	if (dp->notify_open) {
		return NSS_GMAC_SUCCESS;
	}
	if (nss_phys_if_open(dp->nss_ctx, tx_desc_ring, rx_desc_ring, mode, dp->if_num, dp->bypass_nw_process) == NSS_TX_SUCCESS) {
		dp->notify_open = 1;
		return NSS_GMAC_SUCCESS;
	}
	return NSS_GMAC_FAILURE;
}

/*
 * nss_data_plane_close()
 *	Called by gmac to notify close to nss-fw
 */
static int nss_data_plane_close(void *arg)
{
	/*
	 * We don't actually do synopsys gmac close in fw, just return success
	 */
	return NSS_GMAC_SUCCESS;
}

/*
 * nss_data_plane_link_state()
 *	Called by gmac to notify link state change to nss-fw
 */
static int nss_data_plane_link_state(void *arg, uint32_t link_state)
{
	struct nss_data_plane_param *dp = (struct nss_data_plane_param *)arg;

	return nss_phys_if_link_state(dp->nss_ctx, link_state, dp->if_num);
}

/*
 * nss_data_plane_mac_addr()
 *	Called by gmac to set mac address
 */
static int nss_data_plane_mac_addr(void *arg, uint8_t *addr)
{
	struct nss_data_plane_param *dp = (struct nss_data_plane_param *)arg;

	return nss_phys_if_mac_addr(dp->nss_ctx, addr, dp->if_num);
}

/*
 * nss_data_plane_change_mtu()
 *	Called by gmac to change mtu of a gmac
 */
static int nss_data_plane_change_mtu(void *arg, uint32_t mtu)
{
	struct nss_data_plane_param *dp = (struct nss_data_plane_param *)arg;

	return nss_phys_if_change_mtu(dp->nss_ctx, mtu, dp->if_num);
}

/*
 * nss_data_plane_pause_on_off()
 *	Called by gmac to enable/disable pause frames
 */
static int nss_data_plane_pause_on_off(void *arg, uint32_t pause_on)
{
	struct nss_data_plane_param *dp = (struct nss_data_plane_param *)arg;

	return nss_phys_if_pause_on_off(dp->nss_ctx, pause_on, dp->if_num);
}

/*
 * nss_data_plane_buf()
 *	Called by gmac to pass a sk_buff for xmit
 */
static int nss_data_plane_buf(void *arg, struct sk_buff *os_buf)
{
	struct nss_data_plane_param *dp = (struct nss_data_plane_param *)arg;

	return nss_phys_if_buf(dp->nss_ctx, os_buf, dp->if_num);
}

/*
 * nss_data_plane_set_features()
 *	Called by gmac to allow data plane to modify the set of features it supports
*/
static void nss_data_plane_set_features(struct net_device *netdev)
{
	netdev->features |= NSS_DP_SUPPORTED_FEATURES;
	netdev->hw_features |= NSS_DP_SUPPORTED_FEATURES;
	netdev->vlan_features |= NSS_DP_SUPPORTED_FEATURES;
	netdev->wanted_features |= NSS_DP_SUPPORTED_FEATURES;
}

/*
 * nss_data_plane_work_function()
 *	Work function that gets queued to "install" the gmac overlays
 */
static void nss_data_plane_work_function(struct work_struct *work)
{
	int i;
	struct nss_ctx_instance *nss_ctx = &nss_top_main.nss[NSS_CORE_0];

	for (i = 0; i < NSS_MAX_PHYSICAL_INTERFACES; i++) {
		if (!nss_data_plane_register_to_nss_gmac(nss_ctx, i)) {
			nss_warning("%p: Register data plane failed for gmac:%d\n", nss_ctx, i);
		} else {
			nss_info("%p: Register data plan to gmac:%d success\n", nss_ctx, i);
		}
	}
}

/*
 * nss offload data plane ops
 */
static struct nss_gmac_data_plane_ops dp_ops =
{
	.open		= nss_data_plane_open,
	.close		= nss_data_plane_close,
	.link_state	= nss_data_plane_link_state,
	.mac_addr	= nss_data_plane_mac_addr,
	.change_mtu	= nss_data_plane_change_mtu,
	.xmit		= nss_data_plane_buf,
	.set_features	= nss_data_plane_set_features,
	.pause_on_off	= nss_data_plane_pause_on_off,
};

/*
 * nss_data_plane_set_enabled()
 */
void nss_data_plane_set_enabled(int if_num)
{
	nss_data_plane_params[if_num].enabled = 1;
}

/*
 * nss_data_plane_register_to_nss_gmac()
 */
bool nss_data_plane_register_to_nss_gmac(struct nss_ctx_instance *nss_ctx, int if_num)
{
	struct nss_data_plane_param *ndpp = &nss_data_plane_params[if_num];
	struct nss_top_instance *nss_top = nss_ctx->nss_top;
	struct net_device *netdev;
	bool is_open;

	if (!ndpp->enabled) {
		return false;
	}

	netdev = nss_gmac_get_netdev_by_macid(if_num);
	if (!netdev) {
		nss_info("Platform don't have gmac%d enabled, don't bring up nss_phys_if and don't register to nss-gmac", if_num);
		return false;
	}

	is_open = nss_gmac_is_in_open_state(netdev);
	ndpp->dev = netdev;
	ndpp->nss_ctx = nss_ctx;
	ndpp->if_num = if_num;
	ndpp->notify_open = 0;
	ndpp->features = 0;

	/*
	 * Check if NSS NW processing to be bypassed for this GMAC
	 */
	if (nss_skip_nw_process) {
		ndpp->bypass_nw_process = 1;
	} else {
		ndpp->bypass_nw_process = 0;
	}

	if (nss_gmac_override_data_plane(netdev, &dp_ops, ndpp) != NSS_GMAC_SUCCESS) {
		nss_info("Override nss-gmac data plane failed\n");
		return false;
	}

	/*
	 * Setup the receive callback so that data pkts received form NSS-FW will
	 * be redirected to the gmac driver as we are overriding the data plane
	 */
	nss_top->phys_if_handler_id[if_num] = nss_ctx->id;
	nss_phys_if_register_handler(if_num);

	nss_top->subsys_dp_register[if_num].ndev = netdev;
	nss_top->subsys_dp_register[if_num].cb = nss_gmac_receive;
	nss_top->subsys_dp_register[if_num].app_data = NULL;
	nss_top->subsys_dp_register[if_num].features = ndpp->features;

	/*
	 * Now we are registered and our side is ready, if the gmac was opened, ask it to start again
	 */
	if (is_open) {
		nss_gmac_start_data_plane(netdev, ndpp);
	}
	return true;
}

/*
 * nss_data_plane_schedule_registration()
 *	Called from nss_init to schedule a work to do data_plane register to nss-gmac
 */
bool nss_data_plane_schedule_registration(void)
{
	if (!queue_work_on(1, nss_data_plane_workqueue, &nss_data_plane_work.work)) {
		nss_warning("Failed to register data plane workqueue on core 1\n");
		return false;
	} else {
		nss_info("Register data plane workqueue on core 1\n");
		return true;
	}
}

/*
 * nss_data_plane_unregister_from_nss_gmac()
 */
void nss_data_plane_unregister_from_nss_gmac(int if_num)
{
	nss_gmac_restore_data_plane(nss_data_plane_params[if_num].dev);
	nss_data_plane_params[if_num].dev = NULL;
	nss_data_plane_params[if_num].nss_ctx = NULL;
	nss_data_plane_params[if_num].if_num = 0;
	nss_data_plane_params[if_num].notify_open = 0;
	nss_data_plane_params[if_num].enabled = 0;
	nss_data_plane_params[if_num].bypass_nw_process = 0;
}

/*
 * nss_data_plane_init_delay_work()
 */
int nss_data_plane_init_delay_work(void)
{
	nss_data_plane_workqueue = create_singlethread_workqueue("nss_data_plane_workqueue");
	if (!nss_data_plane_workqueue) {
		nss_warning("Can't allocate workqueue\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&nss_data_plane_work, nss_data_plane_work_function);
	return 0;
}

/*
 * nss_data_plane_destroy_delay_work()
 */
void nss_data_plane_destroy_delay_work(void)
{
	destroy_workqueue(nss_data_plane_workqueue);
}
