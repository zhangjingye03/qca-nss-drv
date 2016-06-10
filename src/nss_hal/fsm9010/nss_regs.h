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

/**
 * nss_regs.h
 *	NSS register definitions.
 */

#ifndef __NSS_REGS_H
#define __NSS_REGS_H

#include <linux/types.h>
#include <asm/io.h>

#define NSS_FSM9010_TCM_SIZE SZ_64K
#define NSS_FSM9010_TCM_BASE IOMEM(0xe4000000)
#define NSS_FSM9010_FPB_BASE 0xfc800000
/*
 * CSM register offsets
 */
#define NSS_REGS_CORE_ID_OFFSET			0x0000
#define NSS_REGS_RESET_CTRL_OFFSET		0x0004
#define NSS_REGS_CORE_BAR_OFFSET		0x0008
#define NSS_REGS_CORE_AMC_OFFSET		0x000c
#define NSS_REGS_CORE_BOOT_ADDR_OFFSET		0x0010
#define NSS_REGS_C2C_INTR_STATUS_OFFSET		0x0014
#define NSS_REGS_C2C_INTR_SET_OFFSET		0x0018
#define NSS_REGS_C2C_INTR_CLR_OFFSET		0x001c
#define NSS_REGS_N2H_INTR_STATUS_OFFSET		0x0020
#define NSS_REGS_N2H_INTR_SET_OFFSET		0x0024
#define NSS_REGS_N2H_INTR_CLR_OFFSET		0x0028
#define NSS_REGS_N2H_INTR_MASK_OFFSET		0x002c
#define NSS_REGS_N2H_INTR_MASK_SET_OFFSET	0x0030
#define NSS_REGS_N2H_INTR_MASK_CLR_OFFSET	0x0034
#define NSS_REGS_CORE_INT_STAT0_TYPE_OFFSET	0x0038
#define NSS_REGS_CORE_INT_STAT1_TYPE_OFFSET	0x003c
#define NSS_REGS_CORE_INT_STAT2_TYPE_OFFSET	0x0040
#define NSS_REGS_CORE_INT_STAT3_TYPE_OFFSET	0x0044
#define NSS_REGS_CORE_IFETCH_RANGE_OFFSET	0x0048

/*
 * FPB register offsets
 */
#define NSS_REGS_FPB_CSR_CFG_OFFSET		0x0004

/*
 * Defines for N2H interrupts
 *
 * It is required to have 2 COREDUMP_COMPLETE interrupts because
 * both NSS cores may generate interrupt simultaneously
 */
#define NSS_REGS_N2H_INTR_STATUS_EMPTY_BUFFER_QUEUE	(1 << 0)
#define NSS_REGS_N2H_INTR_STATUS_DATA_COMMAND_QUEUE	(1 << 1)
#define NSS_REGS_N2H_INTR_STATUS_DATA_QUEUE_1	    	(1 << 2)
#define NSS_REGS_N2H_INTR_STATUS_EMPTY_BUFFERS_SOS	(1 << 10)
#define NSS_REGS_N2H_INTR_STATUS_TX_UNBLOCKED		(1 << 11)
#define NSS_REGS_N2H_INTR_STATUS_COREDUMP_COMPLETE_1	(1 << 13)
#define NSS_REGS_N2H_INTR_STATUS_COREDUMP_COMPLETE_0	(1 << 14)

/*
 * Defines for H2N interrupts
 */
#define NSS_REGS_H2N_INTR_STATUS_EMPTY_BUFFER_QUEUE	(1 << 0)
#define NSS_REGS_H2N_INTR_STATUS_DATA_COMMAND_QUEUE	(1 << 1)
#define NSS_REGS_H2N_INTR_STATUS_RESET			(1 << 10)	/** Unused */
#define NSS_REGS_H2N_INTR_STATUS_TX_UNBLOCKED		(1 << 11)
#define NSS_REGS_H2N_INTR_STATUS_TRIGGER_COREDUMP	(1 << 15)

/*
 * clock source for NSS cores
 */
enum nss_regs_clk_src_select {
	NSS_REGS_CLK_SRC_DEFAULT,
	NSS_REGS_CLK_SRC_ALTERNATE
};

/*
 * nss_read_32()
 *	Read NSS register
 */
static inline uint32_t nss_read_32(uint32_t addr, uint32_t offs)
{
	return readl((uint32_t *)(addr + offs));
}

/*
 * nss_write_32()
 *	Write NSS register
 */
static inline void nss_write_32(uint32_t addr, uint32_t offs, uint32_t val)
{
	writel(val, (uint32_t *)(addr + offs));
}

#endif /* __NSS_REGS_H */
