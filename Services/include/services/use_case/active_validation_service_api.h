/**
 * @file active_validation_service_api.h
 * @brief 对当前 Active APP/GUI Runtime 进行增量校验的生命周期 API。
 */
#ifndef SERVICES_ACTIVE_VALIDATION_SERVICE_API_H
#define SERVICES_ACTIVE_VALIDATION_SERVICE_API_H

#include "services/common/boot_control_types.h"
#include "services/common/service_result.h"

struct active_validation_service;

/**
 * @brief 启动 Active Record 所描述 APP/GUI 对的摘要与向量表校验。
 *
 * @param[in,out] service 已初始化的校验服务。
 * @param[in] active_record 已从 Boot Control 读取的候选有效记录。
 * @return FIRMWARE_STATUS_OK 后，调用者必须持续调用 Process 至终态。
 */
firmware_status_t ActiveValidationService_Start(
    struct active_validation_service *service,
    const boot_active_record_t *active_record);

/** @brief 推进一步有限长度的存储读取、哈希或状态转换。 */
void ActiveValidationService_Process(
    struct active_validation_service *service);

/** @brief 返回校验生命周期状态；NULL 服务视为失败。 */
service_run_state_t ActiveValidationService_GetState(
    const struct active_validation_service *service);

/** @brief 返回最近一次校验的结果快照；生命周期由服务对象管理。 */
const service_result_t *ActiveValidationService_GetResult(
    const struct active_validation_service *service);

#endif
