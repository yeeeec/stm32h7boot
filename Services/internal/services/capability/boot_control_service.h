/**
 * @file boot_control_service.h
 * @brief 供 Composition 装配的 Boot Control 服务对象及初始化契约。
 *
 * 该对象管理 EEPROM 中两个 Active Record 槽位的读取、选择和增量提交状态。
 * 所有存储缓冲区均内嵌于对象，避免在启动关键路径中进行动态内存分配。
 */
#ifndef SERVICES_BOOT_CONTROL_SERVICE_INTERNAL_H
#define SERVICES_BOOT_CONTROL_SERVICE_INTERNAL_H

#include <stdint.h>

#include "firmware/boot_control_store.h"
#include "firmware/checksum.h"
#include "services/capability/boot_control_service_api.h"

/** Active Record 的固定槽位大小，也是本服务内部缓冲区的容量上限。 */
#define BOOT_CONTROL_MAX_RECORD_SIZE 256U

/** Boot Control 服务初始化时由 Composition 注入的底层能力。 */
typedef struct
{
    /** 提供 EEPROM A/B 槽位读写和就绪轮询的持久化存储端口。 */
    const boot_control_store_t *store;
    /** 用于校验 Active Record 内容完整性的 CRC 计算端口。 */
    const checksum_t *checksum;
} boot_control_service_dependencies_t;

/**
 * 增量提交的细粒度阶段。
 *
 * 每次 Process 调用只发起一次页写、就绪轮询、读取或纯状态推进，因而调用方可
 * 在主循环中非阻塞地驱动 EEPROM 写入过程。
 */
typedef enum
{
    /** 当前没有进行中的提交。 */
    BOOT_CONTROL_STAGE_IDLE = 0,
    /** 先写入无效 marker，使掉电时旧槽位仍保持为唯一有效记录。 */
    BOOT_CONTROL_STAGE_INVALIDATE_MARKER,
    /** 等待无效 marker 页写完成。 */
    BOOT_CONTROL_STAGE_WAIT_INVALIDATE,
    /** 按 EEPROM 页边界写入 marker 之前的记录正文。 */
    BOOT_CONTROL_STAGE_WRITE_BODY,
    /** 等待一页记录正文写入完成。 */
    BOOT_CONTROL_STAGE_WAIT_BODY,
    /** 回读并比较正文，确认提交 marker 前的数据完整性。 */
    BOOT_CONTROL_STAGE_READ_BODY,
    /** 最后写入提交 marker，使新记录原子生效。 */
    BOOT_CONTROL_STAGE_WRITE_MARKER,
    /** 等待提交 marker 页写完成。 */
    BOOT_CONTROL_STAGE_WAIT_MARKER,
    /** 回读完整记录并执行格式与 CRC 的最终验证。 */
    BOOT_CONTROL_STAGE_VERIFY_FINAL
} boot_control_stage_t;

/**
 * Boot Control 服务的运行时实例。
 *
 * 仅由 Composition 静态创建；字段由本实现维护，调用方通过公开 API 查询状态和
 * 结果，而不应直接修改这些成员。
 */
typedef struct boot_control_service
{
    /** 注入的 EEPROM 存储端口，在初始化后保持只读引用。 */
    const boot_control_store_t *store;
    /** 注入的 CRC 端口，在初始化后保持只读引用。 */
    const checksum_t *checksum;
    /** 初始化时读取的 EEPROM 容量与页写几何信息。 */
    boot_control_store_info_t store_info;
    /** 提交操作的粗粒度生命周期状态。 */
    service_run_state_t state;
    /** 最近一次提交的结果、失败阶段和底层错误码。 */
    service_result_t result;
    /** 当前增量提交所处的细粒度阶段。 */
    boot_control_stage_t stage;
    /** 待写入的完整 Active Record，也作为最终提交后的期望镜像。 */
    uint8_t write_buffer[BOOT_CONTROL_MAX_RECORD_SIZE];
    /** EEPROM 回读校验使用的独立缓冲区。 */
    uint8_t verify_buffer[BOOT_CONTROL_MAX_RECORD_SIZE];
    /** 本次提交目标槽位在 EEPROM 中的起始地址。 */
    uint32_t target_address;
    /** 当前记录的实际字节数，固定为 Active Record 格式大小。 */
    uint32_t record_size;
    /** 提交 marker 在记录内的偏移，正文写入在此偏移前停止。 */
    uint32_t marker_offset;
    /** 已成功写入并确认的正文长度。 */
    uint32_t write_offset;
    /** 最近一次发起的页写长度，待就绪后计入 write_offset。 */
    uint32_t last_write_size;
    /** 非零表示依赖、存储几何和初始状态均已完成校验。 */
    int initialized;
} boot_control_service_t;

/**
 * 校验依赖与 EEPROM 几何信息，并初始化 Boot Control 运行时状态。
 *
 * @param service 由 Composition 静态分配的服务对象，必须尚未初始化。
 * @param dependencies 已注入的 EEPROM 存储端口与 CRC 端口。
 * @return 成功时返回 FIRMWARE_STATUS_OK；参数、端口或存储几何不满足要求时返回错误。
 */
firmware_status_t BootControlService_Init(
    boot_control_service_t *service,
    const boot_control_service_dependencies_t *dependencies);

#endif
