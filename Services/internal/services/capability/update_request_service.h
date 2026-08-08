/**
 * @file update_request_service.h
 * @brief 供 Composition 装配的可信更新请求解析服务对象。
 *
 * 该服务使用独立的固定 token 工作区验证更新请求，并以注入的哈希端口把请求中
 * 的 Manifest 摘要与实际读取到的 Manifest 字节绑定。
 */
#ifndef SERVICES_UPDATE_REQUEST_SERVICE_INTERNAL_H
#define SERVICES_UPDATE_REQUEST_SERVICE_INTERNAL_H

#include "firmware/hash.h"
#include "services/capability/json_document.h"
#include "services/capability/update_request_service_api.h"

/** 更新请求 JSON 的最大允许字节数，限制启动阶段的静态资源和解析时间。 */
#define UPDATE_REQUEST_SERVICE_MAX_DOCUMENT_SIZE 512U
/** 更新请求 schema 所需的固定 JSON token 容量。 */
#define UPDATE_REQUEST_SERVICE_TOKEN_CAPACITY     16U

/** 更新请求服务初始化时由 Composition 注入的密码学能力。 */
typedef struct
{
    /** 对原始 Manifest 字节计算 SHA-256 的哈希端口。 */
    const hash_provider_t *hash;
} update_request_service_dependencies_t;

/**
 * 更新请求服务的运行时实例。
 *
 * 该对象不保留解析结果；每次公开解析 API 都将结果复制到调用方提供的输出对象。
 */
typedef struct update_request_service
{
    /** 初始化后持有的哈希端口。 */
    const hash_provider_t *hash;
    /** 无动态分配的 JSON token 工作区。 */
    json_token_t tokens[UPDATE_REQUEST_SERVICE_TOKEN_CAPACITY];
    /** 非零表示哈希端口已经过回调完整性检查。 */
    int initialized;
} update_request_service_t;

/**
 * 校验哈希端口并初始化更新请求服务。
 *
 * @param service 由 Composition 静态分配的服务对象，必须尚未初始化。
 * @param dependencies 包含完整 hash_provider_t 回调集的依赖对象。
 * @return 成功时返回 FIRMWARE_STATUS_OK；参数或哈希端口不完整时返回错误。
 */
firmware_status_t UpdateRequestService_Init(
    update_request_service_t *service,
    const update_request_service_dependencies_t *dependencies);

#endif
