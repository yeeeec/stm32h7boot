/**
 * @file fatfs_package_source_adapter.c
 * @brief Package-source operations backed by the CubeMX SD-card FatFs volume.
 */
#include "adapters/fatfs_package_source_adapter.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "bsp_driver_sd.h"
#include "fatfs.h"
#include "logging.h"

#define FATFS_PACKAGE_SOURCE_PATH_BUFFER_SIZE (_MAX_LFN + 16U)

static firmware_status_t FatFsStatus(FRESULT result)
{
    if (result == FR_OK)
    {
        return FIRMWARE_STATUS_OK;
    }
    if ((result == FR_INVALID_OBJECT) || (result == FR_NOT_ENABLED) ||
        (result == FR_NOT_READY))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (result == FR_INVALID_PARAMETER)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    return FIRMWARE_STATUS_IO_ERROR;
}

/* Prefix the generated SD drive so fixed root-relative paths cannot land on another volume. */
static firmware_status_t BuildSdPath(const char *path, char *full_path, size_t full_path_size)
{
    const char *relative_path;
    size_t path_length;
    size_t relative_path_length;
    size_t volume_path_length;

    if ((path == NULL) || (full_path == NULL) || (full_path_size == 0U))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    path_length = strlen(path);
    if (path_length == 0U)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((path_length >= 2U) && (path[1] == ':'))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    relative_path = (path[0] == '/') ? &path[1] : path;
    if (relative_path[0] == '\0')
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    volume_path_length = strlen(SDPath);
    relative_path_length = strlen(relative_path);
    if (volume_path_length == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((volume_path_length + relative_path_length + 1U) > full_path_size)
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }

    memcpy(full_path, SDPath, volume_path_length);
    memcpy(&full_path[volume_path_length], relative_path, relative_path_length + 1U);
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t IsMediaPresent(void *context, int *present)
{
    if ((context == NULL) || (present == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    *present = (BSP_SD_IsDetected() == SD_PRESENT) ? 1 : 0;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t Mount(void *context)
{
    fatfs_package_source_adapter_t *adapter =
        (fatfs_package_source_adapter_t *)context;
    int present;
    firmware_status_t status;

    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->mounted != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (SDPath[0] == '\0')
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = IsMediaPresent(adapter, &present);
    if (!FirmwareStatus_IsOk(status) || (present == 0))
    {
        return FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE
                                            : status;
    }
    {
        FRESULT mount_result = f_mount(&SDFatFS, SDPath, 1U);

        status = FatFsStatus(mount_result);
        if (!FirmwareStatus_IsOk(status))
        {
            /* Firmware status is coarse; retain the FatFs code on UART. */
            LOG_WARN("sd", "FatFs mount failed: fresult=%d status=%d",
                     (int)mount_result, (int)status);
        }
    }
    if (FirmwareStatus_IsOk(status))
    {
        adapter->mounted = 1;
    }
    return status;
}

static firmware_status_t Unmount(void *context)
{
    fatfs_package_source_adapter_t *adapter =
        (fatfs_package_source_adapter_t *)context;
    firmware_status_t status;

    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((adapter->mounted == 0) || (adapter->file_open != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (SDPath[0] == '\0')
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = FatFsStatus(f_mount(NULL, SDPath, 0U));
    if (FirmwareStatus_IsOk(status))
    {
        adapter->mounted = 0;
    }
    return status;
}

static firmware_status_t Open(void *context, const char *path)
{
    fatfs_package_source_adapter_t *adapter =
        (fatfs_package_source_adapter_t *)context;
    firmware_status_t status;
    char full_path[FATFS_PACKAGE_SOURCE_PATH_BUFFER_SIZE];

    if ((adapter == NULL) || (path == NULL) || (path[0] == '\0'))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((adapter->mounted == 0) || (adapter->file_open != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = BuildSdPath(path, full_path, sizeof(full_path));
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = FatFsStatus(f_open(&SDFile, full_path, FA_READ | FA_OPEN_EXISTING));
    if (FirmwareStatus_IsOk(status))
    {
        adapter->file_open = 1;
    }
    return status;
}

static firmware_status_t Close(void *context)
{
    fatfs_package_source_adapter_t *adapter =
        (fatfs_package_source_adapter_t *)context;
    firmware_status_t status;

    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->file_open == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = FatFsStatus(f_close(&SDFile));
    if (FirmwareStatus_IsOk(status))
    {
        adapter->file_open = 0;
    }
    return status;
}

static firmware_status_t Exists(void *context, const char *path, int *present)
{
    fatfs_package_source_adapter_t *adapter =
        (fatfs_package_source_adapter_t *)context;
    FILINFO file_info;
    firmware_status_t status;
    FRESULT result;
    char full_path[FATFS_PACKAGE_SOURCE_PATH_BUFFER_SIZE];

    if ((adapter == NULL) || (path == NULL) || (present == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((adapter->mounted == 0) || (adapter->file_open != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = BuildSdPath(path, full_path, sizeof(full_path));
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    result = f_stat(full_path, &file_info);
    if (result == FR_OK)
    {
        *present = 1;
        return FIRMWARE_STATUS_OK;
    }
    if (result == FR_NO_FILE || result == FR_NO_PATH)
    {
        *present = 0;
        return FIRMWARE_STATUS_OK;
    }
    return FatFsStatus(result);
}

static firmware_status_t Remove(void *context, const char *path)
{
    fatfs_package_source_adapter_t *adapter =
        (fatfs_package_source_adapter_t *)context;
    firmware_status_t status;
    char full_path[FATFS_PACKAGE_SOURCE_PATH_BUFFER_SIZE];

    if ((adapter == NULL) || (path == NULL) || (path[0] == '\0'))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((adapter->mounted == 0) || (adapter->file_open != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = BuildSdPath(path, full_path, sizeof(full_path));
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    return FatFsStatus(f_unlink(full_path));
}

static firmware_status_t GetSize(void *context, uint32_t *size)
{
    const fatfs_package_source_adapter_t *adapter =
        (const fatfs_package_source_adapter_t *)context;
    FSIZE_t file_size;

    if ((adapter == NULL) || (size == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->file_open == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    file_size = f_size(&SDFile);
    if (file_size > UINT32_MAX)
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    *size = (uint32_t)file_size;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ReadAt(
    void *context,
    uint32_t offset,
    void *data,
    uint32_t size,
    uint32_t *bytes_read)
{
    fatfs_package_source_adapter_t *adapter =
        (fatfs_package_source_adapter_t *)context;
    UINT read_count;
    FRESULT result;

    if ((adapter == NULL) || (data == NULL) || (bytes_read == NULL) ||
        (size == 0U) || (size > UINT_MAX))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (adapter->file_open == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    result = f_lseek(&SDFile, (FSIZE_t)offset);
    if (result != FR_OK)
    {
        return FatFsStatus(result);
    }
    result = f_read(&SDFile, data, (UINT)size, &read_count);
    *bytes_read = (uint32_t)read_count;
    return FatFsStatus(result);
}

firmware_status_t FatFsPackageSourceAdapter_Init(
    fatfs_package_source_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    adapter->mounted = 0;
    adapter->file_open = 0;
    adapter->interface.context = adapter;
    adapter->interface.is_media_present = IsMediaPresent;
    adapter->interface.mount = Mount;
    adapter->interface.unmount = Unmount;
    adapter->interface.open = Open;
    adapter->interface.close = Close;
    adapter->interface.exists = Exists;
    adapter->interface.remove = Remove;
    adapter->interface.get_size = GetSize;
    adapter->interface.read_at = ReadAt;
    return FIRMWARE_STATUS_OK;
}

const package_source_t *FatFsPackageSourceAdapter_Interface(
    const fatfs_package_source_adapter_t *adapter)
{
    return (adapter == NULL) ? NULL : &adapter->interface;
}
