#ifndef BSP_ISP_UART_H
#define BSP_ISP_UART_H

#include <stdint.h>

#include "firmware/status.h"

typedef enum
{
    /** Application communication: 115200 baud, 8 data bits, no parity, 1 stop bit. */
    BSP_ISP_UART_APPLICATION_MODE = 0,
    /** STM32 ROM bootloader: 115200 baud, 8 data bits, even parity, 1 stop bit. */
    BSP_ISP_UART_ROM_MODE
} bsp_isp_uart_mode_t;

firmware_status_t BspIspUart_Configure(bsp_isp_uart_mode_t mode);

firmware_status_t BspIspUart_Transmit(const uint8_t *data, uint32_t size, uint32_t timeout_ms);

firmware_status_t BspIspUart_Receive(uint8_t *data, uint32_t size, uint32_t timeout_ms);

#endif
