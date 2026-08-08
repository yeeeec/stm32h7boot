/**
 * @file launch_service.h
 * @brief 供 Composition 创建的 Launch Service 对象及其依赖。
 *
 * Launch Service 是同步的最终交接步骤。它在 indirect 模式读取向量表并校验，
 * 随后由 XIP Controller 切换到 memory-mapped 模式，成功跳转后不再返回。
 */
#ifndef SERVICES_LAUNCH_SERVICE_INTERNAL_H
#define SERVICES_LAUNCH_SERVICE_INTERNAL_H

#include "firmware/application_jump.h"
#include "firmware/async_block_device.h"
#include "firmware/xip_controller.h"
#include "services/capability/vector_validation.h"
#include "services/use_case/launch_service_api.h"

/** 最终 Application 启动所需的依赖和 SRAM 策略。 */
typedef struct
{
    /** 用于在 indirect 模式读取 Application 向量表的存储接口。 */
    const async_block_device_t *storage;
    /** QSPI memory-mapped 模式及 Cache 失效控制接口。 */
    const xip_controller_t *xip_controller;
    /** 执行 VTOR/MSP 设置及分支跳转的平台接口。 */
    const application_jump_t *application_jump;
    /** 初始 MSP 的允许 SRAM 区间。 */
    const memory_region_t *sram_regions;
    /** sram_regions 的元素数量。 */
    uint32_t sram_region_count;
} launch_service_dependencies_t;

/** 同步 Launch Service 持有的依赖和结果状态。 */
typedef struct launch_service
{
    /** 注入的 Flash 读取接口。 */
    const async_block_device_t *storage;
    /** 注入的 XIP 控制接口。 */
    const xip_controller_t *xip_controller;
    /** 注入的 Cortex-M 跳转接口。 */
    const application_jump_t *application_jump;
    /** 注入的合法 SRAM 区间表。 */
    const memory_region_t *sram_regions;
    /** 合法 SRAM 区间数量。 */
    uint32_t sram_region_count;
    /** 最近一次 Launch 尝试的结果；成功时通常无法观察到，因为不会返回。 */
    service_result_t result;
    /** Init 成功标志。 */
    int initialized;
} launch_service_t;

/**
 * @brief 从 Composition 持有的依赖初始化 Launch Service。
 *
 * @param[out] service 静态分配的服务对象。
 * @param[in] dependencies 所有接口和 SRAM 策略。
 * @return FIRMWARE_STATUS_OK 或参数/重复初始化错误。
 */
firmware_status_t LaunchService_Init(
    launch_service_t *service,
    const launch_service_dependencies_t *dependencies);

#endif
