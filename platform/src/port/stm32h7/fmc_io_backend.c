#include "platform/fmc_io.h"

#include <string.h>

#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_sram.h"

enum {
    PLATFORM_FMC_IO_BASE_ADDRESS = 0x60001000u
};

static SRAM_HandleTypeDef s_fmc_io_handle;
static bool s_fmc_io_ready = false;
static uint32_t s_fmc_io_shadow = 0u;
static bool s_fmc_io_pins_ready = false;
static bool s_fmc_io_clock_ready = false;

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

static uint32_t platform_fmc_io_default_state(void) {
    return PLAT_FMC_IO_PIN_MASK(PLAT_FMC_IO_NRF24L01_CE) |
           PLAT_FMC_IO_PIN_MASK(PLAT_FMC_IO_NRF905_TRX_CE) |
           PLAT_FMC_IO_PIN_MASK(PLAT_FMC_IO_LED1) |
           PLAT_FMC_IO_PIN_MASK(PLAT_FMC_IO_LED2) |
           PLAT_FMC_IO_PIN_MASK(PLAT_FMC_IO_LED3) |
           PLAT_FMC_IO_PIN_MASK(PLAT_FMC_IO_LED4);
}

static bool platform_fmc_io_pin_is_valid(Plat_FmcIoPin_t pin) {
    return ((uint32_t) pin) < (uint32_t) PLAT_FMC_IO_COUNT;
}

static Plat_Status_t platform_fmc_io_prepare_clock(void) {
    RCC_PeriphCLKInitTypeDef periph_clk = {0};

    if (s_fmc_io_clock_ready) {
        return PLAT_OK;
    }

    periph_clk.PeriphClockSelection = RCC_PERIPHCLK_FMC;
    periph_clk.FmcClockSelection = RCC_FMCCLKSOURCE_D1HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&periph_clk) != HAL_OK) {
        return PLAT_ERR_HW_FAILURE;
    }

    s_fmc_io_clock_ready = true;
    return PLAT_OK;
}

static Plat_Status_t platform_fmc_io_commit(uint32_t value) {
    *(__IO uint32_t *) PLATFORM_FMC_IO_BASE_ADDRESS = value;
    s_fmc_io_shadow = value;
    return PLAT_OK;
}

void HAL_SRAM_MspInit(SRAM_HandleTypeDef *hsram) {
    GPIO_InitTypeDef gpio_init = {0};

    (void) hsram;

    if (s_fmc_io_pins_ready) {
        return;
    }

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_FMC_CLK_ENABLE();

    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF12_FMC;

    gpio_init.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7 |
                    GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio_init);

    gpio_init.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 |
                    GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio_init);

    gpio_init.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOG, &gpio_init);

    gpio_init.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
                    GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOH, &gpio_init);

    gpio_init.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_6 |
                    GPIO_PIN_7 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOI, &gpio_init);

    s_fmc_io_pins_ready = true;
}

void HAL_SRAM_MspDeInit(SRAM_HandleTypeDef *hsram) {
    (void) hsram;

    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7 |
                               GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_14 |
                               GPIO_PIN_15);
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                               GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |
                               GPIO_PIN_15);
    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_0 | GPIO_PIN_1);
    HAL_GPIO_DeInit(GPIOH, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 |
                               GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
    HAL_GPIO_DeInit(GPIOI, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_6 |
                               GPIO_PIN_7 | GPIO_PIN_9 | GPIO_PIN_10);

    s_fmc_io_pins_ready = false;
}

Plat_Status_t platform_fmc_io_init(void) {
    FMC_NORSRAM_TimingTypeDef timing = {0};
    Plat_Status_t status;

    if (s_fmc_io_ready) {
        return PLAT_OK;
    }

    status = platform_fmc_io_prepare_clock();
    if (status != PLAT_OK) {
        return status;
    }

    memset(&s_fmc_io_handle, 0, sizeof(s_fmc_io_handle));
    s_fmc_io_handle.Instance = FMC_NORSRAM_DEVICE;
    s_fmc_io_handle.Extended = FMC_NORSRAM_EXTENDED_DEVICE;
    s_fmc_io_handle.Init.NSBank = FMC_NORSRAM_BANK1;
    s_fmc_io_handle.Init.DataAddressMux = FMC_DATA_ADDRESS_MUX_DISABLE;
    s_fmc_io_handle.Init.MemoryType = FMC_MEMORY_TYPE_SRAM;
    s_fmc_io_handle.Init.MemoryDataWidth = FMC_NORSRAM_MEM_BUS_WIDTH_32;
    s_fmc_io_handle.Init.BurstAccessMode = FMC_BURST_ACCESS_MODE_DISABLE;
    s_fmc_io_handle.Init.WaitSignalPolarity = FMC_WAIT_SIGNAL_POLARITY_LOW;
    s_fmc_io_handle.Init.WaitSignalActive = FMC_WAIT_TIMING_BEFORE_WS;
    s_fmc_io_handle.Init.WriteOperation = FMC_WRITE_OPERATION_ENABLE;
    s_fmc_io_handle.Init.WaitSignal = FMC_WAIT_SIGNAL_DISABLE;
    s_fmc_io_handle.Init.ExtendedMode = FMC_EXTENDED_MODE_DISABLE;
    s_fmc_io_handle.Init.AsynchronousWait = FMC_ASYNCHRONOUS_WAIT_DISABLE;
    s_fmc_io_handle.Init.WriteBurst = FMC_WRITE_BURST_DISABLE;
    s_fmc_io_handle.Init.ContinuousClock = FMC_CONTINUOUS_CLOCK_SYNC_ONLY;
    s_fmc_io_handle.Init.WriteFifo = FMC_WRITE_FIFO_ENABLE;
    s_fmc_io_handle.Init.PageSize = FMC_PAGE_SIZE_NONE;

    timing.AddressSetupTime = 5;
    timing.AddressHoldTime = 2;
    timing.DataSetupTime = 2;
    timing.BusTurnAroundDuration = 1;
    timing.CLKDivision = 2;
    timing.DataLatency = 2;
    timing.AccessMode = FMC_ACCESS_MODE_A;

    status = platform_status_from_hal(HAL_SRAM_Init(&s_fmc_io_handle, &timing, &timing));
    if (status != PLAT_OK) {
        memset(&s_fmc_io_handle, 0, sizeof(s_fmc_io_handle));
        return status;
    }

    status = platform_fmc_io_commit(platform_fmc_io_default_state());
    if (status != PLAT_OK) {
        memset(&s_fmc_io_handle, 0, sizeof(s_fmc_io_handle));
        return status;
    }

    s_fmc_io_ready = true;
    return PLAT_OK;
}

bool platform_fmc_io_is_ready(void) {
    return s_fmc_io_ready;
}

Plat_Status_t platform_fmc_io_write_port(uint32_t value) {
    if (!s_fmc_io_ready) {
        return PLAT_ERR_HW_FAILURE;
    }

    return platform_fmc_io_commit(value);
}

uint32_t platform_fmc_io_get_shadow_state(void) {
    return s_fmc_io_shadow;
}

Plat_Status_t platform_fmc_io_write_pin(Plat_FmcIoPin_t pin, bool level) {
    uint32_t value;

    if (!s_fmc_io_ready || !platform_fmc_io_pin_is_valid(pin)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    value = s_fmc_io_shadow;
    if (level) {
        value |= PLAT_FMC_IO_PIN_MASK(pin);
    } else {
        value &= ~PLAT_FMC_IO_PIN_MASK(pin);
    }

    return platform_fmc_io_commit(value);
}

bool platform_fmc_io_read_pin(Plat_FmcIoPin_t pin) {
    if (!s_fmc_io_ready || !platform_fmc_io_pin_is_valid(pin)) {
        return false;
    }

    return (s_fmc_io_shadow & PLAT_FMC_IO_PIN_MASK(pin)) != 0u;
}

Plat_Status_t platform_fmc_io_toggle_pin(Plat_FmcIoPin_t pin) {
    if (!s_fmc_io_ready || !platform_fmc_io_pin_is_valid(pin)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    return platform_fmc_io_commit(s_fmc_io_shadow ^ PLAT_FMC_IO_PIN_MASK(pin));
}
