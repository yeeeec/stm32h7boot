#ifndef BOOT_CONTEXT_H
#define BOOT_CONTEXT_H

#include <stdint.h>

#include "boot/boot_ports.h"
#include "boot/boot_types.h"

typedef struct
{
    boot_stage_t stage;
    boot_result_t last_result;
    uint32_t stage_enter_ms;
    uint32_t retry_count;
    uint32_t loop_counter;
    uint32_t ext_flash_target_addr;
    uint8_t *work_buffer;
    uint32_t work_buffer_size;
    boot_image_metadata_t image_metadata;
    uint32_t error_code;
} boot_context_t;

void boot_context_init(boot_context_t *ctx,
                       uint8_t *work_buffer,
                       uint32_t work_buffer_size,
                       uint32_t now_ms);
void boot_context_enter_stage(boot_context_t *ctx, boot_stage_t stage, uint32_t now_ms);

#endif /* BOOT_CONTEXT_H */
