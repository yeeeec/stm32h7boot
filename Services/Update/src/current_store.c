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

static firmware_status_t ensure_directory(const char *path)
{
    platform_file_info_t info;
    firmware_status_t status = PlatformStorage_Stat(path, &info);

    if (status == FIRMWARE_STATUS_NOT_FOUND)
        return PlatformStorage_Mkdir(path);
    if (FirmwareStatus_IsError(status))
        return status;
    return info.is_directory != 0U ? FIRMWARE_STATUS_OK : FIRMWARE_STATUS_INVALID_STATE;
}

static firmware_status_t copy_file(const char *source, const char *destination)
{
    platform_file_info_t info;
    platform_file_handle_t input = 0U;
    platform_file_handle_t output = 0U;
    uint32_t total = 0U;
    firmware_status_t status = PlatformStorage_Stat(source, &info);

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
        size_t actual = 0U;
        size_t written = 0U;
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

static firmware_status_t verify_package(const char *root, update_package_t *package)
{
    update_package_t local;
    update_operation_result_t result =
        PackageReader_Validate(root, NULL, 1, package != NULL ? package : &local);
    return result.status;
}

static firmware_status_t copy_package(const char *source_root, const update_package_t *package,
                                      const char *destination_root)
{
    char source[UPDATE_PATH_MAX];
    char destination[UPDATE_PATH_MAX];
    firmware_status_t status;
    size_t index;

    status = remove_tree_if_present(destination_root);
    if (FirmwareStatus_IsOk(status))
        status = PlatformStorage_Mkdir(destination_root);
    if (FirmwareStatus_IsError(status))
        return status;

    {
        int source_length = snprintf(source, sizeof(source), "%s/%s", source_root,
                                     UPDATE_MANIFEST_FILE);
        int destination_length = snprintf(destination, sizeof(destination), "%s/%s",
                                          destination_root, UPDATE_MANIFEST_FILE);
        if (source_length <= 0 || (size_t) source_length >= sizeof(source) ||
            destination_length <= 0 || (size_t) destination_length >= sizeof(destination))
            return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    }
    status = copy_file(source, destination);
    for (index = 0U; FirmwareStatus_IsOk(status) && index < package->manifest.component_count;
         ++index)
    {
        const update_manifest_component_t *component = &package->manifest.components[index];
        int source_length = snprintf(source, sizeof(source), "%s/%s", source_root, component->file);
        int destination_length =
            snprintf(destination, sizeof(destination), "%s/%s", destination_root, component->file);
        if (source_length <= 0 || (size_t) source_length >= sizeof(source) ||
            destination_length <= 0 || (size_t) destination_length >= sizeof(destination))
            status = FIRMWARE_STATUS_BUFFER_TOO_SMALL;
        else
            status = copy_file(source, destination);
    }
    return status;
}

static firmware_status_t replace_snapshot(const char *destination_root, const char *part_root,
                                          const char *source_root, const update_package_t *package)
{
    update_package_t verified;
    firmware_status_t status = copy_package(source_root, package, part_root);

    if (FirmwareStatus_IsError(status))
        return status;
    status = PlatformStorage_SyncVolume();
    if (FirmwareStatus_IsError(status))
        return status;
    status = verify_package(part_root, &verified);
    if (FirmwareStatus_IsError(status))
        return status;
    status = remove_tree_if_present(destination_root);
    if (FirmwareStatus_IsOk(status))
        status = PlatformStorage_Rename(part_root, destination_root);
    if (FirmwareStatus_IsOk(status))
        status = PlatformStorage_SyncVolume();
    return status;
}

firmware_status_t CurrentStore_Verify(void)
{
    return verify_package(CURRENT_PACKAGE_ROOT, NULL);
}

firmware_status_t CurrentStore_VerifyLast(void)
{
    return verify_package(LAST_PACKAGE_ROOT, NULL);
}

firmware_status_t CurrentStore_Read(update_package_t *package)
{
    if (package == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return verify_package(CURRENT_PACKAGE_ROOT, package);
}

firmware_status_t CurrentStore_SaveLast(void)
{
    update_package_t current;
    firmware_status_t status = ensure_directory(LAST_ROOT);

    if (FirmwareStatus_IsError(status))
        return status;
    status = CurrentStore_Read(&current);
    if (FirmwareStatus_IsError(status))
        return status;
    return replace_snapshot(LAST_PACKAGE_ROOT, LAST_PACKAGE_PART, CURRENT_PACKAGE_ROOT, &current);
}

firmware_status_t CurrentStore_RebuildFrom(const char *source_root)
{
    update_package_t source;
    firmware_status_t status;

    if (source_root == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = ensure_directory(CURRENT_ROOT);
    if (FirmwareStatus_IsError(status))
        return status;
    status = verify_package(source_root, &source);
    if (FirmwareStatus_IsError(status))
        return status;
    return replace_snapshot(CURRENT_PACKAGE_ROOT, CURRENT_PACKAGE_PART, source_root, &source);
}

firmware_status_t CurrentStore_CleanupUpdate(void)
{
    return remove_tree_if_present(UPDATE_PACKAGE_ROOT);
}
