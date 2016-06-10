/*
 **************************************************************************
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
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
 * nss_core.c
 *	NSS driver core APIs source file.
 */

#include "nss_core.h"
#include "nss_hal.h"
#include <linux/kernel.h>
#include <linux/notifier.h>	/* for panic_notifier_list */
#include <linux/jiffies.h>	/* for time */
#include "nss_tx_rx_common.h"

#if NSS_MAX_CORES > 2	/* see comment in nss_fw_coredump_notify */
#error	too many NSS Cores: should be 1 or 2
#endif

static struct delayed_work coredump_queuewait;
static struct workqueue_struct *coredump_workqueue;

/*
 * nss_coredump_wait()
 *	reboot (panic) if all finished coredump interrupts will not come.
 *   N2H (C2C) interrupt may get lost during trap, as well NSS may start
 * only one core; so timeout if less than desird core sends back finished
 * coredump interrupt.
 */
static void nss_coredump_wait(struct work_struct *work)
{
	panic("did not get all coredump finished signals\n");
}

/*
 * nss_coredump_init_delay_work()
 *	set a wait function in case coredump finish interrupt lost or
 * only one NSS core is up.
 */
int nss_coredump_init_delay_work(void)
{
	coredump_workqueue = create_singlethread_workqueue("coredump_wait");
	if (!coredump_workqueue) {
		nss_warning("can't set wait: hopefully all int will come\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&coredump_queuewait, nss_coredump_wait);
	return 0;
}

/*
 * nss_panic_handler()
 *	notification callback register to panic chain
 */
static int nss_panic_handler(struct notifier_block *nb,
			unsigned long action, void *data)
{
	int dumped, timed;
	int i;

	for (i = 0; i < NSS_MAX_CORES; i++) {
		struct nss_ctx_instance *nss_ctx = &nss_top_main.nss[i];
		if (nss_ctx->state & NSS_CORE_STATE_FW_DEAD || !nss_ctx->nmap)
			continue;
		nss_ctx->state |= NSS_CORE_STATE_PANIC;
		nss_hal_send_interrupt(nss_ctx->nmap, 0,
			NSS_REGS_H2N_INTR_STATUS_TRIGGER_COREDUMP);
		nss_warning("panic call NSS FW %x to dump %x\n",
			nss_ctx->nmap, nss_ctx->state);
	}

	/*
	 * wait for FW coredump done: maximum 2 rounds for each core
	 * 200ms per round -- 16MB * 10 over 200MHz 32-bit memory bus
	 * panic will take another 3-5 seconds to reboot, so longer enough.
	 */
	dumped = timed = 0;
	do {
		mdelay(200);
		for (i = 0; i < NSS_MAX_CORES; i++) {
			struct nss_ctx_instance *nss_ctx = &nss_top_main.nss[i];
			if ((nss_ctx->state & NSS_CORE_STATE_FW_DEAD ||
				!nss_ctx->nmap) &&
			    !(nss_ctx->state & NSS_CORE_STATE_FW_DUMP)) {
				nss_ctx->state |= NSS_CORE_STATE_FW_DUMP;
				dumped++;
			}
		}
		if (dumped >= NSS_MAX_CORES) {
			nss_warning("NSS FW dump completed\n");
			break;
		}
	} while (timed++ < nss_top_main.num_nss * 2);

	if (timed >= nss_top_main.num_nss * 2)
		nss_warning("might get %d FW dumped", dumped);

	return	NOTIFY_DONE;
}

static struct notifier_block	nss_panic_nb = {
	.notifier_call = nss_panic_handler,
};

/*
 * nss_coredump_notify_register()
 *	API for nss_init to register coredump notifier to panic chain
 */
void nss_coredump_notify_register(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &nss_panic_nb);
}

/*
 * nss_fw_coredump_notify()
 *	handler for coredump notification from NSS FW
 */
void nss_fw_coredump_notify(struct nss_ctx_instance *nss_own,
				int intr __attribute__ ((unused)))
{
	int i;
	nss_warning("\n%p: COREDUMP %x Baddr %x stat %x\n",
			nss_own, intr, nss_own->nmap, nss_own->state);
	nss_own->state |= NSS_CORE_STATE_FW_DEAD;
	queue_delayed_work(coredump_workqueue, &coredump_queuewait,
			msecs_to_jiffies(3456));

	if (nss_own->state & NSS_CORE_STATE_PANIC)
		return;

	for (i = 0; i < NSS_MAX_CORES; i++) {
		struct nss_ctx_instance *nss_ctx = &nss_top_main.nss[i];
		/*
		 * only for two core now; if more cores, a counter is required
		 * to remember how many core has dumped.
		 * Do not call panic() till all core dumped.
		 */
		if (nss_ctx != nss_own) {
			if (nss_ctx->state & NSS_CORE_STATE_FW_DEAD ||
					!nss_ctx->nmap) {
				/*
				 * cannot call atomic_notifier_chain_unregister?
				 * (&panic_notifier_list, &nss_panic_nb);
				 */
				panic("NSS FW coredump: bringing system down\n");
			}
			nss_warning("notify NSS FW %X for coredump\n",
				nss_ctx->nmap);
			nss_hal_send_interrupt(nss_ctx->nmap, 0,
				NSS_REGS_H2N_INTR_STATUS_TRIGGER_COREDUMP);
		}
	}
}
