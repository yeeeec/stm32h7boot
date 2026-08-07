/**
 * @file fatfs_update_request_store_adapter.h
 * @brief SD-card FatFs raw trusted-request store.
 */
#ifndef ADAPTERS_FATFS_UPDATE_REQUEST_STORE_ADAPTER_H
#define ADAPTERS_FATFS_UPDATE_REQUEST_STORE_ADAPTER_H

#include "adapters/fatfs_package_source_adapter.h"
#include "firmware/update_request_store.h"

typedef struct
{
    update_request_store_t interface;
    fatfs_release_volume_context_t *volume;
} fatfs_update_request_store_adapter_t;

firmware_status_t FatFsUpdateRequestStoreAdapter_Init(
    fatfs_update_request_store_adapter_t *adapter,
    fatfs_release_volume_context_t *volume);

const update_request_store_t *FatFsUpdateRequestStoreAdapter_Interface(
    const fatfs_update_request_store_adapter_t *adapter);

#endif
