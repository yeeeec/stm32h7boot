#include "platform/fmc.h"

#include <stddef.h>
#include <string.h>

#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_sdram.h"

enum {
    PLATFORM_FMC_SDRAM_TIMEOUT               = 0xFFFFu,
    PLATFORM_FMC_SDRAM_LCD_LAYER_SIZE        = 2u * 1024u * 1024u,
    PLATFORM_FMC_SDRAM_TOTAL_SIZE            = 32u * 1024u * 1024u,
    PLATFORM_FMC_SDRAM_LAYER_COUNT           = 2u,
    PLATFORM_FMC_SDRAM_ROW_COUNT             = 4096u,
    PLATFORM_FMC_SDRAM_REFRESH_WINDOW_MS     = 64u,
    PLATFORM_FMC_SDRAM_REFRESH_MARGIN_CYCLES = 20u
};

#define PLATFORM_FMC_SDRAM_BASE_ADDRESS ((uintptr_t) 0xC0000000u)

#define PLATFORM_FMC_SDRAM_MODEREG_BURST_LENGTH_1          ((uint16_t) 0x0000)
#define PLATFORM_FMC_SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL   ((uint16_t) 0x0000)
#define PLATFORM_FMC_SDRAM_MODEREG_CAS_LATENCY_3           ((uint16_t) 0x0030)
#define PLATFORM_FMC_SDRAM_MODEREG_OPERATING_MODE_STANDARD ((uint16_t) 0x0000)
#define PLATFORM_FMC_SDRAM_MODEREG_WRITEBURST_MODE_SINGLE  ((uint16_t) 0x0200)

static SDRAM_HandleTypeDef s_sdram_handle;
static bool s_sdram_ready = false;
static bool s_sdram_pins_ready = false;
static bool s_sdram_clock_ready = false;

static const Plat_FmcSdramLayout_t s_sdram_layout = {
    .base_address           = PLATFORM_FMC_SDRAM_BASE_ADDRESS,
    .size_bytes             = PLATFORM_FMC_SDRAM_TOTAL_SIZE,
    .lcd_layer0_address     = PLATFORM_FMC_SDRAM_BASE_ADDRESS,
    .lcd_layer1_address     = PLATFORM_FMC_SDRAM_BASE_ADDRESS + PLATFORM_FMC_SDRAM_LCD_LAYER_SIZE,
    .lcd_layer_size_bytes   = PLATFORM_FMC_SDRAM_LCD_LAYER_SIZE,
    .app_region_address     = PLATFORM_FMC_SDRAM_BASE_ADDRESS +
                          (PLATFORM_FMC_SDRAM_LCD_LAYER_SIZE * PLATFORM_FMC_SDRAM_LAYER_COUNT),
    .app_region_size_bytes  = PLATFORM_FMC_SDRAM_TOTAL_SIZE -
                             (PLATFORM_FMC_SDRAM_LCD_LAYER_SIZE * PLATFORM_FMC_SDRAM_LAYER_COUNT),
};

_Static_assert((PLATFORM_FMC_SDRAM_LCD_LAYER_SIZE * PLATFORM_FMC_SDRAM_LAYER_COUNT) <=
                   PLATFORM_FMC_SDRAM_TOTAL_SIZE,
               "SDRAM layout exceeds total SDRAM size");

static Plat_Status_t platform_status_from_hal(HAL_StatusTypeDef status) {
    switch (status) {
        case HAL_OK:
            return PLAT_OK;
        case HAL_TIMEOUT:
            return PLAT_ERR_TIMEOUT;
        case HAL_BUSY:
            return PLAT_ERR_BUSY;
        default:
            return PLAT_ERR_HW_FAILURE;
    }
}

typedef enum {
    PLATFORM_FMC_SDRAM_CACHE_OP_CLEAN = 0,
    PLATFORM_FMC_SDRAM_CACHE_OP_INVALIDATE,
    PLATFORM_FMC_SDRAM_CACHE_OP_CLEAN_INVALIDATE
} Platform_FmcSdramCacheOp_t;

static bool platform_fmc_sdram_range_is_valid(uintptr_t base_address, size_t size_bytes);

static Plat_Status_t platform_fmc_sdram_prepare_clock(void) {
    RCC_PeriphCLKInitTypeDef periph_clk = {0};

    if (s_sdram_clock_ready) {
        return PLAT_OK;
    }

    periph_clk.PeriphClockSelection = RCC_PERIPHCLK_FMC;
    periph_clk.FmcClockSelection    = RCC_FMCCLKSOURCE_D1HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&periph_clk) != HAL_OK) {
        return PLAT_ERR_HW_FAILURE;
    }

    s_sdram_clock_ready = true;
    return PLAT_OK;
}

static uint32_t platform_fmc_sdram_get_clock_divider(const SDRAM_HandleTypeDef *handle) {
    switch (handle->Init.SDClockPeriod) {
        case FMC_SDRAM_CLOCK_PERIOD_2:
            return 2u;
        case FMC_SDRAM_CLOCK_PERIOD_3:
            return 3u;
        default:
            return 0u;
    }
}

static Plat_Status_t platform_fmc_sdram_compute_refresh_count(const SDRAM_HandleTypeDef *handle,
                                                              uint32_t *refresh_count) {
    uint32_t fmc_kernel_clock_hz;
    uint32_t sdram_clock_divider;
    uint64_t refresh_cycles;
    uint64_t refresh_denominator;

    if ((handle == NULL) || (refresh_count == NULL)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    sdram_clock_divider = platform_fmc_sdram_get_clock_divider(handle);
    if (sdram_clock_divider == 0u) {
        return PLAT_ERR_INVALID_PARAM;
    }

    /* FMC kernel clock is explicitly sourced from D1HCLK in platform_fmc_sdram_prepare_clock(). */
    fmc_kernel_clock_hz = HAL_RCC_GetHCLKFreq();
    if (fmc_kernel_clock_hz == 0u) {
        return PLAT_ERR_HW_FAILURE;
    }

    refresh_denominator = (uint64_t) PLATFORM_FMC_SDRAM_ROW_COUNT * 1000u;
    refresh_cycles = ((uint64_t) fmc_kernel_clock_hz * PLATFORM_FMC_SDRAM_REFRESH_WINDOW_MS) /
                     sdram_clock_divider;
    refresh_cycles = (refresh_cycles + (refresh_denominator / 2u)) / refresh_denominator;
    if (refresh_cycles <= PLATFORM_FMC_SDRAM_REFRESH_MARGIN_CYCLES) {
        return PLAT_ERR_HW_FAILURE;
    }

    *refresh_count = (uint32_t) (refresh_cycles - PLATFORM_FMC_SDRAM_REFRESH_MARGIN_CYCLES);
    return PLAT_OK;
}

static Plat_Status_t platform_fmc_sdram_validate_cache_range(uintptr_t base_address,
                                                             size_t size_bytes) {
    if (!s_sdram_ready) {
        return PLAT_ERR_HW_FAILURE;
    }

    if (!platform_fmc_sdram_range_is_valid(base_address, size_bytes)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    if (size_bytes > (size_t) INT32_MAX) {
        return PLAT_ERR_INVALID_PARAM;
    }

    return PLAT_OK;
}

static Plat_Status_t platform_fmc_sdram_apply_cache_op(uintptr_t base_address, size_t size_bytes,
                                                       Platform_FmcSdramCacheOp_t op) {
    Plat_Status_t status;

    status = platform_fmc_sdram_validate_cache_range(base_address, size_bytes);
    if (status != PLAT_OK) {
        return status;
    }

    switch (op) {
        case PLATFORM_FMC_SDRAM_CACHE_OP_CLEAN:
            SCB_CleanDCache_by_Addr((uint32_t *) base_address, (int32_t) size_bytes);
            break;
        case PLATFORM_FMC_SDRAM_CACHE_OP_INVALIDATE:
            SCB_InvalidateDCache_by_Addr((void *) base_address, (int32_t) size_bytes);
            break;
        case PLATFORM_FMC_SDRAM_CACHE_OP_CLEAN_INVALIDATE:
            SCB_CleanInvalidateDCache_by_Addr((uint32_t *) base_address, (int32_t) size_bytes);
            break;
        default:
            return PLAT_ERR_INVALID_PARAM;
    }

    return PLAT_OK;
}

static void platform_fmc_sdram_config_gpio(void) {
    GPIO_InitTypeDef gpio_init = {0};

    if (s_sdram_pins_ready) {
        return;
    }

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_FMC_CLK_ENABLE();

    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF12_FMC;

    gpio_init.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                    GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio_init);

    gpio_init.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 |
                    GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |
                    GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio_init);

    gpio_init.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 |
                    GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |
                    GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gpio_init);

    gpio_init.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 |
                    GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &gpio_init);

    gpio_init.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_9 |
                    GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |
                    GPIO_PIN_15;
    HAL_GPIO_Init(GPIOH, &gpio_init);

    gpio_init.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 |
                    GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOI, &gpio_init);

    s_sdram_pins_ready = true;
}

static void platform_fmc_sdram_fill_handle(SDRAM_HandleTypeDef *handle) {
    handle->Instance                = FMC_SDRAM_DEVICE;
    handle->Init.SDBank             = FMC_SDRAM_BANK1;
    handle->Init.ColumnBitsNumber   = FMC_SDRAM_COLUMN_BITS_NUM_9;
    handle->Init.RowBitsNumber      = FMC_SDRAM_ROW_BITS_NUM_12;
    handle->Init.MemoryDataWidth    = FMC_SDRAM_MEM_BUS_WIDTH_32;
    handle->Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    handle->Init.CASLatency         = FMC_SDRAM_CAS_LATENCY_3;
    handle->Init.WriteProtection    = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    handle->Init.SDClockPeriod      = FMC_SDRAM_CLOCK_PERIOD_2;
    handle->Init.ReadBurst          = FMC_SDRAM_RBURST_ENABLE;
    handle->Init.ReadPipeDelay      = FMC_SDRAM_RPIPE_DELAY_0;
}

static void platform_fmc_sdram_fill_timing(FMC_SDRAM_TimingTypeDef *timing) {
    timing->LoadToActiveDelay    = 2;
    timing->ExitSelfRefreshDelay = 7;
    timing->SelfRefreshTime      = 4;
    timing->RowCycleDelay        = 7;
    timing->WriteRecoveryTime    = 2;
    timing->RPDelay              = 2;
    timing->RCDDelay             = 2;
}

static Plat_Status_t platform_fmc_sdram_run_init_sequence(uint32_t refresh_count) {
    FMC_SDRAM_CommandTypeDef command = {0};
    uint32_t mode_register           = PLATFORM_FMC_SDRAM_MODEREG_BURST_LENGTH_1 |
                             PLATFORM_FMC_SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
                             PLATFORM_FMC_SDRAM_MODEREG_CAS_LATENCY_3 |
                             PLATFORM_FMC_SDRAM_MODEREG_OPERATING_MODE_STANDARD |
                             PLATFORM_FMC_SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;
    Plat_Status_t status;

    command.CommandMode       = FMC_SDRAM_CMD_CLK_ENABLE;
    command.CommandTarget     = FMC_SDRAM_CMD_TARGET_BANK1;
    command.AutoRefreshNumber = 1;
    status                    = platform_status_from_hal(
        HAL_SDRAM_SendCommand(&s_sdram_handle, &command, PLATFORM_FMC_SDRAM_TIMEOUT));
    if (status != PLAT_OK) {
        return status;
    }

    HAL_Delay(1);

    command.CommandMode = FMC_SDRAM_CMD_PALL;
    status              = platform_status_from_hal(
        HAL_SDRAM_SendCommand(&s_sdram_handle, &command, PLATFORM_FMC_SDRAM_TIMEOUT));
    if (status != PLAT_OK) {
        return status;
    }

    command.CommandMode       = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    command.AutoRefreshNumber = 8;
    status                    = platform_status_from_hal(
        HAL_SDRAM_SendCommand(&s_sdram_handle, &command, PLATFORM_FMC_SDRAM_TIMEOUT));
    if (status != PLAT_OK) {
        return status;
    }

    command.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
    command.AutoRefreshNumber      = 1;
    command.ModeRegisterDefinition = mode_register;
    status                         = platform_status_from_hal(
        HAL_SDRAM_SendCommand(&s_sdram_handle, &command, PLATFORM_FMC_SDRAM_TIMEOUT));
    if (status != PLAT_OK) {
        return status;
    }

    return platform_status_from_hal(HAL_SDRAM_ProgramRefreshRate(&s_sdram_handle, refresh_count));
}

static bool platform_fmc_sdram_range_is_valid(uintptr_t base_address, size_t size_bytes) {
    uintptr_t layout_end;

    if (size_bytes == 0u) {
        return false;
    }

    layout_end = s_sdram_layout.base_address + s_sdram_layout.size_bytes;
    if ((base_address < s_sdram_layout.base_address) || (base_address >= layout_end)) {
        return false;
    }

    return size_bytes <= (size_t) (layout_end - base_address);
}

static Plat_Status_t platform_fmc_sdram_run_power_on_self_test(void) {
    size_t failed_offset = 0u;

    return platform_fmc_sdram_destructive_test(s_sdram_layout.app_region_address,
                                               s_sdram_layout.app_region_size_bytes,
                                               &failed_offset);
}

void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef *hsdram) {
    (void) hsdram;
    platform_fmc_sdram_config_gpio();
}

void HAL_SDRAM_MspDeInit(SDRAM_HandleTypeDef *hsdram) {
    (void) hsdram;

    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
                               GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15);
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |
                               GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
                               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
    HAL_GPIO_DeInit(GPIOF, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                               GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 |
                               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 |
                               GPIO_PIN_8 | GPIO_PIN_15);
    HAL_GPIO_DeInit(GPIOH, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_5 | GPIO_PIN_8 |
                               GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
                               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
    HAL_GPIO_DeInit(GPIOI, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                               GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 |
                               GPIO_PIN_9 | GPIO_PIN_10);

    s_sdram_pins_ready = false;
}

Plat_Status_t platform_fmc_sdram_init(void) {
    FMC_SDRAM_TimingTypeDef timing = {0};
    Plat_Status_t status;
    uint32_t refresh_count = 0u;

    if (s_sdram_ready) {
        return PLAT_OK;
    }

    status = platform_fmc_sdram_prepare_clock();
    if (status != PLAT_OK) {
        return status;
    }

    memset(&s_sdram_handle, 0, sizeof(s_sdram_handle));
    platform_fmc_sdram_fill_handle(&s_sdram_handle);
    platform_fmc_sdram_fill_timing(&timing);

    status = platform_fmc_sdram_compute_refresh_count(&s_sdram_handle, &refresh_count);
    if (status != PLAT_OK) {
        memset(&s_sdram_handle, 0, sizeof(s_sdram_handle));
        return status;
    }

    status = platform_status_from_hal(HAL_SDRAM_Init(&s_sdram_handle, &timing));
    if (status != PLAT_OK) {
        memset(&s_sdram_handle, 0, sizeof(s_sdram_handle));
        return status;
    }

    status = platform_fmc_sdram_run_init_sequence(refresh_count);
    if (status != PLAT_OK) {
        memset(&s_sdram_handle, 0, sizeof(s_sdram_handle));
        return status;
    }

    s_sdram_ready = true;
    status = platform_fmc_sdram_run_power_on_self_test();
    if (status != PLAT_OK) {
        s_sdram_ready = false;
        memset(&s_sdram_handle, 0, sizeof(s_sdram_handle));
        return status;
    }

    return PLAT_OK;
}

bool platform_fmc_is_ready(void) {
    return s_sdram_ready;
}

const Plat_FmcSdramLayout_t *platform_fmc_get_sdram_layout(void) {
    return &s_sdram_layout;
}

SDRAM_HandleTypeDef *platform_fmc_get_sdram_handle(void) {
    return &s_sdram_handle;
}

Plat_Status_t platform_fmc_send_sdram_command(FMC_SDRAM_CommandTypeDef *command, uint32_t timeout) {
    if (command == NULL) {
        return PLAT_ERR_INVALID_PARAM;
    }

    if (!s_sdram_ready) {
        return PLAT_ERR_HW_FAILURE;
    }

    return platform_status_from_hal(HAL_SDRAM_SendCommand(&s_sdram_handle, command, timeout));
}

Plat_Status_t platform_fmc_program_refresh_rate(uint32_t refresh_count) {
    if (!s_sdram_ready) {
        return PLAT_ERR_HW_FAILURE;
    }

    if (refresh_count == 0u) {
        return PLAT_ERR_INVALID_PARAM;
    }

    if (HAL_SDRAM_ProgramRefreshRate(&s_sdram_handle, refresh_count) != HAL_OK) {
        return PLAT_ERR_HW_FAILURE;
    }

    return PLAT_OK;
}

Plat_Status_t platform_fmc_sdram_clean_dcache(uintptr_t base_address, size_t size_bytes) {
    return platform_fmc_sdram_apply_cache_op(base_address, size_bytes,
                                             PLATFORM_FMC_SDRAM_CACHE_OP_CLEAN);
}

Plat_Status_t platform_fmc_sdram_invalidate_dcache(uintptr_t base_address, size_t size_bytes) {
    return platform_fmc_sdram_apply_cache_op(base_address, size_bytes,
                                             PLATFORM_FMC_SDRAM_CACHE_OP_INVALIDATE);
}

Plat_Status_t platform_fmc_sdram_clean_invalidate_dcache(uintptr_t base_address, size_t size_bytes) {
    return platform_fmc_sdram_apply_cache_op(base_address, size_bytes,
                                             PLATFORM_FMC_SDRAM_CACHE_OP_CLEAN_INVALIDATE);
}

Plat_Status_t platform_fmc_sdram_destructive_test(uintptr_t base_address, size_t size_bytes,
                                                  size_t *failed_offset_bytes) {
    static const uint8_t k_pattern_bytes[] = {0x55u, 0xA5u, 0x5Au, 0xAAu};
    __IO uint32_t *word_ptr;
    size_t word_count;
    size_t i;
    __IO uint8_t *byte_ptr;
    size_t byte_count;
    __IO uint8_t *tail_ptr;
    size_t tail_offset;
    size_t tail_count;
    Plat_Status_t status;

    if (failed_offset_bytes != NULL) {
        *failed_offset_bytes = 0u;
    }

    if (!s_sdram_ready) {
        return PLAT_ERR_HW_FAILURE;
    }

    if (!platform_fmc_sdram_range_is_valid(base_address, size_bytes)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    word_count = size_bytes / sizeof(uint32_t);
    if (word_count > 0u) {
        word_ptr = (uint32_t *) base_address;
        for (i = 0; i < word_count; ++i) {
            word_ptr[i] = (uint32_t) i;
        }

        status = platform_fmc_sdram_clean_invalidate_dcache(base_address,
                                                            word_count * sizeof(uint32_t));
        if (status != PLAT_OK) {
            return status;
        }

        for (i = 0; i < word_count; ++i) {
            if (word_ptr[i] != (uint32_t) i) {
                if (failed_offset_bytes != NULL) {
                    *failed_offset_bytes = i * sizeof(uint32_t);
                }
                return PLAT_ERR_HW_FAILURE;
            }
        }

        for (i = 0; i < word_count; ++i) {
            word_ptr[i] = ~word_ptr[i];
        }

        status = platform_fmc_sdram_clean_invalidate_dcache(base_address,
                                                            word_count * sizeof(uint32_t));
        if (status != PLAT_OK) {
            return status;
        }

        for (i = 0; i < word_count; ++i) {
            if (word_ptr[i] != ~((uint32_t) i)) {
                if (failed_offset_bytes != NULL) {
                    *failed_offset_bytes = i * sizeof(uint32_t);
                }
                return PLAT_ERR_HW_FAILURE;
            }
        }
    }

    byte_count = (size_bytes < sizeof(k_pattern_bytes)) ? size_bytes : sizeof(k_pattern_bytes);
    byte_ptr   = (uint8_t *) base_address;
    for (i = 0; i < byte_count; ++i) {
        byte_ptr[i] = k_pattern_bytes[i];
    }

    status = platform_fmc_sdram_clean_invalidate_dcache(base_address, byte_count);
    if (status != PLAT_OK) {
        return status;
    }

    for (i = 0; i < byte_count; ++i) {
        if (byte_ptr[i] != k_pattern_bytes[i]) {
            if (failed_offset_bytes != NULL) {
                *failed_offset_bytes = i;
            }
            return PLAT_ERR_HW_FAILURE;
        }
    }

    tail_offset = word_count * sizeof(uint32_t);
    tail_count = size_bytes - tail_offset;
    if ((tail_count > 0u) && (word_count > 0u)) {
        tail_ptr = (uint8_t *) (base_address + tail_offset);
        for (i = 0; i < tail_count; ++i) {
            tail_ptr[i] = k_pattern_bytes[i];
        }

        status = platform_fmc_sdram_clean_invalidate_dcache(base_address + tail_offset, tail_count);
        if (status != PLAT_OK) {
            return status;
        }

        for (i = 0; i < tail_count; ++i) {
            if (tail_ptr[i] != k_pattern_bytes[i]) {
                if (failed_offset_bytes != NULL) {
                    *failed_offset_bytes = tail_offset + i;
                }
                return PLAT_ERR_HW_FAILURE;
            }
        }
    }

    return PLAT_OK;
}
