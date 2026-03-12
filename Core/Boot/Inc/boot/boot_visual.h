#ifndef BOOT_VISUAL_H
#define BOOT_VISUAL_H

#include <stdint.h>

#include "boot/boot_types.h"

typedef struct
{
    boot_stage_t stage;
    boot_result_t result;
    uint8_t progress;
    uint32_t error_code;
} boot_visual_state_t;

void boot_visual_init(void);
void boot_visual_update(boot_stage_t stage, boot_result_t result, uint8_t progress, uint32_t error_code);
void boot_visual_get(boot_visual_state_t *state);
const char *boot_visual_stage_to_text(boot_stage_t stage);

#endif /* BOOT_VISUAL_H */
