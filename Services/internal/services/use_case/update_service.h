/**
 * @file update_service.h
 * @brief 可选 APP/GUI Runtime 安装器的内部状态和依赖。
 *
 * 该对象由 Composition 静态分配。它持有一次 Prepare/Install 所有中间状态，
 * 每次 Process 最多推进一个有界 I/O、哈希、异步轮询或状态转换。
 */
#ifndef SERVICES_UPDATE_SERVICE_INTERNAL_H
#define SERVICES_UPDATE_SERVICE_INTERNAL_H

#include <stdint.h>

#include "firmware/async_block_device.h"
#include "firmware/hash.h"
#include "firmware/package_source.h"
#include "firmware/system_clock.h"
#include "firmware/xip_controller.h"
#include "services/capability/manifest_service.h"
#include "services/capability/update_request_service.h"
#include "services/common/boot_control_types.h"
#include "services/common/runtime_layout.h"
#include "services/use_case/update_service_api.h"

/** Manifest 专用缓冲区的最小容量，必须覆盖最大生产 Manifest。 */
#define UPDATE_SERVICE_MANIFEST_MAX_SIZE 16384U
/** 安装流式 I/O 缓冲区的最小容量；同时必须不小于 Flash program_size。 */
#define UPDATE_SERVICE_IO_BUFFER_MIN_SIZE 4096U
/** QSPI Abort 或状态确认失败时，安装前最多尝试退出 XIP 的次数。 */
#define UPDATE_SERVICE_XIP_EXIT_RETRY_LIMIT 3U
/** 发布文件关闭失败后，服务在进入终态前最多重试 close 的次数。 */
#define UPDATE_SERVICE_CLOSE_RETRY_LIMIT 3U
/** 安装进度日志的最小输出间隔，避免同步串口阻塞擦写状态机。 */
#define UPDATE_SERVICE_PROGRESS_LOG_INTERVAL_MS 1000U
/** 两次安装进度日志之间要求的最小百分比变化。 */
#define UPDATE_SERVICE_PROGRESS_LOG_STEP_PERCENT 1U

/** Update Service 的外部依赖及调用者持有的两个工作缓冲区。 */
typedef struct
{
    /** 已挂载发布卷上的固定文件访问接口。 */
    const package_source_t *package_source;
    /** 严格 Manifest 解析服务。 */
    manifest_service_t *manifest_service;
    /** trusted request 解析和绑定服务。 */
    update_request_service_t *update_request_service;
    /** 用于源和目标 SHA-256 的哈希 Provider。 */
    const hash_provider_t *hash;
    /** 为安装进度日志限流提供的单调毫秒时钟。 */
    const system_clock_t *clock;
    /** 以 Flash 偏移操作 APP/GUI 分区的异步块设备。 */
    const async_block_device_t *storage;
    /** 安装前确保外部 Flash 退出 memory-mapped 模式的独占控制接口。 */
    const xip_controller_t *xip_controller;
    /** 固定 Runtime 地址与容量合同。 */
    const boot_runtime_layout_t *runtime_layout;
    /** 接收完整 Manifest 原文的持久缓冲区。 */
    uint8_t *manifest_buffer;
    /** manifest_buffer 的容量。 */
    uint32_t manifest_buffer_size;
    /** 流式读取、哈希、写入和回读共用的缓冲区。 */
    uint8_t *io_buffer;
    /** io_buffer 的容量。 */
    uint32_t io_buffer_size;
} update_service_dependencies_t;

/** Update Service 的内部细粒度阶段；Application 只观察 service_run_state_t。 */
typedef enum
{
    /** 空闲，尚未开始 Prepare。 */
    UPDATE_STAGE_IDLE = 0,
    /** 打开 Manifest 固定文件。 */
    UPDATE_STAGE_PREPARE_MANIFEST_OPEN,
    /** 读取并检查 Manifest 文件长度。 */
    UPDATE_STAGE_PREPARE_MANIFEST_SIZE,
    /** 将 Manifest 分块读入 manifest_buffer。 */
    UPDATE_STAGE_PREPARE_MANIFEST_READ,
    /** 关闭 Manifest 文件。 */
    UPDATE_STAGE_PREPARE_MANIFEST_CLOSE,
    /** 解析 Manifest 并验证 trusted request 绑定。 */
    UPDATE_STAGE_PREPARE_MANIFEST_PARSE,
    /** Prepare 成功，等待 Application 接受版本策略后调用 InstallStart。 */
    UPDATE_STAGE_PREPARED,
    /** 查询 QSPI 是否仍处于 memory-mapped 模式。 */
    UPDATE_STAGE_XIP_CHECK_INDIRECT,
    /** 请求 QSPI 退出 memory-mapped 模式并回到 indirect 模式。 */
    UPDATE_STAGE_XIP_EXIT,
    /** 重新查询 QSPI 状态，确认退出操作的真实后置条件。 */
    UPDATE_STAGE_XIP_VERIFY_INDIRECT,
    /** 打开 APP 源文件以进行安装前完整性扫描。 */
    UPDATE_STAGE_SOURCE_APP_OPEN,
    /** 检查 APP 源文件精确长度。 */
    UPDATE_STAGE_SOURCE_APP_SIZE,
    /** 分块计算 APP 源文件哈希。 */
    UPDATE_STAGE_SOURCE_APP_HASH,
    /** 比较 APP 源文件哈希并关闭文件。 */
    UPDATE_STAGE_SOURCE_APP_VERIFY,
    /** 打开 GUI 源文件以进行安装前完整性扫描。 */
    UPDATE_STAGE_SOURCE_GUI_OPEN,
    /** 检查 GUI 源文件精确长度。 */
    UPDATE_STAGE_SOURCE_GUI_SIZE,
    /** 分块计算 GUI 源文件哈希。 */
    UPDATE_STAGE_SOURCE_GUI_HASH,
    /** 比较 GUI 源文件哈希并关闭文件。 */
    UPDATE_STAGE_SOURCE_GUI_VERIFY,
    /** 启动一页 APP 分区擦除。 */
    UPDATE_STAGE_APP_ERASE,
    /** 轮询当前 APP 擦除操作。 */
    UPDATE_STAGE_APP_ERASE_POLL,
    /** 打开 APP 源文件，准备流式写入。 */
    UPDATE_STAGE_APP_PROGRAM_OPEN,
    /** 读取下一段 APP 源数据。 */
    UPDATE_STAGE_APP_PROGRAM_READ,
    /** 启动当前 APP 页编程操作。 */
    UPDATE_STAGE_APP_PROGRAM_START,
    /** 轮询当前 APP 页编程操作。 */
    UPDATE_STAGE_APP_PROGRAM_POLL,
    /** 完成并验证 APP 写入期间重新计算的源哈希。 */
    UPDATE_STAGE_APP_PROGRAM_HASH,
    /** 分块回读 APP 目标区。 */
    UPDATE_STAGE_APP_TARGET_READ,
    /** 完成并验证 APP 目标区哈希。 */
    UPDATE_STAGE_APP_TARGET_HASH,
    /** 启动一页 GUI 分区擦除。 */
    UPDATE_STAGE_GUI_ERASE,
    /** 轮询当前 GUI 擦除操作。 */
    UPDATE_STAGE_GUI_ERASE_POLL,
    /** 打开 GUI 源文件，准备流式写入。 */
    UPDATE_STAGE_GUI_PROGRAM_OPEN,
    /** 读取下一段 GUI 源数据。 */
    UPDATE_STAGE_GUI_PROGRAM_READ,
    /** 启动当前 GUI 页编程操作。 */
    UPDATE_STAGE_GUI_PROGRAM_START,
    /** 轮询当前 GUI 页编程操作。 */
    UPDATE_STAGE_GUI_PROGRAM_POLL,
    /** 完成并验证 GUI 写入期间重新计算的源哈希。 */
    UPDATE_STAGE_GUI_PROGRAM_HASH,
    /** 分块回读 GUI 目标区。 */
    UPDATE_STAGE_GUI_TARGET_READ,
    /** 完成并验证 GUI 目标区哈希。 */
    UPDATE_STAGE_GUI_TARGET_HASH,
    /** 由已校验的 Manifest 生成未提交的候选 Active Record。 */
    UPDATE_STAGE_BUILD_RECORD_CANDIDATE,
    /** 某个主操作失败后，重试关闭仍被服务持有的发布源文件。 */
    UPDATE_STAGE_FAILURE_CLOSE,
    /** 取消请求先关闭仍被服务持有的发布源文件。 */
    UPDATE_STAGE_CANCEL_CLOSE,
    /** 取消安装时查询 QSPI 是否仍处于 memory-mapped 模式。 */
    UPDATE_STAGE_CANCEL_XIP_CHECK_INDIRECT,
    /** 取消安装时请求 QSPI 回到 indirect 模式。 */
    UPDATE_STAGE_CANCEL_XIP_EXIT,
    /** 取消安装时确认 QSPI 已真实退出 memory-mapped 模式。 */
    UPDATE_STAGE_CANCEL_XIP_VERIFY_INDIRECT
} update_stage_t;

/** Update Service 在一次 Prepare/Install 生命周期中持有的所有状态。 */
typedef struct update_service
{
    /** 发布源接口。 */
    const package_source_t *package_source;
    /** Manifest 服务实例。 */
    manifest_service_t *manifest_service;
    /** trusted request 服务实例。 */
    update_request_service_t *update_request_service;
    /** 哈希 Provider。 */
    const hash_provider_t *hash;
    /** 单调毫秒时钟，用于限制安装进度日志频率。 */
    const system_clock_t *clock;
    /** Runtime 外部 Flash 接口。 */
    const async_block_device_t *storage;
    /** QSPI XIP 模式控制接口；更新期间只使用其退出与状态查询能力。 */
    const xip_controller_t *xip_controller;
    /** 固定分区布局。 */
    const boot_runtime_layout_t *runtime_layout;
    /** Init 时读取并冻结的 Flash 几何信息。 */
    async_block_device_info_t storage_info;
    /** 完整 Manifest 工作缓冲区。 */
    uint8_t *manifest_buffer;
    /** Manifest 缓冲区容量。 */
    uint32_t manifest_buffer_size;
    /** 安装/校验流式工作缓冲区。 */
    uint8_t *io_buffer;
    /** 流式工作缓冲区容量。 */
    uint32_t io_buffer_size;

    /** 本次安装绑定的 trusted request 副本。 */
    update_request_t request;
    /** Prepare 成功后的 Manifest 副本。 */
    validated_manifest_t manifest;
    /** Manifest 已完成语法、签名/摘要及 trusted request 绑定校验。 */
    int manifest_ready;
    /** Install 成功后等待 EEPROM 提交的候选记录。 */
    boot_active_record_t candidate_record;
    /** 部分更新开始时复制的当前 Active Record。 */
    boot_active_record_t base_record;
    /** base_record 是否由 Application 提供。 */
    int base_record_valid;
    /** 最近计算的源文件 SHA-256。 */
    uint8_t source_digest[FIRMWARE_SHA256_DIGEST_SIZE];
    /** 最近计算的目标 Runtime SHA-256。 */
    uint8_t target_digest[FIRMWARE_SHA256_DIGEST_SIZE];

    /** 对 Application 可见的生命周期状态。 */
    service_run_state_t state;
    /** 最近一次操作的结果快照。 */
    service_result_t result;
    /** 当前细粒度内部阶段。 */
    update_stage_t stage;
    /** 失败时保留的通用状态码。 */
    firmware_status_t failure_status;
    /** 失败时保留的业务错误类别。 */
    boot_error_t failure_error;
    /** 首次失败所在阶段，防止清理阶段覆盖根因诊断位置。 */
    update_stage_t failure_stage;
    /** 已读取的 Manifest 文件长度。 */
    uint32_t manifest_size;
    /** Manifest 分块读取偏移。 */
    uint32_t manifest_offset;
    /** 安装前源哈希扫描偏移。 */
    uint32_t source_offset;
    /** Runtime 回读哈希偏移。 */
    uint32_t target_offset;
    /** 当前分区擦除偏移。 */
    uint32_t erase_offset;
    /** 当前 payload 编程偏移。 */
    uint32_t program_offset;
    /** 已交给异步 Flash 编程但尚未确认的字节数。 */
    uint32_t pending_program_size;
    /** 当前打开源文件的已验证总长度。 */
    uint32_t active_source_size;
    /** 当前处理的 APP 或 GUI Manifest 组件。 */
    const manifest_app_component_t *active_component;
    /** 当前组件的 Flash 起始偏移。 */
    uint32_t active_runtime_offset;
    /** 当前组件分区最大容量。 */
    uint32_t active_runtime_size;
    /** 已执行的 XIP 退出尝试次数。 */
    uint32_t xip_exit_attempts;
    /** 失败清理期间已执行的发布文件关闭重试次数。 */
    uint32_t close_retry_count;
    /** 源文件实际打开状态；仅在 close 成功后清零。 */
    int source_file_open;
    /** candidate_record 是否已构建完成。 */
    int candidate_ready;
    /** 首次 APP 或 GUI 擦除开始后置位，表示 Runtime 可能部分改变。 */
    int runtime_may_be_modified;
    /** 当前 XIP 间接模式确认成功后是否应直接进入首次 APP 擦除。 */
    int xip_check_before_runtime_mutation;
    /** 已接受的取消请求是否还必须确认 QSPI 已回到 indirect 模式。 */
    int cancel_requires_indirect;
    /** 上一次输出的安装总进度百分比。 */
    uint32_t progress_last_percent;
    /** 上一次进度日志时间；安装开始时保存限流基准。 */
    uint32_t progress_last_log_ms;
    /** InstallStart 是否已经启动本次进度跟踪。 */
    int progress_tracking_started;
    /** 上一次已输出日志的内部阶段。 */
    update_stage_t logged_stage;
    /** 是否已经输出当前阶段入口日志。 */
    int stage_logged;
    /** Init 成功标志。 */
    int initialized;
} update_service_t;

/**
 * @brief 校验依赖、Flash 几何和缓冲区后初始化 Update Service。
 *
 * @param[out] service Composition 静态分配的服务对象。
 * @param[in] dependencies 发布源、服务、外部 Flash 和缓冲区。
 * @return FIRMWARE_STATUS_OK 或参数、分区、缓冲区错误。
 */
firmware_status_t UpdateService_Init(
    update_service_t *service,
    const update_service_dependencies_t *dependencies);

#endif
