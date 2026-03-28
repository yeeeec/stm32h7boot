#include "boot_simple_jump.h"

#include <string.h>

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_extflash.h"
#include "boot_handoff.h"
#include "boot_info.h"
#include "boot_log.h"
#include "boot_simple_flash.h"
#include "platform/boot_platform.h"

typedef void (*BootSimpleEntryPoint)(void);

typedef struct __attribute__((packed)) {
    char magic[BOOT_VERSION_MAGIC_LENGTH];
    char project_name[BOOT_VERSION_PROJECT_NAME_LENGTH];
    char image_tag[BOOT_VERSION_IMAGE_TAG_LENGTH];
    char git_hash[BOOT_VERSION_GIT_HASH_LENGTH];
    char build_time[BOOT_BUILD_TIME_LENGTH];
    uint32_t raw_bin_crc32;
    uint32_t write_address;
    uint32_t valid_bin_size;
    uint8_t reserved[BOOT_VERSION_RESERVED_SIZE];
} BootVersionInfoHeader;

_Static_assert(sizeof(BootVersionInfoHeader) == BOOT_VERSION_INFO_SIZE,
               "Boot version header size must be 96 bytes");

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

static bool Boot_SimpleJump_IsVectorValid(uint32_t stack_pointer,
                                          uint32_t reset_handler,
                                          const BootSimpleFlashRegion *region) {
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

static BootError Boot_SimpleJump_ReadVector(const BootSimpleFlashRegion *region,
                                            uint32_t *stack_pointer,
                                            uint32_t *reset_handler) {
    BootError error;

    if ((region == NULL) || (stack_pointer == NULL) || (reset_handler == NULL)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    error = Boot_SimpleFlash_Read(region->base, stack_pointer, sizeof(*stack_pointer));
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    return Boot_SimpleFlash_Read(region->base + sizeof(*stack_pointer), reset_handler,
                                 sizeof(*reset_handler));
}

static bool Boot_SimpleJump_IsEmbeddedHeaderPlausible(const BootVersionInfoHeader *header,
                                                      const BootSimpleFlashRegion *region) {
    if ((header == NULL) || (region == NULL)) {
        return false;
    }

    if (memcmp(header->magic, BOOT_VERSION_MAGIC, BOOT_VERSION_MAGIC_LENGTH) != 0) {
        return false;
    }

    if (memcmp(header->image_tag, BOOT_VERSION_IMAGE_TAG, BOOT_VERSION_IMAGE_TAG_LENGTH) != 0) {
        return false;
    }

    if (header->raw_bin_crc32 == 0U) {
        return false;
    }

    if ((header->valid_bin_size == 0U) || (header->valid_bin_size > region->size)) {
        return false;
    }

    return (header->write_address == BOOT_IMAGE_EXPECTED_WRITE_ADDRESS);
}

static BootError Boot_SimpleJump_FindEmbeddedHeader(const BootSimpleFlashRegion *region,
                                                    BootVersionInfoHeader *header,
                                                    uint32_t *embedded_offset) {
    const uint8_t *flash_base;
    uint32_t offset;
    uint32_t limit;

    if ((region == NULL) || (header == NULL) || (embedded_offset == NULL) ||
        (region->size < BOOT_VERSION_INFO_SIZE)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    if (Boot_ExtFlash_Init() != BOOT_ERR_NONE) {
        return BOOT_ERR_EXTFLASH_READ;
    }

    flash_base = (const uint8_t *)(uintptr_t) region->base;
    limit      = region->size - BOOT_VERSION_INFO_SIZE;

    for (offset = 0U; offset <= limit; ++offset) {
        const BootVersionInfoHeader *candidate =
            (const BootVersionInfoHeader *)(const void *)&flash_base[offset];

        if ((offset & 0x0FFFU) == 0U) {
            Boot_Platform_FeedWatchdog();
        }

        if (Boot_SimpleJump_IsEmbeddedHeaderPlausible(candidate, region) == false) {
            continue;
        }

        *header          = *candidate;
        *embedded_offset = offset;
        return BOOT_ERR_NONE;
    }

    return BOOT_ERR_IMAGE_HEADER;
}

static void Boot_SimpleJump_ZeroCrcField(uint8_t *buffer,
                                         uint32_t buffer_offset,
                                         uint32_t buffer_size,
                                         uint32_t embedded_header_offset) {
    uint32_t zero_begin;
    uint32_t zero_end;
    uint32_t buffer_end;
    uint32_t overlap_begin;
    uint32_t overlap_end;

    if (buffer == NULL) {
        return;
    }

    zero_begin = embedded_header_offset + BOOT_VERSION_CRC32_OFFSET;
    zero_end   = zero_begin + sizeof(uint32_t);
    buffer_end = buffer_offset + buffer_size;

    if ((zero_begin >= buffer_end) || (zero_end <= buffer_offset)) {
        return;
    }

    overlap_begin = (zero_begin > buffer_offset) ? zero_begin : buffer_offset;
    overlap_end   = (zero_end < buffer_end) ? zero_end : buffer_end;
    memset(&buffer[overlap_begin - buffer_offset], 0, overlap_end - overlap_begin);
}

static BootError Boot_SimpleJump_ComputeSlotCrc(const BootSimpleFlashRegion *region,
                                                uint32_t image_size,
                                                uint32_t embedded_header_offset,
                                                uint32_t *crc32) {
    uint8_t buffer[BOOT_UPGRADE_READ_CHUNK_SIZE];
    uint32_t offset = 0U;
    uint32_t current_crc = 0xFFFFFFFFUL;
    BootError error;

    if ((region == NULL) || (crc32 == NULL) || (image_size == 0U) || (image_size > region->size)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    while (offset < image_size) {
        uint32_t chunk_size = image_size - offset;

        if (chunk_size > sizeof(buffer)) {
            chunk_size = sizeof(buffer);
        }

        error = Boot_SimpleFlash_Read(region->base + offset, buffer, chunk_size);
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        Boot_SimpleJump_ZeroCrcField(buffer, offset, chunk_size, embedded_header_offset);
        current_crc = Boot_Crc32_Mpeg2Update(current_crc, buffer, chunk_size);
        offset += chunk_size;
        Boot_Platform_FeedWatchdog();
    }

    *crc32 = current_crc;
    return BOOT_ERR_NONE;
}

BootError Boot_SimpleJump_ValidateSlot(uint8_t slot, uint32_t expected_crc) {
    const BootSimpleFlashRegion *region = Boot_SimpleFlash_GetSlotRegion(slot);
    BootVersionInfoHeader header;
    uint32_t stack_pointer = 0U;
    uint32_t reset_handler = 0U;
    uint32_t embedded_offset = 0U;
    uint32_t flash_crc = 0U;
    BootError error;
    bool vector_valid;

    if (region == NULL) {
        return BOOT_ERR_IMAGE_SLOT;
    }

    error = Boot_SimpleJump_ReadVector(region, &stack_pointer, &reset_handler);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    vector_valid = Boot_SimpleJump_IsVectorValid(stack_pointer, reset_handler, region);
    Boot_Handoff_RecordVector(region->base, stack_pointer, reset_handler, vector_valid);
    if (vector_valid == false) {
        LOG_WARN(BOOT_LOG_TAG,
                 "Slot=%s vector invalid: sp=0x%08lX reset=0x%08lX base=0x%08lX size=0x%08lX",
                 Boot_Info_SlotToString(slot), (unsigned long) stack_pointer,
                 (unsigned long) reset_handler, (unsigned long) region->base,
                 (unsigned long) region->size);
        return BOOT_ERR_IMAGE_VECTOR;
    }

    error = Boot_SimpleJump_FindEmbeddedHeader(region, &header, &embedded_offset);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    error = Boot_SimpleJump_ComputeSlotCrc(region, header.valid_bin_size, embedded_offset, &flash_crc);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    if (flash_crc != header.raw_bin_crc32) {
        LOG_WARN(BOOT_LOG_TAG,
                 "Slot=%s embedded CRC mismatch: flash=0x%08lX embedded=0x%08lX size=%lu header_off=0x%08lX",
                 Boot_Info_SlotToString(slot), (unsigned long) flash_crc,
                 (unsigned long) header.raw_bin_crc32, (unsigned long) header.valid_bin_size,
                 (unsigned long) embedded_offset);
        return BOOT_ERR_IMAGE_CRC;
    }

    if ((expected_crc != 0U) && (flash_crc != expected_crc)) {
        LOG_WARN(BOOT_LOG_TAG,
                 "Slot=%s expected CRC mismatch: flash=0x%08lX expected=0x%08lX",
                 Boot_Info_SlotToString(slot), (unsigned long) flash_crc,
                 (unsigned long) expected_crc);
        return BOOT_ERR_IMAGE_CRC;
    }

    return BOOT_ERR_NONE;
}

bool Boot_SimpleJump_IsSlotValid(uint8_t slot) {
    return (Boot_SimpleJump_ValidateSlot(slot, 0U) == BOOT_ERR_NONE);
}

bool Boot_SimpleJump_IsSlotValidWithCrc(uint8_t slot, uint32_t expected_crc) {
    return (Boot_SimpleJump_ValidateSlot(slot, expected_crc) == BOOT_ERR_NONE);
}

BootError Boot_SimpleJump_ToSlot(uint8_t slot) {
    const BootSimpleFlashRegion *region = Boot_SimpleFlash_GetSlotRegion(slot);
    uint32_t stack_pointer = 0U;
    uint32_t reset_handler = 0U;
    BootError error;

    if (region == NULL) {
        return BOOT_ERR_IMAGE_SLOT;
    }

    error = Boot_SimpleJump_ValidateSlot(slot, 0U);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    error = Boot_SimpleJump_ReadVector(region, &stack_pointer, &reset_handler);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    if (Boot_ExtFlash_Init() != BOOT_ERR_NONE) {
        return BOOT_ERR_EXTFLASH_READ;
    }

    LOG_INFO(BOOT_LOG_TAG, "App vector sp=0x%08lX reset=0x%08lX", (unsigned long)stack_pointer,
             (unsigned long)reset_handler);
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

    ((BootSimpleEntryPoint)reset_handler)();
    return BOOT_ERR_JUMP_FAILED;
}

bool Boot_SimpleJump_IsAppValid(void) {
    return Boot_SimpleJump_IsSlotValid(SLOT_A);
}

BootError Boot_SimpleJump_ToApp(void) {
    return Boot_SimpleJump_ToSlot(SLOT_A);
}
