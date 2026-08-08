/**
 * @file update_request_service_api.h
 * @brief 严格 trusted request 解析及其与 Manifest 绑定的 API。
 */
#ifndef SERVICES_UPDATE_REQUEST_SERVICE_API_H
#define SERVICES_UPDATE_REQUEST_SERVICE_API_H

#include <stdint.h>

#include "firmware/status.h"
#include "services/common/manifest_types.h"
#include "services/common/update_request_types.h"

struct update_request_service;

/**
 * @brief 将原始受信请求解析为领域对象并执行冻结 schema 校验。
 *
 * @param[in,out] service 已初始化的请求解析服务。
 * @param[in] data 原始 JSON 字节流。
 * @param[in] size 原始字节长度。
 * @param[out] request 接收校验后的请求。
 * @return FIRMWARE_STATUS_OK 或参数、格式、哈希相关错误。
 */
firmware_status_t UpdateRequestService_ParseAndValidate(
    struct update_request_service *service,
    const uint8_t *data,
    uint32_t size,
    update_request_t *request);

/**
 * @brief 验证 request 指向的 package_id 和 Manifest SHA-256 与实际 Manifest 一致。
 *
 * 该步骤把已受信的请求与当前读取到的 Manifest 绑定，防止同一存储卷中的文件被
 * 交叉替换后仍继续安装。
 *
 * @param[in,out] service 已初始化的请求服务。
 * @param[in] request 已通过 ParseAndValidate 的请求。
 * @param[in] manifest_data 原始 Manifest 字节流。
 * @param[in] manifest_size 原始 Manifest 长度。
 * @param[in] manifest 已解析的 Manifest 模型。
 * @return FIRMWARE_STATUS_OK 表示绑定成立。
 */
firmware_status_t UpdateRequestService_ValidateManifestBinding(
    struct update_request_service *service,
    const update_request_t *request,
    const uint8_t *manifest_data,
    uint32_t manifest_size,
    const validated_manifest_t *manifest);

#endif
