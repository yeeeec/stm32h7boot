#ifndef PLATFORM_FMC_H
#define PLATFORM_FMC_H

#include "definitions.h"
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uintptr_t base_address;
    size_t size_bytes;
    uintptr_t lcd_layer0_address;
    uintptr_t lcd_layer1_address;
    size_t lcd_layer_size_bytes;
    uintptr_t app_region_address;
    size_t app_region_size_bytes;
} Plat_FmcSdramLayout_t;

Plat_Status_t platform_fmc_sdram_init(void);
bool platform_fmc_is_ready(void);
const Plat_FmcSdramLayout_t *platform_fmc_get_sdram_layout(void);
SDRAM_HandleTypeDef *platform_fmc_get_sdram_handle(void);
Plat_Status_t platform_fmc_send_sdram_command(FMC_SDRAM_CommandTypeDef *command, uint32_t timeout);
Plat_Status_t platform_fmc_program_refresh_rate(uint32_t refresh_count);
Plat_Status_t platform_fmc_sdram_clean_dcache(uintptr_t base_address, size_t size_bytes);
Plat_Status_t platform_fmc_sdram_invalidate_dcache(uintptr_t base_address, size_t size_bytes);
Plat_Status_t platform_fmc_sdram_clean_invalidate_dcache(uintptr_t base_address, size_t size_bytes);
Plat_Status_t platform_fmc_sdram_destructive_test(uintptr_t base_address, size_t size_bytes,
                                                  size_t *failed_offset_bytes);

#ifdef __cplusplus
}
#endif

#endif
