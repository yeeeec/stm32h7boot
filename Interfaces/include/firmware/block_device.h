/**
 * @file block_device.h
 * @brief 有界存储操作使用的同步 Block Device Contract。
 */
#ifndef FIRMWARE_BLOCK_DEVICE_H
#define FIRMWARE_BLOCK_DEVICE_H

#include <stdint.h>

#include "firmware/status.h"

/** 用于校验所有操作的不变设备几何参数。 */
typedef struct
{
    uint32_t capacity_bytes;
    uint32_t write_size;
    uint32_t erase_size;
} block_device_info_t;

typedef firmware_status_t (*block_device_get_info_fn)(
    void *context,
    block_device_info_t *info);
typedef firmware_status_t (*block_device_read_fn)(
    void *context,
    uint32_t address,
    void *data,
    uint32_t size);
typedef firmware_status_t (*block_device_program_fn)(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t size);
typedef firmware_status_t (*block_device_erase_fn)(
    void *context,
    uint32_t address,
    uint32_t size);

/**
 * @brief 同步 Block Device 接口。
 *
 * Provider 持有 context。调用者持有 data Buffer，Buffer 只在 Callback 执行
 * 期间使用。操作必须拒绝越界、未对齐或其他不支持的请求。
 */
typedef struct
{
    void *context;
    block_device_get_info_fn get_info;
    block_device_read_fn read;
    block_device_program_fn program;
    block_device_erase_fn erase;
} block_device_t;

#endif
