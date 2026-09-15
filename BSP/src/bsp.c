#include "bsp/bsp.h"

#include "bsp/bsp_can.h"
#include "bsp/bsp_display.h"
#include "bsp/bsp_sdram.h"
#include "bsp/bsp_touch.h"
#include "firmware/display_layout.h"
#include "usart.h"

#define BSP_TOUCH_ADDRESS_7BIT        0x5DU
#define BSP_TOUCH_TRANSFER_TIMEOUT_MS 5U

static int bsp_initialized;
static int bsp_early_init_attempted;
static int bsp_early_initialized;

firmware_status_t BSP_EarlyInit(void)
{
    firmware_status_t status;

    if (bsp_early_init_attempted != 0)
        return FIRMWARE_STATUS_INVALID_STATE;
    bsp_early_init_attempted = 1;

    status = BSP_SdramInit();
    if (FirmwareStatus_IsError(status))
        return status;

    bsp_early_initialized = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BSP_Init(void)
{
    firmware_status_t status;
    const bsp_touch_config_t touchConfig = {
        BSP_TOUCH_ADDRESS_7BIT,
        BSP_TOUCH_TRANSFER_TIMEOUT_MS,
    };

    if (bsp_initialized != 0 || bsp_early_initialized == 0)
        return FIRMWARE_STATUS_INVALID_STATE;

    BSP_BacklightInit();
    if (huart1.gState == HAL_UART_STATE_RESET)
        return FIRMWARE_STATUS_INVALID_STATE;

    status = BSP_CAN_Init();
    if (FirmwareStatus_IsError(status))
        return status;
    status = BSP_TouchInit(&touchConfig);
    if (FirmwareStatus_IsError(status))
        return status;

    bsp_initialized = 1;
    return FIRMWARE_STATUS_OK;
}

int BSP_IsEarlyInitialized(void)
{
    return bsp_early_initialized;
}

int BSP_IsInitialized(void)
{
    return bsp_initialized;
}
