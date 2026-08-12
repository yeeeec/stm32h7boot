/**
 * @file fatfs_update_request_store_adapter.c
 * @brief 基于 CubeMX SD FatFs 卷的原始固定 trusted request 存储。
 *
 * Request Store 与 Package Source 共享卷状态，并串行复用 CubeMX 的 SDFile，
 * 从而在同步读取中的 close 失败后仍可由后续 unmount 重试关闭。
 */
#include "adapters/fatfs_update_request_store_adapter.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "fatfs.h"

/** trusted request 路径转换所需的临时缓冲区大小。 */
#define FATFS_REQUEST_PATH_BUFFER_SIZE (_MAX_LFN + 16U)
/** 卷内固定 trusted request 文件的逻辑路径。 */
#define FATFS_REQUEST_PATH "/boot_update_request.json"

/** 将 request 文件相关的 FatFs 返回码映射为固件层状态。 */
static firmware_status_t FatFsRequestStatus(FRESULT result)
{
    if (result == FR_OK)
    {
        return FIRMWARE_STATUS_OK;
    }
    if ((result == FR_NO_FILE) || (result == FR_NO_PATH))
    {
        return FIRMWARE_STATUS_NOT_FOUND;
    }
    if ((result == FR_INVALID_OBJECT) || (result == FR_NOT_ENABLED) || (result == FR_NOT_READY))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (result == FR_INVALID_PARAMETER)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    return FIRMWARE_STATUS_IO_ERROR;
}

/** 拼接共享卷中的固定 trusted request 文件路径。 */
static firmware_status_t BuildRequestPath(char *full_path, size_t full_path_size)
{
    const size_t volume_path_length  = strlen(SDPath);
    const size_t request_path_length = strlen(FATFS_REQUEST_PATH + 1U);

    if ((full_path == NULL) || (full_path_size == 0U) || (volume_path_length == 0U))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((volume_path_length + request_path_length + 1U) > full_path_size)
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    memcpy(full_path, SDPath, volume_path_length);
    memcpy(&full_path[volume_path_length], FATFS_REQUEST_PATH + 1U, request_path_length + 1U);
    return FIRMWARE_STATUS_OK;
}

/**
 * @brief 关闭共享上下文中的 request 文件并合并主操作结果。
 *
 * close 失败优先返回，因为它意味着卷仍被占用；无论主读取是否失败，
 * request_file_open 只有在底层 f_close 成功后才清零。
 */
static firmware_status_t CloseRequestFile(fatfs_update_request_store_adapter_t *adapter,
                                          firmware_status_t status)
{
    firmware_status_t close_status;

    if ((adapter == NULL) || (adapter->volume == NULL) || (adapter->volume->request_file_open == 0))
    {
        return status;
    }
    close_status = FatFsRequestStatus(f_close(&SDFile));
    if (FirmwareStatus_IsOk(close_status))
    {
        adapter->volume->request_file_open = 0;
        return status;
    }

    return close_status;
}

/** 读取完整 trusted request 文件，并在返回前关闭共享文件句柄。 */
static firmware_status_t LoadRaw(void *context, uint8_t *buffer, uint32_t capacity, uint32_t *size)
{
    fatfs_update_request_store_adapter_t *adapter =
        (fatfs_update_request_store_adapter_t *) context;
    char full_path[FATFS_REQUEST_PATH_BUFFER_SIZE];
    FSIZE_t request_size;
    UINT bytes_read;
    firmware_status_t status;
    FRESULT result;

    if ((adapter == NULL) || (adapter->volume == NULL) || (buffer == NULL) || (size == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((adapter->volume->mounted == 0) || (adapter->volume->package_file_open != 0) ||
        (adapter->volume->request_file_open != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = BuildRequestPath(full_path, sizeof(full_path));
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    /* request 文件与发布包文件串行复用同一个 CubeMX SDFile。 */
    result = f_open(&SDFile, full_path, FA_READ | FA_OPEN_EXISTING);
    status = FatFsRequestStatus(result);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    adapter->volume->request_file_open = 1;

    request_size = f_size(&SDFile);
    if ((request_size > UINT32_MAX) || (request_size > UPDATE_REQUEST_STORE_MAX_RAW_SIZE) ||
        (request_size > capacity) || (request_size > UINT_MAX))
    {
        return CloseRequestFile(adapter, FIRMWARE_STATUS_BUFFER_TOO_SMALL);
    }
    if (request_size == 0U)
    {
        *size = 0U;
        return CloseRequestFile(adapter, FIRMWARE_STATUS_OK);
    }

    result = f_read(&SDFile, buffer, (UINT) request_size, &bytes_read);
    status = FatFsRequestStatus(result);
    if (FirmwareStatus_IsOk(status) && (bytes_read != (UINT) request_size))
    {
        status = FIRMWARE_STATUS_IO_ERROR;
    }
    if (FirmwareStatus_IsOk(status))
    {
        *size = (uint32_t) request_size;
    }
    return CloseRequestFile(adapter, status);
}

/** 删除 trusted request 文件；调用前必须没有其他文件处于打开状态。 */
static firmware_status_t Clear(void *context)
{
    fatfs_update_request_store_adapter_t *adapter =
        (fatfs_update_request_store_adapter_t *) context;
    char full_path[FATFS_REQUEST_PATH_BUFFER_SIZE];
    firmware_status_t status;

    if ((adapter == NULL) || (adapter->volume == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((adapter->volume->mounted == 0) || (adapter->volume->package_file_open != 0) ||
        (adapter->volume->request_file_open != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    status = BuildRequestPath(full_path, sizeof(full_path));
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    return FatFsRequestStatus(f_unlink(full_path));
}

firmware_status_t FatFsUpdateRequestStoreAdapter_Init(fatfs_update_request_store_adapter_t *adapter,
                                                      fatfs_release_volume_context_t *volume)
{
    if ((adapter == NULL) || (volume == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    /* 保存共享卷引用，所有权仍由调用者管理。 */
    adapter->volume             = volume;
    adapter->interface.context  = adapter;
    adapter->interface.load_raw = LoadRaw;
    adapter->interface.clear    = Clear;
    return FIRMWARE_STATUS_OK;
}

const update_request_store_t *
FatFsUpdateRequestStoreAdapter_Interface(const fatfs_update_request_store_adapter_t *adapter)
{
    /* 返回适配器内嵌的 request 存储接口。 */
    return (adapter == NULL) ? NULL : &adapter->interface;
}
