/**
 * @file manifest_service.h
 * @brief 供 Composition 装配的 Manifest 服务对象和依赖声明。
 *
 * 服务使用固定大小 token 工作区解析严格 JSON Manifest，并借助注入的哈希端口
 * 计算原始 Manifest 与 package_id 的完整性摘要。
 */
#ifndef SERVICES_MANIFEST_SERVICE_INTERNAL_H
#define SERVICES_MANIFEST_SERVICE_INTERNAL_H

#include "firmware/hash.h"
#include "services/capability/json_document.h"
#include "services/capability/manifest_service_api.h"

/** 静态 JSON token 工作区的容量，覆盖 V1 正常文档及受限的异常输入。 */
#define MANIFEST_SERVICE_TOKEN_CAPACITY 64U

/* V1 对象 schema 使用 31 个 token；预留受限的异常输入余量，同时将固定工作区
 * 保持在 4 KiB 静态内存预算以内。 */
_Static_assert(
    (MANIFEST_SERVICE_TOKEN_CAPACITY * sizeof(json_token_t)) <= 4096U,
    "Manifest token workspace exceeds the static-memory budget");

/** Manifest 服务初始化时由 Composition 注入的密码学能力。 */
typedef struct
{
    /** 计算原始 Manifest 和 package_id 摘要的可重入使用约定哈希端口。 */
    const hash_provider_t *hash;
} manifest_service_dependencies_t;

/**
 * Manifest 服务的运行时实例。
 *
 * token 数组归服务独占，单次解析完成前不可由并发调用复用；本裸机架构通过主循环
 * 串行调用保证该条件。
 */
typedef struct manifest_service
{
    /** 初始化后持有的哈希端口。 */
    const hash_provider_t *hash;
    /** 无动态分配的 JSON 语法树 token 工作区。 */
    json_token_t tokens[MANIFEST_SERVICE_TOKEN_CAPACITY];
    /** 非零表示哈希端口已经过完整性检查并可使用。 */
    int initialized;
} manifest_service_t;

/**
 * 校验哈希端口并初始化 Manifest 服务。
 *
 * @param service 由 Composition 静态分配的服务对象，必须尚未初始化。
 * @param dependencies 包含完整 hash_provider_t 回调集的依赖对象。
 * @return 成功时返回 FIRMWARE_STATUS_OK；参数或哈希端口不完整时返回错误。
 */
firmware_status_t ManifestService_Init(
    manifest_service_t *service,
    const manifest_service_dependencies_t *dependencies);

#endif
