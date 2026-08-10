/**
 * @file composition.h
 * @brief 一次性构建 Firmware 依赖图。
 *
 * Composition 是唯一了解具体 Adapter 和 Service 的层。它将 BSP 持有的设备
 * 绑定到静态分配的 Adapter、Service 和 Application，同时不向其他层泄漏实现细节。
 */
#ifndef COMPOSITION_H
#define COMPOSITION_H

#include "firmware/image_source.h"
#include "firmware/mcu_programmer.h"
#include "firmware/status.h"

struct secondary_mcu_update_service;

/**
 * @brief 将具体 Platform 与 BSP 实现绑定到 Application Service。
 *
 * 本函数配置 Logger，构造 Storage、Hash、Package、Update、Validation、XIP、
 * Secondary MCU Programmer、Reset 和 Jump 对象，然后配置 Application。所有
 * 绑定对象均为静态生命周期，由 Composition Root 持有。
 *
 * 初始化不是事务性的：失败可能使私有 Provider 已经初始化，但依赖图永远不会
 * 被报告为就绪。此类失败对当前 Boot 是终态；调用者必须 Fail-closed，不能重试
 * 或使用不完整的依赖图启动 Application。
 *
 * @pre Platform_Init() 和 BSP_Init() 已成功完成。
 * @pre 当前 Boot 中尚未调用过本函数。
 *
 * @return 完整依赖图配置成功时返回 FIRMWARE_STATUS_OK。
 * @return 重复调用、前置条件不满足或具体依赖状态无效时返回
 *         FIRMWARE_STATUS_INVALID_STATE。
 * @return 其他依赖失败时返回对应的依赖错误状态。
 */
firmware_status_t Composition_Init(void);

/**
 * @brief 查询依赖图是否已初始化。
 *
 * @return 仅在 Composition_Init() 完成 Application 配置后返回非零；初始化前
 *         或任意一次失败后返回零。
 */
int Composition_IsInitialized(void);

/**
 * @brief Return the prepared secondary-MCU update service.
 *
 * The returned object is owned by Composition and remains valid for the
 * lifetime of the firmware.  It is NULL until the complete dependency graph
 * has been published.  The caller still supplies the image selection and
 * target layout to SecondaryMcuUpdateService_Start().
 */
struct secondary_mcu_update_service *Composition_GetSecondaryMcuUpdateService(void);

/** Return the random-access source bridge for the currently open package file. */
const firmware_image_source_t *Composition_GetSecondaryMcuImageSource(void);

/** Return the ROM programmer interface owned by Composition. */
const mcu_programmer_t *Composition_GetSecondaryMcuProgrammer(void);

#endif
