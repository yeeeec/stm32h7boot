/**
 * @file fatfs_package_source_adapter.c
 * @brief Package-source operations backed by the CubeMX USB FatFs volume.
 */
#include "adapters/fatfs_package_source_adapter.h"

#include <limits.h>
#include <stddef.h>

#include "fatfs.h"
#include "usb_host.h"

extern ApplicationTypeDef Appli_state;

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

static firmware_status_t IsMediaPresent(void *context, int *present)
{
    if ((context == NULL) || (present == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    *present = (Appli_state == APPLICATION_READY) ? 1 : 0;
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
    status = IsMediaPresent(adapter, &present);
    if (!FirmwareStatus_IsOk(status) || (present == 0))
    {
        return FirmwareStatus_IsOk(status) ? FIRMWARE_STATUS_INVALID_STATE
                                            : status;
    }
    status = FatFsStatus(f_mount(&USBHFatFS, USBHPath, 1U));
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
    status = FatFsStatus(f_mount(NULL, USBHPath, 0U));
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

    if ((adapter == NULL) || (path == NULL) || (path[0] == '\0'))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((adapter->mounted == 0) || (adapter->file_open != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = FatFsStatus(f_open(&USBHFile, path, FA_READ | FA_OPEN_EXISTING));
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
    status = FatFsStatus(f_close(&USBHFile));
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
    FIL file;
    FRESULT result;

    if ((adapter == NULL) || (path == NULL) || (present == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((adapter->mounted == 0) || (adapter->file_open != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    result = f_open(&file, path, FA_READ | FA_OPEN_EXISTING);
    if (result == FR_OK)
    {
        (void)f_close(&file);
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

    if ((adapter == NULL) || (path == NULL) || (path[0] == '\0'))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((adapter->mounted == 0) || (adapter->file_open != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FatFsStatus(f_unlink(path));
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
    file_size = f_size(&USBHFile);
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

    result = f_lseek(&USBHFile, (FSIZE_t)offset);
    if (result != FR_OK)
    {
        return FatFsStatus(result);
    }
    result = f_read(&USBHFile, data, (UINT)size, &read_count);
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
