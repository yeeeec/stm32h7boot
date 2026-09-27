#include "image_installer.h"

#include <stdio.h>
#include <string.h>

#include "crypto/sha256.h"
#include "firmware/memory.h"
#include "logging.h"
#include "platform/platform_flash.h"
#include "platform/platform_memory_map.h"
#include "platform/platform_storage.h"
#include "platform/platform_system.h"
#include "platform/platform_therapy.h"
#include "update/component_registry.h"
#include "update_config.h"
#include "update_package.h"

static FIRMWARE_STORAGE_RAM uint8_t s_source_block[UPDATE_IO_BLOCK_SIZE];
static FIRMWARE_STORAGE_RAM uint8_t s_readback_block[UPDATE_IO_BLOCK_SIZE];

static const char *target_name(image_target_t target)
{
    switch (target)
    {
        case IMAGE_TARGET_APP:
            return "app";
        case IMAGE_TARGET_GUI:
            return "gui";
        case IMAGE_TARGET_THERAPY:
            return "therapy";
        case IMAGE_TARGET_VOICE:
            return "voice";
        case IMAGE_TARGET_CONFIG:
            return "config";
        case IMAGE_TARGET_RESOURCE:
            return "resource";
        default:
            return "unknown";
    }
}

static firmware_status_t target_limits(image_target_t target, uint32_t *address,
                                       uint32_t *maximum_size)
{
    const update_component_descriptor_t *descriptor;
    if (address == NULL || maximum_size == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;

    descriptor = UpdateComponent_FindByTarget(target);
    if (descriptor == NULL)
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    *address = descriptor->address;
    *maximum_size = descriptor->maximum_size;

    switch (target)
    {
        case IMAGE_TARGET_THERAPY:
        case IMAGE_TARGET_APP:
        case IMAGE_TARGET_GUI:
        case IMAGE_TARGET_VOICE:
        case IMAGE_TARGET_CONFIG:
            return FIRMWARE_STATUS_OK;
        case IMAGE_TARGET_RESOURCE:
            return FIRMWARE_STATUS_NOT_SUPPORTED;
        default:
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
}

static firmware_status_t target_erase(image_target_t target, uint32_t address, uint32_t size)
{
    return target == IMAGE_TARGET_THERAPY ? PlatformTherapy_Erase(address, size)
                                          : PlatformFlash_Erase(address, size);
}

static firmware_status_t target_write(image_target_t target, uint32_t address, const void *data,
                                      uint32_t size)
{
    return target == IMAGE_TARGET_THERAPY ? PlatformTherapy_Write(address, data, size)
                                          : PlatformFlash_Write(address, data, size);
}

static firmware_status_t target_read(image_target_t target, uint32_t address, void *data,
                                     uint32_t size)
{
    return target == IMAGE_TARGET_THERAPY ? PlatformTherapy_Read(address, data, size)
                                          : PlatformFlash_Read(address, data, size);
}

firmware_status_t ImageInstaller_Install(const char *root,
                                         const update_manifest_component_t *component)
{
    char path[UPDATE_PATH_MAX];
    platform_file_info_t info;
    platform_file_handle_t file = 0U;
    crypto_sha256_context_t hash;
    uint8_t expected_digest[32];
    uint8_t installed_digest[32];
    uint32_t target_address;
    uint32_t maximum_size;
    uint32_t offset = 0U;
    uint32_t erase_size;
    int file_open = 0;
    int hash_started = 0;
    int therapy_started = 0;
    firmware_status_t status;

    if (root == NULL || component == NULL ||
        target_limits(component->target, &target_address, &maximum_size) != FIRMWARE_STATUS_OK)
    {
        LOG_ERROR("install", "invalid component request");
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    {
        int length = snprintf(path, sizeof(path), "%s/%s", root, component->file);
        if (length <= 0 || (size_t) length >= sizeof(path))
        {
            LOG_ERROR("install", "source path too long: component=%s", component->name);
            return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
        }
    }
    if (!UpdateHex_DecodeSha256(component->sha256, expected_digest))
    {
        LOG_ERROR("install", "component SHA256 is invalid: component=%s", component->name);
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    status = PlatformStorage_Stat(path, &info);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("install", "source stat failed: component=%s status=%u", component->name,
                  (unsigned) status);
        return status;
    }
    if (info.is_directory != 0U)
    {
        LOG_ERROR("install", "source is a directory: component=%s", component->name);
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (info.size == 0U || info.size != component->size || info.size > maximum_size)
    {
        LOG_ERROR("install", "source size invalid: component=%s size=%lu expected=%lu",
                  component->name, (unsigned long) info.size, (unsigned long) component->size);
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    status = PlatformStorage_OpenRead(path, &file);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("install", "source open failed: component=%s status=%u", component->name,
                  (unsigned) status);
        return status;
    }
    file_open = 1;

    status = Crypto_Sha256Init(&hash);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("install", "SHA256 init failed: component=%s status=%u", component->name,
                  (unsigned) status);
        goto cleanup;
    }
    hash_started = 1;

    if (component->target == IMAGE_TARGET_THERAPY)
    {
        status = PlatformTherapy_Init();
        if (FirmwareStatus_IsOk(status))
            status = PlatformTherapy_BeginUpdate(NULL);
        if (FirmwareStatus_IsError(status))
        {
            LOG_ERROR("install", "therapy update begin failed: component=%s status=%u",
                      component->name, (unsigned) status);
            goto cleanup;
        }
        therapy_started = 1;
        erase_size = component->size;
    }
    else
    {
        status = PlatformFlash_Init();
        if (FirmwareStatus_IsOk(status))
            status = PlatformFlash_ExitMemoryMapped();
        if (FirmwareStatus_IsError(status))
        {
            LOG_ERROR("install", "flash prepare failed: component=%s status=%u", component->name,
                      (unsigned) status);
            goto cleanup;
        }
        erase_size =
            (component->size + PLATFORM_FLASH_ERASE_SIZE - 1U) & ~(PLATFORM_FLASH_ERASE_SIZE - 1U);
    }
    LOG_INFO("install", "erase start: component=%s target=%s address=0x%08lx size=%lu bytes",
             component->name, target_name(component->target), (unsigned long) target_address,
             (unsigned long) erase_size);
    status = target_erase(component->target, target_address, erase_size);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("install", "erase failed: component=%s status=%u", component->name,
                  (unsigned) status);
        goto cleanup;
    }
    LOG_INFO("install", "erase complete: component=%s", component->name);

    LOG_INFO("install", "write start: component=%s size=%lu", component->name,
             (unsigned long) component->size);
    while (offset < component->size)
    {
        size_t requested = component->size - offset;
        size_t received = 0U;

        if (requested > sizeof(s_source_block))
            requested = sizeof(s_source_block);
        status = PlatformStorage_Read(file, s_source_block, requested, &received);
        if (FirmwareStatus_IsError(status) || received != requested)
        {
            if (FirmwareStatus_IsOk(status))
                status = FIRMWARE_STATUS_IO_ERROR;
            LOG_ERROR("install", "source read failed: component=%s status=%u", component->name,
                      (unsigned) status);
            goto cleanup;
        }
        status = Crypto_Sha256Update(&hash, s_source_block, received);
        if (FirmwareStatus_IsError(status))
        {
            LOG_ERROR("install", "SHA256 update failed: component=%s status=%u", component->name,
                      (unsigned) status);
            goto cleanup;
        }
        status = target_write(component->target, target_address + offset, s_source_block,
                              (uint32_t) received);
        if (FirmwareStatus_IsError(status))
        {
            LOG_ERROR("install", "write failed: component=%s status=%u", component->name,
                      (unsigned) status);
            goto cleanup;
        }
        status = target_read(component->target, target_address + offset, s_readback_block,
                             (uint32_t) received);
        if (FirmwareStatus_IsError(status) || memcmp(s_source_block, s_readback_block, received) != 0)
        {
            if (FirmwareStatus_IsOk(status))
                status = FIRMWARE_STATUS_AUTHENTICATION_FAILED;
            LOG_ERROR("install", "readback failed: component=%s status=%u", component->name,
                      (unsigned) status);
            goto cleanup;
        }
        offset += (uint32_t) received;
        PlatformSystem_WatchdogRefresh();
    }

    LOG_INFO("install", "write complete: component=%s bytes=%lu", component->name,
             (unsigned long) offset);
    status = Crypto_Sha256Finish(&hash, installed_digest);
    hash_started = 0;
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("install", "SHA256 finish failed: component=%s status=%u", component->name,
                  (unsigned) status);
        goto cleanup;
    }
    if (memcmp(installed_digest, expected_digest, sizeof(installed_digest)) != 0)
    {
        status = FIRMWARE_STATUS_AUTHENTICATION_FAILED;
        LOG_ERROR("install", "hash mismatch: component=%s", component->name);
    }

cleanup:
    if (hash_started != 0)
        Crypto_Sha256Abort(&hash);
    if (file_open != 0)
    {
        firmware_status_t close_status = PlatformStorage_Close(file);
        if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(close_status))
        {
            status = close_status;
            LOG_ERROR("install", "source close failed: component=%s status=%u", component->name,
                      (unsigned) status);
        }
    }
    if (therapy_started != 0)
    {
        firmware_status_t end_status = PlatformTherapy_EndUpdate();
        if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(end_status))
        {
            status = end_status;
            LOG_ERROR("install", "therapy update end failed: component=%s status=%u",
                      component->name, (unsigned) status);
        }
    }
    if (FirmwareStatus_IsOk(status))
        LOG_INFO("install", "verify pass: component=%s", component->name);
    return status;
}
