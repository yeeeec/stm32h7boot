#include "update_package.h"

#include <stdio.h>
#include <string.h>

#include "bootloader_config.h"
#include "crypto/sha256.h"
#include "firmware/memory.h"
#include "firmware/product_identity.h"
#include "logging.h"
#include "platform/platform_storage.h"
#include "platform/platform_system.h"
#include "update_config.h"

static FIRMWARE_STORAGE_RAM uint8_t s_manifest_buffer[UPDATE_MANIFEST_MAX_SIZE];
static FIRMWARE_STORAGE_RAM uint8_t s_hash_buffer[UPDATE_IO_BLOCK_SIZE];

int UpdateHex_DecodeSha256(const char *text, uint8_t digest[32])
{
    size_t index;

    if (text == NULL || digest == NULL || strlen(text) != UPDATE_SHA256_HEX_LENGTH)
        return 0;
    for (index = 0U; index < 32U; ++index)
    {
        uint8_t value = 0U;
        size_t nibble;
        for (nibble = 0U; nibble < 2U; ++nibble)
        {
            char c = text[index * 2U + nibble];
            uint8_t digit;
            if (c >= '0' && c <= '9')
                digit = (uint8_t) (c - '0');
            else if (c >= 'a' && c <= 'f')
                digit = (uint8_t) (c - 'a' + 10);
            else
                return 0;
            value = (uint8_t) ((value << 4U) | digit);
        }
        digest[index] = value;
    }
    return 1;
}

static firmware_status_t read_manifest(const char *root, size_t *length)
{
    char path[UPDATE_PATH_MAX];
    platform_file_info_t info;
    platform_file_handle_t file;
    size_t total = 0U;
    firmware_status_t status;
    int path_length = snprintf(path, sizeof(path), "%s/%s", root, UPDATE_MANIFEST_FILE);

    if (path_length <= 0 || (size_t) path_length >= sizeof(path))
    {
        LOG_ERROR("update", "manifest path too long");
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    }
    status = PlatformStorage_Stat(path, &info);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "manifest read failed: status=%u", (unsigned) status);
        return status;
    }
    if (info.is_directory != 0U || info.size == 0U || info.size > sizeof(s_manifest_buffer))
    {
        LOG_ERROR("update", "manifest size invalid: size=%lu", (unsigned long) info.size);
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    }
    status = PlatformStorage_OpenRead(path, &file);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "manifest open failed: status=%u", (unsigned) status);
        return status;
    }

    while (total < info.size)
    {
        size_t requested = info.size - total;
        size_t actual = 0U;
        if (requested > UPDATE_IO_BLOCK_SIZE)
            requested = UPDATE_IO_BLOCK_SIZE;
        status = PlatformStorage_Read(file, s_manifest_buffer + total, requested, &actual);
        if (FirmwareStatus_IsError(status) || actual != requested)
        {
            (void) PlatformStorage_Close(file);
            if (FirmwareStatus_IsOk(status))
                status = FIRMWARE_STATUS_IO_ERROR;
            LOG_ERROR("update", "manifest read failed: status=%u", (unsigned) status);
            return status;
        }
        total += actual;
    }
    status = PlatformStorage_Close(file);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "manifest close failed: status=%u", (unsigned) status);
        return status;
    }
    *length = total;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t validate_file_set(const char *root,
                                           const update_manifest_t *manifest)
{
    platform_dir_handle_t directory;
    platform_dir_entry_t entry;
    firmware_status_t status = PlatformStorage_DirOpen(root, &directory);

    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "package directory open failed: status=%u", (unsigned) status);
        return status;
    }
    while ((status = PlatformStorage_DirRead(directory, &entry)) == FIRMWARE_STATUS_OK)
    {
        size_t index;
        int allowed = 0;

        if (entry.is_directory != 0U)
        {
            (void) PlatformStorage_DirClose(directory);
            LOG_ERROR("update", "package file set invalid: unexpected directory=%s", entry.name);
            return FIRMWARE_STATUS_INVALID_STATE;
        }
        if (strcmp(entry.name, UPDATE_MANIFEST_FILE) == 0)
        {
            allowed = 1;
        }
        else
        {
            for (index = 0U; index < manifest->component_count; ++index)
            {
                if (strcmp(entry.name, manifest->components[index].file) == 0)
                {
                    allowed = 1;
                    break;
                }
            }
        }
        if (!allowed)
        {
            (void) PlatformStorage_DirClose(directory);
            LOG_ERROR("update", "package file set invalid: unexpected file=%s", entry.name);
            return FIRMWARE_STATUS_INVALID_STATE;
        }
    }
    if (status != FIRMWARE_STATUS_NOT_FOUND)
    {
        (void) PlatformStorage_DirClose(directory);
        LOG_ERROR("update", "package directory read failed: status=%u", (unsigned) status);
        return status;
    }
    status = PlatformStorage_DirClose(directory);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "package directory close failed: status=%u", (unsigned) status);
        return status;
    }

    for (size_t index = 0U; index < manifest->component_count; ++index)
    {
        const update_component_descriptor_t *descriptor =
            UpdateComponent_Find(manifest->components[index].name);
        char path[UPDATE_PATH_MAX];
        platform_file_info_t info;
        int path_length;

        if (descriptor == NULL || !UpdateComponent_IsEnabled(descriptor))
            continue;
        path_length = snprintf(path, sizeof(path), "%s/%s", root, manifest->components[index].file);
        if (path_length <= 0 || (size_t) path_length >= sizeof(path))
        {
            LOG_ERROR("update", "package component path too long: component=%s",
                      manifest->components[index].name);
            return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
        }
        status = PlatformStorage_Stat(path, &info);
        if (FirmwareStatus_IsError(status))
        {
            LOG_ERROR("update", "package component missing: component=%s status=%u",
                      manifest->components[index].name, (unsigned) status);
            return status;
        }
        if (info.is_directory != 0U || info.size != manifest->components[index].size)
        {
            LOG_ERROR("update", "package component size invalid: component=%s size=%lu expected=%lu",
                      manifest->components[index].name, (unsigned long) info.size,
                      (unsigned long) manifest->components[index].size);
            return FIRMWARE_STATUS_INVALID_STATE;
        }
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t verify_payload(const char *root,
                                        const update_manifest_component_t *component)
{
    char path[UPDATE_PATH_MAX];
    platform_file_handle_t file;
    crypto_sha256_context_t hash;
    uint8_t digest[32];
    uint8_t expected[32];
    uint32_t total = 0U;
    firmware_status_t status;
    int path_length;

    if (!UpdateHex_DecodeSha256(component->sha256, expected))
    {
        LOG_ERROR("update", "payload SHA256 is invalid: component=%s", component->name);
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    path_length = snprintf(path, sizeof(path), "%s/%s", root, component->file);
    if (path_length <= 0 || (size_t) path_length >= sizeof(path))
    {
        LOG_ERROR("update", "payload path too long: component=%s", component->name);
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    }
    status = PlatformStorage_OpenRead(path, &file);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "payload open failed: component=%s status=%u", component->name,
                  (unsigned) status);
        return status;
    }
    if (Crypto_Sha256Init(&hash) != 0)
    {
        (void) PlatformStorage_Close(file);
        LOG_ERROR("update", "payload SHA256 init failed: component=%s", component->name);
        return FIRMWARE_STATUS_IO_ERROR;
    }
    while (total < component->size)
    {
        size_t requested = component->size - total;
        size_t actual = 0U;
        if (requested > sizeof(s_hash_buffer))
            requested = sizeof(s_hash_buffer);
        status = PlatformStorage_Read(file, s_hash_buffer, requested, &actual);
        if (FirmwareStatus_IsError(status) || actual != requested ||
            Crypto_Sha256Update(&hash, s_hash_buffer, actual) != 0)
        {
            Crypto_Sha256Abort(&hash);
            (void) PlatformStorage_Close(file);
            if (FirmwareStatus_IsOk(status))
                status = FIRMWARE_STATUS_IO_ERROR;
            LOG_ERROR("update", "payload read failed: component=%s status=%u", component->name,
                      (unsigned) status);
            return status;
        }
        total += (uint32_t) actual;
        PlatformSystem_WatchdogRefresh();
    }
    status = PlatformStorage_Close(file);
    if (FirmwareStatus_IsError(status))
    {
        Crypto_Sha256Abort(&hash);
        LOG_ERROR("update", "payload close failed: component=%s status=%u", component->name,
                  (unsigned) status);
        return status;
    }
    if (Crypto_Sha256Finish(&hash, digest) != 0)
    {
        LOG_ERROR("update", "payload SHA256 finish failed: component=%s", component->name);
        return FIRMWARE_STATUS_IO_ERROR;
    }
    if (memcmp(digest, expected, sizeof(digest)) != 0)
    {
        LOG_ERROR("update", "payload SHA256 mismatch: component=%s", component->name);
        return FIRMWARE_STATUS_AUTHENTICATION_FAILED;
    }
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PackageReader_Validate(const char *root, update_package_t *package)
{
    update_version_t bootloader_version = {FIRMWARE_BOOTLOADER_VERSION_MAJOR,
                                           FIRMWARE_BOOTLOADER_VERSION_MINOR,
                                           FIRMWARE_BOOTLOADER_VERSION_PATCH, 0U};
    size_t manifest_length;
    firmware_status_t status;

    LOG_INFO("update", "package validation start: root=%s", root != NULL ? root : "(null)");
    if (root == NULL || package == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    (void) memset(package, 0, sizeof(*package));

    status = read_manifest(root, &manifest_length);
    if (FirmwareStatus_IsError(status))
        return status;
    status = UpdateManifest_Parse(s_manifest_buffer, manifest_length, &package->manifest);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "manifest parse failed: status=%u", (unsigned) status);
        return status;
    }
    status = UpdateManifest_ValidateTarget(&package->manifest);
    if (FirmwareStatus_IsError(status))
    {
        LOG_ERROR("update", "target validation failed: status=%u", (unsigned) status);
        return status;
    }
    if (UpdateVersion_Compare(&bootloader_version,
                              &package->manifest.minimum_bootloader_version) < 0)
    {
        LOG_ERROR("update", "minimum bootloader version rejected");
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = validate_file_set(root, &package->manifest);
    if (FirmwareStatus_IsError(status))
        return status;
    for (size_t index = 0U; index < package->manifest.component_count; ++index)
    {
        status = verify_payload(root, &package->manifest.components[index]);
        if (FirmwareStatus_IsError(status))
            return status;
    }

    LOG_INFO("update", "package validation pass: package=%s version=%lu.%lu.%lu components=%lu",
             package->manifest.package_id, (unsigned long) package->manifest.release.major,
             (unsigned long) package->manifest.release.minor,
             (unsigned long) package->manifest.release.patch,
             (unsigned long) package->manifest.component_count);
    return FIRMWARE_STATUS_OK;
}
