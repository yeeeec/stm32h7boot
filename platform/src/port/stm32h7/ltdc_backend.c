#include "platform/ltdc.h"

#include "main.h"

extern LTDC_HandleTypeDef hltdc;

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

static bool platform_ltdc_layer_is_valid(uint32_t layer_idx) {
    return layer_idx <= LTDC_LAYER_2;
}

bool platform_ltdc_is_ready(void) {
    return hltdc.Instance != NULL;
}

LTDC_HandleTypeDef *platform_ltdc_get_handle(void) {
    return &hltdc;
}

Plat_Status_t platform_ltdc_set_framebuffer(uint32_t layer_idx, uint32_t address,
                                            uint32_t reload_type) {
    HAL_StatusTypeDef status;

    if (!platform_ltdc_layer_is_valid(layer_idx)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    status = HAL_LTDC_SetAddress_NoReload(&hltdc, address, layer_idx);
    if (status != HAL_OK) {
        return platform_status_from_hal(status);
    }

    return platform_ltdc_reload(reload_type);
}

Plat_Status_t platform_ltdc_set_pitch(uint32_t layer_idx, uint32_t pitch_pixels,
                                      uint32_t reload_type) {
    HAL_StatusTypeDef status;

    if (!platform_ltdc_layer_is_valid(layer_idx)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    status = HAL_LTDC_SetPitch_NoReload(&hltdc, pitch_pixels, layer_idx);
    if (status != HAL_OK) {
        return platform_status_from_hal(status);
    }

    return platform_ltdc_reload(reload_type);
}

Plat_Status_t platform_ltdc_reload(uint32_t reload_type) {
    return platform_status_from_hal(HAL_LTDC_Reload(&hltdc, reload_type));
}

Plat_Status_t platform_ltdc_set_layer_enabled(uint32_t layer_idx, bool enabled) {
    if (!platform_ltdc_layer_is_valid(layer_idx)) {
        return PLAT_ERR_INVALID_PARAM;
    }

    if (enabled) {
        __HAL_LTDC_LAYER_ENABLE(&hltdc, layer_idx);
    } else {
        __HAL_LTDC_LAYER_DISABLE(&hltdc, layer_idx);
    }

    return platform_ltdc_reload(LTDC_RELOAD_IMMEDIATE);
}
