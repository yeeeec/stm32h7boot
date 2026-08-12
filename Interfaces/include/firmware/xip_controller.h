/**
 * @file xip_controller.h
 * @brief 外部 Flash memory-mapped 执行控制 Contract。
 */
#ifndef FIRMWARE_XIP_CONTROLLER_H
#define FIRMWARE_XIP_CONTROLLER_H

#include <stdint.h>

#include "firmware/status.h"

/** 外部 Flash 就绪后进入只读 memory-mapped 模式。 */
typedef firmware_status_t (*xip_controller_enter_fn)(void *context);
/** 在 erase 或 program 前请求切换到 indirect mode。 */
typedef firmware_status_t (*xip_controller_exit_fn)(void *context);
/** 查询硬件后置条件；过期的软件 Cache 不能作为依据。 */
typedef firmware_status_t (*xip_controller_is_mapped_fn)(void *context, int *mapped);
/** 外部 Flash 写入后使 mapped 范围覆盖的 Cache 状态失效。 */
typedef firmware_status_t (*xip_controller_invalidate_fn)(void *context, uint32_t mapped_address,
                                                          uint32_t size);

/**
 * @brief 外部 Flash memory-mapped Window 的 Exclusive Controller。
 *
 * Runtime 修改要求 is_memory_mapped() == 0 的肯定后置条件；在所有 HAL 路径
 * 上，enter/exit 返回值本身都不是权威依据。
 */
typedef struct
{
    void *context;
    xip_controller_enter_fn enter_memory_mapped_read;
    xip_controller_exit_fn exit_memory_mapped;
    xip_controller_is_mapped_fn is_memory_mapped;
    xip_controller_invalidate_fn invalidate_mapped_cache;
} xip_controller_t;

#endif
