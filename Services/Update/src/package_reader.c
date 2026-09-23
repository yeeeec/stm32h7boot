#include "update_internal.h"

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

static update_operation_result_t package_result(update_failure_t failure, firmware_status_t status)
{
    update_operation_result_t result = {failure, status};
    return result;
}

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

static update_operation_result_t read_manifest(const char *root, size_t *length)
{
    char path[UPDATE_PATH_MAX];
    platform_file_info_t info;
    platform_file_handle_t file;
    size_t total = 0U;
    firmware_status_t status;

    {
        int length = snprintf(path, sizeof(path), "%s/%s", root, UPDATE_MANIFEST_FILE);
        if (length <= 0 || (size_t) length >= sizeof(path))
            return package_result(UPDATE_FAILURE_MANIFEST_READ, FIRMWARE_STATUS_BUFFER_TOO_SMALL);
    }
    status = PlatformStorage_Stat(path, &info);
    if (FirmwareStatus_IsError(status))
        return package_result(UPDATE_FAILURE_MANIFEST_READ, status);
    if (info.is_directory != 0U || info.size == 0U || info.size > sizeof(s_manifest_buffer))
        return package_result(UPDATE_FAILURE_MANIFEST_READ, FIRMWARE_STATUS_BUFFER_TOO_SMALL);
    status = PlatformStorage_OpenRead(path, &file);
    if (FirmwareStatus_IsError(status))
        return package_result(UPDATE_FAILURE_MANIFEST_READ, status);

    while (total < info.size)
    {
        size_t requested = info.size - total;
        size_t actual    = 0U;
        if (requested > UPDATE_IO_BLOCK_SIZE)
            requested = UPDATE_IO_BLOCK_SIZE;
        status = PlatformStorage_Read(file, s_manifest_buffer + total, requested, &actual);
        if (FirmwareStatus_IsError(status) || actual != requested)
        {
            (void) PlatformStorage_Close(file);
            return package_result(UPDATE_FAILURE_MANIFEST_READ, FirmwareStatus_IsError(status)
                                                                    ? status
                                                                    : FIRMWARE_STATUS_IO_ERROR);
        }
        total += actual;
    }
    status = PlatformStorage_Close(file);
    if (FirmwareStatus_IsError(status))
        return package_result(UPDATE_FAILURE_MANIFEST_READ, status);
    *length = total;
    return package_result(UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}

static update_operation_result_t validate_file_set(const char *root,
                                                   const update_manifest_t *manifest)
{
    platform_dir_handle_t directory;
    platform_dir_entry_t entry;
    /* 打开目录 */
    firmware_status_t status = PlatformStorage_DirOpen(root, &directory);
    if (FirmwareStatus_IsError(status))
        return package_result(UPDATE_FAILURE_FILE_SET, status);
    /* 读取目录每一项 */
    while ((status = PlatformStorage_DirRead(directory, &entry)) == FIRMWARE_STATUS_OK)
    {
        size_t index;
        int allowed = 0;

        if (entry.is_directory != 0U)
        {
            (void) PlatformStorage_DirClose(directory);
            return package_result(UPDATE_FAILURE_FILE_SET, FIRMWARE_STATUS_INVALID_STATE);
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
            return package_result(UPDATE_FAILURE_FILE_SET, FIRMWARE_STATUS_INVALID_STATE);
        }
    }
    if (status != FIRMWARE_STATUS_NOT_FOUND)
        return package_result(UPDATE_FAILURE_FILE_SET, status);
    status = PlatformStorage_DirClose(directory);
    if (FirmwareStatus_IsError(status))
        return package_result(UPDATE_FAILURE_FILE_SET, status);

    for (size_t index = 0U; index < manifest->component_count; ++index)
    {
        const update_component_descriptor_t *descriptor =
            UpdateComponent_Find(manifest->components[index].name);
        if (descriptor == NULL || !UpdateComponent_IsEnabled(descriptor))
            continue;
        {
            char path[UPDATE_PATH_MAX];
            platform_file_info_t info;
            {
                int length =
                    snprintf(path, sizeof(path), "%s/%s", root, manifest->components[index].file);
                if (length <= 0 || (size_t) length >= sizeof(path))
                    return package_result(UPDATE_FAILURE_FILE_SET,
                                          FIRMWARE_STATUS_BUFFER_TOO_SMALL);
            }
            status = PlatformStorage_Stat(path, &info);
            if (FirmwareStatus_IsError(status) || info.is_directory != 0U ||
                info.size != manifest->components[index].size)
                return package_result(UPDATE_FAILURE_FILE_SET, FirmwareStatus_IsError(status)
                                                                   ? status
                                                                   : FIRMWARE_STATUS_INVALID_STATE);
        }
    }
    return package_result(UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}

static update_operation_result_t verify_payload(const char *root,
                                                const update_manifest_component_t *component)
{
    char path[UPDATE_PATH_MAX];
    platform_file_handle_t file;
    crypto_sha256_context_t hash;
    uint8_t digest[32];
    uint8_t expected[32];
    uint32_t total = 0U;
    firmware_status_t status;

    if (!UpdateHex_DecodeSha256(component->sha256, expected))
        return package_result(UPDATE_FAILURE_CURRENT_VERIFY, FIRMWARE_STATUS_INVALID_ARGUMENT);
    {
        int length = snprintf(path, sizeof(path), "%s/%s", root, component->file);
        if (length <= 0 || (size_t) length >= sizeof(path))
            return package_result(UPDATE_FAILURE_CURRENT_VERIFY, FIRMWARE_STATUS_BUFFER_TOO_SMALL);
    }
    status = PlatformStorage_OpenRead(path, &file);
    if (FirmwareStatus_IsError(status))
        return package_result(UPDATE_FAILURE_CURRENT_VERIFY, status);
    if (Crypto_Sha256Init(&hash) != 0)
    {
        (void) PlatformStorage_Close(file);
        return package_result(UPDATE_FAILURE_CURRENT_VERIFY, FIRMWARE_STATUS_IO_ERROR);
    }
    while (total < component->size)
    {
        size_t requested = component->size - total;
        size_t actual    = 0U;
        if (requested > sizeof(s_hash_buffer))
            requested = sizeof(s_hash_buffer);
        status = PlatformStorage_Read(file, s_hash_buffer, requested, &actual);
        if (FirmwareStatus_IsError(status) || actual != requested ||
            Crypto_Sha256Update(&hash, s_hash_buffer, actual) != 0)
        {
            Crypto_Sha256Abort(&hash);
            (void) PlatformStorage_Close(file);
            return package_result(UPDATE_FAILURE_CURRENT_VERIFY, FirmwareStatus_IsError(status)
                                                                     ? status
                                                                     : FIRMWARE_STATUS_IO_ERROR);
        }
        total += (uint32_t) actual;
        PlatformSystem_WatchdogRefresh();
    }
    status = PlatformStorage_Close(file);
    if (FirmwareStatus_IsError(status))
    {
        Crypto_Sha256Abort(&hash);
        return package_result(UPDATE_FAILURE_CURRENT_VERIFY, status);
    }
    if (Crypto_Sha256Finish(&hash, digest) != 0)
        return package_result(UPDATE_FAILURE_CURRENT_VERIFY, FIRMWARE_STATUS_IO_ERROR);
    if (memcmp(digest, expected, sizeof(digest)) != 0)
        return package_result(UPDATE_FAILURE_CURRENT_VERIFY, FIRMWARE_STATUS_AUTHENTICATION_FAILED);
    return package_result(UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}

update_operation_result_t PackageReader_Validate(const char *root,
                                                 const uint8_t *expected_manifest_digest,
                                                 int verify_payload_hashes,
                                                 update_package_t *package)
{
    update_operation_result_t result;
    update_version_t bootloader_version = {FIRMWARE_BOOTLOADER_VERSION_MAJOR,
                                           FIRMWARE_BOOTLOADER_VERSION_MINOR,
                                           FIRMWARE_BOOTLOADER_VERSION_PATCH, 0U};
    size_t manifest_length;

    LOG_INFO("update", "package validation start: root=%s", root != NULL ? root : "(null)");

    if (root == NULL || package == NULL ||
        (verify_payload_hashes != 0 && verify_payload_hashes != 1))
        return package_result(UPDATE_FAILURE_MANIFEST_READ, FIRMWARE_STATUS_INVALID_ARGUMENT);
    (void) memset(package, 0, sizeof(*package));
    if (strlen(root) >= sizeof(package->root))
        return package_result(UPDATE_FAILURE_MANIFEST_READ, FIRMWARE_STATUS_BUFFER_TOO_SMALL);
    (void) strcpy(package->root, root);

    result = read_manifest(root, &manifest_length);
    if (FirmwareStatus_IsError(result.status))
        return result;
    /* 解析 manifest */
    result.status = UpdateManifest_Parse(s_manifest_buffer, manifest_length, &package->manifest);
    if (FirmwareStatus_IsError(result.status))
        return package_result(UPDATE_FAILURE_MANIFEST_PARSE, result.status);
    /* 计算HASH值 manifest */
    if (UpdateManifest_Digest(&package->manifest, package->manifest_sha256) != FIRMWARE_STATUS_OK)
        return package_result(UPDATE_FAILURE_MANIFEST_DIGEST, FIRMWARE_STATUS_IO_ERROR);
    if (expected_manifest_digest != NULL &&
        memcmp(package->manifest_sha256, expected_manifest_digest, 32U) != 0)
        return package_result(UPDATE_FAILURE_MANIFEST_DIGEST,
                              FIRMWARE_STATUS_AUTHENTICATION_FAILED);
    /* 校验内容合法性 manifest */
    result.status = UpdateManifest_ValidateTarget(&package->manifest);
    if (FirmwareStatus_IsError(result.status))
        return package_result(UPDATE_FAILURE_TARGET, result.status);
    /* 检查最低Bootloader版本支持 */
    result.status = VersionPolicy_ValidateMinimumBootloader(
        &package->manifest.minimum_bootloader_version, &bootloader_version);
    if (FirmwareStatus_IsError(result.status))
        return package_result(UPDATE_FAILURE_VERSION, result.status);
    /* 检测fimrware各个文件.bin是否属性匹配manifest */
    result = validate_file_set(root, &package->manifest);
    if (FirmwareStatus_IsError(result.status))
        return result;
    if (verify_payload_hashes != 0)
    {
        size_t index;
        for (index = 0U; index < package->manifest.component_count; ++index)
        {
            result = verify_payload(root, &package->manifest.components[index]);
            if (FirmwareStatus_IsError(result.status))
                return result;
        }
    }
    LOG_INFO("update", "package validation pass: package=%s version=%lu.%lu.%lu components=%lu",
             package->manifest.package_id, (unsigned long) package->manifest.release.major,
             (unsigned long) package->manifest.release.minor,
             (unsigned long) package->manifest.release.patch,
             (unsigned long) package->manifest.component_count);
    return package_result(UPDATE_FAILURE_NONE, FIRMWARE_STATUS_OK);
}

firmware_status_t PackageReader_ValidateUpdateRoot(void)
{
    platform_dir_handle_t directory;
    platform_dir_entry_t entry;
    uint32_t found           = 0U;
    firmware_status_t status = PlatformStorage_DirOpen(UPDATE_ROOT, &directory);
    if (FirmwareStatus_IsError(status))
        return status;
    while ((status = PlatformStorage_DirRead(directory, &entry)) == FIRMWARE_STATUS_OK)
    {
        if (strcmp(entry.name, "firmware") == 0 && entry.is_directory != 0U)
            found |= 1U;
        else if (strcmp(entry.name, "boot_update_request.json") == 0 && entry.is_directory == 0U)
            found |= 2U;
        else
        {
            (void) PlatformStorage_DirClose(directory);
            return FIRMWARE_STATUS_INVALID_STATE;
        }
    }
    {
        firmware_status_t close_status = PlatformStorage_DirClose(directory);
        if (status != FIRMWARE_STATUS_NOT_FOUND)
            return status;
        if (FirmwareStatus_IsError(close_status))
            return close_status;
    }
    return found == 3U ? FIRMWARE_STATUS_OK : FIRMWARE_STATUS_NOT_FOUND;
}

update_operation_result_t PackageReader_ValidateRequest(const char *root,
                                                        const update_request_t *request,
                                                        const uint8_t *expected_manifest_digest,
                                                        int verify_payload_hashes,
                                                        update_package_t *package)
{
    update_operation_result_t result;
    uint8_t request_digest[32];
    if (request == NULL || request->requested == 0U ||
        !UpdateHex_DecodeSha256(request->manifest_sha256, request_digest))
        return package_result(UPDATE_FAILURE_REQUEST_PARSE, FIRMWARE_STATUS_INVALID_ARGUMENT);
    if (expected_manifest_digest != NULL &&
        memcmp(request_digest, expected_manifest_digest, sizeof(request_digest)) != 0)
        return package_result(UPDATE_FAILURE_MANIFEST_DIGEST,
                              FIRMWARE_STATUS_AUTHENTICATION_FAILED);
    result = PackageReader_Validate(root, request_digest, verify_payload_hashes, package);
    if (FirmwareStatus_IsError(result.status))
        return result;
    if (strcmp(request->package_id, package->manifest.package_id) != 0)
        return package_result(UPDATE_FAILURE_REQUEST_PACKAGE_ID_MISMATCH,
                              FIRMWARE_STATUS_AUTHENTICATION_FAILED);
    if (request->component_mask != package->manifest.component_mask)
        return package_result(UPDATE_FAILURE_REQUEST_COMPONENT_MASK_MISMATCH,
                              FIRMWARE_STATUS_AUTHENTICATION_FAILED);
    return result;
}
