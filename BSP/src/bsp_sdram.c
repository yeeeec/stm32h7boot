/**
 * @file bsp_sdram.c
 * @brief 外部 SDRAM 的板级时序和 STM32 FMC 绑定。
 *
 * 可移植 SDRAM 驱动负责必需的上电命令顺序。本模块持有静态驱动实例，并提供
 * 板级 Bank、Mode Register、Refresh 和 HAL Status 转换。BSP_SdramInit() 成功
 * 完成前不得访问外部 SDRAM。
 */
#include "bsp/bsp_sdram.h"

#include <stddef.h>

#include "fmc.h"
#include "sdram.h"

#define BSP_SDRAM_COMMAND_TIMEOUT_MS 100U
#define BSP_SDRAM_STARTUP_DELAY_MS   1U
#define BSP_SDRAM_AUTO_REFRESH_COUNT 8U

/* 根据板级 SDCLK、设备行周期和 FMC 裕量推导。 */
#define BSP_SDRAM_REFRESH_RATE 761U

#define BSP_SDRAM_MODE_BURST_LENGTH_1 0x0000U
#define BSP_SDRAM_MODE_SEQUENTIAL     0x0000U
#define BSP_SDRAM_MODE_CAS_LATENCY_3  0x0030U
#define BSP_SDRAM_MODE_STANDARD       0x0000U
#define BSP_SDRAM_MODE_SINGLE_WRITE   0x0200U

static sdram_t board_sdram;

static firmware_status_t HalStatus(HAL_StatusTypeDef status)
{
    if (status == HAL_OK)
    {
        return FIRMWARE_STATUS_OK;
    }
    if (status == HAL_TIMEOUT)
    {
        return FIRMWARE_STATUS_TIMEOUT;
    }
    return FIRMWARE_STATUS_IO_ERROR;
}

static firmware_status_t SendCommand(void *context, sdram_command_t command,
                                     uint32_t auto_refresh_count, uint32_t mode_register)
{
    SDRAM_HandleTypeDef *handle          = (SDRAM_HandleTypeDef *) context;
    FMC_SDRAM_CommandTypeDef hal_command = {0};

    /* 所有板级命令都指向实际安装的唯一 SDRAM Bank。 */
    hal_command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    hal_command.AutoRefreshNumber      = auto_refresh_count;
    hal_command.ModeRegisterDefinition = mode_register;

    switch (command)
    {
        case SDRAM_COMMAND_CLOCK_ENABLE:
            hal_command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
            break;
        case SDRAM_COMMAND_PRECHARGE_ALL:
            hal_command.CommandMode = FMC_SDRAM_CMD_PALL;
            break;
        case SDRAM_COMMAND_AUTO_REFRESH:
            hal_command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
            break;
        case SDRAM_COMMAND_LOAD_MODE:
            hal_command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
            break;
        default:
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    return HalStatus(HAL_SDRAM_SendCommand(handle, &hal_command, BSP_SDRAM_COMMAND_TIMEOUT_MS));
}

static firmware_status_t SetRefreshRate(void *context, uint32_t refresh_rate)
{
    return HalStatus(HAL_SDRAM_ProgramRefreshRate((SDRAM_HandleTypeDef *) context, refresh_rate));
}

static void DelayMs(void *context, uint32_t delay_ms)
{
    (void) context;
    HAL_Delay(delay_ms);
}

firmware_status_t BSP_SdramInit(void)
{
    sdram_port_t port;
    sdram_config_t config;

    if (hsdram1.State == HAL_SDRAM_STATE_RESET)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    port.context          = &hsdram1;
    port.send_command     = SendCommand;
    port.set_refresh_rate = SetRefreshRate;
    port.delay_ms         = DelayMs;

    config.startup_delay_ms   = BSP_SDRAM_STARTUP_DELAY_MS;
    config.auto_refresh_count = BSP_SDRAM_AUTO_REFRESH_COUNT;
    config.mode_register      = BSP_SDRAM_MODE_BURST_LENGTH_1 | BSP_SDRAM_MODE_SEQUENTIAL |
                                BSP_SDRAM_MODE_CAS_LATENCY_3 | BSP_SDRAM_MODE_STANDARD |
                                BSP_SDRAM_MODE_SINGLE_WRITE;
    config.refresh_rate       = BSP_SDRAM_REFRESH_RATE;

    return Sdram_Init(&board_sdram, &port, &config);
}
