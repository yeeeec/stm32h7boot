/**
 * @file platform_cache.h
 * @brief STM32H7 D-Cache 与 DMA 可见 Buffer 的 Ownership 切换。
 *
 * API 要求使用完整 Cache Line，避免 Maintenance 意外清理或丢弃相邻对象的
 * 无关数据。DMA Engine 可访问性及 CPU/DMA 并发访问互斥仍由调用者负责。
 */
#ifndef PLATFORM_CACHE_H
#define PLATFORM_CACHE_H

#include <stddef.h>

#include "firmware/status.h"

#define PLATFORM_DCACHE_LINE_SIZE 32U /**< STM32H7 D-Cache Line 的字节数。 */

/**
 * @brief Clean 覆盖 DMA Source Buffer 的 D-Cache Line。
 *
 * @param[in] address 起始地址；必须按 32 字节对齐。
 * @param[in] size Buffer 字节数；必须非零且为 32 的倍数。
 *
 * @return 成功时返回 FIRMWARE_STATUS_OK。
 * @return 地址或大小无效时返回 FIRMWARE_STATUS_INVALID_ARGUMENT。
 *
 * @pre Buffer 位于目标 DMA Engine 可访问的内存中。
 * @pre CPU 对 Buffer 的写入已经完成，且 DMA Engine 读取完成前 CPU 不再写入。
 */
firmware_status_t Platform_DCacheClean(const void *address, size_t size);

/**
 * @brief Invalidate 覆盖 DMA Destination Buffer 的 D-Cache Line。
 *
 * @param[in] address 起始地址；必须按 32 字节对齐。
 * @param[in] size Buffer 字节数；必须非零且为 32 的倍数。
 *
 * @return 成功时返回 FIRMWARE_STATUS_OK。
 * @return 地址或大小无效时返回 FIRMWARE_STATUS_INVALID_ARGUMENT。
 *
 * @pre DMA 传输已经完成，且 Buffer 不再被写入。
 * @pre DMA 持有 Buffer 期间，CPU 没有向 Destination Cache Line 写入脏数据。
 */
firmware_status_t Platform_DCacheInvalidate(const void *address, size_t size);

/**
 * @brief Clean 后再 Invalidate 覆盖 Buffer 的 D-Cache Line。
 *
 * @param[in] address 起始地址；必须按 32 字节对齐。
 * @param[in] size Buffer 字节数；必须非零且为 32 的倍数。
 *
 * @return 成功时返回 FIRMWARE_STATUS_OK。
 * @return 地址或大小无效时返回 FIRMWARE_STATUS_INVALID_ARGUMENT。
 *
 * @note 仅当需要同时协调调用者持有 Buffer 的 CPU 脏数据和潜在过期 Cache
 *       Line 时使用。
 * @pre 调用者在 Maintenance 期间独占 Buffer，并已协调 DMA 传输边界。
 */
firmware_status_t Platform_DCacheCleanInvalidate(const void *address, size_t size);

#endif
