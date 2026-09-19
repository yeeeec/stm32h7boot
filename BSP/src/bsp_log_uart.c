#include "bsp/bsp_log_uart.h"

#include "usart.h"

#include <limits.h>
#include <stddef.h>

#define BSP_LOG_UART_TIMEOUT_MS 100U

static firmware_status_t ConvertHalStatus(HAL_StatusTypeDef status)
{
    if (status == HAL_OK)
    {
        return FIRMWARE_STATUS_OK;
    }
    if (status == HAL_BUSY)
    {
        return FIRMWARE_STATUS_BUSY;
    }
    if (status == HAL_TIMEOUT)
    {
        return FIRMWARE_STATUS_TIMEOUT;
    }
    return FIRMWARE_STATUS_IO_ERROR;
}

firmware_status_t BspLogUart_Write(const uint8_t *data, size_t size)
{
    if ((data == NULL) || (size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    while (size != 0U)
    {
        uint16_t chunk           = (size > UINT16_MAX) ? UINT16_MAX : (uint16_t) size;
        firmware_status_t status = ConvertHalStatus(
            HAL_UART_Transmit(&huart1, (uint8_t *) data, chunk, BSP_LOG_UART_TIMEOUT_MS));

        if (status != FIRMWARE_STATUS_OK)
        {
            return status;
        }
        data += chunk;
        size -= chunk;
    }

    return FIRMWARE_STATUS_OK;
}
