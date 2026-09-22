#ifndef BSP_LOG_UART_H
#define BSP_LOG_UART_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

firmware_status_t BspLogUart_Write(const uint8_t *data, size_t size);

#endif
