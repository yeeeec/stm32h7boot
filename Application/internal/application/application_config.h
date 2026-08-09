/**
 * @file application_config.h
 * @brief 面向 Composition 的 Application 依赖契约。
 *
 * 本头文件仅用于装配内部对象，不属于 Application 对外生命周期 API。
 * Application 借用所有注入对象，且不负责释放其存储。
 */
#ifndef APPLICATION_CONFIG_H
#define APPLICATION_CONFIG_H

#include "application/application.h"
#include "firmware/package_source.h"
#include "firmware/system_reset.h"
#include "firmware/update_request_store.h"
#include "services/common/boot_types.h"

struct active_validation_service;
struct boot_control_service;
struct launch_service;
struct update_service;
struct update_request_service;

/** Application 顶层策略所需的长生命周期 Service、接口和版本策略。 */
typedef struct
{
    /** 读取和原子提交 Active Record 的 Boot Control Service。 */
    struct boot_control_service *boot_control;
    /** 准备发布包并安装固定 Runtime 的 Update Service。 */
    struct update_service *update;
    /** 启动前校验当前 APP/GUI Runtime 的 Active Validation Service。 */
    struct active_validation_service *validation;
    /** 完成 XIP 配置和最终 APP 跳转的 Launch Service。 */
    struct launch_service *launch;
    /** 查询、挂载和卸载固定发布卷的只读 Source 接口。 */
    const package_source_t *package_source;
    /** 加载并清除 Trusted Request 原始文档的存储接口。 */
    const update_request_store_t *update_request_store;
    /** 严格解析 Trusted Request 并校验其 Manifest 绑定的 Service。 */
    struct update_request_service *update_request_service;
    /** 安装完成或需要恢复时请求全系统复位的平台接口。 */
    const system_reset_t *system_reset;
    /** 当前 Bootloader 版本，用于拒绝要求更高最低版本的发布包。 */
    release_version_t bootloader_version;
} application_dependencies_t;

/**
 * @brief 校验并发布 Application 使用的完整依赖图。
 *
 * 函数复制依赖结构中的指针和值，但不复制其指向的对象。所有依赖对象及其函数表
 * 必须在整个固件运行期间保持有效。
 *
 * @param[in] dependencies Composition 构造的完整依赖集合，不允许为 NULL。
 *
 * @return FIRMWARE_STATUS_OK 表示依赖已发布。
 * @return FIRMWARE_STATUS_INVALID_ARGUMENT 表示依赖、必要接口或回调缺失。
 * @return FIRMWARE_STATUS_INVALID_STATE 表示已配置或已初始化，禁止重复发布。
 */
firmware_status_t Application_Configure(
    const application_dependencies_t *dependencies);

#endif
