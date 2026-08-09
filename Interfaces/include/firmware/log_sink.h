/**
 * @file log_sink.h
 * @brief Firmware Logger 使用的同步字节输出端。
 */
#ifndef FIRMWARE_LOG_SINK_H
#define FIRMWARE_LOG_SINK_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

/**
 * 写入一条完整 Log Record。Sink 必须在返回前消费 Buffer，不得保留该指针。
 */
typedef firmware_status_t (*log_sink_write_fn)(
    void *context,
    const uint8_t *data,
    size_t size);

/**
 * @brief Logger 输出 Contract。
 *
 * Logging 是否 Best-effort 由调用者策略决定；Sink 通过返回值报告传输失败，
 * 且不持有调用者数据。
 */
typedef struct
{
    void *context;
    log_sink_write_fn write;
} log_sink_t;

#endif
