/**
 * @file checksum.h
 * @brief Firmware Service 所需的增量 Checksum 能力。
 */
#ifndef FIRMWARE_CHECKSUM_H
#define FIRMWARE_CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

/** 开始新的计算并丢弃 Provider 之前的状态。 */
typedef firmware_status_t (*checksum_reset_fn)(void *context);
/** 同步消费字节，不保留调用者的 Buffer。 */
typedef firmware_status_t (*checksum_update_fn)(void *context, const void *data, size_t size);
/** 返回最近一次 reset() 后已接受全部字节的 Checksum 值。 */
typedef firmware_status_t (*checksum_get_value_fn)(void *context, uint32_t *value);

/**
 * @brief 增量 Checksum 接口。
 *
 * Provider 持有可变计算 context。调用者必须串行使用同一实例，并在重新使用
 * 前调用 reset()。
 */
typedef struct
{
    void *context;
    checksum_reset_fn reset;
    checksum_update_fn update;
    checksum_get_value_fn get_value;
} checksum_t;

#endif
