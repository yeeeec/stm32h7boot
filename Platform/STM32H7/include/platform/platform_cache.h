/**
 * @file platform_cache.h
 * @brief STM32H7 D-Cache maintenance helpers for DMA buffers.
 */
#ifndef PLATFORM_CACHE_H
#define PLATFORM_CACHE_H

#include <stddef.h>

#include "firmware/status.h"

#define PLATFORM_DCACHE_LINE_SIZE 32U /**< STM32H7 D-Cache line size in bytes. */

/**
 * @brief Clean D-Cache lines covering a DMA source buffer.
 *
 * @param[in] address Start address; must be 32-byte aligned.
 * @param[in] size Buffer size in bytes; must be nonzero and a multiple of 32.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT if address or size is invalid.
 *
 * @pre The buffer is located in memory accessible to the target DMA engine.
 */
firmware_status_t Platform_DCacheClean(const void *address, size_t size);

/**
 * @brief Invalidate D-Cache lines covering a DMA destination buffer.
 *
 * @param[in] address Start address; must be 32-byte aligned.
 * @param[in] size Buffer size in bytes; must be nonzero and a multiple of 32.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT if address or size is invalid.
 *
 * @pre The DMA transfer has completed and the buffer is no longer being written.
 */
firmware_status_t Platform_DCacheInvalidate(const void *address, size_t size);

/**
 * @brief Clean and then invalidate D-Cache lines covering a buffer.
 *
 * @param[in] address Start address; must be 32-byte aligned.
 * @param[in] size Buffer size in bytes; must be nonzero and a multiple of 32.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT if address or size is invalid.
 */
firmware_status_t Platform_DCacheCleanInvalidate(const void *address, size_t size);

#endif
