/**
 * @file update_request_types.h
 * @brief 受信升级请求的领域模型。
 *
 * 该请求是 Application 到 Bootloader 的受信 handoff；其可信性来自系统级存储
 * 保护，不来自普通 JSON 或 FAT 文件本身。
 */
#ifndef SERVICES_UPDATE_REQUEST_TYPES_H
#define SERVICES_UPDATE_REQUEST_TYPES_H

#include <stdint.h>

/** 当前 trusted request 文档格式版本。 */
#define UPDATE_REQUEST_FORMAT_VERSION      1U
/** package_id 最大字符数，不含结尾 NUL。 */
#define UPDATE_REQUEST_PACKAGE_ID_MAX_SIZE 63U
/** Manifest SHA-256 摘要长度，单位为字节。 */
#define UPDATE_REQUEST_MANIFEST_HASH_SIZE  32U

/** Upgrade-request component selection bits. */
#define UPDATE_COMPONENT_APP     (1U << 0)
#define UPDATE_COMPONENT_GUI     (1U << 1)
#define UPDATE_COMPONENT_THERAPY (1U << 2)
#define UPDATE_COMPONENT_ALL     (UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI | UPDATE_COMPONENT_THERAPY)

/** 严格解析且语义校验通过的升级请求。 */
typedef struct
{
    /** 文档格式版本，必须等于 @ref UPDATE_REQUEST_FORMAT_VERSION。 */
    uint32_t format_version;
    /** 非零表示请求安装指定 package_id。 */
    uint8_t requested;
    /** 本次请求选择的一个或多个 @ref UPDATE_COMPONENT_APP 组件位。 */
    uint32_t component_mask;
    /** 目标发布包标识，包含结尾 NUL。 */
    char package_id[UPDATE_REQUEST_PACKAGE_ID_MAX_SIZE + 1U];
    /** 调用者期望的 Manifest 摘要，用于防止包与请求错配。 */
    uint8_t manifest_sha256[UPDATE_REQUEST_MANIFEST_HASH_SIZE];
} update_request_t;

#endif
