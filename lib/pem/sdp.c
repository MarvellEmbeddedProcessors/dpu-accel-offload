/* SPDX-License-Identifier: Marvell-MIT
 * Copyright (c) 2024 Marvell.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "sdp.h"
#include <dao_log.h>
#include <dao_util.h>

#define SDP_PLAT_DEV_NAME  "86e000000000.dpi_sdp_regs"
#define SDP0_PCIE_DEV_NAME "0002:18:00.0"
#define SDP1_PCIE_DEV_NAME "0002:19:00.0"

int
sdp_reg_write(struct dao_vfio_device *sdp_pdev, uint64_t offset, uint64_t val)
{
	uint8_t rbar = sdp_pdev->rbar;

	if (offset > sdp_pdev->mem[rbar].len)
		return -ENOMEM;

	*((volatile uint64_t *)(sdp_pdev->mem[rbar].addr + offset)) = val;
	return 0;
}

uint64_t
sdp_reg_read(struct dao_vfio_device *sdp_pdev, uint64_t offset)
{
	uint8_t rbar = sdp_pdev->rbar;

	if (offset > sdp_pdev->mem[rbar].len)
		return -ENOMEM;

	return *(volatile uint64_t *)(sdp_pdev->mem[rbar].addr + offset);
}

uint64_t *
sdp_reg_addr(struct dao_vfio_device *sdp_pdev, uint64_t offset)
{
	uint8_t rbar = sdp_pdev->rbar;

	if (offset > sdp_pdev->mem[rbar].len)
		return NULL;

	return (uint64_t *)(sdp_pdev->mem[rbar].addr + offset);
}

int
sdp_init(struct dao_vfio_device *sdp_pdev)
{
	uint8_t idx, ring_idx, rpvf, vfid, num_vfs;
	uint64_t reg_val, info, base;
	int rc;

	rc = dao_vfio_device_setup(SDP_PLAT_DEV_NAME, sdp_pdev);
	if (rc < 0) {
		sdp_pdev->type = DAO_VFIO_DEV_PCIE;
		if (sdp_pdev->prime)
			rc = dao_vfio_device_setup(SDP0_PCIE_DEV_NAME, sdp_pdev);
		else
			rc = dao_vfio_device_setup(SDP1_PCIE_DEV_NAME, sdp_pdev);
		if (rc < 0) {
			dao_err("Filed to setup DAO VFIO device %s",
				sdp_pdev->prime ? SDP0_PCIE_DEV_NAME : SDP1_PCIE_DEV_NAME);
			return errno;
		}
		sdp_pdev->rbar = DAO_VFIO_DEV_BAR2;
		sdp_pdev->mbar = DAO_VFIO_DEV_BAR4;
	} else {
		sdp_pdev->type = DAO_VFIO_DEV_PLATFORM;
		sdp_pdev->rbar = DAO_VFIO_DEV_BAR0;
	}

	if (sdp_pdev->prime) {
		base = sdp_pdev->rbar ? 0 : 0x80000000;
		reg_val = sdp_reg_read(sdp_pdev, base + SDP_EPFX_RINFO(0));
		reg_val &= ~SDP_EPFX_RINFO_SRN_MASK;
		sdp_reg_write(sdp_pdev, base + SDP_EPFX_RINFO(0), reg_val);
		reg_val = sdp_reg_read(sdp_pdev, base + SDP_EPFX_RINFO(0));
		rpvf = (reg_val >> SDP_EPFX_RINFO_RPVF_SHIFT) & 0xf;
		num_vfs = (reg_val >> SDP_EPFX_RINFO_RPVF_SHIFT) & 0x7f;
		/* Disable PF Ring */
		reg_val = sdp_reg_read(sdp_pdev, base + SDP_MAC0_PF_RING_CTL);
		reg_val &= ~SDP_MAC0_PF_RING_CTL_RPPF_MASK;
		sdp_reg_write(sdp_pdev, base + SDP_MAC0_PF_RING_CTL, reg_val);

		for (vfid = 1; vfid <= num_vfs; vfid++) {
			for (idx = 0; idx < rpvf; idx++) {
				ring_idx = idx + ((vfid - 1) * rpvf);

				sdp_reg_write(sdp_pdev, base + SDP_EPVF_RINGX(ring_idx), vfid);
			}
		}

		vfid = 0;
		info = rpvf | ((uint64_t)vfid << 8) | ((uint64_t)num_vfs << 16);
		info <<= 32;
		sdp_reg_write(sdp_pdev, base + SDP_PF_MBOX_DATA(0), info);
		vfid = num_vfs >> 1;
		info = rpvf | ((uint64_t)vfid << 8) | ((uint64_t)num_vfs << 16);
		info <<= 32;
		sdp_reg_write(sdp_pdev, base + SDP_PF_MBOX_DATA(32), info);
	}

	return 0;
}

void
sdp_fini(struct dao_vfio_device *sdp_pdev)
{
	dao_vfio_device_free(sdp_pdev);
}
