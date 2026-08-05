#include "bsp/bsp.h"

#include "bsp/bsp_external_flash.h"
#include "bsp/bsp_sdram.h"
#include "usart.h"

static int bsp_initialized;

firmware_status_t BSP_Init(void)
{
    firmware_status_t status;

    if (bsp_initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    if (huart1.gState == HAL_UART_STATE_RESET)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = BSP_SdramInit();
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    status = BSP_ExternalFlashInit();
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    bsp_initialized = 1;
    return FIRMWARE_STATUS_OK;
}

int BSP_IsInitialized(void)
{
    return bsp_initialized;
}
