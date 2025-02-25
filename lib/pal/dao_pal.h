/* SPDX-License-Identifier: Marvell-Proprietary
 * Copyright(C) 2024 Marvell.
 */

/**
 * @file dao_pal.h
 *
 * DAO platform abstraction library
 */
#ifndef _VIRTIO_HELPER__H__
#define _VIRTIO_HELPER__H__
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <rte_branch_prediction.h>
#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_dmadev.h>
#include <rte_vfio.h>

#include <dao_dma.h>
#include <dao_log.h>
#include <dao_pem.h>
#include <dao_virtio_netdev.h>

#define DAO_PAL_MAX_WORKERS RTE_MAX_LCORE

/** DAO PAL library initialization data structure format */
typedef struct dao_pal_global_conf {
	/** Miscellaneous device list */
	char **misc_devices;
	/** Number of Miscellaneous devices present in misc_devices */
	uint16_t nb_misc_devices;
	/** DMA device list */
	char **dma_devices;
	/** Number of DMA devices present in dma_devices */
	uint16_t nb_dma_devs;
	/** Number of virtio devices */
	uint16_t nb_virtio_devs;
	/** PEM device identifier */
	uint8_t pem_devid;
} dao_pal_global_conf_t;

/** Worker to DMA mapping list data structure format */
typedef struct dao_pal_lcore_dma_id {
	/** Application worker identifier */
	int32_t wrk_id;
	/** Memory to Device DMA device ID */
	int16_t m2d_dma_devid;
	/** Device to Memory DMA device ID */
	int16_t d2m_dma_devid;
} dao_pal_lcore_dma_id_t;

/**
 * DAO PAL library initialization function
 *
 * @param conf
 *   Pointer to library initialization data structure.
 * @return
 *   0 on success, -1 on failure
 */
int dao_pal_global_init(dao_pal_global_conf_t *conf);

/**
 * Enable or Disable auto free on a mem2dev dma device's vchan of a given lcore
 * for all virtio devices.
 * @param wrk_id
 *   Worker ID to fetch mapped memory to device DMA ID.
 * @param enable
 *   Flag
 * @return
 *   Zero on success, -1 on failure.
 */
int dao_pal_dma_lcore_mem2dev_autofree_set(uint32_t wrk_id, bool enable);

/**
 * Set device to memory and memory to device DMA devices per lcore to backend.
 * @param wrk_id
 *   Worker ID to fetch mapped DMA ID's.
 * @return
 *   Zero on success, -1 on failure.
 */
int dao_pal_thread_init(uint32_t wrk_id);

/**
 * Map DMA device's virtual channel to vfio.
 * @param devid
 *   vfio ID.
 * @param dma_vchan
 *   DMA's virtul channel ID
 * @param pool
 *   This memory pool is valid if no external packet buffer pool is available.
 * @return
 *   Zero on success, -1 on failure.
 */
int dao_pal_dma_vchan_setup(uint32_t devid, uint16_t dma_vchan, void *pool);

/**
 * Allocate DMA devices per lcore, each for inbound anf outbound.
 * @param wrk_mask
 *   Worker mask to allocate DMA devices per lcore.
 * @return
 *   Zero on success, -1 on failure.
 */
int dao_pal_dma_dev_setup(uint64_t wrk_mask);

/**
 * Unregister current thread with backend.
 * @param wrk_id
 *   Worker ID to fetch mapped DMA ID's.
 * @return
 *   Zero on success, -1 on failure.
 */
int dao_pal_thread_fini(uint32_t wrk_id);

/**
 * Assign DMA devices for control path.
 * @param wrk_id
 *   Control plain thread  ID to fetch mapped DMA ID's.
 * @return
 *   Zero on success, -1 on failure.
 */
int dao_pal_dma_ctrl_dev_set(uint32_t wrk_id);

/**
 * DAO PAL library uninitialization function.
 *
 */
void dao_pal_global_fini(void);

/**
 * Perform DMA mapping for devices.
 * for all virtio devices.
 * @param vaddr
 *    Starting virtual address of memory to be mapped.
 * @param iova
 *   Starting IOVA address of memory to be mapped.
 * @param len
 *   Length of memory segment being mapped.
 * @return
 *   0 if successful
 *   <0 if failed
 */
int dao_pal_vfio_dma_map(uint64_t vaddr, uint64_t iova, uint64_t len);

/**
 * Get the iova mode
 *
 * @return
 *   enum rte_iova_mode value.
 */
enum rte_iova_mode dao_pal_iova_mode(void);

/**
 * Change the stream that will be used by the logging system.
 *
 * This can be done at any time. The f argument represents the stream
 * to be used to send the logs. If f is NULL, the default output is
 * used (stderr).
 *
 * @param f
 *   Pointer to the stream.
 * @return
 *   - 0 on success.
 *   - Negative on error.
 */

int dao_pal_openlog_stream(FILE *f);
#endif
