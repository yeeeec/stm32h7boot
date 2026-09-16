#ifndef BSP_LOG_UART_H
#define BSP_LOG_UART_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

/**
 * @brief Write all bytes through huart1.
 *
 * The context argument is unused and exists so this function can be assigned
 * directly to log_output_port_t.write.
 */
firmware_status_t BspLogUart_Write(void *context, const uint8_t *data, size_t size);

#endif
