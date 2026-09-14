/**
 * @file secondary_mcu_update_service_api.h
 * @brief 外部 MCU 固件安装服务的生命周期 API。
 *
 * Service 只处理一次已经选定的镜像和目标布局。它不解析 Manifest、不决定
 * 版本策略，也不猜测目标芯片的 Flash 地址；这些信息由上层在 Start 时注入。
 */
#ifndef SERVICES_SECONDARY_MCU_UPDATE_SERVICE_API_H
#define SERVICES_SECONDARY_MCU_UPDATE_SERVICE_API_H

#include <stdint.h>

#include "firmware/hash.h"
#include "services/common/service_result.h"

struct secondary_mcu_update_service;

/** 一次外部 MCU 镜像安装所需的目标布局。 */
typedef struct
{
    /** 目标 MCU Flash 的绝对起始地址。 */
    uint32_t target_address;
    /** 目标区域容量；用于拒绝镜像越界。 */
    uint32_t target_capacity_bytes;
    /** ROM 擦除协议使用的起始页号，而不是字节地址。 */
    uint32_t erase_page_start;
    /** 本次镜像覆盖的连续页数。 */
    uint32_t erase_page_count;
    /** 镜像必须匹配的精确字节数。 */
    uint32_t image_size_bytes;
    /** 源镜像必须匹配的 SHA-256 摘要。 */
    uint8_t sha256[FIRMWARE_SHA256_DIGEST_SIZE];
} secondary_mcu_update_request_t;

/**
 * @brief 启动一次外部 MCU 安装。
 *
 * 返回成功只表示请求已接受；调用者必须持续调用 Process()，直到状态变为
 * SUCCEEDED、FAILED 或 CANCELLED。Service 会在首次擦除前完成 Source 尺寸检查、
 * 目标 ROM 能力检查和地址范围检查。
 */
firmware_status_t SecondaryMcuUpdateService_Start(struct secondary_mcu_update_service *service,
                                                  const secondary_mcu_update_request_t *request);

/** 推进一步有界的 Source 读取、ROM 命令、回读校验或状态转换。 */
void SecondaryMcuUpdateService_Process(struct secondary_mcu_update_service *service);

/**
 * @brief 请求取消安装。
 *
 * 目标尚未擦除或写入时允许取消。目标已经可能改变后拒绝取消，调用者应继续
 * Process() 让失败清理完成，以免把半成品误当成可启动镜像。
 */
firmware_status_t SecondaryMcuUpdateService_Cancel(struct secondary_mcu_update_service *service);

/** 返回服务生命周期状态；NULL 服务按 FAILED 处理。 */
service_run_state_t
SecondaryMcuUpdateService_GetState(const struct secondary_mcu_update_service *service);

/** 返回最近一次操作结果；结果对象由 Service 持有。 */
const service_result_t *
SecondaryMcuUpdateService_GetResult(const struct secondary_mcu_update_service *service);

/** 返回目标是否已经可能被擦写；失败恢复策略据此决定是否必须复位。 */
int SecondaryMcuUpdateService_TargetMayBeModified(
    const struct secondary_mcu_update_service *service);

#endif
