/**
 * @file bsp_debug.h
 * @brief Blocking byte output through the board debug UART.
 */
#ifndef BSP_DEBUG_H
#define BSP_DEBUG_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

/**
 * @brief Transmit bytes through the initialized board debug UART.
 *
 * @param[in] data Source bytes; may be NULL only when @p size is zero.
 * @param[in] size Number of bytes to transmit.
 *
 * @return FIRMWARE_STATUS_OK when all bytes are transmitted.
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT for a NULL buffer with nonzero size.
 * @return FIRMWARE_STATUS_INVALID_STATE before BSP initialization.
 * @return FIRMWARE_STATUS_IO_ERROR if a UART transfer fails.
 *
 * @note The operation may block for up to 20 ms per HAL transfer chunk.
 */
firmware_status_t BSP_DebugWrite(const uint8_t *data, size_t size);

#endif
