/**
 * @file launch_service_api.h
 * @brief 经验证的 XIP 配置与最终 Application 交接 API。
 */
#ifndef SERVICES_LAUNCH_SERVICE_API_H
#define SERVICES_LAUNCH_SERVICE_API_H

#include "services/common/boot_control_types.h"
#include "services/common/service_result.h"

struct launch_service;

/**
 * @brief 校验活动 Runtime 的向量表、进入 XIP 模式并跳转。
 *
 * @param[in,out] service 已初始化的 Launch Service。
 * @param[in] active_record 已选择且已校验的 Boot Control Active Record。
 *
 * @return 仅在失败时返回；成功的 Application 交接不会返回。
 */
firmware_status_t LaunchService_Execute(struct launch_service *service,
                                        const boot_active_record_t *active_record);

/** @brief 返回最近一次启动尝试的结果快照。 */
const service_result_t *LaunchService_GetResult(const struct launch_service *service);

#endif
