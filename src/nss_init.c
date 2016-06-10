/*
 **************************************************************************
 * Copyright (c) 2013 - 2016, The Linux Foundation. All rights reserved.
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
 * nss_init.c
 *	NSS init APIs
 *
 */
#include "nss_core.h"
#if (NSS_PM_SUPPORT == 1)
#include "nss_pm.h"
#endif
#include "nss_tx_rx_common.h"
#include "nss_data_plane.h"
#include "nss_capwap.h"

#include <nss_hal.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/device.h>

#if (NSS_DT_SUPPORT == 1)
#if (NSS_FABRIC_SCALING_SUPPORT == 1)
#include <linux/fab_scaling.h>
#endif
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reset.h>
#else
#include <mach/msm_nss.h>
#endif

#include <linux/sysctl.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>

/*
 * Global declarations
 */
int nss_ctl_redirect __read_mostly = 0;
int nss_ctl_debug __read_mostly = 0;
int nss_ctl_logbuf __read_mostly = 0;
int nss_jumbo_mru  __read_mostly = 0;
int nss_paged_mode __read_mostly = 0;
int nss_skip_nw_process = 0x0;
module_param(nss_skip_nw_process, int, S_IRUGO);

/*
 * PM client handle
 */
#if (NSS_PM_SUPPORT == 1)
static void *pm_client;
#endif

/*
 * Handler to send NSS messages
 */
struct clk *nss_core0_clk;

/*
 * Handle fabric requests - only on new kernel
 */
#if (NSS_DT_SUPPORT == 1)
struct clk *nss_fab0_clk;
struct clk *nss_fab1_clk;
#endif

/*
 * Top level nss context structure
 */
struct nss_top_instance nss_top_main;
struct nss_cmd_buffer nss_cmd_buf;
struct nss_runtime_sampling nss_runtime_samples;
struct workqueue_struct *nss_wq;

/*
 * Work Queue to handle messages to Kernel
 */
nss_work_t *nss_work;

extern struct of_device_id nss_dt_ids[];

/*
 * nss_probe()
 * 	HLOS device probe callback
 */
inline int nss_probe(struct platform_device *nss_dev)
{
	return nss_hal_probe(nss_dev);
}

/*
 * nss_remove()
 * 	HLOS device remove callback
 */
inline int nss_remove(struct platform_device *nss_dev)
{
	return nss_hal_remove(nss_dev);
}

/*
 * nss_driver
 *	Platform driver structure for NSS
 */
struct platform_driver nss_driver = {
	.probe	= nss_probe,
	.remove	= nss_remove,
	.driver	= {
		.name	= "qca-nss",
		.owner	= THIS_MODULE,
#if (NSS_DT_SUPPORT == 1)
		.of_match_table = of_match_ptr(nss_dt_ids),
#endif
	},
};

#if (NSS_FREQ_SCALE_SUPPORT == 1)
/*
 * nss_reset_frequency_stats_samples()
 *	Reset all frequency sampling state when auto scaling is turned off.
 */
static void nss_reset_frequency_stats_samples (void)
{
	nss_runtime_samples.buffer_index = 0;
	nss_runtime_samples.sum = 0;
	nss_runtime_samples.average = 0;
	nss_runtime_samples.sample_count = 0;
	nss_runtime_samples.message_rate_limit = 0;
	nss_runtime_samples.freq_scale_rate_limit_down = 0;
}

/*
 ***************************************************************************************************
 * nss_wq_function() is used to queue up requests to change NSS frequencies.
 * The function will take care of NSS notices and also control clock.
 * The auto rate algorithmn will queue up requests or the procfs may also queue up these requests.
 ***************************************************************************************************
 */

/*
 * nss_wq_function()
 *	Added to Handle BH requests to kernel
 */
void nss_wq_function (struct work_struct *work)
{
	nss_work_t *my_work = (nss_work_t *)work;

	nss_freq_change(&nss_top_main.nss[NSS_CORE_0], my_work->frequency, my_work->stats_enable, 0);
	if (nss_top_main.nss[NSS_CORE_1].state == NSS_CORE_STATE_INITIALIZED) {
		nss_freq_change(&nss_top_main.nss[NSS_CORE_1], my_work->frequency, my_work->stats_enable, 0);
	}
	clk_set_rate(nss_core0_clk, my_work->frequency);
	nss_freq_change(&nss_top_main.nss[NSS_CORE_0], my_work->frequency, my_work->stats_enable, 1);
	if (nss_top_main.nss[NSS_CORE_1].state == NSS_CORE_STATE_INITIALIZED) {
		nss_freq_change(&nss_top_main.nss[NSS_CORE_1], my_work->frequency, my_work->stats_enable, 1);
	}

/*
 * If we are running NSS_PM_SUPPORT, we are on banana
 * otherwise, we check if we are are on new kernel by checking if the
 * fabric lookups are not NULL (success in init()))
 */
#if (NSS_PM_SUPPORT == 1)
	if (!pm_client) {
		goto out;
	}

	if (my_work->frequency >= NSS_FREQ_733) {
		nss_pm_set_perf_level(pm_client, NSS_PM_PERF_LEVEL_TURBO);
	} else if (my_work->frequency > NSS_FREQ_110) {
		nss_pm_set_perf_level(pm_client, NSS_PM_PERF_LEVEL_NOMINAL);
	} else {
		nss_pm_set_perf_level(pm_client, NSS_PM_PERF_LEVEL_IDLE);
	}

out:
#else
#if (NSS_DT_SUPPORT == 1)
#if (NSS_FABRIC_SCALING_SUPPORT == 1)
	scale_fabrics();
#endif
	if ((nss_fab0_clk != NULL) && (nss_fab0_clk != NULL)) {
		if (my_work->frequency >= NSS_FREQ_733) {
			clk_set_rate(nss_fab0_clk, NSS_FABRIC0_TURBO);
			clk_set_rate(nss_fab1_clk, NSS_FABRIC1_TURBO);
		} else if (my_work->frequency > NSS_FREQ_110) {
			clk_set_rate(nss_fab0_clk, NSS_FABRIC0_NOMINAL);
			clk_set_rate(nss_fab1_clk, NSS_FABRIC1_NOMINAL);
		} else {
			clk_set_rate(nss_fab0_clk, NSS_FABRIC0_IDLE);
			clk_set_rate(nss_fab1_clk, NSS_FABRIC1_IDLE);
		}
	}
#endif
#endif
	kfree((void *)work);
}

/*
 * nss_current_freq_handler()
 *	Handle Userspace Frequency Change Requests
 */
static int nss_current_freq_handler(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret, i;

	BUG_ON(!nss_wq);

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);

	if (!*lenp || (*ppos && !write)) {
		printk("Frequency Set to %d\n", nss_cmd_buf.current_freq);
		*lenp = 0;
		return ret;
	}

	/*
	 * Check if frequency exists in frequency Table
	 */
	i = 0;
	while (i < NSS_FREQ_MAX_SCALE) {
		if (nss_runtime_samples.freq_scale[i].frequency == nss_cmd_buf.current_freq) {
			break;
		}
		i++;
	}
	if (i == NSS_FREQ_MAX_SCALE) {
		printk("Frequency not found. Please check Frequency Table\n");
		return ret;
	}

	/* Turn off Auto Scale */
	nss_cmd_buf.auto_scale = 0;
	nss_runtime_samples.freq_scale_ready = 0;

	nss_work = (nss_work_t *)kmalloc(sizeof(nss_work_t), GFP_ATOMIC);
	if (!nss_work) {
		nss_info("NSS Freq WQ kmalloc fail");
		return ret;
	}
	INIT_WORK((struct work_struct *)nss_work, nss_wq_function);
	nss_work->frequency = nss_cmd_buf.current_freq;
	nss_work->stats_enable = 0;

	/* Ensure we start with a fresh set of samples later */
	nss_reset_frequency_stats_samples();

	queue_work(nss_wq, (struct work_struct *)nss_work);

	return ret;
}

/*
 * nss_auto_scale_handler()
 *	Enables or Disable Auto Scaling
 */
static int nss_auto_scale_handler(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);

	if (!*lenp || (*ppos && !write)) {
		return ret;
	}

	if (nss_cmd_buf.auto_scale != 1) {
		/*
		 * Is auto scaling currently enabled? If so, send the command to
		 * disable stats reporting to NSS
		 */
		if (nss_runtime_samples.freq_scale_ready != 0) {
			nss_cmd_buf.current_freq = nss_runtime_samples.freq_scale[nss_runtime_samples.freq_scale_index].frequency;
			nss_work = (nss_work_t *)kmalloc(sizeof(nss_work_t), GFP_ATOMIC);
			if (!nss_work) {
				nss_info("NSS Freq WQ kmalloc fail");
				return ret;
			}
			INIT_WORK((struct work_struct *)nss_work, nss_wq_function);
			nss_work->frequency = nss_cmd_buf.current_freq;
			nss_work->stats_enable = 0;
			queue_work(nss_wq, (struct work_struct *)nss_work);
			nss_runtime_samples.freq_scale_ready = 0;

			/*
			 * The current samples would be stale later when scaling is
			 * enabled again, hence reset them
			 */
			nss_reset_frequency_stats_samples();
		}
		return ret;
	}

	/*
	 * Auto Scaling is already being done
	 */
	if (nss_runtime_samples.freq_scale_ready == 1) {
		return ret;
	}

	/*
	 * Setup default values - Middle of Freq Scale Band
	 */
	nss_runtime_samples.freq_scale_index = 1;
	nss_cmd_buf.current_freq = nss_runtime_samples.freq_scale[nss_runtime_samples.freq_scale_index].frequency;

	nss_work = (nss_work_t *)kmalloc(sizeof(nss_work_t), GFP_ATOMIC);
	if (!nss_work) {
		nss_info("NSS Freq WQ kmalloc fail");
		return ret;
	}
	INIT_WORK((struct work_struct *)nss_work, nss_wq_function);
	nss_work->frequency = nss_cmd_buf.current_freq;
	nss_work->stats_enable = 1;
	queue_work(nss_wq, (struct work_struct *)nss_work);

	nss_cmd_buf.auto_scale = 0;
	nss_runtime_samples.freq_scale_ready = 1;

	return ret;
}

/*
 * nss_get_freq_table_handler()
 *	Display Support Freq and Ex how to Change.
 */
static int nss_get_freq_table_handler(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret, i;

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);

	if (write) {
		return ret;
	}

	printk("Frequency Supported - ");

	i = 0;
	while (i < NSS_FREQ_MAX_SCALE) {
		printk("%dMhz ", nss_runtime_samples.freq_scale[i].frequency/1000000);
		i++;
	}
	printk("\n");

	*lenp = 0;
	return ret;
}

/*
 * nss_get_average_inst_handler()
 *	Display AVG Inst Per Ms.
 */
static int nss_get_average_inst_handler(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);

	if (write) {
		return ret;
	}

	printk("Current Inst Per Ms %x\n", nss_runtime_samples.average);

	*lenp = 0;
	return ret;
}
#endif

#if (NSS_FW_DBG_SUPPORT == 1)
/*
 * nss_debug_handler()
 *	Enable NSS debug output
 */
static int nss_debug_handler(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);
	if (!ret) {
		if ((write) && (nss_ctl_debug != 0)) {
			printk("Enabling NSS SPI Debug\n");
			nss_hal_debug_enable();
		}
	}

	return ret;
}
#endif

/*
 * nss_coredump_handler()
 *	Send Signal To Coredump NSS Cores
 */
static int nss_coredump_handler(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct nss_ctx_instance *nss_ctx = &nss_top_main.nss[NSS_CORE_0];
	int ret;

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);
	if (!ret) {
		if ((write) && (nss_ctl_debug != 0)) {
			printk("Coredumping to DDR\n");
			nss_hal_send_interrupt(nss_ctx->nmap, nss_ctx->h2n_desc_rings[NSS_IF_CMD_QUEUE].desc_ring.int_bit, NSS_REGS_H2N_INTR_STATUS_TRIGGER_COREDUMP);
		}
	}

	return ret;
}

/*
 * nss_jumbo_mru_handler()
 *	Sysctl to modify nss_jumbo_mru
 */
static int nss_jumbo_mru_handler(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);
	if (ret) {
		return ret;
	}

	if (write) {
		nss_core_set_jumbo_mru(nss_jumbo_mru);
		nss_info("jumbo_mru set to %d\n", nss_jumbo_mru);
	}

	return ret;
}

/* nss_paged_mode_handler()
 *	Sysctl to modify nss_paged_mode.
 */

static int nss_paged_mode_handler(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);
	if (ret) {
		return ret;
	}

	if (write) {
		nss_core_set_paged_mode(nss_paged_mode);
		nss_info("paged_mode set to %d\n", nss_paged_mode);
	}

	return ret;
}

#if (NSS_FREQ_SCALE_SUPPORT == 1)
/*
 * sysctl-tuning infrastructure.
 */
static struct ctl_table nss_freq_table[] = {
	{
		.procname		= "current_freq",
		.data			= &nss_cmd_buf.current_freq,
		.maxlen			= sizeof(int),
		.mode			= 0644,
		.proc_handler	= &nss_current_freq_handler,
	},
	{
		.procname		= "freq_table",
		.data			= &nss_cmd_buf.max_freq,
		.maxlen			= sizeof(int),
		.mode			= 0644,
		.proc_handler	= &nss_get_freq_table_handler,
	},
	{
		.procname		= "auto_scale",
		.data			= &nss_cmd_buf.auto_scale,
		.maxlen			= sizeof(int),
		.mode			= 0644,
		.proc_handler	= &nss_auto_scale_handler,
	},
	{
		.procname		= "inst_per_sec",
		.data			= &nss_cmd_buf.average_inst,
		.maxlen			= sizeof(int),
		.mode			= 0644,
		.proc_handler	= &nss_get_average_inst_handler,
	},
	{ }
};
#endif

static struct ctl_table nss_general_table[] = {
	{
		.procname               = "redirect",
		.data                   = &nss_ctl_redirect,
		.maxlen                 = sizeof(int),
		.mode                   = 0644,
		.proc_handler   	= proc_dointvec,
	},
#if (NSS_FW_DBG_SUPPORT == 1)
	{
		.procname               = "debug",
		.data                   = &nss_ctl_debug,
		.maxlen                 = sizeof(int),
		.mode                   = 0644,
		.proc_handler   	= &nss_debug_handler,
	},
#endif
	{
		.procname               = "coredump",
		.data                   = &nss_cmd_buf.coredump,
		.maxlen                 = sizeof(int),
		.mode                   = 0644,
		.proc_handler   	= &nss_coredump_handler,
	},
	{
		.procname               = "logbuf",
		.data                   = &nss_ctl_logbuf,
		.maxlen                 = sizeof(int),
		.mode                   = 0644,
		.proc_handler   	= &nss_logbuffer_handler,
	},
	{
		.procname               = "jumbo_mru",
		.data                   = &nss_jumbo_mru,
		.maxlen                 = sizeof(int),
		.mode                   = 0644,
		.proc_handler           = &nss_jumbo_mru_handler,
	},
	{
		.procname               = "paged_mode",
		.data                   = &nss_paged_mode,
		.maxlen                 = sizeof(int),
		.mode                   = 0644,
		.proc_handler           = &nss_paged_mode_handler,
	},
	{ }
};

static struct ctl_table nss_clock_dir[] = {
#if (NSS_FREQ_SCALE_SUPPORT == 1)
	{
		.procname               = "clock",
		.mode                   = 0555,
		.child                  = nss_freq_table,
	},
#endif
	{
		.procname               = "general",
		.mode                   = 0555,
		.child                  = nss_general_table,
	},
	{ }
};

static struct ctl_table nss_root_dir[] = {
	{
		.procname		= "nss",
		.mode			= 0555,
		.child			= nss_clock_dir,
	},
	{ }
};

static struct ctl_table nss_root[] = {
	{
		.procname		= "dev",
		.mode			= 0555,
		.child			= nss_root_dir,
	},
	{ }
};

static struct ctl_table_header *nss_dev_header;

/*
 * nss_init()
 *	Registers nss driver
 */
static int __init nss_init(void)
{
#if (NSS_DT_SUPPORT == 1)
	struct device_node *cmn = NULL;
	struct resource res_nss_fpb_base;
#endif

	nss_info("Init NSS driver");

#if (NSS_DT_SUPPORT == 1)
	/*
	 * Get reference to NSS common device node
	 */
	cmn = of_find_node_by_name(NULL, "nss-common");
	if (!cmn) {
		nss_info_always("qca-nss-drv.ko is loaded for symbol link\n");
		return 0;
	}

	if (of_address_to_resource(cmn, 0, &res_nss_fpb_base) != 0) {
		nss_info("of_address_to_resource() return error for nss_fpb_base\n");
		of_node_put(cmn);
		return -EFAULT;
	}

	nss_top_main.nss_fpb_base = ioremap_nocache(res_nss_fpb_base.start,
						    resource_size(&res_nss_fpb_base));
	if (!nss_top_main.nss_fpb_base) {
		nss_info("ioremap fail for nss_fpb_base\n");
		of_node_put(cmn);
		return -EFAULT;
	}

	nss_top_main.nss_hal_common_init_done = false;

	/*
	 * Release reference to NSS common device node
	 */
	of_node_put(cmn);
	cmn = NULL;
#else
	/*
	 * Perform clock init common to all NSS cores
	 */
	nss_hal_common_reset(&(nss_top_main.clk_src));

#endif /* NSS_DT_SUPPORT */

	/*
	 * Initialize data_plane workqueue
	 */
	if (nss_data_plane_init_delay_work()) {
		nss_warning("Error initializing nss_data_plane_workqueue\n");
		return -EFAULT;
	}

	/*
	 * Enable spin locks
	 */
	spin_lock_init(&(nss_top_main.lock));
	spin_lock_init(&(nss_top_main.stats_lock));

	/*
	 * Enable NSS statistics
	 */
	nss_stats_init();

	/*
	 * Register sysctl table.
	 */
	nss_dev_header = register_sysctl_table(nss_root);

	/*
	 * Registering sysctl for ipv4/6 specific config.
	 */
	nss_ipv4_register_sysctl();
	nss_ipv6_register_sysctl();

	/*
	 * Registering sysctl for n2h specific config.
	 */
	nss_n2h_register_sysctl();

	/*
	 * Setup Runtime Sample values
	 */
	nss_runtime_samples.freq_scale_index = 1;
	nss_runtime_samples.freq_scale_ready = 0;
	nss_runtime_samples.freq_scale_rate_limit_down = 0;
	nss_runtime_samples.buffer_index = 0;
	nss_runtime_samples.sum = 0;
	nss_runtime_samples.sample_count = 0;
	nss_runtime_samples.average = 0;
	nss_runtime_samples.message_rate_limit = 0;
	nss_runtime_samples.initialized = 0;

	nss_cmd_buf.current_freq = nss_runtime_samples.freq_scale[nss_runtime_samples.freq_scale_index].frequency;

	/*
	 * Initial Workqueue
	 */
	nss_wq = create_workqueue("nss_freq_queue");

#if (NSS_PM_SUPPORT == 1)
	/*
	 * Initialize NSS Bus PM module
	 */
	nss_pm_init();

	/*
	 * Register with Bus driver
	 */
	pm_client = nss_pm_client_register(NSS_PM_CLIENT_NETAP);
	if (!pm_client) {
		nss_warning("Error registering with PM driver");
	}
#endif

	/*
	 * Initialize mtu size needed as start
	 */
	nss_top_main.prev_mtu_sz = NSS_GMAC_NORMAL_FRAME_MTU;

	/*
	 * register panic handler and timeout control
	 */
	nss_coredump_notify_register();
	nss_coredump_init_delay_work();

	/*
	 * Init capwap
	 */
	nss_capwap_init();

	/*
	 * Register platform_driver
	 */
	return platform_driver_register(&nss_driver);
}

/*
 * nss_cleanup()
 *	Unregisters nss driver
 */
static void __exit nss_cleanup(void)
{
#if (NSS_DT_SUPPORT == 1)
	struct device_node *cmn = NULL;

	/*
	 * Get reference to NSS common device node
	 */
	cmn = of_find_node_by_name(NULL, "nss-common");
	if (!cmn) {
		nss_info_always("cannot find nss-common node, maybe just for symbol link\n");
		return;
	}
#endif

	nss_info("Exit NSS driver");

	if (nss_dev_header)
		unregister_sysctl_table(nss_dev_header);

	/*
	 * Unregister n2h specific sysctl
	 */
	nss_n2h_unregister_sysctl();

	/*
	 * Unregister ipv4/6 specific sysctl
	 */
	nss_ipv4_unregister_sysctl();
	nss_ipv6_unregister_sysctl();

#if (NSS_DT_SUPPORT == 1)
	if(nss_top_main.nss_fpb_base) {
		iounmap(nss_top_main.nss_fpb_base);
		nss_top_main.nss_fpb_base = 0;
	}
#endif

	nss_data_plane_destroy_delay_work();

	platform_driver_unregister(&nss_driver);
}

module_init(nss_init);
module_exit(nss_cleanup);

MODULE_DESCRIPTION("QCA NSS Driver");
MODULE_AUTHOR("Qualcomm Atheros Inc");
MODULE_LICENSE("Dual BSD/GPL");
