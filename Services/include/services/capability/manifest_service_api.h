/**
 * @file manifest_service_api.h
 * @brief 严格 Manifest 解析和完整性哈希 API。
 */
#ifndef SERVICES_MANIFEST_SERVICE_API_H
#define SERVICES_MANIFEST_SERVICE_API_H

#include <stdint.h>

#include "firmware/status.h"
#include "services/common/manifest_types.h"

/** 允许进入严格解析器的 Manifest 最大字节数。 */
#define MANIFEST_SERVICE_MAX_DOCUMENT_SIZE 16384U

struct manifest_service;

/**
 * @brief 解析、schema 校验并计算一份完整量产 Manifest 的摘要。
 *
 * @param[in,out] service 已初始化的解析服务；内部 token 缓冲区归该对象所有。
 * @param[in] data 原始 Manifest UTF-8 JSON 字节流。
 * @param[in] size data 的精确长度，不包含额外终止符。
 * @param[out] manifest 接收严格校验后的领域模型及派生摘要。
 * @return FIRMWARE_STATUS_OK，或代表格式、容量、哈希 Provider 的错误状态。
 */
firmware_status_t ManifestService_ParseAndValidate(struct manifest_service *service,
                                                   const uint8_t *data, uint32_t size,
                                                   validated_manifest_t *manifest);

#endif
