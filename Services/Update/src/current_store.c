#include "update_internal.h"

#include <stdio.h>
#include <string.h>

#include "firmware/memory.h"
#include "platform/platform_storage.h"
#include "platform/platform_system.h"
#include "update_config.h"

static FIRMWARE_STORAGE_RAM uint8_t s_copy_buffer[UPDATE_IO_BLOCK_SIZE];

static firmware_status_t remove_tree_if_present(const char *path)
{
    platform_file_info_t info;
    firmware_status_t status = PlatformStorage_Stat(path, &info);
    if (status == FIRMWARE_STATUS_NOT_FOUND)
        return FIRMWARE_STATUS_OK;
    if (FirmwareStatus_IsError(status))
        return status;
    return PlatformStorage_RemoveTree(path);
}

static firmware_status_t copy_file(const char *source, const char *destination)
{
    platform_file_info_t info;
    platform_file_handle_t input  = 0U;
    platform_file_handle_t output = 0U;
    uint32_t total                = 0U;
    firmware_status_t status      = PlatformStorage_Stat(source, &info);

    if (FirmwareStatus_IsError(status) || info.is_directory != 0U)
        return FirmwareStatus_IsError(status) ? status : FIRMWARE_STATUS_INVALID_STATE;
    status = PlatformStorage_OpenRead(source, &input);
    if (FirmwareStatus_IsError(status))
        return status;
    status = PlatformStorage_OpenWrite(destination, &output);
    if (FirmwareStatus_IsError(status))
    {
        (void) PlatformStorage_Close(input);
        return status;
    }
    while (total < info.size)
    {
        size_t requested = info.size - total;
        size_t actual    = 0U;
        size_t written   = 0U;
        if (requested > sizeof(s_copy_buffer))
            requested = sizeof(s_copy_buffer);
        status = PlatformStorage_Read(input, s_copy_buffer, requested, &actual);
        if (FirmwareStatus_IsError(status) || actual != requested)
        {
            if (FirmwareStatus_IsOk(status))
                status = FIRMWARE_STATUS_IO_ERROR;
            break;
        }
        status = PlatformStorage_Write(output, s_copy_buffer, actual, &written);
        if (FirmwareStatus_IsError(status) || written != actual)
        {
            if (FirmwareStatus_IsOk(status))
                status = FIRMWARE_STATUS_IO_ERROR;
            break;
        }
        total += (uint32_t) actual;
        PlatformSystem_WatchdogRefresh();
    }
    if (FirmwareStatus_IsOk(status))
        status = PlatformStorage_Sync(output);
    {
        firmware_status_t close_status = PlatformStorage_Close(input);
        if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(close_status))
            status = close_status;
    }
    {
        firmware_status_t close_status = PlatformStorage_Close(output);
        if (FirmwareStatus_IsOk(status) && FirmwareStatus_IsError(close_status))
            status = close_status;
    }
    return status;
}

static firmware_status_t verify_root(const char *root, update_package_t *package)
{
    update_package_t local;
    update_operation_result_t result =
        PackageReader_Validate(root, NULL, 1, package != NULL ? package : &local);
    return result.status;
}

static firmware_status_t verify_current_root(void)
{
    platform_dir_handle_t directory;
    platform_dir_entry_t entry;
    firmware_status_t status = PlatformStorage_DirOpen(CURRENT_ROOT, &directory);
    if (FirmwareStatus_IsError(status))
        return status;
    while ((status = PlatformStorage_DirRead(directory, &entry)) == FIRMWARE_STATUS_OK)
    {
        if (strcmp(entry.name, "firmware") != 0 || entry.is_directory == 0U)
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
    return FIRMWARE_STATUS_OK;
}

firmware_status_t CurrentStore_Verify(void)
{
    firmware_status_t status = verify_current_root();
    return FirmwareStatus_IsError(status) ? status : verify_root(CURRENT_PACKAGE_ROOT, NULL);
}

firmware_status_t CurrentStore_Read(update_package_t *package)
{
    if (package == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    {
        firmware_status_t status = verify_current_root();
        return FirmwareStatus_IsError(status) ? status : verify_root(CURRENT_PACKAGE_ROOT, package);
    }
}

firmware_status_t CurrentStore_Commit(const update_package_t *package,
                                      const uint8_t expected_manifest_sha256[32])
{
    char source[UPDATE_PATH_MAX];
    char destination[UPDATE_PATH_MAX];
    update_package_t verified;
    firmware_status_t status;
    size_t index;

    if (package == NULL || expected_manifest_sha256 == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (memcmp(package->manifest_sha256, expected_manifest_sha256, 32U) != 0)
        return FIRMWARE_STATUS_AUTHENTICATION_FAILED;

    status = remove_tree_if_present(CURRENT_ROOT);
    if (FirmwareStatus_IsOk(status))
        status = PlatformStorage_Mkdir(CURRENT_ROOT);
    if (FirmwareStatus_IsOk(status))
        status = PlatformStorage_Mkdir(CURRENT_PACKAGE_ROOT);
    if (FirmwareStatus_IsError(status))
        return status;

    {
        int source_length =
            snprintf(source, sizeof(source), "%s/%s", package->root, UPDATE_MANIFEST_FILE);
        int destination_length = snprintf(destination, sizeof(destination), "%s/%s",
                                          CURRENT_PACKAGE_ROOT, UPDATE_MANIFEST_FILE);
        if (source_length <= 0 || (size_t) source_length >= sizeof(source) ||
            destination_length <= 0 || (size_t) destination_length >= sizeof(destination))
            return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    }
    status = copy_file(source, destination);
    for (index = 0U; FirmwareStatus_IsOk(status) && index < package->manifest.component_count;
         ++index)
    {
        const update_component_descriptor_t *descriptor =
            UpdateComponent_Find(package->manifest.components[index].name);
        int source_length = snprintf(source, sizeof(source), "%s/%s", package->root,
                                     package->manifest.components[index].file);
        int destination_length =
            snprintf(destination, sizeof(destination), "%s/%s", CURRENT_PACKAGE_ROOT,
                     package->manifest.components[index].file);
        if (descriptor == NULL || !UpdateComponent_IsEnabled(descriptor))
            continue;
        if (source_length <= 0 || (size_t) source_length >= sizeof(source) ||
            destination_length <= 0 || (size_t) destination_length >= sizeof(destination))
            status = FIRMWARE_STATUS_BUFFER_TOO_SMALL;
        else
            status = copy_file(source, destination);
    }
    if (FirmwareStatus_IsError(status))
        return status;
    status = verify_root(CURRENT_PACKAGE_ROOT, &verified);
    if (FirmwareStatus_IsOk(status) &&
        memcmp(verified.manifest_sha256, expected_manifest_sha256, 32U) != 0)
        status = FIRMWARE_STATUS_AUTHENTICATION_FAILED;
    return status;
}

firmware_status_t CurrentStore_CleanupUpdate(void)
{
    return remove_tree_if_present(UPDATE_PACKAGE_ROOT);
}
