/**
 * @file boot_control_types.h
 * @brief Active Record 的稳定领域模型。
 *
 * 该文件只描述 Boot Control 服务在 EEPROM A/B 槽间持久化和读取的数据，
 * 不包含任何介质、校验算法或写入流程的实现细节。Application、Services 与
 * Adapter 必须使用同一份布局语义，避免把存储格式耦合到某个具体驱动。
 */
#ifndef SERVICES_BOOT_CONTROL_TYPES_H
#define SERVICES_BOOT_CONTROL_TYPES_H

#include <stdint.h>

#include "services/common/boot_types.h"

/** Package ID 截断哈希长度，单位为字节。 */
#define BOOT_CONTROL_PACKAGE_ID_HASH_SIZE 16U
/** 完整 Manifest SHA-256 摘要长度，单位为字节。 */
#define BOOT_CONTROL_MANIFEST_HASH_SIZE   32U
/** APP 或 GUI SHA-256 摘要长度，单位为字节。 */
#define BOOT_CONTROL_IMAGE_HASH_SIZE      32U
/** 当前持久化记录格式版本。 */
#define BOOT_ACTIVE_RECORD_FORMAT_V2      2U
/** 单个 EEPROM 槽中 Active Record 的固定占用大小，单位为字节。 */
#define BOOT_ACTIVE_RECORD_SIZE           256U
/** 表示记录已通过提交流程并可作为启动候选项的状态值。 */
#define BOOT_ACTIVE_RECORD_STATE_VALID    1U

/**
 * @brief 已提交 Runtime 对的逻辑描述。
 *
 * 该结构的字节级封装、CRC 与提交标记由 Boot Control 服务负责；本结构只保存
 * 经验证的业务字段。sequence 用于在 A/B 槽都有效时选择较新的记录。
 */
typedef struct
{
    /** 记录格式，当前必须为 @ref BOOT_ACTIVE_RECORD_FORMAT_V2。 */
    uint16_t format_version;
    /** 记录有效性状态，正常启动只接受 @ref BOOT_ACTIVE_RECORD_STATE_VALID。 */
    uint8_t state;
    /** 预留标志位；当前版本必须保持为兼容值。 */
    uint8_t flags;
    /** 单调递增的提交序号，用于选择 A/B 槽中的最新记录。 */
    uint32_t sequence;
    /** 已安装发布包的语义版本。 */
    release_version_t release_version;
    /** 发布构建号，用于追踪同版本下的构建产物。 */
    uint32_t build_number;
    /** APP 有效 payload 长度，单位为字节。 */
    uint32_t app_size;
    /** GUI 有效 payload 长度，单位为字节。 */
    uint32_t gui_size;
    /** Package ID 的固定长度截断哈希，用于识别发布包。 */
    uint8_t package_id_hash[BOOT_CONTROL_PACKAGE_ID_HASH_SIZE];
    /** 原始 Manifest 的 SHA-256 摘要。 */
    uint8_t manifest_sha256[BOOT_CONTROL_MANIFEST_HASH_SIZE];
    /** 已安装 APP payload 的 SHA-256 摘要。 */
    uint8_t app_sha256[BOOT_CONTROL_IMAGE_HASH_SIZE];
    /** 已安装 GUI payload 的 SHA-256 摘要。 */
    uint8_t gui_sha256[BOOT_CONTROL_IMAGE_HASH_SIZE];
} boot_active_record_t;

#endif
