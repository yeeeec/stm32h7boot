/**
 * @file update_request_store.h
 * @brief 固定 Trusted Request 原始存储 Contract。
 */
#ifndef FIRMWARE_UPDATE_REQUEST_STORE_H
#define FIRMWARE_UPDATE_REQUEST_STORE_H

#include <stdint.h>

#include "firmware/status.h"

#define UPDATE_REQUEST_STORE_MAX_RAW_SIZE 1024U

/** 加载 Trusted Request 原始文档，不解析也不校验。 */
typedef firmware_status_t (*update_request_store_load_raw_fn)(void *context, uint8_t *buffer,
                                                              uint32_t capacity, uint32_t *size);
/** 只有所属 Application Workflow 进入 Cleanup 后才清除 Request。 */
typedef firmware_status_t (*update_request_store_clear_fn)(void *context);

/**
 * @brief 对固定 Trusted Request 文档的原始访问。
 *
 * Source 已由 Package Owner 挂载。该接口从不解析 JSON 或做信任决策；调用者
 * 持有传入 Buffer，Provider 持有底层介质。
 */
typedef struct
{
    void *context;
    update_request_store_load_raw_fn load_raw;
    update_request_store_clear_fn clear;
} update_request_store_t;

#endif
