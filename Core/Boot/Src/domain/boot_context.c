#include "boot/boot_context.h"

#include <string.h>

#include "boot/boot_config.h"

void boot_context_init(boot_context_t *ctx,
                       uint8_t *work_buffer,
                       uint32_t work_buffer_size,
                       uint32_t now_ms)
{
    if (ctx == 0)
    {
        return;
    }

    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->stage = BOOT_STAGE_INIT;
    ctx->last_result = BOOT_RESULT_IN_PROGRESS;
    ctx->stage_enter_ms = now_ms;
    ctx->ext_flash_target_addr = BOOT_CFG_EXT_FLASH_SLOT0_ADDR;
    ctx->work_buffer = work_buffer;
    ctx->work_buffer_size = work_buffer_size;
}

void boot_context_enter_stage(boot_context_t *ctx, boot_stage_t stage, uint32_t now_ms)
{
    if (ctx == 0)
    {
        return;
    }

    ctx->stage = stage;
    ctx->stage_enter_ms = now_ms;
}
