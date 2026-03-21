#include "boot_simple_jump.h"

#include "boot_config.h"
#include "boot_handoff.h"
#include "boot_log.h"
#include "boot_platform.h"
#include "boot_simple_flash.h"

typedef void (*BootSimpleEntryPoint)(void);

static bool Boot_SimpleJump_IsStackInRange(uint32_t value, uint32_t base, uint32_t size) {
    uint32_t end_address = base + size;
    return ((value & 0x7U) == 0U) && (value >= base) && (value <= end_address);
}

static bool Boot_SimpleJump_IsAppCodeAddress(uint32_t value) {
    const BootSimpleFlashRegion *region = Boot_SimpleFlash_GetAppRegion();
    uint32_t address                    = value & ~1UL;

    return (address >= region->base) && (address < (region->base + region->size));
}

bool Boot_SimpleJump_IsAppValid(void) {
    uint32_t stack_pointer = *(const uint32_t *) BOOT_APP_BASE;
    uint32_t reset_handler = *(const uint32_t *) (BOOT_APP_BASE + sizeof(uint32_t));

    if ((stack_pointer == 0xFFFFFFFFUL) || (reset_handler == 0xFFFFFFFFUL)) {
        return false;
    }

    if (Boot_SimpleJump_IsStackInRange(stack_pointer, BOOT_DTCM_BASE, BOOT_DTCM_SIZE) != false) {
        return Boot_SimpleJump_IsAppCodeAddress(reset_handler);
    }

    if (Boot_SimpleJump_IsStackInRange(stack_pointer, BOOT_AXI_SRAM_BASE, BOOT_AXI_SRAM_SIZE) !=
        false) {
        return Boot_SimpleJump_IsAppCodeAddress(reset_handler);
    }

    if (Boot_SimpleJump_IsStackInRange(stack_pointer, BOOT_SRAM_D2_BASE, BOOT_SRAM_D2_SIZE) !=
        false) {
        return Boot_SimpleJump_IsAppCodeAddress(reset_handler);
    }

    if (Boot_SimpleJump_IsStackInRange(stack_pointer, BOOT_SRAM_D3_BASE, BOOT_SRAM_D3_SIZE) !=
        false) {
        return Boot_SimpleJump_IsAppCodeAddress(reset_handler);
    }

    return false;
}

BootError Boot_SimpleJump_ToApp(void) {
    uint32_t stack_pointer;
    uint32_t reset_handler;
    bool app_valid;

    stack_pointer = *(const uint32_t *) BOOT_APP_BASE;
    reset_handler = *(const uint32_t *) (BOOT_APP_BASE + sizeof(uint32_t));
    app_valid     = Boot_SimpleJump_IsAppValid();

    Boot_Handoff_RecordVector(BOOT_APP_BASE, stack_pointer, reset_handler, app_valid);
    LOG_INFO(BOOT_LOG_TAG, "App vector sp=0x%08lX reset=0x%08lX", (unsigned long) stack_pointer,
             (unsigned long) reset_handler);

    if (app_valid == false) {
        LOG_ERROR(BOOT_LOG_TAG, "App vector invalid for base=0x%08lX",
                  (unsigned long) BOOT_APP_BASE);
        return BOOT_ERR_IMAGE_VECTOR;
    }

    Boot_Handoff_SetStage(BOOT_HANDOFF_STAGE_JUMP_READY);
    Boot_Handoff_LogCurrent("Jump handoff");
    LOG_INFO(BOOT_LOG_TAG, "Jumping to app");
    Boot_Handoff_SetStage(BOOT_HANDOFF_STAGE_JUMPING);

    Boot_Platform_PrepareForJump();

    SCB->VTOR = BOOT_APP_BASE;
    __set_MSP(stack_pointer);
    __set_PSP(0U);
    __set_CONTROL(0U);
    __DSB();
    __ISB();

    ((BootSimpleEntryPoint) reset_handler)();
    return BOOT_ERR_JUMP_FAILED;
}
