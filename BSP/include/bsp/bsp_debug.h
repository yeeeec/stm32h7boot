/**
 * @file bsp_debug.h
 * @brief 通过板级 Debug UART 阻塞输出字节。
 */
#ifndef BSP_DEBUG_H
#define BSP_DEBUG_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 通过已初始化的板级 Debug UART 发送字节。
     *
     * @param[in] data 源字节；仅当 @p size 为零时允许为 NULL。
     * @param[in] size 要发送的字节数。
     *
     * @return 所有字节发送完成时返回 FIRMWARE_STATUS_OK。
     * @return Buffer 为 NULL 且 size 非零时返回 FIRMWARE_STATUS_INVALID_ARGUMENT。
     * @return BSP 初始化前返回 FIRMWARE_STATUS_INVALID_STATE。
     * @return UART 传输失败时返回 FIRMWARE_STATUS_IO_ERROR。
     *
     * @note 调用是同步的，每个传输 Chunk 可能阻塞至配置的 HAL Timeout；函数
     *       返回后不会保留 @p data。
     */
    firmware_status_t BSP_DebugWrite(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
