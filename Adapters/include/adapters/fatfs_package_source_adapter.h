/**
 * @file fatfs_package_source_adapter.h
 * @brief SD 卡 FatFs 发布卷适配器。
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
    /** 对外暴露的发布包访问回调表。 */
    package_source_t interface;
    /** 与 Request Store 共享的卷状态及文件所有权。 */
    fatfs_release_volume_context_t *volume;
} fatfs_package_source_adapter_t;

/** 初始化共享卷状态，初始时卷和文件均未打开。 */
firmware_status_t FatFsReleaseVolumeContext_Init(fatfs_release_volume_context_t *volume);

/**
 * 初始化固定文件访问及旧路径名兼容访问。
 * 调用者负责持有 @p volume，并必须将其与 request-store 适配器共享。
 *
 * @param[out] adapter 待初始化的发布包适配器。
 * @param[in] volume 由调用者持有的共享卷状态。
 * @return 成功时返回 FIRMWARE_STATUS_OK；参数无效时返回
 *         FIRMWARE_STATUS_INVALID_ARGUMENT。
 */
firmware_status_t FatFsPackageSourceAdapter_Init(fatfs_package_source_adapter_t *adapter,
                                                 fatfs_release_volume_context_t *volume);

/** 返回适配器持有的发布包访问接口；参数为 NULL 时返回 NULL。 */
const package_source_t *
FatFsPackageSourceAdapter_Interface(const fatfs_package_source_adapter_t *adapter);

#endif
