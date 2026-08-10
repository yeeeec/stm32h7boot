/**
 * @file manifest_types.h
 * @brief 严格解析后的升级 Manifest 领域模型。
 *
 * 这里的结构只在 Manifest 完成 schema、字段范围及摘要格式校验后使用，调用者
 * 不应把未校验的 JSON 文本直接映射到这些类型。
 */
#ifndef SERVICES_MANIFEST_TYPES_H
#define SERVICES_MANIFEST_TYPES_H

#include <stdint.h>

#include "services/common/boot_types.h"
#include "services/common/update_request_types.h"

/** package_id 的最大可见字符数，不含结尾 NUL。 */
#define MANIFEST_PACKAGE_ID_MAX_SIZE 63U
/** 固定发布文件名的最大字符数，不含结尾 NUL。 */
#define MANIFEST_FILE_NAME_MAX_SIZE  31U
/** SHA-256 摘要长度，单位为字节。 */
#define MANIFEST_SHA256_SIZE         32U
/** package_id 派生哈希的截断长度，单位为字节。 */
#define MANIFEST_PACKAGE_HASH_SIZE   16U
/** 单个 APP 或 GUI payload 在发布包中的受验证描述。 */
typedef struct
{
    /** 发布包中的固定相对文件名，包含结尾 NUL。 */
    char file[MANIFEST_FILE_NAME_MAX_SIZE + 1U];
    /** payload 的精确字节长度。 */
    uint32_t size_bytes;
    /** payload 原始字节流的 SHA-256 摘要。 */
    uint8_t sha256[MANIFEST_SHA256_SIZE];
} manifest_app_component_t;

/** GUI 的字段契约当前与 APP payload 相同，保留别名以表达业务语义。 */
typedef manifest_app_component_t manifest_gui_component_t;
/** Therapy MCU 镜像沿用无头 RAW payload 字段契约。 */
typedef manifest_app_component_t manifest_therapy_component_t;

/** 已通过严格校验且可绑定到 trusted request 的完整 Manifest。 */
typedef struct
{
    /** 发布包唯一标识，包含结尾 NUL。 */
    char package_id[MANIFEST_PACKAGE_ID_MAX_SIZE + 1U];
    /** 发布构建号。 */
    uint32_t build_number;
    /** 运行该发布包所需的最低 Bootloader 版本。 */
    release_version_t minimum_bootloader_version;
    /** 此发布包声明的语义版本。 */
    release_version_t release_version;
    /** APP payload 描述。 */
    manifest_app_component_t app;
    /** GUI payload 描述。 */
    manifest_gui_component_t gui;
    /** Therapy MCU payload 描述；未声明时保持为零。 */
    manifest_therapy_component_t therapy;
    /** Manifest 实际声明的 @ref UPDATE_COMPONENT_APP 组件集合。 */
    uint32_t component_mask;
    /** 原始 Manifest 文本的 SHA-256 摘要。 */
    uint8_t manifest_sha256[MANIFEST_SHA256_SIZE];
    /** package_id 的固定长度截断哈希，用于 Active Record。 */
    uint8_t package_id_hash128[MANIFEST_PACKAGE_HASH_SIZE];
} validated_manifest_t;

#endif
