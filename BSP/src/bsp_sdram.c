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
#include <stdint.h>

#include "bsp/bsp_memory_map.h"
#include "fmc.h"
#include "sdram.h"

#define BSP_SDRAM_COMMAND_TIMEOUT_MS 100U
#define BSP_SDRAM_STARTUP_DELAY_MS   1U
#define BSP_SDRAM_AUTO_REFRESH_COUNT 8U
#define BSP_SDRAM_REFRESH_PERIOD_US  64000ULL
#define BSP_SDRAM_REFRESH_MARGIN     20ULL
#define BSP_SDRAM_REFRESH_RATE_MAX   8191ULL

#define BSP_SDRAM_MODE_BURST_LENGTH_1 0x0000U
#define BSP_SDRAM_MODE_SEQUENTIAL     0x0000U
#define BSP_SDRAM_MODE_CAS_LATENCY_3  0x0030U
#define BSP_SDRAM_MODE_STANDARD       0x0000U
#define BSP_SDRAM_MODE_SINGLE_WRITE   0x0200U
#define BSP_SDRAM_TEST_PATTERN        0xA5A5A5A5UL

#define PLATFORM_DCACHE_LINE_SIZE 32U /**< STM32H7 D-Cache Line 的字节数。 */

static sdram_t board_sdram;
static int sdram_init_attempted;
static int sdram_ready;

static const bsp_sdram_layout_t sdram_layout = {
    BSP_SDRAM_BASE_ADDRESS,
    BSP_SDRAM_SIZE_BYTES,
    {BSP_FRAMEBUFFER0_ADDRESS, BSP_FRAMEBUFFER1_ADDRESS},
    BSP_FRAMEBUFFER_REGION_SIZE_BYTES,
    BSP_SDRAM_APP_ADDRESS,
    BSP_SDRAM_APP_SIZE_BYTES,
};

static int RangeContains(uintptr_t region_address, size_t region_size, uintptr_t address,
                         size_t size)
{
    uintptr_t offset;

    if ((size == 0U) || (address < region_address))
    {
        return 0;
    }

    offset = address - region_address;
    return (offset < region_size) && (size <= (region_size - (size_t) offset));
}

static uint32_t GetCkperClockHz(void)
{
    switch (__HAL_RCC_GET_CLKP_SOURCE())
    {
        case RCC_CLKPSOURCE_HSI:
            if (!HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSIRDY))
            {
                return 0U;
            }
            return HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3U);
        case RCC_CLKPSOURCE_CSI:
            return HAL_IS_BIT_SET(RCC->CR, RCC_CR_CSIRDY) ? CSI_VALUE : 0U;
        case RCC_CLKPSOURCE_HSE:
            return HAL_IS_BIT_SET(RCC->CR, RCC_CR_HSERDY) ? HSE_VALUE : 0U;
        default:
            return 0U;
    }
}

static uint32_t GetFmcKernelClockHz(void)
{
    switch (__HAL_RCC_GET_FMC_SOURCE())
    {
        case RCC_FMCCLKSOURCE_HCLK:
            return HAL_RCC_GetHCLKFreq();
        case RCC_FMCCLKSOURCE_PLL:
        {
            PLL1_ClocksTypeDef clocks = {0};

            if (!HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL1RDY))
            {
                return 0U;
            }
            HAL_RCCEx_GetPLL1ClockFreq(&clocks);
            return clocks.PLL1_Q_Frequency;
        }
        case RCC_FMCCLKSOURCE_PLL2:
        {
            PLL2_ClocksTypeDef clocks = {0};

            if (!HAL_IS_BIT_SET(RCC->CR, RCC_CR_PLL2RDY))
            {
                return 0U;
            }
            HAL_RCCEx_GetPLL2ClockFreq(&clocks);
            return clocks.PLL2_R_Frequency;
        }
        case RCC_FMCCLKSOURCE_CLKP:
            return GetCkperClockHz();
        default:
            return 0U;
    }
}

static firmware_status_t CalculateRefreshRate(uint32_t *refresh_rate)
{
    uint32_t kernel_clock_hz;
    uint32_t sdclk_divider;
    uint32_t row_count;
    uint64_t refresh_denominator;
    uint64_t refresh_cycles;

    if (refresh_rate == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    kernel_clock_hz = GetFmcKernelClockHz();
    if (kernel_clock_hz == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    switch (hsdram1.Init.SDClockPeriod)
    {
        case FMC_SDRAM_CLOCK_PERIOD_2:
            sdclk_divider = 2U;
            break;
        case FMC_SDRAM_CLOCK_PERIOD_3:
            sdclk_divider = 3U;
            break;
        default:
            return FIRMWARE_STATUS_INVALID_STATE;
    }

    switch (hsdram1.Init.RowBitsNumber)
    {
        case FMC_SDRAM_ROW_BITS_NUM_11:
            row_count = 2048U;
            break;
        case FMC_SDRAM_ROW_BITS_NUM_12:
            row_count = 4096U;
            break;
        case FMC_SDRAM_ROW_BITS_NUM_13:
            row_count = 8192U;
            break;
        default:
            return FIRMWARE_STATUS_INVALID_STATE;
    }

    refresh_denominator = (uint64_t) sdclk_divider * 1000000ULL * row_count;
    refresh_cycles =
        ((uint64_t) kernel_clock_hz * BSP_SDRAM_REFRESH_PERIOD_US + (refresh_denominator / 2ULL)) /
        refresh_denominator;
    if (refresh_cycles <= BSP_SDRAM_REFRESH_MARGIN)
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    refresh_cycles -= BSP_SDRAM_REFRESH_MARGIN;
    if (refresh_cycles > BSP_SDRAM_REFRESH_RATE_MAX)
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    *refresh_rate = (uint32_t) refresh_cycles;
    return FIRMWARE_STATUS_OK;
}

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
    firmware_status_t status;

    if ((sdram_init_attempted != 0) || (hsdram1.State == HAL_SDRAM_STATE_RESET))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    sdram_init_attempted = 1;

    status = CalculateRefreshRate(&config.refresh_rate);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
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

    status = Sdram_Init(&board_sdram, &port, &config);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    sdram_ready = 1;
    return FIRMWARE_STATUS_OK;
}

int BSP_SdramIsReady(void)
{
    return sdram_ready;
}

const bsp_sdram_layout_t *BSP_SdramGetLayout(void)
{
    return &sdram_layout;
}

firmware_status_t BSP_SdramDCacheClean(uintptr_t address, size_t size)
{
    if (sdram_ready == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (!RangeContains(sdram_layout.base_address, sdram_layout.size_bytes, address, size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    SCB_CleanDCache_by_Addr((uint32_t *) address, (int32_t) size);

    return FIRMWARE_STATUS_OK;
}

firmware_status_t BSP_SdramDCacheInvalidate(uintptr_t address, size_t size)
{
    if (sdram_ready == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (!RangeContains(sdram_layout.base_address, sdram_layout.size_bytes, address, size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    SCB_InvalidateDCache_by_Addr((uint32_t *) address, (int32_t) size);

    return FIRMWARE_STATUS_OK;
}

firmware_status_t BSP_SdramDCacheCleanInvalidate(uintptr_t address, size_t size)
{
    if (sdram_ready == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (!RangeContains(sdram_layout.base_address, sdram_layout.size_bytes, address, size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    SCB_CleanInvalidateDCache_by_Addr((uint32_t *) address, (int32_t) size);

    return FIRMWARE_STATUS_OK;
}

firmware_status_t BSP_SdramTest(uintptr_t address, size_t size)
{
    volatile uint32_t *words;
    size_t word_count;
    size_t index;
    firmware_status_t status = FIRMWARE_STATUS_OK;
    firmware_status_t cache_status;

    if (sdram_ready == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (!RangeContains(sdram_layout.app_address, sdram_layout.app_size_bytes, address, size))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    if (((address % PLATFORM_DCACHE_LINE_SIZE) != 0U) || ((size % PLATFORM_DCACHE_LINE_SIZE) != 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    words      = (volatile uint32_t *) address;
    word_count = size / sizeof(*words);

    for (index = 0U; index < word_count; ++index)
    {
        words[index] = ((uint32_t) (address + (index * sizeof(*words)))) ^ BSP_SDRAM_TEST_PATTERN;
    }
    cache_status = BSP_SdramDCacheCleanInvalidate(address, size);
    if (!FirmwareStatus_IsOk(cache_status))
    {
        return cache_status;
    }

    for (index = 0U; index < word_count; ++index)
    {
        uint32_t expected =
            ((uint32_t) (address + (index * sizeof(*words)))) ^ BSP_SDRAM_TEST_PATTERN;

        if (words[index] != expected)
        {
            status = FIRMWARE_STATUS_IO_ERROR;
            break;
        }
    }

    if (FirmwareStatus_IsOk(status))
    {
        for (index = 0U; index < word_count; ++index)
        {
            words[index] =
                ~(((uint32_t) (address + (index * sizeof(*words)))) ^ BSP_SDRAM_TEST_PATTERN);
        }
        cache_status = BSP_SdramDCacheCleanInvalidate(address, size);
        if (!FirmwareStatus_IsOk(cache_status))
        {
            return cache_status;
        }

        for (index = 0U; index < word_count; ++index)
        {
            uint32_t expected =
                ~(((uint32_t) (address + (index * sizeof(*words)))) ^ BSP_SDRAM_TEST_PATTERN);

            if (words[index] != expected)
            {
                status = FIRMWARE_STATUS_IO_ERROR;
                break;
            }
        }
    }

    /* 无论测试结果如何，都将调用方授权的破坏性区域归零。 */
    for (index = 0U; index < word_count; ++index)
    {
        words[index] = 0U;
    }
    cache_status = BSP_SdramDCacheCleanInvalidate(address, size);
    return FirmwareStatus_IsOk(cache_status) ? status : cache_status;
}
