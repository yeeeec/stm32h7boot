/**
 * @file secondary_mcu_update_service.h
 * @brief 外部 MCU 安装 Service 的内部对象和依赖。
 */
#ifndef SERVICES_SECONDARY_MCU_UPDATE_SERVICE_INTERNAL_H
#define SERVICES_SECONDARY_MCU_UPDATE_SERVICE_INTERNAL_H

#include <stdint.h>

#include "firmware/hash.h"
#include "firmware/image_source.h"
#include "firmware/mcu_programmer.h"
#include "services/use_case/secondary_mcu_update_service_api.h"

/** ROM 单次写入最大值之外仍需保留的最小工作缓冲区。 */
#define SECONDARY_MCU_UPDATE_SERVICE_MIN_BUFFER_SIZE 256U

/** Composition 注入的外部 MCU 安装依赖。 */
typedef struct
{
    /** 已准备好的随机访问镜像 Source。 */
    const firmware_image_source_t *source;
    /** 已绑定 BSP/Adapter 的目标 MCU 编程器。 */
    const mcu_programmer_t *programmer;
    /** 用于安装前源镜像完整性校验的 SHA-256 Provider。 */
    const hash_provider_t *hash;
    /** 保存 Source 原文的一块工作缓冲区。 */
    uint8_t *write_buffer;
    /** 保存目标回读数据的一块工作缓冲区。 */
    uint8_t *readback_buffer;
    /** 两个缓冲区的共同容量。 */
    uint32_t buffer_size;
} secondary_mcu_update_service_dependencies_t;

/** Service 的内部细粒度阶段；不属于 Application 公共 Contract。 */
typedef enum
{
    SECONDARY_MCU_UPDATE_STAGE_IDLE = 0,
    SECONDARY_MCU_UPDATE_STAGE_SOURCE_INFO,
    SECONDARY_MCU_UPDATE_STAGE_SOURCE_HASH_RESET,
    SECONDARY_MCU_UPDATE_STAGE_SOURCE_HASH_READ,
    SECONDARY_MCU_UPDATE_STAGE_SOURCE_HASH_FINISH,
    SECONDARY_MCU_UPDATE_STAGE_BEGIN,
    SECONDARY_MCU_UPDATE_STAGE_ERASE,
    SECONDARY_MCU_UPDATE_STAGE_READ_SOURCE,
    SECONDARY_MCU_UPDATE_STAGE_WRITE,
    SECONDARY_MCU_UPDATE_STAGE_READBACK,
    SECONDARY_MCU_UPDATE_STAGE_END,
    SECONDARY_MCU_UPDATE_STAGE_ABORT
} secondary_mcu_update_stage_t;

/** 外部 MCU Service 的静态生命周期对象。 */
typedef struct secondary_mcu_update_service
{
    const firmware_image_source_t *source;
    const mcu_programmer_t *programmer;
    const hash_provider_t *hash;
    uint8_t *write_buffer;
    uint8_t *readback_buffer;
    uint32_t buffer_size;
    firmware_image_info_t source_info;
    mcu_programmer_info_t programmer_info;
    secondary_mcu_update_request_t request;
    service_run_state_t state;
    service_result_t result;
    secondary_mcu_update_stage_t stage;
    secondary_mcu_update_stage_t failure_stage;
    firmware_status_t failure_status;
    boot_error_t failure_error;
    uint32_t source_offset;
    uint8_t source_digest[FIRMWARE_SHA256_DIGEST_SIZE];
    uint32_t erase_page_offset;
    uint32_t pending_size;
    int session_active;
    int target_may_be_modified;
    int cancel_requested;
    int initialized;
} secondary_mcu_update_service_t;

/** 校验依赖并初始化外部 MCU Service。 */
firmware_status_t
SecondaryMcuUpdateService_Init(secondary_mcu_update_service_t *service,
                               const secondary_mcu_update_service_dependencies_t *dependencies);

#endif
