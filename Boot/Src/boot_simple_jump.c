#include "boot_simple_jump.h"

#include "boot_config.h"
#include "boot_info.h"
#include "boot_handoff.h"
#include "boot_log.h"
#include "boot_platform.h"
#include "boot_simple_flash.h"

typedef void (*BootSimpleEntryPoint)(void);

static bool Boot_SimpleJump_IsStackInRange(uint32_t value, uint32_t base, uint32_t size) {
    uint32_t end_address = base + size;
    return ((value & 0x7U) == 0U) && (value >= base) && (value <= end_address);
}

static bool Boot_SimpleJump_IsCodeAddressInRegion(uint32_t value, const BootSimpleFlashRegion *region) {
    uint32_t address;

    if (region == NULL) {
        return false;
    }

    address = value & ~1UL;
    return (address >= region->base) && (address < (region->base + region->size));
}

bool Boot_SimpleJump_IsSlotValid(uint8_t slot) {
    const BootSimpleFlashRegion *region = Boot_SimpleFlash_GetSlotRegion(slot);
    uint32_t stack_pointer;
    uint32_t reset_handler;

    if (region == NULL) {
        return false;
    }

    stack_pointer = *(const uint32_t *) region->base;
    reset_handler = *(const uint32_t *) (region->base + sizeof(uint32_t));

    if ((stack_pointer == 0xFFFFFFFFUL) || (reset_handler == 0xFFFFFFFFUL)) {
        return false;
    }

    if (Boot_SimpleJump_IsStackInRange(stack_pointer, BOOT_DTCM_BASE, BOOT_DTCM_SIZE) != false) {
        return Boot_SimpleJump_IsCodeAddressInRegion(reset_handler, region);
    }

    if (Boot_SimpleJump_IsStackInRange(stack_pointer, BOOT_AXI_SRAM_BASE, BOOT_AXI_SRAM_SIZE) !=
        false) {
        return Boot_SimpleJump_IsCodeAddressInRegion(reset_handler, region);
    }

    if (Boot_SimpleJump_IsStackInRange(stack_pointer, BOOT_SRAM_D2_BASE, BOOT_SRAM_D2_SIZE) !=
        false) {
        return Boot_SimpleJump_IsCodeAddressInRegion(reset_handler, region);
    }

    if (Boot_SimpleJump_IsStackInRange(stack_pointer, BOOT_SRAM_D3_BASE, BOOT_SRAM_D3_SIZE) !=
        false) {
        return Boot_SimpleJump_IsCodeAddressInRegion(reset_handler, region);
    }

    return false;
}

BootError Boot_SimpleJump_ToSlot(uint8_t slot) {
    const BootSimpleFlashRegion *region = Boot_SimpleFlash_GetSlotRegion(slot);
    uint32_t stack_pointer;
    uint32_t reset_handler;
    bool app_valid;

    if (region == NULL) {
        return BOOT_ERR_IMAGE_SLOT;
    }

    stack_pointer = *(const uint32_t *) region->base;
    reset_handler = *(const uint32_t *) (region->base + sizeof(uint32_t));
    app_valid     = Boot_SimpleJump_IsSlotValid(slot);

    Boot_Handoff_RecordVector(region->base, stack_pointer, reset_handler, app_valid);
    LOG_INFO(BOOT_LOG_TAG, "App vector sp=0x%08lX reset=0x%08lX", (unsigned long) stack_pointer,
             (unsigned long) reset_handler);

    if (app_valid == false) {
        LOG_ERROR(BOOT_LOG_TAG, "App vector invalid for slot=%s base=0x%08lX",
                  Boot_Info_SlotToString(slot), (unsigned long) region->base);
        return BOOT_ERR_IMAGE_VECTOR;
    }

    Boot_Handoff_SetStage(BOOT_HANDOFF_STAGE_JUMP_READY);
    Boot_Handoff_LogCurrent("Jump handoff");
    LOG_INFO(BOOT_LOG_TAG, "Jumping to app slot=%s", Boot_Info_SlotToString(slot));
    Boot_Handoff_SetStage(BOOT_HANDOFF_STAGE_JUMPING);

    Boot_Platform_PrepareForJump();

    SCB->VTOR = region->base;
    __set_MSP(stack_pointer);
    __set_PSP(0U);
    __set_CONTROL(0U);
    __DSB();
    __ISB();

    ((BootSimpleEntryPoint) reset_handler)();
    return BOOT_ERR_JUMP_FAILED;
}

bool Boot_SimpleJump_IsAppValid(void) {
    return Boot_SimpleJump_IsSlotValid(SLOT_A);
}

BootError Boot_SimpleJump_ToApp(void) {
    return Boot_SimpleJump_ToSlot(SLOT_A);
}
