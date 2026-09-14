/**
 * @file fatfs_update_request_store_adapter.h
 * @brief SD/FatFs 原始 trusted request 存储适配器。
 */
#ifndef ADAPTERS_FATFS_UPDATE_REQUEST_STORE_ADAPTER_H
#define ADAPTERS_FATFS_UPDATE_REQUEST_STORE_ADAPTER_H

#include "adapters/fatfs_package_source_adapter.h"
#include "firmware/update_request_store.h"

/** 绑定共享 FatFs 卷状态的原始 trusted request 存储适配器。 */
typedef struct
{
    /** 对外暴露的原始 request 存储回调表。 */
    update_request_store_t interface;
    /** 与 Package Source 共享的卷状态及 request FIL 所有权。 */
    fatfs_release_volume_context_t *volume;
} fatfs_update_request_store_adapter_t;

/**
 * @brief 将共享 FatFs 卷绑定到 trusted request 存储接口。
 *
 * @param[out] adapter 待初始化的 request 存储适配器。
 * @param[in] volume 必须与 Package Source 适配器共享的卷状态。
 * @return 成功时返回 FIRMWARE_STATUS_OK；参数为 NULL 时返回
 *         FIRMWARE_STATUS_INVALID_ARGUMENT。
 */
firmware_status_t FatFsUpdateRequestStoreAdapter_Init(fatfs_update_request_store_adapter_t *adapter,
                                                      fatfs_release_volume_context_t *volume);

/** 返回适配器持有的 request 存储接口；参数为 NULL 时返回 NULL。 */
const update_request_store_t *
FatFsUpdateRequestStoreAdapter_Interface(const fatfs_update_request_store_adapter_t *adapter);

#endif
