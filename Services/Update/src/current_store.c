#include "update_internal.h"

#include <stdio.h>
#include <string.h>

#include "platform/platform_storage.h"
#include "platform/platform_system.h"
#include "update_config.h"

static uint8_t s_copy_buffer[UPDATE_IO_BLOCK_SIZE];

static int path_exists(const char *path)
{
    platform_file_info_t info;
    return PlatformStorage_Stat(path, &info) == FIRMWARE_STATUS_OK;
}

static firmware_status_t ensure_directory(const char *path)
{
    platform_file_info_t info;
    firmware_status_t status = PlatformStorage_Stat(path, &info);

    if (status == FIRMWARE_STATUS_OK)
        return info.is_directory != 0U ? FIRMWARE_STATUS_OK
                                       : FIRMWARE_STATUS_INVALID_STATE;
    if (status != FIRMWARE_STATUS_NOT_FOUND)
        return status;
    return PlatformStorage_Mkdir(path);
}

static firmware_status_t remove_tree_if_present(const char *path)
{
    return path_exists(path) ? PlatformStorage_RemoveTree(path) : FIRMWARE_STATUS_OK;
}

static firmware_status_t copy_file(const char *source, const char *destination)
{
    platform_file_info_t info;
    platform_file_handle_t input = 0U;
    platform_file_handle_t output = 0U;
    uint32_t total = 0U;
    int output_open = 0;
    firmware_status_t status;

    status = PlatformStorage_Stat(source, &info);
    if (FirmwareStatus_IsError(status) || info.is_directory != 0U)
        return FirmwareStatus_IsError(status) ? status
                                              : FIRMWARE_STATUS_INVALID_STATE;
    status = PlatformStorage_OpenRead(source, &input);
    if (FirmwareStatus_IsError(status))
        return status;
    status = PlatformStorage_OpenWrite(destination, &output);
    if (FirmwareStatus_IsError(status))
    {
        (void) PlatformStorage_Close(input);
        return status;
    }
    output_open = 1;

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
    if (output_open != 0)
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
    update_operation_result_t result = PackageReader_Validate(
        root, NULL, 1, package != NULL ? package : &local);
    return result.status;
}

firmware_status_t CurrentStore_Verify(void)
{
    return verify_root(CURRENT_PACKAGE_ROOT, NULL);
}

firmware_status_t CurrentStore_Read(update_package_t *package)
{
    if (package == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return verify_root(CURRENT_PACKAGE_ROOT, package);
}

firmware_status_t CurrentStore_Commit(const update_package_t *package)
{
    const update_manifest_t *manifest;
    char source[UPDATE_PATH_MAX];
    char destination[UPDATE_PATH_MAX];
    update_package_t staged;
    firmware_status_t status;
    size_t index;
    int previous_created = 0;

    if (package == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    manifest = &package->manifest;

    status = remove_tree_if_present(CURRENT_NEW_ROOT);
    if (FirmwareStatus_IsOk(status))
        status = ensure_directory(CURRENT_NEW_ROOT);
    if (FirmwareStatus_IsOk(status))
        status = ensure_directory(CURRENT_NEW_PACKAGE_ROOT);
    if (FirmwareStatus_IsError(status))
        return status;

    if (snprintf(source, sizeof(source), "%s/%s", package->root,
                 UPDATE_MANIFEST_FILE) <= 0 ||
        snprintf(destination, sizeof(destination), "%s/%s",
                 CURRENT_NEW_PACKAGE_ROOT, UPDATE_MANIFEST_FILE) <= 0)
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    status = copy_file(source, destination);
    for (index = 0U; FirmwareStatus_IsOk(status) &&
                     index < manifest->component_count; ++index)
    {
        if (snprintf(source, sizeof(source), "%s/%s", package->root,
                     manifest->components[index].file) <= 0 ||
            snprintf(destination, sizeof(destination), "%s/%s",
                     CURRENT_NEW_PACKAGE_ROOT,
                     manifest->components[index].file) <= 0)
            status = FIRMWARE_STATUS_BUFFER_TOO_SMALL;
        else
            status = copy_file(source, destination);
    }
    if (FirmwareStatus_IsError(status))
    {
        (void) remove_tree_if_present(CURRENT_NEW_ROOT);
        return status;
    }

    status = verify_root(CURRENT_NEW_PACKAGE_ROOT, &staged);
    if (FirmwareStatus_IsOk(status) &&
        memcmp(staged.raw_manifest_sha256, package->raw_manifest_sha256,
               sizeof(staged.raw_manifest_sha256)) != 0)
        status = FIRMWARE_STATUS_AUTHENTICATION_FAILED;
    if (FirmwareStatus_IsError(status))
    {
        (void) remove_tree_if_present(CURRENT_NEW_ROOT);
        return status;
    }

    status = remove_tree_if_present(CURRENT_PREVIOUS_ROOT);
    if (FirmwareStatus_IsError(status))
        return status;
    if (path_exists(CURRENT_ROOT))
    {
        status = PlatformStorage_Rename(CURRENT_ROOT, CURRENT_PREVIOUS_ROOT);
        if (FirmwareStatus_IsError(status))
            return status;
        previous_created = 1;
    }
    status = PlatformStorage_Rename(CURRENT_NEW_ROOT, CURRENT_ROOT);
    if (FirmwareStatus_IsError(status))
    {
        if (previous_created != 0)
            (void) PlatformStorage_Rename(CURRENT_PREVIOUS_ROOT, CURRENT_ROOT);
        return status;
    }

    status = CurrentStore_Verify();
    if (FirmwareStatus_IsError(status))
    {
        (void) remove_tree_if_present(CURRENT_ROOT);
        if (previous_created != 0)
            (void) PlatformStorage_Rename(CURRENT_PREVIOUS_ROOT, CURRENT_ROOT);
        return status;
    }
    return remove_tree_if_present(CURRENT_PREVIOUS_ROOT);
}

firmware_status_t CurrentStore_Reconcile(void)
{
    firmware_status_t current_status = verify_root(CURRENT_PACKAGE_ROOT, NULL);
    firmware_status_t candidate_status;

    if (FirmwareStatus_IsOk(current_status))
    {
        (void) remove_tree_if_present(CURRENT_NEW_ROOT);
        (void) remove_tree_if_present(CURRENT_PREVIOUS_ROOT);
        return FIRMWARE_STATUS_OK;
    }

    candidate_status = verify_root(CURRENT_NEW_PACKAGE_ROOT, NULL);
    if (FirmwareStatus_IsOk(candidate_status))
    {
        (void) remove_tree_if_present(CURRENT_ROOT);
        candidate_status = PlatformStorage_Rename(CURRENT_NEW_ROOT, CURRENT_ROOT);
        if (FirmwareStatus_IsOk(candidate_status))
            candidate_status = CurrentStore_Verify();
        if (FirmwareStatus_IsOk(candidate_status))
        {
            (void) remove_tree_if_present(CURRENT_PREVIOUS_ROOT);
            return FIRMWARE_STATUS_OK;
        }
    }
    (void) remove_tree_if_present(CURRENT_NEW_ROOT);

    candidate_status = verify_root(CURRENT_PREVIOUS_PACKAGE_ROOT, NULL);
    if (FirmwareStatus_IsOk(candidate_status))
    {
        (void) remove_tree_if_present(CURRENT_ROOT);
        candidate_status = PlatformStorage_Rename(CURRENT_PREVIOUS_ROOT, CURRENT_ROOT);
        if (FirmwareStatus_IsOk(candidate_status))
            candidate_status = CurrentStore_Verify();
        if (FirmwareStatus_IsOk(candidate_status))
            return FIRMWARE_STATUS_OK;
    }
    return current_status;
}

firmware_status_t CurrentStore_Restore(void)
{
    update_package_t package;
    size_t index;
    firmware_status_t status = CurrentStore_Read(&package);

    if (FirmwareStatus_IsError(status))
        return status;
    for (index = 0U; index < package.manifest.component_count; ++index)
    {
        update_operation_result_t result = ImageInstaller_Install(
            package.root, &package.manifest.components[index]);
        if (FirmwareStatus_IsError(result.status))
            return result.status;
    }
    return FIRMWARE_STATUS_OK;
}
