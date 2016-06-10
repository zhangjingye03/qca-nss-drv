/*
 **************************************************************************
 * Copyright (c) 2013, 2016 The Linux Foundation. All rights reserved.
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

/**
 * nss_hal.h
 *	NSS HAL public declarations.
 */

#ifndef __NSS_HAL_H
#define __NSS_HAL_H

#include <linux/platform_device.h>
#include <linux/version.h>

#include <nss_hal_pvt.h>

#if (NSS_DT_SUPPORT != 1)
/*
 * nss_hal_common_reset()
 */
static inline void nss_hal_common_reset(uint32_t *clk_src)
{
	__nss_hal_common_reset(clk_src);
}
#endif

/*
 * nss_hal_core_reset()
 */
#if (NSS_DT_SUPPORT != 1)
static inline void nss_hal_core_reset(uint32_t core_id, uint32_t map, uint32_t addr, uint32_t clk_src)
{
	__nss_hal_core_reset(core_id, map, addr, clk_src);
}
#else
static inline void nss_hal_core_reset(uint32_t map_base, uint32_t reset_addr)
{
	__nss_hal_core_reset(map_base, reset_addr);
}
#endif

/*
 * nss_hal_read_interrupt_cause()
 */
static inline void nss_hal_read_interrupt_cause(uint32_t map, uint32_t irq, uint32_t shift_factor, uint32_t *cause)
{
	__nss_hal_read_interrupt_cause(map, irq, shift_factor, cause);
}

/*
 * nss_hal_clear_interrupt_cause()
 */
static inline void nss_hal_clear_interrupt_cause(uint32_t map, uint32_t irq, uint32_t shift_factor, uint32_t cause)
{
	__nss_hal_clear_interrupt_cause(map, irq, shift_factor, cause);
}

/*
 * nss_hal_disable_interrupt()
 */
static inline void nss_hal_disable_interrupt(uint32_t map, uint32_t irq, uint32_t shift_factor, uint32_t cause)
{
	__nss_hal_disable_interrupt(map, irq, shift_factor, cause);
}

/*
 * nss_hal_enable_interrupt()
 */
static inline void nss_hal_enable_interrupt(uint32_t map, uint32_t irq, uint32_t shift_factor, uint32_t cause)
{
	__nss_hal_enable_interrupt(map, irq, shift_factor, cause);
}

/*
 * nss_hal_send_interrupt()
 */
static inline void nss_hal_send_interrupt(uint32_t map, uint32_t irq, uint32_t cause)
{
	__nss_hal_send_interrupt(map, irq, cause);
}

/*
 * nss_hal_debug_enable()
 */
static inline void nss_hal_debug_enable(void)
{
	__nss_hal_debug_enable();
}

/*
 * nss_hal_probe()
 */
int nss_hal_probe(struct platform_device *nss_dev);

/*
 * nss_hal_remove()
 */
int nss_hal_remove(struct platform_device *nss_dev);

#endif /* __NSS_HAL_H */
