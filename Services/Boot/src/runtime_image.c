#include "boot/runtime_image.h"

#include <stddef.h>
#include <stdint.h>

#include "platform/platform_cpu.h"
#include "platform/platform_flash.h"
#include "platform/platform_memory_map.h"

firmware_status_t RuntimeImage_Prepare(uint32_t *vector_address)
{
    const volatile uint32_t *vectors;
    uint32_t address;
    uint32_t stack_pointer;
    uint32_t reset_handler_raw;
    uint32_t reset_handler;
    firmware_status_t status;

    if (vector_address == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    status = PlatformFlash_Init();
    if (FirmwareStatus_IsError(status))
        return status;
    status = PlatformFlash_EnterMemoryMapped();
    if (FirmwareStatus_IsError(status))
        return status;

    address = PLATFORM_QSPI_MAPPED_BASE + PLATFORM_APP_OFFSET;
    if ((address & (PLATFORM_APP_VECTOR_ALIGNMENT - 1U)) != 0U)
        return FIRMWARE_STATUS_INVALID_STATE;

    vectors           = (const volatile uint32_t *) (uintptr_t) address;
    stack_pointer     = vectors[0];
    reset_handler_raw = vectors[1];
    reset_handler     = reset_handler_raw & ~1UL;

    if (!PlatformCpu_IsValidStackPointer(stack_pointer) || (stack_pointer & 7U) != 0U ||
        (reset_handler_raw & 1U) == 0U || reset_handler < address ||
        reset_handler >= address + PLATFORM_APP_MAX_SIZE)
        return FIRMWARE_STATUS_INVALID_STATE;

    *vector_address = address;
    return FIRMWARE_STATUS_OK;
}
