/**
 * @file fatfs_package_source_adapter.h
 * @brief SD-card FatFs adapters for the release volume.
 */
#ifndef ADAPTERS_FATFS_PACKAGE_SOURCE_ADAPTER_H
#define ADAPTERS_FATFS_PACKAGE_SOURCE_ADAPTER_H

#include "firmware/package_source.h"

/**
 * @brief 一个 SD/FatFs 发布卷的共享真实所有权状态。
 *
 * package_file_open 由 Package Source 使用，request_file_open 由 Request Store 使用。
 * 两者都引用 CubeMX 提供的单例 SDFile，因此 f_close 失败后可由后续卸载重试，
 * 同时不把 FatFs FIL 类型泄漏给 Composition。
 */
typedef struct
{
    /** FatFs 卷是否已由 f_mount 成功挂载。 */
    int mounted;
    /** 固定 APP/GUI/Manifest 文件是否仍由 Package Source 打开。 */
    int package_file_open;
    /** trusted request 文件是否仍由 Request Store 打开。 */
    int request_file_open;
} fatfs_release_volume_context_t;

typedef struct
{
    package_source_t interface;
    fatfs_release_volume_context_t *volume;
} fatfs_package_source_adapter_t;

firmware_status_t FatFsReleaseVolumeContext_Init(
    fatfs_release_volume_context_t *volume);

/**
 * Initialize fixed-file and deprecated pathname compatibility access.
 * The caller owns @p volume and must share it with the request-store adapter.
 */
firmware_status_t FatFsPackageSourceAdapter_Init(
    fatfs_package_source_adapter_t *adapter,
    fatfs_release_volume_context_t *volume);

const package_source_t *FatFsPackageSourceAdapter_Interface(
    const fatfs_package_source_adapter_t *adapter);

#endif
