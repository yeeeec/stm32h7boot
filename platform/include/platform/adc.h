/** platform/include/platform/adc.h */
#ifndef PLATFORM_ADC_H
#define PLATFORM_ADC_H

#include <stdint.h>

#include "definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ---------------- Configuration ---------------- */
/** Total hardware channels scanned per trigger */
/** adc channels count. */
#define ADC_ADC_CHANNELS_COUNT 3U
/** @deprecated Use ADC_ADC_CHANNELS_COUNT. */
#define PLAT_ADC_CHANNELS_COUNT ADC_ADC_CHANNELS_COUNT

/** ---------------- Callback Types ---------------- */

typedef void (*adc_dma_callback_t)(uint16_t pos);

/**
 * @brief Initialize ADC peripheral backend.
 * @return Platform status.
 */
Plat_Status_t platform_adc_init(void);

/**
 * @brief Start ADC DMA sampling stream.
 * @param buffer Destination DMA buffer (16-bit words).
 * @param length DMA transfer length in 16-bit words.
 * @param sample_freq_hz Sampling frequency in Hz.
 * @return Platform status.
 */
Plat_Status_t platform_adc_start(uint16_t *buffer, uint32_t length, uint32_t sample_freq_hz);

/**
 * @brief Stop ADC sampling stream.
 */
void platform_adc_stop(void);

/**
 * @brief Get current DMA write position.
 * @return Write position in 16-bit words.
 */
uint32_t adc_get_write_pos(void);

/**
 * @brief Register DMA half/full callback from backend.
 * @param rb Callback function.
 */
void platform_adc_register_dma_rb(adc_dma_callback_t rb);

/**
 * @brief Get and clear ADC DMA/backend error flag.
 * @return 1 when error pending, otherwise 0.
 */
uint8_t platform_adc_get_error(void);

#ifdef __cplusplus
}
#endif

#endif /**< PLATFORM_ADC_H */
