/**
 * @file bsp.c
 * @brief Board-support-package lifecycle implementation.
 */
#include "bsp/bsp.h"

#include "bsp/bsp_eeprom.h"
#include "bsp/bsp_external_flash.h"
#include "bsp/bsp_sdram.h"
#include "usart.h"

#define BSP_EEPROM_ADDRESS_7BIT       0x50U
#define BSP_EEPROM_WRITE_TIMEOUT_MS   10U

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

    {
        const bsp_eeprom_config_t eeprom_config = {
            BSP_EEPROM_ADDRESS_7BIT,
            BSP_EEPROM_WRITE_TIMEOUT_MS,
        };

        status = BSP_EepromInit(&eeprom_config);
    }
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
