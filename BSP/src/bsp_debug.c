#include "bsp/bsp_debug.h"

#include <limits.h>

#include "bsp/bsp.h"
#include "usart.h"

#define BSP_DEBUG_TIMEOUT_MS 20U

firmware_status_t BSP_DebugWrite(const uint8_t *data, size_t size)
{
    size_t offset = 0U;

    if ((data == NULL) && (size != 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    if (BSP_IsInitialized() == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    while (offset < size)
    {
        size_t remaining = size - offset;
        uint16_t chunk = (remaining > UINT16_MAX) ? UINT16_MAX : (uint16_t)remaining;

        if (HAL_UART_Transmit(&huart1, (uint8_t *)&data[offset], chunk,
                              BSP_DEBUG_TIMEOUT_MS) != HAL_OK)
        {
            return FIRMWARE_STATUS_IO_ERROR;
        }
        offset += chunk;
    }

    return FIRMWARE_STATUS_OK;
}
