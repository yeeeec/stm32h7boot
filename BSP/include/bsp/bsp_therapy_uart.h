#ifndef BSP_THERAPY_UART_H
#define BSP_THERAPY_UART_H

#include <stdint.h>

#include "firmware/status.h"

typedef enum
{
    BSP_THERAPY_UART_APPLICATION_MODE = 0,
    BSP_THERAPY_UART_ROM_MODE
} bsp_therapy_uart_mode_t;

firmware_status_t BspTherapyUart_Configure(bsp_therapy_uart_mode_t mode);
firmware_status_t BspTherapyUart_Transmit(const uint8_t *data, uint32_t size, uint32_t timeout_ms);
firmware_status_t BspTherapyUart_Receive(uint8_t *data, uint32_t size, uint32_t timeout_ms);

#endif /* BSP_THERAPY_UART_H */
