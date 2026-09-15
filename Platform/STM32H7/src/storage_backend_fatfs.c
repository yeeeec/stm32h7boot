/**
 * @file storage_backend_fatfs.c
 * @brief SD/USB 共用的通用 FatFs backend。
 *
 * backend 只管理 FatFs 挂载、文件/目录句柄和路径转换，不拥有具体媒体的
 * 插拔检测或初始化生命周期；这些职责由 platform_storage.c 与对应任务负责。
 */

#include "storage_backend_fatfs.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "storage/storage_common.h"

/** 将 FatFs 错误码映射为项目统一固件状态。 */
static firmware_status_t MapResult(FRESULT result)
{
    switch (result)
    {
        case FR_OK:
            return FIRMWARE_STATUS_OK;
        case FR_NO_FILE:
        case FR_NO_PATH:
            return FIRMWARE_STATUS_NOT_FOUND;
        case FR_INVALID_OBJECT:
        case FR_INVALID_PARAMETER:
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        case FR_TIMEOUT:
            return FIRMWARE_STATUS_TIMEOUT;
        case FR_NOT_READY:
            return FIRMWARE_STATUS_BUSY;
        case FR_EXIST:
            return FIRMWARE_STATUS_INVALID_STATE;
        default:
            return FIRMWARE_STATUS_IO_ERROR;
    }
}

/** 将逻辑绝对路径拼接到 backend 卷路径并执行安全校验。 */
static int MakePath(const fatfs_backend_t *backend, const char *path,
                    char output[STORAGE_PATH_MAX + 4U])
{
    const int length =
        (backend != NULL && backend->config.volumePath != NULL && Storage_IsSafePath(path))
            ? snprintf(output, STORAGE_PATH_MAX + 4U, "%s%s", backend->config.volumePath, path + 1)
            : -1;
    return length > 0 && length < (int) (STORAGE_PATH_MAX + 4U);
}

/** 根据句柄查找已占用的文件槽。 */
static FIL *FindFile(fatfs_backend_t *backend, uint32_t handle)
{
    if (backend == NULL || handle == 0U || handle > STORAGE_MAX_OPEN_FILES ||
        backend->fileUsed[handle - 1U] == 0U)
        return NULL;
    return &backend->files[handle - 1U];
}

/** 根据句柄查找已占用的目录槽。 */
static DIR *FindDirectory(fatfs_backend_t *backend, uint32_t handle)
{
    if (backend == NULL || handle == 0U || handle > STORAGE_MAX_OPEN_DIRECTORIES ||
        backend->directoryUsed[handle - 1U] == 0U)
        return NULL;
    return &backend->directories[handle - 1U];
}

/** 绑定卷路径、FatFs 对象和媒体就绪回调。 */
void FatFsBackend_Configure(fatfs_backend_t *backend, const fatfs_backend_config_t *config)
{
    if (backend == NULL || config == NULL)
        return;
    (void) memset(backend, 0, sizeof(*backend));
    backend->config = *config;
}

/** 清空句柄表并将 backend 置为未挂载。 */
firmware_status_t FatFsBackend_Init(fatfs_backend_t *backend)
{
    if (backend == NULL || backend->config.fileSystem == NULL || backend->config.volumePath == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    (void) memset(backend->fileUsed, 0, sizeof(backend->fileUsed));
    (void) memset(backend->directoryUsed, 0, sizeof(backend->directoryUsed));
    backend->mounted = 0U;
    return FIRMWARE_STATUS_OK;
}

/** 检查媒体就绪后挂载 FatFs 卷。 */
firmware_status_t FatFsBackend_Mount(fatfs_backend_t *backend)
{
    FRESULT result;
    firmware_status_t status;
    if (backend == NULL || backend->config.fileSystem == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    backend->mounted = 0U;
    if (backend->config.prepareMount != NULL)
    {
        status = backend->config.prepareMount(backend->config.context);
        if (FirmwareStatus_IsError(status))
            return status;
    }
    result = f_mount(backend->config.fileSystem, backend->config.volumePath, 1U);
    if (result == FR_OK)
        backend->mounted = 1U;
    if (result != FR_OK)
        backend->mounted = 0U;
    return MapResult(result);
}

/** 关闭所有句柄并卸载 FatFs 卷。 */
firmware_status_t FatFsBackend_Unmount(fatfs_backend_t *backend)
{
    uint32_t i;
    FRESULT result;
    if (backend == NULL || backend->config.fileSystem == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    for (i = 0U; i < STORAGE_MAX_OPEN_FILES; ++i)
    {
        if (backend->fileUsed[i] != 0U)
            (void) f_close(&backend->files[i]);
        backend->fileUsed[i] = 0U;
    }
    for (i = 0U; i < STORAGE_MAX_OPEN_DIRECTORIES; ++i)
    {
        if (backend->directoryUsed[i] != 0U)
            (void) f_closedir(&backend->directories[i]);
        backend->directoryUsed[i] = 0U;
    }
    result           = f_mount(NULL, backend->config.volumePath, 0U);
    backend->mounted = 0U;
    return MapResult(result);
}

/** 创建逻辑路径对应的目录。 */
firmware_status_t FatFsBackend_Mkdir(fatfs_backend_t *backend, const char *path)
{
    char fullPath[STORAGE_PATH_MAX + 4U];
    if (backend == NULL || backend->mounted == 0U)
        return FIRMWARE_STATUS_INVALID_STATE;
    if (!MakePath(backend, path, fullPath))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return MapResult(f_mkdir(fullPath));
}

/** 为写入文件逐级创建缺失的父目录。 */
static firmware_status_t EnsureParentDirectories(fatfs_backend_t *backend, const char *path)
{
    char parent[STORAGE_PATH_MAX];
    char *slash;
    firmware_status_t status;
    if (path == NULL || strlen(path) >= sizeof(parent))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    (void) strcpy(parent, path);
    slash = strrchr(parent, '/');
    if (slash == NULL || slash == parent)
        return FIRMWARE_STATUS_OK;
    *slash = '\0';
    for (slash = parent + 1; *slash != '\0'; ++slash)
    {
        if (*slash == '/')
        {
            *slash = '\0';
            status = FatFsBackend_Mkdir(backend, parent);
            if (status != FIRMWARE_STATUS_OK && status != FIRMWARE_STATUS_INVALID_STATE)
                return status;
            *slash = '/';
        }
    }
    status = FatFsBackend_Mkdir(backend, parent);
    return status == FIRMWARE_STATUS_INVALID_STATE ? FIRMWARE_STATUS_OK : status;
}

/** 分配句柄并按模式打开文件。 */
firmware_status_t FatFsBackend_Open(fatfs_backend_t *backend, const char *path,
                                    storage_open_mode_t mode, uint32_t *handle)
{
    char fullPath[STORAGE_PATH_MAX + 4U];
    BYTE flags;
    uint32_t i;
    if (backend == NULL || handle == NULL || backend->mounted == 0U ||
        !MakePath(backend, path, fullPath))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (mode != STORAGE_OPEN_READ && backend->config.writable == 0U)
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    switch (mode)
    {
        case STORAGE_OPEN_READ:
            flags = FA_READ;
            break;
        case STORAGE_OPEN_WRITE_TRUNCATE:
            flags = FA_WRITE | FA_CREATE_ALWAYS;
            break;
        case STORAGE_OPEN_WRITE_APPEND:
            flags = FA_WRITE | FA_OPEN_APPEND;
            break;
        case STORAGE_OPEN_READ_WRITE:
            flags = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;
            break;
        default:
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (mode != STORAGE_OPEN_READ)
    {
        firmware_status_t status = EnsureParentDirectories(backend, path);
        if (FirmwareStatus_IsError(status))
            return status;
    }
    for (i = 0U; i < STORAGE_MAX_OPEN_FILES; ++i)
    {
        if (backend->fileUsed[i] == 0U)
        {
            const FRESULT result = f_open(&backend->files[i], fullPath, flags);
            if (result != FR_OK)
                return MapResult(result);
            backend->fileUsed[i] = 1U;
            *handle              = i + 1U;
            return FIRMWARE_STATUS_OK;
        }
    }
    return FIRMWARE_STATUS_BUSY;
}

/** 从指定偏移读取文件数据。 */
firmware_status_t FatFsBackend_Read(fatfs_backend_t *backend, uint32_t handle, uint32_t offset,
                                    void *buffer, size_t size, size_t *bytesRead)
{
    UINT count = 0U;
    FIL *file  = FindFile(backend, handle);
    FRESULT result;
    if (file == NULL || bytesRead == NULL || (buffer == NULL && size != 0U) || size > UINT_MAX)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    result = f_lseek(file, (FSIZE_t) offset);
    if (result == FR_OK)
        result = f_read(file, buffer, (UINT) size, &count);
    *bytesRead = count;
    return MapResult(result);
}

/** 向已打开文件写入数据并返回实际字节数。 */
firmware_status_t FatFsBackend_Write(fatfs_backend_t *backend, uint32_t handle, const void *buffer,
                                     size_t size, size_t *bytesWritten)
{
    UINT written = 0U;
    FIL *file    = FindFile(backend, handle);
    FRESULT result;
    if (backend == NULL || backend->config.writable == 0U)
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    if (file == NULL || bytesWritten == NULL || (buffer == NULL && size != 0U) || size > UINT_MAX)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    result        = f_write(file, buffer, (UINT) size, &written);
    *bytesWritten = written;
    return MapResult(result == FR_OK && written != (UINT) size ? FR_DISK_ERR : result);
}

/** 同步文件数据；只读 backend 无需执行同步。 */
firmware_status_t FatFsBackend_Sync(fatfs_backend_t *backend, uint32_t handle)
{
    FIL *file = FindFile(backend, handle);
    if (file == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return backend->config.writable != 0U ? MapResult(f_sync(file)) : FIRMWARE_STATUS_OK;
}

/** 关闭文件并释放句柄槽。 */
firmware_status_t FatFsBackend_Close(fatfs_backend_t *backend, uint32_t handle)
{
    FIL *file = FindFile(backend, handle);
    FRESULT result;
    if (file == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    result                         = f_close(file);
    backend->fileUsed[handle - 1U] = 0U;
    return MapResult(result);
}

/** 查询文件大小等基本信息。 */
firmware_status_t FatFsBackend_Stat(fatfs_backend_t *backend, const char *path,
                                    storage_file_info_t *info)
{
    char fullPath[STORAGE_PATH_MAX + 4U];
    FILINFO fileInfo;
    FRESULT result;
    if (backend == NULL || backend->mounted == 0U)
        return FIRMWARE_STATUS_INVALID_STATE;
    if (info == NULL || !MakePath(backend, path, fullPath))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    result       = f_stat(fullPath, &fileInfo);
    info->handle = 0U;
    info->size   = result == FR_OK && fileInfo.fsize <= UINT32_MAX ? (uint32_t) fileInfo.fsize : 0U;
    info->crc32  = 0U;
    return MapResult(result);
}

/** 分配句柄并打开目录。 */
firmware_status_t FatFsBackend_DirOpen(fatfs_backend_t *backend, const char *path, uint32_t *handle)
{
    char fullPath[STORAGE_PATH_MAX + 4U];
    uint32_t i;
    if (backend == NULL || handle == NULL || backend->mounted == 0U ||
        !MakePath(backend, path, fullPath))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    for (i = 0U; i < STORAGE_MAX_OPEN_DIRECTORIES; ++i)
    {
        if (backend->directoryUsed[i] == 0U)
        {
            const FRESULT result = f_opendir(&backend->directories[i], fullPath);
            if (result != FR_OK)
                return MapResult(result);
            backend->directoryUsed[i] = 1U;
            *handle                   = i + 1U;
            return FIRMWARE_STATUS_OK;
        }
    }
    return FIRMWARE_STATUS_BUSY;
}

/** 读取目录下一项并转换为通用目录结构。 */
firmware_status_t FatFsBackend_DirRead(fatfs_backend_t *backend, uint32_t handle,
                                       storage_dir_entry_t *entry)
{
    DIR *directory = FindDirectory(backend, handle);
    FILINFO info;
    FRESULT result;
    if (directory == NULL || entry == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    (void) memset(&info, 0, sizeof(info));
    (void) memset(entry, 0, sizeof(*entry));
    result = f_readdir(directory, &info);
    if (result != FR_OK)
        return MapResult(result);
    if (info.fname[0] == '\0')
    {
        entry->end_of_directory = 1U;
        return FIRMWARE_STATUS_OK;
    }
    if (strlen(info.fname) >= sizeof(entry->name))
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    (void) strcpy(entry->name, info.fname);
    entry->size         = info.fsize <= UINT32_MAX ? (uint32_t) info.fsize : 0U;
    entry->is_directory = (info.fattrib & AM_DIR) != 0U ? 1U : 0U;
    return FIRMWARE_STATUS_OK;
}

/** 关闭目录并释放句柄槽。 */
firmware_status_t FatFsBackend_DirClose(fatfs_backend_t *backend, uint32_t handle)
{
    DIR *directory = FindDirectory(backend, handle);
    FRESULT result;
    if (directory == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    result                              = f_closedir(directory);
    backend->directoryUsed[handle - 1U] = 0U;
    return MapResult(result);
}

/** 在同一卷内重命名文件。 */
firmware_status_t FatFsBackend_Rename(fatfs_backend_t *backend, const char *oldPath,
                                      const char *newPath)
{
    char oldFullPath[STORAGE_PATH_MAX + 4U];
    char newFullPath[STORAGE_PATH_MAX + 4U];
    if (backend == NULL || backend->mounted == 0U)
        return FIRMWARE_STATUS_INVALID_STATE;
    if (backend->config.writable == 0U || !MakePath(backend, oldPath, oldFullPath) ||
        !MakePath(backend, newPath, newFullPath))
        return backend->config.writable == 0U ? FIRMWARE_STATUS_NOT_SUPPORTED
                                              : FIRMWARE_STATUS_INVALID_ARGUMENT;
    return MapResult(f_rename(oldFullPath, newFullPath));
}

/** 删除同一卷内文件。 */
firmware_status_t FatFsBackend_Unlink(fatfs_backend_t *backend, const char *path)
{
    char fullPath[STORAGE_PATH_MAX + 4U];
    if (backend == NULL || backend->mounted == 0U)
        return FIRMWARE_STATUS_INVALID_STATE;
    if (backend->config.writable == 0U)
        return FIRMWARE_STATUS_NOT_SUPPORTED;
    if (!MakePath(backend, path, fullPath))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    return MapResult(f_unlink(fullPath));
}
