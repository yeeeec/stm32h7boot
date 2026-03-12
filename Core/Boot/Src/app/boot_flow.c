#include "boot/boot_flow.h"

#include "boot/boot_config.h"
#include "boot/boot_diag.h"
#include "boot/boot_porting.h"
#include "boot/boot_ports.h"
#include "boot/boot_visual.h"

static uint8_t boot_flow_progress_for_stage(boot_stage_t stage)
{
    switch (stage)
    {
        case BOOT_STAGE_INIT:
            return 5U;
        case BOOT_STAGE_WAIT_USB:
            return 10U;
        case BOOT_STAGE_LOAD_PACKAGE:
            return 30U;
        case BOOT_STAGE_VERIFY_PACKAGE:
            return 50U;
        case BOOT_STAGE_PROGRAM_EXT_FLASH:
            return 70U;
        case BOOT_STAGE_MAP_XIP:
            return 85U;
        case BOOT_STAGE_JUMP_APP:
            return 95U;
        case BOOT_STAGE_DONE:
            return 100U;
        case BOOT_STAGE_ERROR:
            return 100U;
        default:
            return 0U;
    }
}

static void boot_flow_enter_stage(boot_context_t *ctx, boot_stage_t stage)
{
    uint32_t now_ms = boot_port_get_time_ms();

    boot_context_enter_stage(ctx, stage, now_ms);
    boot_diag_push_event(stage, BOOT_DIAG_EVT_STAGE_ENTER, 0, now_ms);
    boot_visual_update(stage, BOOT_RESULT_IN_PROGRESS, boot_flow_progress_for_stage(stage), ctx->error_code);
}

static boot_result_t boot_flow_fail(boot_context_t *ctx, boot_result_t result, uint32_t gap_mask)
{
    uint32_t now_ms = boot_port_get_time_ms();

    if (gap_mask != 0U)
    {
        boot_diag_report_gap(gap_mask, ctx->stage, now_ms);
    }

    ctx->last_result = result;
    ctx->error_code = (((uint32_t)ctx->stage << 16U) | ((uint32_t)((-result) & 0xFFFF)));

    boot_diag_push_event(ctx->stage, BOOT_DIAG_EVT_ERROR, (int32_t)result, now_ms);
    boot_context_enter_stage(ctx, BOOT_STAGE_ERROR, now_ms);
    boot_visual_update(BOOT_STAGE_ERROR, result, boot_flow_progress_for_stage(BOOT_STAGE_ERROR), ctx->error_code);

    return result;
}

void boot_flow_init(boot_context_t *ctx)
{
    if (ctx == 0)
    {
        return;
    }

    ctx->last_result = BOOT_RESULT_IN_PROGRESS;
    ctx->retry_count = 0U;
    ctx->error_code = 0U;
    boot_flow_enter_stage(ctx, BOOT_STAGE_INIT);
}

boot_result_t boot_flow_step(boot_context_t *ctx)
{
    boot_result_t result = BOOT_RESULT_IN_PROGRESS;

    if (ctx == 0)
    {
        return BOOT_RESULT_INVALID_PARAM;
    }

    ctx->loop_counter++;

    switch (ctx->stage)
    {
        case BOOT_STAGE_INIT:
        {
            boot_porting_report_t report;
            boot_porting_check_run(&report);

            if (report.gap_mask != 0U)
            {
                boot_diag_report_gap(report.gap_mask, BOOT_STAGE_INIT, boot_port_get_time_ms());
            }

            boot_flow_enter_stage(ctx, BOOT_STAGE_WAIT_USB);
            return BOOT_RESULT_IN_PROGRESS;
        }

        case BOOT_STAGE_WAIT_USB:
            result = boot_port_usb_is_ready();
            if (result == BOOT_RESULT_OK)
            {
                boot_flow_enter_stage(ctx, BOOT_STAGE_LOAD_PACKAGE);
                return BOOT_RESULT_IN_PROGRESS;
            }
            if ((result == BOOT_RESULT_NOT_READY) || (result == BOOT_RESULT_IN_PROGRESS))
            {
                boot_visual_update(BOOT_STAGE_WAIT_USB,
                                   BOOT_RESULT_IN_PROGRESS,
                                   boot_flow_progress_for_stage(BOOT_STAGE_WAIT_USB),
                                   0U);
                return BOOT_RESULT_IN_PROGRESS;
            }
            if (result == BOOT_RESULT_NOT_IMPLEMENTED)
            {
                return boot_flow_fail(ctx, result, BOOT_GAP_PORT_USB_LOADER);
            }
            return boot_flow_fail(ctx, result, 0U);

        case BOOT_STAGE_LOAD_PACKAGE:
            result = boot_port_usb_load_package(BOOT_CFG_UPDATE_FILE_PATH,
                                                ctx->work_buffer,
                                                ctx->work_buffer_size,
                                                &ctx->image_metadata);
            if (result == BOOT_RESULT_OK)
            {
                boot_flow_enter_stage(ctx, BOOT_STAGE_VERIFY_PACKAGE);
                return BOOT_RESULT_IN_PROGRESS;
            }
            if ((result == BOOT_RESULT_NOT_READY) || (result == BOOT_RESULT_IN_PROGRESS))
            {
                return BOOT_RESULT_IN_PROGRESS;
            }
            if (result == BOOT_RESULT_NOT_IMPLEMENTED)
            {
                return boot_flow_fail(ctx, result, BOOT_GAP_PORT_USB_LOADER);
            }
            return boot_flow_fail(ctx, result, 0U);

        case BOOT_STAGE_VERIFY_PACKAGE:
            result = boot_port_verify_package(ctx->work_buffer,
                                              ctx->image_metadata.image_size,
                                              &ctx->image_metadata);
            if (result == BOOT_RESULT_OK)
            {
                boot_flow_enter_stage(ctx, BOOT_STAGE_PROGRAM_EXT_FLASH);
                return BOOT_RESULT_IN_PROGRESS;
            }
            if (result == BOOT_RESULT_NOT_IMPLEMENTED)
            {
                return boot_flow_fail(ctx, result, BOOT_GAP_PORT_IMAGE_VERIFY);
            }
            return boot_flow_fail(ctx, result, 0U);

        case BOOT_STAGE_PROGRAM_EXT_FLASH:
            result = boot_port_program_external_flash(ctx->ext_flash_target_addr,
                                                      ctx->work_buffer,
                                                      ctx->image_metadata.image_size);
            if (result == BOOT_RESULT_OK)
            {
                boot_flow_enter_stage(ctx, BOOT_STAGE_MAP_XIP);
                return BOOT_RESULT_IN_PROGRESS;
            }
            if (result == BOOT_RESULT_NOT_IMPLEMENTED)
            {
                return boot_flow_fail(ctx, result, BOOT_GAP_PORT_FLASH_PROGRAM);
            }
            return boot_flow_fail(ctx, result, 0U);

        case BOOT_STAGE_MAP_XIP:
            result = boot_port_enter_xip_mode();
            if (result == BOOT_RESULT_OK)
            {
                boot_flow_enter_stage(ctx, BOOT_STAGE_JUMP_APP);
                return BOOT_RESULT_IN_PROGRESS;
            }
            if (result == BOOT_RESULT_NOT_IMPLEMENTED)
            {
                return boot_flow_fail(ctx, result, BOOT_GAP_PORT_XIP_MAP);
            }
            return boot_flow_fail(ctx, result, 0U);

        case BOOT_STAGE_JUMP_APP:
            result = boot_port_jump_to_app(BOOT_CFG_APP_VECTOR_ADDR);
            if (result == BOOT_RESULT_OK)
            {
                boot_flow_enter_stage(ctx, BOOT_STAGE_DONE);
                ctx->last_result = BOOT_RESULT_OK;
                boot_visual_update(BOOT_STAGE_DONE, BOOT_RESULT_OK, boot_flow_progress_for_stage(BOOT_STAGE_DONE), 0U);
                return BOOT_RESULT_OK;
            }
            if (result == BOOT_RESULT_NOT_IMPLEMENTED)
            {
                return boot_flow_fail(ctx, result, BOOT_GAP_PORT_APP_JUMP);
            }
            return boot_flow_fail(ctx, result, 0U);

        case BOOT_STAGE_DONE:
            return BOOT_RESULT_OK;

        case BOOT_STAGE_ERROR:
            return ctx->last_result;

        default:
            return boot_flow_fail(ctx, BOOT_RESULT_FAIL, 0U);
    }
}
