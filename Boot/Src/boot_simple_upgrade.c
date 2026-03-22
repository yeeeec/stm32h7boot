#include "boot_simple_upgrade.h"

#include <string.h>

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_info.h"
#include "boot_handoff.h"
#include "boot_log.h"
#include "boot_platform.h"
#include "boot_simple_flash.h"
#include "boot_simple_jump.h"

typedef struct {
    uint32_t magic;
    uint32_t payload_crc32;
} BootUpgradePacketHeader;

static void Boot_SimpleUpgrade_LogProgress(const BootManifestOperation *operation,
                                           uint8_t target_slot,
                                           uint32_t completed_bytes,
                                           uint32_t total_bytes,
                                           uint32_t *last_reported_percent,
                                           uint32_t max_percent) {
    uint32_t current_percent;
    uint32_t percent;

    if ((operation == NULL) || (last_reported_percent == NULL) || (total_bytes == 0U)) {
        return;
    }

    current_percent = (completed_bytes * 100U) / total_bytes;
    if (current_percent > max_percent) {
        current_percent = max_percent;
    }

    for (percent = *last_reported_percent + 1U; percent <= current_percent; ++percent) {
        LOG_INFO(BOOT_LOG_TAG, "Upgrade slot=%s %s progress %lu%%",
                 Boot_Info_SlotToString(target_slot), operation->file,
                 (unsigned long) percent);
    }

    *last_reported_percent = current_percent;
}

static uint32_t Boot_SimpleUpgrade_ReadLe32(const uint8_t *data) {
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8U) | ((uint32_t) data[2] << 16U) |
           ((uint32_t) data[3] << 24U);
}

static BootError Boot_SimpleUpgrade_ReadHeader(BootPlatformFile *file,
                                               BootUpgradePacketHeader *header) {
    uint8_t raw_header[BOOT_UPGRADE_PACKET_HEADER_SIZE];
    uint32_t bytes_read = 0U;
    BootError error;

    if ((file == NULL) || (header == NULL)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    error = Boot_Platform_FileRead(file, raw_header, sizeof(raw_header), &bytes_read);
    if ((error != BOOT_ERR_NONE) || (bytes_read != sizeof(raw_header))) {
        return BOOT_ERR_FILE_HEADER;
    }

    header->magic         = Boot_SimpleUpgrade_ReadLe32(&raw_header[0]);
    header->payload_crc32 = Boot_SimpleUpgrade_ReadLe32(&raw_header[4]);
    return BOOT_ERR_NONE;
}

static BootError Boot_SimpleUpgrade_RunSingleAttempt(const BootManifestOperation *operation,
                                                     uint8_t target_slot) {
    BootPlatformFile file;
    const BootSimpleFlashRegion *slot_region;
    BootUpgradePacketHeader header;
    uint8_t read_buffer[BOOT_UPGRADE_READ_CHUNK_SIZE];
    uint8_t verify_buffer[BOOT_UPGRADE_READ_CHUNK_SIZE];
    char relative_path[BOOT_FILE_PATH_LENGTH];
    uint32_t source_crc    = 0xFFFFFFFFUL;
    uint32_t flash_crc     = 0xFFFFFFFFUL;
    uint32_t total_written = 0U;
    uint32_t last_reported_percent = 0U;
    uint32_t write_address;
    uint32_t expected_file_size;
    BootError error;

    if (operation == NULL) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    slot_region = Boot_SimpleFlash_GetSlotRegion(target_slot);
    if (slot_region == NULL) {
        return BOOT_ERR_IMAGE_SLOT;
    }

    if (operation->size > slot_region->size) {
        return BOOT_ERR_IMAGE_SIZE;
    }

    error = Boot_SimpleManifest_BuildCrcPath(operation->file, relative_path, sizeof(relative_path));
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    error = Boot_Platform_FileOpenRead(relative_path, &file);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    expected_file_size = operation->size + BOOT_UPGRADE_PACKET_HEADER_SIZE;
    if (Boot_Platform_FileSize(&file) != expected_file_size) {
        Boot_Platform_FileClose(&file);
        return BOOT_ERR_FILE_SIZE;
    }

    error = Boot_SimpleUpgrade_ReadHeader(&file, &header);
    if (error != BOOT_ERR_NONE) {
        Boot_Platform_FileClose(&file);
        return error;
    }

    if (header.magic != BOOT_UPGRADE_PACKET_FLAG) {
        Boot_Platform_FileClose(&file);
        return BOOT_ERR_FILE_HEADER;
    }

    if (header.payload_crc32 != operation->crc32) {
        Boot_Platform_FileClose(&file);
        return BOOT_ERR_FILE_CRC;
    }

    LOG_INFO(BOOT_LOG_TAG, "Erase slot=%s", Boot_Info_SlotToString(target_slot));
    error = Boot_SimpleFlash_EraseSlot(target_slot);
    if (error != BOOT_ERR_NONE) {
        Boot_Platform_FileClose(&file);
        return error;
    }

    LOG_INFO(BOOT_LOG_TAG, "Upgrade slot=%s %s progress 0%%", Boot_Info_SlotToString(target_slot),
             operation->file);

    write_address = slot_region->base;
    while (total_written < operation->size) {
        uint32_t chunk_size = operation->size - total_written;
        uint32_t bytes_read = 0U;

        if (chunk_size > BOOT_UPGRADE_READ_CHUNK_SIZE) {
            chunk_size = BOOT_UPGRADE_READ_CHUNK_SIZE;
        }

        error = Boot_Platform_FileRead(&file, read_buffer, chunk_size, &bytes_read);
        if ((error != BOOT_ERR_NONE) || (bytes_read != chunk_size)) {
            Boot_Platform_FileClose(&file);
            return BOOT_ERR_FILE_SIZE;
        }

        source_crc = Boot_Crc32_Mpeg2Update(source_crc, read_buffer, bytes_read);

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

        flash_crc = Boot_Crc32_Mpeg2Update(flash_crc, verify_buffer, bytes_read);
        write_address += bytes_read;
        total_written += bytes_read;
        Boot_SimpleUpgrade_LogProgress(operation, target_slot, total_written, operation->size,
                                       &last_reported_percent, 99U);
        Boot_Platform_FeedWatchdog();
    }

    Boot_Platform_FileClose(&file);

    if (source_crc != operation->crc32) {
        return BOOT_ERR_FILE_CRC;
    }

    if ((flash_crc != operation->crc32) || (flash_crc != source_crc)) {
        return BOOT_ERR_FLASH_VERIFY;
    }

    if (Boot_SimpleJump_IsSlotValid(target_slot) == false) {
        return BOOT_ERR_IMAGE_VECTOR;
    }

    Boot_Handoff_RecordUpgrade(operation->size, operation->crc32, source_crc, flash_crc);
    LOG_INFO(BOOT_LOG_TAG, "Upgrade slot=%s verify src=0x%08lX flash=0x%08lX",
             Boot_Info_SlotToString(target_slot), (unsigned long) source_crc,
             (unsigned long) flash_crc);
    Boot_Handoff_LogCurrent("Upgrade handoff");
    Boot_SimpleUpgrade_LogProgress(operation, target_slot, operation->size, operation->size,
                                   &last_reported_percent, 100U);

    return BOOT_ERR_NONE;
}

BootError Boot_SimpleUpgrade_Run(const BootManifestOperation *operation, uint8_t target_slot) {
    BootError last_error = BOOT_ERR_INVALID_ARGUMENT;
    uint32_t attempt;

    if (operation == NULL) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    for (attempt = 0U; attempt < BOOT_UPGRADE_MAX_RETRIES; ++attempt) {
        LOG_INFO(BOOT_LOG_TAG, "Upgrade slot=%s %s attempt %lu/%u",
                 Boot_Info_SlotToString(target_slot), operation->file,
                 (unsigned long) (attempt + 1U), (unsigned int) BOOT_UPGRADE_MAX_RETRIES);

        last_error = Boot_SimpleUpgrade_RunSingleAttempt(operation, target_slot);
        if (last_error == BOOT_ERR_NONE) {
            LOG_INFO(BOOT_LOG_TAG, "Upgrade slot=%s %s success, size=%lu crc=0x%08lX",
                     Boot_Info_SlotToString(target_slot), operation->file,
                     (unsigned long) operation->size, (unsigned long) operation->crc32);
            return BOOT_ERR_NONE;
        }

        LOG_WARN(BOOT_LOG_TAG, "Upgrade slot=%s %s failed: %s",
                 Boot_Info_SlotToString(target_slot), operation->file,
                 Boot_ErrorToString(last_error));
    }

    return last_error;
}
