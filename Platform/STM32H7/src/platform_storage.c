/**
 * @file platform_storage.c
 * @brief SD/USB 存储门面：为两个媒体配置同一个通用 FatFs backend。
 *
 * 本文件只绑定 CubeMX 文件系统对象和媒体就绪检测；挂载、文件句柄和
 * 目录操作由 storage_backend_fatfs.c 统一实现，媒体挂载所有权留给任务。
 */

#include "ports/storage.h"

#include "bsp_driver_sd.h"
#include "fatfs.h"
#include "firmware/memory.h"
#include "sdmmc.h"
#include "storage_backend_fatfs.h"

extern char SDPath[4];
extern FATFS SDFatFS;

/* SD 与 USB 各自独立的 backend 状态，避免文件句柄互相影响。 */
static fatfs_backend_t s_sd FIRMWARE_STORAGE_RAM;
/* 延迟配置标志：首次调用对应 facade 时绑定 CubeMX 文件系统对象。 */
static uint8_t s_sd_configured;

/** 检查 SD 卡检测脚状态。 */
static firmware_status_t PrepareSdMount(void *context)
{
    (void) context;
    return BSP_SD_IsDetected() == SD_PRESENT ? FIRMWARE_STATUS_OK : FIRMWARE_STATUS_NOT_FOUND;
}

/** 延迟初始化 SD backend 配置。 */
static void ConfigureSd(void)
{
    if (s_sd_configured == 0U)
    {
        const fatfs_backend_config_t config = {.volumePath   = SDPath,
                                               .fileSystem   = &SDFatFS,
                                               .prepareMount = PrepareSdMount,
                                               .context      = NULL,
                                               .writable     = 1U};
        FatFsBackend_Configure(&s_sd, &config);
        s_sd_configured = 1U;
    }
}

/** 初始化 SD backend 状态，不执行挂载。 */
firmware_status_t StorageSd_Init(void)
{
    ConfigureSd();
    MX_FATFS_Init();
    MX_SDMMC1_SD_Init();
    return FatFsBackend_Init(&s_sd);
}

/** 挂载 SD 文件系统；调用者任务拥有挂载生命周期。 */
firmware_status_t StorageSd_Mount(void)
{
    ConfigureSd();
    return FatFsBackend_Mount(&s_sd);
}

/** 卸载 SD 文件系统并关闭遗留句柄。 */
firmware_status_t StorageSd_Unmount(void)
{
    ConfigureSd();
    return FatFsBackend_Unmount(&s_sd);
}

/** 在 SD 上创建目录。 */
firmware_status_t StorageSd_Mkdir(const char *path)
{
    ConfigureSd();
    return FatFsBackend_Mkdir(&s_sd, path);
}

/** 按指定模式打开 SD 文件。 */
firmware_status_t StorageSd_Open(const char *path, storage_open_mode_t mode, uint32_t *handle)
{
    ConfigureSd();
    return FatFsBackend_Open(&s_sd, path, mode, handle);
}

firmware_status_t StorageSd_Read(uint32_t handle, uint32_t offset, void *buffer, size_t size,
                                 size_t *bytes_read)
{
    ConfigureSd();
    return FatFsBackend_Read(&s_sd, handle, offset, buffer, size, bytes_read);
}

firmware_status_t StorageSd_Stat(const char *path, storage_file_info_t *info)
{
    ConfigureSd();
    return FatFsBackend_Stat(&s_sd, path, info);
}

/** 向 SD 文件写入数据块。 */
firmware_status_t StorageSd_Write(uint32_t handle, const void *buffer, size_t size,
                                  size_t *bytes_written)
{
    ConfigureSd();
    return FatFsBackend_Write(&s_sd, handle, buffer, size, bytes_written);
}

/** 将 SD 文件缓冲区同步到介质。 */
firmware_status_t StorageSd_Sync(uint32_t handle)
{
    ConfigureSd();
    return FatFsBackend_Sync(&s_sd, handle);
}

/** 关闭 SD 文件句柄。 */
firmware_status_t StorageSd_Close(uint32_t handle)
{
    ConfigureSd();
    return FatFsBackend_Close(&s_sd, handle);
}

/** 在 SD 上重命名文件。 */
firmware_status_t StorageSd_Rename(const char *old_path, const char *new_path)
{
    ConfigureSd();
    return FatFsBackend_Rename(&s_sd, old_path, new_path);
}

/** 删除 SD 文件。 */
firmware_status_t StorageSd_Unlink(const char *path)
{
    ConfigureSd();
    return FatFsBackend_Unlink(&s_sd, path);
}
