/**
 * @file service_result.h
 * @brief Use-case Service 对外暴露的稳定生命周期与失败结果类型。
 *
 * Application 只根据这些类型做顶层决策，不依赖内部状态机枚举或底层原生错误码。
 */
#ifndef SERVICES_SERVICE_RESULT_H
#define SERVICES_SERVICE_RESULT_H

#include <stdint.h>

#include "firmware/status.h"

typedef enum
{
    /** 服务尚未开始或已被复位到空闲状态。 */
    SERVICE_RUN_STATE_IDLE = 0,
    /** 服务正在等待后续 Process 调用推进。 */
    SERVICE_RUN_STATE_RUNNING,
    /** 服务已成功完成，结果对象可读取。 */
    SERVICE_RUN_STATE_SUCCEEDED,
    /** 服务已失败，结果对象保存失败上下文。 */
    SERVICE_RUN_STATE_FAILED,
    /** 调用者在允许的阶段主动取消了服务。 */
    SERVICE_RUN_STATE_CANCELLED
} service_run_state_t;

/**
 * @brief 供 Application 处理的稳定错误分类。
 *
 * 该枚举表达业务错误，不等同于 firmware_status_t。status 提供技术状态，error
 * 指明应记录、恢复或 fail-closed 的业务原因。
 */
typedef enum
{
    /** 未发生错误。 */
    BOOT_ERROR_NONE = 0,
    /** Active Record 缺失、损坏或字段非法。 */
    BOOT_ERROR_CONTROL_RECORD,
    /** 发布介质或目标存储不可用。 */
    BOOT_ERROR_MEDIA_UNAVAILABLE,
    /** Manifest JSON 或 schema 不符合冻结合同。 */
    BOOT_ERROR_MANIFEST_FORMAT,
    /** 发布包产品标识不匹配。 */
    BOOT_ERROR_INCOMPATIBLE_PRODUCT,
    /** 版本策略拒绝该发布包。 */
    BOOT_ERROR_VERSION_REJECTED,
    /** APP 源文件摘要不匹配。 */
    BOOT_ERROR_APP_SOURCE_HASH,
    /** GUI 源文件摘要不匹配。 */
    BOOT_ERROR_GUI_SOURCE_HASH,
    /** APP 分区擦除失败。 */
    BOOT_ERROR_APP_ERASE,
    /** APP 分区写入失败。 */
    BOOT_ERROR_APP_PROGRAM,
    /** GUI 分区擦除失败。 */
    BOOT_ERROR_GUI_ERASE,
    /** GUI 分区写入失败。 */
    BOOT_ERROR_GUI_PROGRAM,
    /** EEPROM Active Record 原子提交失败。 */
    BOOT_ERROR_EEPROM_COMMIT,
    /** QSPI XIP 模式或 Cache 处理失败。 */
    BOOT_ERROR_XIP_SETUP,
    /** APP 初始向量表不满足 MSP/Reset Handler 规则。 */
    BOOT_ERROR_VECTOR_TABLE,
    /** 未归类的内部服务错误。 */
    BOOT_ERROR_INTERNAL,
    /** Manifest 准备或读取阶段失败。 */
    BOOT_ERROR_PREPARE,
    /** trusted request 与 Manifest 的身份或摘要绑定不一致。 */
    BOOT_ERROR_MANIFEST_BINDING,
    /** APP 源文件长度不满足 Manifest 或分区限制。 */
    BOOT_ERROR_APP_SOURCE_SIZE,
    /** GUI 源文件长度不满足 Manifest 或分区限制。 */
    BOOT_ERROR_GUI_SOURCE_SIZE,
    /** APP 目标区域回读失败。 */
    BOOT_ERROR_APP_TARGET_READ,
    /** APP 目标区域摘要不匹配。 */
    BOOT_ERROR_APP_TARGET_HASH,
    /** GUI 目标区域回读失败。 */
    BOOT_ERROR_GUI_TARGET_READ,
    /** GUI 目标区域摘要不匹配。 */
    BOOT_ERROR_GUI_TARGET_HASH,
    /** 生命周期调用顺序或对象状态非法。 */
    BOOT_ERROR_INVALID_STATE,
    /** Runtime 地址、长度或分区边界非法。 */
    BOOT_ERROR_RUNTIME_BOUNDS,
    /** 外部 MCU 镜像 Source 不可用、尺寸不符或读取失败。 */
    BOOT_ERROR_SECONDARY_MCU_SOURCE,
    /** 外部 MCU 目标地址、容量、擦除页或 ROM 能力不满足请求。 */
    BOOT_ERROR_SECONDARY_MCU_TARGET,
    /** 进入外部 MCU System Memory Bootloader 或设备探测失败。 */
    BOOT_ERROR_SECONDARY_MCU_ENTER,
    /** 外部 MCU Flash 页擦除失败。 */
    BOOT_ERROR_SECONDARY_MCU_ERASE,
    /** 外部 MCU Flash 写入失败。 */
    BOOT_ERROR_SECONDARY_MCU_PROGRAM,
    /** 外部 MCU Flash 回读失败或内容与 Source 不一致。 */
    BOOT_ERROR_SECONDARY_MCU_VERIFY,
    /** 外部 MCU 退出 ROM、恢复 BOOT/RST/UART 条件失败。 */
    BOOT_ERROR_SECONDARY_MCU_EXIT
} boot_error_t;

/** 某次服务操作的最终或进行中结果快照。 */
typedef struct
{
    /** 底层通用状态码。 */
    firmware_status_t status;
    /** 可供 Application 决策的业务错误类别。 */
    boot_error_t error;
    /** 产生该结果的内部阶段编号，仅用于诊断。 */
    uint32_t stage;
    /** 保留原生或附加诊断值；未使用时为零。 */
    int32_t native_error;
} service_result_t;

#endif
