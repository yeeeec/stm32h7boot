/**
 * @file adc_backend.c
 * @brief GD32E23x ADC backend implementation (HAL + DMA + timer trigger).
 */
#include "gd32e23x_hal.h"

#include <stddef.h>
#include <string.h>

#include "platform/adc.h"

/** ADC HAL handle. */
static hal_adc_dev_struct g_adc_dev;
/** ADC DMA HAL handle. */
static hal_dma_dev_struct g_dma_adc_dev;
/** Timer trigger HAL handle. */
static hal_timer_dev_struct g_timer_trigger_dev;

/** Total DMA length used to compute write offset. */
static uint32_t g_adc_dma_total_len = 0;
static uint8_t g_adc_dma_error_code = 0;

static adc_dma_callback_t g_adc_dma_callback = NULL;

static void adc_wrap_error(void *adc_dev);
static void adc_wrap_dma_full(void *adc_dev);
static void adc_wrap_dma_half(void *adc_dev);

static void adc_init_gpio(void);
static void adc_config_dma(void);
static void adc_config_timer_trigger(uint32_t freq_hz);

/**
 * @brief Configure GPIO pins for ADC (PA0, PA1, PA5).
 */
static void adc_init_gpio(void) {
    hal_gpio_init_struct gpio_init_struct;

    hal_rcu_periph_clk_enable(RCU_GPIOA);

    hal_gpio_struct_init(&gpio_init_struct);
    gpio_init_struct.mode   = HAL_GPIO_MODE_ANALOG;
    gpio_init_struct.pull   = HAL_GPIO_PULL_NONE;
    gpio_init_struct.ospeed = HAL_GPIO_OSPEED_50MHZ;

    hal_gpio_init(GPIOA, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_5, &gpio_init_struct);
}

/**
 * @brief Configure DMA for circular transfer from ADC to memory.
 */
static void adc_config_dma(void) {
    hal_dma_init_struct dma_init_struct;

    hal_rcu_periph_clk_enable(RCU_DMA);

    hal_dma_struct_init(HAL_DMA_INIT_STRUCT, &dma_init_struct);
    hal_dma_struct_init(HAL_DMA_DEV_STRUCT, &g_dma_adc_dev);

    dma_init_struct.direction    = DMA_DIR_PERIPH_TO_MEMORY;
    dma_init_struct.periph_inc   = DISABLE;
    dma_init_struct.memory_inc   = ENABLE;
    dma_init_struct.periph_width = DMA_PERIPH_SIZE_16BITS;
    dma_init_struct.memory_width = DMA_MEMORY_SIZE_16BITS;
    dma_init_struct.mode         = DMA_MODE_CIRCULAR;
    dma_init_struct.priority     = DMA_PRIORITY_LEVEL_HIGH;

    hal_dma_init(&g_dma_adc_dev, DMA_CH0, &dma_init_struct);

    dma_interrupt_enable(g_dma_adc_dev.channel, DMA_INT_HTF);
    dma_interrupt_enable(g_dma_adc_dev.channel, DMA_INT_FTF);

    dma_interrupt_flag_clear(g_dma_adc_dev.channel, DMA_INT_FLAG_FTF);
    dma_interrupt_flag_clear(g_dma_adc_dev.channel, DMA_INT_FLAG_HTF);

    hal_periph_dma_info_bind(g_adc_dev, p_dma_adc, g_dma_adc_dev);

    nvic_irq_enable(DMA_Channel0_IRQn, 0);
}

/**
 * @brief Configure TIMER2 as ADC trigger source.
 *
 * @param freq_hz Target sampling frequency (Hz).
 */
static void adc_config_timer_trigger(uint32_t freq_hz) {
    hal_timer_basic_struct timer_basic_para;
    hal_timer_clocksource_struct timer_clk_para;

    hal_rcu_periph_clk_enable(RCU_TIMER2);

    hal_timer_struct_init(HAL_TIMER_DEV_STRUCT, &g_timer_trigger_dev);
    hal_timer_struct_init(HAL_TIMER_BASIC_STRUCT, &timer_basic_para);

    uint16_t prescaler                 = (SystemCoreClock / 1000000U) - 1;
    timer_basic_para.prescaler         = prescaler;
    timer_basic_para.alignedmode       = TIMER_COUNTER_EDGE;
    timer_basic_para.counterdirection  = TIMER_COUNTER_UP;
    timer_basic_para.period            = (1000000U / freq_hz) - 1;
    timer_basic_para.clockdivision     = TIMER_CKDIV_DIV1;
    timer_basic_para.autoreload_shadow = AUTO_RELOAD_SHADOW_ENABLE;

    timer_basic_para.trgo_selection    = TIMRE_TRGO_SRC_UPDATE;
    timer_basic_para.master_slave_mode = TIMER_MASTER_SLAVE_MODE_ENABLE;

    hal_timer_basic_init(&g_timer_trigger_dev, TIMER2, &timer_basic_para);

    hal_timer_struct_init(HAL_TIMER_CLOCKSOURCE_STRUCT, &timer_clk_para);
    timer_clk_para.clock_source = TIMER_CLOCK_SOURCE_CK_TIMER;
    hal_timer_clock_source_config(&g_timer_trigger_dev, &timer_clk_para);
}

/**
 * @brief Initialize ADC peripheral and regular channel sequence.
 *
 * @return Platform status code.
 */
Plat_Status_t platform_adc_init(void) {
    hal_adc_init_struct adc_init_struct;
    hal_adc_regularch_init_struct adc_reg_init;
    hal_adc_regularch_config_struct adc_reg_cfg;

    adc_init_gpio();

    hal_rcu_periph_clk_enable(RCU_ADC);

    rcu_adc_clock_config(RCU_ADCCK_APB2_DIV6);

    hal_adc_struct_init(HAL_ADC_DEV_STRUCT, &g_adc_dev);
    hal_adc_struct_init(HAL_ADC_INIT_STRUCT, &adc_init_struct);
    hal_adc_struct_init(HAL_ADC_REGULARCH_INIT_STRUCT, &adc_reg_init);
    hal_adc_struct_init(HAL_ADC_REGULARCH_CONFIG_STRUCT, &adc_reg_cfg);

    adc_init_struct.resolution_select                 = ADC_RESOLUTION_12B;
    adc_init_struct.data_alignment                    = ADC_DATAALIGN_RIGHT;
    adc_init_struct.scan_mode                         = ENABLE;
    adc_init_struct.oversample_config.oversample_mode = DISABLE;
    hal_adc_init(&g_adc_dev, &adc_init_struct);

    adc_reg_init.length             = 3;
    adc_reg_init.exttrigger_select  = ADC_EXTTRIG_REGULAR_T2_TRGO;
    adc_reg_init.continuous_mode    = DISABLE;
    adc_reg_init.discontinuous_mode = DISABLE;
    hal_adc_regular_channel_init(&g_adc_dev, &adc_reg_init);

    adc_reg_cfg.sample_time = ADC_SAMPLETIME_55POINT5;

    adc_reg_cfg.regular_channel  = ADC_CHANNEL_0;
    adc_reg_cfg.regular_sequence = ADC_REGULAR_SEQUENCE_0;
    hal_adc_regular_channel_config(&g_adc_dev, &adc_reg_cfg);

    adc_reg_cfg.regular_channel  = ADC_CHANNEL_1;
    adc_reg_cfg.regular_sequence = ADC_REGULAR_SEQUENCE_1;
    hal_adc_regular_channel_config(&g_adc_dev, &adc_reg_cfg);

    adc_reg_cfg.regular_channel  = ADC_CHANNEL_5;
    adc_reg_cfg.regular_sequence = ADC_REGULAR_SEQUENCE_2;
    hal_adc_regular_channel_config(&g_adc_dev, &adc_reg_cfg);

    hal_adc_calibration(&g_adc_dev);

    return PLAT_OK;
}

/**
 * @brief Start ADC sampling with DMA in circular mode.
 *
 * @param buffer Destination buffer.
 * @param length Number of samples (16-bit units).
 * @param sample_freq_hz Sampling frequency (Hz).
 * @return Platform status code.
 */
Plat_Status_t platform_adc_start(uint16_t *buffer, uint32_t length, uint32_t sample_freq_hz) {
    if ((buffer == NULL) || (length == 0u) || (sample_freq_hz == 0u) || (sample_freq_hz > 1000000u)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    if (((length % 2u) != 0u) || ((length % ADC_ADC_CHANNELS_COUNT) != 0u)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    g_adc_dma_total_len = length;
    g_adc_dma_error_code = 0u;

    adc_config_timer_trigger(sample_freq_hz);

    adc_config_dma();

    g_dma_adc_dev.dma_irq.half_finish_handle = adc_wrap_dma_half;
    hal_adc_dma_handle_cb_struct adc_dma_cb;
    memset(&adc_dma_cb, 0, sizeof(adc_dma_cb));
    adc_dma_cb.transcom_handle = adc_wrap_dma_full;
    adc_dma_cb.error_handle    = adc_wrap_error;
    int32_t ret = hal_adc_start_dma(&g_adc_dev, (uint32_t *) buffer, length, &adc_dma_cb);
    if (ret != HAL_ERR_NONE) {
        return PLAT_ERR_HW_FAILURE;
    }

    hal_timer_start_counter(&g_timer_trigger_dev);

    return PLAT_OK;
}

/**
 * @brief Stop ADC sampling and related DMA/timer.
 */
void platform_adc_stop(void) {
    hal_timer_stop_counter(&g_timer_trigger_dev);
    hal_adc_stop(&g_adc_dev);
    hal_dma_stop(&g_dma_adc_dev);
    g_adc_dma_total_len = 0u;
}

/**
 * @brief DMA Channel 0 interrupt handler for ADC DMA.
 */
void DMA_Channel0_IRQHandler(void) {
    hal_dma_irq(&g_dma_adc_dev);
}

void platform_adc_register_dma_rb(adc_dma_callback_t rb) {
    if (rb != NULL) {
        g_adc_dma_callback = rb;
    }
}

/**
 * @brief Get current DMA write position (in samples/words as configured).
 *
 * @return Current write position, or 0 if total length is unknown.
 */
uint32_t adc_get_write_pos(void) {
    if (g_adc_dma_total_len == 0) {
        return 0;
    }
    uint32_t counter = dma_transfer_number_get(DMA_CH0);
    return g_adc_dma_total_len - counter;
}

/**
 * @brief HAL ADC error callback wrapper.
 *
 * @param adc_dev ADC device pointer (unused).
 */
static void adc_wrap_error(void *adc_dev) {
    (void) adc_dev;
    g_adc_dma_error_code = 1u;
}

/**
 * @brief HAL ADC DMA full conversion callback wrapper.
 *
 * @param adc_dev ADC device pointer (unused).
 */
static void adc_wrap_dma_full(void *adc_dev) {
    (void) adc_dev;
    if (g_adc_dma_callback != NULL) {
        g_adc_dma_callback(g_adc_dma_total_len / 2u);
    }
}

/**
 * @brief HAL ADC DMA half conversion callback wrapper.
 *
 * @param adc_dev ADC device pointer (unused).
 */
static void adc_wrap_dma_half(void *adc_dev) {
    (void) adc_dev;
    if (g_adc_dma_callback != NULL) {
        g_adc_dma_callback(0u);
    }
}

uint8_t platform_adc_get_error(void) {
    uint8_t state;
    uint32_t irq_state = __get_PRIMASK();

    __disable_irq();
    state = g_adc_dma_error_code;
    g_adc_dma_error_code = 0u;
    if (irq_state == 0u) {
        __enable_irq();
    }

    return state;
}
