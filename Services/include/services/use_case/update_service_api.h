/**
 * @file update_service_api.h
 * @brief 增量发布包准备与固定 Runtime 安装能力 API。
 *
 * Update Service 拥有 Manifest 读取、源/目标哈希、APP/GUI 擦写及候选 Active
 * Record 生成；Application 只驱动生命周期并决定策略、提交与复位。
 */
#ifndef SERVICES_UPDATE_SERVICE_API_H
#define SERVICES_UPDATE_SERVICE_API_H

#include "services/common/manifest_types.h"
#include "services/common/service_result.h"
#include "services/common/update_request_types.h"

struct update_service;

/**
 * @brief 从已挂载发布源读取、解析并绑定固定 Manifest。
 *
 * @param[in,out] service 已初始化且空闲的更新服务。
 * @param[in] request 已严格校验的受信升级请求。
 * @return FIRMWARE_STATUS_OK 后，调用者必须调用 Process 直至终态。
 */
firmware_status_t UpdateService_PrepareStart(struct update_service *service,
                                             const update_request_t *request);

/**
 * @brief 将请求选择的 APP/GUI 组件安装到固定 Runtime 区域。
 *
 * 仅当 Prepare 成功且版本策略已由 Application 接受时调用；安装过程可能擦除
 * 选中 Runtime，调用者须依据 RuntimeMayBeModified 决定后续恢复策略。
 */
firmware_status_t UpdateService_InstallStart(struct update_service *service);

/**
 * @brief 以当前 Active Record 为基底启动一次可选组件安装。
 *
 * 单独更新 APP 或 GUI 时 current_record 必须非 NULL，以便保留未选组件元数据；
 * APP+GUI 首次安装允许传入 NULL。
 */
firmware_status_t UpdateService_InstallStartWithRecord(struct update_service *service,
                                                       const boot_active_record_t *current_record);

/** @brief 推进一步有界的读取、哈希、Flash 异步轮询或状态转换。 */
void UpdateService_Process(struct update_service *service);

/**
 * @brief 在首个 APP 擦除开始前请求取消更新；已修改 Runtime 或进入失败清理后不允许取消。
 *
 * 取消会先关闭已打开的发布文件；若安装已开始，还会确认 QSPI 回到 indirect
 * 模式。返回 FIRMWARE_STATUS_OK 表示请求已被接受，调用者必须继续调用 Process
 * 直到状态变为 CANCELLED 或 FAILED；没有待清理资源时会立即完成。
 */
firmware_status_t UpdateService_Cancel(struct update_service *service);

/** @brief 返回更新生命周期状态；NULL 服务视为失败。 */
service_run_state_t UpdateService_GetState(const struct update_service *service);

/** @brief 返回最近一次准备或安装尝试的结果快照。 */
const service_result_t *UpdateService_GetResult(const struct update_service *service);

/** @brief 返回已准备的 Manifest；Prepare 未成功前返回 NULL。 */
const validated_manifest_t *UpdateService_GetManifest(const struct update_service *service);

/** @brief 安装成功后返回尚未提交到 EEPROM 的候选 Active Record。 */
const boot_active_record_t *UpdateService_GetCandidate(const struct update_service *service);

/**
 * 返回本次安装是否可能已经修改 Runtime。
 * 首次 APP 或 GUI 擦除开始时该值变为真，并保持到下次 Prepare 重新初始化为止。
 */
int UpdateService_RuntimeMayBeModified(const struct update_service *service);

#endif
