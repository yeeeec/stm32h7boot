#include "boot/boot_visual.h"

#include <string.h>

static boot_visual_state_t s_state;

void boot_visual_init(void)
{
    (void)memset(&s_state, 0, sizeof(s_state));
    s_state.stage = BOOT_STAGE_IDLE;
    s_state.result = BOOT_RESULT_IN_PROGRESS;
}

void boot_visual_update(boot_stage_t stage, boot_result_t result, uint8_t progress, uint32_t error_code)
{
    s_state.stage = stage;
    s_state.result = result;
    s_state.progress = progress;
    s_state.error_code = error_code;
}

void boot_visual_get(boot_visual_state_t *state)
{
    if (state == 0)
    {
        return;
    }

    *state = s_state;
}

const char *boot_visual_stage_to_text(boot_stage_t stage)
{
    switch (stage)
    {
        case BOOT_STAGE_IDLE:
            return "IDLE";
        case BOOT_STAGE_INIT:
            return "INIT";
        case BOOT_STAGE_WAIT_USB:
            return "WAIT_USB";
        case BOOT_STAGE_LOAD_PACKAGE:
            return "LOAD_PACKAGE";
        case BOOT_STAGE_VERIFY_PACKAGE:
            return "VERIFY_PACKAGE";
        case BOOT_STAGE_PROGRAM_EXT_FLASH:
            return "PROGRAM_EXT_FLASH";
        case BOOT_STAGE_MAP_XIP:
            return "MAP_XIP";
        case BOOT_STAGE_JUMP_APP:
            return "JUMP_APP";
        case BOOT_STAGE_DONE:
            return "DONE";
        case BOOT_STAGE_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}
