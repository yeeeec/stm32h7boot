/**
 * @file async_block_device.h
 * @brief 面向长时运行 Service 的有界 Block Device 访问 Contract。
 */
#ifndef FIRMWARE_ASYNC_BLOCK_DEVICE_H
#define FIRMWARE_ASYNC_BLOCK_DEVICE_H

#include <stdint.h>

#include "firmware/status.h"

/**
 * @brief 异步设备的不变几何参数和传输限制。
 *
 * 传给操作的地址和大小必须位于 capacity_bytes 范围内，并满足 program_size
 * 与 erase_size 的对齐约束。
 */
typedef struct
{
    uint32_t capacity_bytes;
/* 单次 program 调用可接受的最大字节数；一次调用不得跨越该边界。 */
    uint32_t program_size;
    uint32_t erase_size;
} async_block_device_info_t;

/** 当前已提交异步操作所报告的状态。 */
typedef enum
{
    ASYNC_BLOCK_DEVICE_OPERATION_IDLE = 0,
    ASYNC_BLOCK_DEVICE_OPERATION_BUSY,
    ASYNC_BLOCK_DEVICE_OPERATION_SUCCEEDED,
    ASYNC_BLOCK_DEVICE_OPERATION_FAILED
} async_block_device_operation_state_t;

/**
 * @brief 最近一次提交操作的结果。
 *
 * state 是轮询后观察到的快照。state 进入终态后，status 才是权威结果；
 * 不能仅因 start Callback 返回成功就提交逻辑偏移。
 */
typedef struct
{
    async_block_device_operation_state_t state;
    firmware_status_t status;
} async_block_device_operation_result_t;

typedef firmware_status_t (*async_block_device_get_info_fn)(
    void *context,
    async_block_device_info_t *info);
typedef firmware_status_t (*async_block_device_read_fn)(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size);
typedef firmware_status_t (*async_block_device_program_start_fn)(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t size);
typedef firmware_status_t (*async_block_device_erase_start_fn)(
    void *context,
    uint32_t address,
    uint32_t size);
typedef firmware_status_t (*async_block_device_poll_fn)(void *context);
typedef firmware_status_t (*async_block_device_get_operation_result_fn)(
    void *context,
    async_block_device_operation_result_t *result);
typedef firmware_status_t (*async_block_device_cancel_fn)(void *context);

/**
 * @brief 有界异步 Block Device Contract。
 *
 * read() 是同步操作。program_start() 和 erase_start() 提交一次操作，
 * poll() 推进该操作，get_operation_result() 观察其状态。操作处于活动状态
 * 时不允许其他操作。每次 poll 调用必须有界，以便 Service 继续刷新 watchdog。
 * cancel Callback 为 NULL 明确表示活动操作不可中止。
 */
typedef struct
{
    void *context;
    async_block_device_get_info_fn get_info;
    async_block_device_read_fn read;
    async_block_device_program_start_fn program_start;
    async_block_device_erase_start_fn erase_start;
    async_block_device_poll_fn poll;
    async_block_device_get_operation_result_fn get_operation_result;
    async_block_device_cancel_fn cancel;
} async_block_device_t;

#endif
