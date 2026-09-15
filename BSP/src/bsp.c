/**
 * @file bsp.c
 * @brief 按顺序构建 BSP 持有的板级设备。
 *
 * CubeMX 负责创建外设 Handle；本模块校验这些 Handle，并将其绑定到可移植
 * 设备驱动。设备初始化有固定顺序且不是事务性的。失败可能使前面的设备保持
 * 就绪，但 BSP_IsInitialized() 仍为 false，上层必须 Fail-closed。
 */
#include "bsp/bsp.h"

#include "fatfs.h"
#include "gpio.h"
#include "i2c.h"
#include "quadspi.h"
#include "sdmmc.h"
#include "bsp/bsp_eeprom.h"
#include "bsp/bsp_external_flash.h"
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

    /* All CubeMX handles used by the boot flow are initialized here. */
    MX_GPIO_Init();
    MX_QUADSPI_Init();
    MX_I2C1_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    MX_SDMMC1_SD_Init();
    MX_FATFS_Init();

    if (huart1.gState == HAL_UART_STATE_RESET)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
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

    /* 只有整套板级依赖有效后才发布就绪状态。 */
    bsp_initialized = 1;
    return FIRMWARE_STATUS_OK;
}

int BSP_IsInitialized(void)
{
    return bsp_initialized;
}
