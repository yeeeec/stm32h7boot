#include "boot_simple_upgrade.h"

#include <string.h>

#include "boot_crc32.h"
#include "boot_handoff.h"
#include "boot_info.h"
#include "boot_log.h"
#include "boot_simple_flash.h"
#include "boot_simple_jump.h"
#include "platform/boot_platform.h"

static void Boot_SimpleUpgrade_LogProgress(const BootAppImageInfo *image,
                                           uint8_t target_slot,
                                           uint32_t completed_bytes,
                                           uint32_t total_bytes,
                                           uint32_t *last_reported_percent,
                                           uint32_t max_percent) {
    uint32_t current_percent;
    uint32_t percent;

    if ((image == NULL) || (last_reported_percent == NULL) || (total_bytes == 0U)) {
        return;
    }

    current_percent = (completed_bytes * 100U) / total_bytes;
    if (current_percent > max_percent) {
        current_percent = max_percent;
    }

    for (percent = *last_reported_percent + 1U; percent <= current_percent; ++percent) {
        LOG_INFO(BOOT_LOG_TAG, "Upgrade slot=%s %s progress %lu%%",
                 Boot_Info_SlotToString(target_slot), image->file, (unsigned long)percent);
    }

    *last_reported_percent = current_percent;
}

static void Boot_SimpleUpgrade_LogCopyProgress(uint8_t source_slot,
                                               uint8_t target_slot,
                                               uint32_t completed_bytes,
                                               uint32_t total_bytes,
                                               uint32_t *last_reported_percent) {
    uint32_t current_percent;
    uint32_t percent;

    if ((last_reported_percent == NULL) || (total_bytes == 0U)) {
        return;
    }

    current_percent = (completed_bytes * 100U) / total_bytes;
    if (current_percent > 100U) {
        current_percent = 100U;
    }

    for (percent = *last_reported_percent + 1U; percent <= current_percent; ++percent) {
        LOG_INFO(BOOT_LOG_TAG, "Copy slot=%s->%s progress %lu%%",
                 Boot_Info_SlotToString(source_slot), Boot_Info_SlotToString(target_slot),
                 (unsigned long)percent);
    }

    *last_reported_percent = current_percent;
}

static BootError Boot_SimpleUpgrade_ComputeRegionCrc(const BootSimpleFlashRegion *region,
                                                     uint32_t image_size,
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

        current_crc = Boot_Crc32_Mpeg2Update(current_crc, buffer, chunk_size);
        offset += chunk_size;
        Boot_Platform_FeedWatchdog();
    }

    *crc32 = current_crc;
    return BOOT_ERR_NONE;
}

BootError Boot_SimpleUpgrade_ComputeSlotCrc(uint8_t slot, uint32_t image_size, uint32_t *crc32) {
    const BootSimpleFlashRegion *slot_region = Boot_SimpleFlash_GetSlotRegion(slot);

    if (slot_region == NULL) {
        return BOOT_ERR_IMAGE_SLOT;
    }

    return Boot_SimpleUpgrade_ComputeRegionCrc(slot_region, image_size, crc32);
}

BootError Boot_SimpleUpgrade_VerifySlotData(uint8_t slot, uint32_t expected_size,
                                            uint32_t expected_crc) {
    const BootSimpleFlashRegion *slot_region = Boot_SimpleFlash_GetSlotRegion(slot);
    uint32_t flash_crc = 0U;
    BootError error;

    if (slot_region == NULL) {
        return BOOT_ERR_IMAGE_SLOT;
    }

    if ((expected_size == 0U) || (expected_crc == 0U)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    if (expected_size > slot_region->size) {
        LOG_WARN(BOOT_LOG_TAG, "Slot=%s size out of range: size=%lu region=0x%08lX",
                 Boot_Info_SlotToString(slot), (unsigned long)expected_size,
                 (unsigned long)slot_region->size);
        return BOOT_ERR_IMAGE_SIZE;
    }

    error = Boot_SimpleUpgrade_ComputeRegionCrc(slot_region, expected_size, &flash_crc);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    if (flash_crc != expected_crc) {
        LOG_WARN(BOOT_LOG_TAG,
                 "Slot=%s CRC mismatch: flash=0x%08lX expected=0x%08lX size=%lu",
                 Boot_Info_SlotToString(slot), (unsigned long)flash_crc,
                 (unsigned long)expected_crc, (unsigned long)expected_size);
        return BOOT_ERR_IMAGE_CRC;
    }

    return BOOT_ERR_NONE;
}

BootError Boot_SimpleUpgrade_CopySlot(uint8_t source_slot, uint8_t target_slot, uint32_t image_size,
                                      uint32_t expected_crc) {
    const BootSimpleFlashRegion *source_region = Boot_SimpleFlash_GetSlotRegion(source_slot);
    const BootSimpleFlashRegion *target_region = Boot_SimpleFlash_GetSlotRegion(target_slot);
    uint8_t read_buffer[BOOT_UPGRADE_READ_CHUNK_SIZE];
    uint8_t verify_buffer[BOOT_UPGRADE_READ_CHUNK_SIZE];
    uint32_t source_address;
    uint32_t target_address;
    uint32_t total_copied = 0U;
    uint32_t last_reported_percent = 0U;
    BootError error;

    if ((source_region == NULL) || (target_region == NULL)) {
        return BOOT_ERR_IMAGE_SLOT;
    }

    if ((source_slot == target_slot) || (image_size == 0U) || (expected_crc == 0U)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    if ((image_size > source_region->size) || (image_size > target_region->size)) {
        return BOOT_ERR_IMAGE_SIZE;
    }

    error = Boot_SimpleUpgrade_VerifySlotData(source_slot, image_size, expected_crc);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    LOG_INFO(BOOT_LOG_TAG, "Copy slot=%s to slot=%s size=%lu",
             Boot_Info_SlotToString(source_slot), Boot_Info_SlotToString(target_slot),
             (unsigned long)image_size);

    error = Boot_SimpleFlash_EraseSlot(target_slot);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    source_address = source_region->base;
    target_address = target_region->base;
    while (total_copied < image_size) {
        uint32_t chunk_size = image_size - total_copied;

        if (chunk_size > sizeof(read_buffer)) {
            chunk_size = sizeof(read_buffer);
        }

        error = Boot_SimpleFlash_Read(source_address, read_buffer, chunk_size);
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        error = Boot_SimpleFlash_Write(target_address, read_buffer, chunk_size);
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        error = Boot_SimpleFlash_Read(target_address, verify_buffer, chunk_size);
        if (error != BOOT_ERR_NONE) {
            return error;
        }

        if (memcmp(read_buffer, verify_buffer, chunk_size) != 0) {
            return BOOT_ERR_FLASH_VERIFY;
        }

        source_address += chunk_size;
        target_address += chunk_size;
        total_copied += chunk_size;
        Boot_SimpleUpgrade_LogCopyProgress(source_slot, target_slot, total_copied, image_size,
                                           &last_reported_percent);
        Boot_Platform_FeedWatchdog();
    }

    error = Boot_SimpleUpgrade_VerifySlotData(target_slot, image_size, expected_crc);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    LOG_INFO(BOOT_LOG_TAG, "Copy slot=%s to slot=%s success, size=%lu crc=0x%08lX",
             Boot_Info_SlotToString(source_slot), Boot_Info_SlotToString(target_slot),
             (unsigned long)image_size, (unsigned long)expected_crc);
    return BOOT_ERR_NONE;
}

static BootError Boot_SimpleUpgrade_RunSingleAttempt(const BootAppImageInfo *image,
                                                     uint8_t target_slot) {
    BootPlatformFile file;
    const BootSimpleFlashRegion *slot_region;
    uint8_t read_buffer[BOOT_UPGRADE_READ_CHUNK_SIZE];
    uint8_t verify_buffer[BOOT_UPGRADE_READ_CHUNK_SIZE];
    char relative_path[BOOT_FILE_PATH_LENGTH];
    uint32_t total_written = 0U;
    uint32_t last_reported_percent = 0U;
    uint32_t write_address;
    uint32_t expected_file_size;
    BootError error;

    if (image == NULL) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    slot_region = Boot_SimpleFlash_GetSlotRegion(target_slot);
    if (slot_region == NULL) {
        return BOOT_ERR_IMAGE_SLOT;
    }

    if (image->size > slot_region->size) {
        return BOOT_ERR_IMAGE_SIZE;
    }

    error = Boot_SimpleManifest_BuildAppPath(relative_path, sizeof(relative_path));
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    error = Boot_Platform_FileOpenRead(relative_path, &file);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    expected_file_size = image->size + BOOT_VERSION_INFO_SIZE;
    if (Boot_Platform_FileSize(&file) != expected_file_size) {
        Boot_Platform_FileClose(&file);
        return BOOT_ERR_FILE_SIZE;
    }

    error = Boot_Platform_FileSeek(&file, BOOT_VERSION_INFO_SIZE);
    if (error != BOOT_ERR_NONE) {
        Boot_Platform_FileClose(&file);
        return BOOT_ERR_FILE_HEADER;
    }

    LOG_INFO(BOOT_LOG_TAG, "Erase slot=%s", Boot_Info_SlotToString(target_slot));
    error = Boot_SimpleFlash_EraseSlot(target_slot);
    if (error != BOOT_ERR_NONE) {
        Boot_Platform_FileClose(&file);
        return error;
    }

    LOG_INFO(BOOT_LOG_TAG, "Upgrade slot=%s %s progress 0%%", Boot_Info_SlotToString(target_slot),
             image->file);

    write_address = slot_region->base;
    while (total_written < image->size) {
        uint32_t chunk_size = image->size - total_written;
        uint32_t bytes_read = 0U;

        if (chunk_size > BOOT_UPGRADE_READ_CHUNK_SIZE) {
            chunk_size = BOOT_UPGRADE_READ_CHUNK_SIZE;
        }

        error = Boot_Platform_FileRead(&file, read_buffer, chunk_size, &bytes_read);
        if ((error != BOOT_ERR_NONE) || (bytes_read != chunk_size)) {
            Boot_Platform_FileClose(&file);
            return BOOT_ERR_FILE_SIZE;
        }

        error = Boot_SimpleFlash_Write(write_address, read_buffer, bytes_read);
        if (error != BOOT_ERR_NONE) {
            Boot_Platform_FileClose(&file);
            return error;
        }

        error = Boot_SimpleFlash_Read(write_address, verify_buffer, bytes_read);
        if (error != BOOT_ERR_NONE) {
            Boot_Platform_FileClose(&file);
            return error;
        }

        if (memcmp(read_buffer, verify_buffer, bytes_read) != 0) {
            Boot_Platform_FileClose(&file);
            return BOOT_ERR_FLASH_VERIFY;
        }

        write_address += bytes_read;
        total_written += bytes_read;
        Boot_SimpleUpgrade_LogProgress(image, target_slot, total_written, image->size,
                                       &last_reported_percent, 99U);
        Boot_Platform_FeedWatchdog();
    }

    Boot_Platform_FileClose(&file);

    error = Boot_SimpleJump_ValidateSlot(target_slot, image->size, image->crc32);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    Boot_Handoff_RecordUpgrade(image->size, image->crc32, image->crc32, image->crc32);
    Boot_Handoff_LogCurrent("Upgrade handoff");
    Boot_SimpleUpgrade_LogProgress(image, target_slot, image->size, image->size,
                                   &last_reported_percent, 100U);
    return BOOT_ERR_NONE;
}

BootError Boot_SimpleUpgrade_Run(const BootAppImageInfo *image, uint8_t target_slot) {
    BootError last_error = BOOT_ERR_INVALID_ARGUMENT;
    uint32_t attempt;

    if (image == NULL) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    for (attempt = 0U; attempt < BOOT_UPGRADE_MAX_RETRIES; ++attempt) {
        LOG_INFO(BOOT_LOG_TAG, "Upgrade slot=%s %s attempt %lu/%u",
                 Boot_Info_SlotToString(target_slot), image->file,
                 (unsigned long)(attempt + 1U), (unsigned int)BOOT_UPGRADE_MAX_RETRIES);

        last_error = Boot_SimpleUpgrade_RunSingleAttempt(image, target_slot);
        if (last_error == BOOT_ERR_NONE) {
            LOG_INFO(BOOT_LOG_TAG, "Upgrade slot=%s %s success, size=%lu crc=0x%08lX",
                     Boot_Info_SlotToString(target_slot), image->file,
                     (unsigned long)image->size, (unsigned long)image->crc32);
            return BOOT_ERR_NONE;
        }

        LOG_WARN(BOOT_LOG_TAG, "Upgrade slot=%s %s failed: %s",
                 Boot_Info_SlotToString(target_slot), image->file,
                 Boot_ErrorToString(last_error));
    }

    return last_error;
}
