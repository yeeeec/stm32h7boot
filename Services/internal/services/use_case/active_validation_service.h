/**
 * @file active_validation_service.h
 * @brief 供 Composition 创建的 Active Validation 对象及其依赖。
 *
 * 这是 Services 的内部装配头，不属于 Application 的公共 API。对象由
 * Composition 静态持有，调用者必须保证依赖对象和工作缓冲区的生命周期覆盖
 * 整个校验过程。
 */
#ifndef SERVICES_ACTIVE_VALIDATION_SERVICE_INTERNAL_H
#define SERVICES_ACTIVE_VALIDATION_SERVICE_INTERNAL_H

#include "firmware/async_block_device.h"
#include "firmware/hash.h"
#include "services/capability/vector_validation.h"
#include "services/common/runtime_layout.h"
#include "services/use_case/active_validation_service_api.h"

/** 固定 Runtime 增量校验所需的调用者持有依赖与工作缓冲区。 */
typedef struct
{
    /** 以 Flash 偏移读 APP/GUI 的异步块设备接口。 */
    const async_block_device_t *storage;
    /** 用于计算 APP/GUI SHA-256 的可重置哈希 Provider。 */
    const hash_provider_t *hash;
    /** 单次读取和哈希所复用的调用者缓冲区。 */
    uint8_t *buffer;
    /** buffer 容量，决定单个 Process 步骤的最大读取长度。 */
    uint32_t buffer_size;
    /** Application 初始 MSP 允许落入的 SRAM 区间。 */
    const memory_region_t *sram_regions;
    /** sram_regions 中的区间数量。 */
    uint32_t sram_region_count;
} active_validation_service_dependencies_t;

/** Active Validation 内部的增量扫描阶段。 */
typedef enum
{
    /** 未运行或成功完成后的空闲阶段。 */
    ACTIVE_VALIDATION_STAGE_IDLE = 0,
    /** 初始化 APP 哈希上下文。 */
    ACTIVE_VALIDATION_STAGE_RESET_APP_HASH,
    /** 分块读取 APP，并在首块同时检查向量表。 */
    ACTIVE_VALIDATION_STAGE_READ_APP,
    /** 完成 APP 摘要并与 Active Record 比较。 */
    ACTIVE_VALIDATION_STAGE_FINISH_APP_HASH,
    /** 初始化 GUI 哈希上下文。 */
    ACTIVE_VALIDATION_STAGE_RESET_GUI_HASH,
    /** 分块读取并哈希 GUI。 */
    ACTIVE_VALIDATION_STAGE_READ_GUI,
    /** 完成 GUI 摘要并与 Active Record 比较。 */
    ACTIVE_VALIDATION_STAGE_FINISH_GUI_HASH
} active_validation_stage_t;

/** APP 与 GUI 增量读取期间保留的完整服务状态。 */
typedef struct active_validation_service
{
    /** 注入的外部 Flash 存储接口。 */
    const async_block_device_t *storage;
    /** 注入的哈希 Provider。 */
    const hash_provider_t *hash;
    /** 共享工作缓冲区。 */
    uint8_t *buffer;
    /** 共享工作缓冲区的容量。 */
    uint32_t buffer_size;
    /** 合法 Application 栈区间表。 */
    const memory_region_t *sram_regions;
    /** 合法 Application 栈区间数量。 */
    uint32_t sram_region_count;
    /** 本次校验开始时复制的 Active Record，避免外部对象被修改。 */
    boot_active_record_t active_record;
    /** 指向固定 Runtime 布局，Start 时取得。 */
    const boot_runtime_layout_t *layout;
    /** 对外可见的服务生命周期。 */
    service_run_state_t state;
    /** 最近一次成功或失败的结果快照。 */
    service_result_t result;
    /** 当前内部扫描阶段。 */
    active_validation_stage_t stage;
    /** 当前组件已处理的字节偏移。 */
    uint32_t offset;
    /** Init 成功标志，防止重复初始化或未初始化使用。 */
    int initialized;
} active_validation_service_t;

/**
 * @brief 校验依赖并初始化 Active Validation Service。
 *
 * @param[out] service 由 Composition 静态分配的服务对象。
 * @param[in] dependencies 长生命周期依赖和工作缓冲区。
 * @return FIRMWARE_STATUS_OK，或参数/状态错误。
 */
firmware_status_t
ActiveValidationService_Init(active_validation_service_t *service,
                             const active_validation_service_dependencies_t *dependencies);

#endif
