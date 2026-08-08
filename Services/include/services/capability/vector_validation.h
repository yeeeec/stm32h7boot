/**
 * @file vector_validation.h
 * @brief Cortex-M7 初始 MSP 与 Reset Handler 的验证策略。
 */
#ifndef SERVICES_VECTOR_VALIDATION_H
#define SERVICES_VECTOR_VALIDATION_H

#include <stdint.h>

#include "firmware/status.h"
#include "services/common/boot_types.h"

/** 调用者允许 Application 初始栈指针落入的一段 SRAM 区间。 */
typedef struct
{
    /** 区间起始地址，包含该地址。 */
    uint32_t start_address;
    /** 区间长度，单位为字节。 */
    uint32_t size;
} memory_region_t;

/** 从 Application 向量表前两个 word 解码出的启动信息。 */
typedef struct
{
    /** Application 复位后应装载的主栈指针。 */
    uint32_t initial_msp;
    /** Application Reset_Handler 的 Thumb 地址。 */
    uint32_t reset_handler;
} vector_table_values_t;

/**
 * 对照选定的 APP 镜像和允许 SRAM 区间验证向量值。
 *
 * Reset Handler 必须是落在镜像实际长度内的 Thumb 地址；初始 MSP 必须 8 字节
 * 对齐，并落在调用者提供的任一 SRAM 区间内。
 *
 * @param[in] vectors 从向量表读取的两个启动值。
 * @param[in] app_region APP 的固定分区描述。
 * @param[in] app_image_size 当前镜像实际长度，而非分区最大容量。
 * @param[in] sram_regions 允许使用的 SRAM 区间数组。
 * @param[in] sram_region_count 数组元素数。
 * @return FIRMWARE_STATUS_OK 表示可安全尝试跳转。
 */
firmware_status_t VectorValidation_Validate(
    const vector_table_values_t *vectors,
    const boot_region_t *app_region,
    uint32_t app_image_size,
    const memory_region_t *sram_regions,
    uint32_t sram_region_count);

#endif
