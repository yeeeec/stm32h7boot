#ifndef PLATFORM_LTDC_H
#define PLATFORM_LTDC_H

#include "definitions.h"
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

bool platform_ltdc_is_ready(void);
LTDC_HandleTypeDef *platform_ltdc_get_handle(void);
Plat_Status_t platform_ltdc_set_framebuffer(uint32_t layer_idx, uint32_t address, uint32_t reload_type);
Plat_Status_t platform_ltdc_set_pitch(uint32_t layer_idx, uint32_t pitch_pixels,
                                      uint32_t reload_type);
Plat_Status_t platform_ltdc_reload(uint32_t reload_type);
Plat_Status_t platform_ltdc_set_layer_enabled(uint32_t layer_idx, bool enabled);

#ifdef __cplusplus
}
#endif

#endif
