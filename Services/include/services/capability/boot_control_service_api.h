/**
 * @file boot_control_service_api.h
 * @brief Boot Control A/B 记录选择和增量提交 API。
 *
 * 此 API 屏蔽 EEPROM 页写、CRC 及提交标记细节。调用者负责决定何时提交候选
 * 记录，并在服务运行期间持续调用 Process。
 */
#ifndef SERVICES_BOOT_CONTROL_SERVICE_API_H
#define SERVICES_BOOT_CONTROL_SERVICE_API_H

#include "firmware/status.h"
#include "services/common/boot_control_types.h"
#include "services/common/service_result.h"

struct boot_control_service;

/**
 * @brief 从 A/B 存储中读取序号最新且校验有效的 Active Record。
 *
 * @param[in] service 已初始化的 Boot Control 服务。
 * @param[out] record 接收选中的有效记录。
 * @return 成功时为 FIRMWARE_STATUS_OK；未找到有效记录或底层读取失败时返回对应状态。
 */
firmware_status_t BootControlService_LoadActive(
    struct boot_control_service *service,
    boot_active_record_t *record);

/**
 * 启动一个 Active Record 原子提交。
 *
 * 服务会分配下一个 sequence，调用者必须持续调用
 * BootControlService_Process，直到服务进入终态。
 *
 * @param[in] service 已初始化且未在运行的服务。
 * @param[in] record 候选记录；服务会复制其内容并补齐 sequence。
 * @return 可启动时为 FIRMWARE_STATUS_OK；否则为参数或状态错误。
 */
firmware_status_t BootControlService_CommitActiveStart(
    struct boot_control_service *service,
    const boot_active_record_t *record);

/**
 * @brief 推进一步 EEPROM 页写、就绪轮询、回读校验或内部状态转换。
 *
 * 单次调用不会执行完整提交，适合由裸机主循环以增量方式驱动。
 */
void BootControlService_Process(struct boot_control_service *service);

/** @brief 返回当前提交生命周期状态；NULL 服务视为失败。 */
service_run_state_t BootControlService_GetState(
    const struct boot_control_service *service);

/** @brief 返回最近一次提交的结果快照；返回指针由服务持有。 */
const service_result_t *BootControlService_GetResult(
    const struct boot_control_service *service);

#endif
